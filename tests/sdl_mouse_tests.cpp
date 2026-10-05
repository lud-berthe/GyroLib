#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL.h>
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/sdl.h>
#include "../third_party/SDL/gyrolib_mouse.h"
#include "../src/detail/internal.hpp"
#include <atomic>
#include <thread>
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x " "<<SDL_GetError()<<'\n';return 1;}}while(0)
static bool SDLCALL host_filter(void*,SDL_Event*){return true;}
int main(){
    CHECK(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    auto* window=SDL_CreateWindow("GyroLib input test",160,120,SDL_WINDOW_HIDDEN);CHECK(window);
    const auto props=SDL_GetWindowProperties(window);
    CHECK(SDL_GetNumberProperty(props,GL_SDL_MOUSE_VERSION,0)==1);
    auto* hwnd=SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);CHECK(hwnd);
    // Verify the real SDL window procedure consults the per-window filter
    // before publishing movement. SendMessage doesn't move the desktop cursor.
    unsigned messages=0;GyroLibSDLMouseFilter probe{};probe.user=&messages;
    probe.message=[](void* p,void*,Uint32 msg,Uint64,Sint64){if(msg==WM_MOUSEMOVE){++*static_cast<unsigned*>(p);return true;}return false;};
    CHECK(SDL_SetPointerProperty(props,GL_SDL_MOUSE_FILTER,&probe));
    SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
    SendMessageW(static_cast<HWND>(hwnd),WM_MOUSEMOVE,0,MAKELPARAM(50,50));
    CHECK(messages==1);CHECK(!SDL_HasEvent(SDL_EVENT_MOUSE_MOTION));
    SDL_ClearProperty(props,GL_SDL_MOUSE_FILTER);
    gyrolib::Context context,other;
    auto* reader=gl_sdl_create(context.get(),1);auto* second=gl_sdl_create(other.get(),1);auto* duplicate=gl_sdl_create(context.get(),1);
    CHECK(reader&&second&&duplicate);
    SDL_SetEventFilter(host_filter,&messages);
    CHECK(gl_sdl_attach_window(reader,window)==GL_OK);
    CHECK(context.get()->virtual_mouse&&context.get()->virtual_mouse_user);
    CHECK(gl_sdl_attach_window(reader,window)==GL_OK);
    CHECK(gl_sdl_attach_window(second,window)==GL_UNAVAILABLE);
    CHECK(gl_sdl_attach_window(duplicate,window)==GL_UNAVAILABLE);
    auto* other_window=SDL_CreateWindow("Second",160,120,SDL_WINDOW_HIDDEN);CHECK(other_window);
    CHECK(gl_sdl_attach_window(duplicate,other_window)==GL_UNAVAILABLE);
    CHECK(gl_sdl_attach_window(second,other_window)==GL_OK);
    SDL_EventFilter filter{};void* user{};CHECK(SDL_GetEventFilter(&filter,&user));CHECK(filter==host_filter&&user==&messages);
    // Concurrent raw callback access must complete before detachment frees the
    // reader's bridge. This does not inject input or confine the desktop cursor.
    std::atomic<bool> started{},finish{};
    std::thread raw([&]{while(!finish){SDL_LockProperties(props);
        auto* cb=static_cast<GyroLibSDLMouseFilter*>(SDL_GetPointerProperty(props,GL_SDL_MOUSE_FILTER,nullptr));
        if(cb&&cb->raw)cb->raw(cb->user,123,false,0,1,1);
        started=true;SDL_UnlockProperties(props);std::this_thread::yield();}});
    while(!started)std::this_thread::yield();
    const auto detached=gl_sdl_attach_window(reader,nullptr);finish=true;raw.join();
    CHECK(detached==GL_OK);CHECK(!context.get()->virtual_mouse);
    CHECK(!SDL_GetPointerProperty(props,GL_SDL_MOUSE_FILTER,nullptr));
    CHECK(gl_sdl_attach_window(reader,window)==GL_OK);
    SDL_DestroyWindow(window); // property cleanup invalidates the window safely
    CHECK(gl_sdl_attach_window(reader,nullptr)==GL_OK);CHECK(!context.get()->virtual_mouse);
    gl_sdl_destroy(second);CHECK(!other.get()->virtual_mouse);
    const auto other_props=SDL_GetWindowProperties(other_window);
    CHECK(!SDL_GetPointerProperty(other_props,GL_SDL_MOUSE_FILTER,nullptr));
    // An external SDL runtime without this extension fails explicitly.
    SDL_ClearProperty(other_props,GL_SDL_MOUSE_VERSION);
    CHECK(gl_sdl_attach_window(reader,other_window)==GL_UNAVAILABLE);
    CHECK(!context.get()->virtual_mouse);
    CHECK(SDL_GetEventFilter(&filter,&user)&&filter==host_filter&&user==&messages);
    SDL_SetEventFilter(nullptr,nullptr);gl_sdl_destroy(reader);gl_sdl_destroy(duplicate);
    SDL_DestroyWindow(other_window);SDL_Quit();
    std::cout<<"SDL window bridge: pre-event interception, ownership, host filter, concurrent detach, destruction and unsupported runtime passed\n";
}
