#include <gyrolib/gyrolib.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <source_location>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
static std::string key(uint32_t id,const char* setting){return "context."+std::to_string(id)+"."+setting;}
static void set(gl_context* c,uint32_t id,const char* setting,double v){CHECK(gl_setting_set(c,key(id,setting).c_str(),v)==GL_OK);}
static double get(gl_context* c,uint32_t id,const char* setting){double v;CHECK(gl_setting_get(c,key(id,setting).c_str(),&v)==GL_OK);return v;}
static void near(double a,double b,std::source_location at=std::source_location::current()){
    if(std::abs(a-b)>=1e-5)throw std::runtime_error(std::to_string(at.line())+": expected "+std::to_string(b)+", got "+std::to_string(a));}
static void view(gl_context* c,uint32_t id){gl_gameplay_context v{id,"View","",0};CHECK(gl_register_gameplay_context(c,&v)==GL_OK);}
static gl_setting_inheritance_info info(gl_context* c,uint32_t id,const char* s){gl_setting_inheritance_info i{};CHECK(gl_setting_inheritance(c,key(id,s).c_str(),&i)==GL_OK);return i;}
static void inherit(gl_context* c,uint32_t id,const char* s){CHECK(gl_setting_inherit(c,key(id,s).c_str())==GL_OK);}
static gl_setting_info metadata(gl_context* c,const char* id){for(uint32_t n=0;n<gl_menu_setting_count(c);++n){gl_setting_info s{};gl_setting_at(c,n,&s);if(!std::strcmp(s.id,id))return s;}throw std::runtime_error(id);}
static void chains(){
    gyrolib::Context owner;auto* c=owner.get();for(uint32_t id:{1,2,3,4})view(c,id);
    set(c,1,"sensitivity_x",6);set(c,1,"gyro.smoothing_ms",30);
    CHECK(gl_set_context_parent(c,2,1)==GL_OK);CHECK(gl_set_context_parent(c,3,2)==GL_OK);
    near(get(c,3,"sensitivity_x"),6);CHECK(info(c,3,"sensitivity_x").source_context==1);
    CHECK(!gl_can_inherit_context(c,1,3));CHECK(gl_set_context_parent(c,1,3)==GL_INVALID);
    CHECK(gl_set_context_parent(c,2,2)==GL_INVALID);CHECK(gl_set_context_parent(c,2,999)==GL_INVALID);
    CHECK(gl_get_context_parent(c,2)==1);
    set(c,3,"sensitivity_x",6);CHECK(info(c,3,"sensitivity_x").overridden);
    set(c,1,"sensitivity_x",4);near(get(c,2,"sensitivity_x"),4);near(get(c,3,"sensitivity_x"),6);
    inherit(c,3,"sensitivity_x");near(get(c,3,"sensitivity_x"),4);CHECK(!info(c,3,"sensitivity_x").overridden);
    set(c,2,"gyro.smoothing_ms",55);near(get(c,3,"gyro.smoothing_ms"),55);CHECK(info(c,3,"gyro.smoothing_ms").source_context==2);
    near(metadata(c,"context.3.gyro.smoothing_ms").value,55);
    bool notified=false;gl_event event{};while(gl_poll_event(c,&event)==1)if(event.type==GL_EVENT_SETTING&&!std::strcmp(event.setting_id,"context.3.gyro.smoothing_ms"))notified=true;CHECK(notified);
    CHECK(gl_set_context_parent(c,2,4)==GL_OK);near(get(c,2,"gyro.smoothing_ms"),55);near(get(c,3,"sensitivity_x"),2.5);
    CHECK(gl_set_context_parent(c,2,0)==GL_OK);set(c,4,"sensitivity_x",10);near(get(c,2,"sensitivity_x"),2.5);
    CHECK(gl_set_context_parent(c,2,1)==GL_OK);near(get(c,2,"gyro.smoothing_ms"),30); // re-link starts inherited
    CHECK(gl_unregister_gameplay_context(c,1)==GL_OK);CHECK(!gl_get_context_parent(c,2));near(get(c,2,"sensitivity_x"),4);
    near(get(c,3,"sensitivity_x"),4);view(c,1);set(c,1,"sensitivity_x",8);near(get(c,3,"sensitivity_x"),4);
    CHECK(gl_set_gameplay_context_output_target(c,3,GL_OUTPUT_CURSOR)==GL_OK);
    CHECK(!metadata(c,"context.3.flick.mode").visible);CHECK(gl_get_context_parent(c,3)==2);
}
static void coupled_settings(){
    gyrolib::Context owner;auto* c=owner.get();view(c,1);view(c,2);CHECK(gl_set_context_parent(c,2,1)==GL_OK);
    set(c,1,"gyro.acceleration",1);set(c,2,"gyro.acceleration",1);
    CHECK(info(c,2,"gyro.fast_sensitivity_x").overridden); // entire explicit preset is local, even equal values
    set(c,1,"gyro.acceleration",3);near(get(c,2,"gyro.fast_sensitivity_x"),3.75);
    inherit(c,2,"gyro.acceleration");near(get(c,2,"gyro.fast_sensitivity_x"),7.5);CHECK(!info(c,2,"gyro.fast_sensitivity_x").overridden);
    set(c,2,"gyro.fast_sensitivity_x",8);near(get(c,2,"gyro.acceleration"),4);inherit(c,2,"gyro.fast_sensitivity_x");near(get(c,2,"gyro.acceleration"),3);
    set(c,2,"flick.stick_release_threshold",.8);set(c,1,"flick.stick_start_threshold",.3);
    CHECK(get(c,2,"flick.stick_release_threshold")<get(c,2,"flick.stick_start_threshold"));
    set(c,2,"gyro.invert_x",1);CHECK(info(c,2,"gyro.invert_x").overridden);CHECK(!info(c,2,"gyro.invert_y").overridden);
}
static void persistence_and_recommendations(){
    const auto path=std::filesystem::absolute("inheritance-test.ini");std::filesystem::remove(path);
    gyrolib::Context owner;auto* c=owner.get();for(uint32_t id:{1,2,3})view(c,id);
    CHECK(!gl_has_recommended_settings(c));CHECK(!metadata(c,"settings.recommended").visible);
    CHECK(gl_apply_recommended_settings(c)==GL_UNAVAILABLE);
    set(c,1,"sensitivity_x",3);set(c,1,"gyro.smoothing_ms",30);
    CHECK(gl_set_context_parent(c,2,1)==GL_OK);CHECK(gl_set_context_parent(c,3,2)==GL_OK);set(c,3,"sensitivity_x",1.5);
    CHECK(gl_capture_recommended_settings(c)==GL_OK);CHECK(metadata(c,"settings.recommended").visible);
    CHECK(gl_set_settings_path(c,path.string().c_str())==GL_OK);
    set(c,1,"gyro.smoothing_ms",80);set(c,3,"sensitivity_x",2);
    {std::ifstream file(path);std::string text((std::istreambuf_iterator<char>(file)),{});
        CHECK(text.find("schema=0.2.0")!=std::string::npos);CHECK(text.find("context.3.inherit=2")!=std::string::npos);
        CHECK(text.find("context.3.gyro.smoothing_ms=")==std::string::npos);CHECK(text.find("context.3.sensitivity_x=2")!=std::string::npos);}
    gyrolib::Context restored;auto* r=restored.get();for(uint32_t id:{1,2,3})view(r,id);
    CHECK(gl_load_settings(r,path.string().c_str())==GL_OK);CHECK(gl_get_context_parent(r,3)==2);near(get(r,3,"gyro.smoothing_ms"),80);near(get(r,3,"sensitivity_x"),2);
    CHECK(!gl_has_recommended_settings(r)); // recommendations are code-owned, never loaded from player settings
    CHECK(gl_set_language(c,"fr")==GL_OK);CHECK(gl_set_menu_key(c,8)==GL_OK);
    gl_reset_settings(c);near(get(c,3,"sensitivity_x"),2.5);CHECK(!gl_get_context_parent(c,3));CHECK(gl_has_recommended_settings(c));
    view(c,99);set(c,99,"sensitivity_x",11);CHECK(gl_action(c,"settings.recommended")==GL_OK);
    near(get(c,3,"sensitivity_x"),1.5);near(get(c,3,"gyro.smoothing_ms"),30);CHECK(gl_get_context_parent(c,3)==2);
    near(get(c,99,"sensitivity_x"),11);CHECK(gl_get_menu_key(c)==8);CHECK(!std::strcmp(gl_get_language(c),"fr"));
    CHECK(gl_load_settings(r,path.string().c_str())==GL_OK);near(get(r,3,"sensitivity_x"),1.5);
    // Invalid files leave effective values, parents and menu key untouched.
    for(const char* invalid:{"schema=0.2.0\ncontext.1.inherit=3\ncontext.2.inherit=1\ncontext.3.inherit=2\n",
        "schema=0.2.0\ncontext.3.inherit=888\n","schema=0.2.0\ncontext.3.inherit=-1\n"}){
        {std::ofstream file(path);file<<invalid;}
        CHECK(gl_load_settings(r,path.string().c_str())==GL_INVALID);near(get(r,3,"sensitivity_x"),1.5);CHECK(gl_get_context_parent(r,3)==2);
    }
    {std::ofstream file(path);file<<"schema=0.2.0\ncontext.1.sensitivity_x=7\ncontext.3.sensitivity_x=5\nfuture.keep=yes\n";}
    CHECK(gl_load_settings(r,path.string().c_str())==GL_OK);CHECK(!gl_get_context_parent(r,3));near(get(r,3,"sensitivity_x"),5);
    CHECK(gl_save_settings(r,path.string().c_str())==GL_OK);
    {std::ifstream file(path);std::string text((std::istreambuf_iterator<char>(file)),{});CHECK(text.find("future.keep=yes")!=std::string::npos);}
    // Detaching after restoring a sensitivity must not turn Off into Custom on reload.
    set(r,1,"gyro.acceleration",0);CHECK(gl_set_context_parent(r,3,1)==GL_OK);set(r,3,"sensitivity_x",2);inherit(r,3,"sensitivity_x");
    CHECK(gl_set_context_parent(r,3,0)==GL_OK);CHECK(gl_load_settings(c,path.string().c_str())==GL_OK);
    near(get(c,3,"gyro.acceleration"),0);near(get(c,3,"sensitivity_x"),7);
    std::filesystem::remove(path);
}
static void output_uses_effective_values(){
    gyrolib::Context owner;auto* c=owner.get();view(c,1);view(c,2);
    set(c,1,"gyro.space",GL_SPACE_LOCAL_YAW);set(c,1,"sensitivity_x",2);CHECK(gl_set_context_parent(c,2,1)==GL_OK);
    gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.gyro=e.caps.accelerometer=1;
    CHECK(gl_register_endpoint(c,&e)==GL_OK);uint64_t now=1000000000;
    const auto tick=[&]{now+=10000000;gl_sample s{now,now,{0,30,0},{0,1,0}};CHECK(gl_submit_sample(c,1,&s)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,2,1,1)==GL_OK);gl_host_state host{};host.focused=host.camera_allowed=1;gl_output out{};
        CHECK(gl_update(c,now,&host,&out)==GL_OK);return out;};
    for(int i=0;i<5;++i)tick();near(tick().yaw_degrees,-.6);
    set(c,1,"sensitivity_x",6);near(tick().yaw_degrees,-1.8); // parent need not be the active view
    set(c,2,"sensitivity_x",3);near(tick().yaw_degrees,-.9);inherit(c,2,"sensitivity_x");near(tick().yaw_degrees,-1.8);
}
int main()try{chains();coupled_settings();persistence_and_recommendations();output_uses_effective_values();std::cout<<"Inheritance, recommendations, persistence and real gyro output passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
