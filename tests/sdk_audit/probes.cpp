// Audit probes, separate from the regression suite. These print observations;
// they deliberately do not encode known defects as the desired behavior.
#include <gyrolib/gyrolib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <chrono>

static bool fail_allocation;
static std::size_t allocations;
void* operator new(std::size_t n) {
    if(fail_allocation)throw std::bad_alloc();
    ++allocations;
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

static void ok(int result){if(result!=GL_OK){std::printf("setup failed: %d\n",result);std::exit(2);}}
struct Fixture {
    gl_context* c=gl_create(GL_ABI_VERSION);uint64_t now=1000000000;
    gl_host_state host{};
    Fixture(){if(!c)std::exit(2);host.focused=host.camera_allowed=1;
        const gl_gameplay_context view{1,"Audit","",0};ok(gl_register_gameplay_context(c,&view));}
    ~Fixture(){gl_destroy(c);}
    void device(uint64_t id,uint32_t source,uint64_t physical){
        gl_endpoint e{};e.id=id;e.physical_id=physical;e.source=source;e.connected=1;
        e.caps.buttons=0xffffffffu;e.caps.sticks=GL_RIGHT;e.caps.stick_touch=GL_LEFT;e.caps.gyro=e.caps.accelerometer=1;
        std::strcpy(e.name,"Audit synthetic controller");ok(gl_register_endpoint(c,&e));
    }
    void controls(uint64_t id){gl_controls input{};input.timestamp_ns=now;ok(gl_submit_controls(c,id,&input));}
    void tick(){ok(gl_set_gameplay_context_state(c,1,1,1));gl_output out{};ok(gl_update(c,now,&host,&out));now+=10000000;}
    double get(const char* id){double value{};ok(gl_setting_get(c,id,&value));return value;}
};
static void event_id(){
    Fixture f;const gl_gameplay_context view{UINT32_MAX,"Long ID","",0};ok(gl_register_gameplay_context(f.c,&view));
    gl_event event{};while(gl_poll_event(f.c,&event)>0){}
    const char* id="context.4294967295.flick.touchpad_release_threshold";
    ok(gl_setting_set(f.c,id,.25));
    while(gl_poll_event(f.c,&event)>0)if(event.type==GL_EVENT_SETTING){double value{};
        std::printf("event_id: expected='%s' actual='%s' lookup=%d\n",id,event.setting_id,gl_setting_get(f.c,event.setting_id,&value));}
    else if(event.type==GL_EVENT_CONTEXT&&event.detail==6)std::puts("event_id: legacy full-model invalidation (no truncated key)");
    ok(gl_setting_set(f.c,id,.30));gl_event_ex extended{};
    while(gl_poll_event_ex(f.c,&extended)>0)if(extended.type==GL_EVENT_SETTING){double value{};
        std::printf("event_ex: actual='%s' lookup=%d\n",extended.setting_id,gl_setting_get(f.c,extended.setting_id,&value));}
}
static void contact_migration(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);f.device(2,GL_SOURCE_SDL,2);
    // An extra physical button on pad A, a capacitive alias at the same
    // normalized button index on pad B. Both are legal provider metadata.
    ok(gl_set_button_contact(f.c,2,21,GL_CONTACT_STICK,GL_LEFT));
    ok(gl_setting_set(f.c,"context.1.activation.button",22));
    ok(gl_select_device(f.c,1));f.controls(1);f.controls(2);f.tick();
    std::printf("contact_migration: before button=%.0f\n",f.get("context.1.activation.button"));
    ok(gl_select_device(f.c,2));f.controls(1);f.controls(2);f.tick();
    ok(gl_select_device(f.c,1));f.controls(1);f.controls(2);f.tick();
    std::printf("contact_migration: after return button=%.0f stick_touch=%.0f\n",
        f.get("context.1.activation.button"),f.get("context.1.activation.stick_touch"));
}
static void stale_flick(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);f.device(2,GL_SOURCE_STEAM,1);
    gl_flick_input sdl{f.now,GL_FLICK_INPUT_STICK,0,0,0,0,0};ok(gl_submit_flick_input(f.c,1,&sdl));
    f.controls(1);f.controls(2);f.tick();f.now+=200000000;
    gl_flick_input steam{f.now,GL_FLICK_INPUT_STICK,0,1,0,0,0};ok(gl_submit_flick_input(f.c,2,&steam));f.controls(2);f.tick();
    gl_flick_input result{};ok(gl_get_flick_input(f.c,&result));
    std::printf("stale_flick: fresh Steam available=%u, resolved available=%u x=%.1f\n",steam.available,result.available,result.stick_x);
}
static void c_exception(){
    Fixture f;bool escaped=false;
    // This read-only API must parse a context-prefixed long ID without allocating.
    double value{};fail_allocation=true;
    try{(void)gl_setting_get(f.c,"context.1.flick.touchpad_release_threshold",&value);}
    catch(const std::bad_alloc&){escaped=true;}
    fail_allocation=false;
    std::printf("c_exception: bad_alloc crossed gl_setting_get = %s\n",escaped?"YES":"no");
}
static void hidden_tightening(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);
    ok(gl_setting_set(f.c,"context.1.gyro.space",GL_SPACE_LOCAL_YAW));
    ok(gl_setting_set(f.c,"context.1.sensitivity_x",1));
    ok(gl_setting_set(f.c,"context.1.gyro.smoothing_ms",50));
    ok(gl_setting_set(f.c,"context.1.gyro.tightening_dps",10));
    ok(gl_setting_set(f.c,"context.1.gyro.smoothing_ms",0));
    gl_output out{};
    for(int n=0;n<5;++n){f.controls(1);const gl_sample sample{f.now,f.now,{0,5,0},{0,1,0}};
        ok(gl_submit_sample(f.c,1,&sample));ok(gl_set_gameplay_context_state(f.c,1,1,1));
        ok(gl_update(f.c,f.now,&f.host,&out));f.now+=10000000;}
    unsigned visible=99;
    for(uint32_t n=0;n<gl_menu_tab_setting_count(f.c,2);++n){gl_setting_info info{};ok(gl_menu_tab_setting_at(f.c,2,n,&info));
        if(!std::strcmp(info.id,"context.1.gyro.tightening_dps"))visible=info.visible;}
    std::printf("hidden_tightening: smoothing=0 visible=%u yaw=%.6f (unmodified -0.050000)\n",visible,out.yaw_degrees);
}
static void core_cost(){
    Fixture f;f.device(1,GL_SOURCE_SDL,1);
    for(uint32_t id=2;id<=6;++id){const gl_gameplay_context view{id,"Audit view","",0};ok(gl_register_gameplay_context(f.c,&view));}
    const auto before=allocations;const auto start=std::chrono::steady_clock::now();
    constexpr int frames=10000;
    for(int n=0;n<frames;++n){f.controls(1);const gl_sample sample{f.now,f.now,{0,5,0},{0,1,0}};ok(gl_submit_sample(f.c,1,&sample));f.tick();}
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/frames;
    std::printf("core_cost: six views, no UI/SDL/I-O, %.2f us/frame, %.2f new calls/frame\n",us,double(allocations-before)/frames);
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);event_id();contact_migration();stale_flick();hidden_tightening();c_exception();core_cost();}
