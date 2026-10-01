// Render the real F10 panel and its real tooltip helper at the viewport edge.
// No physical input, saved settings, or installed extra demo.
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/panel.h>
#include <gyrolib/runtime.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdlrenderer3.h>
#include "../src/detail/panel_help.hpp"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){
#ifdef GL_BUNDLED_RUNTIME
    if(gl_runtime_prepare()!=GL_OK){std::fprintf(stderr,"Test runtime: %s\n",gl_runtime_error());return 1;}
#endif
    SDL_SetMainReady();
    const bool capture=argc>1&&std::strcmp(argv[1],"--capture")==0;
    if(!SDL_Init(SDL_INIT_VIDEO))return 1;
    bool ok=true;
    for(int size=0;size<2;++size)for(const char* language:{"en","fr"}){
        const int width=size?3840:1024,height=size?2160:720;const float scale=size?2.f:1.f;
        auto* window=SDL_CreateWindow("GyroLib help regression",width,height,SDL_WINDOW_HIDDEN);
        auto* renderer=window?SDL_CreateRenderer(window,nullptr):nullptr;if(!renderer)return 2;
        gyrolib::Context context;gl_set_language(context.get(),language);gl_set_panel_open(context.get(),1);
        const gl_gameplay_context view{1,"Camera","",0};gl_register_gameplay_context(context.get(),&view);
        gl_set_recenter_callback(context.get(),[](void*){},nullptr);
        gl_endpoint endpoint{};endpoint.id=endpoint.physical_id=1;endpoint.source=GL_SOURCE_SDL;endpoint.connected=1;
        endpoint.caps.sticks=endpoint.caps.touchpads=3;std::strcpy(endpoint.name,"Steam Controller (synthetic)");
        endpoint.caps.buttons=0xffffffffu;
        gl_register_endpoint(context.get(),&endpoint);gl_select_device(context.get(),1);
        gl_set_host_capabilities(context.get(),GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION|GL_HOST_SHORT_PRESS_FILTER);
        auto* panel=gl_panel_create(context.get(),nullptr);
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.Fonts->AddFontDefaultVector();
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize=ImVec2(float(width),float(height));io.DeltaTime=1.f/60;
        ImGui_ImplSDLRenderer3_Init(renderer);
        // Check actual glyph coverage, not just valid UTF-8: missing apostrophes
        // used to render as '?' despite correct source text.
        auto check_text=[&](const char* text){
            while(text&&*text){unsigned cp=0;int bytes=ImTextCharFromUtf8(&cp,text,nullptr);if(bytes<=0){ok=false;break;}text+=bytes;
                if(cp>=32&&!io.Fonts->Fonts[0]->IsGlyphInFont(static_cast<ImWchar>(cp))){
                    std::printf("Missing %s font glyph U+%04X\n",language,cp);ok=false;
                }
            }
        };
        ImVec2 hover(float(width)-140*scale,float(height)-45*scale);bool found=false;
        for(int frame=0;frame<8;++frame){
            const uint64_t now=1000000000ull+uint64_t(frame)*16000000;
            gl_controls controls{};controls.timestamp_ns=now;gl_submit_controls(context.get(),1,&controls);
            gl_trigger_input triggers{now,GL_LEFT|GL_RIGHT,0,0};gl_submit_trigger_input(context.get(),1,&triggers);
            gl_flick_input flick{now,3,0,0,0,0,0};gl_submit_flick_input(context.get(),1,&flick);
            gl_set_gameplay_context_state(context.get(),1,1,1);
            gl_host_state host{};host.focused=host.camera_allowed=1;gl_output output{};gl_update(context.get(),now,&host,&output);
            io.AddMousePosEvent(hover.x,hover.y);ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
            if(frame==0){
                for(unsigned n=0;n<gl_setting_count();++n){gl_setting_info setting{};gl_setting_at(context.get(),n,&setting);
                    check_text(setting.label);check_text(setting.description);
                    for(unsigned i=0;i<gl_menu_choice_count(context.get(),setting.id);++i){gl_choice choice{};
                        gl_choice_at(context.get(),setting.id,i,&choice);check_text(choice.label);check_text(gl_choice_description(context.get(),setting.id,choice.value));}
                }
                for(const char* key:{"LocalAxisAngle","ui.operation_failed","ui.auto_calibration","ui.source.waiting"})check_text(gl_text(context.get(),key));
            }
            gl_panel_draw(panel,float(width),float(height),scale);
            ImGui::PushFont(nullptr,18*scale);
            ImGui::SetNextWindowPos(ImVec2(float(width)-280*scale,float(height)-85*scale));
            ImGui::SetNextWindowSize(ImVec2(275*scale,80*scale));
            ImGui::Begin("Hovered option",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
            ImGui::Selectable(gl_text(context.get(),"Smoothing"),false,0,ImVec2(250*scale,50*scale));
            const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();hover=ImVec2((lo.x+hi.x)/2,(lo.y+hi.y)/2);
            gyrolib_panel_detail::help(gl_text(context.get(),"description.Smoothing"));
            ImGui::End();ImGui::PopFont();ImGui::Render();
            SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
            if(frame==7){
                for(auto* w:GImGui->Windows)if(w->Active&&(w->Flags&ImGuiWindowFlags_Tooltip)){
                    found=true;ok&=w->Pos.x>=0&&w->Pos.y>=0&&w->Pos.x+w->Size.x<=width&&w->Pos.y+w->Size.y<=height;
                    ok&=w->Size.y>2*ImGui::GetFontSize(); // description actually wraps
                    std::printf("%s %dx%d tooltip=(%.0f,%.0f %.0fx%.0f)\n",language,width,height,w->Pos.x,w->Pos.y,w->Size.x,w->Size.y);
                }
                if(capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);char path[80];
                    std::snprintf(path,sizeof(path),"panel-help-%s-%d.bmp",language,width);if(surface){SDL_SaveBMP(surface,path);SDL_DestroySurface(surface);}}
            }
            SDL_RenderPresent(renderer);
        }
        ok&=found;
        gl_setting_set(context.get(),"context.1.gyro.smoothing_ms",25);
        gl_setting_set(context.get(),"context.1.gyro.fast_sensitivity_x",8);
        gl_setting_set(context.get(),"context.1.flick.mode",GL_FLICK_ON);
        gl_setting_set(context.get(),"context.1.flick.snap",2);
        io.AddMousePosEvent(-100,-100);
        bool advanced=false;
        const char* parents[]={"context.1.gyro.smoothing_ms","context.1.gyro.acceleration","context.1.flick.mode","context.1.gyro.activation"};
        gl_setting_set(context.get(),"context.1.gyro.activation",GL_HOLD_DISABLE);
        gl_setting_set(context.get(),"context.1.activation.temporary_invert",1);
        gl_setting_set(context.get(),"context.1.activation.trackball",1);
        for(int group=0;group<4;++group)for(int frame=0;frame<6;++frame){
            for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"/Settings")){
                for(int n=0;n<4;++n)w->StateStorage.SetBool(ImHashStr("advanced-open",0,w->GetID(parents[n])),n==group);
                advanced=true;if(frame>1)ImGui::SetScrollY(w,w->ScrollMax.y);
            }
            ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();gl_panel_draw(panel,float(width),float(height),scale);ImGui::Render();
            SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
            if(frame==5&&capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);char path[80];
                std::snprintf(path,sizeof(path),"panel-options-%d-%s-%d.bmp",group,language,width);if(surface){SDL_SaveBMP(surface,path);SDL_DestroySurface(surface);}}
            SDL_RenderPresent(renderer);
        }
        ok&=advanced;
        // No expander is reachable while Flick is Off. Enabled Flick exposes
        // its expander on the left, without changing the selected input mode.
        ImGuiWindow* child=nullptr;
        for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"/Settings"))child=w;
        if(!child)ok=false;
        else{
            for(const auto* parent:parents)child->StateStorage.SetBool(ImHashStr("advanced-open",0,child->GetID(parent)),false);
            const auto parent=child->GetID(parents[2]);
            const auto combo=ImHashStr("##value",0,parent),toggle=ImHashStr("###advanced-toggle",0,parent);
            const auto open_id=ImHashStr("advanced-open",0,parent);
            const auto draw=[&]{ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
                gl_panel_draw(panel,float(width),float(height),scale);ImGui::Render();};
            const auto key=[&](ImGuiKey code){io.AddKeyEvent(code,true);draw();io.AddKeyEvent(code,false);draw();};
            ImGui::FocusWindow(child);draw();draw();
            gl_setting_set(context.get(),parents[0],0);gl_setting_set(context.get(),parents[1],0);gl_setting_set(context.get(),parents[2],GL_FLICK_OFF);
            draw();draw();const auto off_height=child->ContentSize.y;
            for(int n=0;n<3;++n)child->StateStorage.SetBool(ImHashStr("advanced-open",0,child->GetID(parents[n])),true);
            draw();draw();ok&=child->ContentSize.y==off_height;
            for(int n=0;n<60&&GImGui->NavId!=combo;++n)key(ImGuiKey_DownArrow);
            ok&=GImGui->NavId==combo;key(ImGuiKey_RightArrow);ok&=GImGui->NavId!=toggle;
            for(const auto* p:parents)child->StateStorage.SetBool(ImHashStr("advanced-open",0,child->GetID(p)),false);
            gl_setting_set(context.get(),parents[0],25);gl_setting_set(context.get(),"context.1.gyro.fast_sensitivity_x",8);gl_setting_set(context.get(),parents[2],GL_FLICK_ON);draw();draw();
            for(int n=0;n<60&&GImGui->NavId!=combo;++n)key(ImGuiKey_DownArrow);
            ok&=GImGui->NavId==combo;key(ImGuiKey_LeftArrow);ok&=GImGui->NavId==toggle;
            key(ImGuiKey_Space);ok&=child->StateStorage.GetBool(open_id);
            double mode=1;ok&=gl_setting_get(context.get(),parents[2],&mode)==GL_OK&&mode==double(GL_FLICK_ON);
            if(!ok)std::printf("Flick expander failed in %s %dx%d\n",language,width,height);
            gl_setting_set(context.get(),"context.1.gyro.activation",GL_HOLD);
            gl_setting_set(context.get(),"context.1.activation.button",3);
            gl_setting_set(context.get(),"context.1.activation.short_press",0);draw();
            const auto button_parent=child->GetID("context.1.activation.button");
            const auto button_combo=ImHashStr("##value",0,button_parent);
            const auto tap_parent=ImHashStr("context.1.activation.short_press",0,button_parent);
            const auto checkbox=ImHashStr(gl_text(context.get(),"ui.short_press.short"),0,tap_parent);
            for(int n=0;n<60&&GImGui->NavId!=button_combo&&GImGui->NavId!=checkbox;++n)key(ImGuiKey_UpArrow);
            ok&=GImGui->NavId==button_combo||GImGui->NavId==checkbox;
            if(GImGui->NavId==button_combo)key(ImGuiKey_RightArrow);
            ok&=GImGui->NavId==checkbox;key(ImGuiKey_Space);
            double tap=0;ok&=gl_setting_get(context.get(),"context.1.activation.short_press",&tap)==GL_OK&&tap==1;
            ImGui::SetScrollY(child,0);draw();draw();
            if(capture){SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
                auto* surface=SDL_RenderReadPixels(renderer,nullptr);char path[80];
                std::snprintf(path,sizeof(path),"panel-button-filter-%s-%d.bmp",language,width);
                if(surface){SDL_SaveBMP(surface,path);SDL_DestroySurface(surface);}}
            if(!ok)std::printf("Inline button checkbox failed in %s %dx%d\n",language,width,height);
            gl_setting_set(context.get(),"context.1.gyro.space",GL_SPACE_LOCAL_YAW_ROLL);
            gl_setting_set(context.get(),"context.1.gyro.acceleration",0);
            for(const auto* id:{"context.1.gyro.invert_x","context.1.gyro.invert_roll","context.1.gyro.invert_y"})gl_setting_set(context.get(),id,0);
            ImGui::SetScrollY(child,0);draw();draw();
            const auto x_parent=child->GetID("context.1.sensitivity_x");
            const auto x_slider=ImHashStr("##value",0,x_parent);
            const auto yaw_check=ImHashStr(gl_text(context.get(),"ui.invert.yaw.short"),0,ImHashStr("context.1.gyro.invert_x",0,x_parent));
            const auto roll_check=ImHashStr(gl_text(context.get(),"ui.invert.roll.short"),0,ImHashStr("context.1.gyro.invert_roll",0,x_parent));
            for(int n=0;n<80&&GImGui->NavId!=x_slider&&GImGui->NavId!=yaw_check&&GImGui->NavId!=roll_check;++n)key(ImGuiKey_UpArrow);
            for(int n=0;n<3&&GImGui->NavId!=x_slider;++n)key(ImGuiKey_LeftArrow);
            ok&=GImGui->NavId==x_slider;key(ImGuiKey_RightArrow);ok&=GImGui->NavId==yaw_check;
            key(ImGuiKey_Space);double yaw=0,roll=0,pitch=0;
            gl_setting_get(context.get(),"context.1.gyro.invert_x",&yaw);gl_setting_get(context.get(),"context.1.gyro.invert_roll",&roll);
            ok&=yaw==1&&roll==0;key(ImGuiKey_RightArrow);ok&=GImGui->NavId==roll_check;
            key(ImGuiKey_Space);gl_setting_get(context.get(),"context.1.gyro.invert_roll",&roll);ok&=roll==1;
            gl_setting_get(context.get(),"context.1.gyro.invert_y",&pitch);ok&=pitch==0;
            const auto bounds=ImGui::WindowRectRelToAbs(child,child->NavRectRel[ImGuiNavLayer_Main]);
            ok&=bounds.Max.x<=child->InnerClipRect.Max.x+1&&child->ScrollMax.x==0;
            if(!ok)std::printf("Independent Yaw/Roll inline inversions failed in %s %dx%d\n",language,width,height);
            if(capture){gl_setting_set(context.get(),"context.1.gyro.invert_roll",0);ImGui::FocusWindow(nullptr);}
            ImGui::SetScrollY(child,0);draw();draw();
            if(capture){SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
                auto* surface=SDL_RenderReadPixels(renderer,nullptr);char path[80];
                std::snprintf(path,sizeof(path),"panel-yaw-roll-%s-%d.bmp",language,width);
                if(surface){SDL_SaveBMP(surface,path);SDL_DestroySurface(surface);}}
            const float enabled_height=child->ContentSize.y;
            gl_setting_set(context.get(),"context.1.gyro.activation",GL_GYRO_OFF);draw();draw();
            ok&=child->ContentSize.y<enabled_height;
            ImGui::SetScrollY(child,0);draw();draw();
            if(capture){SDL_SetRenderDrawColor(renderer,12,22,32,255);SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
                auto* surface=SDL_RenderReadPixels(renderer,nullptr);char path[80];
                std::snprintf(path,sizeof(path),"panel-gyro-off-%s-%d.bmp",language,width);
                if(surface){SDL_SaveBMP(surface,path);SDL_DestroySurface(surface);}}
        }
        ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();gl_panel_destroy(panel);
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    }
    SDL_Quit();return ok?0:3;
}
