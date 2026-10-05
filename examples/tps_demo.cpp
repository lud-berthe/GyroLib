#include <gyrolib/gyrolib.hpp>
#include <gyrolib/panel.h>
#include <gyrolib/runtime.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include "sdl_demo_input.hpp"
#include "tps/ui.hpp"
#include "tps/pause_menu.hpp"
#include "tps/audio.hpp"
#include "tps/input_log.hpp"
#include <array>
#include <cstring>
#include <iostream>

static_assert(tps::ButtonSouth==int(SDL_GAMEPAD_BUTTON_SOUTH)&&tps::ButtonEast==int(SDL_GAMEPAD_BUTTON_EAST)&&
    tps::ButtonWest==int(SDL_GAMEPAD_BUTTON_WEST)&&tps::ButtonNorth==int(SDL_GAMEPAD_BUTTON_NORTH)&&
    tps::ButtonStart==int(SDL_GAMEPAD_BUTTON_START));

namespace {
constexpr uint64_t simulated_id=0x900099;
double stick(Sint16 raw){double value=raw<0?raw/32768.0:raw/32767.0;return std::abs(value)<.16?0:std::copysign((std::abs(value)-.16)/.84,value);}
const char* trigger_name(SDL_Gamepad* pad,bool right){
    if(!pad)return right?"right trigger":"left trigger";const auto type=SDL_GetRealGamepadType(pad);
    if(type==SDL_GAMEPAD_TYPE_PS3||type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5)return right?"R2":"L2";
    if(type==SDL_GAMEPAD_TYPE_XBOXONE||type==SDL_GAMEPAD_TYPE_XBOX360)return right?"RT":"LT";
    if(type>=SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO&&type<=SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)return right?"ZR":"ZL";
    return right?"right trigger":"left trigger";
}
void key(SDL_Scancode code){
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=code;event.key.down=true;SDL_PushEvent(&event);
    event.type=SDL_EVENT_KEY_UP;event.key.down=false;SDL_PushEvent(&event);
}
void button(Uint8 which,bool down){SDL_Event event{};event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;event.button.button=which;event.button.down=down;SDL_PushEvent(&event);}
void scripted_events(unsigned frame,int w,int h,bool keep_inventory=false){
    if(frame==16)button(SDL_BUTTON_RIGHT,true);
    if(frame==32)button(SDL_BUTTON_RIGHT,false);
    if(frame==40||(frame==65&&!keep_inventory))key(SDL_SCANCODE_TAB);
    if(frame==42){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=w*.68f;e.motion.y=h*.5f;SDL_PushEvent(&e);}
    if(frame==45)button(SDL_BUTTON_LEFT,true);if(frame==46)button(SDL_BUTTON_LEFT,false);
    if(frame==49||frame==54||frame==58){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;
        e.motion.x=w*(frame==49?.30f:.68f);e.motion.y=h*(frame==58?.5f:.75f);SDL_PushEvent(&e);}
    if(frame==50||frame==55||frame==59)button(SDL_BUTTON_LEFT,true);
    if(frame==51||frame==56||frame==60)button(SDL_BUTTON_LEFT,false);
    if(!keep_inventory&&frame==67)button(SDL_BUTTON_RIGHT,true);
    if(!keep_inventory&&frame==69)key(SDL_SCANCODE_V);
    if(frame==70)button(SDL_BUTTON_RIGHT,false);
    if(frame==72||frame==85)key(SDL_SCANCODE_F10);
    if(frame==92||frame==96)key(SDL_SCANCODE_ESCAPE);
}
}
int main(int argc,char** argv){
#ifdef GL_BUNDLED_RUNTIME
    if(gl_runtime_prepare()!=GL_OK){std::cerr<<gl_runtime_error()<<'\n';return 1;}
#endif
    SDL_SetMainReady();
    const char* benchmark=nullptr;bool cpu_scene=false;
    bool smoke=false,capture=false,synthetic=false,four_k=false,cursor_settings=false,occlusion=false,calibration_capture=false,combat_capture=false;
    int preview_weapon=tps::Rifle;bool look_up=false,look_down=false,native_capture=false,french=false;
    for(int i=1;i<argc;++i){if(std::strcmp(argv[i],"--benchmark")==0&&i+1<argc){benchmark=argv[++i];continue;}cpu_scene|=std::strcmp(argv[i],"--cpu-scene")==0;smoke|=std::strcmp(argv[i],"--smoke")==0;capture|=std::strcmp(argv[i],"--capture")==0;
        synthetic|=std::strcmp(argv[i],"--synthetic")==0;four_k|=std::strcmp(argv[i],"--4k")==0;
        cursor_settings|=std::strcmp(argv[i],"--capture-cursor-settings")==0;occlusion|=std::strcmp(argv[i],"--capture-occlusion")==0;
        calibration_capture|=std::strcmp(argv[i],"--capture-calibration")==0;combat_capture|=std::strcmp(argv[i],"--capture-combat")==0;
        look_up|=std::strcmp(argv[i],"--capture-look-up")==0;look_down|=std::strcmp(argv[i],"--capture-look-down")==0;
        native_capture|=std::strcmp(argv[i],"--capture-native-menu")==0;french|=std::strcmp(argv[i],"--french")==0;
        if(std::strcmp(argv[i],"--capture-pistol")==0){preview_weapon=tps::Pistol;combat_capture=true;}
        if(std::strcmp(argv[i],"--capture-shotgun")==0){preview_weapon=tps::Shotgun;combat_capture=true;}}
    capture|=cursor_settings||occlusion||calibration_capture||combat_capture||look_up||look_down||native_capture;
    const bool scripted=smoke||capture||benchmark;synthetic|=scripted;
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)){std::cerr<<SDL_GetError();return 1;}
    auto* window=SDL_CreateWindow("GyroLib Demo",four_k?3840:1440,four_k?2160:900,
        SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(scripted?SDL_WINDOW_HIDDEN:0));
    // Deferred D3D11 commands preserve SDL's rendering state around the 3D pass.
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE,"1");
    auto* renderer=window?SDL_CreateRenderer(window,nullptr):nullptr;
    if(!renderer){std::cerr<<SDL_GetError();SDL_Quit();return 1;}
    if(benchmark)SDL_Log("Demo benchmark renderer: %s",SDL_GetRendererName(renderer));
    SDL_SetWindowMinimumSize(window,1024,720);SDL_SetRenderVSync(renderer,scripted?0:1);
    gyrolib::Context context;tps::Host host;tps::Scene scene;tps::Audio audio;tps::PauseMenu pause_menu;
    scene.force_cpu=cpu_scene;
    if(!scripted)audio.open();
    if(!context||!host.setup(context.get())){std::cerr<<"Could not create demo contexts\n";return 2;}
    host.equip(preview_weapon);
    if(scripted&&french)gl_set_language(context.get(),"fr");
    if(look_up)host.pitch=80;if(look_down)host.pitch=-80;
    if(occlusion){host.player={-6.85,0,0};host.pitch=-6;host.targets[0].position={-6,.72,8};}
    DemoInputLog input_log;tps::Performance performance;
    if(benchmark)performance.open(benchmark);
    if(!scripted){
        // Diagnostics use the per-user folder; settings live beside GyroLib.
        const char* pref=SDL_GetPrefPath("GyroLib","TPSDemo");
        if(pref&&!synthetic)performance.open((std::string(pref)+"performance.csv").c_str());
        if(pref&&!synthetic)input_log.open(context.get(),std::string(pref)+"input-diagnostics.log");
        if(gl_initialize_settings(context.get(),nullptr,nullptr)!=GL_OK){
            host.notification="Could not load or create gyrolib.ini";host.notification_time=8;}
    }
    auto* reader=synthetic?nullptr:gl_sdl_create(context.get(),1);
    if(reader&&gl_sdl_attach_window(reader,window)!=GL_OK)
        SDL_Log("GyroLib window input: %s",gl_sdl_error(reader));
    if(!synthetic&&!reader){std::cerr<<SDL_GetError();return 2;}
    if(synthetic){
        gl_endpoint e{};e.id=e.physical_id=simulated_id;e.source=GL_SOURCE_SDL;e.connected=1;
        e.caps.gyro=e.caps.accelerometer=1;e.caps.sticks=GL_LEFT|GL_RIGHT;
        std::snprintf(e.name,sizeof(e.name),"Simulated gyro (arrow keys)");gl_register_endpoint(context.get(),&e);gl_select_device(context.get(),e.physical_id);
    }
    auto* panel=gl_panel_create(context.get(),nullptr);if(!panel)return 2;
    IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.Fonts->AddFontDefaultVector();
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();ImGui_ImplSDL3_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer3_Init(renderer);demo_ui_gamepad(nullptr);
    std::array<bool,SDL_SCANCODE_COUNT> keys{};bool mouse_aim=false,mouse_fire=false,running=true,relative=false,cursor_hidden=false;
    bool previous_fire=false;SDL_JoystickID previous_pad=0;
    uint64_t previous_time=SDL_GetTicksNS();unsigned frame=0;int result=0;
    double explore_delta=0,aim_delta=0,sniper_delta=0,sniper_zoom_delta=0,inventory_yaw=0,inventory_pitch=0;unsigned inventory_callbacks=0;
    bool saw_inventory=false,saw_selection=false,saw_pistol=false,saw_shotgun=false,saw_pause=false,saw_settings=false,saw_return=false;
    host.measure_performance=performance.active();
    uint64_t performance_report=0,last_gpu_sample=0;
    while(running){
        const auto frame_start=performance.active()?tps::performance_clock():0;
        uint64_t stage_start=frame_start;
        const bool measured=performance.active()&&(!benchmark||frame%120>=20);
        const auto mark=[&](tps::Performance::Stage stage){if(performance.active()){const auto end=tps::performance_clock();
            if(measured)performance.add(stage,end-stage_start);stage_start=end;}};
        int w=0,h=0;SDL_GetWindowSize(window,&w,&h);if(scripted&&!benchmark)scripted_events(frame,w,h,cursor_settings);
        tps::Input input{};input.focused=scripted||(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)!=0;
        SDL_Event event{};
        while(SDL_PollEvent(&event)){
            ImGui_ImplSDL3_ProcessEvent(&event);
            if(event.type==SDL_EVENT_QUIT)running=false;
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST&&!scripted){keys.fill(false);mouse_aim=false;mouse_fire=false;}
            if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP){
                auto code=event.key.scancode;if(code>=0&&code<SDL_SCANCODE_COUNT)keys[code]=event.key.down;
                if(event.key.down&&!event.key.repeat){
                    if(demo_panel_key(panel,event.key)){}
                    else if(code==SDL_SCANCODE_ESCAPE&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)){
                        if(gl_panel_open(context.get()))gl_set_panel_open(context.get(),0);else input.pause_press=true;}
                    else if(code==SDL_SCANCODE_TAB||code==SDL_SCANCODE_I)input.inventory_press=true;
                    else if(code==SDL_SCANCODE_RETURN)input.confirm_press=true;
                    else if(code==SDL_SCANCODE_R)input.reload_press=true;
                    else if(code==SDL_SCANCODE_HOME)input.recenter_press=true;
                    else if(code==SDL_SCANCODE_V)input.zoom_press=true;
                }
            }
            if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP){
                if(event.button.button==SDL_BUTTON_RIGHT)mouse_aim=event.button.down;
                if(event.button.button==SDL_BUTTON_LEFT){mouse_fire=event.button.down;input.fire_press|=event.button.down;}
            }
            if(event.type==SDL_EVENT_MOUSE_MOTION&&!gl_panel_open(context.get())){
                if(host.inventory){input.pointer_moved=true;input.pointer_x=event.motion.x/std::max(w,1);input.pointer_y=event.motion.y/std::max(h,1);}
                else if(relative&&!host.paused){input.mouse_yaw+=event.motion.xrel*.12;input.mouse_pitch-=event.motion.yrel*.12;}
            }
            if(event.type==SDL_EVENT_MOUSE_WHEEL&&event.wheel.y!=0)input.zoom_press=true;
        }
        mark(tps::Performance::Events);
        const uint64_t now=scripted?1000000000ull+frame*16666667ull:SDL_GetTicksNS();
        const double dt=scripted?1.0/60:std::clamp((now-previous_time)*1e-9,0.0,.05);previous_time=now;
        demo_ui_gamepad(nullptr);if(reader)gl_sdl_poll(reader,now);
        mark(tps::Performance::Acquisition);
        auto* pad=reader?demo_selected_pad(context.get(),reader):nullptr;
        input.aim=mouse_aim;input.fire_held=mouse_fire;
        input.move_x=double(keys[SDL_SCANCODE_D])-keys[SDL_SCANCODE_A];input.move_z=double(keys[SDL_SCANCODE_W])-keys[SDL_SCANCODE_S];
        if(!synthetic){input.look_x=double(keys[SDL_SCANCODE_RIGHT])-keys[SDL_SCANCODE_LEFT];input.look_y=double(keys[SDL_SCANCODE_UP])-keys[SDL_SCANCODE_DOWN];}
        if(pad){
            uint32_t buttons=0;for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT&&i<32;++i)if(SDL_GetGamepadButton(pad,static_cast<SDL_GamepadButton>(i)))buttons|=1u<<i;
            bool fire=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>7000;
            auto instance=SDL_GetGamepadID(pad);if(instance!=previous_pad||!input.focused)previous_fire=fire;previous_pad=instance;
            input.controller_id=uint64_t(instance);input.controller_buttons=buttons;
            if(host.paused&&ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))input.controller_buttons&=~(1u<<SDL_GAMEPAD_BUTTON_EAST);
            input.fire_press|=fire&&!previous_fire;input.fire_held|=fire;previous_fire=fire;
            input.aim|=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)>7000;
            input.move_x+=stick(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX));input.move_z-=stick(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY));
            input.controller_look_x=stick(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTX));input.controller_look_y=-stick(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTY));
            input.look_x+=input.controller_look_x;input.look_y+=input.controller_look_y;
        }else{previous_pad=0;previous_fire=false;}
        if(synthetic){
            const double x=double(keys[SDL_SCANCODE_RIGHT])-keys[SDL_SCANCODE_LEFT],y=double(keys[SDL_SCANCODE_UP])-keys[SDL_SCANCODE_DOWN];
            gl_sample sample{now,now,{float(y*30),smoke?-12.f:float(-x*30),0},{0,1,0}};
            gl_controls controls{};controls.timestamp_ns=now;controls.right_x=float(input.look_x);controls.right_y=float(input.look_y);
            gl_submit_sample(context.get(),simulated_id,&sample);gl_submit_controls(context.get(),simulated_id,&controls);
        }
        if(frame==40){inventory_yaw=host.yaw;inventory_pitch=host.pitch;inventory_callbacks=host.camera_callbacks;}
        if(combat_capture&&frame==11){const auto eye=host.eye();host.targets[0].position={eye.x,1.7,6};
            host.pitch=std::atan2(1.7-eye.y,6-eye.z)/tps::rad;input.fire_press=true;}
        if(combat_capture&&frame==(preview_weapon==tps::Shotgun?33u:13u))input.reload_press=true;
        if(capture&&!cursor_settings&&frame==67){
            // Inspect a distant static target at both magnifications in captures.
            const auto to=host.targets[13].position-host.weapon_point({0,.17,.10});
            host.yaw=std::atan2(to.x,to.z)/tps::rad;host.pitch=std::atan2(to.y,std::hypot(to.x,to.z))/tps::rad;
        }
        if(benchmark){const unsigned part=frame/120;
            host.yaw=std::sin(frame*.012)*75;host.pitch=part==1?-75:part==2?75:-8;
            host.inventory=part==4;host.paused=false;input.aim=part==3;
            host.equip(part==3?tps::Sniper:tps::Rifle);gl_set_panel_open(context.get(),part==5);
        }
        stage_start=performance.active()?tps::performance_clock():0;
        if(!host.step(context.get(),now,dt,input)){std::cerr<<"Demo host update failed\n";result=3;break;}
        mark(tps::Performance::Host);
        if(measured)performance.add(tps::Performance::Core,host.gyro_update_ns);
        audio.update(host,input.focused&&!host.inventory&&!host.paused&&!gl_panel_open(context.get()));
        if(reader)gl_sdl_apply_feedback(reader);
        input_log.update(context.get(),reader,host.output,now,host);
        if(calibration_capture&&frame==74)gl_action(context.get(),"calibration.begin");
        if(calibration_capture&&frame==80)gl_action(context.get(),"calibration.cancel");
        if(native_capture&&frame==93)host.gyro_menu=true;
        if(smoke){
            if(frame==12)explore_delta=host.output.yaw_degrees;
            if(frame==16){aim_delta=host.output.yaw_degrees;if(!host.aiming)result=4;}
            if(frame==32&&host.aiming)result=4;
            if(frame==55)saw_inventory=host.inventory&&host.yaw==inventory_yaw&&host.pitch==inventory_pitch&&host.camera_callbacks==inventory_callbacks&&host.output.gyro_active&&host.cursor_x!=.5;
            if(frame==46)saw_selection=host.equipped==1;
            if(frame==51)saw_pistol=host.equipped==tps::Pistol;
            if(frame==56)saw_shotgun=host.equipped==tps::Shotgun;
            if(frame==66)saw_return=!host.inventory&&host.output.gyro_active&&host.camera_callbacks>inventory_callbacks;
            if(frame==68){sniper_delta=host.output.yaw_degrees;if(host.mode()!=tps::AimSniper||!host.scoped())result=4;}
            if(frame==69){sniper_zoom_delta=host.output.yaw_degrees;if(host.sniper_zoom!=1||host.fov()!=10)result=4;}
            if(frame==70&&(host.scoped()||host.mode()!=tps::Explore))result=4;
            if(frame==74)saw_settings=gl_panel_open(context.get())&&!host.output.gyro_active;
            if(frame==94)saw_pause=host.paused&&!host.output.gyro_active;
        }
        const bool wants_relative=!scripted&&input.focused&&!host.inventory&&!host.paused&&!gl_panel_open(context.get());
        if(wants_relative!=relative){SDL_SetWindowRelativeMouseMode(window,wants_relative);relative=wants_relative;}
        const bool hide_cursor=!scripted&&input.focused&&host.inventory&&!gl_panel_open(context.get());
        if(cursor_hidden!=hide_cursor){if(hide_cursor)SDL_HideCursor();else SDL_ShowCursor();cursor_hidden=hide_cursor;}
        demo_ui_gamepad(pad);
        SDL_SetRenderDrawColor(renderer,11,20,31,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_NewFrame();ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
        auto* draw=ImGui::GetBackgroundDrawList();
        stage_start=performance.active()?tps::performance_clock():0;
        if(!scene.draw(draw,renderer,host,io.DisplaySize)){std::cerr<<"Demo rendering failed: "<<SDL_GetError()<<'\n';result=6;ImGui::EndFrame();break;}
        mark(tps::Performance::Scene);
        if(scene.gpu.measured_sample()!=last_gpu_sample){if(measured)performance.add(tps::Performance::GPU,scene.gpu.measured_ns());last_gpu_sample=scene.gpu.measured_sample();}
        const auto label=[&](int button,const char* fallback){return pad?gl_get_button_label(context.get(),button):fallback;};
        // Snapshot the selected controller's labels for this frame.
        const std::string inventory_button=label(SDL_GAMEPAD_BUTTON_NORTH,"north button");
        const std::string reload_button=label(SDL_GAMEPAD_BUTTON_WEST,"west button");
        const std::string confirm_button=label(SDL_GAMEPAD_BUTTON_SOUTH,"south button");
        tps::ui(draw,host,context.get(),io.DisplaySize,synthetic,inventory_button.c_str(),trigger_name(pad,false),trigger_name(pad,true),
            reload_button.c_str(),confirm_button.c_str(),input.focused&&(input.confirm_press||input.fire_press||host.controller_confirm));
        pause_menu.draw(context.get(),host,io.DisplaySize,SDL_GetWindowDisplayScale(window),input.focused);
        gl_panel_draw(panel,io.DisplaySize.x,io.DisplaySize.y,SDL_GetWindowDisplayScale(window));
        ImGui::Render();ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
        mark(tps::Performance::UI);
        if(capture&&(frame==12||frame==28||frame==56||frame==68||frame==69||frame==78||(combat_capture&&frame==38)||(calibration_capture&&frame==82)||(native_capture&&(frame==92||frame==95)))){
            const char* mode=frame==92?"pause":frame==95?"native-menu":frame==12?"exploration":frame==28?"aim-standard":frame==38?"reload":frame==56?"inventory":frame==68?"aim-sniper":frame==69?"aim-sniper-zoom":frame==82?"cancelled":"settings";
            const auto path=std::string(look_up?"demo-look-up-":look_down?"demo-look-down-":preview_weapon==tps::Pistol?"demo-pistol-":preview_weapon==tps::Shotgun?"demo-shotgun-":combat_capture?"demo-combat-":calibration_capture?"demo-calibration-":occlusion?"demo-occlusion-":cursor_settings?"demo-cursor-":"demo-")+mode+(french?"-fr":"")+(four_k?"-4k":"")+".bmp";
            auto* surface=SDL_RenderReadPixels(renderer,nullptr);
            if(!surface||!SDL_SaveBMP(surface,path.c_str())){std::cerr<<SDL_GetError();result=5;}
            if(surface)SDL_DestroySurface(surface);
        }
        stage_start=performance.active()?tps::performance_clock():0;
        SDL_RenderPresent(renderer);mark(tps::Performance::Present);
        if(measured)performance.add(tps::Performance::Frame,tps::performance_clock()-frame_start);
        ++frame;
        if(benchmark){if(frame%120==0){constexpr const char* names[]={"exploration","floor","ceiling","scope","inventory","settings"};performance.report(names[frame/120-1]);}if(frame>=720)running=false;}
        else if(scripted){SDL_Delay(1);if(frame>=101)running=false;}
        else if(performance.active()&&now>=performance_report){performance.report(gl_panel_open(context.get())?"settings":host.inventory?"inventory":host.scoped()?"scope":"gameplay");performance_report=now+2000000000ull;}

    }
    if(smoke){
        const bool ratios=std::abs(explore_delta-.5)<.005&&std::abs(aim_delta-.5)<.005&&std::abs(sniper_delta-.2)<.005&&std::abs(sniper_zoom_delta-.0992346)<.001;
        std::cout<<"Demo: exploration="<<explore_delta<<" rifle="<<aim_delta<<" sniper="<<sniper_delta<<" inventory="<<saw_inventory<<" selected="<<saw_selection
            <<" zoom2="<<sniper_zoom_delta<<" pistol="<<saw_pistol<<" shotgun="<<saw_shotgun<<" resumed="<<saw_return<<" F10="<<saw_settings<<" paused="<<saw_pause<<'\n';
        if(!ratios||!saw_inventory||!saw_selection||!saw_pistol||!saw_shotgun||!saw_return||!saw_settings||!saw_pause)result=4;
    }
    SDL_SetWindowRelativeMouseMode(window,false);SDL_ShowCursor();
    demo_ui_gamepad(nullptr);ImGui_ImplSDLRenderer3_Shutdown();ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();
    audio.close();scene.release();gl_panel_destroy(panel);gl_sdl_destroy(reader);input_log.close();context.reset();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return result;
}
