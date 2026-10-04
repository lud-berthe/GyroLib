#include "detail/internal.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
using namespace gyrolib;
namespace gyrolib {
bool valid_links(const Profiles& profiles,const ProfileLinks& links){
    for(const auto& [id,link]:links){
        if(!profiles.count(id)||!link.parent)return false;
        uint32_t current=id;size_t steps=0;
        for(;;){auto it=links.find(current);if(it==links.end())break;
            current=it->second.parent;
            if(!profiles.count(current)||++steps>links.size())return false;
        }
    }return true;
}
Settings resolve_profile(const Profiles& profiles,const ProfileLinks& links,uint32_t id){
    auto result=defaults();std::bitset<SettingCount> resolved;size_t steps=0;
    std::array<size_t,SettingCount> depth{};depth.fill(SIZE_MAX);
    while(id){
        auto values=profiles.find(id);if(values==profiles.end())break;
        auto link=links.find(id);
        for(int f=0;f<SettingCount;++f)if(!resolved[f]&&(link==links.end()||link->second.overrides[f])){
            result[f]=values->second[f];resolved.set(f);depth[f]=steps;
        }
        if(link==links.end()||++steps>links.size())break;
        id=link->second.parent;
    }
    result[Enabled]=int(result[Activation])!=GL_GYRO_OFF;
    // A local threshold and a subsequently edited parent must still form a valid
    // interval. Resolve the release edge, without rewriting either saved profile.
    result[FlickStickOuter]=std::max(result[FlickStickOuter],result[FlickStickInner]+.05);
    result[FlickPadOuter]=std::max(result[FlickPadOuter],result[FlickPadInner]+.05);
    // Off stays Off. A composed active curve reports Custom when its components
    // no longer match the inherited preset; it never replaces those components.
    const int preset=int(result[Acceleration]);
    if(preset==0){result[FastSensX]=result[SensX];result[FastSensY]=result[SensY];result[SlowSpeed]=5;result[FastSpeed]=75;}
    if(preset>0&&preset<4){
        constexpr double gains[]={1,1.5,2,3};
        // A more local base sensitivity derives its preset's fast value, unless
        // the curve component itself has an equally local explicit override.
        if(depth[SensX]<depth[FastSensX])result[FastSensX]=result[SensX]*gains[preset];
        if(depth[SensY]<depth[FastSensY])result[FastSensY]=result[SensY]*gains[preset];
        if(std::abs(result[FastSensX]-result[SensX]*gains[preset])>1e-8||
           std::abs(result[FastSensY]-result[SensY]*gains[preset])>1e-8||result[SlowSpeed]!=5||result[FastSpeed]!=75)
            result[Acceleration]=4;
    }
    return result;
}
}
Profiles gl_context::profile_snapshot()const{
    Profiles out;for(const auto& [id,state]:gameplay_contexts)out[id]=effective_settings(id);return out;
}
void gl_context::profiles_changed(const Profiles& previous){
    bool reset_controls=false;const auto active_view=winning_context();
    for(const auto& [id,state]:gameplay_contexts){
        auto it=previous.find(id);if(it==previous.end())continue;const auto current=effective_settings(id);
        for(int f=0;f<SettingCount;++f)if(profile_setting(f)&&current[f]!=it->second[f]){
            emit(GL_EVENT_SETTING,f,0,current[f],state.setting_ids[f].c_str());
            if(id==active_view){
                if(f==RecenterButton)recenter_primed=false;
                if(f==Activation||(f>=Button&&f<=BlockLongPress)||f==Trigger||f==TriggerThreshold)reset_controls=true;
            }
        }
    }
    if(reset_controls){for(auto& gate:long_press_blockers)gate.cancel();controls_primed=false;stick_gate.reset();trigger_gate.reset();}
}
int gl_context::save_change(){return settings_batch_depth||settings_path.empty()?GL_OK:gl_save_settings(this,settings_path.c_str());}
namespace {
bool identify(const gl_context* c,const char* name,uint32_t& view,int& field){
    if(!c||!name)return false;std::string_view key(name);if(!key.starts_with("context."))return false;
    key.remove_prefix(8);const auto dot=key.find('.');if(dot==std::string_view::npos)return false;
    const auto parsed=std::from_chars(key.data(),key.data()+dot,view);
    if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+dot||!view||!c->gameplay_contexts.count(view))return false;
    const auto suffix=key.substr(dot+1);field=suffix=="sensitivity_x"?SensX:suffix=="sensitivity_y"?SensY:setting_index(suffix.data());
    if(field==Enabled)field=Activation;
    return profile_setting(field);
}
}
extern "C" {
uint32_t GL_CALL gl_get_context_parent(const gl_context* c,uint32_t view){
    if(!c)return 0;auto it=c->profile_links.find(view);return it==c->profile_links.end()?0:it->second.parent;
}
uint32_t GL_CALL gl_can_inherit_context(const gl_context* c,uint32_t view,uint32_t parent){
    if(!c||!c->gameplay_contexts.count(view))return 0;if(!parent)return 1;
    if(!c->gameplay_contexts.count(parent))return 0;size_t steps=0;
    for(auto current=parent;current;current=gl_get_context_parent(c,current))
        if(current==view||++steps>c->profile_links.size()+1)return 0;
    return 1;
}
int32_t GL_CALL gl_set_context_parent(gl_context* c,uint32_t view,uint32_t parent)try{
    if(!gl_can_inherit_context(c,view,parent))return GL_INVALID;
    const auto old=gl_get_context_parent(c,view);if(old==parent)return GL_OK;
    const auto before=c->profile_snapshot();
    if(!parent){c->context_settings[view]=c->effective_settings(view);c->profile_links.erase(view);}
    else c->profile_links[view].parent=parent;
    c->profiles_changed(before);c->emit(GL_EVENT_CONTEXT,6,0,view);return c->save_change();
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_setting_inheritance(const gl_context* c,const char* key,gl_setting_inheritance_info* out)try{
    uint32_t view;int field;if(!out||!identify(c,key,view,field))return GL_INVALID;
    *out={};out->source_context=view;out->parent_context=gl_get_context_parent(c,view);
    if(!out->parent_context)return GL_OK;
    out->overridden=c->profile_links.at(view).overrides[field]?1u:0u;
    out->parent_value=c->effective_settings(out->parent_context)[field];
    size_t steps=0;auto source=view;
    for(;;){auto it=c->profile_links.find(source);if(it==c->profile_links.end())break;
        if(it->second.overrides[field]||++steps>c->profile_links.size())break;source=it->second.parent;
    }
    out->source_context=source;return GL_OK;
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_setting_inherit(gl_context* c,const char* key)try{
    uint32_t view;int field;if(!identify(c,key,view,field))return GL_INVALID;
    auto link=c->profile_links.find(view);if(link==c->profile_links.end())return GL_UNAVAILABLE;
    const auto before=c->profile_snapshot();auto& mask=link->second.overrides;mask.reset(field);
    if(field==Acceleration)for(int f=FastSensX;f<=FastSpeed;++f)mask.reset(f);
    if(field>=FastSensX&&field<=FastSpeed&&c->context_settings.at(view)[Acceleration]==4){
        bool any=false;for(int f=FastSensX;f<=FastSpeed;++f)any|=mask[f];if(!any)mask.reset(Acceleration);
    }
    c->profiles_changed(before);c->emit(GL_EVENT_CONTEXT,6,0,view);return c->save_change();
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_capture_recommended_settings(gl_context* c)try{
    if(!c)return GL_INVALID;if(c->gameplay_contexts.empty())return GL_UNAVAILABLE;
    c->recommended=RecommendedSettings{c->context_settings,c->profile_links,c->settings[AutoCal]};
    c->emit(GL_EVENT_CONTEXT,7);return GL_OK;
}catch(...){return GL_LIMIT;}
uint32_t GL_CALL gl_has_recommended_settings(const gl_context* c){return c&&c->recommended.has_value();}
int32_t GL_CALL gl_apply_recommended_settings(gl_context* c)try{
    if(!c)return GL_INVALID;if(!c->recommended)return GL_UNAVAILABLE;
    const auto before=c->profile_snapshot();
    for(const auto& [id,settings]:c->recommended->profiles){c->context_settings[id]=settings;c->profile_links.erase(id);}
    for(const auto& [id,link]:c->recommended->links)c->profile_links[id]=link;
    c->settings[AutoCal]=c->recommended->calibration;c->profiles_changed(before);
    c->emit(GL_EVENT_SETTING,AutoCal,0,c->settings[AutoCal],"calibration.automatic");
    c->emit(GL_EVENT_CONTEXT,6);return c->save_change();
}catch(...){return GL_LIMIT;}
}
