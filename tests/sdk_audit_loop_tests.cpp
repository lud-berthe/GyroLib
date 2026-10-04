#include <gyrolib/gyrolib.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <fstream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
struct Context {
    gl_context* c=gl_create(GL_ABI_VERSION);
    Context(){CHECK(c);for(uint32_t id:{1u,2u,3u}){gl_gameplay_context view{id,"View","",0};CHECK(gl_register_gameplay_context(c,&view)==GL_OK);}}
    ~Context(){gl_destroy(c);}
    void set(const std::string& key,double v){CHECK(gl_setting_set(c,key.c_str(),v)==GL_OK);}
    double get(const std::string& key){double v{};CHECK(gl_setting_get(c,key.c_str(),&v)==GL_OK);return v;}
    void device(){gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.sticks=GL_RIGHT;
        CHECK(gl_register_endpoint(c,&e)==GL_OK);CHECK(gl_select_device(c,1)==GL_OK);}
    unsigned drain(uint32_t type=0){gl_event_ex e{};unsigned count=0;while(gl_poll_event_ex(c,&e)>0)if(!type||e.type==type)++count;return count;}
};
static void labels(){
    Context f;f.device();f.drain();
    for(uint32_t side:{uint32_t(GL_LEFT),uint32_t(GL_RIGHT)}){
        CHECK(gl_set_trigger_label(f.c,1,side,"Trigger")==GL_OK);
        CHECK(std::strcmp(gl_get_trigger_label(f.c,side),"Trigger")==0);
        gl_event_ex event{};CHECK(gl_poll_event_ex(f.c,&event)==1);
        CHECK(event.type==GL_EVENT_BUTTON_LABELS&&event.detail==-1&&event.endpoint_id==1);CHECK(f.drain()==0);
        CHECK(gl_set_trigger_label(f.c,1,side,"Trigger")==GL_OK);CHECK(f.drain()==0);
        CHECK(gl_set_trigger_label(f.c,1,side,nullptr)==GL_OK);CHECK(f.drain(GL_EVENT_BUTTON_LABELS)==1);
        CHECK(gl_set_trigger_label(f.c,1,side,"")==GL_OK);CHECK(f.drain()==0);
    }
    CHECK(gl_set_trigger_label(f.c,1,GL_LEFT,"Keep")==GL_OK);f.drain();
    CHECK(gl_set_trigger_label(f.c,1,GL_LEFT,std::string(128,'x').c_str())==GL_INVALID);
    CHECK(gl_set_trigger_label(f.c,2,GL_LEFT,"Invalid")==GL_INVALID);
    CHECK(gl_set_trigger_label(f.c,1,0,"Invalid")==GL_INVALID);
    CHECK(std::strcmp(gl_get_trigger_label(f.c,GL_LEFT),"Keep")==0);CHECK(f.drain()==0);
}
static double pivot(bool change_global,bool explicit_view){
    Context f;f.device();CHECK(gl_set_host_capabilities(f.c,GL_HOST_NATIVE_STICK_SUPPRESSION)==GL_OK);
    if(explicit_view)CHECK(gl_set_gameplay_context_output_target(f.c,1,GL_OUTPUT_CAMERA)==GL_OK);
    f.set("context.1.flick.mode",GL_FLICK_ON);f.set("context.1.flick.duration_ms",100);
    f.set("context.1.flick.smoothing_ms",0);uint64_t now=1000000000;double yaw=0;
    const auto tick=[&](float x){gl_flick_input input{now,GL_FLICK_INPUT_STICK,0,x,0,0,0};
        CHECK(gl_submit_flick_input(f.c,1,&input)==GL_OK);CHECK(gl_set_gameplay_context_state(f.c,1,1,1)==GL_OK);
        gl_host_state h{};h.focused=h.camera_allowed=1;gl_output o{};CHECK(gl_update(f.c,now,&h,&o)==GL_OK);
        now+=10000000;yaw+=o.yaw_degrees;};
    tick(0);tick(1);if(change_global)CHECK(gl_set_output_target(f.c,GL_OUTPUT_CURSOR)==GL_OK);
    for(unsigned n=0;n<12;++n)tick(1);return yaw;
}
static void destination(){
    const auto baseline=pivot(false,true),changed=pivot(true,true),following=pivot(true,false);
    std::printf("pivot: baseline=%.6f explicit_with_global_change=%.6f following_default=%.6f\n",baseline,changed,following);
    CHECK(std::abs(baseline-90)<1e-4);CHECK(std::abs(changed-baseline)<1e-6);
    CHECK(following<baseline); // The undeclared view really switches to a suspended cursor.
}
struct Field {std::string suffix;double min,max,step;};
static std::string key(unsigned id,const Field& f){return "context."+std::to_string(id)+"."+f.suffix;}
static std::vector<Field> fields(Context& f){
    const char* retired[]={"gyro.enabled","gyro.context","flick.context","ui.scale","flick.smoothing_threshold_degrees",
        "gyro.temporary_invert_button","gyro.trackball_button","gyro.look_stick_effect"};
    std::vector<Field> result;
    for(uint32_t i=0;i<gl_menu_tab_setting_count(f.c,2);++i){gl_setting_info s{};CHECK(gl_menu_tab_setting_at(f.c,2,i,&s)==GL_OK);
        const std::string suffix=std::string(s.id).substr(std::strlen("context.1."));
        if(std::find(std::begin(retired),std::end(retired),suffix)!=std::end(retired))continue;
        result.push_back({suffix,s.minimum,s.maximum,s.step});
    }return result;
}
static void compare(Context& a,Context& b,const std::vector<Field>& fs){
    for(unsigned view=1;view<=3;++view){CHECK(gl_get_context_parent(a.c,view)==gl_get_context_parent(b.c,view));
        for(const auto& f:fs){const auto k=key(view,f);const auto x=a.get(k),y=b.get(k);
            if(std::abs(x-y)>1e-8)throw std::runtime_error(k+" changed from "+std::to_string(x)+" to "+std::to_string(y));
            gl_setting_inheritance_info ia{},ib{};CHECK(gl_setting_inheritance(a.c,k.c_str(),&ia)==GL_OK);
            CHECK(gl_setting_inheritance(b.c,k.c_str(),&ib)==GL_OK);
            if(ia.overridden!=ib.overridden||ia.source_context!=ib.source_context)
                throw std::runtime_error(k+" inheritance changed: override "+std::to_string(ia.overridden)+" -> "+std::to_string(ib.overridden)+
                    ", source "+std::to_string(ia.source_context)+" -> "+std::to_string(ib.source_context));
        }
    }
}
static void persistence(){
    // A local preset plus a component restored to inheritance must survive a
    // restart, including equal values and later parent edits through a chain.
    const auto path=std::filesystem::absolute("sdk-loop-inherited.ini");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
    for(int preset=0;preset<4;++preset)for(const char* suffix:{"gyro.fast_sensitivity_x","gyro.fast_sensitivity_y","gyro.slow_threshold_dps","gyro.fast_threshold_dps"}){
        Context original;const auto fs=fields(original);
        original.set("context.1.gyro.acceleration",1);
        CHECK(gl_set_context_parent(original.c,2,1)==GL_OK);CHECK(gl_set_context_parent(original.c,3,2)==GL_OK);
        original.set("context.2.gyro.acceleration",preset);
        const auto setting=std::string("context.2.")+suffix;
        CHECK(gl_setting_inherit(original.c,setting.c_str())==GL_OK);
        CHECK(gl_save_settings(original.c,path.string().c_str())==GL_OK);
        Context restored;CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_OK);compare(original,restored,fs);
        const auto parent=std::string("context.1.")+suffix;
        original.set(parent,original.get(parent)+1);restored.set(parent,restored.get(parent)+1);compare(original,restored,fs);
    }
    Context invalid;invalid.set("context.1.sensitivity_x",6);invalid.drain();
    for(const char* text:{"context.1.sensitivity_x=inherit\n","context.1.inherit=0\ncontext.1.sensitivity_x=inherit\n",
        "context.2.inherit=1\ncontext.2.sensitivity_x=inherit\ncontext.2.sensitivity_x=3\n",
        "context.2.inherit=1\ncontext.2.sensitivity_x=inheritx\n"}){
        {std::ofstream out(path);out<<"schema=0.2.0\n"<<text;}
        CHECK(gl_load_settings(invalid.c,path.string().c_str())==GL_INVALID);
        CHECK(invalid.get("context.1.sensitivity_x")==6);CHECK(gl_get_context_parent(invalid.c,2)==0);CHECK(invalid.drain()==0);
    }
    for(unsigned seed:{1u,42u,1009u,98765u}){
        Context f;const auto fs=fields(f);unsigned random=seed;
        const auto next=[&]{random=random*1664525u+1013904223u;return random>>8;};
        const auto path=std::filesystem::absolute("sdk-loop-"+std::to_string(seed)+".ini");
        struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
        for(unsigned step=0;step<200;++step){
            const auto view=1+next()%3,action=next()%10;const auto& field=fs[next()%fs.size()];
            const auto k=key(view,field);
            try{
                if(action<7){
                    double value=field.min+(next()%(1+unsigned(std::llround((field.max-field.min)/field.step))))*field.step;
                    value=std::clamp(value,field.min,field.max);
                    if(field.suffix=="gyro.activation"){const double choices[]={GL_ALWAYS,GL_HOLD,GL_TOGGLE,GL_HOLD_DISABLE,GL_GYRO_OFF};value=choices[next()%5];}
                    if(field.suffix=="flick.mode"){const double choices[]={GL_FLICK_OFF,GL_FLICK_ON,GL_FLICK_TOUCHPAD,GL_FLICK_BOTH};value=choices[next()%4];}
                    f.set(k,value);
                }else if(action==7){const auto parent=next()%4;if(gl_can_inherit_context(f.c,view,parent))CHECK(gl_set_context_parent(f.c,view,parent)==GL_OK);}
                else if(action==8){const int r=gl_setting_inherit(f.c,k.c_str());CHECK(r==GL_OK||r==GL_UNAVAILABLE);}
                else{
                    CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);Context before;CHECK(gl_load_settings(before.c,path.string().c_str())==GL_OK);
                    CHECK(gl_capture_recommended_settings(f.c)==GL_OK);gl_reset_settings(f.c);
                    CHECK(gl_apply_recommended_settings(f.c)==GL_OK);compare(f,before,fs);
                }
                CHECK(gl_save_settings(f.c,path.string().c_str())==GL_OK);Context restored;
                CHECK(gl_load_settings(restored.c,path.string().c_str())==GL_OK);compare(f,restored,fs);
            }catch(const std::exception& e){throw std::runtime_error("seed="+std::to_string(seed)+" step="+std::to_string(step)+" key="+k+" action="+std::to_string(action)+": "+e.what());}
        }
        std::printf("persistence seed=%u fields=%zu operations=200 passed\n",seed,fs.size());
    }
}
int main(int argc,char** argv)try{
    CHECK(argc==2);if(!std::strcmp(argv[1],"labels"))labels();else if(!std::strcmp(argv[1],"destination"))destination();
    else if(!std::strcmp(argv[1],"persistence"))persistence();else return 2;return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
