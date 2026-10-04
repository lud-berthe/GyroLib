#include <gyrolib/gyrolib.hpp>
#include "../examples/tps/host.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(0)
struct Fixture {
    gyrolib::Context owner;gl_context* c=owner.get();
    uint64_t now=1000000000;gl_endpoint e{};gl_controls controls{};
    gl_flick_input flick{};gl_trigger_input triggers{};gl_host_state host{};gl_output out{};
    Fixture(){
        for(uint32_t id:{1u,2u}){gl_gameplay_context view{id,"Camera","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);}
        CHECK(gl_set_context_parent(c,2,1)==GL_OK);CHECK(gl_set_gameplay_context_state(c,2,1,1)==GL_OK);
        CHECK(gl_set_host_capabilities(c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION|GL_HOST_MENU_STATE)==GL_OK);
        e.id=e.physical_id=1;e.connected=1;e.source=GL_SOURCE_SDL;std::strcpy(e.name,"Same unknown controller");
        e.caps={0xffffffffu,3,3,3,3,1,1};CHECK(gl_register_endpoint(c,&e)==GL_OK);
        flick.available=3;triggers.available=3;host.focused=host.camera_allowed=1;
        set("gyro.space",GL_SPACE_LOCAL_YAW);set("sensitivity_x",1);
        for(int n=0;n<4;++n)tick();
    }
    void set(const char* suffix,double value){CHECK(gl_setting_set(c,(std::string("context.1.")+suffix).c_str(),value)==GL_OK);}
    double get(const char* suffix,bool effective=true,uint32_t view=2){double value{};
        const auto key="context."+std::to_string(view)+"."+suffix;
        CHECK((effective?gl_setting_get_effective:gl_setting_get)(c,key.c_str(),&value)==GL_OK);return value;}
    gl_setting_info info(const char* suffix){const auto key=std::string("context.2.")+suffix;
        for(uint32_t i=0;i<gl_menu_tab_setting_count(c,3);++i){gl_setting_info s{};gl_menu_tab_setting_at(c,3,i,&s);if(key==s.id)return s;}
        throw std::runtime_error("Missing menu field");}
    void tick(){
        now+=10000000;controls.timestamp_ns=flick.timestamp_ns=triggers.timestamp_ns=now;
        CHECK(gl_submit_controls(c,1,&controls)==GL_OK);CHECK(gl_submit_flick_input(c,1,&flick)==GL_OK);
        CHECK(gl_submit_trigger_input(c,1,&triggers)==GL_OK);
        gl_sample sample{now,now,{0,20,0},{0,1,0}};CHECK(gl_submit_sample(c,1,&sample)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,2,1,1)==GL_OK);
        CHECK(gl_update(c,now,&host,&out)==GL_OK);
    }
    void layout(uint32_t sides,uint32_t inputs){
        e.caps.touchpads=e.caps.stick_touch=e.caps.grip_touch=e.caps.sticks=sides;
        if(sides==GL_SINGLE)e.caps.sticks=0;
        CHECK(gl_register_endpoint(c,&e)==GL_OK);flick.available=inputs;
        triggers.available=sides&3;controls={};triggers.left=triggers.right=0;tick();
    }
};
static void combined_activators(){
    for(const char* family:{"activation.touchpad","activation.stick_touch","activation.grip_touch","activation.stick_deflection","activation.trigger"}){
        for(int choice:{GL_SIDE_EITHER,GL_SIDE_BOTH})for(uint32_t side:{uint32_t(GL_LEFT),uint32_t(GL_RIGHT)}){
            Fixture f;f.set("gyro.activation",GL_HOLD);f.set(family,choice);f.layout(side,1);
            CHECK(f.get(family)==side&&f.get(family,false)==choice&&f.info(family).value==side);
            f.controls.touchpads=f.controls.stick_touch=f.controls.grip_touch=side;
            f.controls.left_x=side==GL_LEFT?1.f:0;f.controls.right_x=side==GL_RIGHT?1.f:0;
            f.triggers.left=f.controls.left_x;f.triggers.right=f.controls.right_x;f.tick();
            if(!f.out.gyro_active)throw std::runtime_error(std::string(family)+" choice "+std::to_string(choice)+" side "+std::to_string(side));
            f.layout(3,3);CHECK(f.get(family)==choice&&!f.out.gyro_active);
            // Restored AND still requires two held controls; OR requires one.
            f.controls.touchpads=f.controls.stick_touch=f.controls.grip_touch=GL_LEFT;
            f.controls.left_x=f.triggers.left=1;f.tick();CHECK(bool(f.out.gyro_active)==(choice==GL_SIDE_EITHER));
            f.controls.touchpads=f.controls.stick_touch=f.controls.grip_touch=3;
            f.controls.right_x=f.triggers.right=1;f.tick();CHECK(f.out.gyro_active);
            f.layout(0,0);CHECK(f.get(family)==0&&!f.out.gyro_active);
            gl_setting_inheritance_info inherit{};const auto key=std::string("context.2.")+family;
            CHECK(gl_setting_inheritance(f.c,key.c_str(),&inherit)==GL_OK&&!inherit.overridden);
        }
    }
    Fixture f;f.set("gyro.activation",GL_HOLD);f.set("activation.touchpad",GL_SIDE_BOTH);f.layout(GL_SINGLE,1);
    CHECK(f.get("activation.touchpad")==double(GL_SIDE_EITHER));f.controls.touchpads=GL_SINGLE;f.tick();CHECK(f.out.gyro_active);
    gl_choice on{};CHECK(gl_choice_at(f.c,"context.2.activation.touchpad",GL_SIDE_EITHER,&on)==GL_OK);
    CHECK(on.available&&!std::strcmp(on.label,"On"));
    f.set("activation.touchpad",0);f.set("activation.stick_deflection",GL_SIDE_RIGHT);f.layout(GL_LEFT,0);
    f.controls.left_x=1;f.tick();CHECK(f.get("activation.stick_deflection")==0&&!f.out.gyro_active);
    // Never invent a replacement button or switch an explicit right binding to left.
    f.set("activation.button",3);f.e.caps.buttons=1;CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);f.tick();
    CHECK(f.get("activation.button")==0&&f.get("activation.button",false)==3);
}
static void flick_and_persistence(){
    Fixture f;f.set("gyro.activation",GL_GYRO_OFF);f.set("flick.mode",GL_FLICK_BOTH);f.set("flick.duration_ms",0);
    const auto path=std::filesystem::current_path()/"device-fallback.ini";
    const auto saved=[&]{std::ifstream file(path);return std::string(std::istreambuf_iterator<char>(file),{});};
    CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);const auto original=saved();
    for(uint32_t inputs:{1u,2u,3u,0u,3u}){
        f.flick.stick_x=f.flick.touchpad_x=0;f.flick.touching=0;f.layout(3,inputs);
        const int expected=inputs==1?GL_FLICK_ON:inputs==2?GL_FLICK_TOUCHPAD:inputs==3?GL_FLICK_BOTH:GL_FLICK_OFF;
        CHECK(f.get("flick.mode")==expected&&f.info("flick.mode").value==expected);
        CHECK(f.get("flick.mode",false)==double(GL_FLICK_BOTH));
        f.flick.stick_x=1;f.flick.touchpad_x=1;f.flick.touching=1;f.tick();
        CHECK(std::abs(f.out.yaw_degrees-(inputs==3?180:inputs?90:0))<.001);
        CHECK(bool(f.out.suppress_native_right_stick)==bool(inputs&1));
        CHECK(bool(gl_suppress_native_right_touchpad(f.c))==bool(inputs&2));
        CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);CHECK(saved()==original);
    }
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION)==GL_OK);CHECK(f.get("flick.mode")==double(GL_FLICK_ON));
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)==GL_OK);CHECK(f.get("flick.mode")==double(GL_FLICK_TOUCHPAD));
    CHECK(gl_set_gameplay_context_output_target(f.c,2,GL_OUTPUT_CURSOR)==double(GL_OK));CHECK(f.get("flick.mode")==double(GL_FLICK_OFF));
    CHECK(!f.info("flick.mode").visible);
    CHECK(gl_set_gameplay_context_output_target(f.c,2,GL_OUTPUT_CAMERA)==double(GL_OK));
    CHECK(gl_set_settings_path(f.c,"")==GL_OK); // keep the saved preference for the reload check
    f.set("flick.mode",GL_FLICK_ON);CHECK(f.get("flick.mode")==0); // no stick suppression hook
    CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);CHECK(f.get("flick.mode",false)==double(GL_FLICK_BOTH));
    // Disconnect and stale reports suspend, reconnect restores the same inherited preference.
    CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);CHECK(f.get("flick.mode")==0);
    CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);f.tick();CHECK(f.get("flick.mode")==double(GL_FLICK_TOUCHPAD));
    f.now+=200000000;CHECK(gl_set_gameplay_context_state(f.c,2,1,1)==GL_OK);
    CHECK(gl_update(f.c,f.now,&f.host,&f.out)==GL_OK);CHECK(f.get("flick.mode")==0);
    f.tick();CHECK(f.get("flick.mode")==double(GL_FLICK_TOUCHPAD));
    std::filesystem::remove(path);
}
static void toggle_recovery(){
    Fixture f;f.set("gyro.activation",GL_TOGGLE);f.set("activation.grip_touch",GL_SIDE_BOTH);f.tick();
    CHECK(f.out.gyro_active);f.controls.grip_touch=3;f.tick();CHECK(!f.out.gyro_active);
    f.e.caps.grip_touch=GL_LEFT;CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);f.controls.grip_touch=GL_LEFT;f.tick();
    CHECK(!f.out.gyro_active); // adapting a held binding is not a new toggle press
    f.controls.grip_touch=0;f.tick();f.controls.grip_touch=GL_LEFT;f.tick();CHECK(f.out.gyro_active);
}
static void controller_takeover(){
    Fixture f;f.set("flick.mode",GL_FLICK_BOTH);CHECK(f.get("flick.mode")==double(GL_FLICK_BOTH));
    auto other=f.e;other.id=other.physical_id=2;other.caps.touchpads=other.caps.grip_touch=other.caps.stick_touch=0;
    CHECK(gl_register_endpoint(f.c,&other)==GL_OK);
    const auto tick_other=[&]{
        f.now+=10000000;gl_controls controls{};controls.timestamp_ns=f.now;
        gl_flick_input flick{};flick.timestamp_ns=f.now;flick.available=GL_FLICK_INPUT_STICK;
        CHECK(gl_submit_controls(f.c,2,&controls)==GL_OK);CHECK(gl_submit_flick_input(f.c,2,&flick)==GL_OK);
        CHECK(gl_set_gameplay_context_state(f.c,2,1,1)==GL_OK);CHECK(gl_update(f.c,f.now,&f.host,&f.out)==GL_OK);
    };
    tick_other();CHECK(gl_get_selected_device(f.c)==1&&f.get("flick.mode")==double(GL_FLICK_BOTH));
    CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);tick_other();
    CHECK(gl_get_selected_device(f.c)==2&&f.get("flick.mode")==double(GL_FLICK_ON));
    CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);f.tick(); // another connected controller must not add capabilities
    CHECK(gl_get_selected_device(f.c)==2&&f.get("flick.mode")==double(GL_FLICK_ON));
    CHECK(gl_select_device(f.c,1)==GL_OK);f.tick();CHECK(f.get("flick.mode")==double(GL_FLICK_BOTH));
    CHECK(f.get("flick.mode",false)==double(GL_FLICK_BOTH)&&gl_get_context_parent(f.c,2)==1);
    CHECK(gl_set_output_target(f.c,GL_OUTPUT_CURSOR)==double(GL_OK));
    CHECK(f.get("flick.mode")==0&&!f.info("flick.mode").visible);
}
static void labels(){
    Fixture f;CHECK(gl_set_trigger_label(f.c,1,GL_LEFT,"L2")==GL_OK);CHECK(gl_set_trigger_label(f.c,1,GL_RIGHT,"R2")==GL_OK);
    const auto label=[&](const char* suffix,uint32_t choice){gl_choice value{};
        CHECK(gl_choice_at(f.c,(std::string("context.2.")+suffix).c_str(),choice,&value)==GL_OK);return std::string(value.label);};
    CHECK(label("flick.mode",GL_FLICK_BOTH)=="Stick or Touchpad");CHECK(label("flick.mode",GL_FLICK_ON)=="Stick");
    CHECK(label("activation.touchpad",GL_SIDE_EITHER)=="Left or Right");CHECK(label("activation.stick_touch",GL_SIDE_BOTH)=="Left and Right");
    CHECK(label("activation.trigger",GL_SIDE_EITHER)=="L2 or R2");CHECK(label("activation.trigger",GL_SIDE_BOTH)=="L2 and R2");
    CHECK(label("gyro.trackball_axes",2)=="Horizontal and Vertical");
    CHECK(gl_set_language(f.c,"fr")==GL_OK);
    CHECK(label("activation.trigger",GL_SIDE_EITHER)=="L2 ou R2");CHECK(label("activation.trigger",GL_SIDE_BOTH)=="L2 et R2");
    CHECK(label("activation.grip_touch",GL_SIDE_BOTH)=="Gauche et Droite");
}
static void menu_hardware_availability(){
    Fixture f;gl_set_recenter_callback(f.c,[](void*){},nullptr);
    CHECK(gl_capture_recommended_settings(f.c)==GL_OK);
    const auto shared=[&](const char* id){
        for(uint32_t i=0;i<gl_menu_shared_setting_count(f.c);++i){gl_setting_info s{};
            CHECK(gl_menu_shared_setting_at(f.c,i,&s)==GL_OK);if(!std::strcmp(id,s.id))return s;}
        throw std::runtime_error("Missing shared action");
    };
    const auto all_disabled=[&]{for(uint32_t i=0;i<gl_menu_setting_count(f.c);++i){gl_setting_info s{};
        CHECK(gl_setting_at(f.c,i,&s)==GL_OK);CHECK(!s.available);}};
    CHECK(f.info("gyro.activation").available&&shared("settings.recommended").available);
    f.set("gyro.activation",GL_HOLD_DISABLE);f.set("sensitivity_x",6);
    CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);all_disabled(); // even before the next update
    CHECK(f.get("gyro.activation",false)==double(GL_HOLD_DISABLE)&&f.get("sensitivity_x",false)==6);
    CHECK(gl_get_context_parent(f.c,2)==1);
    CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);f.tick();
    CHECK(f.info("gyro.activation").available&&f.info("sensitivity_x").available);
    // A connected gyro-less controller can still use flick and recenter.
    f.e.caps.gyro=f.e.caps.accelerometer=0;CHECK(gl_register_endpoint(f.c,&f.e)==GL_OK);f.tick();
    CHECK(f.info("gyro.activation").visible&&!f.info("gyro.activation").available);
    CHECK(f.info("flick.mode").available&&f.info("camera.recenter_button").available);
    CHECK(shared("settings.reset").available&&shared("settings.recommended").available);
    gl_endpoint other=f.e;other.id=other.physical_id=2;other.caps.gyro=1;
    CHECK(gl_register_endpoint(f.c,&other)==GL_OK);f.tick();CHECK(!f.info("gyro.activation").available);
    gl_endpoint sensor=other;sensor.id=sensor.physical_id=81;sensor.caps.accelerometer=1;
    CHECK(gl_register_endpoint(f.c,&sensor)==GL_OK);CHECK(gl_set_motion_companion(f.c,81)==GL_OK);
    CHECK(!f.info("gyro.activation").available); // unassociated sensors do not count
    CHECK(gl_bind_motion_sensor(f.c,1,81)==GL_OK);CHECK(f.info("gyro.activation").available);
    f.now+=2000000000;f.tick();CHECK(f.info("gyro.activation").available); // known sensor, no samples
    CHECK(gl_disconnect_endpoint(f.c,81)==GL_OK);CHECK(!f.info("gyro.activation").available);
    CHECK(gl_disconnect_endpoint(f.c,2)==GL_OK);CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);all_disabled();
    // Programmatic presets and INI editing remain independent of hardware.
    f.set("sensitivity_x",7);CHECK(f.get("sensitivity_x",false)==7);
    CHECK(gl_apply_recommended_settings(f.c)==GL_OK);all_disabled();
    gyrolib::Context empty;tps::Host demo;CHECK(demo.setup(empty.get()));
    for(uint32_t i=0;i<gl_menu_setting_count(empty.get());++i){gl_setting_info s{};
        CHECK(gl_setting_at(empty.get(),i,&s)==GL_OK);CHECK(!s.available);}
}
static void recommendations(){
    gyrolib::Context owner;auto* c=owner.get();tps::Host host;CHECK(host.setup(c));
    const auto get=[&](uint32_t id,const char* field){double value{};CHECK(gl_setting_get(c,("context."+std::to_string(id)+"."+field).c_str(),&value)==GL_OK);return value;};
    for(int pass=0;pass<2;++pass){
        CHECK(gl_get_context_parent(c,205)==101&&gl_get_context_parent(c,206)==205&&gl_get_context_parent(c,309)==101);
        for(uint32_t id:{101u,205u,206u,309u}){
            CHECK(get(id,"sensitivity_x")== (id==206?1:2.5));CHECK(get(id,"sensitivity_y")== (id==206?1:2.5));
            CHECK(get(id,"gyro.activation")==double(GL_HOLD_DISABLE)&&get(id,"activation.button")==3&&get(id,"activation.block_long_press")==1);
            CHECK(get(id,"gyro.space")==double(id==309?GL_SPACE_LASER_POINTER:GL_SPACE_PLAYER));
            CHECK(get(id,"flick.mode")==double(id==205||id==206?GL_FLICK_OFF:GL_FLICK_BOTH));
            CHECK(get(id,"gyro.smoothing_ms")==0&&get(id,"gyro.acceleration")==0);
            CHECK(get(id,"gyro.zoom_compensation")==double(id==206));
        }
        gl_reset_settings(c);CHECK(gl_apply_recommended_settings(c)==GL_OK);
    }
}
int main()try{
    combined_activators();flick_and_persistence();toggle_recovery();controller_takeover();labels();recommendations();menu_hardware_availability();
    std::cout<<"Capability fallback: runtime, menus, inheritance, persistence, reconnection, labels and demo recommendations passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
