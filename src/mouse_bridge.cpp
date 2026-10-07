#include "detail/internal.hpp"
#include "detail/mouse_bridge.hpp"
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detail/mouse_route.hpp"
#include "detail/mouse_hook.hpp"
#ifdef GL_EXPERIMENTAL_MOUSE_PROBE
#include "detail/mouse_probe.hpp"
#endif
#include <atomic>
#include <chrono>
#include <mutex>
namespace gyrolib_detail {
struct MouseBridge {
    gl_context* owner{};
    std::mutex mutex;
    std::atomic<void*> window{};
    MouseRoute mouse_route;
    MouseHook mouse_hook;
    bool panel_open{};
#ifdef GL_EXPERIMENTAL_MOUSE_PROBE
    MouseProbe mouse_probe;
#endif
};
namespace {
uint64_t ticks(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void stop_mouse(MouseBridge* o){std::lock_guard lock(o->mutex);o->mouse_hook.gate.clear();o->mouse_route.clear();}
bool foreground(HWND window){return window&&GetForegroundWindow()==GetAncestor(window,GA_ROOT)&&!IsIconic(window);}
void apply_mouse(void* user,gl_output* output,bool safe,uint32_t view,uint32_t contacts){
    auto* o=static_cast<MouseBridge*>(user);auto* c=o->owner;
#ifdef GL_EXPERIMENTAL_MOUSE_PROBE
    o->mouse_probe.configure(c->settings_path);
#endif
    const auto values=c->device_settings(view);
    const bool attached=o->window.load()!=nullptr;
    const bool observe=attached&&c->host.focused&&c->selected;
    const bool steam_source=c->steam_input_available();
    double yaw=0,pitch=0;uint64_t detected=0;
    {std::lock_guard lock(o->mutex);const auto now=ticks()/1000000;
        o->mouse_route.consume(now,safe&&observe&&!c->panel&&!o->panel_open,view,c->selected,
            (contacts&(GL_RIGHT|GL_SINGLE))!=0,static_cast<uint32_t>(values[gyrolib::SteamMouse]),observe,steam_source,yaw,pitch,!c->panel&&!o->panel_open);
        o->mouse_hook.gate.publish(o->mouse_route,now);
        detected=o->mouse_route.detected?c->selected:0;
    }
    if(c->steam_mouse_device!=detected){c->steam_mouse_device=detected;c->emit(GL_EVENT_CONTEXT,6);}
    output->yaw_degrees+=yaw;output->pitch_degrees+=pitch;
#ifdef GL_EXPERIMENTAL_MOUSE_PROBE
    o->mouse_probe.camera(view,yaw,pitch);
#endif
}
bool raw_mouse_locked(MouseBridge* o,uint64_t now,uint64_t device,bool absolute,uint16_t buttons,int32_t dx,int32_t dy){
    if(device)return false;
    o->mouse_hook.gate.corroborate(o->mouse_route,now);
    const bool routed=o->mouse_route.raw(now,device,absolute,buttons,dx,dy);
    o->mouse_hook.gate.publish(o->mouse_route,now);
    return routed;
}
bool route_mouse(MouseBridge* o,HWND window,uint32_t message,uint64_t wp,int64_t lp,bool read_raw){
    std::lock_guard lock(o->mutex);auto& route=o->mouse_route;
    if(message==WM_KILLFOCUS||message==WM_DESTROY||message==WM_NCDESTROY||
        (message==WM_ACTIVATEAPP&&!wp)||!foreground(window)){
        o->mouse_hook.gate.clear();route.clear();return false;
    }
    const auto now=ticks()/1000000;
    // Detect with the panel open too, but leave its normal UI input alone.

    if(message==WM_MOUSEMOVE){INPUT_MESSAGE_SOURCE source{};
        if(!GetCurrentInputMessageSource(&source)||source.deviceType!=IMDT_MOUSE)return false;
        if(source.originId==IMO_HARDWARE)return false;
        const bool routed=source.originId==IMO_INJECTED&&route.injected(now);
        o->mouse_hook.gate.publish(route,now);return routed;}
    if(message!=WM_INPUT||!read_raw)return false;
    RAWINPUTHEADER header{};UINT size=sizeof(header);
    if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_HEADER,&header,&size,sizeof(header))==UINT(-1)||
        header.dwType!=RIM_TYPEMOUSE)return false;
    if(header.hDevice)return false;
    RAWINPUT raw{};size=sizeof(raw);
    const auto received=GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,&raw,&size,sizeof(header));
    if(received==UINT(-1)||received<sizeof(RAWINPUT)||raw.header.dwType!=RIM_TYPEMOUSE)return false;
    return raw_mouse_locked(o,now,reinterpret_cast<uintptr_t>(raw.header.hDevice),
        (raw.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)!=0,raw.data.mouse.usButtonFlags,raw.data.mouse.lLastX,raw.data.mouse.lLastY);
}
}
MouseBridge* mouse_bridge_create(gl_context* c){
    if(!c||c->virtual_mouse)return nullptr;
    auto* b=new(std::nothrow) MouseBridge;if(!b)return nullptr;b->owner=c;
    if(!b->mouse_hook.start()){delete b;return nullptr;}
    c->virtual_mouse_user=b;c->virtual_mouse=apply_mouse;
    c->virtual_mouse_stop=[](void* user){stop_mouse(static_cast<MouseBridge*>(user));};
    return b;
}
void mouse_bridge_destroy(MouseBridge* b){
    if(!b)return;stop_mouse(b);auto* c=b->owner;
    if(c->virtual_mouse_user==b){c->virtual_mouse=nullptr;c->virtual_mouse_user=nullptr;c->virtual_mouse_stop=nullptr;
        c->steam_mouse_device=0;}
    delete b;
}
void mouse_bridge_window(MouseBridge* b,void* window){if(b){stop_mouse(b);b->window=window;b->mouse_hook.window=static_cast<HWND>(window);}}
void mouse_bridge_stop(MouseBridge* b){if(b)stop_mouse(b);}
void mouse_bridge_panel(MouseBridge* b,bool open){if(b){std::lock_guard lock(b->mutex);b->panel_open=open;
    if(open){b->mouse_hook.gate.clear();b->mouse_route.clear();}}}
bool mouse_bridge_message(MouseBridge* b,void* window,uint32_t message,uint64_t wp,int64_t lp,bool read_raw){
    if(!b||window!=b->window.load())return false;
    const bool routed=route_mouse(b,static_cast<HWND>(window),message,wp,lp,read_raw);
#ifdef GL_EXPERIMENTAL_MOUSE_PROBE
    b->mouse_probe.message(message,wp,lp,routed);
#endif
    return routed;
}
bool mouse_bridge_raw(MouseBridge* b,uint64_t device,bool absolute,uint16_t buttons,int32_t dx,int32_t dy){
    if(!b)return false;std::lock_guard lock(b->mutex);auto window=static_cast<HWND>(b->window.load());
    if(!foreground(window)){b->mouse_hook.gate.clear();b->mouse_route.clear();return false;}
    return raw_mouse_locked(b,ticks()/1000000,device,absolute,buttons,dx,dy);
}
}
#else
namespace gyrolib_detail {
MouseBridge* mouse_bridge_create(gl_context*){return nullptr;}
void mouse_bridge_destroy(MouseBridge*){}
void mouse_bridge_window(MouseBridge*,void*){}
void mouse_bridge_stop(MouseBridge*){}
void mouse_bridge_panel(MouseBridge*,bool){}
bool mouse_bridge_message(MouseBridge*,void*,uint32_t,uint64_t,int64_t,bool){return false;}
bool mouse_bridge_raw(MouseBridge*,uint64_t,bool,uint16_t,int32_t,int32_t){return false;}
}
#endif
