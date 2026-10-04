#include <gyrolib/gyrolib.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(0)
struct Fixture {
    gl_context* c=gl_create(GL_ABI_VERSION);uint64_t now=1000000000;
    gl_host_state host{};
    Fixture(){CHECK(c);host.focused=host.camera_allowed=1;
        for(uint32_t id:{1u,2u}){gl_gameplay_context view{id,"Camera","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);}}
    ~Fixture(){gl_destroy(c);}
    void device(uint64_t id,uint32_t source,uint64_t physical){
        gl_endpoint e{};e.id=id;e.physical_id=physical;e.source=source;e.connected=1;
        e.caps.buttons=0xffffffffu;e.caps.sticks=GL_RIGHT;e.caps.stick_touch=GL_LEFT;
        CHECK(gl_register_endpoint(c,&e)==GL_OK);
    }
    void controls(uint64_t id){gl_controls v{};v.timestamp_ns=now;CHECK(gl_submit_controls(c,id,&v)==GL_OK);}
    void flick(uint64_t id,float x){gl_flick_input v{now,GL_FLICK_INPUT_STICK,0,x,0,0,0};CHECK(gl_submit_flick_input(c,id,&v)==GL_OK);}
    void tick(){CHECK(gl_set_gameplay_context_state(c,2,1,1)==GL_OK);gl_output out{};CHECK(gl_update(c,now,&host,&out)==GL_OK);now+=10000000;}
    double get(const char* key,bool effective=false){double value{};CHECK((effective?gl_setting_get_effective:gl_setting_get)(c,key,&value)==GL_OK);return value;}
};
static void contacts_preserve_preferences(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);f.device(2,GL_SOURCE_SDL,2);
    CHECK(gl_set_button_contact(f.c,2,21,GL_CONTACT_STICK,GL_LEFT)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.1.activation.button",22)==GL_OK);
    CHECK(gl_set_context_parent(f.c,2,1)==GL_OK);
    const auto path=std::filesystem::current_path()/"sdk-contact-preferences.ini";
    const auto read=[&]{std::ifstream in(path);return std::string(std::istreambuf_iterator<char>(in),{});};
    CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);const auto original=read();
    for(uint64_t selected:{1ull,2ull,1ull}){
        CHECK(gl_select_device(f.c,selected)==GL_OK);f.controls(1);f.controls(2);f.tick();
        CHECK(f.get("context.2.activation.button")==22);
        CHECK(f.get("context.2.activation.stick_touch")==0);
        CHECK(f.get("context.2.activation.button",true)==(selected==1?22:0));
        CHECK(f.get("context.2.activation.stick_touch",true)==(selected==2?GL_SIDE_LEFT:0));
        gl_setting_inheritance_info info{};CHECK(gl_setting_inheritance(f.c,"context.2.activation.button",&info)==GL_OK);
        CHECK(info.parent_context==1&&!info.overridden);
        CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);CHECK(read()==original);
    }
    std::filesystem::remove(path);
}
static void complete_event_ids(){
    static_assert(sizeof(gl_event)==72,"Original C ABI must not change");
    Fixture f;gl_gameplay_context view{UINT32_MAX,"Long ID","",0};CHECK(gl_register_gameplay_context(f.c,&view)==GL_OK);
    const char* key="context.4294967295.flick.touchpad_release_threshold";
    gl_event_ex ex{};while(gl_poll_event_ex(f.c,&ex)>0){}
    CHECK(gl_setting_set(f.c,key,.25)==GL_OK);bool found=false;
    while(gl_poll_event_ex(f.c,&ex)>0)if(ex.type==GL_EVENT_SETTING){
        CHECK(std::strlen(ex.setting_id)<sizeof(ex.setting_id));double value{};
        CHECK(gl_setting_get(f.c,ex.setting_id,&value)==GL_OK);
        if(!std::strcmp(ex.setting_id,key)){CHECK(value==.25);found=true;}
    }CHECK(found);
    CHECK(gl_setting_set(f.c,key,.30)==GL_OK);gl_event legacy{};bool invalidated=false;
    while(gl_poll_event(f.c,&legacy)>0){
        if(legacy.type==GL_EVENT_SETTING){double value{};CHECK(gl_setting_get(f.c,legacy.setting_id,&value)==GL_OK);}
        if(legacy.type==GL_EVENT_CONTEXT&&legacy.detail==6){CHECK(!legacy.setting_id[0]);invalidated=true;}
    }CHECK(invalidated);
    // Every profile setting fits, including future additions to the model.
    for(uint32_t tab=0;tab<gl_menu_tab_count(f.c);++tab){gl_menu_tab metadata{};CHECK(gl_menu_tab_at(f.c,tab,&metadata)==GL_OK);
        for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,metadata.id);++i){gl_setting_info info{};
            CHECK(gl_menu_tab_setting_at(f.c,metadata.id,i,&info)==GL_OK);CHECK(std::strlen(info.id)<sizeof(ex.setting_id));}}
    // Bounded FIFO: retain the latest 128 events and share the two poll APIs.
    for(uint32_t i=0;i<200;++i)CHECK(gl_set_gameplay_context_zoom_available(f.c,1,(i+1)%2)==GL_OK);
    unsigned count=0;while(gl_poll_event_ex(f.c,&ex)>0){CHECK(ex.type==GL_EVENT_CONTEXT&&ex.detail==4);++count;
        if(gl_poll_event(f.c,&legacy)>0){CHECK(legacy.type==ex.type&&legacy.detail==ex.detail);++count;}}
    CHECK(count==128);CHECK(gl_poll_event_ex(f.c,nullptr)==GL_INVALID);
}
static void stale_flick_fallback(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);f.device(2,GL_SOURCE_STEAM,1);f.device(3,GL_SOURCE_SDL,3);
    CHECK(gl_select_device(f.c,1)==GL_OK);f.controls(1);f.flick(1,.25f);f.tick();
    f.now+=200000000;f.controls(2);f.flick(2,.75f);f.flick(3,1);f.tick();
    gl_flick_input v{};CHECK(gl_get_flick_input(f.c,&v)==GL_OK);CHECK(v.available==GL_FLICK_INPUT_STICK&&v.stick_x==.75f);
    // Explicit physical authority blocks remapped virtual controls, even stale.
    CHECK(gl_set_endpoint_control_authority(f.c,1,GL_CONTROL_STICKS)==GL_OK);
    CHECK(gl_get_flick_input(f.c,&v)==GL_OK);CHECK(!v.available);
    f.controls(1);f.flick(1,.5f);f.tick();CHECK(gl_get_flick_input(f.c,&v)==GL_OK);CHECK(v.stick_x==.5f);
    CHECK(gl_set_endpoint_control_authority(f.c,1,0)==GL_OK);
    CHECK(gl_disconnect_endpoint(f.c,1)==GL_OK);f.controls(2);f.flick(2,.75f);f.tick();
    CHECK(gl_get_flick_input(f.c,&v)==GL_OK);CHECK(v.stick_x==.75f);
    f.device(1,GL_SOURCE_SDL,1);f.controls(1);f.flick(1,.25f);f.tick();
    CHECK(gl_get_flick_input(f.c,&v)==GL_OK);CHECK(v.stick_x==.25f);
    // Physical topology says there is no right stick: never invent one.
    gl_endpoint e{};CHECK(gl_get_endpoint(f.c,0,&e)==GL_OK);e.caps.sticks=GL_LEFT;
    CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(gl_set_endpoint_control_authority(f.c,1,GL_CONTROL_STICKS)==GL_OK);
    CHECK(gl_get_flick_input(f.c,&v)==GL_OK);CHECK(!v.available);
}
static void contact_edits_replace_alias(){
    for(auto family:{GL_CONTACT_TOUCHPAD,GL_CONTACT_STICK,GL_CONTACT_GRIP}){
        Fixture f;f.device(1,GL_SOURCE_SDL,1);gl_endpoint e{};CHECK(gl_get_endpoint(f.c,0,&e)==GL_OK);
        e.caps.touchpads=e.caps.stick_touch=e.caps.grip_touch=GL_LEFT|GL_RIGHT;
        CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(gl_set_button_contact(f.c,1,21,family,GL_LEFT)==GL_OK);
        CHECK(gl_select_device(f.c,1)==GL_OK);f.controls(1);f.tick();
        CHECK(gl_setting_set(f.c,"context.1.activation.button",22)==GL_OK);
        CHECK(gl_set_context_parent(f.c,2,1)==GL_OK);
        const char* key=family==GL_CONTACT_TOUCHPAD?"context.2.activation.touchpad":
            family==GL_CONTACT_STICK?"context.2.activation.stick_touch":"context.2.activation.grip_touch";
        CHECK(f.get(key,true)==double(GL_SIDE_LEFT));
        CHECK(gl_setting_set(f.c,key,GL_SIDE_RIGHT)==GL_OK);
        CHECK(f.get(key,true)==double(GL_SIDE_RIGHT));CHECK(f.get("context.2.activation.button")==0);
        CHECK(f.get("context.1.activation.button")==22); // editing a child never rewrites its parent
        CHECK(gl_setting_set(f.c,key,GL_SIDE_OFF)==GL_OK);CHECK(f.get(key,true)==double(GL_SIDE_OFF));
        CHECK(gl_setting_inherit(f.c,"context.2.activation.button")==GL_OK);
        CHECK(f.get(key,true)==double(GL_SIDE_LEFT));
        CHECK(gl_setting_set(f.c,key,GL_SIDE_OFF)==GL_OK);CHECK(f.get(key,true)==double(GL_SIDE_OFF));
    }
}
static void inheritance_has_no_hidden_curve_override(){
    Fixture f;
    CHECK(gl_setting_set(f.c,"context.1.sensitivity_x",6)==GL_OK);
    CHECK(gl_set_context_parent(f.c,2,1)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.sensitivity_x",2)==GL_OK);
    CHECK(gl_setting_inherit(f.c,"context.2.sensitivity_x")==GL_OK);
    CHECK(gl_setting_set(f.c,"context.1.gyro.acceleration",1)==GL_OK);
    CHECK(f.get("context.2.gyro.acceleration")==1);CHECK(f.get("context.2.gyro.fast_sensitivity_x")==9);
    gl_setting_inheritance_info i{};CHECK(gl_setting_inheritance(f.c,"context.2.gyro.fast_sensitivity_x",&i)==GL_OK);CHECK(!i.overridden);
    CHECK(gl_setting_set(f.c,"context.2.sensitivity_x",2)==GL_OK);
    CHECK(f.get("context.2.gyro.fast_sensitivity_x")==3);CHECK(f.get("context.2.gyro.acceleration")==1);
    const auto path=std::filesystem::current_path()/"sdk-derived-curve.ini";
    CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);
    CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);
    CHECK(f.get("context.2.gyro.fast_sensitivity_x")==3);
    CHECK(gl_setting_set(f.c,"context.1.gyro.acceleration",3)==GL_OK);
    CHECK(f.get("context.2.gyro.fast_sensitivity_x")==6); // derives the new inherited preset
    CHECK(gl_setting_inherit(f.c,"context.2.sensitivity_x")==GL_OK);
    CHECK(f.get("context.2.gyro.fast_sensitivity_x")==18);
    CHECK(gl_setting_set(f.c,"context.2.gyro.fast_sensitivity_x",8)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.sensitivity_x",3)==GL_OK);
    CHECK(gl_setting_inherit(f.c,"context.2.sensitivity_x")==GL_OK);
    CHECK(f.get("context.2.gyro.fast_sensitivity_x")==8); // preserve an intentional advanced edit
    CHECK(f.get("context.2.gyro.acceleration")==4);
    std::filesystem::remove(path);
}
static void partial_ini_presets(){
    Fixture f;const auto path=std::filesystem::current_path()/"sdk-partial-preset.ini";
    const double gain[]={1,1.5,2,3};
    for(int preset=0;preset<4;++preset){
        {std::ofstream out(path);out<<"schema=0.2.0\ncontext.1.sensitivity_x=6\ncontext.1.sensitivity_y=4\ncontext.1.gyro.acceleration="<<preset<<'\n';}
        CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);
        CHECK(f.get("context.1.gyro.acceleration")==preset);
        CHECK(f.get("context.1.gyro.fast_sensitivity_x")==6*gain[preset]);
        CHECK(f.get("context.1.gyro.fast_sensitivity_y")==4*gain[preset]);
        CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);
        CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);CHECK(f.get("context.1.gyro.acceleration")==preset);
    }
    {std::ofstream out(path);out<<"schema=0.2.0\ncontext.1.sensitivity_x=6\ncontext.1.gyro.acceleration=1\ncontext.1.gyro.fast_sensitivity_x=8\n";}
    CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);
    CHECK(f.get("context.1.gyro.acceleration")==4);CHECK(f.get("context.1.gyro.fast_sensitivity_x")==8);
    CHECK(f.get("context.1.gyro.fast_sensitivity_y")==3.75);
    {std::ofstream out(path);out<<"schema=0.2.0\ncontext.1.sensitivity_x=6\ncontext.1.gyro.acceleration=3\n"
        "context.2.inherit=1\ncontext.2.gyro.acceleration=1\n";}
    CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);
    CHECK(f.get("context.2.gyro.acceleration")==1);CHECK(f.get("context.2.gyro.fast_sensitivity_x")==9);
    std::filesystem::remove(path);
}
static void complementary_flick_sources(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);f.device(2,GL_SOURCE_SDL,2);
    gl_endpoint e{};CHECK(gl_get_endpoint(f.c,1,&e)==GL_OK);e.caps.sticks=0;e.caps.touchpads=GL_RIGHT;
    CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(gl_set_motion_companion(f.c,2)==GL_OK);
    CHECK(gl_bind_motion_sensor(f.c,1,2)==GL_OK);
    CHECK(gl_set_endpoint_control_authority(f.c,1,GL_CONTROL_STICKS)==GL_OK);
    CHECK(gl_set_endpoint_control_authority(f.c,2,GL_CONTROL_TOUCHPADS)==GL_OK);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.flick.mode",GL_FLICK_BOTH)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.flick.duration_ms",0)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.flick.smoothing_ms",0)==GL_OK);
    const auto submit=[&](uint64_t id,uint64_t stamp,float x,float y){
        gl_flick_input input{stamp,uint32_t(id==1?GL_FLICK_INPUT_STICK:GL_FLICK_INPUT_TOUCHPAD),id==2?1u:0u,
            id==1?x:0,id==1?y:0,id==2?x:0,id==2?y:0};
        CHECK(gl_submit_flick_input(f.c,id,&input)==GL_OK);
    };
    submit(1,f.now,0,0);submit(2,f.now,0,0);f.controls(1);f.controls(2);f.tick();
    CHECK(f.get("context.2.flick.mode",true)==double(GL_FLICK_BOTH));
    gl_flick_input input{};CHECK(gl_get_flick_input(f.c,&input)==GL_OK);CHECK(input.available==3);
    // Two different report clocks in one frame: each physical input pivots once.
    submit(1,f.now-5000000,1,0);submit(2,f.now,1,0);f.tick();
    double yaw=0;gl_set_camera_callback(f.c,[](void* p,double x,double){*static_cast<double*>(p)+=x;},&yaw);
    submit(1,f.now-5000000,0,-1);submit(2,f.now,0,-1);f.tick();
    CHECK(std::abs(yaw-180)<1e-4); // both circular rotations, no family is lost
    yaw=0;f.tick();CHECK(std::abs(yaw)<1e-6); // unchanged reports are not replayed
    // A duplicate endpoint cannot double the stick movement.
    f.device(3,GL_SOURCE_STEAM,1);submit(1,f.now,-1,0);f.flick(3,1);f.tick();
    CHECK(std::abs(yaw-90)<1e-4);
    // Keep the stick alive at normal frame intervals while only the pad expires.
    for(int n=0;n<17;++n){submit(1,f.now,-1,0);f.controls(1);f.tick();}
    CHECK(f.get("context.2.flick.mode",true)==double(GL_FLICK_ON));
    CHECK(gl_get_flick_input(f.c,&input)==GL_OK);CHECK(input.available==GL_FLICK_INPUT_STICK);
    yaw=0;submit(1,f.now,0,1);f.tick();CHECK(std::abs(yaw-90)<1e-4);
    yaw=0;submit(2,f.now,1,0);f.controls(2);f.tick();
    CHECK(f.get("context.2.flick.mode",true)==double(GL_FLICK_BOTH));CHECK(std::abs(yaw)<1e-6); // no reconnect pivot
}
static void model_notifications(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);f.device(2,GL_SOURCE_SDL,2);
    const auto drain=[&](uint32_t type){gl_event_ex e{};unsigned n=0;while(gl_poll_event_ex(f.c,&e)>0)if(e.type==type)++n;return n;};
    CHECK(gl_select_device(f.c,1)==GL_OK);f.controls(1);f.controls(2);f.tick();drain(0);
    CHECK(gl_select_device(f.c,2)==GL_OK);CHECK(drain(GL_EVENT_DEVICE)>0);
    CHECK(gl_select_device(f.c,2)==GL_OK);CHECK(drain(GL_EVENT_DEVICE)==0);
    gl_endpoint e{};CHECK(gl_get_endpoint(f.c,1,&e)==GL_OK);e.caps.touchpads=GL_SINGLE;
    CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(drain(GL_EVENT_DEVICE)>0);
    CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(drain(GL_EVENT_DEVICE)==0);
    f.tick();drain(0);f.now+=200000000;f.tick();CHECK(drain(GL_EVENT_DEVICE)>0);
    f.tick();CHECK(drain(GL_EVENT_DEVICE)==0); // expiry notifies once
    f.controls(2);f.tick();CHECK(drain(GL_EVENT_DEVICE)>0); // data resumes
    CHECK(gl_set_gameplay_context_state(f.c,1,1,1)==GL_OK);
    CHECK(gl_set_gameplay_context_state(f.c,2,0,1)==GL_OK);CHECK(drain(GL_EVENT_CONTEXT)>0);
    CHECK(gl_set_gameplay_context_state(f.c,1,1,1)==GL_OK);
    CHECK(gl_set_gameplay_context_state(f.c,2,0,1)==GL_OK);CHECK(drain(GL_EVENT_CONTEXT)==0);
    gl_output out{};CHECK(gl_update(f.c,f.now,&f.host,&out)==GL_OK);CHECK(gl_get_active_gameplay_context(f.c)==1);
    CHECK(drain(GL_EVENT_CONTEXT)>0);
}
static void local_acceleration_presets_follow_sensitivity(){
    for(int preset=0;preset<4;++preset){
        Fixture f;gl_gameplay_context view{3,"Grandchild","",0};
        CHECK(gl_register_gameplay_context(f.c,&view)==GL_OK);
        CHECK(gl_set_context_parent(f.c,2,1)==GL_OK);CHECK(gl_set_context_parent(f.c,3,2)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.3.gyro.acceleration",preset)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.3.sensitivity_x",6)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.3.sensitivity_y",4)==GL_OK);
        const double gain[]={1,1.5,2,3};
        const auto verify=[&]{CHECK(f.get("context.3.gyro.acceleration")==preset);
            CHECK(f.get("context.3.gyro.fast_sensitivity_x")==6*gain[preset]);
            CHECK(f.get("context.3.gyro.fast_sensitivity_y")==4*gain[preset]);};
        verify();CHECK(f.get("context.1.sensitivity_x")==2.5);CHECK(f.get("context.2.sensitivity_x")==2.5);
        CHECK(gl_capture_recommended_settings(f.c)==GL_OK);
        const auto path=std::filesystem::current_path()/"sdk-local-preset.ini";
        CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);
        CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);verify();
        CHECK(gl_setting_set(f.c,"context.3.gyro.fast_sensitivity_x",8)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.3.sensitivity_x",3)==GL_OK);
        CHECK(f.get("context.3.gyro.acceleration")==4);CHECK(f.get("context.3.gyro.fast_sensitivity_x")==8);
        CHECK(gl_apply_recommended_settings(f.c)==GL_OK);verify();
        CHECK(gl_setting_inherit(f.c,"context.3.gyro.acceleration")==GL_OK);
        gl_setting_inheritance_info info{};
        CHECK(gl_setting_inheritance(f.c,"context.3.gyro.fast_sensitivity_x",&info)==GL_OK);CHECK(!info.overridden);
        std::filesystem::remove(path);
    }
}
static void custom_transition_preserves_effective_curve(){
    for(const char* edited:{"gyro.fast_sensitivity_x","gyro.fast_sensitivity_y","gyro.slow_threshold_dps","gyro.fast_threshold_dps"}){
        Fixture f;gl_gameplay_context view{3,"Grandchild","",0};
        CHECK(gl_register_gameplay_context(f.c,&view)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.1.sensitivity_x",6)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.1.sensitivity_y",4)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.1.gyro.acceleration",1)==GL_OK);
        CHECK(gl_set_context_parent(f.c,2,1)==GL_OK);CHECK(gl_set_context_parent(f.c,3,2)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.3.sensitivity_x",2)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.3.sensitivity_y",1)==GL_OK);
        const char* fields[]={"gyro.fast_sensitivity_x","gyro.fast_sensitivity_y","gyro.slow_threshold_dps","gyro.fast_threshold_dps"};
        double expected[]={3,1.5,5,75};
        const std::string key=std::string("context.3.")+edited;
        // An unchanged value must neither select Custom nor pin the whole curve.
        CHECK(gl_setting_set(f.c,key.c_str(),f.get(key.c_str()))==GL_OK);
        CHECK(f.get("context.3.gyro.acceleration")==1);
        for(unsigned i=0;i<4;++i)if(!std::strcmp(fields[i],edited))expected[i]+=1;
        CHECK(gl_setting_set(f.c,key.c_str(),f.get(key.c_str())+1)==GL_OK);
        const auto verify=[&]{CHECK(f.get("context.3.gyro.acceleration")==4);
            for(unsigned i=0;i<4;++i){const auto field=std::string("context.3.")+fields[i];
                CHECK(f.get(field.c_str())==expected[i]);gl_setting_inheritance_info info{};
                CHECK(gl_setting_inheritance(f.c,field.c_str(),&info)==GL_OK);
                CHECK(bool(info.overridden)==(i<2||!std::strcmp(fields[i],edited)));}};
        verify();CHECK(gl_capture_recommended_settings(f.c)==GL_OK);
        const auto path=std::filesystem::current_path()/"sdk-custom-transition.ini";
        CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);
        CHECK(gl_load_settings(f.c,path.string().c_str())==GL_OK);verify();
        CHECK(gl_setting_set(f.c,"context.1.gyro.acceleration",3)==GL_OK);verify();
        CHECK(gl_setting_inherit(f.c,"context.3.gyro.acceleration")==GL_OK);
        CHECK(f.get("context.3.gyro.acceleration")==3);CHECK(f.get("context.3.gyro.fast_sensitivity_x")==6);
        CHECK(gl_apply_recommended_settings(f.c)==GL_OK);verify();
        std::filesystem::remove(path);
    }
}
static void delayed_flick_reports(){
    for(bool pad:{false,true})for(int smoothing:{0,50}){
        Fixture f;f.device(1,GL_SOURCE_SDL,1);gl_endpoint e{};CHECK(gl_get_endpoint(f.c,0,&e)==GL_OK);
        e.caps.touchpads=GL_RIGHT;CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(gl_select_device(f.c,1)==GL_OK);
        CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.2.flick.mode",pad?GL_FLICK_TOUCHPAD:GL_FLICK_ON)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.2.flick.duration_ms",0)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.2.flick.smoothing_ms",smoothing)==GL_OK);
        double yaw=0;gl_set_camera_callback(f.c,[](void* p,double x,double){*static_cast<double*>(p)+=x;},&yaw);
        const auto submit=[&](uint64_t stamp,float x,float y){
            gl_flick_input v{stamp,uint32_t(pad?GL_FLICK_INPUT_TOUCHPAD:GL_FLICK_INPUT_STICK),pad?1u:0u,
                pad?0:x,pad?0:y,pad?x:0,pad?y:0};CHECK(gl_submit_flick_input(f.c,1,&v)==GL_OK);};
        submit(f.now,0,0);f.tick();submit(f.now,0,1);f.tick();f.tick();f.tick();
        // Both reports precede the last rendered frame, but follow the last input.
        submit(f.now-25000000,1,0);submit(f.now-15000000,0,-1);f.tick();
        CHECK(std::abs(yaw-180)<1e-4);
        yaw=0;f.tick();f.tick();CHECK(std::abs(yaw)<1e-6);
        // An exact duplicate stays silent; an out-of-order report is rejected.
        submit(f.now-45000000,0,-1);f.tick();CHECK(std::abs(yaw)<1e-6);
        gl_flick_input old{f.now-60000000,uint32_t(pad?GL_FLICK_INPUT_TOUCHPAD:GL_FLICK_INPUT_STICK)};
        CHECK(gl_submit_flick_input(f.c,1,&old)==GL_INVALID);
        // An input timestamp equal to the last frame is also consumed once.
        submit(f.now-10000000,-1,0);f.tick();CHECK(std::abs(yaw-90)<1e-4);
        yaw=0;for(int n=0;n<17;++n)f.tick(); // expire without a long frame
        submit(f.now,1,0);f.tick();CHECK(std::abs(yaw)<1e-6); // rejoin without pivot
        submit(f.now,0,-1);f.tick();CHECK(std::abs(yaw-90)<1e-4);
    }
}
static void delayed_flick_does_not_replay_animation_time(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);CHECK(gl_select_device(f.c,1)==GL_OK);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.flick.mode",GL_FLICK_ON)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.flick.duration_ms",100)==GL_OK);
    CHECK(gl_setting_set(f.c,"context.2.flick.smoothing_ms",0)==GL_OK);
    f.flick(1,0);f.tick();f.tick();f.tick();
    gl_flick_input delayed{f.now-15000000,GL_FLICK_INPUT_STICK,0,1,0,0,0};
    CHECK(gl_submit_flick_input(f.c,1,&delayed)==GL_OK);
    double yaw=0;gl_set_camera_callback(f.c,[](void* p,double x,double){*static_cast<double*>(p)+=x;},&yaw);
    f.tick();CHECK(std::abs(yaw-90*(1-std::pow(.9,3)))<1e-6); // only this frame's 10 ms
    for(int i=0;i<9;++i)f.tick();CHECK(std::abs(yaw-90)<1e-4);
}
static void delayed_flick_uses_report_speed_for_smoothing(){
    for(bool pad:{false,true}){
        Fixture f;f.device(1,GL_SOURCE_SDL,1);gl_endpoint e{};CHECK(gl_get_endpoint(f.c,0,&e)==GL_OK);
        e.caps.touchpads=GL_RIGHT;CHECK(gl_register_endpoint(f.c,&e)==GL_OK);CHECK(gl_select_device(f.c,1)==GL_OK);
        CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.2.flick.mode",pad?GL_FLICK_TOUCHPAD:GL_FLICK_ON)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.2.flick.duration_ms",0)==GL_OK);
        CHECK(gl_setting_set(f.c,"context.2.flick.smoothing_ms",50)==GL_OK);
        const auto submit=[&](uint64_t stamp,float x,float y){
            gl_flick_input v{stamp,uint32_t(pad?GL_FLICK_INPUT_TOUCHPAD:GL_FLICK_INPUT_STICK),pad?1u:0u,
                pad?0:x,pad?0:y,pad?x:0,pad?y:0};CHECK(gl_submit_flick_input(f.c,1,&v)==GL_OK);};
        submit(f.now,0,0);f.tick();submit(f.now,0,1);f.tick();f.tick();f.tick();
        const double radians=.3*std::acos(-1)/180;
        submit(f.now-20000000,float(std::sin(radians)),float(std::cos(radians)));
        double yaw=0;gl_set_camera_callback(f.c,[](void* p,double x,double){*static_cast<double*>(p)+=x;},&yaw);
        f.tick(); // 0.3 degrees / 10 ms = 30 deg/s, between half and full 45 deg/s limit
        CHECK(std::abs(yaw-(.1+.2*(-std::expm1(-.01/.05))))<1e-5);
        submit(f.now,0,0);f.tick();CHECK(std::abs(yaw-.3)<1e-5); // release flushes the remaining smoothing
        f.tick();CHECK(std::abs(yaw-.3)<1e-5);
    }
}
static void forgetting_endpoints_notifies(){
    for(bool disconnected:{false,true}){
        Fixture f;f.device(1,GL_SOURCE_SDL,1);f.device(2,GL_SOURCE_SDL,2);
        CHECK(gl_select_device(f.c,1)==GL_OK);f.controls(1);f.controls(2);f.tick();
        if(disconnected)CHECK(gl_disconnect_endpoint(f.c,2)==GL_OK);
        gl_event_ex event{};while(gl_poll_event_ex(f.c,&event)>0){}
        CHECK(gl_forget_endpoint(f.c,2)==GL_OK);
        CHECK(gl_poll_event_ex(f.c,&event)==1);CHECK(event.type==GL_EVENT_DEVICE&&event.endpoint_id==2&&event.detail==0);
        CHECK(gl_poll_event_ex(f.c,&event)==0);CHECK(gl_endpoint_count(f.c)==1);CHECK(gl_get_selected_device(f.c)==1);
        f.tick();CHECK(gl_poll_event_ex(f.c,&event)==0);
        CHECK(gl_forget_endpoint(f.c,2)==GL_INVALID);CHECK(gl_poll_event_ex(f.c,&event)==0);
    }
}
static void global_output_target_notifications(){
    Fixture f;gl_gameplay_context view{3,"Inactive view","",0};CHECK(gl_register_gameplay_context(f.c,&view)==GL_OK);
    CHECK(gl_set_host_capabilities(f.c,GL_HOST_MENU_STATE|GL_HOST_NATIVE_STICK_SUPPRESSION)==GL_OK);
    CHECK(gl_set_gameplay_context_output_target(f.c,2,GL_OUTPUT_CAMERA)==GL_OK);
    CHECK(gl_set_gameplay_context_state(f.c,2,1,1)==GL_OK);
    const auto flick_visible=[&](uint32_t id){
        const uint64_t tab=uint64_t(id)+1;
        for(uint32_t n=0;n<gl_menu_tab_setting_count(f.c,tab);++n){gl_setting_info info{};
            CHECK(gl_menu_tab_setting_at(f.c,tab,n,&info)==GL_OK);
            if(std::string(info.id)=="context."+std::to_string(id)+".flick.mode")return info.visible;
        }throw std::runtime_error("Missing flick row");
    };
    gl_event_ex event{};while(gl_poll_event_ex(f.c,&event)>0){}
    // Both inactive, undeclared views must invalidate; the explicit active view must not.
    for(auto target:{GL_OUTPUT_CURSOR,GL_OUTPUT_CAMERA}){
        CHECK(gl_set_output_target(f.c,target)==GL_OK);
        for(uint32_t id:{1u,3u}){
            CHECK(gl_poll_event_ex(f.c,&event)==1);
            CHECK(event.type==GL_EVENT_CONTEXT&&event.detail==3&&event.value==id);
            CHECK(flick_visible(id)==uint32_t(target==GL_OUTPUT_CAMERA));
        }
        CHECK(gl_poll_event_ex(f.c,&event)==0);
        CHECK(gl_get_output_target(f.c)==GL_OUTPUT_CAMERA);CHECK(flick_visible(2)==1);
        CHECK(gl_set_output_target(f.c,target)==GL_OK);CHECK(gl_poll_event_ex(f.c,&event)==0);
    }
    CHECK(gl_set_output_target(f.c,2)==GL_INVALID);CHECK(gl_poll_event_ex(f.c,&event)==0);
    // Explicit destinations are independent even when they differ from each other.
    CHECK(gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CURSOR)==GL_OK);
    CHECK(gl_set_gameplay_context_output_target(f.c,3,GL_OUTPUT_CAMERA)==GL_OK);
    while(gl_poll_event_ex(f.c,&event)>0){}
    CHECK(gl_set_output_target(f.c,GL_OUTPUT_CURSOR)==GL_OK);CHECK(gl_poll_event_ex(f.c,&event)==0);
    CHECK(flick_visible(1)==0);CHECK(flick_visible(2)==1);CHECK(flick_visible(3)==1);
}
int main()try{contacts_preserve_preferences();complete_event_ids();stale_flick_fallback();
    contact_edits_replace_alias();inheritance_has_no_hidden_curve_override();partial_ini_presets();complementary_flick_sources();model_notifications();
    local_acceleration_presets_follow_sensitivity();custom_transition_preserves_effective_curve();
    delayed_flick_reports();delayed_flick_does_not_replay_animation_time();delayed_flick_uses_report_speed_for_smoothing();
    forgetting_endpoints_notifies();global_output_target_notifications();return 0;}
catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
