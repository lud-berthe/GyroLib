#pragma once
#include <gyrolib/sdl.h>
#include <gyrolib/panel.h>
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>

// SDL scancodes F13..F24 are a separate range. No global keyboard hooks.
inline bool demo_panel_key(gl_panel* panel,const SDL_KeyboardEvent& event){
    uint32_t number=0;
    if(event.scancode>=SDL_SCANCODE_F1&&event.scancode<=SDL_SCANCODE_F12)number=event.scancode-SDL_SCANCODE_F1+1;
    else if(event.scancode>=SDL_SCANCODE_F13&&event.scancode<=SDL_SCANCODE_F24)number=event.scancode-SDL_SCANCODE_F13+13;
    if(number)gl_panel_function_key(panel,number,event.down,event.repeat);
    return number!=0;
}

// Borrow, never independently open: ImGui must not win the hotplug race and
// leave a handle's sensors disabled. Clear its manual list before reader.poll.
inline SDL_Gamepad* demo_selected_pad(gl_context* context,gl_sdl* reader) {
    uint64_t physical=gl_get_selected_device(context);
    if(!physical)for(uint32_t i=0;i<gl_endpoint_count(context);++i){
        gl_endpoint e{};gl_get_endpoint(context,i,&e);if(e.connected){physical=e.physical_id;break;}
    }
    SDL_Gamepad* pad=nullptr;int count=0;auto* ids=SDL_GetGamepads(&count);
    for(int i=0;i<count&&!pad;++i){
        auto id=gl_sdl_endpoint_for_instance(reader,ids[i]);if(!id)continue;
        for(uint32_t n=0;n<gl_endpoint_count(context);++n){
            gl_endpoint e{};gl_get_endpoint(context,n,&e);
            if(e.id==id&&e.connected&&e.physical_id==physical){pad=SDL_GetGamepadFromID(ids[i]);break;}
        }
    }
    SDL_free(ids);return pad;
}
inline void demo_ui_gamepad(SDL_Gamepad* pad){
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual,pad?&pad:nullptr,pad?1:0);
}
