// Exercise the host-owned widgets using actual ImGui mouse/keyboard/gamepad
// events. This test has no dependency on gyrolib_panel or its implementation.
#include "../examples/tps/pause_menu.hpp"
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/runtime.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui_internal.h>
#include <imgui_impl_sdlrenderer3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv){
#ifdef GL_BUNDLED_RUNTIME
    if(gl_runtime_prepare()!=GL_OK){std::fprintf(stderr,"Test runtime: %s\n",gl_runtime_error());return 1;}
#endif
    SDL_SetMainReady();
    const bool capture=argc>1&&std::strcmp(argv[1],"--capture")==0;
    if(!SDL_Init(SDL_INIT_VIDEO))return 1;
    auto* window=SDL_CreateWindow("Native gyro menu test",1024,720,SDL_WINDOW_HIDDEN);
    auto* renderer=window?SDL_CreateRenderer(window,nullptr):nullptr;if(!renderer)return 2;
    int result=0;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.Fonts->AddFontDefaultVector();
    io.DisplaySize={1024,720};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDLRenderer3_Init(renderer);io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    const auto path=std::filesystem::absolute("native-menu-test.ini");
    try{
        gyrolib::Context c;tps::Host host;tps::PauseMenu menu;require(host.setup(c.get()),"host setup failed");
        gl_set_menu_key(c.get(),0);host.paused=true;
        gl_endpoint e{};e.id=e.physical_id=1;e.connected=1;e.source=GL_SOURCE_SDL;e.caps={0xffffffffu,3,3,3,3,1,1};
        std::snprintf(e.name,sizeof(e.name),"Synthetic controller");gl_register_endpoint(c.get(),&e);
        uint64_t now=1000000000;
        const auto update=[&]{now+=10000000;gl_sample s{now,now,{0,0,0},{0,1,0}};
            gl_controls controls{};controls.timestamp_ns=now;gl_submit_controls(c.get(),1,&controls);
            gl_trigger_input triggers{now,GL_LEFT|GL_RIGHT,0,0};gl_submit_trigger_input(c.get(),1,&triggers);
            gl_submit_sample(c.get(),1,&s);require(host.step(c.get(),now,.01,{}),"host update failed");};
        for(int i=0;i<4;++i)update();
        const auto frame=[&](auto draw){
            update();ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();draw();ImGui::Render();
            SDL_SetRenderDrawColor(renderer,10,20,30,255);SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
        };
        const auto native=[&]{menu.draw(c.get(),host,io.DisplaySize,1,true);};
        const auto key=[&](ImGuiKey code,auto draw){io.AddKeyEvent(code,true);frame(draw);io.AddKeyEvent(code,false);frame(draw);};
        frame(native);frame(native);
        key(ImGuiKey_DownArrow,native);key(ImGuiKey_Enter,native);frame(native);
        require(host.paused&&host.gyro_menu&&!gl_panel_open(c.get()),"keyboard must open host gyro settings without the F10 panel");
        require(gl_get_menu_key(c.get())==0,"native menu must work with the library shortcut disabled");
        for(const char* language:{"en","fr","de","es","it","pt"}){
            gl_set_language(c.get(),language);frame(native);frame(native);
            auto* w=ImGui::FindWindowByName("Pause###DemoPause");require(w&&w->Active,"native menu did not render");
            require(w->Pos.x>=0&&w->Pos.y>=0&&w->Pos.x+w->Size.x<=1024&&w->Pos.y+w->Size.y<=720,"native window exceeded viewport");
            require(w->ScrollMax.y==0,"native footer must fit without scrolling the whole window");
            if(capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);
                const auto file=std::string("native-menu-")+language+"-1024.bmp";
                require(surface&&SDL_SaveBMP(surface,file.c_str()),"capture failed");SDL_DestroySurface(surface);}
        }
        // Expand each real native family independently, including all its
        // conditional fields. The fixed footer remains outside the scroll area.
        gl_setting_set(c.get(),"context.101.gyro.smoothing_ms",25);
        gl_setting_set(c.get(),"context.101.gyro.fast_sensitivity_x",8);
        gl_setting_set(c.get(),"context.101.flick.mode",GL_FLICK_ON);
        gl_setting_set(c.get(),"context.101.flick.snap",2);
        gl_setting_set(c.get(),"context.101.camera.recenter_button",9);
        const char* parents[]={"context.101.gyro.smoothing_ms","context.101.gyro.acceleration","context.101.flick.mode","context.101.gyro.activation","context.101.camera.recenter_button"};
        gl_setting_set(c.get(),"context.101.gyro.activation",GL_HOLD_DISABLE);
        gl_setting_set(c.get(),"context.101.activation.temporary_invert",1);
        gl_setting_set(c.get(),"context.101.activation.trackball",1);
        for(int size=0;size<2;++size)for(const char* language:{"en","fr","de","es","it","pt"})for(int group=0;group<5;++group){
            io.DisplaySize=size?ImVec2(3840,2160):ImVec2(1024,720);
            SDL_SetWindowSize(window,int(io.DisplaySize.x),int(io.DisplaySize.y));
            gl_set_language(c.get(),language);frame(native);
            bool expanded=false;
            for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"NativeRows")){
                const auto table=w->GetID("profile");
                for(int n=0;n<4;++n)w->StateStorage.SetBool(ImHashStr("advanced-open",0,ImHashStr(parents[n],0,table)),n==group);
                expanded=true;
            }
            require(expanded,"advanced section not found");frame(native);frame(native);
            for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"NativeRows"))ImGui::SetScrollY(w,w->ScrollMax.y);
            frame(native);frame(native);
            auto* w=ImGui::FindWindowByName("Pause###DemoPause");
            require(w&&w->ScrollMax.y==0,"advanced section must scroll independently of the footer");
            if(capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);
                const auto file=std::string("native-options-")+std::to_string(group)+"-"+language+(size?"-4k.bmp":"-1024.bmp");
                require(surface&&SDL_SaveBMP(surface,file.c_str()),"advanced capture failed");SDL_DestroySurface(surface);}
        }
        io.DisplaySize={1024,720};SDL_SetWindowSize(window,1024,720);
        // Reach the real Smoothing expander by keyboard navigation, then use
        // keyboard and gamepad activation. Opening one family must leave the
        // others closed, and changing language must retain its stable state.
        frame(native);ImGuiWindow* child=nullptr;
        for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"NativeRows"))child=w;
        require(child,"native row window not found");const auto table=child->GetID("profile");
        const auto parent=ImHashStr(parents[0],0,table);
        const auto toggle=ImHashStr("###advanced-toggle",0,parent),open_id=ImHashStr("advanced-open",0,parent);
        for(const auto* p:parents)child->StateStorage.SetBool(ImHashStr("advanced-open",0,ImHashStr(p,0,table)),false);
        gl_setting_set(c.get(),parents[0],0);gl_setting_set(c.get(),parents[1],0);gl_setting_set(c.get(),parents[2],GL_FLICK_OFF);
        for(int n=0;n<4;++n)frame(native);const auto off_height=child->ContentSize.y;
        for(int n=0;n<3;++n)child->StateStorage.SetBool(ImHashStr("advanced-open",0,ImHashStr(parents[n],0,table)),true);
        for(int n=0;n<4;++n)frame(native);
        if(child->ContentSize.y!=off_height)std::cerr<<"Off content heights "<<off_height<<" / "<<child->ContentSize.y<<'\n';
        require(child->ContentSize.y==off_height,"Off features rendered their child settings");
        const auto off_flick=ImHashStr("##value",0,ImHashStr(parents[2],0,table));
        const auto off_toggle=ImHashStr("###advanced-toggle",0,ImHashStr(parents[2],0,table));
        for(int n=0;n<60&&GImGui->NavId!=off_flick;++n)key(ImGuiKey_DownArrow,native);
        require(GImGui->NavId==off_flick,"keyboard cannot reach disabled flick");key(ImGuiKey_RightArrow,native);
        require(GImGui->NavId!=off_toggle,"Off flick exposed an expander");
        for(const auto* p:parents)child->StateStorage.SetBool(ImHashStr("advanced-open",0,ImHashStr(p,0,table)),false);
        gl_setting_set(c.get(),parents[0],25);gl_setting_set(c.get(),"context.101.gyro.fast_sensitivity_x",8);gl_setting_set(c.get(),parents[2],GL_FLICK_ON);
        frame(native);
        const auto slider=ImHashStr("##value",0,parent);
        for(int i=0;i<60&&GImGui->NavId!=slider;++i)key(ImGuiKey_UpArrow,native);
        require(GImGui->NavId==slider,"keyboard cannot reach smoothing slider");
        key(ImGuiKey_LeftArrow,native);
        if(GImGui->NavId!=toggle)std::cerr<<"Expected toggle "<<toggle<<"; nav "<<GImGui->NavId<<" in "<<(GImGui->NavWindow?GImGui->NavWindow->Name:"none")<<'\n';
        require(GImGui->NavId==toggle,"keyboard cannot reach smoothing options on the left");
        key(ImGuiKey_Space,native);require(child->StateStorage.GetBool(open_id),"keyboard cannot expand smoothing options");
        require(!child->StateStorage.GetBool(ImHashStr("advanced-open",0,ImHashStr(parents[1],0,table))),"smoothing also expanded acceleration");
        key(ImGuiKey_GamepadFaceDown,native);require(!child->StateStorage.GetBool(open_id),"gamepad cannot collapse smoothing options");
        // Collapsing rows can animate navigation scrolling. Let the real widget
        // settle before deriving its screen position for a mouse click.
        for(int n=0;n<15;++n)frame(native);
        const auto rect=ImGui::WindowRectRelToAbs(GImGui->NavWindow,GImGui->NavWindow->NavRectRel[GImGui->NavLayer]);
        io.AddMousePosEvent((rect.Min.x+rect.Max.x)*.5f,(rect.Min.y+rect.Max.y)*.5f);frame(native);
        io.AddMouseButtonEvent(0,true);frame(native);io.AddMouseButtonEvent(0,false);frame(native);
        if(!child->StateStorage.GetBool(open_id)){
            std::cerr<<"Click at "<<io.MousePos.x<<','<<io.MousePos.y<<"; clip "<<child->InnerClipRect.Min.x<<','<<child->InnerClipRect.Min.y<<' '
                <<child->InnerClipRect.Max.x<<','<<child->InnerClipRect.Max.y<<"; scroll "<<child->Scroll.y<<'/'<<child->ScrollMax.y
                <<"; hovered "<<GImGui->HoveredId<<"; toggle "<<toggle<<"; nav "<<GImGui->NavId<<'\n';
            if(capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);if(surface){SDL_SaveBMP(surface,"native-nav-click.bmp");SDL_DestroySurface(surface);}}
        }
        require(child->StateStorage.GetBool(open_id),"mouse cannot expand smoothing options");
        gl_set_language(c.get(),"en");frame(native);
        require(child->StateStorage.GetBool(open_id),"changing language lost the open family");
        // Use actual input on enabled Flick's left expander. Opening it must
        // not change its selected input mode.
        child->StateStorage.SetBool(open_id,false);frame(native);
        const auto flick_parent=ImHashStr(parents[2],0,table);
        const auto flick_combo=ImHashStr("##value",0,flick_parent);
        const auto flick_toggle=ImHashStr("###advanced-toggle",0,flick_parent);
        const auto flick_open=ImHashStr("advanced-open",0,flick_parent);
        // From another left-side expander, vertical navigation can land
        // directly on this expander instead of its neighbouring combo.
        for(int i=0;i<60&&GImGui->NavId!=flick_combo&&GImGui->NavId!=flick_toggle;++i)key(ImGuiKey_DownArrow,native);
        require(GImGui->NavId==flick_combo||GImGui->NavId==flick_toggle,"keyboard cannot reach Flick Stick");
        if(GImGui->NavId==flick_combo)key(ImGuiKey_LeftArrow,native);
        require(GImGui->NavId==flick_toggle,"Flick expander is unreachable");
        key(ImGuiKey_Space,native);require(child->StateStorage.GetBool(flick_open),"Flick options cannot expand");
        double flick_mode=1;gl_setting_get(c.get(),parents[2],&flick_mode);
        require(flick_mode==double(GL_FLICK_ON),"expanding Flick options enabled it");
        key(ImGuiKey_GamepadFaceDown,native);require(!child->StateStorage.GetBool(flick_open),"Flick options cannot collapse");
        // The long-press blocking setting shares the button row. Drive the actual
        // inline checkbox rather than a separate copy of the native widget.
        gl_setting_set(c.get(),"context.101.gyro.activation",GL_HOLD);
        gl_setting_set(c.get(),"context.101.activation.button",tps::ButtonWest+1);
        gl_setting_set(c.get(),"context.101.activation.block_long_press",0);frame(native);
        const auto button_parent=ImHashStr("context.101.activation.button",0,table);
        const auto button_combo=ImHashStr("##value",0,button_parent);
        const auto blocker_parent=ImHashStr("context.101.activation.block_long_press",0,table);
        const auto blocker_checkbox=ImHashStr(gl_text(c.get(),"ui.block_long_press"),0,blocker_parent);
        for(int n=0;n<60&&GImGui->NavId!=button_combo&&GImGui->NavId!=blocker_checkbox;++n)key(ImGuiKey_UpArrow,native);
        require(GImGui->NavId==button_combo||GImGui->NavId==blocker_checkbox,"keyboard cannot reach inline long-press blocking option");
        if(GImGui->NavId==button_combo)key(ImGuiKey_RightArrow,native);
        require(GImGui->NavId==blocker_checkbox,"long-press blocking checkbox must be to the right of the button");
        key(ImGuiKey_Space,native);double tap=0;gl_setting_get(c.get(),"context.101.activation.block_long_press",&tap);
        require(tap==1,"inline checkbox did not update the shared setting");
        key(ImGuiKey_GamepadFaceDown,native);gl_setting_get(c.get(),"context.101.activation.block_long_press",&tap);
        require(tap==0,"gamepad cannot toggle the inline long-press blocking option");
        gl_setting_set(c.get(),"context.101.gyro.activation",GL_HOLD_DISABLE);frame(native);
        const auto hold_parent=ImHashStr(parents[3],0,table);
        const auto hold_combo=ImHashStr("##value",0,hold_parent),hold_toggle=ImHashStr("###advanced-toggle",0,hold_parent);
        const auto hold_open=ImHashStr("advanced-open",0,hold_parent);
        for(int n=0;n<60&&GImGui->NavId!=hold_combo&&GImGui->NavId!=hold_toggle;++n)key(ImGuiKey_UpArrow,native);
        require(GImGui->NavId==hold_combo||GImGui->NavId==hold_toggle,"keyboard cannot reach Hold to disable");
        if(GImGui->NavId==hold_combo)key(ImGuiKey_LeftArrow,native);
        require(GImGui->NavId==hold_toggle,"Hold to disable expander must be on the left");
        key(ImGuiKey_Space,native);require(child->StateStorage.GetBool(hold_open),"hold options cannot expand");
        key(ImGuiKey_GamepadFaceDown,native);require(!child->StateStorage.GetBool(hold_open),"hold options cannot collapse");
        gl_setting_set(c.get(),"context.101.gyro.activation",GL_HOLD);frame(native);
        if(capture)for(int size=0;size<2;++size)for(const char* language:{"en","fr"}){
            io.DisplaySize=size?ImVec2(3840,2160):ImVec2(1024,720);SDL_SetWindowSize(window,int(io.DisplaySize.x),int(io.DisplaySize.y));
            gl_set_language(c.get(),language);ImGui::SetScrollY(child,0);frame(native);frame(native);
            auto* surface=SDL_RenderReadPixels(renderer,nullptr);
            const auto file=std::string("native-button-filter-")+language+(size?"-4k.bmp":"-1024.bmp");
            require(surface&&SDL_SaveBMP(surface,file.c_str()),"button-filter capture failed");SDL_DestroySurface(surface);
        }
        gl_setting_set(c.get(),"context.101.gyro.space",GL_SPACE_LOCAL_YAW_ROLL);
        gl_setting_set(c.get(),"context.101.gyro.acceleration",0);
        for(int size=0;size<2;++size)for(const char* language:{"en","fr"}){
            io.DisplaySize=size?ImVec2(3840,2160):ImVec2(1024,720);SDL_SetWindowSize(window,int(io.DisplaySize.x),int(io.DisplaySize.y));
            gl_set_language(c.get(),language);
            for(const auto* id:{"context.101.gyro.invert_x","context.101.gyro.invert_roll","context.101.gyro.invert_y"})gl_setting_set(c.get(),id,0);
            ImGui::SetScrollY(child,0);frame(native);frame(native);ImGui::FocusWindow(child);
            ImGui::NavInitWindow(child,true);frame(native);frame(native);
            const auto x_slider=ImHashStr("##value",0,ImHashStr("context.101.sensitivity_x",0,table));
            const auto yaw_check=ImHashStr(gl_text(c.get(),"ui.invert.yaw.short"),0,ImHashStr("context.101.gyro.invert_x",0,table));
            const auto roll_check=ImHashStr(gl_text(c.get(),"ui.invert.roll.short"),0,ImHashStr("context.101.gyro.invert_roll",0,table));
            for(int n=0;n<80&&GImGui->NavId!=x_slider&&GImGui->NavId!=yaw_check&&GImGui->NavId!=roll_check;++n)key(ImGuiKey_DownArrow,native);
            for(int n=0;n<3&&GImGui->NavId!=x_slider;++n)key(ImGuiKey_LeftArrow,native);
            require(GImGui->NavId==x_slider,"native X sensitivity is unreachable");
            key(ImGuiKey_RightArrow,native);require(GImGui->NavId==yaw_check,"Yaw inversion must follow the X slider");
            key(ImGuiKey_Space,native);double yaw=0,roll=0,pitch=0;
            gl_setting_get(c.get(),"context.101.gyro.invert_x",&yaw);gl_setting_get(c.get(),"context.101.gyro.invert_roll",&roll);
            require(yaw==1&&roll==0,"Yaw checkbox changed the wrong inversion");
            key(ImGuiKey_RightArrow,native);require(GImGui->NavId==roll_check,"Roll inversion must follow Yaw inversion");
            key(ImGuiKey_GamepadFaceDown,native);gl_setting_get(c.get(),"context.101.gyro.invert_roll",&roll);
            gl_setting_get(c.get(),"context.101.gyro.invert_y",&pitch);require(roll==1&&pitch==0,"Roll checkbox changed Pitch or failed gamepad input");
            const auto bounds=ImGui::WindowRectRelToAbs(child,child->NavRectRel[ImGuiNavLayer_Main]);
            require(bounds.Max.x<=child->InnerClipRect.Max.x+1&&child->ScrollMax.x==0,"native inversion checkboxes exceed the row");
            if(capture){gl_setting_set(c.get(),"context.101.gyro.invert_roll",0);ImGui::FocusWindow(nullptr);}
            ImGui::SetScrollY(child,0);frame(native);frame(native);
            if(capture){auto* surface=SDL_RenderReadPixels(renderer,nullptr);
                const auto file=std::string("native-yaw-roll-")+language+(size?"-4k.bmp":"-1024.bmp");
                require(surface&&SDL_SaveBMP(surface,file.c_str()),"Yaw/Roll capture failed");SDL_DestroySurface(surface);}
        }
        io.DisplaySize={1024,720};SDL_SetWindowSize(window,1024,720);gl_set_language(c.get(),"en");frame(native);
        for(int n=0;n<4;++n)frame(native);const float enabled_height=child->ContentSize.y;
        gl_setting_set(c.get(),"context.101.gyro.activation",GL_GYRO_OFF);
        for(int n=0;n<4;++n)frame(native);
        require(child->ContentSize.y<enabled_height,"gyro Off did not hide gyro settings and activators");
        if(capture)for(int size=0;size<2;++size)for(const char* language:{"en","fr"}){
            io.DisplaySize=size?ImVec2(3840,2160):ImVec2(1024,720);SDL_SetWindowSize(window,int(io.DisplaySize.x),int(io.DisplaySize.y));
            gl_set_language(c.get(),language);ImGui::SetScrollY(child,0);frame(native);frame(native);
            auto* surface=SDL_RenderReadPixels(renderer,nullptr);
            const auto file=std::string("native-gyro-off-")+language+(size?"-4k.bmp":"-1024.bmp");
            require(surface&&SDL_SaveBMP(surface,file.c_str()),"gyro-off capture failed");SDL_DestroySurface(surface);
        }
        io.DisplaySize={1024,720};SDL_SetWindowSize(window,1024,720);gl_set_language(c.get(),"en");frame(native);
        for(uint64_t tab:{102,206,207})for(uint32_t n=0;n<gl_menu_tab_setting_count(c.get(),tab);++n){gl_setting_info info{};
            gl_menu_tab_setting_at(c.get(),tab,n,&info);
            if(std::strstr(info.id,"gyro.zoom_compensation"))require(!info.visible,"demo must not expose zoom compensation");}
        // The row helper below is exactly the adapter used by the native screen.
        // Its metadata is re-read after every change, as it is in the real menu.
        gl_set_settings_path(c.get(),path.string().c_str());
        gl_setting_set(c.get(),"context.101.gyro.activation",GL_HOLD_DISABLE);
        gl_setting_set(c.get(),"context.101.activation.temporary_invert",1);
        const char* setting="context.101.activation.temporary_invert";ImVec2 lo{},hi{};bool focus=false;
        const auto widget=[&]{
            gl_setting_info info{};bool found=false;
            for(uint32_t i=0;i<gl_menu_tab_setting_count(c.get(),102);++i){gl_setting_info s{};
                gl_menu_tab_setting_at(c.get(),102,i,&s);if(!std::strcmp(s.id,setting)){info=s;found=true;break;}}
            require(found&&info.available,"test row is unavailable");
            ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({500,200});
            if(focus)ImGui::SetNextWindowFocus();
            ImGui::Begin("Native row input",nullptr,ImGuiWindowFlags_NoSavedSettings);
            if(focus)ImGui::SetKeyboardFocusHere();
            menu.widget(c.get(),info,360);lo=ImGui::GetItemRectMin();hi=ImGui::GetItemRectMax();
            ImGui::End();focus=false;
        };
        focus=true;frame(widget);frame(widget);
        io.AddMousePosEvent((lo.x+hi.x)/2,(lo.y+hi.y)/2);frame(widget);
        io.AddMouseButtonEvent(0,true);frame(widget);io.AddMouseButtonEvent(0,false);frame(widget);
        double value=1;gl_setting_get(c.get(),setting,&value);require(value==0,"mouse checkbox must update the public setting");
        key(ImGuiKey_Tab,widget);
        double before=0;gl_setting_get(c.get(),setting,&before);key(ImGuiKey_Space,widget);
        gl_setting_get(c.get(),setting,&value);require(value==1-before,"keyboard checkbox must update the public setting");
        before=value;key(ImGuiKey_GamepadFaceDown,widget);
        gl_setting_get(c.get(),setting,&value);require(value==1-before,"gamepad checkbox must update the public setting");
        setting="context.101.sensitivity_x";frame(widget);frame(widget);
        io.AddMousePosEvent(lo.x+(hi.x-lo.x)*.7f,(lo.y+hi.y)/2);frame(widget);
        io.AddMouseButtonEvent(0,true);frame(widget);io.AddMouseButtonEvent(0,false);frame(widget);
        gl_setting_get(c.get(),setting,&value);require(value>10&&value<18,"native slider must use metadata bounds and steps");
        require(gl_get_settings_save_result(c.get())==GL_OK,"native edits were not saved");
        gyrolib::Context restored;tps::Host second;second.setup(restored.get());
        require(gl_load_settings(restored.get(),path.string().c_str())==GL_OK,"native settings file cannot be loaded");
        double saved=0;gl_setting_get(restored.get(),setting,&saved);require(saved==value,"native and F10/core values must share persistence");
        // An edit through another frontend must be reflected by fresh model data.
        gl_setting_set(c.get(),setting,6.3);frame(widget);gl_setting_get(c.get(),setting,&value);require(std::abs(value-6.3)<1e-8,"external edit was overwritten by a native stale value");
        gl_setting_set(c.get(),"context.101.gyro.acceleration",1);
        setting="context.101.gyro.fast_sensitivity_x";frame(widget);frame(widget);
        io.AddMousePosEvent(lo.x+(hi.x-lo.x)*.4f,(lo.y+hi.y)/2);frame(widget);
        io.AddMouseButtonEvent(0,true);frame(widget);io.AddMouseButtonEvent(0,false);frame(widget);
        gl_setting_get(c.get(),"context.101.gyro.acceleration",&value);require(value==4,"editing an enabled acceleration detail must select Custom");
        for(uint32_t i=0;i<gl_menu_tab_setting_count(c.get(),310);++i){gl_setting_info s{};gl_menu_tab_setting_at(c.get(),310,i,&s);
            require(!s.visible||!std::strstr(s.id,"flick."),"native cursor tab must not expose flick stick");}
        host.gyro_menu=false;host.paused=false;frame(native);host.paused=true;frame(native);frame(native);
        key(ImGuiKey_GamepadDpadDown,native);key(ImGuiKey_GamepadFaceDown,native);frame(native);
        require(host.gyro_menu&&host.paused,"gamepad must navigate from pause to gyro settings");
        require(gl_forget_endpoint(c.get(),1)==GL_OK,"forget synthetic SDL controller");
        e.source=GL_SOURCE_STEAM;require(gl_register_endpoint(c.get(),&e)==GL_OK,"register synthetic Steam controller");
        for(int i=0;i<40;++i)frame(native);
        gl_diagnostics diagnostics{};gl_get_diagnostics(c.get(),&diagnostics);
        require(diagnostics.source==GL_SOURCE_STEAM,"Steam motion source did not qualify");
        for(const char* language:{"en","fr","de","es","it","pt"}){
            gl_set_language(c.get(),language);frame(native);frame(native);
            auto* w=ImGui::FindWindowByName("Pause###DemoPause");
            require(w&&w->ScrollMax.y==0&&w->ScrollMax.x==0,"native Steam calibration message exceeded the footer");
        }
        std::cout<<"Native menu: keyboard/gamepad navigation, mouse/keyboard/gamepad edits, metadata, persistence and EN/FR bounds passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=3;}
    std::error_code error;std::filesystem::remove(path,error);
    ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return result;
}
