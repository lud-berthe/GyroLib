#include "detail/internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
using namespace gyrolib;
namespace {
constexpr uint64_t fresh_ns=150000000,qualify_gap=100000000;
bool fresh(uint64_t t,uint64_t now) { return t && now>=t && now-t<fresh_ns; }
bool finite(gl_vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool healthy(const EndpointState& e,uint64_t now) {
    return e.info.connected && e.info.caps.gyro &&
        e.consecutive>=2 && fresh(e.last_arrival,now);
}
bool native_motion(const gl_context* c,uint64_t physical) {
    for(const auto& e:c->endpoints)if(!e.motion_companion&&e.info.physical_id==physical&&healthy(e,c->now))return true;
    return false;
}
const EndpointState* automatic_sensor(const gl_context* c,uint64_t physical) {
    if(!physical||c->manual_motion_groups.count(physical))return nullptr;
    uint32_t vendor=0;
    for(const auto& e:c->endpoints)if(e.info.connected&&!e.motion_companion&&e.info.physical_id==physical&&e.virtual_controller){
        if(vendor&&vendor!=e.pairing_vendor)return nullptr;
        vendor=e.pairing_vendor;
    }
    if(!vendor)return nullptr;
    const EndpointState* sensor=nullptr;
    for(const auto& e:c->endpoints)if(e.info.connected&&e.pairing_vendor==vendor){
        if(!e.motion_companion){if(e.info.physical_id!=physical)return nullptr;}
        else {
            // Count even silent candidates: a temporary stall must not disambiguate
            // two physical controllers or steal a sensor bound to another player.
            if(sensor||e.info.physical_id!=e.companion_identity)return nullptr;
            sensor=&e;
        }
    }
    return sensor;
}
}
EndpointState* gl_context::endpoint(uint64_t id) {
    for (auto& e:endpoints) if (e.info.id==id) return &e;
    return nullptr;
}
const EndpointState* gl_context::endpoint(uint64_t id) const {
    for (auto& e:endpoints) if (e.info.id==id) return &e;
    return nullptr;
}
void gl_context::emit(uint32_t type,int detail,uint64_t ep,double value,const char* setting) {
    gl_event e{}; e.type=type; e.detail=detail; e.endpoint_id=ep; e.value=value;
    if (setting) std::memcpy(e.setting_id,setting,std::min(std::strlen(setting),sizeof(e.setting_id)-1));
    if (events.size()==128) events.pop_front();
    events.push_back(e);
}
void gl_context::reset_temporal() {
    active=0; controls_primed=false; held_previous=false; previous_frame=0;
    recenter_primed=recenter_requested=false;
    safe_previous=false; flick.reset();flick_touchpad.reset();suppress_touchpad=false; for (auto& g:gates) g.cancel();
    gyro_state={};stick_gate.reset();trigger_gate.reset();flick_endpoint=flick_time=flick_report=0;
    modifiers=0;gyro_override=-1;
    for (auto& e:endpoints) { e.samples.clear();e.flick_samples.clear(); e.motion.resume(); e.motion.cancel(); }
}
const EndpointState* gl_context::button_companion() const {
    if(const auto* e=control_authority(GL_CONTROL_BUTTONS))return e;
    for(const auto& e:endpoints)if(e.info.connected&&e.motion_companion&&e.info.physical_id==selected&&
        e.info.physical_id!=e.companion_identity&&e.info.caps.buttons)return &e;
    return nullptr;
}
const EndpointState* gl_context::control_authority(uint32_t family) const {
    const EndpointState* chosen=nullptr;
    for(const auto& e:endpoints)if(e.info.connected&&e.info.physical_id==selected&&(e.control_authority&family)&&
        (!e.motion_companion||e.info.physical_id!=e.companion_identity)){
        if(!chosen||e.motion_companion>chosen->motion_companion||
            (e.motion_companion==chosen->motion_companion&&e.info.source<chosen->info.source))chosen=&e;
    }
    return chosen;
}
gl_capabilities gl_context::capabilities() const {
    gl_capabilities cap{};
    const auto* physical=button_companion();
    const auto* pads=control_authority(GL_CONTROL_TOUCHPADS);
    const auto* touch=control_authority(GL_CONTROL_STICK_TOUCH);
    const auto* grip=control_authority(GL_CONTROL_GRIP_TOUCH);
    const auto* sticks=control_authority(GL_CONTROL_STICKS);
    for (const auto& e:endpoints) if (e.info.connected && e.info.physical_id==selected) {
        // Do not offer controls from a stale supplemental endpoint.
        if (fresh(e.controls.timestamp_ns,now)) {
            if(!physical||physical==&e)cap.buttons|=e.info.caps.buttons;
            if(!pads||pads==&e)cap.touchpads|=e.info.caps.touchpads;
            if(!touch||touch==&e)cap.stick_touch|=e.info.caps.stick_touch;
            if(!grip||grip==&e)cap.grip_touch|=e.info.caps.grip_touch;
            if(!sticks||sticks==&e)cap.sticks|=e.info.caps.sticks;
        }
        if (healthy(e,now)) { cap.gyro|=e.info.caps.gyro;if(fresh(e.last_accel,now))cap.accelerometer|=e.info.caps.accelerometer; }
    }
    return cap;
}
const EndpointState* gl_context::flick_provider() const {
    const EndpointState* selected_input=nullptr;
    for(const auto& e:endpoints)if(e.info.connected&&e.info.physical_id==selected&&e.flick_input.timestamp_ns){
        if(!selected_input||e.motion_companion>selected_input->motion_companion||
           (e.motion_companion==selected_input->motion_companion&&e.info.source<selected_input->info.source))selected_input=&e;
    }
    if(selected_input)return selected_input;
    for(unsigned source:{GL_SOURCE_SDL,GL_SOURCE_STEAM})for(const auto& e:endpoints)
        if(e.info.connected&&e.info.physical_id==selected&&e.info.source==source&&
           (e.info.caps.sticks&GL_RIGHT)&&fresh(e.controls.timestamp_ns,now))
            return &e;
    return nullptr;
}
gl_flick_input gl_context::flick_inputs() const {
    const auto* e=flick_provider();if(!e)return {};
    if(e->flick_explicit)return fresh(e->flick_input.timestamp_ns,now)?e->flick_input:gl_flick_input{};
    return fresh(e->controls.timestamp_ns,now)?gl_flick_input{e->controls.timestamp_ns,GL_FLICK_INPUT_STICK,0,e->controls.right_x,e->controls.right_y,0,0}:gl_flick_input{};
}
gl_trigger_input gl_context::trigger_inputs() const {
    const auto* authority=control_authority(GL_CONTROL_TRIGGERS);gl_trigger_input result{};
    for(unsigned source:{GL_SOURCE_SDL,GL_SOURCE_STEAM})for(const auto& e:endpoints)
        if(e.info.connected&&e.info.physical_id==selected&&e.info.source==source&&(!authority||authority==&e)&&fresh(e.triggers.timestamp_ns,now)){
            if(!(result.available&GL_LEFT)&&(e.triggers.available&GL_LEFT))result.left=e.triggers.left;
            if(!(result.available&GL_RIGHT)&&(e.triggers.available&GL_RIGHT))result.right=e.triggers.right;
            result.available|=e.triggers.available;result.timestamp_ns=std::max(result.timestamp_ns,e.triggers.timestamp_ns);
        }
    return result;
}
extern "C" {
int32_t GL_CALL gl_get_gyro_state(const gl_context* c,gl_gyro_state* out){if(!c||!out)return GL_INVALID;*out=c->gyro_state;return GL_OK;}
int32_t GL_CALL gl_set_gyro_modifiers(gl_context* c,uint32_t flags){if(!c||(flags&~15u))return GL_INVALID;c->modifiers=flags;return GL_OK;}
int32_t GL_CALL gl_set_gyro_override(gl_context* c,int32_t value){if(!c||value< -1||value>1)return GL_INVALID;c->gyro_override=value;return GL_OK;}
int32_t GL_CALL gl_submit_trigger_input(gl_context* c,uint64_t id,const gl_trigger_input* input){
    auto* e=c?c->endpoint(id):nullptr;if(!e||!input||!input->timestamp_ns||(input->available&~3u))return GL_INVALID;
    if(!e->info.connected)return GL_UNAVAILABLE;
    if(!std::isfinite(input->left)||!std::isfinite(input->right)||input->left<0||input->left>1||input->right<0||input->right>1||input->timestamp_ns<e->triggers.timestamp_ns)return GL_INVALID;
    e->triggers=*input;return GL_OK;
}
int32_t GL_CALL gl_get_trigger_input(const gl_context* c,gl_trigger_input* input){if(!c||!input)return GL_INVALID;*input=c->trigger_inputs();return GL_OK;}
int32_t GL_CALL gl_set_trigger_label(gl_context* c,uint64_t id,uint32_t side,const char* label)try{
    auto* e=c?c->endpoint(id):nullptr;if(!e||(side!=GL_LEFT&&side!=GL_RIGHT)||(label&&std::strlen(label)>127))return GL_INVALID;
    e->trigger_labels[side==GL_RIGHT]=label?label:"";return GL_OK;
}catch(...){return GL_LIMIT;}
const char* GL_CALL gl_get_trigger_label(const gl_context* c,uint32_t side){
    if(!c||(side!=GL_LEFT&&side!=GL_RIGHT))return "";
    const auto* authority=c->control_authority(GL_CONTROL_TRIGGERS);
    for(unsigned source:{GL_SOURCE_SDL,GL_SOURCE_STEAM})for(const auto& e:c->endpoints)
        if(e.info.connected&&e.info.physical_id==c->selected&&e.info.source==source&&(!authority||authority==&e)&&!e.trigger_labels[side==GL_RIGHT].empty())return e.trigger_labels[side==GL_RIGHT].c_str();
    return gl_text(c,side==GL_LEFT?"trigger.left":"trigger.right");
}
int32_t GL_CALL gl_set_endpoint_control_authority(gl_context* c,uint64_t id,uint32_t families){
    if(!c||(families&~GL_CONTROL_ALL))return GL_INVALID;
    auto* e=c->endpoint(id);if(!e)return GL_INVALID;
    if(e->control_authority!=families){e->control_authority=families;c->emit(GL_EVENT_DEVICE,e->info.connected,id);}
    return GL_OK;
}
uint32_t GL_CALL gl_abi_version(void) { return GL_ABI_VERSION; }
gl_context* GL_CALL gl_create(uint32_t version) { try { return version==GL_ABI_VERSION ? new gl_context : nullptr; } catch (...) { return nullptr; } }
void GL_CALL gl_destroy(gl_context* c) { delete c; }
void GL_CALL gl_set_camera_callback(gl_context* c,gl_camera_callback f,void* user) { if(c){c->camera=f;c->camera_user=user;} }
void GL_CALL gl_set_sample_observer(gl_context* c,gl_sample_observer observer,void* user){
    if(c){c->sample_observer=observer;c->sample_observer_user=user;}
}
int32_t GL_CALL gl_get_sensor_clock_scale(const gl_context* c,uint64_t id,double* scale){
    const auto* e=c?c->endpoint(id):nullptr;if(!e||!scale)return GL_INVALID;*scale=e->motion.clock_scale();return GL_OK;
}
int32_t GL_CALL gl_get_input_metrics(const gl_context* c,uint64_t id,gl_input_metrics* out){
    const auto* e=c?c->endpoint(id):nullptr;if(!e||!out)return GL_INVALID;*out={};
    out->clock_scale=e->info.source==GL_SOURCE_STEAM?1:e->motion.clock_scale();
    out->clock_qualified=e->info.source==GL_SOURCE_STEAM||e->motion.clock_qualified();
    out->gyro_available=healthy(*e,c->now);out->gravity_available=out->gyro_available&&e->info.caps.accelerometer&&fresh(e->last_accel,c->now);
    out->sample_age_ns=e->last_arrival&&c->now>=e->last_arrival?c->now-e->last_arrival:UINT64_MAX;
    if(e->interval_count){double mean=0,variance=0;
        for(unsigned n=0;n<e->interval_count;++n)mean+=e->intervals[n]/e->interval_count;
        for(unsigned n=0;n<e->interval_count;++n)variance+=std::pow(e->intervals[n]-mean,2)/e->interval_count;
        out->report_hz=1/(mean*out->clock_scale);out->interval_jitter_ms=std::sqrt(variance)*out->clock_scale*1000;
    }
    return GL_OK;
}
void GL_CALL gl_set_recenter_callback(gl_context* c,gl_recenter_callback f,void* user) {
    if(c){c->recenter=f;c->recenter_user=user;c->recenter_primed=c->recenter_requested=false;c->emit(GL_EVENT_CONTEXT);}
}
int32_t GL_CALL gl_request_recenter(gl_context* c){
    if(!c)return GL_INVALID;if(!c->recenter)return GL_UNAVAILABLE;c->recenter_requested=true;return GL_OK;
}
int32_t GL_CALL gl_set_auto_calibration_allowed(gl_context* c,uint32_t allowed){
    if(!c||allowed>1)return GL_INVALID;c->allow_calibration=allowed!=0;return GL_OK;
}
int32_t GL_CALL gl_set_output_target(gl_context* c,uint32_t target) {
    if(!c||target>GL_OUTPUT_CURSOR)return GL_INVALID;
    if(c->output_target!=target){
        c->output_target=target;c->flick.reset();c->flick_touchpad.reset();c->suppress_touchpad=false;c->controls_primed=false;c->safe_previous=false;
        for(auto& gate:c->gates)gate.cancel();
    }
    return GL_OK;
}
uint32_t GL_CALL gl_get_output_target(const gl_context* c){return c?c->effective_output_target():GL_OUTPUT_CAMERA;}
uint64_t GL_CALL gl_get_selected_device(const gl_context* c){return c?c->selected:0;}
int32_t GL_CALL gl_register_endpoint(gl_context* c,const gl_endpoint* info) try {
    if (!c||!info||!info->id||!info->physical_id||info->source<1||info->source>2||
        !std::memchr(info->name,0,sizeof(info->name))) return GL_INVALID;
    auto* e=c->endpoint(info->id);
    if (e && (e->info.source!=info->source || e->info.physical_id!=info->physical_id)) return GL_INVALID;
    if (!e) {
        if(c->endpoints.size()>=32) return GL_LIMIT;
        c->endpoints.emplace_back(); e=&c->endpoints.back();
    }
    bool changed=e->info.connected!=info->connected;
    e->info=*info;
    if (changed) {
        e->samples.clear(); e->last_sensor=e->last_arrival=e->healthy_since=0; e->consecutive=0;
        e->controls={};e->flick_input={};e->flick_samples.clear();e->flick_explicit=false;e->triggers={};e->last_accel=0;
        e->interval_count=e->interval_next=0;e->motion.resume(); e->motion.cancel();
        if(c->active==info->id) c->reset_temporal();
        c->emit(GL_EVENT_DEVICE,info->connected,info->id);
    }
    return GL_OK;
} catch (...) { return GL_LIMIT; }
int32_t GL_CALL gl_disconnect_endpoint(gl_context* c,uint64_t id) {
    if(!c) return GL_INVALID; auto* e=c->endpoint(id); if(!e) return GL_INVALID;
    auto info=e->info; info.connected=0; return gl_register_endpoint(c,&info);
}
int32_t GL_CALL gl_forget_endpoint(gl_context* c,uint64_t id) {
    if(!c) return GL_INVALID;
    auto i=std::find_if(c->endpoints.begin(),c->endpoints.end(),[&](auto& e){return e.info.id==id;});
    if(i==c->endpoints.end()) return GL_INVALID;
    if(c->active==id)c->reset_temporal(); c->endpoints.erase(i); return GL_OK;
}
uint32_t GL_CALL gl_endpoint_count(const gl_context* c) {return c?static_cast<uint32_t>(c->endpoints.size()):0;}
uint32_t GL_CALL gl_endpoint_motion_available(const gl_context* c,uint64_t id) {
    const auto* e=c?c->endpoint(id):nullptr;return e&&healthy(*e,c->now);
}
int32_t GL_CALL gl_set_endpoint_pairing_hint(gl_context* c,uint64_t id,uint32_t vendor,uint32_t product,uint32_t is_virtual) {
    auto* e=c?c->endpoint(id):nullptr;if(!e||vendor>65535||product>65535||is_virtual>1)return GL_INVALID;
    e->pairing_vendor=vendor;e->virtual_controller=is_virtual!=0;return GL_OK;
}
uint32_t GL_CALL gl_motion_sensor_needs_selection(const gl_context* c,uint64_t physical) {
    if(!c||!physical||gl_get_motion_sensor(c,physical)||native_motion(c,physical)||automatic_sensor(c,physical))return 0;
    for(const auto& e:c->endpoints)if(e.motion_companion&&healthy(e,c->now))return 1;
    return 0;
}
int32_t GL_CALL gl_get_endpoint(const gl_context* c,uint32_t i,gl_endpoint* out) {
    if(!c||!out||i>=c->endpoints.size()) return GL_INVALID; *out=std::next(c->endpoints.begin(),i)->info; return GL_OK;
}
int32_t GL_CALL gl_associate_endpoint(gl_context* c,uint64_t id,uint64_t physical) try {
    if(!c||!physical)return GL_INVALID; auto* e=c->endpoint(id); if(!e)return GL_INVALID;
    auto previous=e->info.physical_id; e->info.physical_id=physical;
    if(c->selected==previous)c->selected=physical;
    c->reset_temporal(); c->emit(GL_EVENT_ASSOCIATION,0,id); return GL_OK;
} catch (...) {return GL_LIMIT;}
int32_t GL_CALL gl_set_motion_companion(gl_context* c,uint64_t id){
    auto* e=c?c->endpoint(id):nullptr;if(!e)return GL_INVALID;
    if(!e->motion_companion){e->motion_companion=true;e->companion_identity=e->info.physical_id;}
    return GL_OK;
}
uint32_t GL_CALL gl_is_motion_companion(const gl_context* c,uint64_t id){
    auto* e=c?c->endpoint(id):nullptr;return e&&e->motion_companion;
}
uint64_t GL_CALL gl_get_motion_sensor(const gl_context* c,uint64_t physical){
    if(c&&physical)for(const auto& e:c->endpoints)
        if(e.motion_companion&&e.info.physical_id==physical&&e.info.physical_id!=e.companion_identity)return e.info.id;
    return 0;
}
int32_t GL_CALL gl_bind_motion_sensor(gl_context* c,uint64_t physical,uint64_t sensor) try {
    if(!c||!physical)return GL_INVALID;
    if(std::none_of(c->endpoints.begin(),c->endpoints.end(),[&](const auto& e){return !e.motion_companion&&e.info.connected&&e.info.physical_id==physical;}))return GL_UNAVAILABLE;
    auto* requested=sensor?c->endpoint(sensor):nullptr;
    if(sensor&&(!requested||!requested->motion_companion||!requested->info.connected))return GL_INVALID;
    c->manual_motion_groups.insert(physical);
    if(gl_get_motion_sensor(c,physical)==sensor)return GL_OK;
    for(auto& e:c->endpoints)if(e.motion_companion&&(e.info.physical_id==physical||&e==requested)){
        e.info.physical_id=&e==requested?physical:e.companion_identity;
        e.samples.clear();e.last_sensor=e.last_arrival=e.last_accel=e.healthy_since=0;e.consecutive=0;
        e.interval_count=e.interval_next=0;
        e.motion.resume();e.motion.cancel();c->emit(GL_EVENT_ASSOCIATION,0,e.info.id);
    }
    // Keep the host's chosen controller; only its motion endpoint has changed.
    c->reset_temporal();return GL_OK;
} catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_select_device(gl_context* c,uint64_t physical) {
    if(!c)return GL_INVALID;
    if(physical && std::none_of(c->endpoints.begin(),c->endpoints.end(),[&](auto& e){return e.info.physical_id==physical;}))return GL_INVALID;
    c->selected=physical;c->selected_at=c->now;c->reset_temporal();return GL_OK;
}
int32_t GL_CALL gl_submit_sample(gl_context* c,uint64_t id,const gl_sample* s) try {
    if(!c||!s)return GL_INVALID; auto* e=c->endpoint(id);
    if(!e||!e->info.connected)return GL_UNAVAILABLE;
    // A hardware clock may restart on wake. Only a genuine arrival gap permits
    // an epoch reset; duplicate/out-of-order packets in a live stream are rejected.
    const bool new_epoch=e->last_arrival&&s->arrival_ns>e->last_arrival&&s->arrival_ns-e->last_arrival>=fresh_ns;
    const double accel=std::hypot(s->accel_g.x,s->accel_g.y,s->accel_g.z);
    if(!s->sensor_ns||!s->arrival_ns||!finite(s->gyro_dps)||!finite(s->accel_g)||
       (accel>0&&accel<0.05)||accel>3.5||std::hypot(s->gyro_dps.x,s->gyro_dps.y,s->gyro_dps.z)>4000||
       (!new_epoch&&s->sensor_ns<=e->last_sensor)||s->arrival_ns<e->last_arrival) {
        ++c->totals.rejected_samples;c->emit(GL_EVENT_INVALID_SAMPLE,0,id);return GL_INVALID;
    }
    // Only a validated wake packet may discard the previous clock epoch and
    // pending samples. Malformed packets cannot reset a healthy stream.
    if(new_epoch) {
        e->last_sensor=e->last_accel=0;e->consecutive=0;e->healthy_since=0;e->interval_count=e->interval_next=0;e->samples.clear();e->motion.resume();e->motion.cancel();
    }
    if(!e->last_arrival||s->arrival_ns-e->last_arrival>qualify_gap) {
        e->consecutive=0;e->healthy_since=s->arrival_ns;
    }
    e->consecutive=std::min(e->consecutive+1,100000u);
    if(e->last_sensor){const auto interval=(s->sensor_ns-e->last_sensor)*1e-9;
        if(interval<=.1){e->intervals[e->interval_next]=interval;e->interval_next=(e->interval_next+1)%64;e->interval_count=std::min(e->interval_count+1,64u);}
        else e->interval_count=e->interval_next=0;
    }
    e->last_sensor=s->sensor_ns;e->last_arrival=s->arrival_ns;
    if(e->info.caps.accelerometer&&accel>=.05)e->last_accel=s->arrival_ns;
    if(e->samples.size()==512){e->samples.pop_front();++c->totals.dropped_samples;c->emit(GL_EVENT_OVERFLOW,0,id);}
    e->samples.push_back(*s);
    if(c->sample_observer)try{c->sample_observer(c->sample_observer_user,id,s);}catch(...){/* Diagnostics must not interrupt acquisition. */}
    return GL_OK;
} catch (...) {return GL_LIMIT;}
int32_t GL_CALL gl_submit_controls(gl_context* c,uint64_t id,const gl_controls* s) try {
    if(!c||!s)return GL_INVALID;auto* e=c->endpoint(id);
    if(!e||!e->info.connected)return GL_UNAVAILABLE;
    for(float v:{s->left_x,s->left_y,s->right_x,s->right_y})if(!std::isfinite(v)||std::abs(v)>1)return GL_INVALID;
    if(s->timestamp_ns<e->controls.timestamp_ns)return GL_INVALID;
    e->controls=*s;
    if(!e->flick_explicit&&(e->info.caps.sticks&GL_RIGHT)){
        const gl_flick_input f{s->timestamp_ns,GL_FLICK_INPUT_STICK,0,s->right_x,s->right_y,0,0};
        if(!e->flick_samples.empty()&&e->flick_samples.back().timestamp_ns==s->timestamp_ns)e->flick_samples.back()=f;
        else {if(e->flick_samples.size()==512){e->flick_samples.pop_front();c->emit(GL_EVENT_OVERFLOW,1,id);}e->flick_samples.push_back(f);}
    }
    return GL_OK;
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_submit_flick_input(gl_context* c,uint64_t id,const gl_flick_input* input)try{
    auto* e=c?c->endpoint(id):nullptr;if(!e||!input||!input->timestamp_ns||(input->available&~3u)||input->touching>1)return GL_INVALID;
    if(!e->info.connected)return GL_UNAVAILABLE;
    for(float value:{input->stick_x,input->stick_y,input->touchpad_x,input->touchpad_y})if(!std::isfinite(value)||std::abs(value)>1)return GL_INVALID;
    if(input->timestamp_ns<e->flick_input.timestamp_ns)return GL_INVALID;
    if(!e->flick_explicit)e->flick_samples.clear();e->flick_explicit=true;
    e->flick_input=*input;
    if(!e->flick_samples.empty()&&e->flick_samples.back().timestamp_ns==input->timestamp_ns)e->flick_samples.back()=*input;
    else {if(e->flick_samples.size()==512){e->flick_samples.pop_front();c->emit(GL_EVENT_OVERFLOW,1,id);}e->flick_samples.push_back(*input);}
    return GL_OK;
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_get_flick_input(const gl_context* c,gl_flick_input* input){
    if(!c||!input)return GL_INVALID;*input=c->flick_inputs();return GL_OK;
}
uint32_t GL_CALL gl_suppress_native_right_touchpad(const gl_context* c){return c&&c->suppress_touchpad;}
int32_t GL_CALL gl_get_touchpad_feedback(const gl_context* c,gl_touchpad_feedback* feedback){
    if(!c||!feedback)return GL_INVALID;
    *feedback={c->now,c->selected,uint32_t(c->suppress_touchpad&&c->touchpad_pulse)};return GL_OK;
}
int32_t GL_CALL gl_update(gl_context* c,uint64_t now,const gl_host_state* host,gl_output* output) try {
    if(c){c->suppress_touchpad=false;c->touchpad_pulse=false;c->gyro_state={};}
    if(output)*output={};
    if(!c||!host||!output||!now||now<c->now)return GL_INVALID;
    bool double_gyro_notice=host->steam_gyro_output==2&&c->host.steam_gyro_output!=2;
    c->now=now;c->host=*host;*output={};
    for(auto& [id,state]:c->gameplay_contexts) {
        if(!state.reported&&state.available) {state.available=false;c->emit(GL_EVENT_CONTEXT,0,0,id);}
        state.reported=false;
    }
    const auto connected_controller=[&](uint64_t physical){return std::any_of(c->endpoints.begin(),c->endpoints.end(),[&](const auto& e){
        return e.info.connected&&!e.motion_companion&&e.info.physical_id==physical;});};
    // A sensor that is still shutting down must not keep a removed controller
    // selected. Keep a live choice stable, including temporary sensor stalls.
    if(c->selected&&!connected_controller(c->selected)){
        c->selected=0;c->reset_temporal();
    }
    // Release companions of removed game controllers for a new virtual identity.
    for(auto& e:c->endpoints)if(e.motion_companion&&e.info.physical_id!=e.companion_identity&&!connected_controller(e.info.physical_id)){
        e.info.physical_id=e.companion_identity;c->emit(GL_EVENT_ASSOCIATION,0,e.info.id);
    }
    if(!c->selected) {
        for(auto& e:c->endpoints)if(e.info.connected&&!e.motion_companion){c->selected=e.info.physical_id;c->selected_at=now;break;}
    }
    for(auto i=c->manual_motion_groups.begin();i!=c->manual_motion_groups.end();){
        const auto physical=*i;
        const bool connected=std::any_of(c->endpoints.begin(),c->endpoints.end(),[&](const auto& e){return e.info.connected&&!e.motion_companion&&e.info.physical_id==physical;});
        if(!connected)i=c->manual_motion_groups.erase(i);else ++i;
    }
    if(!gl_get_motion_sensor(c,c->selected)&&!native_motion(c,c->selected)) {
        if(const auto* candidate=automatic_sensor(c,c->selected);candidate&&healthy(*candidate,now)&&now-candidate->healthy_since>=300000000){
            if(gl_bind_motion_sensor(c,c->selected,candidate->info.id)==GL_OK)c->manual_motion_groups.erase(c->selected);
        }
    }
    // Resolve the host's view once for this frame. Contact migration may update
    // its activators, so finish it before taking the settings snapshot.
    c->migrate_contact_bindings();
    const auto profile=c->winning_context();
    const auto v=c->effective_settings(profile);
    EndpointState *sdl=nullptr,*steam=nullptr;
    const bool need_gravity=gravity_space(int(v[Space]));
    const auto usable=[&](const EndpointState& e){return healthy(e,now)&&
        (!need_gravity||(e.info.caps.accelerometer&&fresh(e.last_accel,now)));};
    for(auto& e:c->endpoints)if(e.info.physical_id==c->selected&&usable(e)) {
        auto*& target=e.info.source==GL_SOURCE_SDL?sdl:steam;
        // If multiple proven endpoints exist, retain current one; otherwise lowest stable ID.
        if(!target||e.info.id==c->active||(target->info.id!=c->active&&e.info.id<target->info.id))target=&e;
    }
    auto* active=c->endpoint(c->active);
    bool current_good=active&&usable(*active);
    EndpointState* chosen=current_good?active:nullptr;
    if(!chosen) {
        if(sdl)chosen=sdl;
        else if(steam && now-c->selected_at>=250000000)chosen=steam;
    } else if(chosen->info.source==GL_SOURCE_STEAM && sdl &&
              now-sdl->healthy_since>=1000000000 && now-c->switched_at>=2000000000) chosen=sdl;
    uint64_t next=chosen?chosen->info.id:0;
    if(next!=c->active) {
        if(active){active->motion.resume();active->motion.cancel();}
        c->active=next;c->switched_at=now;c->flick.reset();c->flick_touchpad.reset();c->controls_primed=false;c->recenter_primed=false;
        if(chosen)chosen->motion.resume();
        c->emit(GL_EVENT_SOURCE,chosen?static_cast<int>(chosen->info.source):0,next);
    }
    output->physical_id=c->selected;output->endpoint_id=next;output->source=chosen?chosen->info.source:0;
    gl_controls controls{};bool controls_fresh=false;
    const auto* physical_buttons=c->button_companion();
    const auto* physical_pads=c->control_authority(GL_CONTROL_TOUCHPADS);
    const auto* physical_touch=c->control_authority(GL_CONTROL_STICK_TOUCH);
    const auto* physical_grip=c->control_authority(GL_CONTROL_GRIP_TOUCH);
    const auto* physical_sticks=c->control_authority(GL_CONTROL_STICKS);
    uint32_t analog_owned=0;
    // SDL controls take priority. Contacts/buttons on a proven paired endpoint may supplement.
    for(unsigned source:{GL_SOURCE_SDL,GL_SOURCE_STEAM})for(auto& e:c->endpoints)
      if(e.info.connected&&e.info.physical_id==c->selected&&e.info.source==source&&fresh(e.controls.timestamp_ns,now)) {
        controls_fresh=true;auto& s=e.controls;
        // Physical labels must describe physical activators, not the Steam
        // layout's remapped game buttons. A stalled companion cannot silently
        // change the meaning of an activator back to a virtual button.
        if(!physical_buttons||physical_buttons==&e)controls.buttons|=s.buttons&e.info.caps.buttons;
        if(!physical_pads||physical_pads==&e)controls.touchpads|=s.touchpads&e.info.caps.touchpads;
        if(!physical_touch||physical_touch==&e)controls.stick_touch|=s.stick_touch&e.info.caps.stick_touch;
        if(!physical_grip||physical_grip==&e)controls.grip_touch|=s.grip_touch&e.info.caps.grip_touch;
        if(!physical_sticks||physical_sticks==&e){
            if((e.info.caps.sticks&GL_LEFT)&&!(analog_owned&GL_LEFT)){controls.left_x=s.left_x;controls.left_y=s.left_y;analog_owned|=GL_LEFT;}
            if((e.info.caps.sticks&GL_RIGHT)&&!(analog_owned&GL_RIGHT)){controls.right_x=s.right_x;controls.right_y=s.right_y;analog_owned|=GL_RIGHT;}
        }
    }
    const bool profile_changed=profile!=c->previous_profile;
    if(profile_changed){
        c->previous_profile=profile;c->controls_primed=false;c->stick_gate.reset();c->trigger_gate.reset();
        if(chosen){chosen->motion.clear_smoothing();chosen->motion.clear_trackball();}
    }
    if(c->filter_profile!=profile){for(auto& gate:c->gates)gate.cancel();c->filter_profile=profile;}
    auto& toggle=c->toggle; // one live toggle state shared by every view using Toggle
    auto& previous_activation=profile?c->gameplay_contexts.at(profile).activation_previous:c->activation_previous;
    unsigned button=static_cast<unsigned>(v[Button]);
    bool held=button_matches(controls.buttons,button);
    held|=side_held(controls.touchpads,static_cast<int>(v[Touchpad]));
    held|=side_held(controls.stick_touch,static_cast<int>(v[StickTouch]));
    held|=side_held(controls.grip_touch,static_cast<int>(v[GripTouch]));
    uint32_t deflected=c->stick_gate.update(std::hypot(controls.left_x,controls.left_y),std::hypot(controls.right_x,controls.right_y),v[Threshold]);
    held|=side_held(deflected,static_cast<int>(v[Stick]));
    const auto triggers=c->trigger_inputs();
    const auto pulled=c->trigger_gate.update((triggers.available&GL_LEFT)?triggers.left:0,(triggers.available&GL_RIGHT)?triggers.right:0,v[TriggerThreshold]);
    held|=side_held(pulled,int(v[Trigger]));controls_fresh|=triggers.timestamp_ns!=0;
    const auto output_target=c->effective_output_target(profile);
    if(c->previous_output_target!=output_target){
        c->previous_output_target=output_target;c->flick.reset();c->flick_touchpad.reset();c->controls_primed=false;c->safe_previous=false;
        for(auto& gate:c->gates)gate.cancel();
    }
    const bool cursor=output_target==GL_OUTPUT_CURSOR;
    const bool destination_allowed=cursor?((c->host_capabilities&GL_HOST_MENU_STATE)&&host->menu_open):
        (host->camera_allowed&&!host->menu_open);
    const bool profile_valid=profile!=0;
    bool safe=profile_valid&&host->focused&&destination_allowed&&!host->paused&&!c->panel;
    int activation=static_cast<int>(v[Activation]);
    if(previous_activation!=activation){previous_activation=activation;c->controls_primed=false;}
    if(!c->controls_primed||!safe||!c->safe_previous) {c->held_previous=held;c->controls_primed=controls_fresh;}
    if(activation==GL_TOGGLE&&safe&&controls_fresh&&held&&!c->held_previous)toggle=!toggle;
    c->held_previous=held;
    const bool hold_modifiers=activation==GL_HOLD_DISABLE&&held;
    bool enabled=activation==GL_ALWAYS || (activation==GL_HOLD_DISABLE&&(!held||v[HoldInvert]||v[HoldTrackball]))||
        (activation==GL_HOLD&&held)||(activation==GL_TOGGLE&&toggle);
    if(c->gyro_override!=-1)enabled=c->gyro_override!=0;
    const bool gravity_ok=chosen&&(!need_gravity||(chosen->info.caps.accelerometer&&fresh(chosen->last_accel,now)));
    bool gate=safe&&enabled&&activation!=GL_GYRO_OFF&&gravity_ok;
    uint32_t modifiers=c->modifiers;
    const auto axes=[](int value,uint32_t x,uint32_t y){return value==0?x:value==1?y:x|y;};
    if(hold_modifiers&&v[HoldInvert])modifiers^=axes(int(v[TemporaryInvertAxes]),GL_MOD_INVERT_X,GL_MOD_INVERT_Y);
    if(hold_modifiers&&v[HoldTrackball])modifiers|=axes(int(v[TrackballAxes]),GL_MOD_TRACK_X,GL_MOD_TRACK_Y);
    const uint32_t track_axes=modifiers&(GL_MOD_TRACK_X|GL_MOD_TRACK_Y);
    const double dt=c->previous_frame?(now-c->previous_frame)*1e-9:0;c->previous_frame=now;
    // Input edges immediately change the gate; fusion is never reset for these edges.
    if(chosen) {
        if(dt>.1)chosen->motion.clear_trackball();
        uint32_t previous_cal=chosen->motion.diagnostics.calibration_state;
        gl_vec3 previous_bias=chosen->motion.diagnostics.bias;
        // A gyro cursor is a use of motion, not a safe calibration menu.
        // Keep host.menu_open for cursor routing; pauses/settings suspend it.
        const bool calibration_menu=(c->host_capabilities&GL_HOST_MENU_STATE)&&
            (c->panel||(host->menu_open&&(!cursor||host->paused)));
        const auto accepted=chosen->motion.diagnostics.accepted_samples;
        for(const auto& s:chosen->samples)if(fresh(s.arrival_ns,now)){
            auto sample=s;if(!chosen->info.caps.accelerometer)sample.accel_g={};
            const bool sample_usable=gate&&(!need_gravity||std::hypot(sample.accel_g.x,sample.accel_g.y,sample.accel_g.z)>=.05);
            chosen->motion.process(sample,v,chosen->info.source==GL_SOURCE_STEAM,calibration_menu,sample_usable,c->allow_calibration,*output,track_axes);
        }
        auto& d=chosen->motion.diagnostics;
        const bool calibrating=d.calibration_state==GL_CAL_COUNTDOWN||d.calibration_state==GL_CAL_COLLECTING||d.calibration_state==GL_CAL_MOVING;
        c->gyro_state={uint32_t(gate&&!calibrating),uint32_t(gravity_ok),uint32_t(d.accepted_samples-accepted),uint32_t(gate&&!calibrating&&track_axes!=0)};
        if(gate&&!calibrating)chosen->motion.trackball(dt,v[TrackballDecay],track_axes,*output);
        else chosen->motion.clear_trackball();
        if(previous_cal!=d.calibration_state)c->emit(GL_EVENT_CALIBRATION,static_cast<int>(d.calibration_state),next);
        else if(previous_bias.x!=d.bias.x||previous_bias.y!=d.bias.y||previous_bias.z!=d.bias.z)c->emit(GL_EVENT_CALIBRATION,GL_CAL_IDLE,next);
    }
    if(modifiers&GL_MOD_INVERT_X)output->yaw_degrees=-output->yaw_degrees;
    if(modifiers&GL_MOD_INVERT_Y)output->pitch_degrees=-output->pitch_degrees;
    c->modifiers=0;c->gyro_override=-1;
    for(auto& e:c->endpoints)e.samples.clear(); // never replay alternate-source backlog
    if(profile){const auto& view=c->gameplay_contexts.at(profile);
        if(!cursor&&v[ZoomCompensation]&&view.zoom_available&&view.fov_reported){
            const double ratio=std::tan(view.fov*std::numbers::pi/360)/std::tan(view.reference_fov*std::numbers::pi/360);
            output->yaw_degrees*=ratio;output->pitch_degrees*=ratio;
        }
    }
    for(auto& [id,view]:c->gameplay_contexts)view.fov_reported=false;
    c->allow_calibration=true;
    const bool recenter_held=button_matches(controls.buttons,static_cast<int>(v[RecenterButton]));
    if(!safe||profile_changed||!controls_fresh||!c->safe_previous)c->recenter_primed=false;
    bool recenter=safe&&!cursor&&c->recenter&&(c->recenter_requested||
        (c->recenter_primed&&recenter_held&&!c->recenter_held));
    c->recenter_requested=false;c->recenter_held=recenter_held;c->recenter_primed=safe&&controls_fresh;
    int flick_mode=static_cast<int>(v[FlickMode]);
    bool flick_enabled=flick_mode==GL_FLICK_ON ||flick_mode==GL_FLICK_TOUCHPAD||flick_mode==GL_FLICK_BOTH;
    const auto flick_input=c->flick_inputs();
    const bool use_stick=flick_mode!=GL_FLICK_TOUCHPAD;
    const bool use_pad=flick_mode==GL_FLICK_TOUCHPAD||flick_mode==GL_FLICK_BOTH;
    bool flick_safe=!cursor&&safe&&use_stick&&(flick_input.available&GL_FLICK_INPUT_STICK)&&(c->host_capabilities&GL_HOST_NATIVE_STICK_SUPPRESSION);
    const bool pad_safe=!cursor&&safe&&use_pad&&(flick_input.available&GL_FLICK_INPUT_TOUCHPAD)&&(c->host_capabilities&GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
    const FlickOptions flick_options{int(v[FlickStyle]),int(v[FlickSmoothing]),int(v[FlickSnap]),
        v[FlickSmoothAngle],v[FlickSnapStrength],v[FlickForward],v[FlickExponent]};
    c->flick.configure(flick_options);c->flick_touchpad.configure(flick_options);
    c->flick.thresholds(v[FlickStickInner],v[FlickStickOuter]);c->flick_touchpad.pad_inner=v[FlickPadInner];c->flick_touchpad.pad_outer=v[FlickPadOuter];
    c->flick.smoothing_speed=c->flick_touchpad.smoothing_speed=v[FlickSmoothSpeed];
    const auto* provider=c->flick_provider();const auto provider_id=provider?provider->info.id:0;
    const bool restart=profile_changed||c->flick_endpoint!=provider_id||!c->flick_time||dt>.1||!safe||!c->safe_previous;
    const auto play=[&](const gl_flick_input& input,double elapsed,bool changed,double report_dt,bool resume){
        const bool stick_safe=flick_safe&&(input.available&GL_FLICK_INPUT_STICK);
        const bool touch_safe=pad_safe&&(input.available&GL_FLICK_INPUT_TOUCHPAD);
        output->yaw_degrees+=c->flick.process(input.stick_x,input.stick_y,elapsed,stick_safe,flick_enabled,
            flick_mode,int(v[FlickMs]),0,resume,false,changed,report_dt);
        output->yaw_degrees+=c->flick_touchpad.process(input.touching?input.touchpad_x:0,input.touching?input.touchpad_y:0,
            elapsed,touch_safe,flick_enabled,flick_mode,int(v[FlickMs]),0,resume,true,changed,report_dt);
        c->touchpad_pulse|=c->flick_touchpad.feedback();
    };
    if(restart){c->flick.reset();c->flick_touchpad.reset();c->flick_time=now;c->flick_report=flick_input.timestamp_ns;
        play(flick_input,dt>0&&dt<=.1?dt:.001,true,dt,true);}
    else if(provider){
        for(const auto& sample:provider->flick_samples)if(sample.timestamp_ns>c->flick_time&&sample.timestamp_ns<=now){
            const double interval=(sample.timestamp_ns-c->flick_time)*1e-9;
            const double report_interval=c->flick_report?(sample.timestamp_ns-c->flick_report)*1e-9:interval;
            play(sample,interval,true,report_interval,false);c->flick_time=c->flick_report=sample.timestamp_ns;
        }
        if(now>c->flick_time){play(flick_input,(now-c->flick_time)*1e-9,false,0,false);c->flick_time=now;}
    }
    c->flick_endpoint=provider_id;
    for(auto& endpoint:c->endpoints)endpoint.flick_samples.clear();
    output->suppress_native_right_stick=flick_safe&&flick_enabled;
    c->suppress_touchpad=pad_safe&&flick_enabled;
    if(!safe || host->suspend_short_press)for(auto& g:c->gates)g.cancel();
    if(host->steam_gyro_output==2 && (double_gyro_notice||c->output.source==0) && output->source)c->emit(GL_EVENT_DOUBLE_GYRO,2,next);
    c->safe_previous=safe;c->output=*output;
    if(!cursor&&c->camera&&(output->yaw_degrees||output->pitch_degrees))c->camera(c->camera_user,output->yaw_degrees,output->pitch_degrees);
    if(recenter)c->recenter(c->recenter_user);
    return GL_OK;
} catch (...) {if(output)*output={};return GL_LIMIT;}
int32_t GL_CALL gl_get_diagnostics(const gl_context* c,gl_diagnostics* d) {
    if(!c||!d)return GL_INVALID;*d=c->totals;
    auto* e=c->endpoint(c->active);
    if(e){*d=e->motion.diagnostics;d->rejected_samples+=c->totals.rejected_samples;d->dropped_samples+=c->totals.dropped_samples;d->source=e->info.source;}
    return GL_OK;
}
int32_t GL_CALL gl_poll_event(gl_context* c,gl_event* e) {
    if(!c||!e)return GL_INVALID;if(c->events.empty())return 0;*e=c->events.front();c->events.pop_front();return 1;
}
int32_t GL_CALL gl_begin_calibration(gl_context* c) try {
    if(!c)return GL_INVALID;auto* e=c->endpoint(c->active);
    if(!e||e->info.source==GL_SOURCE_STEAM||!fresh(e->last_accel,c->now))return GL_UNAVAILABLE;
    e->motion.begin();c->emit(GL_EVENT_CALIBRATION,GL_CAL_COUNTDOWN,c->active);return GL_OK;
} catch (...) {return GL_LIMIT;}
void GL_CALL gl_cancel_calibration(gl_context* c) {
    if(c)for(auto& e:c->endpoints) {
        bool changed=e.motion.diagnostics.calibration_state!=GL_CAL_IDLE;
        e.motion.cancel();
        if(changed)try{c->emit(GL_EVENT_CALIBRATION,GL_CAL_IDLE,e.info.id);}catch(...){}
    }
}
int32_t GL_CALL gl_filter_event(gl_context* c,uint32_t key,uint32_t event,uint64_t now,uint32_t eligible,uint32_t native_hold) {
    if(!c||key>=64||event>GL_REPEAT)return GL_INVALID;
    const auto profile=c->winning_context();
    if(c->filter_profile!=profile){for(auto& gate:c->gates)gate.cancel();c->filter_profile=profile;}
    bool safe=c->effective_output_target(profile)==GL_OUTPUT_CAMERA&&profile!=0&&
        c->host.focused&&c->host.camera_allowed&&!c->host.menu_open&&!c->host.paused&&!c->panel;
    const auto settings=c->effective_settings(profile);
    const bool hold=settings[Activation]==double(GL_HOLD)||settings[Activation]==double(GL_HOLD_DISABLE);
    return c->gates[key].event(event,now,eligible&&settings[Button]>0&&settings[ShortPress]&&hold&&safe&&
        (c->host_capabilities&GL_HOST_SHORT_PRESS_FILTER),native_hold||c->host.suspend_short_press);
}
void GL_CALL gl_set_panel_open(gl_context* c,uint32_t open) {if(c){c->panel=open!=0;if(open)for(auto& g:c->gates)g.cancel();}}
uint32_t GL_CALL gl_panel_open(const gl_context* c) {return c&&c->panel;}
}
