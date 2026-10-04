// Public-API observations for audit C. No physical input or player settings.
#include <gyrolib/gyrolib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
static void ok(int r){if(r!=GL_OK){std::printf("setup failed %d\n",r);std::exit(2);}}
struct Context {
    gl_context* c=gl_create(GL_ABI_VERSION);uint64_t now=1000000000;
    Context(){if(!c)std::exit(2);for(uint32_t id:{1u,2u}){gl_gameplay_context v{id,"View","",int(id)};ok(gl_register_gameplay_context(c,&v));}}
    ~Context(){gl_destroy(c);}
    void set(const char* k,double v){ok(gl_setting_set(c,k,v));}
    double get(const char* k){double v{};ok(gl_setting_get(c,k,&v));return v;}
    double tick(){ok(gl_set_gameplay_context_state(c,1,1,1));gl_host_state h{};h.focused=h.camera_allowed=1;
        gl_output o{};ok(gl_update(c,now,&h,&o));now+=10000000;return o.yaw_degrees;}
};
static void explicit_child_preset(){
    for(bool linked:{false,true}){
        Context f;if(linked)ok(gl_set_context_parent(f.c,2,1));
        f.set("context.2.gyro.acceleration",1);f.set("context.2.sensitivity_x",6);
        std::printf("explicit_preset: linked=%d mode=%.0f slow=%.1f fast=%.2f\n",linked,
            f.get("context.2.gyro.acceleration"),f.get("context.2.sensitivity_x"),f.get("context.2.gyro.fast_sensitivity_x"));
    }
}
static void derived_curve_to_custom(){
    Context f;f.set("context.1.sensitivity_x",6);f.set("context.1.gyro.acceleration",1);
    ok(gl_set_context_parent(f.c,2,1));f.set("context.2.sensitivity_x",2);
    const auto before=f.get("context.2.gyro.fast_sensitivity_x");
    f.set("context.2.gyro.slow_threshold_dps",10);
    std::printf("custom_transition: fast_before=%.1f fast_after=%.1f mode=%.0f\n",before,
        f.get("context.2.gyro.fast_sensitivity_x"),f.get("context.2.gyro.acceleration"));
}
static void delayed_flick(){
    for(bool delayed:{false,true}){
        Context f;gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.sticks=GL_RIGHT;
        ok(gl_register_endpoint(f.c,&e));ok(gl_select_device(f.c,1));
        ok(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION));
        f.set("context.1.flick.mode",GL_FLICK_ON);f.set("context.1.flick.duration_ms",0);f.set("context.1.flick.smoothing_ms",0);
        const auto input=[&](uint64_t stamp,float x,float y){gl_flick_input v{stamp,GL_FLICK_INPUT_STICK,0,x,y,0,0};ok(gl_submit_flick_input(f.c,1,&v));};
        input(f.now,0,0);f.tick();input(f.now,0,1);f.tick(); // armed, now pointing forward
        f.tick(); // no new report; render clock advances beyond next sample's timestamp
        input(delayed?f.now-15000000:f.now,1,0);
        const auto turn=f.tick();double later=0;for(int n=0;n<3;++n)later+=f.tick();
        gl_flick_input read{};ok(gl_get_flick_input(f.c,&read));
        std::printf("delayed_flick: delayed=%d yaw=%.3f later=%.3f available=%u x=%.1f\n",delayed,turn,later,read.available,read.stick_x);
    }
}
static void removed_device_notification(){
    Context f;
    for(uint64_t id:{1ull,2ull}){gl_endpoint e{};e.id=e.physical_id=id;e.connected=1;e.source=GL_SOURCE_SDL;
        ok(gl_register_endpoint(f.c,&e));}
    ok(gl_select_device(f.c,1));f.tick();gl_event_ex event{};while(gl_poll_event_ex(f.c,&event)>0){}
    ok(gl_forget_endpoint(f.c,2));f.tick();unsigned count=0;while(gl_poll_event_ex(f.c,&event)>0)++count;
    std::printf("forget_device: remaining=%u selected=%llu events=%u\n",gl_endpoint_count(f.c),
        static_cast<unsigned long long>(gl_get_selected_device(f.c)),count);
}
int main(){explicit_child_preset();derived_curve_to_custom();delayed_flick();removed_device_notification();}
