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
    auto result=resolve_profile(context_settings,profile_links,id);
    result[AutoCal]=settings[AutoCal];
    return result;
}
Settings gl_context::device_settings(uint32_t id) const {
    auto result=effective_settings(id);
    const auto caps=capabilities();
    const auto side=[](double value,uint32_t available)->double {
        if(!value||!available)return GL_SIDE_OFF;
        // A central surface is a single control, regardless of the saved side.
        if(available==GL_SINGLE)return GL_SIDE_EITHER;
        if(value==double(GL_SIDE_EITHER)||value==double(GL_SIDE_BOTH)){
            if((available&3)==3)return value;
            return (available&GL_LEFT)?GL_SIDE_LEFT:GL_SIDE_RIGHT;
        }
        return side_available(available,int(value))?value:double(GL_SIDE_OFF);
    };
    result[Touchpad]=side(result[Touchpad],caps.touchpads);
    result[StickTouch]=side(result[StickTouch],caps.stick_touch);
    result[GripTouch]=side(result[GripTouch],caps.grip_touch);
    result[Stick]=side(result[Stick],caps.sticks);
    result[Trigger]=side(result[Trigger],trigger_inputs().available);
    const auto button=[&](double value)->double {
        const int choice=int(value);
        if(choice<=32)return button_matches(caps.buttons,choice)?value:0;
        // Old paired-button preferences follow the same fallback rule.
        constexpr unsigned pairs[][2]={{9,10},{7,8},{16,17},{18,19}};
        const auto& pair=pairs[(choice-33)/2];
        const bool left=(caps.buttons&(1u<<pair[0]))!=0,right=(caps.buttons&(1u<<pair[1]))!=0;
        return left&&right?value:left?double(pair[0]+1):right?double(pair[1]+1):0;
    };
    result[Button]=button(result[Button]);result[RecenterButton]=button(result[RecenterButton]);
    // A contact alias belongs to this device only. Never rewrite the stored
    // binding: the same normalized button can be physical on the next device.
    const auto binding=static_cast<uint32_t>(result[Button]);
    if(binding&&binding<=32){
        const auto alias=button_contact(binding-1);
        if(alias.family){
            const int field=alias.family==GL_CONTACT_TOUCHPAD?Touchpad:alias.family==GL_CONTACT_STICK?StickTouch:GripTouch;
            const uint32_t available=field==Touchpad?caps.touchpads:field==StickTouch?caps.stick_touch:caps.grip_touch;
            const int contact=static_cast<int>(side(alias.side==GL_LEFT?GL_SIDE_LEFT:alias.side==GL_RIGHT?GL_SIDE_RIGHT:GL_SIDE_EITHER,available));
            const int previous=static_cast<int>(result[field]);
            if(contact)result[field]=previous==GL_SIDE_OFF||previous==GL_SIDE_BOTH||previous==contact?contact:GL_SIDE_EITHER;
            result[Button]=0;
        }
    }
    const auto inputs=flick_inputs().available;
    const bool camera_target=effective_output_target(id)==GL_OUTPUT_CAMERA;
    const bool stick=camera_target&&(inputs&GL_FLICK_INPUT_STICK)&&(host_capabilities&GL_HOST_NATIVE_STICK_SUPPRESSION);
    const bool pad=camera_target&&(inputs&GL_FLICK_INPUT_TOUCHPAD)&&(host_capabilities&GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
    const int flick_mode=int(result[FlickMode]);
    result[FlickMode]=flick_mode==GL_FLICK_BOTH?(stick&&pad?GL_FLICK_BOTH:stick?GL_FLICK_ON:pad?GL_FLICK_TOUCHPAD:GL_FLICK_OFF):
        flick_mode==GL_FLICK_ON&&stick?GL_FLICK_ON:flick_mode==GL_FLICK_TOUCHPAD&&pad?GL_FLICK_TOUCHPAD:GL_FLICK_OFF;
    return result;
}
uint32_t gl_context::effective_output_target(uint32_t id) const {
    const auto it=gameplay_contexts.find(id);
    return it!=gameplay_contexts.end()&&it->second.output_target>=0?static_cast<uint32_t>(it->second.output_target):output_target;
}
extern "C" {
int32_t GL_CALL gl_set_host_capabilities(gl_context* c,uint32_t flags) try {
    if(!c||(flags&~(GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_LONG_PRESS_BLOCKING|GL_HOST_MENU_STATE|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)))return GL_INVALID;
    if(flags==c->host_capabilities)return GL_OK;
    c->host_capabilities=flags;c->flick.reset();c->flick_touchpad.reset();c->suppress_touchpad=false;for(auto& g:c->long_press_blockers)g.cancel();
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
        pending.camera_in_menu=it->second.camera_in_menu;
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
    if(!c||!c->gameplay_contexts.count(id))return GL_INVALID;
    bool detached=false;
    for(auto it=c->profile_links.begin();it!=c->profile_links.end();){
        if(it->second.parent!=id){++it;continue;}
        c->context_settings[it->first]=c->effective_settings(it->first);
        it=c->profile_links.erase(it);detached=true;
    }
    c->gameplay_contexts.erase(id);c->emit(GL_EVENT_CONTEXT,-1,0,id);
    if(detached){c->emit(GL_EVENT_CONTEXT,6);return c->save_change();}return GL_OK;
}catch(...){return GL_LIMIT;}
uint32_t GL_CALL gl_gameplay_context_count(const gl_context* c){return c?static_cast<uint32_t>(c->gameplay_contexts.size()):0;}
uint32_t GL_CALL gl_get_active_gameplay_context(const gl_context* c){return c?c->winning_context():0;}
int32_t GL_CALL gl_set_gameplay_context_output_target(gl_context* c,uint32_t id,uint32_t target) try {
    if(!c||target>GL_OUTPUT_CURSOR)return GL_INVALID;
    auto it=c->gameplay_contexts.find(id);if(it==c->gameplay_contexts.end())return GL_INVALID;
    if(it->second.output_target!=static_cast<int>(target)){
        it->second.output_target=static_cast<int>(target);c->emit(GL_EVENT_CONTEXT,3,0,id);
        if(id==c->winning_context())for(auto& gate:c->long_press_blockers)gate.cancel();
    }
    return GL_OK;
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_set_gameplay_context_camera_in_menu(gl_context* c,uint32_t id,uint32_t allowed) try {
    if(!c||allowed>1)return GL_INVALID;
    auto it=c->gameplay_contexts.find(id);if(it==c->gameplay_contexts.end())return GL_INVALID;
    if(it->second.camera_in_menu!=(allowed!=0)){
        it->second.camera_in_menu=allowed!=0;
        if(id==c->winning_context()){
            c->flick.reset();c->flick_touchpad.reset();c->suppress_touchpad=false;
            c->controls_primed=false;c->safe_previous=false;c->recenter_primed=false;
        }
        c->emit(GL_EVENT_CONTEXT,5,0,id);
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
    if(value.available!=(available!=0)||value.active!=(active!=0))c->emit(GL_EVENT_CONTEXT,0,0,id);
    value.active=active!=0;value.available=available!=0;value.reported=true;return GL_OK;
}catch(...){return GL_LIMIT;}
}
