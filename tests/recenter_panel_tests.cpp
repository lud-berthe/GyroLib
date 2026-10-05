// Actual supplied panel, synthetic endpoints, hidden renderer. --capture saves
// review images; never opens a real controller or modifies a user's INI.
#include <gyrolib/gyrolib.hpp>
#include "../src/detail/internal.hpp"
#include <gyrolib/panel.h>
#include <gyrolib/runtime.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdlrenderer3.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(int argc,char** argv)try{
#ifdef GL_BUNDLED_RUNTIME
    CHECK(gl_runtime_prepare()==GL_OK);
#endif
    SDL_SetMainReady();CHECK(SDL_Init(SDL_INIT_VIDEO));
    for(const auto size:{ImVec2(1024,720),ImVec2(1600,1100),ImVec2(3840,2160)})for(const char* lang:{"en","fr","de","es","it","pt"}){
        auto* window=SDL_CreateWindow("GyroLib panel preview",int(size.x),int(size.y),SDL_WINDOW_HIDDEN);CHECK(window);
        auto* renderer=SDL_CreateRenderer(window,nullptr);CHECK(renderer);
        gyrolib::Context context;auto* c=context.get();gl_set_language(c,lang);
        const gl_gameplay_context view{1,"Camera","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
        gl_set_recenter_step_callback(c,[](void*,double){},nullptr);
        gl_set_host_capabilities(c,GL_HOST_NATIVE_STICK_SUPPRESSION);
        gl_endpoint endpoint{};endpoint.id=endpoint.physical_id=1;endpoint.connected=1;endpoint.source=GL_SOURCE_STEAM;
        endpoint.caps.gyro=endpoint.caps.accelerometer=1;endpoint.caps.buttons=0xffffffffu;endpoint.caps.sticks=3;endpoint.caps.touchpads=GL_SINGLE;
        std::strcpy(endpoint.name,"DualSense Wireless Controller");CHECK(gl_register_endpoint(c,&endpoint)==GL_OK);
        CHECK(gl_capture_recommended_settings(c)==GL_OK);
        uint64_t now=1000000000;gl_host_state host{};host.focused=host.camera_allowed=1;gl_output output{};
        auto update=[&]{now+=16000000;gl_sample sample{now,now,{0,0,0},{0,1,0}};CHECK(gl_submit_sample(c,endpoint.id,&sample)==GL_OK);
            gl_controls controls{};controls.timestamp_ns=now;CHECK(gl_submit_controls(c,endpoint.id,&controls)==GL_OK);
            gl_set_gameplay_context_state(c,1,1,1);CHECK(gl_update(c,now,&host,&output)==GL_OK);};
        for(int i=0;i<25;++i)update();CHECK(output.source==GL_SOURCE_STEAM);
        auto visible=[&](const char* key){for(uint32_t i=0;i<gl_menu_setting_count(c);++i){gl_setting_info s{};CHECK(gl_setting_at(c,i,&s)==GL_OK);if(!std::strcmp(s.id,key))return s.visible!=0;}throw std::runtime_error("Missing setting");};
        for(const auto* key:{"calibration.automatic","calibration.begin","calibration.cancel"})CHECK(!visible(key));
        auto* panel=gl_panel_create(c,nullptr);CHECK(panel);gl_set_panel_open(c,1);
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=size;io.DeltaTime=1.f/60;
        io.Fonts->AddFontDefaultVector();CHECK(ImGui_ImplSDLRenderer3_Init(renderer));
        auto draw=[&]{update();ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();gl_panel_draw(panel,size.x,size.y,1);ImGui::Render();
            for(auto* w:GImGui->Windows)if(w->Active&&!(w->Flags&ImGuiWindowFlags_ChildWindow)&&!(w->Flags&ImGuiWindowFlags_Tooltip)){
                CHECK(w->Pos.x>=0&&w->Pos.y>=0&&w->Pos.x+w->Size.x<=size.x&&w->Pos.y+w->Size.y<=size.y);
                CHECK(w->ScrollMax.y==0&&w->ScrollMax.x==0);
            }
        };
        draw();draw();ImGuiWindow* child=nullptr;for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"/Settings"))child=w;CHECK(child);
        const auto key=ImHashStr("advanced-open",0,child->GetID("context.1.camera.recenter_button"));
        const auto off_height=child->ContentSize.y;child->StateStorage.SetBool(key,true);draw();draw();CHECK(child->ContentSize.y==off_height);
        CHECK(gl_setting_set(c,"context.1.camera.recenter_button",9)==GL_OK);
        CHECK(gl_setting_set(c,"context.1.camera.recenter_duration_ms",150)==GL_OK);
        draw();draw();CHECK(child->ContentSize.y>off_height);CHECK(child->ScrollMax.x==0);
        if(argc>1&&std::strcmp(argv[1],"--capture")==0&&size.x==1600){
            SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
            auto* surface=SDL_RenderReadPixels(renderer,nullptr);CHECK(surface);char name[128];std::snprintf(name,sizeof(name),"panel-steam-recenter-%s.bmp",lang);CHECK(SDL_SaveBMP(surface,name));SDL_DestroySurface(surface);
        }
        CHECK(gl_setting_set(c,"context.1.camera.recenter_button",0)==GL_OK);draw();draw();CHECK(child->ContentSize.y==off_height);
        CHECK(gl_forget_endpoint(c,1)==GL_OK);endpoint.source=GL_SOURCE_SDL;CHECK(gl_register_endpoint(c,&endpoint)==GL_OK);
        draw();draw();CHECK(output.source==GL_SOURCE_SDL);CHECK(visible("calibration.automatic"));CHECK(visible("calibration.begin"));
        CHECK(gl_setting_set(c,"context.1.camera.recenter_button",9)==GL_OK);draw();draw();
        if(argc>1&&std::strcmp(argv[1],"--capture")==0&&size.x==1600){
            SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
            auto* surface=SDL_RenderReadPixels(renderer,nullptr);CHECK(surface);char name[128];std::snprintf(name,sizeof(name),"panel-sdl-recenter-%s.bmp",lang);CHECK(SDL_SaveBMP(surface,name));SDL_DestroySurface(surface);
        }
        CHECK(!visible("context.1.input.steam_mouse"));
        const float before_mouse=child->ContentSize.y;
        c->virtual_mouse=[](void*,gl_output*,bool,uint32_t,uint32_t){};
        CHECK(gl_set_endpoint_steam_input(c,1,1)==GL_OK);draw();draw();
        CHECK(visible("context.1.input.steam_mouse"));CHECK(child->ContentSize.y>before_mouse);CHECK(child->ScrollMax.x==0);
        if(argc>1&&std::strcmp(argv[1],"--capture")==0&&size.x==1600){
            ImGui::SetScrollY(child,child->ScrollMax.y);draw();draw();
            SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
            auto* surface=SDL_RenderReadPixels(renderer,nullptr);CHECK(surface);char name[128];std::snprintf(name,sizeof(name),"panel-steam-mouse-%s.bmp",lang);CHECK(SDL_SaveBMP(surface,name));SDL_DestroySurface(surface);
        }
        CHECK(gl_set_endpoint_steam_input(c,1,0)==GL_OK);c->virtual_mouse=nullptr;draw();draw();CHECK(!visible("context.1.input.steam_mouse"));CHECK(child->ContentSize.y==before_mouse);
        ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();gl_panel_destroy(panel);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    }
    SDL_Quit();std::puts("Recenter expansion and Steam calibration replacement: six languages, three sizes passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
