#include "detail/internal.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace gyrolib {
const std::array<SettingDef,SettingCount> definitions={{
#define GL_SETTING(symbol,id,key,type,def,min,max,step,unit) {id,key,"description." key,type,def,min,max,step,unit},
#include "detail/settings.def"
#undef GL_SETTING
}};
Settings defaults() { Settings s{};for(size_t i=0;i<s.size();++i)s[i]=definitions[i].def;return s; }
static void apply_acceleration_preset(Settings& values){
    if(values[Acceleration]>=4)return;
    constexpr double gain[]={1,1.5,2,3};
    values[FastSensX]=values[SensX]*gain[int(values[Acceleration])];
    values[FastSensY]=values[SensY]*gain[int(values[Acceleration])];
    values[SlowSpeed]=5;values[FastSpeed]=75;
}
bool profile_setting(int field){return field>=0&&field<SettingCount&&field!=AutoCal&&field!=UIScale&&field!=GyroContext&&field!=FlickContext;}
std::string profile_key(uint32_t id,int field){
    return "context."+std::to_string(id)+"."+(field==SensX?"sensitivity_x":field==SensY?"sensitivity_y":definitions[field].id);
}
int setting_index(const char* id) {
    if(!id)return -1;for(int i=0;i<SettingCount;++i)if(std::strcmp(id,definitions[i].id)==0)return i;return -1;
}
bool side_available(uint32_t caps,int choice) {
    switch(choice){case GL_SIDE_OFF:return true;case GL_SIDE_LEFT:return (caps&GL_LEFT)!=0;
        case GL_SIDE_RIGHT:return (caps&GL_RIGHT)!=0;case GL_SIDE_EITHER:return caps!=0;
        case GL_SIDE_BOTH:return (caps&3)==3;default:return false;}
}
bool side_held(uint32_t state,int choice) { return choice!=0 && side_available(state,choice); }
bool button_matches(uint32_t state,int choice) {
    if(choice<=0||choice>40)return false;
    if(choice<=32)return (state&(1u<<(choice-1)))!=0;
    constexpr uint32_t pairs[]={(1u<<9)|(1u<<10),(1u<<7)|(1u<<8),(1u<<16)|(1u<<17),(1u<<18)|(1u<<19)};
    uint32_t mask=pairs[(choice-33)/2];return choice%2?(state&mask)!=0:(state&mask)==mask;
}
}
using namespace gyrolib;
namespace {
struct SettingsBatch {
    gl_context* context;
    explicit SettingsBatch(gl_context* c):context(c){++context->settings_batch_depth;}
    ~SettingsBatch(){--context->settings_batch_depth;}
};
int save_change(gl_context* c){
    return c->save_change();
}
bool known_language(const char* language){
    if(!language)return false;
    for(const char* known:{"en","fr","de","es","it","pt"})if(std::strcmp(language,known)==0)return true;
    return false;
}
const char* action_ids[]={"calibration.begin","calibration.cancel","settings.reset","settings.save","settings.recommended"};
const char* action_keys[]={"calibration.action","cancel","reset","save","recommended"};
uint32_t family_cap(const gl_capabilities& c,int index) {
    switch(index){case Touchpad:return c.touchpads;case StickTouch:return c.stick_touch;
        case GripTouch:return c.grip_touch;case Stick:return c.sticks;default:return 0;}
}
double quantize(int index,double value) {
    const auto& d=definitions[index];value=std::clamp(value,d.min,d.max);
    return std::clamp(d.min+std::round((value-d.min)/d.step)*d.step,d.min,d.max);
}
std::string trim(std::string s) {
    auto first=s.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};
    return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
bool parse_number(const std::string& s,double& value) {
    std::istringstream in(s);in.imbue(std::locale::classic());in>>value;
    if(in.fail()||!std::isfinite(value))return false;in>>std::ws;return in.eof();
}
// Semantic format identifiers are compared component-wise, never as decimals.
bool parse_schema(std::string_view text,std::array<uint32_t,3>& version){
    for(size_t i=0;i<version.size();++i){
        const auto dot=text.find('.');
        if((i<2)==(dot==std::string_view::npos))return false;
        const auto part=text.substr(0,dot);
        if(part.empty()||(part.size()>1&&part.front()=='0'))return false;
        const auto parsed=std::from_chars(part.data(),part.data()+part.size(),version[i]);
        if(parsed.ec!=std::errc{}||parsed.ptr!=part.data()+part.size())return false;
        if(i<2)text.remove_prefix(dot+1);
    }
    return true;
}
int schema_status(std::string_view text){
    std::array<uint32_t,3> version{},current{};
    if(!parse_schema(text,version)||!parse_schema(GL_SETTINGS_SCHEMA,current))return GL_INVALID;
    if(version>current)return GL_NEWER_SCHEMA;
    return version==current?GL_OK:GL_INVALID;
}
bool persisted_profile_setting(int field){
    return profile_setting(field)&&field!=Enabled&&field!=FlickSmoothAngle&&
        field!=TemporaryInvertButton&&field!=TrackballButton&&field!=StickEffect;
}
bool context_setting(const char* key,uint32_t& id,int& field) {
    if(!key)return false;std::string_view text(key),prefix="context.";
    if(!text.starts_with(prefix))return false;text.remove_prefix(prefix.size());
    auto dot=text.find('.');if(dot==std::string_view::npos)return false;
    auto number=text.substr(0,dot),suffix=text.substr(dot);
    if(number.empty()||number.front()=='0')return false;
    auto result=std::from_chars(number.data(),number.data()+number.size(),id);
    if(result.ec!=std::errc{}||result.ptr!=number.data()+number.size()||!id)return false;
    if(suffix==".sensitivity_x")field=SensX;else if(suffix==".sensitivity_y")field=SensY;
    else {field=-1;for(int i=0;i<SettingCount;++i)if(suffix.substr(1)==definitions[i].id){field=i;break;}}
    return profile_setting(field);
}
bool context_selector(int index){return index==GyroContext||index==FlickContext;}
uint32_t profile_count(){uint32_t count=0;for(int i=0;i<SettingCount;++i)count+=profile_setting(i);return count;}
int profile_field(uint32_t index){
    constexpr int first[]={Activation,Space,LocalAngle,LocalContribution,SensX,SensY,InvertX,InvertRoll,InvertY,
        Button,Trigger,TriggerThreshold,Touchpad,StickTouch,GripTouch,Stick,Threshold,BlockLongPress};
    for(int field:first)if(index--==0)return field;
    for(int i=0;i<SettingCount;++i)if(profile_setting(i)&&std::find(std::begin(first),std::end(first),i)==std::end(first)){
    // Place the replacement checkboxes before their axis/decay controls without
    // changing stored field ordinals. Retired bindings stay hidden at the end.
    const int field=i==TemporaryInvertButton?HoldInvert:i==TrackballButton?HoldTrackball:
        i==HoldInvert?TemporaryInvertButton:i==HoldTrackball?TrackballButton:i;
    if(index--==0)return field;
    }return -1;}
int resolved_field(const char* key){uint32_t id;int field;return context_setting(key,id,field)?field:setting_index(key);}
bool valid_profile_value(int field,double value){
    return (field!=Activation||(value==double(GL_ALWAYS)||value==double(GL_HOLD)||value==double(GL_TOGGLE)||value==double(GL_HOLD_DISABLE)||value==double(GL_GYRO_OFF)))&&
           (field!=FlickMode||(value==double(GL_FLICK_OFF)||value==double(GL_FLICK_ON)||value==double(GL_FLICK_TOUCHPAD)||value==double(GL_FLICK_BOTH)));
}
}
extern "C" {
uint32_t GL_CALL gl_setting_advanced_group(const char* id){
    const int field=resolved_field(id);
    if(field==SmoothThreshold||field==Tightening)return GL_ADVANCED_SMOOTHING;
    if(field==RecenterMs)return GL_ADVANCED_RECENTER;
    if(field>=FastSensX&&field<=FastSpeed)return GL_ADVANCED_ACCELERATION;
    if(field==FlickMs||(field>=FlickStyle&&field<=FlickExponent)||(field>=FlickSmoothSpeed&&field<=FlickPadOuter))return GL_ADVANCED_FLICK;
    if(field==HoldInvert||field==HoldTrackball||field==TemporaryInvertAxes||field==TrackballAxes||field==TrackballDecay)return GL_ADVANCED_HOLD_DISABLE;
    return GL_ADVANCED_NONE;
}
uint32_t GL_CALL gl_setting_is_advanced(const char* id){return gl_setting_advanced_group(id)!=GL_ADVANCED_NONE;}
uint32_t GL_CALL gl_setting_is_activator(const char* id){
    const int field=resolved_field(id);
    return (field>=Button&&field<=BlockLongPress)||field==Trigger||field==TriggerThreshold;
}
uint32_t GL_CALL gl_menu_shared_setting_count(const gl_context* c){return c?7:0;}
int32_t GL_CALL gl_menu_shared_setting_at(const gl_context* c,uint32_t index,gl_setting_info* out){
    if(!c||!out||index>=gl_menu_shared_setting_count(c))return GL_INVALID;
    const uint32_t fields[]={AutoCal,UIScale,SettingCount,SettingCount+1,SettingCount+2,SettingCount+3,SettingCount+4};
    return gl_setting_at(c,fields[index],out);
}
uint32_t GL_CALL gl_menu_tab_count(const gl_context* c){return gl_gameplay_context_count(c);}
int32_t GL_CALL gl_menu_tab_at(const gl_context* c,uint32_t index,gl_menu_tab* out){
    if(!c||!out||index>=gl_menu_tab_count(c))return GL_INVALID;
    *out={};out->available=1;
    const auto& [id,state]=*std::next(c->gameplay_contexts.begin(),index);
    out->id=uint64_t(id)+1;out->context_id=id;out->active=c->winning_context()==id;out->available=state.available;
    out->label=state.label.c_str();out->description=state.description.c_str();return GL_OK;
}
uint32_t GL_CALL gl_menu_tab_setting_count(const gl_context* c,uint64_t tab){
    if(!c)return 0;if(tab==GL_TAB_GENERAL)return gl_menu_shared_setting_count(c);
    if(tab==GL_TAB_CAMERA)return 0; // reserved legacy implicit tab, never exposed
    return tab<=uint64_t(UINT32_MAX)+1&&c->gameplay_contexts.count(static_cast<uint32_t>(tab-1))?profile_count():0;
}
int32_t GL_CALL gl_menu_tab_setting_at(const gl_context* c,uint64_t tab,uint32_t index,gl_setting_info* out){
    if(!c||!out||index>=gl_menu_tab_setting_count(c,tab))return GL_INVALID;
    if(tab==GL_TAB_GENERAL)return gl_menu_shared_setting_at(c,index,out);
    const auto field=profile_field(index);
    auto it=c->gameplay_contexts.find(static_cast<uint32_t>(tab-1));
    auto flat=gl_setting_count()+static_cast<uint32_t>(std::distance(c->gameplay_contexts.begin(),it))*profile_count()+index;
    const auto result=gl_setting_at(c,flat,out);
    if(result==GL_OK){
        const auto effective=c->effective_settings(it->first);
        const bool custom=effective[Acceleration]!=0;
        const bool combined=effective[Space]==double(GL_SPACE_LOCAL_YAW_ROLL);
        out->label=gl_text(c,field==InvertX&&combined?"InvertYaw":custom&&field==SensX?"SlowSensitivityX":
            custom&&field==SensY?"SlowSensitivityY":definitions[field].key);
    }
    return result;
}
uint32_t GL_CALL gl_setting_count(void) {return SettingCount+5;}
uint32_t GL_CALL gl_menu_setting_count(const gl_context* c) {return c?gl_setting_count()+profile_count()*gl_gameplay_context_count(c):0;}
int32_t GL_CALL gl_setting_at(const gl_context* c,uint32_t index,gl_setting_info* out) {
    if(!c||!out||index>=gl_menu_setting_count(c))return GL_INVALID;
    *out={};out->visible=out->available=1;
    const bool connected=std::any_of(c->endpoints.begin(),c->endpoints.end(),[&](const auto& e){
        return e.info.connected&&!e.motion_companion&&e.info.physical_id==c->selected;
    });
    if(index>=SettingCount&&index<gl_setting_count()) {
        auto i=index-SettingCount;out->id=action_ids[i];out->label=gl_text(c,action_keys[i]);out->type=GL_SETTING_ACTION;
        out->description=gl_text(c,i==0?"description.calibration":i==1?"description.calibration.cancel":i==2?"description.reset":i==4?"description.recommended":"description.save");out->unit="";
        if(i==4)out->visible=out->available=gl_has_recommended_settings(c);
        if(i<2){
            auto* e=c->endpoint(c->active);out->available=e&&e->info.source==GL_SOURCE_SDL&&
                (i==1||(e->last_accel&&c->now>=e->last_accel&&c->now-e->last_accel<150000000));
            const auto state=e?e->motion.diagnostics.calibration_state:GL_CAL_IDLE;
            const bool pending=state==GL_CAL_COUNTDOWN||state==GL_CAL_COLLECTING||state==GL_CAL_MOVING;
            out->visible=(!e||e->info.source!=GL_SOURCE_STEAM)&&(i==0?!pending:pending);
            out->available=out->available&&out->visible;
        }
        if(i==3)out->visible=out->available=0; // legacy explicit save API; frontends auto-save edits
        out->available=out->available&&connected;
        return GL_OK;
    }
    Settings effective{};const Settings* values=&c->settings;const GameplayContext* context=nullptr;uint32_t context_id=0;
    if(index>=gl_setting_count()){
        const auto offset=index-gl_setting_count();
        const auto& [id,state]=*std::next(c->gameplay_contexts.begin(),offset/profile_count());
        index=profile_field(offset%profile_count());context=&state;context_id=id;effective=c->device_settings(id);values=&effective;
    }
    const auto& v=*values;
    const auto& d=definitions[index];out->id=d.id;out->label=gl_text(c,d.key);
    out->description=translate(c->language,d.description);
    out->unit=d.unit;out->type=d.type;out->value=v[index];out->minimum=d.min;
    out->maximum=d.max;out->step=d.step;out->default_value=d.def;
    auto caps=c->capabilities();
    int space=static_cast<int>(v[Space]);
    if(index==LocalAngle)out->visible=space==GL_SPACE_LOCAL_ADVANCED;
    if(index==LocalContribution)out->visible=space==GL_SPACE_LOCAL_ADVANCED||space==GL_SPACE_LOCAL_YAW_ROLL;
    if(index==InvertRoll)out->visible=out->available=space==GL_SPACE_LOCAL_YAW_ROLL;
    if(index>=Button&&index<=Stick){uint32_t cap=index==Button?caps.buttons:family_cap(caps,index);out->visible=out->available=cap!=0;}
    if(index==Threshold){
        const int stick=static_cast<int>(v[Stick]);
        out->visible=out->available=stick!=GL_SIDE_OFF&&side_available(caps.sticks,stick);
    }
    if(index==Trigger)out->visible=out->available=c->trigger_inputs().available!=0&&int(v[Activation])!=GL_ALWAYS;
    if(index==TriggerThreshold)out->visible=out->available=v[Trigger]!=0&&c->trigger_inputs().available!=0&&int(v[Activation])!=GL_ALWAYS;
    if(index==AutoCal){auto* e=c->endpoint(c->active);out->visible=!e||e->info.source!=GL_SOURCE_STEAM;out->available=e&&e->info.source==GL_SOURCE_SDL&&
        e->last_accel&&c->now>=e->last_accel&&c->now-e->last_accel<150000000;}
    const auto flick_inputs=c->flick_inputs();
    bool stick_hook=(c->host_capabilities&GL_HOST_NATIVE_STICK_SUPPRESSION)!=0;
    bool pad_hook=(c->host_capabilities&GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)!=0;
    if(index==FlickMode||index==FlickMs||index==FlickContext||(index>=FlickStyle&&index<=FlickExponent)||(index>=FlickSmoothSpeed&&index<=FlickPadOuter)) {
        const bool cursor=c->effective_output_target(context_id)==GL_OUTPUT_CURSOR;
        out->visible=(stick_hook||pad_hook)&&!cursor;out->available=out->visible&&
            ((stick_hook&&(flick_inputs.available&GL_FLICK_INPUT_STICK))||(pad_hook&&(flick_inputs.available&GL_FLICK_INPUT_TOUCHPAD)));
    }
    if(index==FlickSnapStrength)out->visible=out->visible&&v[FlickSnap]!=0;
    if(index==FlickSmoothAngle)out->visible=out->available=0; // migrated per-frame angle limit; use angular speed
    const int flick_mode=int(v[FlickMode]);
    if(index==FlickStickInner||index==FlickStickOuter)out->visible=out->visible&&stick_hook&&
        (flick_inputs.available&GL_FLICK_INPUT_STICK)&&(flick_mode==GL_FLICK_ON||flick_mode==GL_FLICK_BOTH);
    if(index==FlickPadInner||index==FlickPadOuter)out->visible=out->visible&&pad_hook&&
        (flick_inputs.available&GL_FLICK_INPUT_TOUCHPAD)&&(flick_mode==GL_FLICK_TOUCHPAD||flick_mode==GL_FLICK_BOTH);
    if(index==FlickMs||index==FlickSnap||index==FlickSnapStrength||index==FlickForward||index==FlickExponent)
        out->visible=out->visible&&int(v[FlickStyle])!=GL_FLICK_STYLE_ROTATE_ONLY;
    if(index==FlickSmoothing||index==FlickSmoothSpeed)out->visible=out->visible&&int(v[FlickStyle])!=GL_FLICK_STYLE_PIVOT_ONLY;
    if(index==TemporaryInvertButton||index==TrackballButton)out->visible=out->available=0; // retired independent bindings
    const bool hold_disable=int(v[Activation])==GL_HOLD_DISABLE;
    if(index==HoldInvert||index==HoldTrackball)out->visible=out->available=hold_disable;
    if(index==TemporaryInvertAxes)out->visible=out->available=hold_disable&&v[HoldInvert]!=0;
    if(index==TrackballAxes||index==TrackballDecay)out->visible=out->available=hold_disable&&v[HoldTrackball]!=0;
    if(index==StickEffect)out->visible=out->available=0; // retired duplicate stick policy
    const auto group=gl_setting_advanced_group(d.id);
    if((group==GL_ADVANCED_SMOOTHING&&!v[Smoothing])||
       (group==GL_ADVANCED_ACCELERATION&&!v[Acceleration])||
       (group==GL_ADVANCED_FLICK&&flick_mode==GL_FLICK_OFF))out->visible=out->available=0;
    const bool camera_view=c->effective_output_target(context_id)!=GL_OUTPUT_CURSOR;
    if(index==RecenterButton)out->visible=out->available=camera_view&&(c->recenter||c->recenter_step);
    if(index==RecenterMs)out->visible=out->available=camera_view&&c->recenter_step&&v[RecenterButton]!=0;
    if(index==SteamMouse){
        const bool ready=c->virtual_mouse&&c->steam_input_available();
        out->visible=out->available=connected&&c->selected&&(ready||c->steam_mouse_device==c->selected);
    }
    if(index==ZoomCompensation)out->visible=out->available=camera_view&&context&&context->zoom_available;
    if(index==BlockLongPress){
        const bool hold=v[Activation]==double(GL_HOLD)||v[Activation]==double(GL_HOLD_DISABLE);
        const auto button=static_cast<unsigned>(v[Button]);
        out->visible=camera_view&&hold&&caps.buttons&&(c->host_capabilities&GL_HOST_LONG_PRESS_BLOCKING);
        out->available=out->visible&&button>=1&&button<=32&&(caps.buttons&(1u<<(button-1)));
    }
    if(index==UIScale)out->visible=out->available=0; // legacy saved value; panels always use automatic DPI/resolution scaling
    if(index==Enabled)out->visible=out->available=0; // compatibility alias; Activation owns Off
    if(index>=Button&&index<=BlockLongPress&&v[Activation]==double(GL_ALWAYS))out->visible=out->available=0;
    if(int(v[Activation])==GL_GYRO_OFF&&(gl_setting_is_activator(d.id)||
        (std::strncmp(d.id,"gyro.",5)==0&&index!=Activation)||index==HoldInvert||index==HoldTrackball))
        out->visible=out->available=0;
    if(context_selector(index))out->visible=out->available=0; // legacy API only; tabs own their settings
    if(context){
        out->id=context->setting_ids[index].c_str();out->label=context->setting_labels[index].c_str();
        out->description=context->setting_descriptions[index].c_str();
        // Registration owns the editable profile. Runtime observations only
        // decide whether it can drive output, not whether it can be configured.
    }
    else if(profile_setting(index))out->visible=out->available=0;
    if(index==InvertX&&space==GL_SPACE_LOCAL_YAW_ROLL){
        out->label=gl_text(c,"InvertYaw");out->description=gl_text(c,"description.InvertYaw");
    }
    if(v[Acceleration]!=0&&(index==SensX||index==SensY)){
        out->label=gl_text(c,index==SensX?"SlowSensitivityX":"SlowSensitivityY");
        out->description=gl_text(c,index==SensX?"description.SlowSensitivityX":"description.SlowSensitivityY");
    }
    out->available=out->available&&connected;
    if(index==Activation){
        // Capability belongs to the selected physical group, including its
        // associated SDL/Steam sensor. A temporary sample gap is not a missing gyro.
        out->available=out->available&&std::any_of(c->endpoints.begin(),c->endpoints.end(),[&](const auto& e){
            return e.info.connected&&e.info.physical_id==c->selected&&e.info.caps.gyro;
        });
    }
    return GL_OK;
}
int32_t GL_CALL gl_setting_get(const gl_context* c,const char* id,double* value) {
    if(!c||!value)return GL_INVALID;int index=setting_index(id);
    if(index>=0){if(index!=AutoCal)return GL_UNAVAILABLE;*value=c->settings[index];return GL_OK;}
    uint32_t context_id;int field;
    if(!context_setting(id,context_id,field)||!c->gameplay_contexts.count(context_id))return GL_INVALID;
    const auto saved=c->effective_settings(context_id);
    *value=field==Enabled?saved[Activation]!=double(GL_GYRO_OFF):saved[field];return GL_OK;
}
int32_t GL_CALL gl_setting_get_effective(const gl_context* c,const char* id,double* value) {
    const auto status=gl_setting_get(c,id,value);if(status!=GL_OK)return status;
    uint32_t context_id;int field;
    if(context_setting(id,context_id,field))*value=c->device_settings(context_id)[field];
    return GL_OK;
}
int32_t GL_CALL gl_setting_set(gl_context* c,const char* id,double value) try {
    int index=setting_index(id);if(!c||!std::isfinite(value))return GL_INVALID;
    if(index<0) {
        uint32_t context_id;int field;
        if(!context_setting(id,context_id,field)||!c->gameplay_contexts.count(context_id))return GL_INVALID;
        if(field==TemporaryInvertButton||field==TrackballButton||field==StickEffect)return GL_UNAVAILABLE;
        const auto& d=definitions[field];
        if(value<d.min||value>d.max||!valid_profile_value(field,value))return GL_INVALID;
        value=quantize(field,value);auto& saved=c->context_settings.at(context_id);
        const auto before=c->profile_snapshot();const auto current=c->effective_settings(context_id);
        auto pending=current;
        if(field==Enabled){
            if(!value)pending[Activation]=GL_GYRO_OFF;
            else if(int(pending[Activation])==GL_GYRO_OFF)pending[Activation]=GL_ALWAYS;
        }else pending[field]=value;
        pending[Enabled]=int(pending[Activation])!=GL_GYRO_OFF;
        // An explicitly edited contact replaces the legacy button alias shown
        // in that family. Device changes alone never alter saved preferences.
        if(field==Touchpad||field==StickTouch||field==GripTouch){
            const auto binding=static_cast<uint32_t>(current[Button]);
            const uint32_t family=field==Touchpad?GL_CONTACT_TOUCHPAD:field==StickTouch?GL_CONTACT_STICK:GL_CONTACT_GRIP;
            if(binding&&binding<=32&&c->button_contact(binding-1).family==family)pending[Button]=0;
        }
        if(field==FlickSmoothAngle)pending[FlickSmoothSpeed]=value*30;
        if(field==FlickStickInner&&value>=pending[FlickStickOuter])pending[FlickStickOuter]=std::min(1.0,value+.05);
        if(field==FlickStickOuter&&value<=pending[FlickStickInner])pending[FlickStickInner]=std::max(0.0,value-.05);
        if(field==FlickPadInner&&value>=pending[FlickPadOuter])pending[FlickPadOuter]=std::min(1.0,value+.05);
        if(field==FlickPadOuter&&value<=pending[FlickPadInner])pending[FlickPadInner]=std::max(0.0,value-.05);
        if(field==Acceleration||(field==SensX||field==SensY))apply_acceleration_preset(pending);
        if(field>=FastSensX&&field<=FastSpeed&&current[field]!=value)pending[Acceleration]=4;
        const bool custom_transition=pending[Acceleration]==4&&current[Acceleration]!=4;
        bool changed=false;auto link=c->profile_links.find(context_id);
        const auto inherited=custom_transition&&link!=c->profile_links.end()?c->effective_settings(link->second.parent):current;
        const int explicit_field=field==Enabled?Activation:field;
        for(int f=0;f<SettingCount;++f){
            // Derive inherited preset components without creating hidden overrides;
            // components of an explicitly selected local preset must still update.
            if(link!=c->profile_links.end()&&(field==SensX||field==SensY)&&
                f>=FastSensX&&f<=FastSpeed&&!link->second.overrides[f])continue;
            // Pin derived components that would change on entering Custom. Values
            // already equal to the parent's remain inherited, not hidden overrides.
            const bool preset_group=(field==Acceleration||(custom_transition&&current[f]!=inherited[f]))&&
                (f>=FastSensX&&f<=FastSpeed);
            if(f!=explicit_field&&!preset_group&&pending[f]==current[f])continue;
            if(link!=c->profile_links.end()&&!link->second.overrides[f]){link->second.overrides.set(f);changed=true;}
            changed|=saved[f]!=pending[f];saved[f]=pending[f];
        }
        if(!changed)return GL_OK;
        c->profiles_changed(before);
        if(link!=c->profile_links.end())c->emit(GL_EVENT_CONTEXT,6,0,context_id);
        return save_change(c);
    }
    const auto& d=definitions[index];if(value<d.min||value>d.max)return GL_INVALID;
    if(index!=AutoCal)return GL_UNAVAILABLE; // per-view values require context.<id> keys
    if(context_selector(index)&&value!=std::floor(value))return GL_INVALID;
    value=quantize(index,value);if(c->settings[index]==value)return GL_OK;
    c->settings[index]=value;c->emit(GL_EVENT_SETTING,index,0,value,id);
    if(index==Activation||(index>=Button&&index<=BlockLongPress)){for(auto& g:c->long_press_blockers)g.cancel();c->controls_primed=false;}
    return save_change(c);
} catch (...) {return GL_LIMIT;}
uint32_t GL_CALL gl_choice_count(const char* id) {
    int index=resolved_field(id);if(index<0||definitions[index].type!=GL_SETTING_ENUM)return 0;
    if(context_selector(index))return 0;
    return static_cast<uint32_t>(definitions[index].max)+1;
}
uint32_t GL_CALL gl_menu_choice_count(const gl_context* c,const char* id) {
    if(!c)return 0;
    uint32_t context_id;int field;if(context_setting(id,context_id,field)&&!c->gameplay_contexts.count(context_id))return 0;
    return context_selector(resolved_field(id))?1+gl_gameplay_context_count(c):gl_choice_count(id);
}
int32_t GL_CALL gl_choice_at(const gl_context* c,const char* id,uint32_t index,gl_choice* out) try {
    if(!c||!out||index>=gl_menu_choice_count(c,id))return GL_INVALID;
    int field=resolved_field(id);out->value=index;out->available=1;auto caps=c->capabilities();
    if(context_selector(field)) {
        if(!index){out->value=0;out->label=gl_text(c,"context.none");return GL_OK;}
        const auto& [context_id,state]=*std::next(c->gameplay_contexts.begin(),index-1);
        out->value=context_id;out->label=state.label.c_str();out->available=state.available;return GL_OK;
    }
    const char* key="off";
    if(field==Space){
        // Presentation order is independent of the persisted/public enum values.
        constexpr unsigned values[]={GL_SPACE_PLAYER,GL_SPACE_WORLD,GL_SPACE_PLAYER_LEAN,GL_SPACE_WORLD_LEAN,GL_SPACE_LASER_POINTER,
            GL_SPACE_LOCAL_YAW,GL_SPACE_LOCAL_ROLL,GL_SPACE_LOCAL_YAW_ROLL,GL_SPACE_LOCAL_ADVANCED};
        const char* keys[]={"space.player","space.world","space.player_lean","space.world_lean","space.laser","space.yaw","space.roll","space.yaw_roll","space.local"};
        out->value=values[index];key=keys[index];
        if(gravity_space(int(out->value))&&!caps.accelerometer)out->available=0;
    }
    if(field==Activation){
        constexpr unsigned values[]={GL_GYRO_OFF,GL_ALWAYS,GL_HOLD_DISABLE,GL_HOLD,GL_TOGGLE,GL_CONTEXT_ONLY,GL_OUTSIDE_CONTEXT};
        const char* keys[]={"off","mode.always","mode.hold_disable","mode.hold","mode.toggle","mode.context","mode.outside_context"};
        out->value=values[index];key=keys[index];
        if(values[index]==GL_CONTEXT_ONLY||values[index]==GL_OUTSIDE_CONTEXT)out->available=0;}
    if(field==Acceleration){const char* keys[]={"off","acceleration.low","acceleration.medium","acceleration.high","custom"};key=keys[index];
        // Custom describes an edited curve; it is not a selectable preset.
        if(index==4)out->available=0;}
    if(field==SteamMouse){const char* keys[]={"steam_mouse.pass","steam_mouse.block","steam_mouse.convert"};key=keys[index];}
    if(field==FlickStyle){const char* keys[]={"flick.full","flick.pivot_only","flick.rotate_only"};key=keys[index];}
    if(field==FlickSnap){const char* keys[]={"off","flick.snap90","flick.snap45"};key=keys[index];}
    if(field==AutoCal){const char* keys[]={"off","menus","anytime"};key=keys[index];
        if(index==GL_CAL_MENUS)out->available=(c->host_capabilities&GL_HOST_MENU_STATE)!=0;}
    if(field==FlickMode){const char* keys[]={"off","mode.outside_context","on","mode.context","flick.touchpad","flick.both"};key=keys[index];
        const auto inputs=c->flick_inputs();const bool pad=(inputs.available&GL_FLICK_INPUT_TOUCHPAD)!=0;
        if(index==GL_FLICK_ON)key="flick.stick";
        if(index==GL_FLICK_CONTEXT_ONLY||index==GL_FLICK_OUTSIDE_CONTEXT)out->available=0;
        if(index){
            uint32_t mode_id;int mode_field;
            const bool cursor=c->effective_output_target(context_setting(id,mode_id,mode_field)?mode_id:0)==GL_OUTPUT_CURSOR;
            const bool stick=(inputs.available&GL_FLICK_INPUT_STICK)&&(c->host_capabilities&GL_HOST_NATIVE_STICK_SUPPRESSION);
            const bool touchpad=pad&&(c->host_capabilities&GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
            out->available=out->available&&!cursor&&
                (index==GL_FLICK_ON?stick:index==GL_FLICK_TOUCHPAD?touchpad:index==GL_FLICK_BOTH?stick&&touchpad:false);
        }}
    if((field>=Touchpad&&field<=Stick)||field==Trigger){
        const char* keys[]={"off","left","right","either","both"};key=keys[index];
        const auto available=field==Trigger?c->trigger_inputs().available:family_cap(caps,field);out->available=side_available(available,index);
        if(field==Trigger&&(index==GL_SIDE_LEFT||index==GL_SIDE_RIGHT)){out->label=gl_get_trigger_label(c,index==GL_SIDE_LEFT?GL_LEFT:GL_RIGHT);return GL_OK;}
        if(field==Trigger&&(index==GL_SIDE_EITHER||index==GL_SIDE_BOTH)){
            auto& label=c->trigger_pair_labels[index-GL_SIDE_EITHER];
            const auto composed=std::string(gl_get_trigger_label(c,GL_LEFT))+gl_text(c,index==GL_SIDE_EITHER?"button.or":"button.and")+gl_get_trigger_label(c,GL_RIGHT);
            if(label!=composed)label=composed;out->label=label.c_str();return GL_OK;
        }
        // A single surface has no left/right distinction. Keep its persisted
        // EITHER value while presenting the meaningful Off/On pair.
        if(field==Touchpad&&available==GL_SINGLE&&index==GL_SIDE_EITHER)key="on";
    }
    if(field==TemporaryInvertAxes||field==TrackballAxes){const char* keys[]={"axes.x","axes.y","axes.both"};key=keys[index];}
    if(field==StickEffect){const char* keys[]={"off","stick.disables","stick.enables"};key=keys[index];}
    if(field==Button||field==RecenterButton||field==TemporaryInvertButton||field==TrackballButton){
        if(index>32) {
            constexpr unsigned pairs[][2]={{9,10},{7,8},{17,16},{19,18}};
            auto pair=pairs[(index-33)/2];auto& label=c->button_pair_labels[index-33];
            std::string composed=std::string(gl_get_button_label(c,pair[0]))+
                (index%2?gl_text(c,"button.or"):gl_text(c,"button.and"))+gl_get_button_label(c,pair[1]);
            if(label!=composed)label=std::move(composed);out->label=label.c_str();
        } else out->label=index?gl_get_button_label(c,index-1):gl_text(c,"off");
        // Keep legacy IDs readable/loadable, but offer only individual buttons.
        out->available=!index||(index<=32&&button_matches(caps.buttons,index)&&!c->button_contact(index-1).family);return GL_OK;
    }
    out->label=gl_text(c,key);return GL_OK;
}catch(...){return GL_LIMIT;}
const char* GL_CALL gl_choice_description(const gl_context* c,const char* id,double value) {
    if(!c||!std::isfinite(value)||value!=std::floor(value)||value<0||value>UINT32_MAX)return "";
    const int field=resolved_field(id);const auto v=static_cast<uint32_t>(value);const char* key=nullptr;
    if(field==Space&&v<9){const char* keys[]={"choice.space.player","choice.space.yaw","choice.space.roll","choice.space.world","choice.space.yaw_roll","choice.space.local","choice.space.laser","choice.space.player_lean","choice.space.world_lean"};key=keys[v];}
    if(field==Activation&&v<7){const char* keys[]={"choice.activation.always",nullptr,nullptr,"choice.activation.hold","choice.activation.toggle","choice.activation.hold_disable","choice.activation.off"};key=keys[v];}
    if(field==Acceleration&&v<5){const char* keys[]={"choice.acceleration.off","choice.acceleration.low","choice.acceleration.medium","choice.acceleration.high","choice.acceleration.custom"};key=keys[v];}
    if(field==FlickStyle&&v<3){const char* keys[]={"choice.flick.full","choice.flick.pivot_only","choice.flick.rotate_only"};key=keys[v];}
    if(field==FlickSnap&&v<3){const char* keys[]={"choice.flick.snap_off","choice.flick.snap90","choice.flick.snap45"};key=keys[v];}
    if(field==RecenterButton&&v<=32)key=v?"description.RecenterButton":"choice.side.off";
    if(field==SteamMouse&&v<3){const char* keys[]={"choice.steam_mouse.pass","choice.steam_mouse.block","choice.steam_mouse.convert"};key=keys[v];}
    if(field==AutoCal&&v<3){const char* keys[]={"choice.calibration.off","choice.calibration.menus","choice.calibration.anytime"};key=keys[v];}
    if(field==FlickMode){if(v==GL_FLICK_OFF)key="choice.flick.off";if(v==GL_FLICK_ON)key="choice.flick.on";
        if(v==GL_FLICK_TOUCHPAD)key="choice.flick.touchpad";if(v==GL_FLICK_BOTH)key="choice.flick.both";}
    if(field>=Touchpad&&field<=Stick&&v<5){const char* keys[]={"choice.side.off","choice.side.left","choice.side.right","choice.side.either","choice.side.both"};key=keys[v];}
    if(field==Touchpad&&c->capabilities().touchpads==GL_SINGLE&&v==GL_SIDE_EITHER)key="choice.touchpad.single_on";
    if(field==Button&&v<=40)key=v?"choice.button.on":"choice.side.off";
    if(field==Trigger&&v<5){const char* keys[]={"choice.side.off","choice.trigger.left","choice.trigger.right","choice.side.either","choice.side.both"};key=keys[v];}
    if((field==TemporaryInvertAxes||field==TrackballAxes)&&v<3){const char* keys[]={"choice.axes.x","choice.axes.y","choice.axes.both"};key=keys[v];}
    if(field==TemporaryInvertButton&&v<=32)key=v?"description.TemporaryInvertButton":"choice.side.off";
    if(field==TrackballButton&&v<=32)key=v?"description.TrackballButton":"choice.side.off";
    if(field==StickEffect&&v<3){const char* keys[]={"choice.side.off","choice.stick.disables","choice.stick.enables"};key=keys[v];}
    return key?gl_text(c,key):"";
}
int32_t GL_CALL gl_action(gl_context* c,const char* id) {
    if(!c||!id)return GL_INVALID;
    if(std::strcmp(id,"calibration.begin")==0)return gl_begin_calibration(c);
    if(std::strcmp(id,"calibration.cancel")==0){gl_cancel_calibration(c);return GL_OK;}
    if(std::strcmp(id,"settings.reset")==0){gl_reset_settings(c);return c->settings_save_result;}
    if(std::strcmp(id,"settings.recommended")==0)return gl_apply_recommended_settings(c);
    if(std::strcmp(id,"settings.save")==0)return c->settings_path.empty()?GL_UNAVAILABLE:gl_save_settings(c,c->settings_path.c_str());
    return GL_INVALID;
}
void GL_CALL gl_reset_settings(gl_context* c) try {
    if(!c)return;
    const auto before=c->profile_snapshot();
    {
    SettingsBatch batch(c);
    gl_setting_set(c,definitions[AutoCal].id,definitions[AutoCal].def);
    c->profile_links.clear();
    for(auto& [id,profile]:c->context_settings)profile=defaults(); // including retired modes
    c->profiles_changed(before);c->emit(GL_EVENT_CONTEXT,6);
    }
    c->settings_save_result=save_change(c);
} catch (...) {if(c)c->settings_save_result=GL_LIMIT;}
int32_t GL_CALL gl_set_language(gl_context* c,const char* language) try {
    if(!c||!language)return GL_INVALID;
    if(c->language==language)return GL_OK;
    for(const char* known:{"en","fr","de","es","it","pt"})if(std::strcmp(language,known)==0){
        c->language=language;
        for(auto& [id,state]:c->gameplay_contexts) {
            for(int field=0;field<SettingCount;++field)if(profile_setting(field)){
                state.setting_labels[field]=state.label+" / "+gl_text(c,definitions[field].key);
                state.setting_descriptions[field]=gl_text(c,definitions[field].description);
            }
        }
        c->emit(GL_EVENT_CONTEXT,2);return save_change(c);}
    return GL_INVALID;
} catch (...) {return GL_LIMIT;}
const char* GL_CALL gl_text(const gl_context* c,const char* key) {return translate(c?std::string_view(c->language):std::string_view("en"),key);}
const char* GL_CALL gl_get_language(const gl_context* c){return c?c->language.c_str():"en";}
int32_t GL_CALL gl_set_menu_key(gl_context* c,uint32_t key) try {
    if(!c||key>24)return GL_INVALID;
    if(c->menu_key==key)return GL_OK;
    c->menu_key=key;c->emit(GL_EVENT_SETTING,0,0,key,"ui.menu_key");return save_change(c);
}catch(...){return GL_LIMIT;}
uint32_t GL_CALL gl_get_menu_key(const gl_context* c){return c?c->menu_key:0;}
int32_t GL_CALL gl_set_gamepad_menu_shortcut(gl_context* c,uint32_t enabled) try {
    if(!c||enabled>1)return GL_INVALID;
    if(c->gamepad_menu_shortcut==bool(enabled))return GL_OK;
    c->gamepad_menu_shortcut=enabled!=0;c->menu_chord_armed=false;
    c->emit(GL_EVENT_SETTING,0,0,enabled,"ui.gamepad_menu_shortcut");return save_change(c);
}catch(...){return GL_LIMIT;}
uint32_t GL_CALL gl_get_gamepad_menu_shortcut(const gl_context* c){return c&&c->gamepad_menu_shortcut;}
const char* GL_CALL gl_get_settings_path(const gl_context* c){return c?c->settings_path.c_str():"";}
int32_t GL_CALL gl_set_settings_path(gl_context* c,const char* path) try {
    if(!c||!path)return GL_INVALID;if(c->settings_path!=path){c->settings_path=path;c->settings_save_result=GL_OK;}return GL_OK;
}catch(...){return GL_LIMIT;}
int32_t GL_CALL gl_get_settings_save_result(const gl_context* c){
    return !c?GL_INVALID:c->settings_save_result!=GL_OK?c->settings_save_result:c->settings_path.empty()?GL_UNAVAILABLE:GL_OK;
}
int32_t GL_CALL gl_load_settings(gl_context* c,const char* path) try {
    if(!c||!path||!*path)return GL_INVALID;
    std::string remembered_path(path);
    std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path)));if(!file)return GL_IO_ERROR;
    std::map<std::string,std::string> values;std::string line;
    while(std::getline(file,line)) {
        if(line.size()>4096)return GL_LIMIT; // bound individual records, not the number of modes
        line=trim(line);if(line.empty()||line[0]=='#'||line[0]==';')continue;
        auto pos=line.find('=');if(pos==std::string::npos)return GL_INVALID;
        auto key=trim(line.substr(0,pos));if(key.empty()||values.count(key))return GL_INVALID;
        values[key]=trim(line.substr(pos+1));
    }
    if(file.bad())return GL_IO_ERROR;
    const auto schema=values.find("schema");
    if(schema==values.end())return GL_INVALID;
    if(const auto status=schema_status(schema->second);status!=GL_OK)return status;
    values.erase(schema);
    std::string language=c->language;
    if(auto it=values.find("ui.language");it!=values.end()){
        if(!known_language(it->second.c_str()))return GL_INVALID;
        language=it->second;values.erase(it);
    }
    uint32_t menu_key=10; // An absent key uses F10; an empty value disables it.
    if(auto it=values.find("ui.menu_key");it!=values.end()){
        const auto& key=it->second;menu_key=0;
        if(!key.empty()){
            if(key.size()<2||key.size()>3||(key[0]!='F'&&key[0]!='f')||key[1]<'1'||key[1]>'9')return GL_INVALID;
            const auto parsed=std::from_chars(key.data()+1,key.data()+key.size(),menu_key);
            if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+key.size()||menu_key<1||menu_key>24)return GL_INVALID;
        }
        values.erase(it);
    }
    bool gamepad_menu_shortcut=true;
    if(auto it=values.find("ui.gamepad_menu_shortcut");it!=values.end()){
        if(it->second!="0"&&it->second!="1")return GL_INVALID;
        gamepad_menu_shortcut=it->second=="1";values.erase(it);
    }
    double automatic_calibration=c->settings[AutoCal];
    auto profiles=c->context_settings;std::map<std::string,std::string> unknown;
    auto links=c->profile_links;std::map<uint32_t,uint32_t> parents;
    std::map<uint32_t,std::map<int,double>> edits;
    std::map<uint32_t,std::bitset<SettingCount>> explicitly_inherited;
    for(const auto& [key,text]:values) {
        uint32_t context_id;int field;
        if(key.starts_with("context.")&&key.ends_with(".inherit")){
            const auto probe=key.substr(0,key.size()-8)+".sensitivity_x";double parent;
            if(!context_setting(probe.c_str(),context_id,field)||!parse_number(text,parent)||
                parent<0||parent>UINT32_MAX||parent!=std::floor(parent))return GL_INVALID;
            parents[context_id]=static_cast<uint32_t>(parent);continue;
        }
        if(context_setting(key.c_str(),context_id,field)) {
            if(!persisted_profile_setting(field))return GL_INVALID;
            if(text=="inherit"){explicitly_inherited[context_id].set(field);continue;}
            double number;const auto& d=definitions[field];
            if(!parse_number(text,number)||number<d.min||number>d.max||!valid_profile_value(field,number))return GL_INVALID;
            edits[context_id][field]=quantize(field,number);continue;
        }
        int field_index=setting_index(key.c_str());
        if(field_index<0){unknown[key]=text;continue;}
        if(field_index!=AutoCal)return GL_INVALID; // settings always belong to a named view
        double number;const auto& d=definitions[AutoCal];
        if(!parse_number(text,number)||number<d.min||number>d.max)return GL_INVALID;
        automatic_calibration=quantize(AutoCal,number);
    }
    for(const auto& [id,parent]:parents){profiles[id]=defaults();links.erase(id);}
    for(const auto& [id,fields]:edits){profiles[id]=defaults();links.erase(id);}
    for(const auto& [id,fields]:edits){
        auto [it,inserted]=profiles.try_emplace(id,defaults());
        for(auto [field,value]:fields)it->second[field]=value;
    }
    for(const auto& [id,parent]:parents)if(parent){
        auto& link=links[id];link.parent=parent;
        if(auto fields=edits.find(id);fields!=edits.end())for(const auto& [f,value]:fields->second)link.overrides.set(f);
    }
    for(const auto& [id,fields]:explicitly_inherited){
        // An explicit marker is meaningful only in a linked row from this file.
        if(!parents.count(id)||!parents.at(id)||!links.count(id))return GL_INVALID;
    }
    if(!valid_links(profiles,links))return GL_INVALID;
    // Selecting a preset in a partial inherited row has the same meaning as
    // selecting it through the API: own the curve, derive omitted components.
    for(auto& [id,link]:links){
        const auto fields=edits.find(id);if(fields==edits.end())continue;
        const auto choice=fields->second.find(Acceleration);
        if(choice==fields->second.end()||choice->second>=4)continue;
        auto preset=resolve_profile(profiles,links,id);preset[Acceleration]=choice->second;apply_acceleration_preset(preset);
        for(int field=FastSensX;field<=FastSpeed;++field)if(!fields->second.count(field)&&!explicitly_inherited[id][field]){
            profiles.at(id)[field]=preset[field];link.overrides.set(field);
        }
    }
    for(auto& [id,profile]:profiles){
        profile[Enabled]=int(profile[Activation])!=GL_GYRO_OFF;
        if(links.count(id))continue; // Partial inherited rows resolve against the parent below.
        if(profile[FlickStickInner]>=profile[FlickStickOuter]||profile[FlickPadInner]>=profile[FlickPadOuter])return GL_INVALID;
        if(profile[Acceleration]<4){
            auto preset=profile;apply_acceleration_preset(preset);
            if(auto fields=edits.find(id);fields!=edits.end())
                for(int field=FastSensX;field<=FastSpeed;++field)
                    if(!fields->second.count(field))profile[field]=preset[field];
            for(int field=FastSensX;field<=FastSpeed;++field)if(std::abs(profile[field]-preset[field])>1e-8){
                profile[Acceleration]=4;break; // Preserve manual INI curve edits.
            }
        }
    }
    // Transaction: no setting changes on malformed or newer files.
    const auto before=c->profile_snapshot();
    SettingsBatch batch(c);
    gl_setting_set(c,definitions[AutoCal].id,automatic_calibration);
    c->context_settings=std::move(profiles);c->profile_links=std::move(links);
    c->profiles_changed(before);c->emit(GL_EVENT_CONTEXT,6);
    gl_set_language(c,language.c_str());
    gl_set_menu_key(c,menu_key);
    gl_set_gamepad_menu_shortcut(c,gamepad_menu_shortcut);
    c->unknown_settings=std::move(unknown);c->settings_path=std::move(remembered_path);c->settings_save_result=GL_OK;return GL_OK;
} catch (...) {return GL_IO_ERROR;}
static int32_t write_settings(gl_context* c,const char* path) try {
    if(!c||!path||!*path)return GL_INVALID;
    std::string remembered_path(path);
    auto destination=std::filesystem::path(reinterpret_cast<const char8_t*>(path)),temporary=destination;temporary+=".tmp";
    // Never overwrite an unsupported/missing format, even after a failed load.
    if(std::filesystem::exists(destination)) {
        std::ifstream existing(destination);if(!existing)return GL_IO_ERROR;
        std::string line;bool found=false;
        while(std::getline(existing,line)) {
            line=trim(line);if(line.empty()||line[0]=='#'||line[0]==';')continue;
            auto pos=line.find('=');if(pos!=std::string::npos&&trim(line.substr(0,pos))=="schema") {
                if(found)return GL_INVALID;found=true;
                if(const auto status=schema_status(trim(line.substr(pos+1)));status!=GL_OK)return status;
            }
        }
        if(existing.bad())return GL_IO_ERROR;if(!found)return GL_INVALID;
    }
    std::ofstream file(temporary,std::ios::trunc);if(!file)return GL_IO_ERROR;
    file.imbue(std::locale::classic());file<<"# GyroLib settings\nschema=" GL_SETTINGS_SCHEMA "\n"
        "# Menu shortcut: F1..F24, or leave ui.menu_key= empty to disable. Restart after editing.\n"
        "ui.menu_key=";
    if(c->menu_key)file<<'F'<<c->menu_key;
    file<<"\n# Back + Start opens the panel. Set 0 to disable.\nui.gamepad_menu_shortcut="<<int(c->gamepad_menu_shortcut);
    file<<"\nui.language="<<c->language<<'\n'<<std::setprecision(12);
    file<<"calibration.automatic="<<c->settings[AutoCal]<<'\n';
    for(const auto& [id,profile]:c->context_settings){
        file<<"\n# View "<<id<<'\n';
        auto link=c->profile_links.find(id);
        if(link!=c->profile_links.end())file<<"context."<<id<<".inherit="<<link->second.parent<<'\n';
        for(int field=0;field<SettingCount;++field)
            if(persisted_profile_setting(field)&&
                (link==c->profile_links.end()||link->second.overrides[field]))file<<profile_key(id,field)<<'='<<profile[field]<<'\n';
        // A local preset normally derives omitted curve components on load.
        // Preserve components explicitly restored to the parent, even when equal.
        if(link!=c->profile_links.end()&&link->second.overrides[Acceleration]&&profile[Acceleration]<4)
            for(int field=FastSensX;field<=FastSpeed;++field)if(!link->second.overrides[field])
                file<<profile_key(id,field)<<"=inherit\n";
    }
    for(const auto& [key,value]:c->unknown_settings)file<<key<<'='<<value<<'\n';
    file.flush();if(!file)return GL_IO_ERROR;file.close();if(!file)return GL_IO_ERROR;
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return GL_IO_ERROR;
#else
    std::error_code error;std::filesystem::rename(temporary,destination,error);if(error)return GL_IO_ERROR;
#endif
    c->settings_path=std::move(remembered_path);return GL_OK;
} catch (...) {return GL_IO_ERROR;}
int32_t GL_CALL gl_save_settings(gl_context* c,const char* path){
    const auto result=write_settings(c,path);if(c)c->settings_save_result=result;return result;
}
}
