// Second audit: observations through the public SDK, never a player INI.
#include <gyrolib/gyrolib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
static void ok(int r){if(r!=GL_OK){std::printf("setup failed %d\n",r);std::exit(2);}}
struct Context {
    gl_context* c=gl_create(GL_ABI_VERSION);uint64_t now=1000000000;
    Context(){if(!c)std::exit(2);for(uint32_t id:{1u,2u}){gl_gameplay_context v{id,"View","",int(id)};ok(gl_register_gameplay_context(c,&v));}}
    ~Context(){gl_destroy(c);}
    void set(const char* k,double v){ok(gl_setting_set(c,k,v));}
    double get(const char* k){double v{};ok(gl_setting_get(c,k,&v));return v;}
    void tick(){gl_host_state h{};h.focused=h.camera_allowed=1;gl_output o{};ok(gl_update(c,now,&h,&o));now+=10000000;}
    void drain(){gl_event_ex e{};while(gl_poll_event_ex(c,&e)>0){}}
    unsigned events(){gl_event_ex e{};unsigned n=0;while(gl_poll_event_ex(c,&e)>0)++n;return n;}
    void device(uint64_t id,uint32_t source,uint64_t physical,uint32_t sticks,uint32_t pads){
        gl_endpoint e{};e.id=id;e.physical_id=physical;e.source=source;e.connected=1;e.caps.sticks=sticks;e.caps.touchpads=pads;
        std::strcpy(e.name,"Audit controller");ok(gl_register_endpoint(c,&e));gl_controls v{};v.timestamp_ns=now;ok(gl_submit_controls(c,id,&v));
    }
};
static void flick_split(){
    Context f;f.device(1,GL_SOURCE_SDL,1,GL_RIGHT,0);f.device(2,GL_SOURCE_SDL,2,0,GL_RIGHT);
    ok(gl_set_motion_companion(f.c,2));ok(gl_bind_motion_sensor(f.c,1,2));
    ok(gl_set_endpoint_control_authority(f.c,1,GL_CONTROL_STICKS));
    ok(gl_set_endpoint_control_authority(f.c,2,GL_CONTROL_TOUCHPADS));
    gl_flick_input stick{f.now,GL_FLICK_INPUT_STICK,0,1,0,0,0};
    gl_flick_input pad{f.now,GL_FLICK_INPUT_TOUCHPAD,1,0,0,1,0};
    ok(gl_submit_flick_input(f.c,1,&stick));ok(gl_submit_flick_input(f.c,2,&pad));
    ok(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION));
    f.set("context.1.flick.mode",GL_FLICK_BOTH);f.tick();gl_flick_input resolved{};ok(gl_get_flick_input(f.c,&resolved));
    double effective{};ok(gl_setting_get_effective(f.c,"context.1.flick.mode",&effective));
    std::printf("split_flick: supplied=%u|%u resolved=%u effective_mode=%.0f (both=%d)\n",stick.available,pad.available,resolved.available,effective,GL_FLICK_BOTH);
}
static void inherited_curve(){
    Context f;f.set("context.1.sensitivity_x",6);ok(gl_set_context_parent(f.c,2,1));
    f.set("context.2.sensitivity_x",2);ok(gl_setting_inherit(f.c,"context.2.sensitivity_x"));
    f.set("context.1.gyro.acceleration",1);gl_setting_inheritance_info i{};ok(gl_setting_inheritance(f.c,"context.2.gyro.fast_sensitivity_x",&i));
    std::printf("inherited_curve: child_slow=%.1f parent_fast=%.1f child_fast=%.1f child_mode=%.0f hidden_override=%u\n",
        f.get("context.2.sensitivity_x"),f.get("context.1.gyro.fast_sensitivity_x"),f.get("context.2.gyro.fast_sensitivity_x"),f.get("context.2.gyro.acceleration"),i.overridden);
}
static void ini_presets(){
    Context f;const auto path=std::filesystem::absolute("reaudit-preset.ini");
    for(int preset:{0,1,3}){
        {std::ofstream out(path);out<<"schema=0.2.0\ncontext.1.sensitivity_x=6\ncontext.1.gyro.acceleration="<<preset<<'\n';}
        ok(gl_load_settings(f.c,path.string().c_str()));
        std::printf("ini_preset: requested=%d actual=%.0f slow=%.1f fast=%.1f\n",preset,f.get("context.1.gyro.acceleration"),f.get("context.1.sensitivity_x"),f.get("context.1.gyro.fast_sensitivity_x"));
    }std::filesystem::remove(path);
}
static void notifications(){
    Context f;f.device(1,GL_SOURCE_SDL,1,GL_RIGHT,0);f.device(2,GL_SOURCE_SDL,2,GL_RIGHT,0);
    ok(gl_select_device(f.c,1));f.tick();f.drain();ok(gl_select_device(f.c,2));f.tick();
    std::printf("device_select_events: selected=%llu events=%u\n",static_cast<unsigned long long>(gl_get_selected_device(f.c)),f.events());
    gl_endpoint e{};ok(gl_get_endpoint(f.c,1,&e));e.caps.touchpads=GL_SINGLE;ok(gl_register_endpoint(f.c,&e));f.tick();
    std::printf("capability_change_events: events=%u\n",f.events());
    ok(gl_set_gameplay_context_state(f.c,1,1,1));ok(gl_set_gameplay_context_state(f.c,2,0,1));f.tick();f.drain();
    ok(gl_set_gameplay_context_state(f.c,1,0,1));ok(gl_set_gameplay_context_state(f.c,2,1,1));f.tick();
    std::printf("active_view_events: active=%u events=%u\n",gl_get_active_gameplay_context(f.c),f.events());
}
static void contact_edit(){
    Context f;gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;
    e.caps.buttons=1u<<21;e.caps.stick_touch=GL_LEFT|GL_RIGHT;ok(gl_register_endpoint(f.c,&e));
    ok(gl_set_button_contact(f.c,1,21,GL_CONTACT_STICK,GL_LEFT));f.set("context.1.activation.button",22);
    gl_controls controls{};controls.timestamp_ns=f.now;ok(gl_submit_controls(f.c,1,&controls));f.tick();
    const auto effective=[&](const char* key){double value{};ok(gl_setting_get_effective(f.c,key,&value));return value;};
    const double before=effective("context.1.activation.stick_touch");
    f.set("context.1.activation.stick_touch",GL_SIDE_OFF);
    std::printf("contact_edit: displayed_button=%.0f displayed_contact_before=%.0f requested_off=%d contact_after=%.0f\n",
        effective("context.1.activation.button"),before,GL_SIDE_OFF,effective("context.1.activation.stick_touch"));
    f.set("context.1.activation.stick_touch",GL_SIDE_RIGHT);
    std::printf("contact_edit: requested_right=%d actual=%.0f saved_button=%.0f\n",GL_SIDE_RIGHT,effective("context.1.activation.stick_touch"),f.get("context.1.activation.button"));
}
int main(){flick_split();inherited_curve();ini_presets();notifications();contact_edit();}
