#include "gyrolib/steam.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>
struct SteamPad {uint64_t handle;gl_endpoint info;};
struct gl_steam {gl_context* context;gl_steam_provider provider;std::vector<SteamPad> pads;uint64_t frame{},next_id{1};bool primed{};
    gl_steam_button_label_callback labels{};void* labels_user{};};
extern "C" {
gl_steam* GL_CALL gl_steam_create(gl_context* c,const gl_steam_provider* p) try {
    if(!c||!p||!p->handles||!p->motion)return nullptr;
    return new(std::nothrow) gl_steam{c,*p,{}};
}catch(...){return nullptr;}
void GL_CALL gl_steam_destroy(gl_steam* s){if(s){for(auto& p:s->pads){gl_disconnect_endpoint(s->context,p.info.id);gl_forget_endpoint(s->context,p.info.id);}delete s;}}
int32_t GL_CALL gl_steam_set_button_label_provider(gl_steam* s,gl_steam_button_label_callback callback,void* user) {
    if(!s)return GL_INVALID;s->labels=callback;s->labels_user=user;
    // Clear the prior provider immediately; an absent provider cannot retain names.
    for(const auto& p:s->pads)for(uint32_t b=0;b<32;++b)gl_set_button_label(s->context,p.info.id,b,nullptr,GL_LABEL_PHYSICAL);
    return GL_OK;
}
int32_t GL_CALL gl_steam_poll(gl_steam* s,uint64_t now,uint64_t frame) try {
    if(!s||!now)return GL_INVALID;
    if(s->primed&&frame<=s->frame)return GL_INVALID;s->primed=true;s->frame=frame;
    std::array<uint64_t,16> handles{};
    auto count=s->provider.handles(s->provider.user,handles.data(),static_cast<uint32_t>(handles.size()));
    if(count>handles.size())return GL_LIMIT;
    // Validate the complete list before removing or creating endpoints. A bad
    // provider frame must not disconnect a controller that is still present.
    for(unsigned i=0;i<count;++i)
        if(!handles[i]||std::find(handles.begin(),handles.begin()+i,handles[i])!=handles.begin()+i)return GL_INVALID;
    for(auto i=s->pads.begin();i!=s->pads.end();) {
        if(std::find(handles.begin(),handles.begin()+count,i->handle)==handles.begin()+count) {
            gl_disconnect_endpoint(s->context,i->info.id);gl_forget_endpoint(s->context,i->info.id);i=s->pads.erase(i);
        }else ++i;
    }
    int poll_result=GL_OK;
    for(unsigned i=0;i<count;++i) {
        auto handle=handles[i];
        auto p=std::find_if(s->pads.begin(),s->pads.end(),[&](auto& p){return p.handle==handle;});
        if(p==s->pads.end()) {
            if(s->pads.size()>=16)return GL_LIMIT;
            gl_endpoint info{};info.id=0x2000000000000000ull|s->next_id++;info.physical_id=handle;
            info.source=GL_SOURCE_STEAM;std::strcpy(info.name,"Steam Input controller");
            s->pads.push_back({handle,info});p=std::prev(s->pads.end());
        }
        gl_steam_motion motion{};bool valid=s->provider.motion(s->provider.user,handle,&motion)!=0;
        bool finite=true;
        for(float v:{motion.accel_x,motion.accel_y,motion.accel_z,motion.pitch,motion.roll,motion.yaw})finite&=std::isfinite(v)&&std::abs(v)<=32768;
        if(valid && !finite)poll_result=GL_INVALID;
        valid&=finite;
        gl_sample sample{now,now,{motion.pitch*2000/32768,motion.yaw*2000/32768,motion.roll*2000/32768},
            {motion.accel_x*2/32768,motion.accel_z*2/32768,-motion.accel_y*2/32768}};
        valid&=std::hypot(sample.accel_g.x,sample.accel_g.y,sample.accel_g.z)>0.05;
        bool previously_capable=p->info.connected&&p->info.caps.gyro;
        p->info.connected=1;p->info.caps={};gl_controls controls{};
        bool have_controls=s->provider.controls&&s->provider.controls(s->provider.user,handle,&p->info.caps,&controls);
        p->info.caps.gyro=p->info.caps.accelerometer=valid||previously_capable;
        // Association is host-owned and may have changed since the previous poll.
        for(uint32_t n=0;n<gl_endpoint_count(s->context);++n){gl_endpoint known{};gl_get_endpoint(s->context,n,&known);
            if(known.id==p->info.id){p->info.physical_id=known.physical_id;break;}}
        int result=gl_register_endpoint(s->context,&p->info);if(result!=GL_OK)return result;
        if(s->labels)for(uint32_t b=0;b<32;++b) {
            result=gl_set_button_label(s->context,p->info.id,b,s->labels(s->labels_user,handle,b),GL_LABEL_PHYSICAL);
            if(result!=GL_OK)return result;
        }
        if(have_controls){
            controls.timestamp_ns=now;result=gl_submit_controls(s->context,p->info.id,&controls);
            if(result!=GL_OK)return result;
        }
        if(valid){result=gl_submit_sample(s->context,p->info.id,&sample);if(result!=GL_OK)return result;}
    }
    return poll_result;
}catch(...){return GL_LIMIT;}
uint64_t GL_CALL gl_steam_endpoint_for_handle(const gl_steam* s,uint64_t handle) {
    if(s)for(auto& p:s->pads)if(p.handle==handle)return p.info.id;return 0;
}
}
