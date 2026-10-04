// Audit D: public API observations, synthetic input and a disposable INI only.
#include <gyrolib/gyrolib.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
static void ok(int r){if(r!=GL_OK){std::printf("setup failed %d\n",r);std::exit(2);}}
struct Context {
    gl_context* c=gl_create(GL_ABI_VERSION);
    Context(){if(!c)std::exit(2);for(unsigned id=1;id<=3;++id){gl_gameplay_context v{id,"View","",0};ok(gl_register_gameplay_context(c,&v));}}
    ~Context(){gl_destroy(c);}
    static std::string key(unsigned id,const char* field){return "context."+std::to_string(id)+"."+field;}
    void set(unsigned id,const char* field,double x){ok(gl_setting_set(c,key(id,field).c_str(),x));}
    double get(unsigned id,const char* field){double x;ok(gl_setting_get(c,key(id,field).c_str(),&x));return x;}
};
static void output_target_notifications(){
    Context f;uint64_t now=1000000000;
    gl_endpoint e{};e.id=e.physical_id=1;e.connected=1;e.source=GL_SOURCE_SDL;e.caps.sticks=GL_RIGHT;
    ok(gl_register_endpoint(f.c,&e));ok(gl_select_device(f.c,1));
    ok(gl_set_host_capabilities(f.c,GL_HOST_MENU_STATE|GL_HOST_NATIVE_STICK_SUPPRESSION));
    const auto tick=[&]{gl_controls input{};input.timestamp_ns=now;ok(gl_submit_controls(f.c,1,&input));
        ok(gl_set_gameplay_context_state(f.c,1,1,1));gl_host_state h{};h.focused=h.camera_allowed=1;
        gl_output o{};ok(gl_update(f.c,now,&h,&o));now+=10000000;};
    const auto visibility=[&]{for(uint32_t n=0;n<gl_menu_tab_setting_count(f.c,2);++n){gl_setting_info s{};
        ok(gl_menu_tab_setting_at(f.c,2,n,&s));if(std::string(s.id)=="context.1.flick.mode")return s.visible;}std::exit(2);};
    tick();gl_event_ex event{};while(gl_poll_event_ex(f.c,&event)>0){}
    const auto before=visibility();ok(gl_set_output_target(f.c,GL_OUTPUT_CURSOR));tick();
    unsigned events=0;while(gl_poll_event_ex(f.c,&event)>0)++events;
    std::printf("global_output: flick_visible_before=%u after=%u events=%u\n",before,visibility(),events);
    ok(gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CAMERA));tick();
    events=0;while(gl_poll_event_ex(f.c,&event)>0)++events;
    std::printf("declared_output: flick_visible=%u events=%u\n",visibility(),events);
}
static void roundtrips(){
    const char* fields[]={"sensitivity_x","sensitivity_y","gyro.acceleration","gyro.fast_sensitivity_x","gyro.fast_sensitivity_y",
        "gyro.slow_threshold_dps","gyro.fast_threshold_dps","flick.stick_release_threshold","flick.stick_start_threshold"};
    const auto path=std::filesystem::absolute("audit4-roundtrip.ini");
    unsigned random=42,mismatches=0;std::vector<std::string> operations;Context f;
    for(unsigned n=0;n<600;++n){
        random=random*1664525u+1013904223u;const unsigned id=1+(random>>16)%3;
        random=random*1664525u+1013904223u;const auto action=(random>>16)%12;
        if(action<9){const unsigned limits[]={201,201,4,601,601,201,501,20,20};
            random=random*1664525u+1013904223u;double value=double((random>>16)%limits[action]);
            if(action<2||action==3||action==4)value/=10;
            if(action==7)value/=20;if(action==8)value=(value+1)/20;
            const auto key=Context::key(id,fields[action]);ok(gl_setting_set(f.c,key.c_str(),value));
            operations.push_back(key+"="+std::to_string(value));
        }else if(action==9){const unsigned parent=id>1?id-1:0;ok(gl_set_context_parent(f.c,id,parent));operations.push_back("parent "+std::to_string(id)+" -> "+std::to_string(parent));}
        else if(action==10){ok(gl_set_context_parent(f.c,id,0));operations.push_back("detach "+std::to_string(id));}
        else {gl_setting_inherit(f.c,Context::key(id,"gyro.acceleration").c_str());operations.push_back("inherit acceleration "+std::to_string(id));}
        ok(gl_save_settings(f.c,path.string().c_str()));Context restored;ok(gl_load_settings(restored.c,path.string().c_str()));
        for(unsigned view=1;view<=3;++view)for(const auto field:fields){
            const double before=f.get(view,field),after=restored.get(view,field);
            if(std::abs(before-after)>1e-8){
                std::printf("roundtrip mismatch step=%u view=%u field=%s before=%.9f after=%.9f\n",n,view,field,before,after);
                for(unsigned i=operations.size()>8?unsigned(operations.size()-8):0;i<operations.size();++i)std::printf("  %s\n",operations[i].c_str());
                if(++mismatches>=4){std::filesystem::remove(path);return;}
            }
        }
    }
    std::filesystem::remove(path);std::printf("roundtrip: 600 operations, mismatches=%u\n",mismatches);
}
int main(){output_target_notifications();roundtrips();}
