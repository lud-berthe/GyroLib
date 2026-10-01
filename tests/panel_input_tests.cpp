#include <gyrolib/gyrolib.hpp>
#include <gyrolib/panel.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include "../examples/sdl_demo_input.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include "declared_view.hpp"

static bool shortcuts(){
    gyrolib::Context c;auto* panel=gl_panel_create(c.get(),nullptr);
    if(!panel)return false;
    bool ok=true;
    gl_panel_function_key(panel,10,1,0);ok&=gl_panel_open(c.get())==1;gl_panel_function_key(panel,10,1,0);ok&=gl_panel_open(c.get())==0;
    for(unsigned key=1;key<=24;++key){
        ok&=gl_set_menu_key(c.get(),key)==GL_OK;
        gl_panel_function_key(panel,key==24?1:key+1,1,0);ok&=gl_panel_open(c.get())==0;
        if(key!=10){gl_panel_function_key(panel,10,1,0);ok&=gl_panel_open(c.get())==0;}
        SDL_KeyboardEvent event{};event.scancode=static_cast<SDL_Scancode>(key<=12?SDL_SCANCODE_F1+key-1:SDL_SCANCODE_F13+key-13);
        event.down=true;event.repeat=true;ok&=demo_panel_key(panel,event);ok&=gl_panel_open(c.get())==0;
        event.repeat=false;event.down=false;demo_panel_key(panel,event);ok&=gl_panel_open(c.get())==0;
        event.down=true;demo_panel_key(panel,event);ok&=gl_panel_open(c.get())==1;
        demo_panel_key(panel,event);ok&=gl_panel_open(c.get())==0;
    }
    gl_set_menu_key(c.get(),0);
    for(unsigned key=0;key<=25;++key)gl_panel_function_key(panel,key,1,0);
    gl_panel_function_key(panel,10,1,0);ok&=gl_panel_open(c.get())==0;
    // Disabling our shortcut must not disable the host's native-menu camera gate.
    gl_set_panel_open(c.get(),1);ok&=gl_panel_open(c.get())==1;gl_set_panel_open(c.get(),0);
    gl_panel_destroy(panel);return ok;
}

// Isolated hotplug regression: a real SDL virtual device and sensor event path.
// This executable is a test fixture and is never installed as a demo.
struct VirtualPad {
    SDL_JoystickID id{};SDL_Joystick* joystick{};
    static bool SDLCALL enable(void*,bool){return true;}
    bool connect(){
        SDL_VirtualJoystickSensorDesc sensors[]={{SDL_SENSOR_ACCEL,100},{SDL_SENSOR_GYRO,100}};
        SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=21;
        desc.vendor_id=0xffff;desc.product_id=0xfffe;
        desc.axis_mask=(1u<<6)-1;desc.button_mask=(1u<<21)-1;
        desc.nsensors=2;desc.sensors=sensors;desc.SetSensorsEnabled=enable;
        desc.name="GyroLib hotplug regression";
        id=SDL_AttachVirtualJoystick(&desc);joystick=id?SDL_OpenJoystick(id):nullptr;
        return joystick!=nullptr;
    }
    bool send(uint64_t now){
        const float accel[]={0,SDL_STANDARD_GRAVITY,0},gyro[]={0,1,0};
        return SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_ACCEL,now,accel,3)&&
               SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_GYRO,now,gyro,3);
    }
    void disconnect(){if(id)SDL_DetachVirtualJoystick(id);if(joystick)SDL_CloseJoystick(joystick);id=0;joystick=nullptr;}
};
int main(int argc,char** argv){
    if(!shortcuts()){std::cerr<<"Configured panel shortcut regression\n";return 4;}
    const bool after=argc>1&&std::strcmp(argv[1],"--after-poll")==0;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0xffff/0xfffe");
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD))return 1;
    auto* window=SDL_CreateWindow("GyroLib panel input test",1024,720,SDL_WINDOW_HIDDEN);
    auto* renderer=window?SDL_CreateRenderer(window,nullptr):nullptr;if(!renderer)return 1;
    gyrolib::Context context;if(!declare_test_view(context.get()))return 5;
    auto* reader=gl_sdl_create(context.get(),1);
    auto* panel=gl_panel_create(context.get(),nullptr);if(!reader||!panel)return 1;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.Fonts->AddFontDefaultVector();
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer3_Init(renderer);demo_ui_gamepad(nullptr);
    VirtualPad pad;bool opened=false,closed=false,ready=false,moving=false,navigation=false,stopped=false;int result=0;
    for(unsigned frame=0;frame<70;++frame){
        if(!after&&frame==3&&!pad.connect()){result=2;break;}
        if(frame==65)pad.disconnect();
        const uint64_t now=1000000000ull+frame*10000000ull;
        if(pad.joystick&&frame>3&&!pad.send(now)){result=2;break;}
        if(frame==2||frame==6){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_F10;e.key.down=true;SDL_PushEvent(&e);}
        SDL_Event event{};while(SDL_PollEvent(&event)){
            ImGui_ImplSDL3_ProcessEvent(&event);
            if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)demo_panel_key(panel,event.key);
        }
        if(frame==3)opened=gl_panel_open(context.get());if(frame==7)closed=!gl_panel_open(context.get());
        demo_ui_gamepad(nullptr);gl_sdl_poll(reader,now);
        if(after&&frame==3&&!pad.connect()){result=2;break;}
        gl_host_state host{};host.focused=host.camera_allowed=1;gl_output output{};
        update_test_view(context.get(),now,&host,&output);
        if(frame==10)ready=output.source==GL_SOURCE_SDL;
        if(frame==60)moving=output.source==GL_SOURCE_SDL&&std::abs(output.yaw_degrees)>.01;
        if(frame==66)stopped=output.source==GL_SOURCE_NONE&&output.yaw_degrees==0;
        demo_ui_gamepad(demo_selected_pad(context.get(),reader));
        ImGui_ImplSDLRenderer3_NewFrame();ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
        if(frame==60)navigation=(io.BackendFlags&ImGuiBackendFlags_HasGamepad)!=0;
        if(frame==66)stopped&=(io.BackendFlags&ImGuiBackendFlags_HasGamepad)==0;
        gl_panel_draw(panel,io.DisplaySize.x,io.DisplaySize.y,1);
        ImGui::Render();SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);SDL_Delay(10);
    }
    std::cout<<"F10="<<opened<<'/'<<closed<<" ready="<<ready<<" moving="<<moving<<" navigation="<<navigation<<" stopped="<<stopped<<'\n';
    if(!opened||!closed||!ready||!moving||!navigation||!stopped)result=3;
    demo_ui_gamepad(nullptr);ImGui_ImplSDLRenderer3_Shutdown();ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();pad.disconnect();
    gl_panel_destroy(panel);gl_sdl_destroy(reader);context.reset();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return result;
}


