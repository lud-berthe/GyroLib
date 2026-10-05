#include <gyrolib/overlay.h>
#include "detail/internal.hpp"
#include "detail/panel_commands.hpp"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "detail/overlay_hdr.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>
#include <cmath>
#include <cfloat>
#include <cstdio>
using Microsoft::WRL::ComPtr;
thread_local ImGuiContext* gl_overlay_imgui_context{};
namespace {
thread_local char overlay_error[512]{};
uint64_t ticks(){return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
int fail(const char* message,int code=GL_INVALID) noexcept {std::snprintf(overlay_error,sizeof(overlay_error),"%s",message);return code;}
int hr(HRESULT result,const char* operation){if(SUCCEEDED(result))return GL_OK;
    char text[180];std::snprintf(text,sizeof(text),"%s (HRESULT 0x%08lx)",operation,static_cast<unsigned long>(result));return fail(text,GL_IO_ERROR);}
struct ContextScope {ImGuiContext* previous;ContextScope(ImGuiContext* c):previous(ImGui::GetCurrentContext()){ImGui::SetCurrentContext(c);}~ContextScope(){ImGui::SetCurrentContext(previous);}};
struct OwnedCommand {gyrolib_panel_detail::CommandType type;std::string id;double value;uint64_t physical,sensor,revision;};
struct Message {UINT message;WPARAM wparam;LPARAM lparam;};
struct Frame {ComPtr<ID3D12Resource> buffer;ComPtr<ID3D12CommandAllocator> allocator;uint64_t fence{};};
// Deliberately do NOT copy Motion/GamepadMotion (self-referencing internals).
// UI needs capability/timestamp/diagnostic snapshots, never fusion or samples.
std::unique_ptr<gl_context> snapshot(const gl_context& c){
    auto out=std::make_unique<gl_context>();
    out->settings=c.settings;out->gameplay_contexts=c.gameplay_contexts;out->context_settings=c.context_settings;
    out->profile_links=c.profile_links;out->recommended=c.recommended;
    out->manual_motion_groups=c.manual_motion_groups;out->host_capabilities=c.host_capabilities;
    out->output_target=c.output_target;out->language=c.language;out->menu_key=c.menu_key;out->gamepad_menu_shortcut=c.gamepad_menu_shortcut;
    out->settings_save_result=c.settings_save_result;out->selected=c.selected;out->active=c.active;
    out->now=c.now;out->selected_at=c.selected_at;out->switched_at=c.switched_at;
    out->host=c.host;out->panel=c.panel;out->output=c.output;out->totals=c.totals;
    out->panel_opening=c.panel_opening;out->panel_opening_context=c.panel_opening_context;
    if(c.recenter)out->recenter=[](void*){};
    for(const auto& e:c.endpoints){out->endpoints.emplace_back();auto& d=out->endpoints.back();
        d.info=e.info;d.motion_companion=e.motion_companion;d.companion_identity=e.companion_identity;
        d.pairing_vendor=e.pairing_vendor;d.virtual_controller=e.virtual_controller;d.control_authority=e.control_authority;
        d.button_labels=e.button_labels;d.label_provenance=e.label_provenance;d.button_contacts=e.button_contacts;
        d.controls=e.controls;d.flick_input=e.flick_input;d.flick_explicit=e.flick_explicit;d.triggers=e.triggers;
        d.trigger_labels=e.trigger_labels;d.last_sensor=e.last_sensor;d.last_arrival=e.last_arrival;
        d.last_accel=e.last_accel;d.healthy_since=e.healthy_since;d.consecutive=e.consecutive;
        d.motion.diagnostics=e.motion.diagnostics;
    }return out;
}
ImGuiKey key(WPARAM value,LPARAM flags){
    if(value>='A'&&value<='Z')return ImGuiKey(ImGuiKey_A+value-'A');
    if(value>='0'&&value<='9')return ImGuiKey(ImGuiKey_0+value-'0');
    if(value>=VK_F1&&value<=VK_F24)return ImGuiKey(ImGuiKey_F1+value-VK_F1);
    switch(value){case VK_TAB:return ImGuiKey_Tab;case VK_LEFT:return ImGuiKey_LeftArrow;
    case VK_RIGHT:return ImGuiKey_RightArrow;case VK_UP:return ImGuiKey_UpArrow;case VK_DOWN:return ImGuiKey_DownArrow;
    case VK_PRIOR:return ImGuiKey_PageUp;case VK_NEXT:return ImGuiKey_PageDown;case VK_HOME:return ImGuiKey_Home;
    case VK_END:return ImGuiKey_End;case VK_INSERT:return ImGuiKey_Insert;case VK_DELETE:return ImGuiKey_Delete;
    case VK_BACK:return ImGuiKey_Backspace;case VK_SPACE:return ImGuiKey_Space;case VK_RETURN:return ImGuiKey_Enter;
    case VK_ESCAPE:return ImGuiKey_Escape;case VK_SHIFT:return ((flags>>16)&255)==0x36?ImGuiKey_RightShift:ImGuiKey_LeftShift;
    case VK_CONTROL:return flags&(1ll<<24)?ImGuiKey_RightCtrl:ImGuiKey_LeftCtrl;
    case VK_MENU:return flags&(1ll<<24)?ImGuiKey_RightAlt:ImGuiKey_LeftAlt;
    case VK_LWIN:return ImGuiKey_LeftSuper;case VK_RWIN:return ImGuiKey_RightSuper;default:return ImGuiKey_None;}
}
}
struct gl_overlay {
    gl_context* owner{};DWORD owner_thread{},render_thread{};
    std::mutex mutex;std::vector<OwnedCommand> commands;std::vector<Message> messages;
    std::unique_ptr<gl_context> published,mirror;uint64_t published_at{},mirror_at{};
    uint64_t requested_revision{},published_revision{},applied_revision{};
    std::atomic<bool> open{},attached{true},ready{};std::atomic<uint32_t> shortcut{10};
    std::atomic<uintptr_t> window{};std::atomic<int> last_result{GL_OK};
    std::atomic<uint64_t> rendered_at{};
    bool publish_pending{},reset_input{};ImGuiContext* imgui{};gl_panel* panel{};
    ComPtr<IDXGISwapChain3> swapchain;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Device> device;
    ComPtr<ID3D12DescriptorHeap> rtv,srv;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    std::vector<Frame> frames;std::vector<unsigned> free_srv;
    HANDLE fence_event{};uint64_t submitted{};UINT rtv_stride{},srv_stride{};DXGI_FORMAT format{};bool backend{};
    uint32_t controls{};float stick_x{},stick_y{};
    std::unique_ptr<gyrolib_hdr::Composer> hdr;
    bool automatic_color{};uint32_t color_space{};
};
namespace {
std::mutex registry_mutex;std::set<gl_context*> registered;
int32_t publish_after_update(void* user)try{
    auto* o=static_cast<gl_overlay*>(user);
    if(!o->publish_pending)return GL_OK;
    // Inputs were polled before process(), so their timestamps can be newer
    // than the preceding core frame. Publish only after gl_update resolves
    // selection/association, advances its clock and processes sensor samples.
    auto next=snapshot(*o->owner);
    {std::lock_guard lock(o->mutex);o->published=std::move(next);
        o->published_at=ticks();o->published_revision=o->applied_revision;}
    o->publish_pending=false;return GL_OK;
}catch(...){
    auto* o=static_cast<gl_overlay*>(user);o->open=false;o->last_result=GL_LIMIT;
    return fail("Cannot publish overlay snapshot",GL_LIMIT);
}
int enqueue(void* user,const gyrolib_panel_detail::Command& command){
    auto* o=static_cast<gl_overlay*>(user);std::lock_guard lock(o->mutex);
    if(!o->attached)return GL_UNAVAILABLE;if(o->commands.size()>=256)return GL_LIMIT;
    o->commands.push_back({command.type,command.id?command.id:"",command.value,command.physical,command.sensor,++o->requested_revision});return GL_OK;
}
int wait(gl_overlay& o,uint64_t value){
    if(!value)return GL_OK;const auto completed=o.fence->GetCompletedValue();
    if(completed==UINT64_MAX)return fail("DX12 device removed",GL_IO_ERROR);
    if(completed>=value)return GL_OK;
    if(auto r=hr(o.fence->SetEventOnCompletion(value,o.fence_event),"SetEventOnCompletion");r!=GL_OK)return r;
    if(WaitForSingleObject(o.fence_event,5000)!=WAIT_OBJECT_0)return fail("DX12 fence wait failed/timed out",GL_IO_ERROR);
    return GL_OK;
}
bool render_thread(gl_overlay* o){return o&&o->render_thread==GetCurrentThreadId();}
void release_targets(gl_overlay& o){for(auto& f:o.frames)f.buffer.Reset();if(o.hdr)o.hdr->release_targets();}
int acquire_targets(gl_overlay& o){
    DXGI_SWAP_CHAIN_DESC1 desc{};if(auto r=hr(o.swapchain->GetDesc1(&desc),"GetDesc1");r)return r;
    if(desc.BufferCount!=o.frames.size()||desc.Format!=o.format||desc.SampleDesc.Count!=1)
        return fail("Changed buffer count/format: shutdown and reinitialize",GL_UNAVAILABLE);
    auto handle=o.rtv->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<desc.BufferCount;++i){if(auto r=hr(o.swapchain->GetBuffer(i,IID_PPV_ARGS(&o.frames[i].buffer)),"GetBuffer");r){release_targets(o);return r;}
        o.device->CreateRenderTargetView(o.frames[i].buffer.Get(),nullptr,handle);handle.ptr+=o.rtv_stride;}
    if(o.hdr)if(auto r=hr(o.hdr->resize(o.device.Get(),o.frames[0].buffer->GetDesc()),"HDR render targets");r){release_targets(o);return r;}
    return GL_OK;
}
void input(gl_overlay& o,const std::vector<Message>& messages){
    auto& io=ImGui::GetIO();
    for(const auto& m:messages){switch(m.message){
    case WM_KEYDOWN:case WM_SYSKEYDOWN:case WM_KEYUP:case WM_SYSKEYUP:{const bool down=m.message==WM_KEYDOWN||m.message==WM_SYSKEYDOWN;
        const auto k=key(m.wparam,m.lparam);if(k!=ImGuiKey_None)io.AddKeyEvent(k,down);
        if(m.wparam==VK_CONTROL)io.AddKeyEvent(ImGuiMod_Ctrl,down);if(m.wparam==VK_SHIFT)io.AddKeyEvent(ImGuiMod_Shift,down);
        if(m.wparam==VK_MENU)io.AddKeyEvent(ImGuiMod_Alt,down);
        if(m.wparam==VK_LWIN||m.wparam==VK_RWIN)io.AddKeyEvent(ImGuiMod_Super,down);break;}
    case WM_CHAR:io.AddInputCharacterUTF16(static_cast<ImWchar16>(m.wparam));break;
    case WM_UNICHAR:if(m.wparam!=UNICODE_NOCHAR)io.AddInputCharacter(static_cast<unsigned>(m.wparam));break;
    case WM_MOUSEMOVE:io.AddMousePosEvent(float(GET_X_LPARAM(m.lparam)),float(GET_Y_LPARAM(m.lparam)));break;
    case WM_MOUSELEAVE:io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);break;
    case WM_LBUTTONDOWN:case WM_LBUTTONUP:io.AddMouseButtonEvent(0,m.message==WM_LBUTTONDOWN);break;
    case WM_RBUTTONDOWN:case WM_RBUTTONUP:io.AddMouseButtonEvent(1,m.message==WM_RBUTTONDOWN);break;
    case WM_MBUTTONDOWN:case WM_MBUTTONUP:io.AddMouseButtonEvent(2,m.message==WM_MBUTTONDOWN);break;
    case WM_XBUTTONDOWN:case WM_XBUTTONUP:io.AddMouseButtonEvent(GET_XBUTTON_WPARAM(m.wparam)==XBUTTON1?3:4,m.message==WM_XBUTTONDOWN);break;
    case WM_MOUSEWHEEL:io.AddMouseWheelEvent(0,float(GET_WHEEL_DELTA_WPARAM(m.wparam))/WHEEL_DELTA);break;
    case WM_MOUSEHWHEEL:io.AddMouseWheelEvent(-float(GET_WHEEL_DELTA_WPARAM(m.wparam))/WHEEL_DELTA,0);break;
    case WM_SETFOCUS:io.AddFocusEvent(true);break;case WM_KILLFOCUS:io.AddFocusEvent(false);break;
    default:break;}}
    const bool current=o.mirror&&o.mirror->host.focused;
    uint32_t buttons=0;float x=0,y=0;bool found=false;
    if(current)for(const auto& e:o.mirror->endpoints)if(!e.motion_companion&&e.info.connected&&e.info.physical_id==o.mirror->selected){
        if(o.mirror->now>=e.controls.timestamp_ns&&o.mirror->now-e.controls.timestamp_ns<150000000){
            buttons=e.controls.buttons;x=e.controls.left_x;y=e.controls.left_y;found=true;break;}}
    io.BackendFlags=(io.BackendFlags&~ImGuiBackendFlags_HasGamepad)|(found?ImGuiBackendFlags_HasGamepad:0);
    const auto button=[&](ImGuiKey k,unsigned bit){io.AddKeyEvent(k,(buttons&(1u<<bit))!=0);};
    button(ImGuiKey_GamepadFaceDown,0);button(ImGuiKey_GamepadFaceRight,1);button(ImGuiKey_GamepadFaceLeft,2);button(ImGuiKey_GamepadFaceUp,3);
    button(ImGuiKey_GamepadStart,6);button(ImGuiKey_GamepadBack,4);button(ImGuiKey_GamepadL1,9);button(ImGuiKey_GamepadR1,10);
    button(ImGuiKey_GamepadDpadUp,11);button(ImGuiKey_GamepadDpadDown,12);button(ImGuiKey_GamepadDpadLeft,13);button(ImGuiKey_GamepadDpadRight,14);
    const auto axis=[&](ImGuiKey k,float v){const float a=std::clamp((v-.2f)/.8f,0.f,1.f);io.AddKeyAnalogEvent(k,a>0,a);};
    axis(ImGuiKey_GamepadLStickLeft,-x);axis(ImGuiKey_GamepadLStickRight,x);axis(ImGuiKey_GamepadLStickUp,y);axis(ImGuiKey_GamepadLStickDown,-y);
}
}
extern "C" {
const char* GL_CALL gl_overlay_error(void){return overlay_error;}
gl_overlay* GL_CALL gl_overlay_create(gl_context* c,uint32_t abi)try{
    if(!c||abi!=GL_OVERLAY_ABI_VERSION){fail("Invalid overlay context/ABI");return nullptr;}
    std::lock_guard lock(registry_mutex);if(registered.count(c)){fail("One overlay per context");return nullptr;}
    auto o=std::make_unique<gl_overlay>();o->owner=c;o->owner_thread=GetCurrentThreadId();
    o->open=c->panel;o->shortcut=c->menu_key;o->published=snapshot(*c);o->published_at=ticks();
    registered.insert(c);c->overlay_user=o.get();c->publish_overlay=publish_after_update;
    c->panel_changed=[](void* user,bool open){static_cast<gl_overlay*>(user)->open=open;};
    return o.release();
}catch(...){fail("Cannot allocate overlay",GL_LIMIT);return nullptr;}
int32_t GL_CALL gl_overlay_process(gl_overlay* o)try{
    if(!o||GetCurrentThreadId()!=o->owner_thread)return fail("process requires context owner thread");
    if(!o->attached)return GL_UNAVAILABLE;
    std::vector<OwnedCommand> commands;{std::lock_guard lock(o->mutex);commands.swap(o->commands);}
    auto* c=o->owner;int result=GL_OK;
    for(const auto& cmd:commands){int r=GL_INVALID;using namespace gyrolib_panel_detail;
        switch(cmd.type){case Setting:r=gl_setting_set(c,cmd.id.c_str(),cmd.value);break;
        case Action:r=gl_action(c,cmd.id.c_str());break;case Language:r=gl_set_language(c,cmd.id.c_str());break;
        case Parent:r=gl_set_context_parent(c,static_cast<uint32_t>(cmd.physical),static_cast<uint32_t>(cmd.sensor));break;
        case Inherit:r=gl_setting_inherit(c,cmd.id.c_str());break;
        case Device:r=gl_select_device(c,cmd.physical);break;case Sensor:r=gl_bind_motion_sensor(c,cmd.physical,cmd.sensor);break;}
        if(r!=GL_OK)result=r;
        o->applied_revision=cmd.revision;
    }
    const auto last_render=o->rendered_at.load();
    if(o->open&&last_render&&ticks()-last_render>250000000)o->open=false;
    gl_set_panel_open(c,o->open);o->shortcut=c->menu_key;o->last_result=result;
    o->publish_pending=true; // gl_update publishes one complete frame automatically.
    return result;
}catch(...){return fail("Cannot process overlay commands",GL_LIMIT);}
int32_t GL_CALL gl_overlay_detach(gl_overlay* o)try{
    if(!o||GetCurrentThreadId()!=o->owner_thread)return fail("detach requires context owner thread");
    std::lock_guard lock(registry_mutex);
    if(!o->attached.exchange(false))return GL_OK;gl_set_panel_open(o->owner,0);
    o->owner->publish_overlay=nullptr;o->owner->panel_changed=nullptr;o->owner->overlay_user=nullptr;o->publish_pending=false;
    registered.erase(o->owner);o->owner=nullptr;o->open=false;return GL_OK;
}catch(...){return fail("Cannot detach overlay",GL_LIMIT);}
int32_t GL_CALL gl_overlay_destroy(gl_overlay* o){
    if(!o)return GL_OK;if(o->attached||o->imgui)return fail("detach and shutdown before destroy");delete o;return GL_OK;
}
int32_t GL_CALL gl_overlay_set_open(gl_overlay* o,uint32_t open){if(!o||open>1)return GL_INVALID;if(!o->attached)return GL_UNAVAILABLE;o->open=open!=0;return GL_OK;}
uint32_t GL_CALL gl_overlay_capture(const gl_overlay* o){return o&&o->attached&&o->ready&&o->open?
    GL_OVERLAY_CAPTURE_MOUSE|GL_OVERLAY_CAPTURE_KEYBOARD|GL_OVERLAY_CAPTURE_GAMEPAD:0;}
uint32_t GL_CALL gl_overlay_win32_message(gl_overlay* o,void* window,uint32_t message,uint64_t wp,int64_t lp)try{
    if(!o||!o->attached||!o->ready||reinterpret_cast<uintptr_t>(window)!=o->window)return 0;
    bool relevant=false;uint32_t capture=0;
    switch(message){case WM_KEYDOWN:case WM_SYSKEYDOWN:case WM_KEYUP:case WM_SYSKEYUP:case WM_CHAR:case WM_UNICHAR:
        relevant=true;capture=GL_OVERLAY_CAPTURE_KEYBOARD;break;
    case WM_MOUSEMOVE:case WM_MOUSELEAVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_RBUTTONDOWN:case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:case WM_MBUTTONUP:case WM_XBUTTONDOWN:case WM_XBUTTONUP:case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:
        relevant=true;capture=GL_OVERLAY_CAPTURE_MOUSE;break;
    case WM_SETFOCUS:case WM_KILLFOCUS:relevant=true;break;default:return 0;}
    const auto shortcut=o->shortcut.load();
    if(shortcut&&(message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&wp==VK_F1+shortcut-1&&!(lp&(1ll<<30))){
        o->open=!o->open.load();capture=GL_OVERLAY_CAPTURE_KEYBOARD;
        std::lock_guard lock(o->mutex);o->messages.clear();o->reset_input=true;return capture;
    }
    if(relevant){std::lock_guard lock(o->mutex);if(o->messages.size()>=1024){o->messages.clear();o->reset_input=true;}
        o->messages.push_back({message,static_cast<WPARAM>(wp),static_cast<LPARAM>(lp)});}
    return o->open?capture:0;
}catch(...){return 0;}
int32_t GL_CALL gl_overlay_dx12_init(gl_overlay* o,const gl_overlay_dx12_desc* desc)try{
    if(!o||!desc||desc->size!=sizeof(*desc)||desc->abi_version!=GL_OVERLAY_ABI_VERSION||desc->reserved||!desc->window||!desc->swapchain||!desc->command_queue)
        return fail("Invalid DX12 descriptor");
    if(o->imgui||!o->attached)return fail("Overlay already initialized/detached");
    if(!IsWindow(static_cast<HWND>(desc->window)))return fail("Invalid game window");
    DWORD process{};GetWindowThreadProcessId(static_cast<HWND>(desc->window),&process);
    if(process!=GetCurrentProcessId())return fail("Window must belong to host process");
    o->render_thread=GetCurrentThreadId();o->swapchain=static_cast<IDXGISwapChain3*>(desc->swapchain);o->queue=static_cast<ID3D12CommandQueue*>(desc->command_queue);
    auto cleanup=[&](int r){gl_overlay_dx12_shutdown(o);return r;};
    if(o->queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return cleanup(fail("DX12 DIRECT queue required"));
    if(auto r=hr(o->swapchain->GetDevice(IID_PPV_ARGS(&o->device)),"Swapchain GetDevice");r)return cleanup(r);
    ComPtr<ID3D12Device> queue_device;if(auto r=hr(o->queue->GetDevice(IID_PPV_ARGS(&queue_device)),"Queue GetDevice");r)return cleanup(r);
    if(queue_device.Get()!=o->device.Get())return cleanup(fail("Queue/swapchain device mismatch"));
    DXGI_SWAP_CHAIN_DESC1 sc{};if(auto r=hr(o->swapchain->GetDesc1(&sc),"GetDesc1");r)return cleanup(r);
    uint32_t color{};
    if(auto r=hr(gyrolib_hdr::resolve_space(o->swapchain.Get(),sc.Format,desc->color_space,color),"Read DXGI output color space (host can provide an explicit space)");r)return cleanup(r);
    if(sc.BufferCount<2||sc.BufferCount>8||sc.SampleDesc.Count!=1||
       !gyrolib_hdr::supported(sc.Format,color))
        return cleanup(fail("Unsupported buffer format/color space; expected SDR, scRGB FP16 or HDR10 PQ with 2..8 single-sample buffers",GL_UNAVAILABLE));
    o->format=sc.Format;o->frames.resize(sc.BufferCount);
    o->automatic_color=desc->color_space==GL_OVERLAY_COLOR_SPACE_AUTO;o->color_space=color;
    if(color!=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709){
        o->hdr=std::make_unique<gyrolib_hdr::Composer>();
        if(auto r=hr(o->hdr->init(o->device.Get(),sc.Format,color),"HDR compositor initialization");r)return cleanup(r);
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,sc.BufferCount,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    if(auto r=hr(o->device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&o->rtv)),"RTV heap");r)return cleanup(r);
    heap={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,64,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    if(auto r=hr(o->device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&o->srv)),"SRV heap");r)return cleanup(r);
    o->rtv_stride=o->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    o->srv_stride=o->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(unsigned i=64;i>0;--i)o->free_srv.push_back(i-1);
    for(auto& f:o->frames)if(auto r=hr(o->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&f.allocator)),"Command allocator");r)return cleanup(r);
    if(auto r=hr(o->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,o->frames[0].allocator.Get(),nullptr,IID_PPV_ARGS(&o->list)),"Command list");r)return cleanup(r);
    if(auto r=hr(o->list->Close(),"Close command list");r)return cleanup(r);
    if(auto r=hr(o->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&o->fence)),"Fence");r)return cleanup(r);
    o->fence_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!o->fence_event)return cleanup(fail("Fence event",GL_IO_ERROR));
    if(auto r=acquire_targets(*o);r)return cleanup(r);
    auto* previous=ImGui::GetCurrentContext();o->imgui=ImGui::CreateContext();ImGui::SetCurrentContext(previous);
    if(!o->imgui)return cleanup(fail("ImGui allocation",GL_LIMIT));
    ContextScope scope(o->imgui);auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad|ImGuiConfigFlags_NoMouseCursorChange;
    io.Fonts->AddFontDefaultVector();io.BackendPlatformName="gyrolib_win32_forwarded";ImGui::StyleColorsDark();
    ImGui_ImplDX12_InitInfo info{};info.Device=o->device.Get();info.CommandQueue=o->queue.Get();info.NumFramesInFlight=sc.BufferCount;
    info.RTVFormat=o->hdr?DXGI_FORMAT_R8G8B8A8_UNORM:sc.Format;info.SrvDescriptorHeap=o->srv.Get();info.UserData=o;
    info.SrvDescriptorAllocFn=[](ImGui_ImplDX12_InitInfo* i,D3D12_CPU_DESCRIPTOR_HANDLE* cpu,D3D12_GPU_DESCRIPTOR_HANDLE* gpu){
        auto& p=*static_cast<gl_overlay*>(i->UserData);if(p.free_srv.empty()){*cpu={};*gpu={};return;}
        auto index=p.free_srv.back();p.free_srv.pop_back();*cpu=p.srv->GetCPUDescriptorHandleForHeapStart();cpu->ptr+=size_t(index)*p.srv_stride;
        *gpu=p.srv->GetGPUDescriptorHandleForHeapStart();gpu->ptr+=uint64_t(index)*p.srv_stride;};
    info.SrvDescriptorFreeFn=[](ImGui_ImplDX12_InitInfo* i,D3D12_CPU_DESCRIPTOR_HANDLE cpu,D3D12_GPU_DESCRIPTOR_HANDLE){
        auto& p=*static_cast<gl_overlay*>(i->UserData);p.free_srv.push_back(unsigned((cpu.ptr-p.srv->GetCPUDescriptorHandleForHeapStart().ptr)/p.srv_stride));};
    o->backend=ImGui_ImplDX12_Init(&info);if(!o->backend)return cleanup(fail("ImGui DX12 initialization",GL_IO_ERROR));
    if(!ImGui_ImplDX12_CreateDeviceObjects())return cleanup(fail("DX12 shader/device objects",GL_IO_ERROR));
    o->window=reinterpret_cast<uintptr_t>(desc->window);o->ready=true;return GL_OK;
}catch(...){if(o&&o->render_thread==GetCurrentThreadId())gl_overlay_dx12_shutdown(o);return fail("DX12 initialization allocation",GL_LIMIT);}
int32_t GL_CALL gl_overlay_dx12_render(gl_overlay* o,double delta,float dpi)try{
    if(!render_thread(o)||!o->ready)return fail("render requires initialized render thread");
    if(!std::isfinite(delta)||delta<=0||delta>1||!std::isfinite(dpi)||dpi<=0)return fail("Invalid frame time/DPI");
    if(o->automatic_color && o->open){
        uint32_t color{};
        if(auto r=hr(gyrolib_hdr::resolve_space(o->swapchain.Get(),o->format,GL_OVERLAY_COLOR_SPACE_AUTO,color),"Refresh DXGI output color space");r)return r;
        if(color!=o->color_space){
            // Keep host objects alive while replacing our own renderer state.
            auto swapchain=o->swapchain;auto queue=o->queue;
            const gl_overlay_dx12_desc desc{sizeof(desc),GL_OVERLAY_ABI_VERSION,GL_OVERLAY_COLOR_SPACE_AUTO,0,
                reinterpret_cast<void*>(o->window.load()),swapchain.Get(),queue.Get()};
            if(auto r=gl_overlay_dx12_shutdown(o);r)return r;
            if(auto r=gl_overlay_dx12_init(o,&desc);r)return r;
        }
    }
    o->rendered_at=ticks();
    ContextScope scope(o->imgui);std::vector<Message> messages;bool reset=false;
    {std::lock_guard lock(o->mutex);if(o->published&&o->published_revision>=o->requested_revision){o->mirror=std::move(o->published);o->mirror_at=o->published_at;}
        messages.swap(o->messages);reset=o->reset_input;o->reset_input=false;}
    auto& io=ImGui::GetIO();if(reset){io.ClearInputKeys();io.ClearEventsQueue();}
    if(!o->attached||!o->mirror||ticks()-o->mirror_at>250000000){o->open=false;io.ClearInputKeys();return GL_OK;}
    if(!o->open){io.ClearInputKeys();return GL_OK;}
    RECT client{};if(!GetClientRect(reinterpret_cast<HWND>(o->window.load()),&client))return fail("GetClientRect",GL_IO_ERROR);
    if(client.right<=0||client.bottom<=0)return GL_OK;
    if(!o->frames[0].buffer){if(auto r=acquire_targets(*o);r)return r;}
    const auto index=o->swapchain->GetCurrentBackBufferIndex();if(index>=o->frames.size())return fail("Invalid backbuffer index",GL_IO_ERROR);
    // One outstanding overlay submission: also protects the backend's cyclic
    // buffers and dynamic texture destruction across skipped Present frames.
    auto& frame=o->frames[index];if(auto r=wait(*o,o->submitted);r)return r;
    if(auto r=hr(frame.allocator->Reset(),"Reset allocator");r)return r;
    if(auto r=hr(o->list->Reset(frame.allocator.Get(),nullptr),"Reset list");r)return r;
    if(!o->panel){o->panel=gl_panel_create(o->mirror.get(),nullptr);if(!o->panel)return GL_LIMIT;
        gyrolib_panel_detail::set_sink(o->panel,enqueue,o);}
    gyrolib_panel_detail::set_context(o->panel,o->mirror.get());gl_set_panel_open(o->mirror.get(),1);
    gyrolib_panel_detail::set_result(o->panel,o->last_result);
    input(*o,messages);io.DisplaySize=ImVec2(float(client.right),float(client.bottom));io.DeltaTime=float(delta);io.MouseDrawCursor=true;
    const auto size=frame.buffer->GetDesc();io.DisplayFramebufferScale=ImVec2(float(size.Width)/client.right,float(size.Height)/client.bottom);
    ImGui_ImplDX12_NewFrame();ImGui::NewFrame();gl_panel_draw(o->panel,io.DisplaySize.x,io.DisplaySize.y,dpi);
    if(!o->mirror->panel)o->open=false;ImGui::Render();
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={frame.buffer.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET};
    auto target=o->rtv->GetCPUDescriptorHandleForHeapStart();target.ptr+=size_t(index)*o->rtv_stride;
    if(o->hdr)o->hdr->begin(o->list.Get(),frame.buffer.Get());
    else{o->list->ResourceBarrier(1,&barrier);o->list->OMSetRenderTargets(1,&target,FALSE,nullptr);}
    ID3D12DescriptorHeap* heaps[]={o->srv.Get()};o->list->SetDescriptorHeaps(1,heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),o->list.Get());
    if(o->hdr)o->hdr->finish(o->list.Get(),frame.buffer.Get(),target);
    else{std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);o->list->ResourceBarrier(1,&barrier);}
    if(auto r=hr(o->list->Close(),"Close overlay list");r)return r;ID3D12CommandList* lists[]={o->list.Get()};o->queue->ExecuteCommandLists(1,lists);
    frame.fence=++o->submitted;if(auto r=hr(o->queue->Signal(o->fence.Get(),frame.fence),"Signal overlay fence");r)return r;
    return GL_OK;
}catch(...){return fail("Overlay render allocation",GL_LIMIT);}
int32_t GL_CALL gl_overlay_dx12_before_resize(gl_overlay* o){
    if(!render_thread(o)||!o->ready)return fail("before_resize requires render thread");
    if(auto r=wait(*o,o->submitted);r)return r;release_targets(*o);return GL_OK;
}
int32_t GL_CALL gl_overlay_dx12_set_hdr_white_level(gl_overlay* o,float nits){
    if(!render_thread(o)||!o->ready)return fail("HDR white level requires initialized render thread");
    if(!std::isfinite(nits)||nits<80||nits>1000)return fail("HDR white level must be 80..1000 nits");
    if(!o->hdr)return fail("HDR white level requires an HDR renderer",GL_UNAVAILABLE);
    o->hdr->white_nits=nits;return GL_OK;
}
int32_t GL_CALL gl_overlay_dx12_shutdown(gl_overlay* o)try{
    if(!o)return GL_OK;if(o->render_thread&& !render_thread(o))return fail("shutdown requires render thread");
    o->ready=false;int result=GL_OK;if(o->fence&&o->submitted)result=wait(*o,o->submitted);
    if(result!=GL_OK&&o->device&&SUCCEEDED(o->device->GetDeviceRemovedReason()))return result;
    if(o->imgui){ContextScope scope(o->imgui);if(o->backend)ImGui_ImplDX12_Shutdown();gl_panel_destroy(o->panel);o->panel=nullptr;
        if(scope.previous==o->imgui)scope.previous=nullptr;
        ImGui::DestroyContext(o->imgui);o->imgui=nullptr;}
    o->hdr.reset();o->backend=false;o->frames.clear();o->list.Reset();o->rtv.Reset();o->srv.Reset();o->queue.Reset();o->swapchain.Reset();o->device.Reset();o->fence.Reset();
    if(o->fence_event)CloseHandle(o->fence_event);o->fence_event=nullptr;o->free_srv.clear();o->submitted=0;o->window=0;o->render_thread=0;o->rendered_at=0;return result;
}catch(...){return fail("Cannot shut down overlay",GL_LIMIT);}
}
