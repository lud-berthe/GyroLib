#include "../examples/tps/pause_menu.hpp"
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/panel.h>
#include <gyrolib/runtime.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui_internal.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(0)
int main(int argc,char** argv)try{
#ifdef GL_BUNDLED_RUNTIME
    CHECK(gl_runtime_prepare()==GL_OK);
#endif
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_VIDEO));const bool capture=argc>1;
    auto* window=SDL_CreateWindow("Inheritance UI test",1024,720,SDL_WINDOW_HIDDEN);CHECK(window);
    auto* renderer=SDL_CreateRenderer(window,nullptr);CHECK(renderer);
    for(bool native:{false,true}){
        gyrolib::Context context;auto* c=context.get();tps::Host host;CHECK(host.setup(c));host.paused=host.gyro_menu=true;
        tps::PauseMenu menu;menu.tab_id=uint64_t(tps::AimSniper)+1;
        gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;e.caps={0xffffffffu,3,3,3,3,1,1};
        std::strcpy(e.name,"Synthetic controller");CHECK(gl_register_endpoint(c,&e)==GL_OK);
        uint64_t now=1000000000;const auto update=[&]{now+=16000000;
            gl_sample sample{now,now,{0,0,0},{0,1,0}};gl_submit_sample(c,1,&sample);gl_controls controls{};controls.timestamp_ns=now;gl_submit_controls(c,1,&controls);
            gl_set_gameplay_context_state(c,tps::AimSniper,1,1);gl_host_state state{};state.focused=state.camera_allowed=1;gl_output output{};CHECK(gl_update(c,now,&state,&output)==GL_OK);};
        for(int n=0;n<4;++n)update();auto* panel=gl_panel_create(c,nullptr);CHECK(panel);
        if(!native)gl_panel_function_key(panel,10,1,0);
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.Fonts->AddFontDefaultVector();
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
        io.DisplaySize={1024,720};io.DeltaTime=1.f/60;ImGui_ImplSDLRenderer3_Init(renderer);io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
        const auto frame=[&]{update();ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
            if(native)menu.draw(c,host,io.DisplaySize,1,true);else gl_panel_draw(panel,io.DisplaySize.x,io.DisplaySize.y,1);
            ImGui::Render();SDL_SetRenderDrawColor(renderer,10,20,30,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);};
        const auto key=[&](ImGuiKey k){io.AddKeyEvent(k,true);frame();io.AddKeyEvent(k,false);frame();};
        const auto root_window=[&]{return ImGui::FindWindowByName(native?"Pause###DemoPause":"GyroLib###GyroLibPanel");};
        const auto child_window=[&]()->ImGuiWindow*{for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,native?"NativeRows":"/Settings"))return w;return nullptr;};
        for(int size:{0,1})for(const char* language:{"en","fr","de","es","it","pt"}){
            io.DisplaySize=size?ImVec2(3840,2160):ImVec2(1024,720);SDL_SetWindowSize(window,int(io.DisplaySize.x),int(io.DisplaySize.y));
            gl_set_language(c,language);frame();frame();frame();auto* root=root_window();auto* child=child_window();CHECK(root&&child);
            CHECK(root->ScrollMax.y==0);CHECK(child->ScrollMax.x==0);
            CHECK(root->Pos.x>=0&&root->Pos.y>=0&&root->Pos.x+root->Size.x<=io.DisplaySize.x&&root->Pos.y+root->Size.y<=io.DisplaySize.y);
            if(capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);CHECK(surface);
                const auto file=std::string(native?"inherit-native-":"inherit-panel-")+language+(size?"-4k.bmp":"-1024.bmp");
                CHECK(SDL_SaveBMP(surface,file.c_str()));SDL_DestroySurface(surface);}
        }
        io.DisplaySize={1024,720};SDL_SetWindowSize(window,1024,720);gl_set_language(c,"en");frame();frame();
        auto* root=root_window();auto* child=child_window();CHECK(root&&child);
        const auto navigate=[&](ImGuiID id){
            for(int n=0;n<180&&GImGui->NavId!=id;++n)key(ImGuiKey_Tab);
            if(GImGui->NavId!=id)std::cerr<<(native?"native":"panel")<<" target "<<id<<" nav "<<GImGui->NavId<<'\n';
            CHECK(GImGui->NavId==id);
        };
        const char* sensitivity="context.206.sensitivity_x";
        const auto seed=native?child->GetID("profile"):child->ID;
        const auto marker=ImHashStr("##inherit",0,ImHashStr(sensitivity,0,ImHashStr(sensitivity,0,seed)));
        ImGui::FocusWindow(child);frame();navigate(marker);
        gl_setting_inheritance_info inherited{};gl_setting_inheritance(c,sensitivity,&inherited);CHECK(inherited.overridden);
        key(ImGuiKey_Space);gl_setting_inheritance(c,sensitivity,&inherited);CHECK(!inherited.overridden);
        double value{};gl_setting_get(c,sensitivity,&value);CHECK(value==2.5);
        gl_setting_set(c,sensitivity,1.5);frame();key(ImGuiKey_GamepadFaceDown);gl_setting_inheritance(c,sensitivity,&inherited);CHECK(!inherited.overridden);
        // Select None through the real parent dropdown; values are frozen.
        navigate(child->GetID("##parent"));key(ImGuiKey_Space);key(ImGuiKey_Home);key(ImGuiKey_Enter);frame();
        CHECK(gl_get_context_parent(c,tps::AimSniper)==0);gl_setting_get(c,sensitivity,&value);CHECK(value==2.5);
        // Footer action restores both the demo's local sniper sensitivity and its parent.
        const auto recommended=ImHashStr(gl_text(c,"recommended"),0,root->GetID("settings.recommended"));
        navigate(recommended);key(ImGuiKey_Space);frame();CHECK(gl_get_context_parent(c,tps::AimSniper)==tps::AimStandard);
        gl_setting_get(c,sensitivity,&value);CHECK(value==1.0);
        // Reset remains a separate library-default action; recommendations survive.
        navigate(ImHashStr(gl_text(c,"reset"),0,root->GetID("settings.reset")));key(ImGuiKey_Space);frame();
        CHECK(!gl_get_context_parent(c,tps::AimSniper));gl_setting_get(c,sensitivity,&value);CHECK(value==2.5&&gl_has_recommended_settings(c));
        // A gyro-less controller disables activation; unplug hides the configuration.
        // Reconnection must restore the same tab and saved profile.
        const auto activation=ImHashStr("##value",0,ImHashStr("context.206.gyro.activation",0,seed));
        navigate(activation);frame();
        const auto rect=GImGui->NavWindow->NavRectRel[0];const auto pos=GImGui->NavWindow->Pos;
        const ImVec2 center(pos.x+(rect.Min.x+rect.Max.x)*.5f,pos.y+(rect.Min.y+rect.Max.y)*.5f);
        const auto click_activation=[&]{io.AddMousePosEvent(center.x,center.y);frame();
            io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
        e.caps.gyro=0;CHECK(gl_register_endpoint(c,&e)==GL_OK);frame();click_activation();
        CHECK(GImGui->OpenPopupStack.empty());
        e.connected=0;CHECK(gl_register_endpoint(c,&e)==GL_OK);frame();
        CHECK(!gyro_profile_widgets::controller_connected(c));frame();CHECK(!child_window());
        gl_setting_get(c,sensitivity,&value);CHECK(value==2.5);
        click_activation();CHECK(GImGui->OpenPopupStack.empty());
        for(int n=0;n<12;++n){key(ImGuiKey_Tab);CHECK(GImGui->NavId!=activation&&GImGui->NavId!=child->GetID("##parent")&&GImGui->NavId!=recommended);}
        if(capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);CHECK(surface);
            CHECK(SDL_SaveBMP(surface,native?"disconnected-native.bmp":"disconnected-panel.bmp"));SDL_DestroySurface(surface);}
        e.connected=1;e.caps.gyro=1;CHECK(gl_register_endpoint(c,&e)==GL_OK);frame();frame();
        CHECK(gyro_profile_widgets::controller_connected(c));CHECK(child_window());navigate(activation);key(ImGuiKey_Space);
        CHECK(!GImGui->OpenPopupStack.empty());key(ImGuiKey_Escape);
        ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();gl_panel_destroy(panel);
    }
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();std::cout<<"Inheritance UI: real panel/native controls, keyboard/gamepad, recommendations and EN/FR bounds passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
