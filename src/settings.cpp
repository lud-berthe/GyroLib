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
void migrate_activation(Settings& values){
    if(values[Activation]==double(GL_ALWAYS)){
        if(values[ShortPress])values[Activation]=GL_HOLD_DISABLE;
        for(int i=Button;i<=Stick;++i)if(values[i]!=0){values[Activation]=GL_HOLD_DISABLE;break;}
    }
}
bool profile_setting(int field){return field>=0&&field<SettingCount&&field!=AutoCal&&field!=UIScale&&field!=GyroContext&&field!=FlickContext;}
std::string profile_key(uint32_t id,int field){
    return "context."+std::to_string(id)+"."+(field==SensX?"sensitivity_x":field==SensY?"sensitivity_y":definitions[field].id);
}
Settings migrate_profile(const Settings& source,uint32_t id){
    auto v=source;
    // Before schema 6, named modes already had absolute axes with 2.5 defaults.
    if(id){v[SensX]=v[SensY]=2.5;}
    const int activation=static_cast<int>(v[Activation]),flick=static_cast<int>(v[FlickMode]);
    if(activation==GL_CONTEXT_ONLY||activation==GL_OUTSIDE_CONTEXT){
        const auto selected=static_cast<uint32_t>(v[GyroContext]);
        const bool allowed=selected&&(activation==GL_CONTEXT_ONLY?selected==id:selected!=id);
        v[Enabled]=v[Enabled]&&allowed;v[Activation]=GL_ALWAYS;
    }
    if(flick==GL_FLICK_CONTEXT_ONLY||flick==GL_FLICK_OUTSIDE_CONTEXT){
        const auto selected=static_cast<uint32_t>(v[FlickContext]);
        v[FlickMode]=selected&&(flick==GL_FLICK_CONTEXT_ONLY?selected==id:selected!=id)?GL_FLICK_ON:GL_FLICK_OFF;
    }
    migrate_activation(v);v[GyroContext]=v[FlickContext]=0;return v;
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
    return c->settings_batch_depth||c->settings_path.empty()?GL_OK:gl_save_settings(c,c->settings_path.c_str());
}
bool known_language(const char* language){
    if(!language)return false;
    for(const char* known:{"en","fr","de","es","it","pt"})if(std::strcmp(language,known)==0)return true;
    return false;
}
const char* action_ids[]={"calibration.begin","calibration.cancel","settings.reset","settings.save"};
const char* action_keys[]={"calibration.action","cancel","reset","save"};
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
bool context_setting(const char* key,uint32_t& id,int& field) {
    if(!key)return false;std::string_view text(key),prefix="context.";
    if(!text.starts_with(prefix))return false;text.remove_prefix(prefix.size());
    auto dot=text.find('.');if(dot==std::string_view::npos)return false;
    auto number=text.substr(0,dot),suffix=text.substr(dot);
    if(number.empty()||number.front()=='0')return false;
    auto result=std::from_chars(number.data(),number.data()+number.size(),id);
    if(result.ec!=std::errc{}||result.ptr!=number.data()+number.size()||!id)return false;
    if(suffix==".sensitivity_x")field=SensX;else if(suffix==".sensitivity_y")field=SensY;
    else {std::string base(suffix.substr(1));field=setting_index(base.c_str());}
    return profile_setting(field);
}
bool context_selector(int index){return index==GyroContext||index==FlickContext;}
uint32_t profile_count(){uint32_t count=0;for(int i=0;i<SettingCount;++i)count+=profile_setting(i);return count;}
int profile_field(uint32_t index){
    constexpr int first[]={Activation,Space,LocalAngle,LocalContribution,SensX,SensY,InvertX,InvertRoll,InvertY,
        Button,Trigger,TriggerThreshold,Touchpad,StickTouch,GripTouch,Stick,Threshold,ShortPress};
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
void gl_context::migrate_contact_bindings(){
    bool changed=false;
    const auto migrate=[&](Settings& values,uint32_t profile){
        const auto button=static_cast<uint32_t>(values[Button]);if(!button||button>32)return;
        const auto alias=button_contact(button-1);if(!alias.family)return;
        const int field=alias.family==GL_CONTACT_TOUCHPAD?Touchpad:alias.family==GL_CONTACT_STICK?StickTouch:GripTouch;
        const int side=alias.side==GL_LEFT?GL_SIDE_LEFT:alias.side==GL_RIGHT?GL_SIDE_RIGHT:GL_SIDE_EITHER;
        const int previous=static_cast<int>(values[field]);
        // Families are ORed: preserve the exact predicate when a saved button
        // alias and a dedicated family were both enabled.
        const int combined=previous==GL_SIDE_OFF||previous==GL_SIDE_BOTH||previous==side?side:GL_SIDE_EITHER;
        values[Button]=0;values[field]=combined;changed=true;
        const auto button_key=profile?profile_key(profile,Button):std::string(definitions[Button].id);
        const auto family_key=profile?profile_key(profile,field):std::string(definitions[field].id);
        emit(GL_EVENT_SETTING,Button,0,0,button_key.c_str());emit(GL_EVENT_SETTING,field,0,combined,family_key.c_str());
    };
    for(auto& [id,values]:context_settings)migrate(values,id);
    if(changed){controls_primed=false;for(auto& gate:gates)gate.cancel();save_change(this);}
}
extern "C" {
uint32_t GL_CALL gl_setting_advanced_group(const char* id){
    const int field=resolved_field(id);
    if(field==SmoothThreshold||field==Tightening)return GL_ADVANCED_SMOOTHING;
    if(field>=FastSensX&&field<=FastSpeed)return GL_ADVANCED_ACCELERATION;
    if(field==FlickMs||(field>=FlickStyle&&field<=FlickExponent)||(field>=FlickSmoothSpeed&&field<=FlickPadOuter))return GL_ADVANCED_FLICK;
    if(field==HoldInvert||field==HoldTrackball||field==TemporaryInvertAxes||field==TrackballAxes||field==TrackballDecay)return GL_ADVANCED_HOLD_DISABLE;
    return GL_ADVANCED_NONE;
}
uint32_t GL_CALL gl_setting_is_advanced(const char* id){return gl_setting_advanced_group(id)!=GL_ADVANCED_NONE;}
uint32_t GL_CALL gl_setting_is_activator(const char* id){
    const int field=resolved_field(id);
    return (field>=Button&&field<=ShortPress)||field==Trigger||field==TriggerThreshold;
}
uint32_t GL_CALL gl_menu_shared_setting_count(const gl_context* c){return c?6:0;}
int32_t GL_CALL gl_menu_shared_setting_at(const gl_context* c,uint32_t index,gl_setting_info* out){
    if(!c||!out||index>=gl_menu_shared_setting_count(c))return GL_INVALID;
    const uint32_t fields[]={AutoCal,UIScale,SettingCount,SettingCount+1,SettingCount+2,SettingCount+3};
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
        const bool custom=c->context_settings.at(it->first)[Acceleration]!=0;
        const bool combined=c->context_settings.at(it->first)[Space]==double(GL_SPACE_LOCAL_YAW_ROLL);
        out->label=gl_text(c,field==InvertX&&combined?"InvertYaw":custom&&field==SensX?"SlowSensitivityX":
            custom&&field==SensY?"SlowSensitivityY":definitions[field].key);
    }
    return result;
}
uint32_t GL_CALL gl_setting_count(void) {return SettingCount+4;}
uint32_t GL_CALL gl_menu_setting_count(const gl_context* c) {return c?gl_setting_count()+profile_count()*gl_gameplay_context_count(c):0;}
int32_t GL_CALL gl_setting_at(const gl_context* c,uint32_t index,gl_setting_info* out) {
    if(!c||!out||index>=gl_menu_setting_count(c))return GL_INVALID;
    *out={};out->visible=out->available=1;
    if(index>=SettingCount&&index<gl_setting_count()) {
        auto i=index-SettingCount;out->id=action_ids[i];out->label=gl_text(c,action_keys[i]);out->type=GL_SETTING_ACTION;
        out->description=gl_text(c,i==0?"description.calibration":i==1?"description.calibration.cancel":i==2?"description.reset":"description.save");out->unit="";
        if(i<2){
            auto* e=c->endpoint(c->active);out->available=e&&e->info.source==GL_SOURCE_SDL&&
                (i==1||(e->last_accel&&c->now>=e->last_accel&&c->now-e->last_accel<150000000));
            const auto state=e?e->motion.diagnostics.calibration_state:GL_CAL_IDLE;
            const bool pending=state==GL_CAL_COUNTDOWN||state==GL_CAL_COLLECTING||state==GL_CAL_MOVING;
            out->visible=i==0?!pending:pending;
            out->available=out->available&&out->visible;
        }
        if(i==3)out->visible=out->available=0; // legacy explicit save API; frontends auto-save edits
        return GL_OK;
    }
    const Settings* values=&c->settings;const GameplayContext* context=nullptr;
    if(index>=gl_setting_count()){
        const auto offset=index-gl_setting_count();
        const auto& [id,state]=*std::next(c->gameplay_contexts.begin(),offset/profile_count());
        index=profile_field(offset%profile_count());context=&state;values=&c->context_settings.at(id);
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
    if(index==AutoCal){auto* e=c->endpoint(c->active);out->available=e&&e->info.source==GL_SOURCE_SDL&&
        e->last_accel&&c->now>=e->last_accel&&c->now-e->last_accel<150000000;}
    const auto flick_inputs=c->flick_inputs();
    bool stick_hook=(c->host_capabilities&GL_HOST_NATIVE_STICK_SUPPRESSION)!=0;
    bool pad_hook=(c->host_capabilities&GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION)!=0;
    if(index==FlickMode||index==FlickMs||index==FlickContext||(index>=FlickStyle&&index<=FlickExponent)||(index>=FlickSmoothSpeed&&index<=FlickPadOuter)) {
        const bool cursor=context?context->output_target==GL_OUTPUT_CURSOR:
            (c->gameplay_contexts.empty()&&c->output_target==GL_OUTPUT_CURSOR);
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
    const bool camera_view=context?context->output_target!=GL_OUTPUT_CURSOR:c->effective_output_target()!=GL_OUTPUT_CURSOR;
    if(index==RecenterButton)out->visible=out->available=camera_view&&c->recenter;
    if(index==ZoomCompensation)out->visible=out->available=camera_view&&context&&context->zoom_available;
    if(index==ShortPress){
        const bool hold=v[Activation]==double(GL_HOLD)||v[Activation]==double(GL_HOLD_DISABLE);
        const auto button=static_cast<unsigned>(v[Button]);
        out->visible=camera_view&&hold&&caps.buttons&&(c->host_capabilities&GL_HOST_SHORT_PRESS_FILTER);
        out->available=out->visible&&button>=1&&button<=32&&(caps.buttons&(1u<<(button-1)));
    }
    if(index==UIScale)out->visible=out->available=0; // legacy saved value; panels always use automatic DPI/resolution scaling
    if(index==Enabled)out->visible=out->available=0; // compatibility alias; Activation owns Off
    if(index>=Button&&index<=ShortPress&&v[Activation]==double(GL_ALWAYS))out->visible=out->available=0;
    if(int(v[Activation])==GL_GYRO_OFF&&(gl_setting_is_activator(d.id)||
        (std::strncmp(d.id,"gyro.",5)==0&&index!=Activation)||index==HoldInvert||index==HoldTrackball))
        out->visible=out->available=0;
    if(context_selector(index))out->visible=out->available=0; // legacy API only; tabs own their settings
    if(context){
        out->id=context->setting_ids[index].c_str();out->label=context->setting_labels[index].c_str();
        out->description=context->setting_descriptions[index].c_str();out->available=out->available&&context->available;
    }
    else if(profile_setting(index))out->visible=out->available=0;
    if(index==InvertX&&space==GL_SPACE_LOCAL_YAW_ROLL){
        out->label=gl_text(c,"InvertYaw");out->description=gl_text(c,"description.InvertYaw");
    }
    if(v[Acceleration]!=0&&(index==SensX||index==SensY)){
        out->label=gl_text(c,index==SensX?"SlowSensitivityX":"SlowSensitivityY");
        out->description=gl_text(c,index==SensX?"description.SlowSensitivityX":"description.SlowSensitivityY");
    }
    return GL_OK;
}
int32_t GL_CALL gl_setting_get(const gl_context* c,const char* id,double* value) {
    if(!c||!value)return GL_INVALID;int index=setting_index(id);
    if(index>=0){if(index!=AutoCal)return GL_UNAVAILABLE;*value=c->settings[index];return GL_OK;}
    uint32_t context_id;int field;
    if(!context_setting(id,context_id,field)||!c->gameplay_contexts.count(context_id))return GL_INVALID;
    const auto& saved=c->context_settings.at(context_id);
    *value=field==Enabled?saved[Activation]!=double(GL_GYRO_OFF):saved[field];return GL_OK;
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
        auto pending=saved;
        if(field==Enabled){
            if(!value)pending[Activation]=GL_GYRO_OFF;
            else if(int(pending[Activation])==GL_GYRO_OFF)pending[Activation]=GL_ALWAYS;
        }else pending[field]=value;
        pending[Enabled]=int(pending[Activation])!=GL_GYRO_OFF;
        if(field==FlickSmoothAngle)pending[FlickSmoothSpeed]=value*30;
        if(field==FlickStickInner&&value>=pending[FlickStickOuter])pending[FlickStickOuter]=std::min(1.0,value+.05);
        if(field==FlickStickOuter&&value<=pending[FlickStickInner])pending[FlickStickInner]=std::max(0.0,value-.05);
        if(field==FlickPadInner&&value>=pending[FlickPadOuter])pending[FlickPadOuter]=std::min(1.0,value+.05);
        if(field==FlickPadOuter&&value<=pending[FlickPadInner])pending[FlickPadInner]=std::max(0.0,value-.05);
        if(field==Acceleration||(field==SensX||field==SensY))apply_acceleration_preset(pending);
        if(field>=FastSensX&&field<=FastSpeed&&saved[field]!=value)pending[Acceleration]=4;
        bool changed=false;const auto& ids=c->gameplay_contexts.at(context_id).setting_ids;
        for(int f=0;f<SettingCount;++f)if(saved[f]!=pending[f]){
            saved[f]=pending[f];c->emit(GL_EVENT_SETTING,f,0,pending[f],ids[f].c_str());changed=true;
        }
        if(!changed)return GL_OK;
        if(field==RecenterButton)c->recenter_primed=false;
        if(context_id==c->winning_context()&&(field==Enabled||field==Activation||(field>=Button&&field<=ShortPress)||field==Trigger||field==TriggerThreshold||field==StickEffect)){
            for(auto& gate:c->gates)gate.cancel();c->controls_primed=false;c->stick_gate.reset();c->trigger_gate.reset();}
        return save_change(c);
    }
    const auto& d=definitions[index];if(value<d.min||value>d.max)return GL_INVALID;
    if(index!=AutoCal)return GL_UNAVAILABLE; // per-view values require context.<id> keys
    if(context_selector(index)&&value!=std::floor(value))return GL_INVALID;
    value=quantize(index,value);if(c->settings[index]==value)return GL_OK;
    c->settings[index]=value;c->emit(GL_EVENT_SETTING,index,0,value,id);
    if(index==Activation||(index>=Button&&index<=ShortPress)){for(auto& g:c->gates)g.cancel();c->controls_primed=false;}
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
    if(field==FlickStyle){const char* keys[]={"flick.full","flick.pivot_only","flick.rotate_only"};key=keys[index];}
    if(field==FlickSnap){const char* keys[]={"off","flick.snap90","flick.snap45"};key=keys[index];}
    if(field==AutoCal){const char* keys[]={"off","menus","anytime"};key=keys[index];
        if(index==GL_CAL_MENUS)out->available=(c->host_capabilities&GL_HOST_MENU_STATE)!=0;}
    if(field==FlickMode){const char* keys[]={"off","mode.outside_context","on","mode.context","flick.touchpad","flick.both"};key=keys[index];
        const auto inputs=c->flick_inputs();const bool pad=(inputs.available&GL_FLICK_INPUT_TOUCHPAD)!=0;
        if(index==GL_FLICK_ON&&pad)key="flick.stick";
        if(index==GL_FLICK_CONTEXT_ONLY||index==GL_FLICK_OUTSIDE_CONTEXT)out->available=0;
        if(index){
            uint32_t mode_id;int mode_field;
            const bool cursor=context_setting(id,mode_id,mode_field)?c->gameplay_contexts.at(mode_id).output_target==GL_OUTPUT_CURSOR:
                (c->gameplay_contexts.empty()&&c->output_target==GL_OUTPUT_CURSOR);
            const bool stick=(inputs.available&GL_FLICK_INPUT_STICK)&&(c->host_capabilities&GL_HOST_NATIVE_STICK_SUPPRESSION);
            const bool touchpad=pad&&(c->host_capabilities&GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
            out->available=out->available&&!cursor&&
                (index==GL_FLICK_ON?stick:index==GL_FLICK_TOUCHPAD?touchpad:index==GL_FLICK_BOTH?stick&&touchpad:false);
        }}
    if((field>=Touchpad&&field<=Stick)||field==Trigger){
        const char* keys[]={"off","left","right","either","both"};key=keys[index];
        const auto available=field==Trigger?c->trigger_inputs().available:family_cap(caps,field);out->available=side_available(available,index);
        if(field==Trigger&&(index==GL_SIDE_LEFT||index==GL_SIDE_RIGHT)){out->label=gl_get_trigger_label(c,index==GL_SIDE_LEFT?GL_LEFT:GL_RIGHT);return GL_OK;}
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
    if(std::strcmp(id,"settings.reset")==0){gl_reset_settings(c);return c->settings_path.empty()?GL_OK:c->settings_save_result;}
    if(std::strcmp(id,"settings.save")==0)return c->settings_path.empty()?GL_UNAVAILABLE:gl_save_settings(c,c->settings_path.c_str());
    return GL_INVALID;
}
void GL_CALL gl_reset_settings(gl_context* c) {
    if(!c)return;
    {
    SettingsBatch batch(c);
    gl_setting_set(c,definitions[AutoCal].id,definitions[AutoCal].def);
    for(const auto& [id,state]:c->gameplay_contexts)for(int field=0;field<SettingCount;++field)
        if(profile_setting(field))gl_setting_set(c,state.setting_ids[field].c_str(),definitions[field].def);
    for(auto& [id,profile]:c->context_settings)profile=defaults(); // including retired modes
    }
    save_change(c);
}
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
    double schema=1;
    if(values.count("schema")&&!parse_number(values["schema"],schema))return GL_INVALID;
    if(schema>17)return GL_NEWER_SCHEMA;if(schema<1||schema!=std::floor(schema))return GL_INVALID;
    values.erase("schema");
    std::string language=c->language;
    if(auto it=values.find("ui.language");it!=values.end()){
        if(!known_language(it->second.c_str()))return GL_INVALID;
        language=it->second;values.erase(it);
    }
    uint32_t menu_key=10; // Old files and a removed key use F10; an empty value disables it.
    if(auto it=values.find("ui.menu_key");it!=values.end()){
        const auto& key=it->second;menu_key=0;
        if(!key.empty()){
            if(key.size()<2||key.size()>3||(key[0]!='F'&&key[0]!='f')||key[1]<'1'||key[1]>'9')return GL_INVALID;
            const auto parsed=std::from_chars(key.data()+1,key.data()+key.size(),menu_key);
            if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+key.size()||menu_key<1||menu_key>24)return GL_INVALID;
        }
        values.erase(it);
    }
    // Library v1 -> v2 migration: legacy smoothing seconds becomes milliseconds.
    if(schema==1 && values.count("gyro.smoothing_seconds")) {
        double seconds;if(!parse_number(values["gyro.smoothing_seconds"],seconds))return GL_INVALID;
        if(!values.count("gyro.smoothing_ms"))values["gyro.smoothing_ms"]=std::to_string(seconds*1000);
        values.erase("gyro.smoothing_seconds");
    }
    auto pending=defaults();pending[AutoCal]=c->settings[AutoCal];
    auto profiles=c->context_settings;std::map<std::string,std::string> unknown;
    std::map<uint32_t,std::map<int,double>> edits;
    auto seed=defaults();bool has_seed=false,has_flat_profile=false;
    // v2 had hard-coded aim conditions. Preserve their mode but require an explicit
    // new context selection. Keep old multiplier keys as unknown migration data.
    if(schema<3){pending[GyroContext]=0;pending[FlickContext]=0;}
    for(const auto& [key,text]:values) {
        uint32_t context_id;int field;
        if(context_setting(key.c_str(),context_id,field)) {
            double number;const auto& d=definitions[field];
            if(!parse_number(text,number)||number<d.min||number>d.max||!valid_profile_value(field,number))return GL_INVALID;
            edits[context_id][field]=quantize(field,number);continue;
        }
        if(key.starts_with("migration.profile.")){
            int i=setting_index(key.c_str()+18);double number;
            if(i<0){unknown[key]=text;continue;}
            if(!parse_number(text,number)||number<definitions[i].min||number>definitions[i].max||
               (context_selector(i)&&number!=std::floor(number)))return GL_INVALID;
            seed[i]=quantize(i,number);has_seed=true;continue;
        }
        int i=setting_index(key.c_str());if(i<0){unknown[key]=text;continue;}
        double number;if(!parse_number(text,number)||number<definitions[i].min||number>definitions[i].max)return GL_INVALID;
        if(context_selector(i)&&number!=std::floor(number))return GL_INVALID;
        pending[i]=quantize(i,number);
        has_flat_profile|=profile_setting(i);
    }
    if(schema<6){seed=pending;has_seed=has_flat_profile;}
    const bool flat_only=schema<10&&has_flat_profile&&edits.empty()&&!has_seed;
    const bool old_single=schema<6&&has_flat_profile&&edits.empty();
    if(flat_only||old_single){
        // A former implicit camera can only be assigned unambiguously to one view.
        if(c->gameplay_contexts.size()!=1)return GL_UNAVAILABLE;
        auto value=pending;
        if(schema<6)value=migrate_profile(value,0);
        if(schema<7)migrate_activation(value);
        if(!valid_profile_value(Activation,value[Activation])||!valid_profile_value(FlickMode,value[FlickMode]))return GL_INVALID;
        value[GyroContext]=value[FlickContext]=0;
        profiles[c->gameplay_contexts.begin()->first]=value;
        has_seed=false;
    }
    if(has_seed){
        // All current views must be registered before importing a shared legacy profile.
        // Materialize it now; future new views start from defaults, never a hidden seed.
        if(c->gameplay_contexts.empty())return GL_UNAVAILABLE;
        for(const auto& [id,state]:c->gameplay_contexts){
            auto old=profiles.at(id);auto profile=migrate_profile(seed,id);
            profile[SensX]=old[SensX];profile[SensY]=old[SensY];profiles[id]=profile;
        }
    }
    for(const auto& [id,fields]:edits){
        auto [it,inserted]=profiles.try_emplace(id,has_seed?migrate_profile(seed,id):defaults());
        for(auto [field,value]:fields)it->second[field]=value;
    }
    if(schema<7)for(auto& [id,profile]:profiles)migrate_activation(profile);
    for(auto& [id,profile]:profiles){
        const auto profile_edits=edits.find(id);
        const bool disabled_alias=profile_edits!=edits.end()&&profile_edits->second.count(Enabled)&&!profile_edits->second.at(Enabled);
        if((schema<16&&!profile[Enabled])||disabled_alias)profile[Activation]=GL_GYRO_OFF;
        profile[Enabled]=int(profile[Activation])!=GL_GYRO_OFF;
        // Before schema 17 Invert X reversed the complete Yaw + Roll sum.
        // Initialize both source inversions alike, unless the new field was
        // explicitly supplied. Invert Y and signed roll contribution are kept.
        if(schema<17&&(profile_edits==edits.end()||!profile_edits->second.count(InvertRoll)))
            profile[InvertRoll]=profile[InvertX];
        if(schema<14){
            if(!edits.count(id)||!edits.at(id).count(HoldInvert))profile[HoldInvert]=profile[TemporaryInvertButton]!=0;
            if(!edits.count(id)||!edits.at(id).count(HoldTrackball))profile[HoldTrackball]=profile[TrackballButton]!=0;
        }
        profile[TemporaryInvertButton]=profile[TrackballButton]=profile[StickEffect]=0;
        if(schema<13&&(!edits.count(id)||!edits.at(id).count(FlickSmoothSpeed)))
            profile[FlickSmoothSpeed]=quantize(FlickSmoothSpeed,profile[FlickSmoothAngle]*30);
        if(profile[FlickStickInner]>=profile[FlickStickOuter]||profile[FlickPadInner]>=profile[FlickPadOuter])return GL_INVALID;
        if(schema<12){
            if(profile[Acceleration]<4)apply_acceleration_preset(profile);
            else if(auto edit=edits.find(id);edit!=edits.end()&&edit->second.count(Acceleration)){
                // Schema 11's Custom defaults were 5x, including partial INIs.
                if(!edit->second.count(FastSensX))profile[FastSensX]=5;
                if(!edit->second.count(FastSensY))profile[FastSensY]=5;
            }
        }
        else if(profile[Acceleration]<4){
            auto preset=profile;apply_acceleration_preset(preset);
            for(int field=FastSensX;field<=FastSpeed;++field)if(std::abs(profile[field]-preset[field])>1e-8){
                profile[Acceleration]=4;break; // Preserve manual INI curve edits.
            }
        }
    }
    // Transaction: no setting changes on malformed or newer files.
    SettingsBatch batch(c);
    gl_setting_set(c,definitions[AutoCal].id,pending[AutoCal]);
    for(const auto& [id,state]:c->gameplay_contexts) {
        const auto& profile=profiles.at(id);
        for(int field=0;field<SettingCount;++field)if(profile_setting(field)&&field!=Enabled)gl_setting_set(c,state.setting_ids[field].c_str(),profile[field]);
    }
    c->context_settings=std::move(profiles);c->settings_need_upgrade=schema<17;
    gl_set_language(c,language.c_str());
    gl_set_menu_key(c,menu_key);
    c->unknown_settings=std::move(unknown);c->settings_path=std::move(remembered_path);c->settings_save_result=GL_OK;return GL_OK;
} catch (...) {return GL_IO_ERROR;}
static int32_t write_settings(gl_context* c,const char* path) try {
    if(!c||!path||!*path)return GL_INVALID;
    std::string remembered_path(path);
    auto destination=std::filesystem::path(reinterpret_cast<const char8_t*>(path)),temporary=destination;temporary+=".tmp";
    // Do not overwrite a future-schema file even if the caller ignored load's status.
    if(std::filesystem::exists(destination)) {
        std::ifstream existing(destination);if(!existing)return GL_IO_ERROR;std::string line;
        while(std::getline(existing,line)) {
            auto pos=line.find('=');if(pos!=std::string::npos&&trim(line.substr(0,pos))=="schema") {
                double schema;if(!parse_number(trim(line.substr(pos+1)),schema))return GL_INVALID;
                if(schema>17)return GL_NEWER_SCHEMA;
            }
        }
    }
    std::ofstream file(temporary,std::ios::trunc);if(!file)return GL_IO_ERROR;
    file.imbue(std::locale::classic());file<<"# GyroLib settings; schema 17\nschema=17\n"
        "# Menu shortcut: F1..F24, or leave ui.menu_key= empty to disable. Restart after editing.\n"
        "ui.menu_key=";
    if(c->menu_key)file<<'F'<<c->menu_key;
    file<<"\nui.language="<<c->language<<'\n'<<std::setprecision(12);
    file<<"calibration.automatic="<<c->settings[AutoCal]<<'\n';
    for(const auto& [id,profile]:c->context_settings){
        file<<"\n# View "<<id<<'\n';
        for(int field=0;field<SettingCount;++field)
            if(profile_setting(field)&&field!=Enabled&&field!=FlickSmoothAngle&&field!=TemporaryInvertButton&&field!=TrackballButton&&field!=StickEffect)file<<profile_key(id,field)<<'='<<profile[field]<<'\n';
    }
    for(const auto& [key,value]:c->unknown_settings)file<<key<<'='<<value<<'\n';
    file.flush();if(!file)return GL_IO_ERROR;file.close();if(!file)return GL_IO_ERROR;
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return GL_IO_ERROR;
#else
    std::error_code error;std::filesystem::rename(temporary,destination,error);if(error)return GL_IO_ERROR;
#endif
    c->settings_path=std::move(remembered_path);c->settings_need_upgrade=false;return GL_OK;
} catch (...) {return GL_IO_ERROR;}
int32_t GL_CALL gl_save_settings(gl_context* c,const char* path){
    const auto result=write_settings(c,path);if(c)c->settings_save_result=result;return result;
}
}
