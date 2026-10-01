#include "detail/internal.hpp"
using namespace gyrolib;
ButtonContact gl_context::button_contact(uint32_t button) const {
    if(button>=32)return {};
    const auto* physical=button_companion();
    for(unsigned source:{GL_SOURCE_SDL,GL_SOURCE_STEAM})for(const auto& e:endpoints){
        if((physical&&physical!=&e)||!e.info.connected||e.info.physical_id!=selected||e.info.source!=source||
           !(e.info.caps.buttons&(1u<<button))||!e.controls.timestamp_ns||e.controls.timestamp_ns>now||now-e.controls.timestamp_ns>=150000000)continue;
        const auto alias=e.button_contacts[button];const auto& caps=e.info.caps;
        const auto available=alias.family==GL_CONTACT_TOUCHPAD?caps.touchpads:alias.family==GL_CONTACT_STICK?caps.stick_touch:
            alias.family==GL_CONTACT_GRIP?caps.grip_touch:0;
        if(available&alias.side)return alias;
    }
    return {};
}
extern "C" {
int32_t GL_CALL gl_set_button_contact(gl_context* c,uint64_t id,uint32_t button,uint32_t family,uint32_t side){
    if(!c||button>=32||family>GL_CONTACT_GRIP||
       (family!=GL_CONTACT_NONE&&side!=GL_LEFT&&side!=GL_RIGHT&&side!=GL_SINGLE))return GL_INVALID;
    auto* e=c->endpoint(id);if(!e)return GL_INVALID;
    auto& alias=e->button_contacts[button];if(!family)side=0;
    if(alias.family!=family||alias.side!=side){alias={family,side};c->emit(GL_EVENT_BUTTON_LABELS,static_cast<int>(button),id);}
    return GL_OK;
}
int32_t GL_CALL gl_set_button_label(gl_context* c,uint64_t id,uint32_t button,const char* label,uint32_t provenance) try {
    if(!c||button>=32||(provenance!=GL_LABEL_DEVICE&&provenance!=GL_LABEL_PHYSICAL))return GL_INVALID;
    auto* endpoint=c->endpoint(id);if(!endpoint)return GL_INVALID;
    std::string name=label?label:"";
    if(name.empty()&&endpoint->button_labels[button].empty()){endpoint->label_provenance[button]=provenance;return GL_OK;}
    if(endpoint->button_labels[button]==name&&endpoint->label_provenance[button]==provenance)return GL_OK;
    endpoint->button_labels[button]=std::move(name);endpoint->label_provenance[button]=provenance;
    c->emit(GL_EVENT_BUTTON_LABELS,static_cast<int>(button),id);return GL_OK;
}catch(...){return GL_LIMIT;}
const char* GL_CALL gl_get_button_label(const gl_context* c,uint32_t button) {
    if(!c||button>=32)return "?";
    const EndpointState* chosen=nullptr;
    const auto* physical=c->button_companion();
    for(const auto& e:c->endpoints)if((!physical||physical==&e)&&e.info.connected&&e.info.physical_id==c->selected&&!e.button_labels[button].empty()) {
        if(!chosen || e.label_provenance[button]>chosen->label_provenance[button] ||
           (e.label_provenance[button]==chosen->label_provenance[button]&&
            (e.info.source<chosen->info.source||(e.info.source==chosen->info.source&&e.info.id<chosen->info.id))))chosen=&e;
    }
    if(chosen)return chosen->button_labels[button].c_str();
    const char* keys[]={"button.south","button.east","button.west","button.north","button.back","button.guide","button.start",
        "button.left_click","button.right_click","button.left_shoulder","button.right_shoulder","button.up","button.down","button.left","button.right",
        "button.misc1","button.right_rear1","button.left_rear1","button.right_rear2","button.left_rear2","button.touchpad",
        "button.misc2","button.misc3","button.misc4","button.misc5","button.misc6","button.26","button.27","button.28","button.29","button.30","button.31"};
    return gl_text(c,keys[button]);
}
}
