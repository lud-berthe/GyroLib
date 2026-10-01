#include "detail/internal.hpp"
#include <cmath>
using namespace gyrolib;

uint32_t gl_context::winning_context() const {
    const GameplayContext* selected_context=nullptr;uint32_t id=0;
    for(const auto& [key,value]:gameplay_contexts) {
        // Ordered map supplies the lowest stable ID for equal priorities.
        if(value.available&&value.active&&(!selected_context||value.priority>selected_context->priority)) {
            selected_context=&value;id=key;
        }
    }
    return id;
}
Settings gl_context::effective_settings(uint32_t id) const {
    auto it=context_settings.find(id);
    auto result=id&&it!=context_settings.end()?it->second:defaults();
    result[AutoCal]=settings[AutoCal];
    return result;
}
uint32_t gl_context::effective_output_target(uint32_t id) const {
    const auto it=gameplay_contexts.find(id);
    return it!=gameplay_contexts.end()&&it->second.output_target>=0?static_cast<uint32_t>(it->second.output_target):output_target;
}
extern "C" {
int32_t GL_CALL gl_set_host_capabilities(gl_context* c,uint32_t flags) try {
    if(!c||(flags&~(GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_SHORT_PRESS_FILTER|GL_HOST_MENU_STATE|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)))return GL_INVALID;
    if(flags==c->host_capabilities)return GL_OK;
    c->host_capabilities=flags;c->flick.reset();c->flick_touchpad.reset();c->suppress_touchpad=false;for(auto& g:c->gates)g.cancel();
    c->emit(GL_EVENT_HOST_CAPABILITIES,0,0,flags);return GL_OK;
}catch(...){return GL_LIMIT;}
uint32_t GL_CALL gl_get_host_capabilities(const gl_context* c){return c?c->host_capabilities:0;}
int32_t GL_CALL gl_register_gameplay_context(gl_context* c,const gl_gameplay_context* info) try {
    if(!c||!info||!info->id||!info->label||!*info->label)return GL_INVALID;
    auto it=c->gameplay_contexts.find(info->id);
    const char* description=info->description?info->description:"";
    if(it!=c->gameplay_contexts.end()&&it->second.label==info->label&&
       it->second.description==description&&it->second.priority==info->priority)return GL_OK;
    if(it==c->gameplay_contexts.end()&&c->gameplay_contexts.size()>=(UINT32_MAX-gl_setting_count())/SettingCount)return GL_LIMIT;
    GameplayContext pending;
    pending.label=info->label;pending.description=description;pending.priority=info->priority;
    if(it!=c->gameplay_contexts.end()) {
        pending.active=it->second.active;pending.available=it->second.available;pending.reported=it->second.reported;
        pending.activation_previous=it->second.activation_previous;
        pending.output_target=it->second.output_target;
        pending.zoom_available=it->second.zoom_available;
        pending.fov_reported=it->second.fov_reported;
        pending.fov=it->second.fov;pending.reference_fov=it->second.reference_fov;
    }
    for(int field=0;field<SettingCount;++field)if(profile_setting(field)) {
        pending.setting_ids[field]=profile_key(info->id,field);
        pending.setting_labels[field]=pending.label+" / "+translate(c->language,definitions[field].key);
        pending.setting_descriptions[field]=translate(c->language,definitions[field].description);
    }
    c->context_settings.try_emplace(info->id,defaults());
    c->gameplay_contexts.insert_or_assign(info->id,std::move(pending));
    c->emit(GL_EVENT_CONTEXT,1,0,info->id);return GL_OK;
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_unregister_gameplay_context(gl_context* c,uint32_t id) try {
    if(!c||!c->gameplay_contexts.erase(id))return GL_INVALID;
    c->emit(GL_EVENT_CONTEXT,-1,0,id);return GL_OK;
}catch(...){return GL_LIMIT;}
uint32_t GL_CALL gl_gameplay_context_count(const gl_context* c){return c?static_cast<uint32_t>(c->gameplay_contexts.size()):0;}
uint32_t GL_CALL gl_get_active_gameplay_context(const gl_context* c){return c?c->winning_context():0;}
int32_t GL_CALL gl_set_gameplay_context_output_target(gl_context* c,uint32_t id,uint32_t target) try {
    if(!c||target>GL_OUTPUT_CURSOR)return GL_INVALID;
    auto it=c->gameplay_contexts.find(id);if(it==c->gameplay_contexts.end())return GL_INVALID;
    if(it->second.output_target!=static_cast<int>(target)){
        it->second.output_target=static_cast<int>(target);c->emit(GL_EVENT_CONTEXT,3,0,id);
        if(id==c->winning_context())for(auto& gate:c->gates)gate.cancel();
    }
    return GL_OK;
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_get_gameplay_context(const gl_context* c,uint32_t index,gl_gameplay_context* out) {
    if(!c||!out||index>=c->gameplay_contexts.size())return GL_INVALID;
    const auto& [id,value]=*std::next(c->gameplay_contexts.begin(),index);
    *out={id,value.label.c_str(),value.description.c_str(),value.priority};return GL_OK;
}
int32_t GL_CALL gl_set_gameplay_context_zoom_available(gl_context* c,uint32_t id,uint32_t available){
    if(!c||available>1)return GL_INVALID;
    auto it=c->gameplay_contexts.find(id);if(it==c->gameplay_contexts.end())return GL_INVALID;
    if(it->second.zoom_available!=(available!=0)){
        it->second.zoom_available=available!=0;it->second.fov_reported=false;c->emit(GL_EVENT_CONTEXT,4,0,id);
    }
    return GL_OK;
}
int32_t GL_CALL gl_set_gameplay_context_fov(gl_context* c,uint32_t id,double current,double reference){
    if(!c)return GL_INVALID;
    auto it=c->gameplay_contexts.find(id);if(it==c->gameplay_contexts.end())return GL_INVALID;
    auto& view=it->second;view.fov_reported=false;
    if(!std::isfinite(current)||!std::isfinite(reference)||current<=0||reference<=0||current>=179||reference>=179)return GL_INVALID;
    if(!view.zoom_available)return GL_UNAVAILABLE;
    view.fov=current;view.reference_fov=reference;view.fov_reported=true;return GL_OK;
}
int32_t GL_CALL gl_set_gameplay_context_state(gl_context* c,uint32_t id,uint32_t active,uint32_t available) try {
    if(!c||active>1||available>1)return GL_INVALID;
    auto it=c->gameplay_contexts.find(id);if(it==c->gameplay_contexts.end())return GL_INVALID;
    auto& value=it->second;
    if(value.available!=(available!=0))c->emit(GL_EVENT_CONTEXT,0,0,id);
    value.active=active!=0;value.available=available!=0;value.reported=true;return GL_OK;
}catch(...){return GL_LIMIT;}
}
