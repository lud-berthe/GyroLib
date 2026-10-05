#include <gyrolib/gyrolib.hpp>
#include "../src/detail/internal.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
static void near(double a,double b,double eps=1e-4){if(std::abs(a-b)>eps)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
struct Fixture{
    gyrolib::Context owner;gl_context* c=owner.get();
    uint64_t now=1000000000;gl_host_state host{};gl_controls controls{};gl_output out{};bool accel;
    explicit Fixture(bool accelerometer=true):accel(accelerometer){
        const gl_gameplay_context view{1,"Camera","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);
        set("gyro.space",GL_SPACE_LOCAL_YAW);set("sensitivity_x",1);set("sensitivity_y",1);
        gl_endpoint e{};e.id=e.physical_id=1;e.connected=1;e.source=GL_SOURCE_SDL;
        e.caps={0xffffffffu,3,3,3,3,1,uint32_t(accel)};CHECK(gl_register_endpoint(c,&e)==GL_OK);
        host.focused=host.camera_allowed=1;for(int i=0;i<8;++i)tick();
    }
    void set(const char* key,double value){CHECK(gl_setting_set(c,(std::string("context.1.")+key).c_str(),value)==GL_OK);}
    void sample(uint64_t t,gl_vec3 gyro={},bool with_accel=true){
        const gl_sample s{t,t,gyro,accel&&with_accel?gl_vec3{0,1,0}:gl_vec3{}};CHECK(gl_submit_sample(c,1,&s)==GL_OK);
    }
    gl_output update(){CHECK(gl_set_gameplay_context_state(c,1,1,1)==GL_OK);CHECK(gl_update(c,now,&host,&out)==GL_OK);return out;}
    gl_output tick(gl_vec3 gyro={},bool report=true,uint64_t dt=10000000){
        now+=dt;if(report)sample(now,gyro);controls.timestamp_ns=now;CHECK(gl_submit_controls(c,1,&controls)==GL_OK);return update();
    }
    gl_gyro_state state(){gl_gyro_state s{};CHECK(gl_get_gyro_state(c,&s)==GL_OK);return s;}
};
static void stable_activity_and_local_only(){
    Fixture f;f.tick({0,90,0});CHECK(f.state().enabled&&f.state().new_samples==1);
    gl_input_metrics metrics{};CHECK(gl_get_input_metrics(f.c,1,&metrics)==GL_OK);near(metrics.report_hz,100);
    near(metrics.interval_jitter_ms,0);CHECK(metrics.gyro_available&&metrics.gravity_available&&metrics.sample_age_ns==0);
    f.tick({},false);CHECK(f.state().enabled&&f.state().motion_available&&!f.state().new_samples&&!f.out.gyro_active);
    f.tick({0,90,0});CHECK(f.state().enabled&&f.state().new_samples==1);
    CHECK(gl_get_input_metrics(f.c,1,&metrics)==GL_OK);CHECK(metrics.report_hz<100&&metrics.interval_jitter_ms>0);
    f.host.focused=0;f.tick();CHECK(!f.state().enabled);f.host.focused=1;f.tick();CHECK(f.state().enabled);
    f.tick({},false,160000000);CHECK(!f.state().enabled&&!f.state().motion_available);
    Fixture local(false);near(local.tick({0,90,0}).yaw_degrees,-.9);CHECK(local.state().enabled);
    local.set("gyro.space",GL_SPACE_LOCAL_ROLL);near(local.tick({0,0,90}).yaw_degrees,.9);
    local.set("gyro.space",GL_SPACE_PLAYER);near(local.tick({0,90,0}).yaw_degrees,0);CHECK(!local.state().motion_available);
    for(uint32_t i=0;i<gl_menu_choice_count(local.c,"context.1.gyro.space");++i){gl_choice choice{};gl_choice_at(local.c,"context.1.gyro.space",i,&choice);
        if(int(choice.value)==GL_SPACE_PLAYER||int(choice.value)==GL_SPACE_WORLD)CHECK(!choice.available);
        if(int(choice.value)==GL_SPACE_LOCAL_YAW)CHECK(choice.available);}
    local.set("gyro.space",GL_SPACE_LOCAL_YAW);local.tick({0,90,0});near(local.tick({0,90,0}).yaw_degrees,-.9);
    CHECK(gl_begin_calibration(local.c)==GL_UNAVAILABLE);
    Fixture missing;missing.now+=10000000;missing.sample(missing.now,{0,90,0},false);near(missing.update().yaw_degrees,-.9);
}
static void activation_and_triggers(){
    Fixture f;f.set("gyro.activation",GL_TOGGLE);f.set("activation.stick_deflection",GL_SIDE_RIGHT);f.set("activation.stick_threshold",.5);f.tick();
    f.controls.right_x=.51f;f.tick();CHECK(!f.state().enabled);
    for(float value:{.49f,.51f,.48f,.52f,.46f}){f.controls.right_x=value;f.tick();CHECK(!f.state().enabled);}
    f.controls.right_x=.44f;f.tick();f.controls.right_x=.51f;f.tick();CHECK(f.state().enabled);
    Fixture t;t.set("gyro.activation",GL_HOLD);t.set("activation.trigger",GL_SIDE_LEFT);
    auto trigger=[&](float value){t.now+=10000000;t.sample(t.now,{0,100,0});gl_trigger_input p{t.now,GL_LEFT|GL_RIGHT,value,0};
        CHECK(gl_submit_trigger_input(t.c,1,&p)==GL_OK);return t.update();};
    near(trigger(.21f).yaw_degrees,-1);CHECK(t.state().enabled);
    near(trigger(.18f).yaw_degrees,-1);near(trigger(.14f).yaw_degrees,0);
    gl_set_trigger_label(t.c,1,GL_LEFT,"L2");CHECK(!std::strcmp(gl_get_trigger_label(t.c,GL_LEFT),"L2"));
    bool label=false;for(uint32_t i=0;i<5;++i){gl_choice c{};gl_choice_at(t.c,"context.1.activation.trigger",i,&c);if(int(c.value)==GL_SIDE_LEFT)label=!std::strcmp(c.label,"L2");}CHECK(label);
    gl_trigger_input bad{t.now,GL_LEFT,std::numeric_limits<float>::quiet_NaN(),0};CHECK(gl_submit_trigger_input(t.c,1,&bad)==GL_INVALID);
    t.tick({},false,160000000);gl_trigger_input expired{};CHECK(gl_get_trigger_input(t.c,&expired)==GL_OK&&!expired.available);
    // A proven physical source declaring no triggers prevents virtual triggers
    // from silently replacing them, including when its reports expire.
    gl_endpoint physical{};physical.id=2;physical.physical_id=1;physical.connected=1;physical.source=GL_SOURCE_SDL;
    CHECK(gl_register_endpoint(t.c,&physical)==GL_OK);CHECK(gl_set_endpoint_control_authority(t.c,2,GL_CONTROL_TRIGGERS)==GL_OK);
    near(trigger(1).yaw_degrees,0);CHECK(gl_get_trigger_input(t.c,&expired)==GL_OK&&!expired.available);
}
static void modifiers_and_safety(){
    Fixture f;CHECK(gl_set_gyro_modifiers(f.c,GL_MOD_INVERT_X)==GL_OK);near(f.tick({10,100,0}).yaw_degrees,1);
    near(f.tick({10,100,0}).yaw_degrees,-1);f.set("gyro.invert_x",1);
    gl_set_gyro_modifiers(f.c,GL_MOD_INVERT_X);near(f.tick({0,100,0}).yaw_degrees,-1);
    f.set("gyro.invert_x",0);f.set("gyro.activation",GL_HOLD_DISABLE);f.set("activation.button",3);
    f.set("activation.temporary_invert",1);f.set("gyro.temporary_invert_axes",0);f.controls.buttons=4;
    const auto inverted=f.tick({10,100,0});near(inverted.yaw_degrees,1);near(inverted.pitch_degrees,.1);
    f.controls.buttons=0;near(f.tick({0,100,0}).yaw_degrees,-1);
    f.set("gyro.activation",GL_HOLD);f.set("activation.touchpad",GL_SIDE_RIGHT);
    f.controls.touchpads=GL_RIGHT;f.controls.right_x=0;near(f.tick({0,100,0}).yaw_degrees,-1);
    f.controls.right_x=1;near(f.tick({0,100,0}).yaw_degrees,-1);f.controls.right_x=0;near(f.tick({0,100,0}).yaw_degrees,-1);
    CHECK(gl_setting_set(f.c,"context.1.gyro.look_stick_effect",1)==GL_UNAVAILABLE);
    f.controls.touchpads=0;f.controls.right_x=1;near(f.tick({0,100,0}).yaw_degrees,0);
    f.controls.right_x=0;near(f.tick({0,100,0}).yaw_degrees,0);
    gl_set_gyro_override(f.c,1);near(f.tick({0,100,0}).yaw_degrees,-1);near(f.tick({0,100,0}).yaw_degrees,0);
    f.set("gyro.enabled",0);gl_set_gyro_override(f.c,1);near(f.tick({0,100,0}).yaw_degrees,0);
    f.set("gyro.enabled",1);f.host.focused=0;gl_set_gyro_override(f.c,1);near(f.tick({0,100,0}).yaw_degrees,0);
    CHECK(gl_set_gyro_modifiers(f.c,16)==GL_INVALID&&gl_set_gyro_override(f.c,2)==GL_INVALID);
}
static void conditional_menu(){
    Fixture f;const auto info=[&](const char* suffix){const auto id=std::string("context.1.")+suffix;
        for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,2);++i){gl_setting_info value{};gl_menu_tab_setting_at(f.c,2,i,&value);if(id==value.id)return value;}
        throw std::runtime_error("Missing menu field");};
    CHECK(!info("gyro.look_stick_effect").visible);
    CHECK(!info("gyro.smoothing_threshold_dps").visible&&!info("gyro.tightening_dps").visible);
    f.set("gyro.smoothing_ms",25);CHECK(info("gyro.smoothing_threshold_dps").visible&&info("gyro.tightening_dps").visible);
    f.set("gyro.acceleration",0);CHECK(!info("gyro.fast_sensitivity_x").visible);
    f.set("gyro.acceleration",1);CHECK(info("gyro.fast_sensitivity_x").visible);
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
    gl_flick_input input{f.now,GL_FLICK_INPUT_STICK|GL_FLICK_INPUT_TOUCHPAD,0,0,0,0,0};
    CHECK(gl_submit_flick_input(f.c,1,&input)==GL_OK);
    for(int mode:{GL_FLICK_OFF,GL_FLICK_ON,GL_FLICK_TOUCHPAD,GL_FLICK_BOTH}){
        f.set("flick.mode",mode);CHECK(bool(info("flick.duration_ms").visible)==(mode!=GL_FLICK_OFF));
        const bool stick=mode==GL_FLICK_ON||mode==GL_FLICK_BOTH,pad=mode==GL_FLICK_TOUCHPAD||mode==GL_FLICK_BOTH;
        CHECK(bool(info("flick.stick_start_threshold").visible)==stick&&bool(info("flick.stick_release_threshold").visible)==stick);
        CHECK(bool(info("flick.touchpad_start_threshold").visible)==pad&&bool(info("flick.touchpad_release_threshold").visible)==pad);
    }
    std::set<std::string> ids;bool button=false,trigger=false;
    for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,2);++i){gl_setting_info value{};CHECK(gl_menu_tab_setting_at(f.c,2,i,&value)==GL_OK);CHECK(ids.insert(value.id).second);
        if(button){CHECK(!std::strcmp(value.id,"context.1.activation.trigger"));trigger=true;button=false;}
        if(!std::strcmp(value.id,"context.1.activation.button"))button=true;
    }CHECK(trigger);
}
static void gyro_off(){
    Fixture f;
    f.set("gyro.smoothing_ms",25);f.set("gyro.acceleration",1);f.set("activation.button",3);
    f.set("activation.touchpad",GL_SIDE_RIGHT);f.set("gyro.activation",GL_GYRO_OFF);
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_LONG_PRESS_BLOCKING);
    f.set("flick.mode",GL_FLICK_ON);f.set("flick.duration_ms",0);
    gl_set_gyro_override(f.c,1);near(f.tick({0,100,0}).yaw_degrees,0);CHECK(!f.state().enabled);
    CHECK(gl_filter_event(f.c,7,GL_PRESS,f.now,1,0)==GL_FORWARD);
    gl_flick_input centered{f.now,GL_FLICK_INPUT_STICK,0,0,0,0,0};gl_submit_flick_input(f.c,1,&centered);f.tick();
    gl_flick_input tilted{f.now+10000000,GL_FLICK_INPUT_STICK,0,1,0,0,0};gl_submit_flick_input(f.c,1,&tilted);
    CHECK(std::abs(f.tick().yaw_degrees)>80); // Flick remains independent of gyro Off.
    std::vector<std::string> visible;
    for(uint32_t n=0;n<gl_menu_tab_setting_count(f.c,2);++n){gl_setting_info info{};gl_menu_tab_setting_at(f.c,2,n,&info);
        if(info.visible)visible.emplace_back(info.id);
        if(std::strncmp(info.id,"context.1.gyro.",15)==0&&std::strcmp(info.id,"context.1.gyro.activation"))CHECK(!info.visible);
        if(gl_setting_is_activator(info.id))CHECK(!info.visible);
    }
    CHECK(!visible.empty()&&visible.front()=="context.1.gyro.activation");
    const auto path=std::filesystem::temp_directory_path()/"gyrolib-off-persistence.ini";
    CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);
    {std::ifstream saved(path);const std::string data((std::istreambuf_iterator<char>(saved)),{});
        CHECK(data.find("gyro.enabled")==std::string::npos&&data.find("gyro.activation=6")!=std::string::npos);}
    Fixture restored;CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_OK);
    double mode=0;gl_setting_get(restored.c,"context.1.gyro.activation",&mode);near(mode,GL_GYRO_OFF);
    restored.set("gyro.activation",GL_ALWAYS);
    double sensitivity=0;gl_setting_get(restored.c,"context.1.sensitivity_x",&sensitivity);near(sensitivity,1);
    double smoothing=0;gl_setting_get(restored.c,"context.1.gyro.smoothing_ms",&smoothing);near(smoothing,25);
    for(uint32_t n=0;n<gl_menu_tab_setting_count(restored.c,2);++n){gl_setting_info info{};gl_menu_tab_setting_at(restored.c,2,n,&info);
        if(!std::strcmp(info.id,"context.1.gyro.space"))CHECK(info.visible&&n==1);}
    CHECK(gl_save_settings(restored.c,path.string().c_str())==GL_OK);
    CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);gl_setting_get(f.c,"context.1.gyro.activation",&mode);near(mode,GL_ALWAYS);
    {std::ofstream old(path);old<<"schema=0.2.0\ncontext.1.gyro.activation=6\ncontext.1.sensitivity_x=6\n";}
    CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_OK);gl_setting_get(restored.c,"context.1.gyro.activation",&mode);near(mode,GL_GYRO_OFF);
    restored.set("gyro.activation",GL_HOLD);gl_setting_get(restored.c,"context.1.sensitivity_x",&sensitivity);near(sensitivity,6);
    gl_setting_get(restored.c,"context.1.gyro.enabled",&mode);near(mode,1);
    std::filesystem::remove(path);
    CHECK(gl_setting_is_activator("context.1.activation.trigger")&&gl_setting_is_activator("activation.block_long_press"));
    CHECK(!gl_setting_is_activator("gyro.activation")&&!gl_setting_is_activator("activation.trackball"));
}
static void trackball(){
    for(double decay:{0.,1.}){
        Fixture f;f.set("gyro.activation",GL_HOLD_DISABLE);f.set("activation.button",3);
        f.set("activation.trackball",1);f.set("gyro.trackball_axes",0);f.set("gyro.trackball_decay",decay);
        for(int i=0;i<100;++i)f.tick({0,100,0});f.controls.buttons=4;
        double total=0;for(int i=0;i<100;++i){total+=f.tick().yaw_degrees;CHECK(f.state().trackball_active&&f.state().enabled);}
        near(total,decay?-50/std::log(2.0):-100);
        const auto idle=f.tick({},false);CHECK(f.state().trackball_active&&!f.state().new_samples);CHECK(idle.yaw_degrees<0);
        f.host.focused=0;near(f.tick().yaw_degrees,0);CHECK(!f.state().trackball_active);
        f.host.focused=1;near(f.tick().yaw_degrees,0); // no old momentum after a focus transition
        f.controls.buttons=0;near(f.tick({0,100,0}).yaw_degrees,-1);
    }
}
static void shared_hold_activators(){
    for(int family=0;family<6;++family){
        Fixture f;f.set("gyro.activation",GL_HOLD_DISABLE);f.set("activation.temporary_invert",1);f.set("gyro.temporary_invert_axes",0);
        const auto trigger=[&](float value){gl_trigger_input input{f.now+10000000,GL_LEFT,value,0};CHECK(gl_submit_trigger_input(f.c,1,&input)==GL_OK);};
        switch(family){
            case 0:f.set("activation.button",3);f.controls.buttons=4;break;
            case 1:f.set("activation.touchpad",GL_SIDE_LEFT);f.controls.touchpads=GL_LEFT;break;
            case 2:f.set("activation.stick_touch",GL_SIDE_RIGHT);f.controls.stick_touch=GL_RIGHT;break;
            case 3:f.set("activation.grip_touch",GL_SIDE_BOTH);f.controls.grip_touch=GL_LEFT|GL_RIGHT;break;
            case 4:f.set("activation.stick_deflection",GL_SIDE_LEFT);f.controls.left_x=1;break;
            case 5:f.set("activation.trigger",GL_SIDE_LEFT);trigger(1);break;
        }
        near(f.tick({10,100,0}).yaw_degrees,1);CHECK(f.state().enabled);
        // The very same hold cuts gyro if both additional behaviors are off.
        f.set("activation.temporary_invert",0);near(f.tick({0,100,0}).yaw_degrees,0);CHECK(!f.state().enabled);
        f.set("activation.temporary_invert",1);f.controls={};if(family==5)trigger(0);
        near(f.tick({0,100,0}).yaw_degrees,-1);
        // Retained options have no effect in other activation modes.
        f.set("gyro.activation",GL_ALWAYS);f.controls.buttons=4;near(f.tick({0,100,0}).yaw_degrees,-1);
        f.set("gyro.activation",GL_HOLD);f.set("activation.button",3);near(f.tick({0,100,0}).yaw_degrees,-1);
        for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,2);++i){gl_setting_info info{};gl_menu_tab_setting_at(f.c,2,i,&info);
            if(gl_setting_advanced_group(info.id)==GL_ADVANCED_HOLD_DISABLE)CHECK(!info.visible);
            if(!std::strcmp(info.id,"context.1.gyro.temporary_invert_button")||!std::strcmp(info.id,"context.1.gyro.trackball_button"))CHECK(!info.visible&&!info.available);
        }
        CHECK(gl_setting_set(f.c,"context.1.gyro.temporary_invert_button",3)==GL_UNAVAILABLE);
    }
    // Trackball also shares touch contacts, rather than needing a button.
    Fixture f;f.set("gyro.activation",GL_HOLD_DISABLE);f.set("activation.touchpad",GL_SIDE_RIGHT);
    f.set("activation.trackball",1);f.set("gyro.trackball_axes",0);f.set("gyro.trackball_decay",0);
    for(int i=0;i<100;++i)f.tick({0,100,0});f.controls.touchpads=GL_RIGHT;
    near(f.tick().yaw_degrees,-1);CHECK(f.state().trackball_active);
    f.set("activation.temporary_invert",1);f.set("gyro.temporary_invert_axes",0);near(f.tick().yaw_degrees,1);
    f.host.paused=1;near(f.tick().yaw_degrees,0);CHECK(!f.state().trackball_active);
    f.host.paused=0;near(f.tick().yaw_degrees,0);f.controls.touchpads=0;near(f.tick({0,100,0}).yaw_degrees,-1);
    // Existing tap filtering does not delay inversion, and still distinguishes
    // short game actions from holds on the same activation button.
    Fixture tap;gl_set_host_capabilities(tap.c,GL_HOST_LONG_PRESS_BLOCKING);
    tap.set("gyro.activation",GL_HOLD_DISABLE);tap.set("activation.button",3);tap.set("activation.block_long_press",1);
    tap.set("activation.temporary_invert",1);tap.set("gyro.temporary_invert_axes",0);
    CHECK(gl_filter_event(tap.c,7,GL_PRESS,tap.now+10000000,1,0)==GL_SUPPRESS);
    tap.controls.buttons=4;near(tap.tick({0,100,0}).yaw_degrees,1);
    tap.now+=90000000;tap.controls.buttons=0;tap.tick();
    CHECK(gl_filter_event(tap.c,7,GL_RELEASE,tap.now,1,0)==GL_EMIT_TAP);
    CHECK(gl_filter_event(tap.c,7,GL_PRESS,tap.now+10000000,1,0)==GL_SUPPRESS);
    tap.controls.buttons=4;near(tap.tick({0,100,0}).yaw_degrees,1);
    for(int n=0;n<25;++n)tap.tick({0,100,0});tap.controls.buttons=0;tap.tick();
    CHECK(gl_filter_event(tap.c,7,GL_RELEASE,tap.now,1,0)==GL_SUPPRESS);
}
static double circle(int fps,bool pad,double* checkpoints){
    Fixture f;gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
    f.set("flick.mode",pad?GL_FLICK_TOUCHPAD:GL_FLICK_ON);f.set("flick.style",GL_FLICK_STYLE_ROTATE_ONLY);
    f.set("flick.smoothing_ms",100);f.set("flick.smoothing_threshold_dps",360);
    uint64_t sampled=f.now;
    auto submit=[&](uint64_t time,double angle,bool touching){gl_flick_input input{time,GL_FLICK_INPUT_STICK|GL_FLICK_INPUT_TOUCHPAD,uint32_t(touching),
        float(std::sin(angle)),float(std::cos(angle)),float(std::sin(angle)),float(std::cos(angle))};
        if(!touching)input.stick_x=input.stick_y=0;CHECK(gl_submit_flick_input(f.c,1,&input)==GL_OK);
        if(time>sampled){f.sample(time);sampled=time;}};
    f.now+=10000000;submit(f.now,0,false);f.update();f.now+=10000000;submit(f.now,0,true);f.update();
    const uint64_t origin=f.now;int report=1;double total=0;
    for(int frame=1;frame<=fps*4;++frame){f.now=origin+uint64_t(std::llround(double(frame)*1e9/fps));
        while(report<=400&&origin+uint64_t(report)*10000000<=f.now){submit(origin+uint64_t(report)*10000000,report*2*std::numbers::pi/400,true);++report;}
        total+=f.update().yaw_degrees;
        if(frame%fps==0)checkpoints[frame/fps-1]=total;
    }
    f.now+=10000000;submit(f.now,0,false);total+=f.update().yaw_degrees;return total;
}
static void flick_cadence(){
    for(bool pad:{false,true}){double reference[4]{};
        for(int fps:{30,60,120,144,240}){double positions[4]{};const auto value=circle(fps,pad,positions);
            std::cout<<"Flick 100 Hz, "<<fps<<" FPS, pad="<<pad<<": "<<value<<" degrees\n";near(value,360,.002);
            for(int i=0;i<4;++i){if(fps==30)reference[i]=positions[i];else near(positions[i],reference[i],.002);}
        }
    }
    Fixture f;f.set("flick.stick_release_threshold",.95);double start=0;
    gl_setting_get(f.c,"context.1.flick.stick_start_threshold",&start);near(start,1);
    f.set("flick.touchpad_start_threshold",.05);double release=1;
    gl_setting_get(f.c,"context.1.flick.touchpad_release_threshold",&release);near(release,0);
}
static void persistence_and_menu(){
    Fixture f;const auto path=std::filesystem::temp_directory_path()/"gyrolib-audit-features.ini";
    {std::ofstream file(path);file<<"schema=0.2.0\nui.menu_key=F8\ncontext.1.sensitivity_x=6\ncontext.1.flick.smoothing_threshold_dps=60\nhost.keep=value\n";}
    CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);double speed=0;gl_setting_get(f.c,"context.1.flick.smoothing_threshold_dps",&speed);near(speed,60);
    f.set("gyro.activation",GL_HOLD_DISABLE);f.set("activation.button",3);
    f.set("activation.temporary_invert",1);f.set("activation.trackball",1);f.set("gyro.trackball_decay",2.5);
    Fixture restored;CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_OK);double decay=0;
    gl_setting_get(restored.c,"context.1.gyro.trackball_decay",&decay);near(decay,2.5);CHECK(gl_get_menu_key(restored.c)==8);
    for(const char* lang:{"en","fr"}){gl_set_language(f.c,lang);
        for(uint32_t n=0;n<gl_menu_tab_setting_count(f.c,2);++n){gl_setting_info info{};gl_menu_tab_setting_at(f.c,2,n,&info);
            CHECK(std::strcmp(info.label,"?")&&std::strcmp(info.description,"?"));
            if((gl_setting_advanced_group(info.id)==GL_ADVANCED_MODIFIERS||gl_setting_advanced_group(info.id)==GL_ADVANCED_HOLD_DISABLE)&&info.type==GL_SETTING_ENUM)
                for(uint32_t j=0;j<gl_menu_choice_count(f.c,info.id);++j){gl_choice option{};gl_choice_at(f.c,info.id,j,&option);CHECK(*gl_choice_description(f.c,info.id,option.value));}
        }
    }
    std::ifstream file(path);const std::string text((std::istreambuf_iterator<char>(file)),{});CHECK(text.find("schema=0.2.0")!=std::string::npos&&text.find("host.keep=value")!=std::string::npos);
    CHECK(text.find("gyro.temporary_invert_button")==std::string::npos&&text.find("gyro.trackball_button")==std::string::npos);
    file.close();
    {std::ofstream old(path);old<<"schema=0.2.0\ncontext.1.gyro.activation=5\ncontext.1.activation.touchpad=2\ncontext.1.activation.temporary_invert=1\ncontext.1.activation.trackball=1\ncontext.1.gyro.trackball_decay=2.5\n";}
    CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_OK);
    for(const char* key:{"context.1.activation.temporary_invert","context.1.activation.trackball"}){double value=0;CHECK(gl_setting_get(restored.c,key,&value)==GL_OK);near(value,1);}
    double pad=0;gl_setting_get(restored.c,"context.1.activation.touchpad",&pad);near(pad,GL_SIDE_RIGHT);
    restored.controls.touchpads=GL_RIGHT;CHECK(restored.tick().yaw_degrees==0&&restored.state().trackball_active);
    CHECK(gl_save_settings(restored.c,path.string().c_str())==GL_OK);std::filesystem::remove(path);
    // Removed development fields are rejected, not silently converted.
    {std::ofstream invalid(path);invalid<<"schema=0.2.0\ncontext.1.gyro.look_stick_effect=1\n";}
    CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_INVALID);
    std::filesystem::remove(path);
}
static void all_menu_options(){
    Fixture f;
    f.c->steam_mouse_device=f.c->selected; // synthetic detection for the complete menu audit
    gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION|
        GL_HOST_LONG_PRESS_BLOCKING|GL_HOST_MENU_STATE);
    gl_set_recenter_step_callback(f.c,[](void*,double){},nullptr);
    f.set("camera.recenter_button",9);
    CHECK(gl_set_gameplay_context_zoom_available(f.c,1,1)==GL_OK);
    const gl_trigger_input triggers{f.now,GL_LEFT|GL_RIGHT,0,0};
    const gl_flick_input flick{f.now,GL_FLICK_INPUT_STICK|GL_FLICK_INPUT_TOUCHPAD,0,0,0,0,0};
    CHECK(gl_submit_trigger_input(f.c,1,&triggers)==GL_OK);
    CHECK(gl_submit_flick_input(f.c,1,&flick)==GL_OK);
    f.set("activation.button",3);f.set("activation.trigger",GL_SIDE_BOTH);
    f.set("activation.stick_deflection",GL_SIDE_BOTH);f.set("activation.temporary_invert",1);
    f.set("activation.trackball",1);f.set("gyro.smoothing_ms",25);
    f.set("gyro.acceleration",1);f.set("flick.snap",2);
    // Union of every supported mode: a current field must be reachable somewhere,
    // including inline controls and children of advanced groups.
    std::map<std::string,gl_setting_info> exposed;
    const auto collect=[&](uint64_t tab){
        for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,tab);++i){gl_setting_info s{};
            CHECK(gl_menu_tab_setting_at(f.c,tab,i,&s)==GL_OK);
            if(s.visible&&s.available&&s.type!=GL_SETTING_ACTION)exposed[s.id]=s;
        }
    };
    for(int activation:{GL_GYRO_OFF,GL_ALWAYS,GL_HOLD_DISABLE,GL_HOLD,GL_TOGGLE})
        for(int space=0;space<9;++space)
            for(int mode:{GL_FLICK_OFF,GL_FLICK_ON,GL_FLICK_TOUCHPAD,GL_FLICK_BOTH})
                for(int style=0;style<3;++style){
                    f.set("gyro.activation",activation);f.set("gyro.space",space);
                    f.set("flick.mode",mode);f.set("flick.style",style);collect(2);
                }
    collect(GL_TAB_GENERAL);
    const std::set<std::string> retired={"gyro.enabled","gyro.context","flick.context","ui.scale",
        "flick.smoothing_threshold_degrees","gyro.temporary_invert_button","gyro.trackball_button","gyro.look_stick_effect"};
    std::set<std::string> expected;
    for(uint32_t i=0;i<gl_setting_count();++i){gl_setting_info s{};CHECK(gl_setting_at(f.c,i,&s)==GL_OK);
        if(s.type==GL_SETTING_ACTION||retired.count(s.id))continue;
        const std::string suffix=!std::strcmp(s.id,"gyro.sensitivity_x")?"sensitivity_x":
            !std::strcmp(s.id,"gyro.sensitivity_y")?"sensitivity_y":s.id;
        expected.insert(suffix=="calibration.automatic"?suffix:"context.1."+suffix);
    }
    for(const auto& id:expected)if(!exposed.count(id))throw std::runtime_error("Unreachable menu option: "+id);
    CHECK(exposed.size()==expected.size());
    // All languages must supply both a label and help for each reachable option
    // and each selectable choice. Derived/retired choices stay nonselectable.
    for(const char* language:{"en","fr","de","es","it","pt"}){
        CHECK(gl_set_language(f.c,language)==GL_OK);
        for(uint64_t tab:{uint64_t(GL_TAB_GENERAL),uint64_t(2)})
            for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,tab);++i){gl_setting_info s{};
                CHECK(gl_menu_tab_setting_at(f.c,tab,i,&s)==GL_OK);if(!exposed.count(s.id))continue;
                CHECK(s.label&&*s.label&&std::strcmp(s.label,"?")&&s.description&&*s.description&&std::strcmp(s.description,"?"));
                for(uint32_t j=0;j<gl_menu_choice_count(f.c,s.id);++j){gl_choice choice{};
                    CHECK(gl_choice_at(f.c,s.id,j,&choice)==GL_OK);if(!choice.available)continue;
                    CHECK(choice.label&&*choice.label&&std::strcmp(choice.label,"?"));
                    CHECK(*gl_choice_description(f.c,s.id,choice.value));
                }
            }
    }
    CHECK(gl_set_language(f.c,"en")==GL_OK);
    const gl_gameplay_context other{2,"Other camera","",0};CHECK(gl_register_gameplay_context(f.c,&other)==GL_OK);
    std::map<std::string,double> untouched;
    for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,3);++i){gl_setting_info s{};
        CHECK(gl_menu_tab_setting_at(f.c,3,i,&s)==GL_OK);double v{};
        CHECK(gl_setting_get(f.c,s.id,&v)==GL_OK);untouched[s.id]=v;
    }
    const auto path=std::filesystem::current_path()/"all-menu-options.ini";
    CHECK(gl_set_settings_path(f.c,path.string().c_str())==GL_OK);
    CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);
    size_t edits=0;
    for(const auto& [id,s]:exposed){
        std::set<double> values;
        if(s.type==GL_SETTING_ENUM){
            for(uint32_t j=0;j<gl_menu_choice_count(f.c,id.c_str());++j){gl_choice choice{};
                CHECK(gl_choice_at(f.c,id.c_str(),j,&choice)==GL_OK);
                if(choice.available)CHECK(values.insert(choice.value).second);
            }
        }else{values.insert(s.minimum);values.insert(s.maximum);
            values.insert(s.minimum+std::floor((s.maximum-s.minimum)/s.step/2)*s.step);}
        CHECK(!values.empty());
        for(double value:values){
            CHECK(gl_setting_set(f.c,id.c_str(),value)==GL_OK);double applied{};
            CHECK(gl_setting_get(f.c,id.c_str(),&applied)==GL_OK);near(applied,value);
            Fixture restored;CHECK(gl_register_gameplay_context(restored.c,&other)==GL_OK);
            CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_OK);
            // Verify the complete saved state after each edit, including coupled
            // acceleration/threshold changes, and isolation of the other view.
            for(const auto& [key,meta]:exposed){double before{},after{};
                CHECK(gl_setting_get(f.c,key.c_str(),&before)==GL_OK);
                CHECK(gl_setting_get(restored.c,key.c_str(),&after)==GL_OK);near(after,before);
            }
            for(const auto& [key,before]:untouched){double after{};
                CHECK(gl_setting_get(restored.c,key.c_str(),&after)==GL_OK);near(after,before);
            }
            ++edits;
        }
    }
    CHECK(gl_action(f.c,"calibration.begin")==GL_OK);
    bool cancel=false;
    for(uint32_t i=0;i<gl_menu_shared_setting_count(f.c);++i){gl_setting_info s{};
        CHECK(gl_menu_shared_setting_at(f.c,i,&s)==GL_OK);
        if(!std::strcmp(s.id,"calibration.cancel"))cancel=s.visible&&s.available;
    }CHECK(cancel);CHECK(gl_action(f.c,"calibration.cancel")==GL_OK);
    CHECK(gl_action(f.c,"settings.reset")==GL_OK);
    Fixture reset;CHECK(gl_load_settings(reset.c,path.string().c_str())==GL_OK);
    for(const auto& [id,s]:exposed){double value{};CHECK(gl_setting_get(reset.c,id.c_str(),&value)==GL_OK);near(value,s.default_value);}
    CHECK(gl_set_settings_path(f.c,"")==GL_OK);std::filesystem::remove(path);
    std::cout<<"Menu audit: "<<exposed.size()<<" current settings reachable, "<<edits
        <<" edits round-tripped with view isolation, six languages and shared actions checked\n";
}
int main(){try{stable_activity_and_local_only();activation_and_triggers();modifiers_and_safety();conditional_menu();gyro_off();trackball();shared_hold_activators();flick_cadence();persistence_and_menu();all_menu_options();
    std::cout<<"Gyro audit: activity, gyro-only sources, analog activation, modifiers, trackball, timed flick and persistence passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
