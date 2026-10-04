#include <gyrolib/gyrolib.hpp>
#include <gyrolib/steam.h>
#include "../src/detail/projection.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
static void near(double a,double b,double tolerance=1e-5){if(std::abs(a-b)>=tolerance)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
struct Fixture {
    gyrolib::Context context;
    gl_context* c=context.get();uint64_t now=1000000000;
    gl_host_state host{};gl_controls controls{};gl_output out{};
    std::map<uint32_t,std::pair<bool,bool>> states;
    explicit Fixture(bool single=true){host.focused=host.camera_allowed=1;if(single){
        mode(1,"Camera");state(1,true);set("context.1.sensitivity_x",1);set("context.1.sensitivity_y",1);}}
    void set(const char* key,double value){CHECK(gl_setting_set(c,key,value)==GL_OK);}
    void mode(uint32_t id,const char* label="Test command",int priority=0){
        if(id!=1&&states.count(1))state(1,false);
        gl_gameplay_context context{id,label,"Synthetic resolved command state",priority};
        CHECK(gl_register_gameplay_context(c,&context)==GL_OK);state(id,false);
        set(("context."+std::to_string(id)+".gyro.space").c_str(),GL_SPACE_LOCAL_YAW);
    }
    void state(uint32_t id,bool active,bool available=true){states[id]={active,available};}
    void report(){for(auto [id,state]:states)CHECK(gl_set_gameplay_context_state(c,id,state.first,state.second)==GL_OK);}
    void device(uint64_t id=1,unsigned source=GL_SOURCE_SDL,uint64_t physical=7){
        gl_endpoint e{};e.id=id;e.physical_id=physical;e.source=source;e.connected=1;
        e.caps={0xffffffffu,3,3,3,3,1,1};std::strcpy(e.name,"Synthetic test controller");CHECK(gl_register_endpoint(c,&e)==GL_OK);
    }
    void submit(uint64_t id,gl_vec3 gyro={0,30,0},gl_vec3 accel={0,1,0}){
        gl_sample sample{now,now,gyro,accel};CHECK(gl_submit_sample(c,id,&sample)==GL_OK);
        controls.timestamp_ns=now;CHECK(gl_submit_controls(c,id,&controls)==GL_OK);
    }
    gl_output tick(gl_vec3 gyro={0,30,0},uint64_t step=10000000,bool sdl=true,bool steam=false){
        now+=step;if(sdl)submit(1,gyro);if(steam)submit(2,gyro);report();CHECK(gl_update(c,now,&host,&out)==GL_OK);return out;
    }
    void prime(){tick();tick();tick();}
};
static void gamepad_menu_shortcut(){
    constexpr uint32_t back=1u<<4,start=1u<<6,chord=back|start;
    Fixture f(false);f.device();f.host.camera_allowed=0;f.host.paused=f.host.menu_open=1;
    CHECK(gl_get_gamepad_menu_shortcut(f.c)==1);
    CHECK(gl_set_gamepad_menu_shortcut(nullptr,1)==GL_INVALID);
    CHECK(gl_set_gamepad_menu_shortcut(f.c,2)==GL_INVALID);
    const auto press=[&](uint32_t buttons){f.controls.buttons=buttons;f.tick();};
    press(chord);CHECK(!gl_panel_open(f.c)); // already held at connection
    press(0);press(back);CHECK(!gl_panel_open(f.c));press(chord);CHECK(gl_panel_open(f.c));
    for(int i=0;i<10;++i)press(chord);CHECK(gl_panel_open(f.c));
    press(start);press(chord);CHECK(gl_panel_open(f.c)); // must release both
    press(0);press(start);press(chord);CHECK(!gl_panel_open(f.c)); // reverse order
    f.host.focused=0;press(0);press(chord);CHECK(!gl_panel_open(f.c));
    f.host.focused=1;press(chord);CHECK(!gl_panel_open(f.c));
    press(0);press(chord);CHECK(gl_panel_open(f.c));gl_set_panel_open(f.c,0);
    press(0);f.tick({},200000000,false);press(chord);CHECK(!gl_panel_open(f.c)); // stale input
    CHECK(gl_set_gamepad_menu_shortcut(f.c,0)==GL_OK);press(0);press(chord);CHECK(!gl_panel_open(f.c));
    CHECK(gl_set_gamepad_menu_shortcut(f.c,1)==GL_OK);press(chord);CHECK(!gl_panel_open(f.c));
    press(0);press(chord);CHECK(gl_panel_open(f.c));gl_set_panel_open(f.c,0);
    // Duplicate SDL/Steam endpoints cannot toggle twice or substitute another pad.
    f.device(2,GL_SOURCE_STEAM,7);press(0);f.controls.buttons=chord;f.tick({},10000000,true,true);CHECK(gl_panel_open(f.c));
    f.tick({},10000000,true,true);CHECK(gl_panel_open(f.c));gl_set_panel_open(f.c,0);
    f.device(3,GL_SOURCE_SDL,8);CHECK(gl_select_device(f.c,8)==GL_OK);
    f.now+=10000000;f.submit(3);f.tick({},10000000,false);CHECK(!gl_panel_open(f.c));
    f.controls.buttons=0;f.now+=10000000;f.submit(3);f.tick({},10000000,false);
    f.controls.buttons=chord;f.now+=10000000;f.submit(3);f.tick({},10000000,false);CHECK(gl_panel_open(f.c));
    CHECK(gl_disconnect_endpoint(f.c,3)==GL_OK);gl_set_panel_open(f.c,0);press(chord);CHECK(!gl_panel_open(f.c));
    // No gyro is needed, but both logical buttons must be exposed by the input.
    Fixture plain(false);gl_endpoint e{};e.id=1;e.physical_id=7;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.buttons=back;
    CHECK(gl_register_endpoint(plain.c,&e)==GL_OK);plain.prime();plain.controls.buttons=chord;plain.tick();CHECK(!gl_panel_open(plain.c));
    e.caps.buttons=chord;CHECK(gl_register_endpoint(plain.c,&e)==GL_OK);plain.controls.buttons=0;plain.tick();
    plain.controls.buttons=chord;plain.tick();CHECK(gl_panel_open(plain.c));
}
static void temporal(){
    for(uint64_t step:{1000000ull,4000000ull,10000000ull,20000000ull}){
        Fixture f;f.device();for(int i=0;i<3;++i)f.tick({0,30,0},step);
        double total=0;for(uint64_t t=0;t<1000000000;t+=step)total+=f.tick({0,30,0},step).yaw_degrees;
        near(total,-30,1e-4);
    }
    // Analytic response integrated over the interval, below the adaptive threshold.
    Fixture f;f.device();f.set("context.1.gyro.smoothing_ms",100);f.set("context.1.gyro.smoothing_threshold_dps",100);
    for(int i=0;i<3;++i)f.tick({0,0,0});
    double total=0,expected=-30*(1-.1*(1-std::exp(-10)));
    for(int i=0;i<100;++i)total+=f.tick().yaw_degrees;
    near(total,expected,1e-5);
    f.set("context.1.gyro.smoothing_ms",0);near(f.tick().yaw_degrees,-.3);
    // Long gaps are discarded, never integrated as a large camera jump.
    near(f.tick({0,30,0},500000000).yaw_degrees,0);
    gl_sample duplicate{f.now,f.now,{0,30,0},{0,1,0}};CHECK(gl_submit_sample(f.c,1,&duplicate)==GL_INVALID);
    duplicate.sensor_ns++;duplicate.gyro_dps.x=std::numeric_limits<float>::quiet_NaN();CHECK(gl_submit_sample(f.c,1,&duplicate)==GL_INVALID);
    // An invalid packet after an apparent gap must not erase the live epoch.
    auto invalid_wake=duplicate;invalid_wake.arrival_ns+=500000000;
    CHECK(gl_submit_sample(f.c,1,&invalid_wake)==GL_INVALID);
    duplicate.sensor_ns=f.now;duplicate.gyro_dps.x=0;
    CHECK(gl_submit_sample(f.c,1,&duplicate)==GL_INVALID);
    // After sleep, a restarted hardware timestamp is a new epoch, not a permanent stall.
    f.now+=500000000;gl_sample wake{1000,f.now,{0,30,0},{0,1,0}};
    CHECK(gl_submit_sample(f.c,1,&wake)==GL_OK);(f.report(),gl_update(f.c,f.now,&f.host,&f.out));
    f.now+=10000000;wake.sensor_ns+=10000000;wake.arrival_ns=f.now;CHECK(gl_submit_sample(f.c,1,&wake)==GL_OK);
    (f.report(),gl_update(f.c,f.now,&f.host,&f.out));CHECK(f.out.source==GL_SOURCE_SDL);
}
static void gains_and_spaces(){
    Fixture f;f.device();f.prime();f.set("context.1.sensitivity_x",2);f.set("context.1.sensitivity_y",3);
    auto out=f.tick({10,20,0});near(out.yaw_degrees,-.4);near(out.pitch_degrees,.3);
    f.mode(10,"Bow drawn",10);f.mode(42,"Scope",20);
    f.set("context.10.sensitivity_x",4);f.set("context.10.sensitivity_y",2);
    f.set("context.42.sensitivity_x",6);f.set("context.42.sensitivity_y",5);f.state(10,true);
    out=f.tick({10,30,0});near(out.yaw_degrees,-1.2);near(out.pitch_degrees,.2);
    f.state(42,true);out=f.tick({10,30,0});near(out.yaw_degrees,-1.8);near(out.pitch_degrees,.5);
    f.set("context.42.gyro.invert_x",1);near(f.tick().yaw_degrees,1.8);
    gl_unregister_gameplay_context(f.c,10);gl_unregister_gameplay_context(f.c,42);f.states.clear();f.state(1,true);
    f.set("context.1.sensitivity_x",1);f.set("context.1.gyro.invert_x",0);
    for(int level=0;level<4;++level){f.set("context.1.gyro.acceleration",level);double max[]={1,1.5,2,3};near(f.tick({0,75,0}).yaw_degrees,-.75*max[level]);}
    f.set("context.1.gyro.acceleration",0);f.set("context.1.gyro.space",GL_SPACE_LOCAL_ROLL);near(f.tick({0,0,30}).yaw_degrees,.3);
    for(int space:{GL_SPACE_PLAYER,GL_SPACE_WORLD}){
        Fixture p;p.device();p.set("context.1.gyro.space",space);p.prime();
        CHECK(std::abs(p.tick().yaw_degrees)>.1);
        p.set("context.1.gyro.enabled",0);for(int i=0;i<60;++i)near(p.tick().yaw_degrees,0);
        gl_diagnostics before{};gl_get_diagnostics(p.c,&before);
        p.set("context.1.gyro.enabled",1);CHECK(std::abs(p.tick().yaw_degrees)>.1);
        gl_diagnostics after{};gl_get_diagnostics(p.c,&after);CHECK(after.accepted_samples>before.accepted_samples);
    }
}
static void activation(){
    Fixture f;f.device();f.prime();
    f.set("context.1.gyro.activation",GL_HOLD);f.set("context.1.activation.button",1);CHECK(!f.tick().gyro_active);
    f.controls.buttons=1;CHECK(f.tick().gyro_active);f.controls.buttons=0;
    f.set("context.1.activation.button",GL_BUTTON_SHOULDERS_BOTH);
    f.controls.buttons=1u<<9;CHECK(!f.tick().gyro_active);
    f.controls.buttons|=1u<<10;CHECK(f.tick().gyro_active);
    f.set("context.1.activation.button",GL_BUTTON_SHOULDERS_EITHER);f.controls.buttons=1u<<10;CHECK(f.tick().gyro_active);
    f.set("context.1.activation.button",1);f.controls.buttons=0;
    // All families work independently; both requires two actual sides.
    for(const char* family:{"context.1.activation.touchpad","context.1.activation.stick_touch","context.1.activation.grip_touch","context.1.activation.stick_deflection"}) {
        f.set(family,GL_SIDE_BOTH);f.controls.touchpads=f.controls.stick_touch=f.controls.grip_touch=GL_LEFT;
        f.controls.left_x=1;f.controls.right_x=0;CHECK(!f.tick().gyro_active);
        f.controls.touchpads=f.controls.stick_touch=f.controls.grip_touch=3;f.controls.right_x=1;CHECK(f.tick().gyro_active);
        f.set(family,0);f.controls={};
    }
    f.set("context.1.activation.touchpad",GL_SIDE_EITHER);f.controls.touchpads=GL_RIGHT;CHECK(f.tick().gyro_active);
    f.controls.touchpads=0;f.set("context.1.gyro.activation",GL_TOGGLE);f.tick();
    f.controls.buttons=1;CHECK(!f.tick().gyro_active);CHECK(!f.tick().gyro_active);
    f.controls.buttons=0;f.tick();f.controls.buttons=1;CHECK(f.tick().gyro_active);
    f.host.menu_open=1;CHECK(!f.tick().gyro_active);f.controls.buttons=0;f.tick();f.controls.buttons=1;f.tick();
    f.host.menu_open=0;CHECK(f.tick().gyro_active); // UI press did not toggle
    f.host.focused=0;CHECK(!f.tick().gyro_active);
}
static void source_selection(){
    Fixture f;f.device();f.device(2,GL_SOURCE_STEAM);f.prime();CHECK(f.out.source==GL_SOURCE_SDL);
    for(int i=0;i<30;++i)CHECK(f.tick({0,30,0},10000000,true,true).source==GL_SOURCE_SDL);
    double both=f.tick({0,30,0},10000000,true,true).yaw_degrees;near(both,-.3); // never -0.6
    for(int i=0;i<20;++i)f.tick({0,30,0},10000000,false,true);CHECK(f.out.source==GL_SOURCE_STEAM);
    near(f.tick({0,30,0},10000000,false,true).yaw_degrees,-.3);
    // Brief SDL recovery doesn't bounce the source.
    for(int i=0;i<50;++i)CHECK(f.tick({0,30,0},10000000,true,true).source==GL_SOURCE_STEAM);
    for(int i=0;i<180;++i)f.tick({0,30,0},10000000,true,true);CHECK(f.out.source==GL_SOURCE_SDL);
    CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);f.tick({0,30,0},10000000,false,true);CHECK(f.out.source==GL_SOURCE_STEAM);
    CHECK(gl_begin_calibration(f.c)==GL_UNAVAILABLE);gl_diagnostics d{};gl_get_diagnostics(f.c,&d);near(d.bias.y,0);
    // A silent SDL handle with advertised sensors must not block a usable Steam stream.
    Fixture silent;silent.device();silent.device(2,GL_SOURCE_STEAM);
    for(int i=0;i<40;++i)silent.tick({0,30,0},10000000,false,true);CHECK(silent.out.source==GL_SOURCE_STEAM);
    // Different physical controllers never merge; explicit association required.
    Fixture split;split.device();split.device(2,GL_SOURCE_STEAM,8);
    for(int i=0;i<40;++i)split.tick({0,30,0},10000000,false,true);CHECK(split.out.source==GL_SOURCE_NONE);
    CHECK(gl_associate_endpoint(split.c,2,7)==GL_OK);
    for(int i=0;i<3;++i)split.tick({0,30,0},10000000,false,true);CHECK(split.out.source==GL_SOURCE_STEAM);
    CHECK(gl_disconnect_endpoint(split.c,2)==GL_OK);split.tick({0,30,0},10000000,false,false);CHECK(split.out.source==GL_SOURCE_NONE);
    split.device(2,GL_SOURCE_STEAM);for(int i=0;i<3;++i)split.tick({0,30,0},10000000,false,true);CHECK(split.out.source==GL_SOURCE_STEAM);
}
static void calibration(){
    const auto buttons=[](gl_context* c,bool pending,bool available){
        CHECK(gl_menu_shared_setting_count(c)==7);
        bool begin=false,cancel=false;
        for(uint32_t i=0;i<gl_menu_shared_setting_count(c);++i){gl_setting_info s{},legacy{};
            CHECK(gl_menu_shared_setting_at(c,i,&s)==GL_OK);
            CHECK(gl_menu_tab_setting_at(c,GL_TAB_GENERAL,i,&legacy)==GL_OK);
            CHECK(std::strcmp(s.id,legacy.id)==0&&s.visible==legacy.visible);
            if(std::strcmp(s.id,"calibration.begin")==0){begin=true;CHECK(bool(s.visible)==!pending);CHECK(bool(s.available)==(!pending&&available));}
            if(std::strcmp(s.id,"calibration.cancel")==0){cancel=true;CHECK(bool(s.visible)==pending);CHECK(bool(s.available)==(pending&&available));}
        }
        CHECK(begin&&cancel);
    };
    Fixture f;buttons(f.c,false,false);f.device();f.prime();buttons(f.c,false,true);
    CHECK(gl_action(f.c,"calibration.begin")==GL_OK);buttons(f.c,true,true);
    for(int i=0;i<490;++i)CHECK(!f.tick({.5f,.3f,0}).gyro_active);
    gl_diagnostics d{};gl_get_diagnostics(f.c,&d);CHECK(d.calibration_state==GL_CAL_COUNTDOWN);
    for(int i=0;i<20;++i)f.tick({.5f,.3f,0});gl_get_diagnostics(f.c,&d);CHECK(d.calibration_state==GL_CAL_COLLECTING);
    buttons(f.c,true,true);
    f.tick({20,0,0});gl_get_diagnostics(f.c,&d);CHECK(d.calibration_state==GL_CAL_MOVING);
    buttons(f.c,true,true);
    for(int i=0;i<110;++i)f.tick({.5f,.3f,0});gl_get_diagnostics(f.c,&d);CHECK(d.calibration_state==GL_CAL_COMPLETE);
    buttons(f.c,false,true);
    near(d.bias.x,.5);near(d.bias.y,.3);near(f.tick({.5f,.3f,0}).yaw_degrees,0);
    CHECK(gl_begin_calibration(f.c)==GL_OK);buttons(f.c,true,true);
    CHECK(gl_action(f.c,"calibration.cancel")==GL_OK);buttons(f.c,false,true);
    gl_get_diagnostics(f.c,&d);CHECK(d.calibration_state==GL_CAL_IDLE);
    gl_event cancelled{};bool cancellation_event=false;
    while(gl_poll_event(f.c,&cancelled)==1)if(cancelled.type==GL_EVENT_CALIBRATION&&cancelled.detail==GL_CAL_IDLE)cancellation_event=true;
    CHECK(cancellation_event);
    Fixture a;a.device();a.set("calibration.automatic",GL_CAL_MENUS);
    gl_choice menu_choice{};CHECK(gl_choice_at(a.c,"calibration.automatic",GL_CAL_MENUS,&menu_choice)==GL_OK);CHECK(!menu_choice.available);
    for(int i=0;i<300;++i)a.tick({0,.5f,0});gl_get_diagnostics(a.c,&d);near(d.bias.y,0);
    // Neither an unverified host flag nor the F10 panel fabricates a menu hook.
    a.host.menu_open=1;gl_set_panel_open(a.c,1);
    for(int i=0;i<400;++i)a.tick({0,.5f,0});gl_get_diagnostics(a.c,&d);near(d.bias.y,0);
    CHECK(gl_set_host_capabilities(a.c,GL_HOST_MENU_STATE)==GL_OK);
    gl_choice_at(a.c,"calibration.automatic",GL_CAL_MENUS,&menu_choice);CHECK(menu_choice.available);
    gl_set_panel_open(a.c,0);
    for(int i=0;i<400;++i)a.tick({0,.5f,0});gl_get_diagnostics(a.c,&d);CHECK(d.bias.y>.2);
    const auto prior_bias=d.bias.y;CHECK(gl_set_host_capabilities(a.c,0)==GL_OK);
    for(int i=0;i<300;++i)a.tick({0,.5f,0});gl_get_diagnostics(a.c,&d);near(d.bias.y,prior_bias);
    double saved=0;gl_setting_get(a.c,"calibration.automatic",&saved);near(saved,GL_CAL_MENUS);
    gl_choice_at(a.c,"calibration.automatic",GL_CAL_MENUS,&menu_choice);CHECK(!menu_choice.available);
    a.set("calibration.automatic",GL_CAL_ANYTIME);a.host.menu_open=0;
    for(int i=0;i<400;++i)a.tick({0,.5f,0});gl_get_diagnostics(a.c,&d);CHECK(d.bias.y>prior_bias);
    Fixture steam;steam.device(2,GL_SOURCE_STEAM);steam.set("calibration.automatic",GL_CAL_ANYTIME);
    for(int i=0;i<800;++i)steam.tick({0,.5f,0},10000000,false,true);gl_get_diagnostics(steam.c,&d);near(d.bias.y,0);CHECK(d.calibration_state==GL_CAL_EXTERNAL);
    buttons(steam.c,false,false);
}
static void noisy_calibration(){
    // Zero-mean MEMS noise well above the old 0.2 dps per-sample reset, at
    // different sensor rates. The bias must converge without filtering output.
    for(uint64_t step:{1000000ull,4000000ull,10000000ull,20000000ull}){
        Fixture f;f.device();f.set("calibration.automatic",GL_CAL_ANYTIME);f.set("context.1.gyro.enabled",0);
        unsigned index=0;
        const auto sample=[&](bool moving=false){
            const float n=(index++%2?1.f:-1.f);
            f.now+=step;
            f.submit(1,{.35f+.6f*n,-.45f-.4f*n,moving?12.f:.25f+.2f*n},{.008f*n,1+.012f*n,-.006f*n});
            f.report();CHECK(gl_update(f.c,f.now,&f.host,&f.out)==GL_OK);
        };
        for(uint64_t elapsed=0;elapsed<10000000000ull;elapsed+=step)sample();
        gl_diagnostics d{};gl_get_diagnostics(f.c,&d);
        CHECK(d.stationary);near(d.bias.x,.35,.035);near(d.bias.y,-.45,.035);near(d.bias.z,.25,.035);
        const auto bias=d.bias;sample(true);gl_get_diagnostics(f.c,&d);CHECK(!d.stationary);
        for(uint64_t elapsed=0;elapsed<2000000000ull;elapsed+=step)sample(true);
        gl_get_diagnostics(f.c,&d);near(d.bias.x,bias.x);near(d.bias.y,bias.y);near(d.bias.z,bias.z);
        // No automatic bias update during the new stillness dwell after motion.
        for(uint64_t elapsed=0;elapsed<700000000ull;elapsed+=step)sample();
        gl_get_diagnostics(f.c,&d);near(d.bias.y,bias.y);
    }
    // A slow tilt changes gravity while keeping its magnitude exactly 1 g.
    Fixture tilt;tilt.device();tilt.set("calibration.automatic",GL_CAL_ANYTIME);
    for(int i=0;i<1200;++i){
        const double angle=i*.01*2*std::numbers::pi/180;
        tilt.now+=10000000;tilt.submit(1,{2,0,0},{0,float(std::cos(angle)),float(std::sin(angle))});
        tilt.report();CHECK(gl_update(tilt.c,tilt.now,&tilt.host,&tilt.out)==GL_OK);
    }
    gl_diagnostics d{};gl_get_diagnostics(tilt.c,&d);near(d.bias.x,0);
    // Manual placement uses the same noise-tolerant detector after its countdown.
    Fixture manual;manual.device();manual.prime();CHECK(gl_begin_calibration(manual.c)==GL_OK);
    for(int i=0;i<660;++i){const float n=i%2?1.f:-1.f;manual.tick({.4f+.55f*n,.2f-.4f*n,.15f*n});}
    gl_get_diagnostics(manual.c,&d);CHECK(d.calibration_state==GL_CAL_COMPLETE);
    near(d.bias.x,.4,.025);near(d.bias.y,.2,.025);
    // Repeated vibration is not stationary, even if its average is near zero.
    Fixture vibration;vibration.device();vibration.set("calibration.automatic",GL_CAL_ANYTIME);
    for(int i=0;i<800;++i)vibration.tick({i%2?2.f:-1.5f,0,0});
    gl_get_diagnostics(vibration.c,&d);CHECK(!d.stationary);near(d.bias.x,0);
}
static void flick(){
    Fixture f(false);f.device();f.mode(10,"Aim",10);f.mode(20,"Explore",0);f.state(20,true);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION)==GL_OK);
    f.set("context.10.gyro.enabled",0);f.set("context.20.gyro.enabled",0);
    f.set("context.20.flick.mode",GL_FLICK_ON);f.prime();
    f.controls.right_x=1;double total=0;
    for(int i=0;i<20;++i){auto out=f.tick();CHECK(out.suppress_native_right_stick);total+=out.yaw_degrees;}
    near(total,90);
    f.state(10,true);CHECK(!f.tick().suppress_native_right_stick);
    f.state(10,false);near(f.tick().yaw_degrees,0);CHECK(f.out.suppress_native_right_stick);
    f.controls.right_x=0;f.controls.right_y=-1;near(f.tick().yaw_degrees,90);
    f.controls.right_y=0;f.tick();f.set("context.20.flick.duration_ms",0);f.tick();
    f.controls.right_x=-1;near(f.tick().yaw_degrees,-90);
    f.host.menu_open=1;CHECK(!f.tick().suppress_native_right_stick);
    f.host.menu_open=0;near(f.tick().yaw_degrees,0); // held through UI: needs neutral
}
static gl_setting_info metadata(gl_context* c,const char* id);
static void block_long_press(){
    Fixture f;f.device();f.prime();f.set("context.1.gyro.activation",GL_HOLD_DISABLE);f.set("context.1.activation.button",1);f.set("context.1.activation.block_long_press",1);auto t=f.now;
    CHECK(gl_filter_event(f.c,0,GL_PRESS,t,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,t+100,1,0)==GL_FORWARD);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_LONG_PRESS_BLOCKING)==GL_OK);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,t,1,0)==GL_SUPPRESS);
    CHECK(gl_filter_event(f.c,0,GL_REPEAT,t+1000000,1,0)==GL_SUPPRESS);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,t+199999999,1,0)==GL_EMIT_TAP);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,t,1,0)==GL_SUPPRESS);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,t+200000000,1,0)==GL_SUPPRESS);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,t,1,1)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,t+100,0,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,t,1,0)==GL_SUPPRESS);
    f.host.suspend_long_press_blocking=1;f.tick();
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,t+100000000,1,0)==GL_SUPPRESS);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now+100,1,0)==GL_FORWARD);
    f.host.suspend_long_press_blocking=0;f.tick();
    for(auto mode:{GL_ALWAYS,GL_TOGGLE}){
        f.set("context.1.gyro.activation",mode);f.tick();
        CHECK(!metadata(f.c,"context.1.activation.block_long_press").visible);
        CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
        CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now+100000000,1,0)==GL_FORWARD);
    }
    f.set("context.1.gyro.activation",GL_HOLD);f.set("context.1.activation.button",0);f.tick();
    CHECK(metadata(f.c,"context.1.activation.block_long_press").visible&&!metadata(f.c,"context.1.activation.block_long_press").available);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now+100000000,1,0)==GL_FORWARD);
    f.set("context.1.activation.button",1);f.set("context.1.gyro.enabled",0);f.tick();
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now+100000000,1,0)==GL_FORWARD);
    f.set("context.1.gyro.enabled",1);
    CHECK(gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CURSOR)==GL_OK);
    CHECK(!metadata(f.c,"context.1.activation.block_long_press").visible);
}
static void settings(){
    Fixture f;f.device();f.prime();f.set("context.1.gyro.smoothing_ms",12);double value=0;gl_setting_get(f.c,"context.1.gyro.smoothing_ms",&value);near(value,10);
    CHECK(gl_setting_set(f.c,"context.1.sensitivity_x",std::numeric_limits<double>::infinity())==GL_INVALID);
    CHECK(gl_setting_set(f.c,"context.1.gyro.space",99)==GL_INVALID);
    gl_event e{};bool changed=false;while(gl_poll_event(f.c,&e)==1)if(e.type==GL_EVENT_SETTING)changed=true;CHECK(changed);
    const char* path="gyrolib_test_settings.ini";
    CHECK(gl_set_settings_path(f.c,path)==GL_OK);CHECK(gl_action(f.c,"settings.save")==GL_OK);
    {std::ofstream o(path);o<<"schema=0.2.0\ncontext.1.gyro.smoothing_ms=125\nfuture.preference=keep-me\n";}
    CHECK(gl_load_settings(f.c,path)==GL_OK);gl_setting_get(f.c,"context.1.gyro.smoothing_ms",&value);near(value,125);
    CHECK(gl_save_settings(f.c,path)==GL_OK);{std::ifstream in(path);std::string text((std::istreambuf_iterator<char>(in)),{});CHECK(text.find("future.preference=keep-me")!=std::string::npos);}
    {std::ofstream o(path);o<<"schema=999.0.0\n";}CHECK(gl_load_settings(f.c,path)==GL_NEWER_SCHEMA);CHECK(gl_save_settings(f.c,path)==GL_NEWER_SCHEMA);
    {std::ofstream o(path);o<<"schema=0.2.0\ncontext.1.gyro.smoothing_ms=200\ncontext.1.gyro.space=nan\n";}CHECK(gl_load_settings(f.c,path)==GL_INVALID);gl_setting_get(f.c,"context.1.gyro.smoothing_ms",&value);near(value,125);
    std::filesystem::remove(path);gl_set_settings_path(f.c,"");
    CHECK(gl_set_language(f.c,"fr")==GL_OK);CHECK(std::strcmp(gl_text(f.c,"off"),"?")!=0);
    gl_choice choice{};CHECK(gl_choice_at(f.c,"context.1.activation.touchpad",GL_SIDE_BOTH,&choice)==GL_OK);CHECK(choice.available);
    gl_endpoint single{};gl_get_endpoint(f.c,0,&single);single.caps.touchpads=GL_SINGLE;single.caps.stick_touch=0;gl_register_endpoint(f.c,&single);
    gl_choice_at(f.c,"context.1.activation.touchpad",GL_SIDE_BOTH,&choice);CHECK(!choice.available);
    gl_choice_at(f.c,"context.1.activation.touchpad",GL_SIDE_EITHER,&choice);CHECK(choice.available);
    // A central PlayStation pad has exactly Off/On, preserving saved value 3.
    for(const char* language:{"en","fr"}){
        gl_set_language(f.c,language);unsigned choices=0;
        for(unsigned i=0;i<gl_menu_choice_count(f.c,"context.1.activation.touchpad");++i){
            gl_choice_at(f.c,"context.1.activation.touchpad",i,&choice);if(!choice.available)continue;++choices;
            CHECK(choice.value==double(GL_SIDE_OFF)||choice.value==double(GL_SIDE_EITHER));
            CHECK(std::strcmp(choice.label,gl_text(f.c,choice.value==double(GL_SIDE_OFF)?"off":"on"))==0);
        }
        CHECK(choices==2);
        CHECK(std::strcmp(gl_choice_description(f.c,"context.1.activation.touchpad",GL_SIDE_EITHER),gl_text(f.c,"choice.side.either"))!=0);
    }
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
    unsigned flick_choices=0;for(unsigned i=0;i<gl_menu_choice_count(f.c,"context.1.flick.mode");++i){
        gl_choice_at(f.c,"context.1.flick.mode",i,&choice);if(choice.available){++flick_choices;CHECK(choice.value==double(GL_FLICK_OFF)||choice.value==double(GL_FLICK_ON));}
    }
    CHECK(flick_choices==2);
    gl_set_language(f.c,"en");
    for(uint32_t i=0;i<gl_setting_count();++i){gl_setting_info setting{};gl_setting_at(f.c,i,&setting);
        CHECK(std::strcmp(setting.label,"?")!=0);CHECK(std::strcmp(setting.description,"?")!=0);}
}
static gl_setting_info metadata(gl_context* c,const char* id) {
    for(uint32_t i=0;i<gl_menu_setting_count(c);++i) {
        gl_setting_info info{};CHECK(gl_setting_at(c,i,&info)==GL_OK);
        if(std::strcmp(info.id,id)==0)return info;
    }
    throw std::runtime_error(std::string("Missing menu metadata: ")+id);
}
static void gameplay_contexts(){
    Fixture f(false);f.device();f.prime();double value=0;
    CHECK(gl_menu_tab_count(f.c)==0);CHECK(gl_gameplay_context_count(f.c)==0);
    CHECK(gl_menu_setting_count(f.c)==gl_setting_count());
    CHECK(!metadata(f.c,"gyro.context").visible);CHECK(!metadata(f.c,"flick.context").visible);
    CHECK(!metadata(f.c,"activation.block_long_press").visible);
    gl_choice choice{};gl_choice_at(f.c,"gyro.activation",5,&choice);CHECK(choice.value==double(GL_CONTEXT_ONLY)&&!choice.available);
    f.host.aiming=f.host.alt_fire=1;near(f.tick().yaw_degrees,0); // reserved fields cannot create a view
    char label[]="Bow drawn";f.mode(4000,label,10);label[0]='X';f.mode(21,"Scope",10);f.tick();
    const auto per_mode=gl_menu_tab_setting_count(f.c,4001);
    CHECK(per_mode>10);CHECK(gl_menu_setting_count(f.c)==gl_setting_count()+2*per_mode);
    CHECK(gl_menu_tab_count(f.c)==2);CHECK(gl_menu_tab_setting_count(f.c,GL_TAB_CAMERA)==0);
    gl_menu_tab tab{};CHECK(gl_menu_tab_at(f.c,1,&tab)==GL_OK);CHECK(tab.id==4001&&tab.context_id==4000);
    CHECK(std::strcmp(tab.label,"Bow drawn")==0);CHECK(tab.available&&!tab.active);
    f.set("context.4000.sensitivity_x",2);f.set("context.4000.sensitivity_y",7);
    f.set("context.21.sensitivity_x",4);f.set("context.21.sensitivity_y",3);
    f.state(4000,true);auto out=f.tick({10,30,0});near(out.yaw_degrees,-.6);near(out.pitch_degrees,.7);
    CHECK(gl_get_active_gameplay_context(f.c)==4000);
    for(uint32_t i=0;i<per_mode;++i){
        gl_setting_info row{};CHECK(gl_menu_tab_setting_at(f.c,22,i,&row)==GL_OK);
        CHECK(std::strncmp(row.id,"context.21.",11)==0);
        CHECK(std::strstr(row.id,"gyro.context")==nullptr&&std::strstr(row.id,"flick.context")==nullptr);
        if(std::strcmp(row.id,"context.21.gyro.activation")==0){
            unsigned available=0;for(uint32_t n=0;n<gl_menu_choice_count(f.c,row.id);++n){
                gl_choice option{};gl_choice_at(f.c,row.id,n,&option);available+=option.available;
                if(option.available)CHECK(option.value==double(GL_GYRO_OFF)||option.value==double(GL_ALWAYS)||option.value==double(GL_HOLD)||option.value==double(GL_TOGGLE)||option.value==double(GL_HOLD_DISABLE));
            }CHECK(available==5);
        }
    }
    CHECK(gl_get_active_gameplay_context(f.c)==4000); // reading another tab never changes output selection
    CHECK(gl_setting_set(f.c,"gyro.sensitivity_x",9)==GL_UNAVAILABLE);
    CHECK(gl_setting_set(f.c,"gyro.invert_x",1)==GL_UNAVAILABLE);
    CHECK(gl_setting_set(f.c,"gyro.enabled",0)==GL_UNAVAILABLE);
    out=f.tick({10,30,0});near(out.yaw_degrees,-.6);near(out.pitch_degrees,.7); // all profile values independent
    f.state(21,true);out=f.tick({10,30,0});near(out.yaw_degrees,-1.2);near(out.pitch_degrees,.3);
    gl_gameplay_context renamed{4000,"Custom localized name","Host description",20};
    CHECK(gl_register_gameplay_context(f.c,&renamed)==GL_OK);
    CHECK(std::strstr(metadata(f.c,"context.4000.sensitivity_x").label,"Custom localized name"));
    CHECK(!std::strstr(metadata(f.c,"context.4000.sensitivity_x").description,"Host description"));
    CHECK(gl_menu_tab_at(f.c,1,&tab)==GL_OK&&std::strcmp(tab.description,"Host description")==0);
    out=f.tick({10,30,0});near(out.yaw_degrees,-.6);near(out.pitch_degrees,.7);
    f.state(4000,true,false);f.tick();CHECK(gl_get_active_gameplay_context(f.c)==21);
    f.states.erase(21);CHECK(!f.tick().gyro_active);CHECK(gl_get_active_gameplay_context(f.c)==0);
    CHECK(metadata(f.c,"context.4000.sensitivity_x").available);
    CHECK(gl_menu_tab_at(f.c,1,&tab)==GL_OK&&!tab.available&&!tab.active);
    // Runtime observation gates output, never editing a registered profile.
    // Compare every setting with the same controller/capabilities across all
    // three non-running states: inactive, explicitly unavailable and omitted.
    f.state(4000,true);f.tick();
    std::map<std::string,std::pair<uint32_t,uint32_t>> editable;
    for(uint32_t i=0;i<per_mode;++i){gl_setting_info row{};
        CHECK(gl_menu_tab_setting_at(f.c,4001,i,&row)==GL_OK);
        editable[row.id]={row.visible,row.available};
    }
    const char* inactive_path="gyrolib_inactive_view.ini";
    CHECK(gl_set_settings_path(f.c,inactive_path)==GL_OK);
    for(int state=0;state<3;++state){
        if(state==0)f.state(4000,false);
        else if(state==1)f.state(4000,true,false);
        else f.states.erase(4000);
        const auto inactive=f.tick();CHECK(!inactive.gyro_active);
        near(inactive.yaw_degrees,0);near(inactive.pitch_degrees,0);
        CHECK(gl_get_active_gameplay_context(f.c)==0);
        for(uint32_t i=0;i<per_mode;++i){gl_setting_info row{};
            CHECK(gl_menu_tab_setting_at(f.c,4001,i,&row)==GL_OK);
            CHECK(editable.at(row.id)==std::make_pair(row.visible,row.available));
        }
        f.set("context.4000.sensitivity_x",5+state);
        gyrolib::Context restored;CHECK(gl_register_gameplay_context(restored.get(),&renamed)==GL_OK);
        CHECK(gl_load_settings(restored.get(),inactive_path)==GL_OK);
        CHECK(gl_setting_get(restored.get(),"context.4000.sensitivity_x",&value)==GL_OK);near(value,5+state);
        CHECK(gl_get_active_gameplay_context(f.c)==0);
    }
    CHECK(gl_set_settings_path(f.c,"")==GL_OK);std::filesystem::remove(inactive_path);
    f.set("context.4000.sensitivity_x",2);
    f.state(4000,true);CHECK(f.tick().gyro_active);
    f.mode(UINT32_MAX,"Maximum stable ID",-10);f.set("context.4294967295.sensitivity_y",8.6);
    f.set("context.4294967295.gyro.smoothing_ms",120);
    CHECK(gl_menu_tab_at(f.c,2,&tab)==GL_OK);CHECK(tab.id==4294967296ull);
    CHECK(gl_menu_tab_setting_count(f.c,4294967297ull)==0);
    CHECK(gl_setting_set(f.c,"context.4000.gyro.activation",GL_CONTEXT_ONLY)==GL_INVALID);
    CHECK(gl_setting_set(f.c,"context.4000.flick.mode",GL_FLICK_OUTSIDE_CONTEXT)==GL_INVALID);
    const char* path="gyrolib_context_test.ini";CHECK(gl_save_settings(f.c,path)==GL_OK);
    CHECK(gl_unregister_gameplay_context(f.c,4000)==GL_OK);f.states.erase(4000);CHECK(!f.tick().gyro_active);
    CHECK(gl_setting_get(f.c,"context.4000.sensitivity_x",&value)==GL_INVALID);
    f.mode(4000);gl_setting_get(f.c,"context.4000.sensitivity_x",&value);near(value,2);
    Fixture loaded(false);CHECK(gl_load_settings(loaded.c,path)==GL_OK);
    loaded.mode(UINT32_MAX,"New localized name");loaded.mode(21);loaded.mode(4000);
    gl_setting_get(loaded.c,"context.4000.sensitivity_y",&value);near(value,7);
    gl_setting_get(loaded.c,"context.4294967295.sensitivity_y",&value);near(value,8.6);
    gl_setting_get(loaded.c,"context.4294967295.gyro.smoothing_ms",&value);near(value,120);
    {std::ofstream file(path);file<<"schema=0.2.0\ncontext.4000.sensitivity_x=9\ncontext.21.gyro.space=nan\n";}
    CHECK(gl_load_settings(loaded.c,path)==GL_INVALID);
    gl_setting_get(loaded.c,"context.4000.sensitivity_x",&value);near(value,2);
    gl_reset_settings(loaded.c);gl_setting_get(loaded.c,"context.4000.sensitivity_x",&value);near(value,2.5);
    gl_setting_get(loaded.c,"context.4294967295.gyro.smoothing_ms",&value);near(value,0);
    std::filesystem::remove(path);gl_set_settings_path(loaded.c,"");
    for(uint32_t id=100;id<356;++id)loaded.mode(id,"Host-defined mode");
    CHECK(gl_gameplay_context_count(loaded.c)==259);CHECK(gl_menu_tab_count(loaded.c)==259);
    CHECK(gl_menu_setting_count(loaded.c)==gl_setting_count()+259*per_mode);
    CHECK(gl_set_language(loaded.c,"fr")==GL_OK);
    CHECK(std::strstr(metadata(loaded.c,"context.4294967295.sensitivity_y").label,"New localized name"));
    gl_event event{};bool context_event=false,setting_event=false;
    while(gl_poll_event(f.c,&event)==1){context_event|=event.type==GL_EVENT_CONTEXT;setting_event|=event.type==GL_EVENT_SETTING&&std::strcmp(event.setting_id,"context.4000.sensitivity_y")==0;}
    CHECK(context_event&&setting_event);
}
static void context_flick(){
    Fixture f(false);f.device();f.mode(100,"Camera mode");f.mode(200,"Circular mode");
    f.set("context.200.gyro.enabled",0);
    f.set("context.200.flick.mode",GL_FLICK_ON);f.prime();
    CHECK(!f.out.suppress_native_right_stick);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION)==GL_OK);
    f.state(100,true);f.state(200,false);CHECK(f.tick().gyro_active);CHECK(!f.out.suppress_native_right_stick);
    f.state(100,false);f.state(200,true);CHECK(!f.tick().gyro_active);CHECK(f.out.suppress_native_right_stick);
    f.controls.right_x=1;double turn=0;for(int i=0;i<20;++i)turn+=f.tick().yaw_degrees;near(turn,90);
    f.state(200,false);CHECK(!f.tick().suppress_native_right_stick);
    f.state(200,true);near(f.tick().yaw_degrees,0);CHECK(f.out.suppress_native_right_stick);
    f.controls.right_x=0;f.controls.right_y=-1;near(f.tick().yaw_degrees,90);
    f.state(200,true,false);near(f.tick().yaw_degrees,0);CHECK(!f.out.suppress_native_right_stick);
    CHECK(gl_set_host_capabilities(f.c,0)==GL_OK);f.state(200,true);CHECK(!f.tick().suppress_native_right_stick);
    double saved=0;gl_setting_get(f.c,"context.200.flick.mode",&saved);near(saved,GL_FLICK_ON);
    CHECK(!metadata(f.c,"context.200.flick.mode").visible);
}
static void profile_persistence(){
    const char* path="gyrolib_profiles.ini";
    {std::ofstream file(path);file<<"schema=0.2.0\ncontext.10.sensitivity_x=4.5\ncontext.20.gyro.activation=6\ncontext.20.gyro.smoothing_ms=85\nfuture.setting=keep\n";}
    Fixture before(false);before.mode(10,"Aim");before.mode(20,"Explore");
    CHECK(gl_load_settings(before.c,path)==GL_OK);CHECK(gl_save_settings(before.c,path)==GL_OK);
    Fixture loaded(false);loaded.mode(10,"Aim");loaded.mode(20,"Explore");
    CHECK(gl_load_settings(loaded.c,path)==GL_OK);double value=0;
    auto get=[&](const char* id){CHECK(gl_setting_get(loaded.c,id,&value)==GL_OK);return value;};
    near(get("context.10.sensitivity_x"),4.5);near(get("context.10.sensitivity_y"),2.5);
    near(get("context.20.gyro.activation"),GL_GYRO_OFF);near(get("context.20.gyro.smoothing_ms"),85);
    loaded.mode(30);near(get("context.30.gyro.smoothing_ms"),0);
    CHECK(gl_unregister_gameplay_context(loaded.c,30)==GL_OK);loaded.states.erase(30);
    CHECK(gl_save_settings(loaded.c,path)==GL_OK);
    {std::ifstream file(path);std::string text((std::istreambuf_iterator<char>(file)),{});CHECK(text.find("schema=0.2.0")!=std::string::npos);CHECK(text.find("future.setting=keep")!=std::string::npos);}
    gl_reset_settings(loaded.c);near(get("context.20.gyro.smoothing_ms"),0);
    std::filesystem::remove(path);
}
static void independent_profiles(){
    Fixture f(false);f.device();f.mode(10);f.mode(20);f.state(10,true);f.prime();
    f.set("context.10.sensitivity_x",1);f.set("context.20.sensitivity_x",2);
    f.set("context.20.gyro.invert_x",1);f.set("context.20.gyro.smoothing_ms",100);
    f.set("context.20.gyro.smoothing_threshold_dps",100);
    near(f.tick().yaw_degrees,-.3);
    CHECK(!metadata(f.c,"context.10.activation.stick_threshold").visible);
    f.set("context.20.gyro.activation",GL_HOLD_DISABLE);
    f.set("context.20.activation.stick_deflection",GL_SIDE_RIGHT);
    CHECK(metadata(f.c,"context.20.activation.stick_threshold").visible);
    CHECK(!metadata(f.c,"context.10.activation.stick_threshold").visible);
    f.set("context.20.gyro.space",GL_SPACE_LOCAL_ROLL);
    CHECK(!metadata(f.c,"context.10.gyro.local_angle_degrees").visible);
    f.set("context.20.gyro.space",GL_SPACE_LOCAL_ADVANCED);
    CHECK(metadata(f.c,"context.20.gyro.local_angle_degrees").visible);
    f.set("context.20.gyro.space",GL_SPACE_LOCAL_YAW);
    f.set("context.10.gyro.activation",GL_TOGGLE);f.set("context.10.activation.button",1);f.tick();
    f.controls.buttons=1;CHECK(!f.tick().gyro_active);f.controls.buttons=0;f.tick();
    f.state(10,false);f.state(20,true);CHECK(f.tick().gyro_active);
    f.state(20,false);f.state(10,true);CHECK(!f.tick().gyro_active); // non-toggle view did not change the shared latch
    f.controls.buttons=1;CHECK(f.tick().gyro_active);near(f.out.yaw_degrees,-.3);
    f.controls.buttons=0;f.tick();
    f.set("context.20.gyro.activation",GL_HOLD);f.set("context.20.activation.button",2);
    f.state(10,false);f.state(20,true);CHECK(!f.tick().gyro_active);
    f.controls.buttons=2;CHECK(f.tick().gyro_active);CHECK(f.out.yaw_degrees>0&&f.out.yaw_degrees<.6);
    f.set("context.20.gyro.smoothing_ms",0);near(f.tick().yaw_degrees,.6);
    f.set("context.20.gyro.acceleration",3);near(f.tick({0,75,0}).yaw_degrees,4.5);
    f.state(20,false);f.state(10,true);near(f.tick({0,75,0}).yaw_degrees,-.75);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_LONG_PRESS_BLOCKING)==GL_OK);
    f.set("context.10.gyro.activation",GL_HOLD_DISABLE);
    f.set("context.10.activation.block_long_press",1);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_SUPPRESS);
    f.state(10,false);f.state(20,true);f.tick();
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now,1,0)==GL_SUPPRESS); // no delayed game tap crosses a mode
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now,1,0)==GL_FORWARD);
}
struct FakeSteam {
    bool available=true;
    uint64_t handle=7;
    uint32_t count=1;
    float stick_x{};
    static uint32_t GL_CALL handles(void* user,uint64_t* h,uint32_t capacity){
        auto& fake=*static_cast<FakeSteam*>(user);for(uint32_t i=0;i<std::min(fake.count,capacity);++i)h[i]=fake.handle;return fake.count;
    }
    static uint32_t GL_CALL motion(void* user,uint64_t,gl_steam_motion* m){m->accel_z=16384;m->yaw=491.52f;return static_cast<FakeSteam*>(user)->available;}
    static uint32_t GL_CALL controls(void* user,uint64_t,gl_capabilities* caps,gl_controls* input){
        caps->sticks=GL_RIGHT;input->right_x=static_cast<FakeSteam*>(user)->stick_x;return 1;
    }
    static const char* GL_CALL label(void*,uint64_t,uint32_t button){return button==0?"Cross":nullptr;}
};
static void steam_adapter(){
    Fixture f;FakeSteam fake;gl_steam_provider provider{&fake,FakeSteam::handles,FakeSteam::motion,nullptr};
    auto* reader=gl_steam_create(f.c,&provider);CHECK(reader);
    CHECK(gl_steam_set_button_label_provider(reader,FakeSteam::label,&fake)==GL_OK);
    for(unsigned i=0;i<60;++i){f.now+=10000000;CHECK(gl_steam_poll(reader,f.now,i)==GL_OK);CHECK((f.report(),gl_update(f.c,f.now,&f.host,&f.out))==GL_OK);}
    CHECK(f.out.source==GL_SOURCE_STEAM);near(f.out.yaw_degrees,-.3,1e-4);
    CHECK(std::strcmp(gl_get_button_label(f.c,0),"Cross")==0);
    CHECK(gl_steam_poll(reader,f.now,59)==GL_INVALID);
    CHECK(gl_associate_endpoint(f.c,gl_steam_endpoint_for_handle(reader,7),987)==GL_OK);
    f.now+=10000000;CHECK(gl_steam_poll(reader,f.now,60)==GL_OK);(f.report(),gl_update(f.c,f.now,&f.host,&f.out));
    fake.available=false;for(unsigned i=61;i<90;++i){f.now+=10000000;CHECK(gl_steam_poll(reader,f.now,i)==GL_OK);(f.report(),gl_update(f.c,f.now,&f.host,&f.out));}CHECK(f.out.source==GL_SOURCE_NONE);
    for(unsigned i=90;i<130;++i){++fake.handle;f.now+=10000000;CHECK(gl_steam_poll(reader,f.now,i)==GL_OK);CHECK(gl_endpoint_count(f.c)==1);}
    CHECK(gl_steam_set_button_label_provider(reader,nullptr,nullptr)==GL_OK);
    CHECK(std::strcmp(gl_get_button_label(f.c,0),"South")==0);
    gl_steam_destroy(reader);

    // Provider errors remain observable, and an invalid handle list cannot
    // remove already registered controllers before reporting that error.
    Fixture errors;FakeSteam bad;
    gl_steam_provider bad_provider{&bad,FakeSteam::handles,FakeSteam::motion,FakeSteam::controls};
    reader=gl_steam_create(errors.c,&bad_provider);CHECK(reader);
    CHECK(gl_steam_poll(reader,errors.now,0)==GL_OK);
    const auto endpoint=gl_steam_endpoint_for_handle(reader,7);CHECK(endpoint);
    bad.handle=0;errors.now+=10000000;
    CHECK(gl_steam_poll(reader,errors.now,1)==GL_INVALID);
    CHECK(gl_endpoint_count(errors.c)==1);CHECK(gl_steam_endpoint_for_handle(reader,7)==endpoint);
    bad.handle=7;bad.count=2;
    CHECK(gl_steam_poll(reader,errors.now,2)==GL_INVALID);CHECK(gl_endpoint_count(errors.c)==1);
    bad.count=1;bad.stick_x=1.5f;
    CHECK(gl_steam_poll(reader,errors.now,3)==GL_INVALID);
    bad.stick_x=.5f;
    CHECK(gl_steam_poll(reader,errors.now,4)==GL_OK);
    CHECK(gl_steam_poll(reader,errors.now,5)==GL_INVALID); // duplicate motion timestamp
    gl_steam_destroy(reader);
}
static void gyro_spaces(){
    using gyrolib::project_local;using gyrolib::project_laser;
    Fixture f;f.device();f.prime();
    CHECK(gl_menu_choice_count(f.c,"context.1.gyro.space")==9);
    constexpr unsigned order[]={0,3,7,8,6,1,2,4,5};
    for(unsigned n=0;n<9;++n){gl_choice choice{};CHECK(gl_choice_at(f.c,"context.1.gyro.space",n,&choice)==GL_OK);CHECK(choice.available&&std::strcmp(choice.label,"?")!=0);near(choice.value,order[n]);}
    near(metadata(f.c,"context.1.gyro.space").default_value,GL_SPACE_PLAYER);
    CHECK(!metadata(f.c,"context.1.gyro.local_angle_degrees").visible);
    CHECK(!metadata(f.c,"context.1.gyro.local_roll_percent").visible);
    f.set("context.1.gyro.space",GL_SPACE_LOCAL_YAW_ROLL);
    auto out=f.tick({10,20,-30});near(out.yaw_degrees,-.5);near(out.pitch_degrees,.1);
    CHECK(metadata(f.c,"context.1.gyro.local_roll_percent").visible);CHECK(!metadata(f.c,"context.1.gyro.local_angle_degrees").visible);
    f.set("context.1.gyro.local_roll_percent",-100);near(f.tick({10,20,-30}).yaw_degrees,.1);
    f.set("context.1.gyro.space",GL_SPACE_LOCAL_ADVANCED);f.set("context.1.gyro.local_roll_percent",0);
    CHECK(metadata(f.c,"context.1.gyro.local_angle_degrees").visible);
    f.set("context.1.gyro.local_angle_degrees",90);near(f.tick({10,20,-30}).yaw_degrees,-.3);
    auto local=project_local({10,20,-30},0,0);near(local.pitch,10);near(local.yaw,20);
    local=project_local({10,20,-30},90,0);near(local.yaw,30);
    local=project_local({10,20,-30},45,0);near(local.yaw,50/std::sqrt(2.));
    local=project_local({10,20,-30},45,1);near(local.yaw,60/std::sqrt(2.));
    local=project_local({10,20,-30},-180,0);near(local.yaw,-20);
    // Geometric fixtures: a forward ray must keep pitch when the pad is on its side.
    auto ray=project_laser({10,20,30},{0,-1,0});near(ray.pitch,10);near(ray.yaw,20);
    ray=project_laser({10,20,30},{-1,0,0});near(ray.pitch,-20);near(ray.yaw,10);
    ray=project_laser({10,20,30},{0,1,0});near(ray.pitch,-10);near(ray.yaw,-20);
    ray=project_laser({0,0,30},{0,-1,0});near(ray.pitch,0);near(ray.yaw,0); // roll along ray does not move it
    ray=project_laser({0,20,0},{0,-.5f,-.8660254f});near(ray.yaw,40,1e-4); // azimuth vs mere world yaw
    for(float x:{-.05f,0.f,.05f})for(float y:{-.05f,0.f,.05f}) {
        ray=project_laser({10,20,30},{x,y,1});CHECK(std::isfinite(ray.pitch)&&std::isfinite(ray.yaw));
        CHECK(std::abs(ray.yaw)<260);CHECK(std::abs(ray.pitch)<23);
    }
    ray=project_laser({10,20,30},{0,0,1});near(ray.pitch,0);near(ray.yaw,0);
    // Full fusion/filter/integration path at multiple sensor rates, with gravity
    // invariant under this yaw movement. Ordinary activation does not re-prime it.
    for(uint64_t dt:{1000000ull,4000000ull,10000000ull,20000000ull}) {
        Fixture laser;laser.device();laser.set("context.1.gyro.space",GL_SPACE_LASER_POINTER);
        for(int n=0;n<3;++n)laser.tick({0,30,0},dt);
        double total=0;for(uint64_t t=0;t<1000000000;t+=dt)total+=laser.tick({0,30,0},dt).yaw_degrees;
        near(total,-30,1e-4);
        laser.set("context.1.gyro.activation",GL_HOLD_DISABLE);laser.set("context.1.activation.button",1);laser.controls.buttons=1;CHECK(!laser.tick({0,30,0},dt).gyro_active);
        laser.controls.buttons=0;CHECK(laser.tick({0,30,0},dt).gyro_active);
    }
    const char* path="gyrolib_spaces_test.ini";
    f.set("context.1.gyro.space",GL_SPACE_LASER_POINTER);CHECK(gl_save_settings(f.c,path)==GL_OK);
    Fixture restored;CHECK(gl_load_settings(restored.c,path)==GL_OK);
    double value=0;gl_setting_get(restored.c,"context.1.gyro.space",&value);near(value,GL_SPACE_LASER_POINTER);
    std::filesystem::remove(path);
}
static void button_names(){
    Fixture f;f.device();f.device(2,GL_SOURCE_STEAM,8);f.prime();gl_choice choice{};
    CHECK(std::strcmp(gl_get_button_label(f.c,0),"South")==0);
    CHECK(gl_set_button_label(f.c,1,0,"A",GL_LABEL_DEVICE)==GL_OK);
    CHECK(gl_set_button_label(f.c,1,9,"LB",GL_LABEL_DEVICE)==GL_OK);
    CHECK(gl_set_button_label(f.c,1,10,"RB",GL_LABEL_DEVICE)==GL_OK);
    CHECK(gl_choice_at(f.c,"context.1.activation.button",1,&choice)==GL_OK);CHECK(std::strcmp(choice.label,"A")==0&&choice.available);
    CHECK(gl_choice_at(f.c,"context.1.activation.button",GL_BUTTON_SHOULDERS_EITHER,&choice)==GL_OK);CHECK(std::strcmp(choice.label,"LB or RB")==0);
    for(uint32_t value=33;value<=40;++value){
        CHECK(gl_choice_at(f.c,"context.1.activation.button",value,&choice)==GL_OK);CHECK(!choice.available);
    }
    // A different physical controller must not rename the selected one's buttons.
    CHECK(gl_set_button_label(f.c,2,0,"Cross",GL_LABEL_PHYSICAL)==GL_OK);
    CHECK(std::strcmp(gl_get_button_label(f.c,0),"A")==0);
    CHECK(gl_associate_endpoint(f.c,2,7)==GL_OK);
    CHECK(std::strcmp(gl_get_button_label(f.c,0),"Cross")==0); // paired virtual Xbox label overridden
    CHECK(gl_set_button_label(f.c,2,9,"L1",GL_LABEL_PHYSICAL)==GL_OK);
    CHECK(gl_set_button_label(f.c,2,10,"R1",GL_LABEL_PHYSICAL)==GL_OK);
    CHECK(gl_choice_at(f.c,"context.1.activation.button",GL_BUTTON_SHOULDERS_BOTH,&choice)==GL_OK);CHECK(std::strcmp(choice.label,"L1 and R1")==0);
    // Identical commercial names have no effect, and labels do not add buttons.
    gl_endpoint endpoint{};gl_get_endpoint(f.c,0,&endpoint);endpoint.caps.buttons=1;gl_register_endpoint(f.c,&endpoint);
    f.tick();CHECK(gl_choice_at(f.c,"context.1.activation.button",10,&choice)==GL_OK);CHECK(!choice.available);
    CHECK(gl_set_button_label(f.c,1,31,"Custom rear button",GL_LABEL_DEVICE)==GL_OK);
    CHECK(gl_choice_at(f.c,"context.1.activation.button",32,&choice)==GL_OK);CHECK(!choice.available);
    CHECK(gl_set_button_label(f.c,2,0,nullptr,GL_LABEL_PHYSICAL)==GL_OK);CHECK(std::strcmp(gl_get_button_label(f.c,0),"A")==0);
    CHECK(gl_disconnect_endpoint(f.c,2)==GL_OK);CHECK(std::strcmp(gl_get_button_label(f.c,9),"LB")==0);
    f.device(3,GL_SOURCE_SDL,9);CHECK(gl_set_button_label(f.c,3,0,"B",GL_LABEL_DEVICE)==GL_OK);
    CHECK(gl_select_device(f.c,9)==GL_OK);CHECK(std::strcmp(gl_get_button_label(f.c,0),"B")==0);
    CHECK(gl_set_button_label(f.c,3,32,"Bad",GL_LABEL_DEVICE)==GL_INVALID);
    gl_event event{};bool changed=false;while(gl_poll_event(f.c,&event)==1)changed|=event.type==GL_EVENT_BUTTON_LABELS;CHECK(changed);
    gl_choice_at(f.c,"context.1.gyro.activation",0,&choice);CHECK(choice.value==double(GL_GYRO_OFF)&&std::strcmp(choice.label,"Off")==0);
    gl_choice_at(f.c,"context.1.gyro.activation",1,&choice);CHECK(choice.value==double(GL_ALWAYS)&&std::strcmp(choice.label,"Always on")==0);
    gl_choice_at(f.c,"context.1.gyro.activation",2,&choice);CHECK(choice.value==double(GL_HOLD_DISABLE)&&std::strcmp(choice.label,"Hold to disable")==0);
}
static void sensitivity_range(){
    Fixture f;f.device();f.mode(77);f.prime();
    for(const char* id:{"context.1.sensitivity_x","context.1.sensitivity_y","context.77.sensitivity_x","context.77.sensitivity_y"}){
        auto info=metadata(f.c,id);near(info.minimum,0);near(info.maximum,20);near(info.step,.1);near(info.default_value,2.5);
        f.set(id,20);CHECK(gl_setting_set(f.c,id,20.1)==GL_INVALID);CHECK(gl_setting_set(f.c,id,-.1)==GL_INVALID);
    }
    auto out=f.tick({10,30,0});near(out.yaw_degrees,0);near(out.pitch_degrees,0); // no observed active view
    f.state(77,true);out=f.tick({10,30,0});near(out.yaw_degrees,-6);near(out.pitch_degrees,2);
    f.set("context.77.sensitivity_y",19.94);double value=0;gl_setting_get(f.c,"context.77.sensitivity_y",&value);near(value,19.9);
    const char* path="gyrolib_sensitivity_range.ini";CHECK(gl_save_settings(f.c,path)==GL_OK);
    {std::ifstream in(path);std::string text((std::istreambuf_iterator<char>(in)),{});CHECK(text.find("schema=0.2.0")!=std::string::npos);}
    Fixture restored;CHECK(gl_load_settings(restored.c,path)==GL_OK);restored.mode(77);
    gl_setting_get(restored.c,"context.1.sensitivity_x",&value);near(value,20);
    gl_setting_get(restored.c,"context.77.sensitivity_x",&value);near(value,20);
    gl_setting_get(restored.c,"context.77.sensitivity_y",&value);near(value,19.9);
    {std::ofstream file(path);file<<"schema=0.2.0\ncontext.77.sensitivity_y=7.3\ncontext.1.gyro.space=3\ncontext.1.sensitivity_x=20\n";}
    CHECK(gl_load_settings(restored.c,path)==GL_OK);
    gl_setting_get(restored.c,"context.1.sensitivity_x",&value);near(value,20); // other views retain their own sensitivities
    gl_setting_get(restored.c,"context.77.sensitivity_y",&value);near(value,7.3);
    gl_setting_get(restored.c,"context.1.gyro.space",&value);near(value,GL_SPACE_WORLD);
    {std::ofstream file(path);file<<"schema=0.2.0\ncontext.1.sensitivity_x=20\ncontext.77.sensitivity_y=20.1\n";}
    CHECK(gl_load_settings(restored.c,path)==GL_INVALID);
    gl_setting_get(restored.c,"context.1.sensitivity_x",&value);near(value,20); // other views retain their own sensitivities
    std::filesystem::remove(path);
}
static void cursor_routing(){
    Fixture f;f.device();f.prime();CHECK(gl_get_selected_device(f.c)==7);
    struct Camera {unsigned calls{};double yaw{};} camera;
    gl_set_camera_callback(f.c,[](void* user,double yaw,double){auto& c=*static_cast<Camera*>(user);++c.calls;c.yaw+=yaw;},&camera);
    CHECK(gl_get_output_target(f.c)==GL_OUTPUT_CAMERA);f.tick();CHECK(camera.calls==1);near(camera.yaw,-.3);
    f.host.menu_open=1;f.tick();CHECK(camera.calls==1);near(f.out.yaw_degrees,0);
    CHECK(gl_set_output_target(f.c,GL_OUTPUT_CURSOR)==GL_OK);f.host.camera_allowed=0;
    f.tick();near(f.out.yaw_degrees,0); // no trusted menu observation
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_MENU_STATE|GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_LONG_PRESS_BLOCKING)==GL_OK);
    f.set("context.1.flick.mode",GL_FLICK_ON);f.controls.right_x=1;f.set("context.1.activation.block_long_press",1);
    for(int i=0;i<6;++i){f.tick();near(f.out.yaw_degrees,-.3);CHECK(!f.out.suppress_native_right_stick);}
    CHECK(camera.calls==1); // cursor deltas must never leak to the camera callback
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    f.host.focused=0;near(f.tick().yaw_degrees,0);f.host.focused=1;
    f.host.paused=1;near(f.tick().yaw_degrees,0);f.host.paused=0;
    gl_set_panel_open(f.c,1);near(f.tick().yaw_degrees,0);gl_set_panel_open(f.c,0);
    f.set("context.1.gyro.enabled",0);near(f.tick().yaw_degrees,0);f.set("context.1.gyro.enabled",1);
    f.host.menu_open=0;near(f.tick().yaw_degrees,0);f.host.menu_open=1;
    CHECK(gl_set_host_capabilities(f.c,0)==GL_OK);near(f.tick().yaw_degrees,0);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_MENU_STATE)==GL_OK);near(f.tick().yaw_degrees,-.3);
    CHECK(gl_set_output_target(f.c,GL_OUTPUT_CAMERA)==GL_OK);near(f.tick().yaw_degrees,0);
    f.host.menu_open=0;f.host.camera_allowed=1;near(f.tick().yaw_degrees,-.3);CHECK(camera.calls==2);
    CHECK(gl_set_output_target(f.c,99)==GL_INVALID);CHECK(gl_get_output_target(f.c)==GL_OUTPUT_CAMERA);
    CHECK(gl_set_output_target(nullptr,GL_OUTPUT_CURSOR)==GL_INVALID);
}
static void no_hidden_profile(){
    Fixture f(false);f.device();f.prime();gl_menu_tab tab{};
    CHECK(gl_menu_tab_count(f.c)==0);CHECK(gl_menu_tab_at(f.c,0,&tab)==GL_INVALID);
    CHECK(gl_setting_set(f.c,"gyro.sensitivity_x",20)==GL_UNAVAILABLE);
    CHECK(gl_setting_set(f.c,"flick.mode",GL_FLICK_ON)==GL_UNAVAILABLE);
    CHECK(gl_setting_set(f.c,"activation.block_long_press",1)==GL_UNAVAILABLE);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_LONG_PRESS_BLOCKING|GL_HOST_MENU_STATE)==GL_OK);
    unsigned callbacks=0;gl_set_camera_callback(f.c,[](void* user,double,double){++*static_cast<unsigned*>(user);},&callbacks);
    f.controls.right_x=1;
    for(int i=0;i<10;++i){auto out=f.tick();near(out.yaw_degrees,0);near(out.pitch_degrees,0);
        CHECK(!out.gyro_active&&!out.suppress_native_right_stick&&!gl_suppress_native_right_touchpad(f.c));}
    CHECK(callbacks==0);CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now,1,0)==GL_FORWARD);f.controls={};
    f.mode(7,"Normal view");
    CHECK(gl_menu_tab_count(f.c)==1);CHECK(gl_menu_tab_at(f.c,0,&tab)==GL_OK&&tab.id==8);
    CHECK(gl_menu_tab_setting_count(f.c,GL_TAB_CAMERA)==0);
    gl_setting_info row{};CHECK(gl_menu_tab_setting_at(f.c,GL_TAB_CAMERA,0,&row)==GL_INVALID);
    CHECK(!metadata(f.c,"gyro.sensitivity_x").visible);
    gl_diagnostics before{},after{};gl_get_diagnostics(f.c,&before);
    for(int i=0;i<10;++i){near(f.tick().yaw_degrees,0);CHECK(!f.out.gyro_active&&!f.out.suppress_native_right_stick);}
    gl_get_diagnostics(f.c,&after);CHECK(after.accepted_samples>before.accepted_samples);CHECK(callbacks==0);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now,1,0)==GL_FORWARD);
    f.state(7,true,false);near(f.tick().yaw_degrees,0); // unknown observation is not a default view
    f.state(7,true);near(f.tick().yaw_degrees,-.75);CHECK(callbacks==1);
    f.set("context.7.flick.mode",GL_FLICK_ON);f.set("context.7.gyro.activation",GL_HOLD_DISABLE);f.set("context.7.activation.button",1);f.set("context.7.activation.block_long_press",1);
    f.tick();CHECK(f.out.suppress_native_right_stick);
    CHECK(gl_filter_event(f.c,1,GL_PRESS,f.now,1,0)==GL_SUPPRESS);
    f.states.erase(7);near(f.tick().yaw_degrees,0);CHECK(!f.out.suppress_native_right_stick);
    CHECK(gl_filter_event(f.c,1,GL_RELEASE,f.now,1,0)==GL_SUPPRESS);
    gl_set_output_target(f.c,GL_OUTPUT_CURSOR);f.host.menu_open=1;f.host.camera_allowed=0;
    near(f.tick().yaw_degrees,0);CHECK(!f.out.gyro_active);
    f.state(7,true);near(f.tick().yaw_degrees,-.75); // immediate return, continuous orientation
    CHECK(gl_unregister_gameplay_context(f.c,7)==GL_OK);f.states.clear();
    CHECK(gl_menu_tab_count(f.c)==0);CHECK(gl_menu_tab_at(f.c,0,&tab)==GL_INVALID);
    near(f.tick().yaw_degrees,0);CHECK(!f.out.gyro_active); // cursor also requires a view
    gl_set_output_target(f.c,GL_OUTPUT_CAMERA);f.host.menu_open=0;f.host.camera_allowed=1;
    const auto previous_calls=callbacks;near(f.tick().yaw_degrees,0);CHECK(callbacks==previous_calls);
    f.mode(7);f.state(7,true);near(f.tick().yaw_degrees,-.75); // saved profile survives unregister/register
}
static void cursor_profile_metadata(){
    Fixture f(false);f.device();f.mode(10,"Camera");f.mode(20,"Cursor");
    CHECK(gl_set_gameplay_context_output_target(f.c,10,GL_OUTPUT_CAMERA)==GL_OK);
    CHECK(gl_set_gameplay_context_output_target(f.c,20,GL_OUTPUT_CURSOR)==GL_OK);
    CHECK(gl_set_gameplay_context_output_target(f.c,99,GL_OUTPUT_CAMERA)==GL_INVALID);
    CHECK(gl_set_gameplay_context_output_target(f.c,20,99)==GL_INVALID);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_MENU_STATE|GL_HOST_LONG_PRESS_BLOCKING)==GL_OK);
    f.state(10,true);f.prime();
    CHECK(metadata(f.c,"context.10.flick.mode").visible);
    CHECK(!metadata(f.c,"context.10.flick.duration_ms").visible);
    f.set("context.10.flick.mode",GL_FLICK_ON);
    CHECK(metadata(f.c,"context.10.flick.duration_ms").visible);
    CHECK(!metadata(f.c,"context.20.flick.mode").visible&&!metadata(f.c,"context.20.flick.mode").available);
    CHECK(!metadata(f.c,"context.20.flick.duration_ms").visible);
    gl_choice choice{};CHECK(gl_choice_at(f.c,"context.20.flick.mode",GL_FLICK_ON,&choice)==GL_OK&&!choice.available);
    f.set("context.20.flick.mode",GL_FLICK_ON);f.set("context.20.activation.block_long_press",1); // old saved values stay inert
    unsigned callbacks=0;gl_set_camera_callback(f.c,[](void* user,double,double){++*static_cast<unsigned*>(user);},&callbacks);
    f.state(10,false);f.state(20,true);f.host.menu_open=1;f.host.camera_allowed=0;
    f.controls.right_x=1;near(f.tick().yaw_degrees,-.75);CHECK(gl_get_output_target(f.c)==GL_OUTPUT_CURSOR);
    CHECK(callbacks==0&&!f.out.suppress_native_right_stick);
    CHECK(metadata(f.c,"context.10.flick.mode").visible); // editing another tab is independent of active destination
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now,1,0)==GL_FORWARD);
    gl_gameplay_context renamed{20,"Localized cursor","UI pointer",0};
    CHECK(gl_register_gameplay_context(f.c,&renamed)==GL_OK);CHECK(!metadata(f.c,"context.20.flick.mode").visible);
    f.state(20,false);f.state(10,true);f.host.menu_open=0;f.host.camera_allowed=1;
    f.tick();CHECK(gl_get_output_target(f.c)==GL_OUTPUT_CAMERA&&callbacks==1);
    CHECK(gl_set_gameplay_context_output_target(f.c,20,GL_OUTPUT_CAMERA)==GL_OK);
    CHECK(metadata(f.c,"context.20.flick.mode").visible);
}
static void activation_split(){
    Fixture f;f.device();f.prime();gl_set_host_capabilities(f.c,GL_HOST_LONG_PRESS_BLOCKING);
    f.set("context.1.activation.button",1);f.set("context.1.activation.touchpad",GL_SIDE_EITHER);f.set("context.1.activation.stick_touch",GL_SIDE_EITHER);
    f.set("context.1.activation.grip_touch",GL_SIDE_EITHER);f.set("context.1.activation.stick_deflection",GL_SIDE_EITHER);f.set("context.1.activation.block_long_press",1);
    f.controls.buttons=1;f.controls.touchpads=f.controls.stick_touch=f.controls.grip_touch=GL_LEFT;f.controls.right_x=1;
    CHECK(f.tick().gyro_active); // remembered bindings must not gate Always on
    const char* controls[]={"context.1.activation.button","context.1.activation.touchpad","context.1.activation.stick_touch","context.1.activation.grip_touch","context.1.activation.stick_deflection","context.1.activation.stick_threshold","context.1.activation.block_long_press"};
    for(auto id:controls)CHECK(!metadata(f.c,id).visible&&!metadata(f.c,id).available);
    CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_filter_event(f.c,0,GL_RELEASE,f.now,1,0)==GL_FORWARD);
    f.set("context.1.gyro.activation",GL_HOLD_DISABLE);CHECK(!f.tick().gyro_active);
    for(auto id:controls)CHECK(metadata(f.c,id).visible&&metadata(f.c,id).available);
    f.controls={};CHECK(f.tick().gyro_active);
    f.mode(20);f.state(20,true);f.set("context.20.activation.button",1);
    f.controls.buttons=1;CHECK(f.tick().gyro_active);CHECK(!metadata(f.c,"context.20.activation.button").visible);
    f.set("context.20.gyro.activation",GL_HOLD_DISABLE);CHECK(!f.tick().gyro_active);
    f.controls.buttons=0;CHECK(f.tick().gyro_active);
    const char* path="gyrolib_activation_modes.ini";
    const std::string old="schema=0.2.0\ncontext.1.gyro.activation=5\ncontext.1.activation.button=1\ncontext.20.gyro.activation=5\ncontext.20.activation.touchpad=3\ncontext.30.gyro.activation=0\n";
    {std::ofstream file(path);file<<old;}
    CHECK(gl_load_settings(f.c,path)==GL_OK);double value=0;
    gl_setting_get(f.c,"context.1.gyro.activation",&value);near(value,GL_HOLD_DISABLE);
    gl_setting_get(f.c,"context.20.gyro.activation",&value);near(value,GL_HOLD_DISABLE);
    {std::ifstream file(path);CHECK(std::string((std::istreambuf_iterator<char>(file)),{})==old);} // loading is read-only
    f.mode(30);gl_setting_get(f.c,"context.30.gyro.activation",&value);near(value,GL_ALWAYS);
    f.set("context.20.gyro.activation",GL_ALWAYS); // Always on retains its ignored bindings when reloaded
    Fixture roundtrip;CHECK(gl_load_settings(roundtrip.c,path)==GL_OK);roundtrip.mode(20);
    gl_setting_get(roundtrip.c,"context.20.gyro.activation",&value);near(value,GL_ALWAYS);
    gl_setting_get(roundtrip.c,"context.20.activation.touchpad",&value);near(value,GL_SIDE_EITHER);
    std::filesystem::remove(path);
}
static void shared_toggle(){
    Fixture f(false);f.device();f.mode(10);f.mode(20);f.mode(30);f.state(10,true);
    for(auto id:{"context.10.gyro.activation","context.20.gyro.activation"})f.set(id,GL_TOGGLE);
    for(auto id:{"context.10.activation.button","context.20.activation.button"})f.set(id,1);
    f.prime();CHECK(f.out.gyro_active);
    f.controls.buttons=1;CHECK(!f.tick().gyro_active);
    f.state(10,false);f.state(20,true);CHECK(!f.tick().gyro_active); // first entry inherits OFF, held press is not replayed
    f.controls.buttons=0;CHECK(!f.tick().gyro_active);f.controls.buttons=1;CHECK(f.tick().gyro_active);
    f.state(20,false);f.state(10,true);CHECK(f.tick().gyro_active); // ON also carries back while held
    f.controls.buttons=0;f.tick();f.controls.buttons=1;CHECK(!f.tick().gyro_active);
    f.state(10,false);f.state(30,true);CHECK(f.tick().gyro_active); // Always on ignores the shared latch
    f.controls.buttons=0;f.tick();f.state(30,false);f.state(20,true);CHECK(!f.tick().gyro_active);
    f.host.focused=0;f.controls.buttons=1;f.tick();f.host.focused=1;CHECK(!f.tick().gyro_active);
    f.set("context.20.activation.button",2);f.controls.buttons=2;CHECK(!f.tick().gyro_active); // editing held binding is not a new press
    f.controls.buttons=0;f.tick();f.controls.buttons=2;CHECK(f.tick().gyro_active);
    f.state(20,false);f.state(10,true);CHECK(f.tick().gyro_active);
    f.controls.buttons=0;f.tick();f.controls.buttons=1;CHECK(!f.tick().gyro_active);
    gl_set_gameplay_context_output_target(f.c,20,GL_OUTPUT_CURSOR);gl_set_host_capabilities(f.c,GL_HOST_MENU_STATE);
    f.state(10,false);f.state(20,true);f.host.menu_open=1;f.host.camera_allowed=0;CHECK(!f.tick().gyro_active);
    f.controls.buttons=0;f.tick();f.controls.buttons=2;CHECK(f.tick().gyro_active);
    f.state(20,false);f.state(10,true);f.host.menu_open=0;f.host.camera_allowed=1;CHECK(f.tick().gyro_active);
}
static void automatic_persistence(){
    const char* path="gyrolib_autosave_test.ini";
    Fixture f;f.mode(10);f.mode(20);CHECK(gl_get_settings_save_result(f.c)==GL_UNAVAILABLE);
    CHECK(gl_set_settings_path(f.c,path)==GL_OK);
    f.set("context.10.sensitivity_x",8.4);CHECK(gl_setting_set(f.c,"ui.scale",1.5)==GL_UNAVAILABLE);CHECK(gl_set_language(f.c,"fr")==GL_OK);
    CHECK(gl_get_settings_save_result(f.c)==GL_OK);CHECK(!metadata(f.c,"settings.save").visible);
    Fixture restored;CHECK(gl_load_settings(restored.c,path)==GL_OK);restored.mode(10);double value=0;
    gl_setting_get(restored.c,"context.10.sensitivity_x",&value);near(value,8.4);
    CHECK(gl_setting_get(restored.c,"ui.scale",&value)==GL_UNAVAILABLE);CHECK(std::strcmp(gl_get_language(restored.c),"fr")==0);
    f.set("context.20.sensitivity_y",9);gl_unregister_gameplay_context(f.c,20);
    CHECK(gl_action(f.c,"settings.reset")==GL_OK);CHECK(gl_load_settings(restored.c,path)==GL_OK);restored.mode(20);
    gl_setting_get(restored.c,"context.10.sensitivity_x",&value);near(value,2.5);
    gl_setting_get(restored.c,"context.20.sensitivity_y",&value);near(value,2.5);
    // A regular file as parent guarantees an I/O failure without OS permission changes.
    const auto bad=std::string(path)+"/blocked.ini";gl_set_settings_path(f.c,bad.c_str());
    CHECK(gl_setting_set(f.c,"context.10.sensitivity_x",6)==GL_IO_ERROR);
    gl_setting_get(f.c,"context.10.sensitivity_x",&value);near(value,6);CHECK(gl_get_settings_save_result(f.c)==GL_IO_ERROR);
    gl_set_settings_path(f.c,path);CHECK(gl_action(f.c,"settings.save")==GL_OK);CHECK(gl_get_settings_save_result(f.c)==GL_OK);
    CHECK(gl_load_settings(restored.c,path)==GL_OK);gl_setting_get(restored.c,"context.10.sensitivity_x",&value);near(value,6);
    {std::ofstream file(path);file<<"schema=999.0.0\nfuture=preserve\n";}
    CHECK(gl_setting_set(f.c,"calibration.automatic",2)==GL_NEWER_SCHEMA);CHECK(gl_get_settings_save_result(f.c)==GL_NEWER_SCHEMA);
    {std::ifstream file(path);CHECK(std::string((std::istreambuf_iterator<char>(file)),{})=="schema=999.0.0\nfuture=preserve\n");}
    std::filesystem::remove(path);
}
static void motion_companions(){
    Fixture f;gl_endpoint sensor{};sensor.id=sensor.physical_id=81;sensor.connected=1;
    sensor.source=GL_SOURCE_SDL;sensor.caps.gyro=sensor.caps.accelerometer=1;
    CHECK(gl_register_endpoint(f.c,&sensor)==GL_OK);CHECK(gl_set_motion_companion(f.c,81)==GL_OK);
    CHECK(!gl_endpoint_motion_available(f.c,81)); // capability alone is not a usable sensor
    f.tick({},10000000,false);CHECK(gl_get_selected_device(f.c)==0); // an unpaired sensor cannot steal player selection
    gl_endpoint controller{};controller.id=1;controller.physical_id=7;controller.connected=1;
    controller.source=GL_SOURCE_SDL;controller.caps.buttons=2;
    CHECK(gl_register_endpoint(f.c,&controller)==GL_OK);f.tick({},10000000,false);CHECK(gl_get_selected_device(f.c)==7);
    CHECK(!gl_endpoint_motion_available(f.c,1)); // Steam's virtual controls have no sensor
    CHECK(gl_bind_motion_sensor(f.c,99,81)==GL_UNAVAILABLE);
    CHECK(gl_bind_motion_sensor(f.c,7,1)==GL_INVALID);
    CHECK(gl_bind_motion_sensor(f.c,7,81)==GL_OK);CHECK(gl_get_motion_sensor(f.c,7)==81);
    for(int i=0;i<5;++i){f.now+=10000000;gl_sample sample{f.now,f.now,{0,20,0},{0,1,0}};
        CHECK(gl_submit_sample(f.c,81,&sample)==GL_OK);CHECK((f.report(),gl_update(f.c,f.now,&f.host,&f.out))==GL_OK);}
    CHECK(f.out.endpoint_id==81);CHECK(std::abs(f.out.yaw_degrees)>.01);CHECK(gl_get_selected_device(f.c)==7);
    CHECK(gl_endpoint_motion_available(f.c,81));
    f.now+=200000000;CHECK((f.report(),gl_update(f.c,f.now,&f.host,&f.out))==GL_OK);CHECK(!gl_endpoint_motion_available(f.c,81));
    // Removing a companion must retain the player's virtual controller.
    CHECK(gl_bind_motion_sensor(f.c,7,0)==GL_OK);CHECK(gl_get_selected_device(f.c)==7);
    f.now+=10000000;CHECK((f.report(),gl_update(f.c,f.now,&f.host,&f.out))==GL_OK);CHECK(f.out.source==GL_SOURCE_NONE);
    CHECK(gl_get_motion_sensor(f.c,7)==0);
    CHECK(gl_bind_motion_sensor(f.c,7,81)==GL_OK);CHECK(gl_forget_endpoint(f.c,81)==GL_OK);
    CHECK(gl_get_motion_sensor(f.c,7)==0);CHECK(gl_get_selected_device(f.c)==7);
}
static void automatic_motion_sensor(){
    Fixture f;
    auto controller=[&](uint64_t id,uint64_t physical,uint32_t vendor){
        gl_endpoint e{};e.id=id;e.physical_id=physical;e.source=GL_SOURCE_SDL;e.connected=1;
        CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(gl_set_endpoint_pairing_hint(f.c,id,vendor,0x1302,1)==GL_OK);
    };
    auto sensor=[&](uint64_t id,uint32_t vendor){
        f.device(id,GL_SOURCE_SDL,id);CHECK(gl_set_motion_companion(f.c,id)==GL_OK);
        CHECK(gl_set_endpoint_pairing_hint(f.c,id,vendor,0x1304,0)==GL_OK);
    };
    auto frames=[&](int count,bool second=false){for(int i=0;i<count;++i){f.now+=10000000;f.submit(81);if(second)f.submit(82);CHECK((f.report(),gl_update(f.c,f.now,&f.host,&f.out))==GL_OK);}};
    controller(1,7,0x28de);sensor(81,0x28de);
    frames(5);CHECK(!gl_get_motion_sensor(f.c,7));CHECK(!gl_motion_sensor_needs_selection(f.c,7)); // qualifying
    frames(35);CHECK(gl_get_motion_sensor(f.c,7)==81);CHECK(f.out.endpoint_id==81&&std::abs(f.out.yaw_degrees)>.01);
    CHECK(gl_get_selected_device(f.c)==7&&!gl_motion_sensor_needs_selection(f.c,7));
    // A stall and a newly connected candidate never change an existing binding.
    f.now+=500000000;(f.report(),gl_update(f.c,f.now,&f.host,&f.out));CHECK(gl_get_motion_sensor(f.c,7)==81);
    sensor(82,0x28de);frames(3,true);CHECK(gl_get_motion_sensor(f.c,7)==81);
    // Manual clearing remains respected even when automatic pairing is possible.
    gl_bind_motion_sensor(f.c,7,0);gl_forget_endpoint(f.c,82);frames(40);CHECK(!gl_get_motion_sensor(f.c,7));
    CHECK(gl_motion_sensor_needs_selection(f.c,7));
    gl_forget_endpoint(f.c,1);f.tick({},10000000,false);controller(1,7,0x28de);frames(3);
    CHECK(gl_get_motion_sensor(f.c,7)==81); // controller reconnection renews automatic policy
    // A removed/recreated sensor is qualified again; there is no saved endpoint ID.
    gl_forget_endpoint(f.c,81);sensor(81,0x28de);frames(40);CHECK(gl_get_motion_sensor(f.c,7)==81);

    Fixture g;gl_endpoint pad{};pad.id=1;pad.physical_id=7;pad.connected=1;pad.source=GL_SOURCE_SDL;
    gl_register_endpoint(g.c,&pad);gl_set_endpoint_pairing_hint(g.c,1,0x054c,0xce6,1);
    g.device(81,GL_SOURCE_SDL,81);gl_set_motion_companion(g.c,81);gl_set_endpoint_pairing_hint(g.c,81,0x28de,0x1304,0);
    auto advance=[&](int n){for(int i=0;i<n;++i){g.now+=10000000;g.submit(81);(g.report(),gl_update(g.c,g.now,&g.host,&g.out));}};
    advance(40);CHECK(!gl_get_motion_sensor(g.c,7));CHECK(gl_motion_sensor_needs_selection(g.c,7)); // different vendor, no guessing
    gl_set_endpoint_pairing_hint(g.c,81,0x054c,0xce6,0);
    g.device(82,GL_SOURCE_SDL,82);gl_set_motion_companion(g.c,82);gl_set_endpoint_pairing_hint(g.c,82,0x054c,0xce6,0);
    advance(40);CHECK(!gl_get_motion_sensor(g.c,7)); // second candidate is silent, still ambiguous
    gl_forget_endpoint(g.c,82);pad.id=2;pad.physical_id=8;gl_register_endpoint(g.c,&pad);gl_set_endpoint_pairing_hint(g.c,2,0x054c,0xce6,1);
    advance(40);CHECK(!gl_get_motion_sensor(g.c,7)); // two virtual controllers
    gl_forget_endpoint(g.c,2);advance(3);CHECK(gl_get_motion_sensor(g.c,7)==81);
}
static void companion_activators(){
    Fixture f;f.device();f.device(81,GL_SOURCE_SDL,81);gl_set_motion_companion(f.c,81);
    CHECK(gl_bind_motion_sensor(f.c,7,81)==GL_OK);
    gl_set_button_label(f.c,81,0,"Physical A",GL_LABEL_PHYSICAL);
    f.set("context.1.gyro.activation",GL_HOLD);f.set("context.1.activation.button",1);
    gl_controls physical{};auto update=[&](bool fresh_physical=true){
        f.now+=10000000;gl_sample sample{f.now,f.now,{0,30,0},{0,1,0}};
        CHECK(gl_submit_sample(f.c,81,&sample)==GL_OK);f.controls.timestamp_ns=f.now;
        CHECK(gl_submit_controls(f.c,1,&f.controls)==GL_OK);f.report();
        if(fresh_physical){physical.timestamp_ns=f.now;CHECK(gl_submit_controls(f.c,81,&physical)==GL_OK);}
        CHECK((f.report(),gl_update(f.c,f.now,&f.host,&f.out))==GL_OK);
    };
    f.controls.buttons=1;for(int i=0;i<4;++i)update();
    CHECK(!f.out.gyro_active); // virtual A may be a remapped physical B
    CHECK(std::strcmp(gl_get_button_label(f.c,0),"Physical A")==0);
    physical.buttons=1;update();CHECK(f.out.gyro_active);
    physical.buttons=0;update();CHECK(!f.out.gyro_active);
    physical.buttons=1;update();CHECK(f.out.gyro_active);
    for(int i=0;i<20;++i)update(false);
    CHECK(!f.out.gyro_active); // no stuck press or virtual-button fallback
    physical.buttons=0;update();CHECK(!f.out.gyro_active);
    // Unpaired contacts cannot activate the selected player's gyro.
    f.device(82,GL_SOURCE_SDL,82);gl_controls stranger{};stranger.timestamp_ns=f.now;stranger.grip_touch=3;
    gl_submit_controls(f.c,82,&stranger);f.set("context.1.activation.button",0);f.set("context.1.activation.grip_touch",GL_SIDE_BOTH);
    update();CHECK(!f.out.gyro_active);physical.grip_touch=3;update();CHECK(f.out.gyro_active);
    // The companion has no sticks; the host's virtual right stick still drives flick.
    for(uint32_t n=0;n<gl_endpoint_count(f.c);++n){gl_endpoint e{};gl_get_endpoint(f.c,n,&e);
        if(e.id==81){e.caps.sticks=0;gl_register_endpoint(f.c,&e);}}
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION);
    f.set("context.1.flick.mode",GL_FLICK_ON);f.set("context.1.flick.duration_ms",0);physical.right_x=0;f.controls.right_x=0;update();
    f.controls.right_x=1;update();CHECK(f.out.suppress_native_right_stick);CHECK(std::abs(f.out.yaw_degrees)>80);
    // Duplicate contacts resolve on this device without changing saved bindings.
    f.mode(10);f.set("context.10.activation.button",26);f.set("context.10.activation.grip_touch",GL_SIDE_LEFT);
    f.set("context.1.activation.button",26);f.set("context.1.activation.grip_touch",GL_SIDE_OFF);
    CHECK(gl_set_button_contact(f.c,81,25,GL_CONTACT_GRIP,GL_RIGHT)==GL_OK);
    physical.buttons=1u<<25;physical.grip_touch=GL_RIGHT;update();double value=0;
    CHECK(gl_setting_get(f.c,"context.1.activation.button",&value)==GL_OK);near(value,26);
    CHECK(gl_setting_get(f.c,"context.1.activation.grip_touch",&value)==GL_OK);near(value,GL_SIDE_OFF);
    CHECK(gl_setting_get(f.c,"context.10.activation.button",&value)==GL_OK);near(value,26);
    CHECK(gl_setting_get(f.c,"context.10.activation.grip_touch",&value)==GL_OK);near(value,GL_SIDE_LEFT);
    CHECK(gl_setting_get_effective(f.c,"context.1.activation.button",&value)==GL_OK);near(value,0);
    CHECK(gl_setting_get_effective(f.c,"context.1.activation.grip_touch",&value)==GL_OK);near(value,GL_SIDE_RIGHT);
    CHECK(gl_setting_get_effective(f.c,"context.10.activation.grip_touch",&value)==GL_OK);near(value,GL_SIDE_EITHER);
    for(const char* key:{"context.1.activation.button","context.10.activation.button"}){
        gl_choice choice{};CHECK(gl_choice_at(f.c,key,26,&choice)==GL_OK);CHECK(!choice.available);
        CHECK(gl_choice_at(f.c,key,9,&choice)==GL_OK);CHECK(choice.available);
    }
    CHECK(gl_set_button_contact(f.c,81,25,GL_CONTACT_NONE,0)==GL_OK);update();
    gl_choice choice{};CHECK(gl_choice_at(f.c,"context.1.activation.button",26,&choice)==GL_OK);CHECK(choice.available);
}
static void controller_takeover(){
    Fixture f;f.device();f.prime();f.device(2,GL_SOURCE_SDL,8);
    f.device(81,GL_SOURCE_SDL,81);gl_set_motion_companion(f.c,81);gl_bind_motion_sensor(f.c,7,81);
    f.now+=10000000;f.submit(2,{});(f.report(),gl_update(f.c,f.now,&f.host,&f.out));
    CHECK(gl_get_selected_device(f.c)==7); // a live choice cannot be stolen
    gl_disconnect_endpoint(f.c,1);
    f.now+=10000000;f.submit(2,{});(f.report(),gl_update(f.c,f.now,&f.host,&f.out));
    CHECK(gl_get_selected_device(f.c)==8&&gl_get_motion_sensor(f.c,7)==0);
    gl_endpoint e{};for(unsigned i=0;i<gl_endpoint_count(f.c);++i){gl_get_endpoint(f.c,i,&e);if(e.id==81)CHECK(e.physical_id==81);}
    gl_forget_endpoint(f.c,2);f.now+=10000000;(f.report(),gl_update(f.c,f.now,&f.host,&f.out));
    CHECK(gl_get_selected_device(f.c)==0); // companions cannot become the game controller
    f.device(3,GL_SOURCE_SDL,9);f.now+=10000000;f.submit(3,{});(f.report(),gl_update(f.c,f.now,&f.host,&f.out));
    CHECK(gl_get_selected_device(f.c)==9);
    f.device();f.now+=10000000;(f.report(),gl_update(f.c,f.now,&f.host,&f.out));CHECK(gl_get_selected_device(f.c)==9);
    // Keep a selected group when its other ordinary provider is still connected.
    f.device(4,GL_SOURCE_STEAM,9);gl_disconnect_endpoint(f.c,3);
    f.now+=10000000;(f.report(),gl_update(f.c,f.now,&f.host,&f.out));CHECK(gl_get_selected_device(f.c)==9);
}
static void touchpad_flick(){
    Fixture f;f.device();f.set("context.1.gyro.enabled",0);f.set("context.1.flick.duration_ms",0);
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
    gl_flick_input input{};input.available=GL_FLICK_INPUT_STICK|GL_FLICK_INPUT_TOUCHPAD;
    const auto step=[&](bool send=true){
        f.now+=10000000;f.submit(1,{});if(send){input.timestamp_ns=f.now;CHECK(gl_submit_flick_input(f.c,1,&input)==GL_OK);}
        f.report();
        CHECK((f.report(),gl_update(f.c,f.now,&f.host,&f.out))==GL_OK);return f.out.yaw_degrees;
    };
    step();step();gl_choice choice{};
    for(auto value:{GL_FLICK_OFF,GL_FLICK_ON,GL_FLICK_TOUCHPAD,GL_FLICK_BOTH}){
        CHECK(gl_choice_at(f.c,"context.1.flick.mode",value,&choice)==GL_OK);CHECK(choice.available);
    }
    gl_choice_at(f.c,"context.1.flick.mode",GL_FLICK_ON,&choice);CHECK(std::strcmp(choice.label,"Stick")==0);
    f.set("context.1.flick.mode",GL_FLICK_TOUCHPAD);step();
    const auto feedback=[&]{gl_touchpad_feedback value{};CHECK(gl_get_touchpad_feedback(f.c,&value)==GL_OK);
        CHECK(value.timestamp_ns==f.now&&value.physical_id==7);return value.pulse;};
    input.touching=1;input.touchpad_x=1;f.controls.right_x=1; // duplicate Steam virtual stick
    near(step(),90);CHECK(!f.out.suppress_native_right_stick&&gl_suppress_native_right_touchpad(f.c));
    CHECK(feedback());near(step(),0);CHECK(!feedback()); // holding still must not buzz
    input.touchpad_x=0;input.touchpad_y=1;near(step(),-90);
    CHECK(feedback());f.host.focused=0;step();CHECK(!feedback());f.host.focused=1;
    step();input.touchpad_x=1;input.touchpad_y=0;step(); // restore the original angle after focus resume
    input.touching=0;near(step(),0);input.touching=1;input.touchpad_x=-1;input.touchpad_y=0;near(step(),-90);
    input.touching=0;input.touchpad_x=0;step();
    input.touching=1;input.touchpad_x=.15f;near(step(),0); // central resting area
    input.touchpad_x=.4f;near(step(),90); // reduced touchpad threshold, previously .9
    input.touchpad_x=0;input.touchpad_y=.3f;near(step(),-90); // circular motion below entry radius
    input.touching=0;input.touchpad_y=0;step();
    f.set("context.1.flick.mode",GL_FLICK_ON);step();input.touching=1;input.touchpad_x=1;near(step(),0); // pad ignored
    CHECK(f.out.suppress_native_right_stick&&!gl_suppress_native_right_touchpad(f.c));
    CHECK(!feedback());
    input.touching=0;input.touchpad_x=0;f.set("context.1.flick.mode",GL_FLICK_BOTH);step();
    input.stick_x=input.touchpad_x=1;input.touching=1;near(step(),180); // two real sources, virtual copy ignored
    input.stick_x=input.touchpad_x=0;input.touching=0;step();
    f.set("context.1.flick.mode",GL_FLICK_TOUCHPAD);f.set("context.1.flick.duration_ms",150);step();
    input.touching=1;input.touchpad_x=1;double total=step();input.touching=0;
    for(int n=0;n<20;++n)total+=step();near(total,90); // release does not truncate a scheduled pivot
    f.set("context.1.flick.duration_ms",0);step();
    input.touching=1;input.touchpad_x=1;step();
    for(int n=0;n<20;++n)step(false);
    near(f.out.yaw_degrees,0);CHECK(!gl_suppress_native_right_touchpad(f.c));
    input.touching=0;step();
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION);input.touching=1;near(step(),0);
    CHECK(!gl_suppress_native_right_touchpad(f.c));gl_choice_at(f.c,"context.1.flick.mode",GL_FLICK_TOUCHPAD,&choice);CHECK(!choice.available);
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION|GL_HOST_MENU_STATE);
    // A mode change while touching the edge suppresses the initial pivot, while circular movement resumes immediately.
    f.mode(10);f.set("context.10.gyro.enabled",0);f.set("context.10.flick.mode",GL_FLICK_TOUCHPAD);f.set("context.10.flick.duration_ms",0);
    f.state(10,true);near(step(),0);input.touchpad_x=0;input.touchpad_y=1;near(step(),-90);
    gl_set_gameplay_context_output_target(f.c,10,GL_OUTPUT_CURSOR);f.host.menu_open=1;near(step(),0);
    CHECK(!f.out.suppress_native_right_stick&&!gl_suppress_native_right_touchpad(f.c));
    CHECK(!feedback());
    gl_choice_at(f.c,"context.10.flick.mode",GL_FLICK_TOUCHPAD,&choice);CHECK(!choice.available);
    input.touchpad_x=std::numeric_limits<float>::quiet_NaN();CHECK(gl_submit_flick_input(f.c,1,&input)==GL_INVALID);
    // Stick and Either modes survive a settings round trip.
    const auto path=(std::filesystem::temp_directory_path()/"gyrolib-flick-input-test.ini").string();
    {std::ofstream file(path);file<<"schema=0.2.0\ncontext.1.flick.mode=2\n";}Fixture loaded;
    CHECK(gl_load_settings(loaded.c,path.c_str())==GL_OK);double value=0;gl_setting_get(loaded.c,"context.1.flick.mode",&value);near(value,GL_FLICK_ON);
    loaded.set("context.1.flick.mode",GL_FLICK_BOTH);Fixture roundtrip;CHECK(gl_load_settings(roundtrip.c,path.c_str())==GL_OK);
    gl_setting_get(roundtrip.c,"context.1.flick.mode",&value);near(value,GL_FLICK_BOTH);std::filesystem::remove(path);
}
static void choice_help(){
    Fixture f;f.device();f.mode(10);f.prime();
    for(const char* lang:{"en","fr"}){
        CHECK(gl_set_language(f.c,lang)==GL_OK);
        for(const char* id:{"context.1.gyro.space","context.10.gyro.space","context.1.gyro.activation","context.1.gyro.acceleration","calibration.automatic","context.1.flick.mode","context.1.activation.touchpad","context.1.activation.button"}){
            for(uint32_t i=0;i<gl_menu_choice_count(f.c,id);++i){gl_choice choice{};CHECK(gl_choice_at(f.c,id,i,&choice)==GL_OK);
                if(!choice.available)continue;const auto* help=gl_choice_description(f.c,id,choice.value);
                CHECK(help&&*help&&std::strcmp(help,"?")!=0);
            }
        }
        CHECK(std::strcmp(gl_choice_description(f.c,"context.1.gyro.space",GL_SPACE_PLAYER),gl_choice_description(f.c,"context.1.gyro.space",GL_SPACE_WORLD))!=0);
        CHECK(std::strcmp(gl_choice_description(f.c,"context.10.gyro.space",GL_SPACE_PLAYER),gl_choice_description(f.c,"context.1.gyro.space",GL_SPACE_PLAYER))==0);
        gl_menu_tab tab{};CHECK(gl_menu_tab_at(f.c,0,&tab)==GL_OK&&std::strcmp(tab.description,"Synthetic resolved command state")==0);
        for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,tab.id);++i){gl_setting_info setting{};
            CHECK(gl_menu_tab_setting_at(f.c,tab.id,i,&setting)==GL_OK);
            CHECK(!std::strstr(setting.description,"Synthetic resolved command state"));}
    }
    CHECK(!*gl_choice_description(f.c,"context.1.gyro.space",99));CHECK(!*gl_choice_description(f.c,"context.1.gyro.space",.5));
    CHECK(!*gl_choice_description(f.c,"missing",0));CHECK(!*gl_choice_description(nullptr,"context.1.gyro.space",0));
}
static void menu_camera_routing(){
    Fixture f;f.device();f.prime();f.host.menu_open=1;
    int recentered=0;
    gl_set_recenter_callback(f.c,[](void* p){++*static_cast<int*>(p);},&recentered);
    const auto caps=GL_HOST_MENU_STATE|GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_LONG_PRESS_BLOCKING;
    CHECK(gl_set_host_capabilities(f.c,caps)==GL_OK);
    near(f.tick().yaw_degrees,0); // unchanged default for existing integrations
    CHECK(gl_set_gameplay_context_camera_in_menu(nullptr,1,1)==GL_INVALID);
    CHECK(gl_set_gameplay_context_camera_in_menu(f.c,99,1)==GL_INVALID);
    CHECK(gl_set_gameplay_context_camera_in_menu(f.c,1,2)==GL_INVALID);
    CHECK(gl_set_gameplay_context_camera_in_menu(f.c,1,1)==GL_OK);
    near(f.tick().yaw_degrees,-.3);
    gl_gameplay_context renamed{1,"Renamed camera","New localized metadata",0};
    CHECK(gl_register_gameplay_context(f.c,&renamed)==GL_OK);
    near(f.tick().yaw_degrees,-.3);
    CHECK(gl_request_recenter(f.c)==GL_OK);f.tick();CHECK(recentered==1);
    f.set("context.1.gyro.activation",GL_GYRO_OFF);near(f.tick().yaw_degrees,0);
    f.set("context.1.flick.mode",GL_FLICK_ON);f.set("context.1.flick.duration_ms",0);
    f.tick({});f.controls.right_x=1;near(f.tick({}).yaw_degrees,90);CHECK(f.out.suppress_native_right_stick);
    for(auto flag:{&f.host.paused,&f.host.focused,&f.host.camera_allowed}){
        const auto previous=*flag;*flag=flag==&f.host.paused?1:0;
        gl_request_recenter(f.c);near(f.tick().yaw_degrees,0);CHECK(!f.out.suppress_native_right_stick);
        *flag=previous;f.tick({});CHECK(recentered==1); // discarded, never replayed
    }
    gl_set_panel_open(f.c,1);gl_request_recenter(f.c);f.tick();CHECK(!f.out.suppress_native_right_stick&&recentered==1);
    gl_set_panel_open(f.c,0);
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION);near(f.tick().yaw_degrees,0);CHECK(!f.out.suppress_native_right_stick);
    gl_set_host_capabilities(f.c,caps);
    CHECK(gl_set_gameplay_context_camera_in_menu(f.c,1,0)==GL_OK);f.tick();CHECK(!f.out.suppress_native_right_stick);
    CHECK(gl_set_gameplay_context_camera_in_menu(f.c,1,1)==GL_OK);
    f.state(1,false);f.tick();CHECK(!f.out.suppress_native_right_stick);
    f.state(1,true,false);f.tick();CHECK(!f.out.suppress_native_right_stick);
    f.state(1,true);f.set("context.1.flick.mode",GL_FLICK_OFF);f.set("context.1.gyro.activation",GL_ALWAYS);
    gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CURSOR);gl_request_recenter(f.c);
    near(f.tick().yaw_degrees,-.3);CHECK(recentered==1&&!f.out.suppress_native_right_stick);
    gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CAMERA);
    f.set("context.1.gyro.activation",GL_HOLD);f.set("context.1.activation.button",1);f.set("context.1.activation.block_long_press",1);
    f.tick();CHECK(gl_filter_event(f.c,0,GL_PRESS,f.now,1,0)==GL_FORWARD);
    CHECK(gl_unregister_gameplay_context(f.c,1)==GL_OK);f.mode(1);f.state(1,true);near(f.tick().yaw_degrees,0);
    Fixture calibration;calibration.device();calibration.host.menu_open=1;
    gl_set_host_capabilities(calibration.c,GL_HOST_MENU_STATE);gl_set_gameplay_context_camera_in_menu(calibration.c,1,1);
    calibration.set("calibration.automatic",GL_CAL_MENUS);
    for(int i=0;i<1200;++i)calibration.tick({0,.5f,0});
    gl_diagnostics d{};gl_get_diagnostics(calibration.c,&d);near(d.bias.y,0);
}
int main(){
    unsigned failures=0;
    for(auto test:{gamepad_menu_shortcut,temporal,gains_and_spaces,activation,source_selection,calibration,noisy_calibration,flick,block_long_press,settings,gameplay_contexts,context_flick,steam_adapter,gyro_spaces,button_names,sensitivity_range,cursor_routing,profile_persistence,independent_profiles,no_hidden_profile,cursor_profile_metadata,activation_split,shared_toggle,automatic_persistence,motion_companions,automatic_motion_sensor,companion_activators,controller_takeover,touchpad_flick,choice_help,menu_camera_routing}){
        try{test();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';++failures;}
    }
    if(failures)return 1;
    std::cout<<"30 core regression groups passed (synthetic samples; no physical controller validation).\n";return 0;
}
