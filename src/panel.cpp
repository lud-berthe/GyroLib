#include "gyrolib/panel.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "detail/panel_help.hpp"
#include "detail/panel_commands.hpp"
#include "detail/inheritance_widgets.hpp"
using gyrolib_panel_detail::help;
struct gl_panel {gl_context* context;int result{};uint64_t selected_tab=GL_TAB_DEFAULT;bool select_tab=true;
    uint64_t opening{};
    gyrolib_panel_detail::CommandSink sink{};void* sink_user{};};
namespace gyrolib_panel_detail {
void set_sink(gl_panel* p,CommandSink sink,void* user){p->sink=sink;p->sink_user=user;}
void set_context(gl_panel* p,gl_context* c){p->context=c;}
void set_result(gl_panel* p,int32_t result){p->result=result;}
}
static int32_t panel_command(gl_panel* p,const gyrolib_panel_detail::Command& command){
    using namespace gyrolib_panel_detail;
    if(p->sink){auto result=p->sink(p->sink_user,command);if(result!=GL_OK)return result;}
    switch(command.type){
    case Setting:return gl_setting_set(p->context,command.id,command.value);
    case Action:return gl_action(p->context,command.id);
    case Language:return gl_set_language(p->context,command.id);
    case Device:return gl_select_device(p->context,command.physical);
    case Sensor:return gl_bind_motion_sensor(p->context,command.physical,command.sensor);
    case Parent:return gl_set_context_parent(p->context,static_cast<uint32_t>(command.physical),static_cast<uint32_t>(command.sensor));
    case Inherit:return gl_setting_inherit(p->context,command.id);
    }return GL_INVALID;
}
static int32_t panel_setting(gl_panel* p,const char* id,double value){return panel_command(p,{gyrolib_panel_detail::Setting,id,value});}
static int32_t panel_action(gl_panel* p,const char* id){return panel_command(p,{gyrolib_panel_detail::Action,id});}
static const char* base_key(const char* id){
    if(std::strncmp(id,"context.",8)!=0)return id;
    auto* suffix=std::strchr(id+8,'.');if(!suffix)return id;++suffix;
    if(std::strcmp(suffix,"sensitivity_x")==0)return "gyro.sensitivity_x";
    if(std::strcmp(suffix,"sensitivity_y")==0)return "gyro.sensitivity_y";
    return suffix;
}
static uint32_t control_group(const char* key,double value){
    if(!std::strcmp(key,"gyro.activation")&&int(value)==GL_HOLD_DISABLE)return GL_ADVANCED_HOLD_DISABLE;
    if(!std::strcmp(key,"gyro.smoothing_ms")&&value>0)return GL_ADVANCED_SMOOTHING;
    if(!std::strcmp(key,"gyro.acceleration")&&value>0)return GL_ADVANCED_ACCELERATION;
    if(!std::strcmp(key,"flick.mode")&&int(value)!=GL_FLICK_OFF)return GL_ADVANCED_FLICK;
    return GL_ADVANCED_NONE;
}
static void draw_action(gl_panel* p,const gl_setting_info& action,float width=0,bool secondary=false){
    ImGui::PushID(action.id);ImGui::BeginDisabled(!action.available);
    if(secondary){ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.f);}
    if(ImGui::Button(action.label,ImVec2(width,0)))p->result=panel_action(p,action.id);
    if(secondary){ImGui::PopStyleVar();ImGui::PopStyleColor();}
    help(action.description);
    ImGui::EndDisabled();ImGui::PopID();
}
static void draw_automatic_calibration(gl_panel* p,const gl_setting_info& setting,float width,bool label_above){
    auto* c=p->context;ImGui::PushID(setting.id);ImGui::BeginDisabled(!setting.available);
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"ui.auto_calibration"));if(!label_above)ImGui::SameLine();
    ImGui::SetNextItemWidth(width);const char* preview=gl_text(c,"unavailable");
    for(uint32_t i=0;i<gl_menu_choice_count(c,setting.id);++i){gl_choice choice{};gl_choice_at(c,setting.id,i,&choice);
        if(choice.value==setting.value&&choice.available)preview=choice.label;}
    if(ImGui::BeginCombo("##value",preview)){
        for(uint32_t i=0;i<gl_menu_choice_count(c,setting.id);++i){gl_choice choice{};gl_choice_at(c,setting.id,i,&choice);
            if(!choice.available)continue;
            if(ImGui::Selectable(choice.label,choice.value==setting.value))p->result=panel_setting(p,setting.id,choice.value);
            help(gl_choice_description(c,setting.id,choice.value));}
        ImGui::EndCombo();
    }
    help(setting.description);
    ImGui::EndDisabled();ImGui::PopID();
}
static void draw_header(gl_panel* p,float scale,bool& open){
    auto* c=p->context;
    const float inset=22*scale,bar_height=ImGui::GetFrameHeight()+16*scale;
    const auto origin=ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(origin,ImVec2(origin.x+ImGui::GetWindowWidth(),origin.y+bar_height),
        ImGui::GetColorU32(ImGuiCol_TitleBgActive),ImGui::GetStyle().WindowRounding,ImDrawFlags_RoundCornersTop);
    const float gap=12*scale,language_width=130*scale,close_width=28*scale;
    const float language_x=ImGui::GetWindowWidth()-inset-language_width-close_width-gap;
    std::string shortcuts="GyroLib";
    if(const auto key=gl_get_menu_key(c))shortcuts+="  |  F"+std::to_string(key);
    if(gl_get_gamepad_menu_shortcut(c)){
        const auto selected=gl_get_selected_device(c);constexpr uint32_t chord=(1u<<4)|(1u<<6);
        for(uint32_t i=0;i<gl_endpoint_count(c);++i){gl_endpoint e{};gl_get_endpoint(c,i,&e);
            if(e.connected&&e.physical_id==selected&&(e.caps.buttons&chord)==chord){
                shortcuts+="  |  ";shortcuts+=gl_get_button_label(c,4);shortcuts+=" + ";shortcuts+=gl_get_button_label(c,6);break;
            }
        }
    }
    // Device-provided names can be long. Keep language and close accessible;
    // shorten at UTF-8 boundaries and leave the complete shortcut in the tooltip.
    auto title=shortcuts;const float title_width=language_x-inset-gap;
    const bool shortened=ImGui::CalcTextSize(title.c_str()).x>title_width;
    if(shortened){
        while(!title.empty()&&ImGui::CalcTextSize((title+"...").c_str()).x>title_width){
            auto end=title.size()-1;while(end>0&&(static_cast<unsigned char>(title[end])&0xc0)==0x80)--end;
            title.resize(end);
        }
        title+="...";
    }
    ImGui::SetCursorPos(ImVec2(inset,8*scale));ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title.c_str());if(shortened)help(shortcuts.c_str());
    const char* languages[]={"English","Français","Deutsch","Español","Italiano","Português"};
    const char* codes[]={"en","fr","de","es","it","pt"};
    int language=0;for(int i=0;i<6;++i)if(std::strcmp(gl_get_language(c),codes[i])==0)language=i;
    ImGui::SameLine(language_x);
    ImGui::SetNextItemWidth(language_width);
    if(ImGui::Combo("##language",&language,languages,6))p->result=panel_command(p,{gyrolib_panel_detail::Language,codes[language]});
    help(gl_text(c,"ui.language"));
    ImGui::SameLine(0,gap);if(ImGui::Button("×##close",ImVec2(close_width,0)))open=false;
    help(gl_text(c,"ui.close"));
    ImGui::SetCursorPos(ImVec2(inset,bar_height+18*scale));
}
extern "C" {
gl_panel* GL_CALL gl_panel_create(gl_context* c,const char* path) {try{
    if(!c)return nullptr;
    auto* p=new gl_panel{c};
    if(path)p->result=gl_set_settings_path(c,path);
    return p;
}catch(...){return nullptr;}}
void GL_CALL gl_panel_destroy(gl_panel* p){delete p;}
void GL_CALL gl_panel_function_key(gl_panel* p,uint32_t key,uint32_t pressed,uint32_t repeat){
    if(p&&key>=1&&key<=24&&key==gl_get_menu_key(p->context)&&pressed&&!repeat){
    const bool opening=!gl_panel_open(p->context);
    gl_set_panel_open(p->context,opening);
}}
int32_t GL_CALL gl_panel_last_result(const gl_panel* p){return p?p->result:GL_INVALID;}
void GL_CALL gl_panel_draw(gl_panel* p,float width,float height,float dpi) try {
    if(!p||!gl_panel_open(p->context)||!ImGui::GetCurrentContext())return;
    auto* c=p->context;
    uint32_t opening_context{};const auto opening=gl_get_panel_opening(c,&opening_context);
    if(opening!=p->opening){
        p->opening=opening;p->selected_tab=uint64_t(opening_context)+1;p->select_tab=true;
    }
    // Complete allocating operations before opening ImGui scopes.
    std::vector<gl_endpoint> devices,sensors;
    for(uint32_t i=0;i<gl_endpoint_count(c);++i){gl_endpoint e{};gl_get_endpoint(c,i,&e);
        if(gl_is_motion_companion(c,e.id)){if(e.connected)sensors.push_back(e);continue;}
        if(e.connected&&std::none_of(devices.begin(),devices.end(),[&](const auto& d){return d.physical_id==e.physical_id;}))devices.push_back(e);}
    float scale=std::max({1.f,dpi,height/1080.f});
    scale=std::clamp(scale,0.75f,std::max(.75f,std::min(3.f,height/760.f)));
    ImGui::SetNextWindowPos(ImVec2(width*.5f,height*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(width-24*scale,960*scale),std::min(height-24*scale,870*scale)),ImGuiCond_Always);
    ImGui::PushFont(nullptr,18*scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(22*scale,18*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8*scale,5*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(12*scale,9*scale));
    auto background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);background.w=1;
    ImGui::PushStyleColor(ImGuiCol_WindowBg,background);
    bool open=true;
    if(ImGui::Begin("GyroLib###GyroLibPanel",&open,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar)) {
        draw_header(p,scale,open);
        ImGui::BeginDisabled(devices.empty());
        gl_diagnostics diagnostic{};gl_get_diagnostics(c,&diagnostic);
        const auto selected=gl_get_selected_device(c);
        const char* controller=gl_text(c,!devices.empty()&&selected?"ui.controller.disconnected":"ui.controller.none");
        for(const auto& e:devices)if(e.physical_id==selected){controller=e.name;break;}
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"ui.controller"));ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##controller",controller)){
            for(const auto& e:devices){const auto id=std::to_string(e.physical_id);ImGui::PushID(id.c_str());
                if(ImGui::Selectable(e.name,e.physical_id==selected))p->result=panel_command(p,{gyrolib_panel_detail::Device,nullptr,0,e.physical_id});
                ImGui::PopID();}
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if(gyro_profile_widgets::controller_connected(c)){
        help(gl_text(c,"warning.double"));
        ImGui::TextDisabled("%s: %s",gl_text(c,"ui.source"),diagnostic.source==GL_SOURCE_SDL?"SDL":diagnostic.source==GL_SOURCE_STEAM?"Steam Input":gl_text(c,"ui.source.waiting"));
        if(gl_motion_sensor_needs_selection(c,selected)&&!sensors.empty()){
            bool native_available=false;
            for(uint32_t i=0;i<gl_endpoint_count(c);++i){gl_endpoint e{};gl_get_endpoint(c,i,&e);
                if(e.physical_id==selected&&!gl_is_motion_companion(c,e.id)&&gl_endpoint_motion_available(c,e.id))native_available=true;}
            const auto bound=gl_get_motion_sensor(c,selected);const char* label=gl_text(c,native_available?"ui.sensor.none":"ui.sensor.choose");
            for(const auto& e:sensors)if(e.id==bound)label=e.name;
            ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"ui.sensor"));ImGui::SameLine();ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##motion_sensor",label)){
                if(native_available&&ImGui::Selectable(gl_text(c,"ui.sensor.none"),!bound))p->result=panel_command(p,{gyrolib_panel_detail::Sensor,nullptr,0,selected,0});
                for(const auto& e:sensors){if(!gl_endpoint_motion_available(c,e.id))continue;const auto id=std::to_string(e.id);ImGui::PushID(id.c_str());
                    if(ImGui::Selectable(e.name,e.id==bound))p->result=panel_command(p,{gyrolib_panel_detail::Sensor,nullptr,0,selected,e.id});ImGui::PopID();}
                ImGui::EndCombo();
            }
            help(gl_text(c,"ui.sensor.help"));
        }
        gl_setting_info automatic{},calibrate{},reset{},recommended{};
        for(uint32_t i=0;i<gl_menu_shared_setting_count(c);++i){gl_setting_info setting{};gl_menu_shared_setting_at(c,i,&setting);
            if(std::strcmp(setting.id,"calibration.automatic")==0)automatic=setting;
            if(setting.visible&&(std::strcmp(setting.id,"calibration.begin")==0||std::strcmp(setting.id,"calibration.cancel")==0))calibrate=setting;
            if(std::strcmp(setting.id,"settings.reset")==0)reset=setting;
            if(std::strcmp(setting.id,"settings.recommended")==0)recommended=setting;
        }
        const float content_width=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemSpacing.x;
        const float padding=2*ImGui::GetStyle().FramePadding.x;
        const float calibration_label=ImGui::CalcTextSize(gl_text(c,"ui.auto_calibration")).x;
        const float calibrate_width=ImGui::CalcTextSize(calibrate.label).x+padding;
        const bool label_above=calibration_label+150*scale+calibrate_width+2*gap>content_width;
        const float combo_width=content_width-calibrate_width-gap-(label_above?0:calibration_label+gap);
        const float half_width=(content_width-gap)*.5f;
        const bool stack_actions=recommended.visible&&std::max(ImGui::CalcTextSize(reset.label).x,
            ImGui::CalcTextSize(recommended.label).x)+padding>half_width;
        const float action_width=stack_actions?content_width:half_width;
        char status[512]{},error[512]{};
        switch(diagnostic.calibration_state){
            case GL_CAL_COUNTDOWN:std::snprintf(status,sizeof(status),"%s %.1f s",gl_text(c,"calibration.countdown"),diagnostic.calibration_seconds_remaining);break;
            case GL_CAL_COLLECTING:std::snprintf(status,sizeof(status),"%s",gl_text(c,"calibration.collecting"));break;
            case GL_CAL_MOVING:std::snprintf(status,sizeof(status),"%s",gl_text(c,"calibration.moving"));break;
            case GL_CAL_COMPLETE:std::snprintf(status,sizeof(status),"%s",gl_text(c,"calibration.complete"));break;
            case GL_CAL_EXTERNAL:std::snprintf(status,sizeof(status),"%s",gl_text(c,"calibration.steamHelp"));break;
            default:break;
        }
        const auto save_result=gl_get_settings_save_result(c);
        if(save_result!=GL_OK&&save_result!=GL_UNAVAILABLE)
            std::snprintf(error,sizeof(error),"%s (%d)",gl_text(c,"ui.save_failed"),save_result);
        else if(p->result!=GL_OK&&p->result!=save_result)
            std::snprintf(error,sizeof(error),"%s (%d)",gl_text(c,"ui.operation_failed"),p->result);
        const float footer_height=(2+int(label_above)+int(stack_actions))*ImGui::GetFrameHeightWithSpacing()+12*scale+
            (status[0]?ImGui::CalcTextSize(status,nullptr,false,content_width).y+gap:0)+
            (error[0]?ImGui::CalcTextSize(error,nullptr,false,content_width).y+gap:0);
        if(!gl_menu_tab_setting_count(c,p->selected_tab)){
            gl_menu_tab first_view{};
            p->selected_tab=gl_menu_tab_at(c,0,&first_view)==GL_OK?first_view.id:GL_TAB_CAMERA;
            p->select_tab=true;
        }
        if(ImGui::BeginTabBar("View modes",ImGuiTabBarFlags_FittingPolicyScroll)) {
        for(uint32_t tab_index=0;tab_index<gl_menu_tab_count(c);++tab_index){
            gl_menu_tab tab{};gl_menu_tab_at(c,tab_index,&tab);
            const std::string tab_label=std::string(tab.label)+"###mode-tab-"+std::to_string(tab.id);
            const bool requested_tab=p->select_tab&&p->selected_tab==tab.id;
            if(!ImGui::BeginTabItem(tab_label.c_str(),nullptr,requested_tab?ImGuiTabItemFlags_SetSelected:0))continue;
            if(requested_tab)p->select_tab=false;
            if(!p->select_tab)p->selected_tab=tab.id;
            ImGui::PushID(tab_label.c_str());
            if(ImGui::BeginChild("Settings",ImVec2(0,-footer_height),ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            ImGui::TextWrapped("%s",tab.description);
            gyro_profile_widgets::parent_selector(c,tab.context_id,[&](uint32_t view,uint32_t parent){p->result=panel_command(p,{gyrolib_panel_detail::Parent,nullptr,0,view,parent});});
        ImGui::Separator();
            const auto total=gl_menu_tab_setting_count(c,tab.id);
            gl_setting_info threshold{},trigger_threshold{},invert_x{},invert_y{},invert_roll{},block_long_press{};
            for(uint32_t n=0;n<total;++n){gl_setting_info setting{};gl_menu_tab_setting_at(c,tab.id,n,&setting);
                const auto* key=base_key(setting.id);
                if(std::strcmp(key,"activation.stick_threshold")==0)threshold=setting;
                if(std::strcmp(key,"activation.trigger_threshold")==0)trigger_threshold=setting;
                if(std::strcmp(key,"gyro.invert_x")==0)invert_x=setting;
                if(std::strcmp(key,"gyro.invert_y")==0)invert_y=setting;
                if(std::strcmp(key,"gyro.invert_roll")==0)invert_roll=setting;
                if(std::strcmp(key,"activation.block_long_press")==0)block_long_press=setting;}
            std::vector<ImVec2> child_labels,activator_labels;
            float label_bottom=0,parent_stem=0,activator_top=0,activator_stem=0;
            bool activator_heading=false;
            const auto mark=[&](const char* key,const char* description){help(description);gyro_profile_widgets::marker(c,key,[&](const char* id){p->result=panel_command(p,{gyrolib_panel_detail::Inherit,id});});};
            const auto marker_width=[&](const char* key){return gyro_profile_widgets::marker_width(c,key);};
            const auto draw_row=[&](const gl_setting_info& info,bool detail){
                const auto* key=base_key(info.id);const auto group=control_group(key,info.value);
                if(!info.visible||std::strcmp(key,"ui.scale")==0||std::strcmp(key,"gyro.invert_x")==0||std::strcmp(key,"gyro.invert_y")==0||std::strcmp(key,"gyro.invert_roll")==0||std::strcmp(key,"activation.stick_threshold")==0||std::strcmp(key,"activation.trigger_threshold")==0||std::strcmp(key,"activation.block_long_press")==0||std::strcmp(key,"settings.save")==0)return;
                const auto& inline_limit=std::strcmp(key,"activation.trigger")==0?trigger_threshold:threshold;
                const bool inline_threshold=(std::strcmp(key,"activation.stick_deflection")==0||std::strcmp(key,"activation.trigger")==0)&&inline_limit.visible;
                const bool inline_block_long_press=std::strcmp(key,"activation.button")==0&&block_long_press.visible;
                const char* tooltip=info.description;
                ImGui::PushID(info.id);ImGui::BeginDisabled(!info.available);
                if(info.type==GL_SETTING_ACTION) {
                    if(ImGui::Button(info.label)) {
                        p->result=panel_action(p,info.id);
                    }
                } else {
                    const float button_width=ImGui::GetFrameHeight();
                    const float start=ImGui::GetCursorPosX(),gutter=button_width+ImGui::GetStyle().ItemSpacing.x;
                    float parent_bottom=0;
                    parent_stem=ImGui::GetCursorScreenPos().x+button_width*.5f;
                    if(group){
                        const auto id=ImGui::GetID("advanced-open");auto* storage=ImGui::GetStateStorage();
                        const bool expanded=storage->GetBool(id);
                        if(ImGui::Button(expanded?"-###advanced-toggle":"+###advanced-toggle",ImVec2(button_width,0)))storage->SetBool(id,!expanded);
                        parent_bottom=ImGui::GetItemRectMax().y;
                        help(gl_text(c,group==GL_ADVANCED_SMOOTHING?"ui.advanced.smoothing":
                            group==GL_ADVANCED_ACCELERATION?"ui.advanced.acceleration":
                            group==GL_ADVANCED_HOLD_DISABLE?"ui.advanced.hold_disable":"ui.advanced.flick"));
                        ImGui::SameLine(start+gutter);
                    }else ImGui::SetCursorPosX(start+gutter+(detail?16*scale:gl_setting_is_activator(info.id)?24*scale:0));
                    ImGui::AlignTextToFramePadding();ImGui::PushTextWrapPos(ImGui::GetWindowWidth()*.43f);
                    ImGui::TextUnformatted(info.label);ImGui::PopTextWrapPos();help(info.description);
                    const auto label_min=ImGui::GetItemRectMin(),label_max=ImGui::GetItemRectMax();label_bottom=std::max(label_max.y,parent_bottom);
                    if(detail)child_labels.push_back(ImVec2(label_min.x,(label_min.y+label_max.y)*.5f));
                    if(gl_setting_is_activator(info.id))activator_labels.push_back(ImVec2(label_min.x,(label_min.y+label_max.y)*.5f));
                    ImGui::SameLine(ImGui::GetWindowWidth()*.46f);
                    ImGui::SetNextItemWidth(-15*scale-marker_width(info.id));
                    if(info.type==GL_SETTING_BOOL){bool v=info.value!=0;if(ImGui::Checkbox("##value",&v))p->result=panel_setting(p,info.id,v);mark(info.id,info.description);}
                    if(info.type==GL_SETTING_ENUM) {
                        if(inline_threshold){
                            const float reserved=ImGui::CalcTextSize(gl_text(c,"ui.threshold.short")).x+110*scale+2*ImGui::GetStyle().ItemSpacing.x+marker_width(inline_limit.id);
                            ImGui::SetNextItemWidth(std::max(50*scale,ImGui::GetContentRegionAvail().x-reserved-15*scale-marker_width(info.id)));
                        }
                        if(inline_block_long_press){
                            const float reserved=ImGui::GetFrameHeight()+ImGui::CalcTextSize(gl_text(c,"ui.block_long_press")).x+
                                ImGui::GetStyle().ItemInnerSpacing.x+ImGui::GetStyle().ItemSpacing.x+marker_width(block_long_press.id);
                            ImGui::SetNextItemWidth(std::max(50*scale,ImGui::GetContentRegionAvail().x-reserved-15*scale-marker_width(info.id)));
                        }
                        const char* preview=gl_text(c,"unavailable");
                        // Stable context IDs are values, never enumeration indexes.
                        for(uint32_t n=0;n<gl_menu_choice_count(c,info.id);++n) {
                            gl_choice choice{};gl_choice_at(c,info.id,n,&choice);
                            // Derived Custom and legacy bindings remain readable
                            // as current values, without becoming selectable presets.
                            if(choice.value==info.value){preview=!info.available||choice.available||
                                (std::strcmp(key,"activation.button")==0&&choice.value>32)||
                                (std::strcmp(key,"gyro.acceleration")==0&&choice.value==4)?choice.label:gl_text(c,"unavailable");break;}
                        }
                        if(ImGui::BeginCombo("##value",preview)) {
                            for(uint32_t n=0;n<gl_menu_choice_count(c,info.id);++n){gl_choice option{};gl_choice_at(c,info.id,n,&option);if(!option.available)continue;
                                ImGui::PushID(static_cast<int>(n));
                                if(ImGui::Selectable(option.label,option.value==info.value))p->result=panel_setting(p,info.id,option.value);
                                help(gl_choice_description(c,info.id,option.value));
                                ImGui::PopID();
                            }ImGui::EndCombo();
                        }
                        mark(info.id,info.description);
                        if(inline_threshold){
                            ImGui::SameLine();ImGui::AlignTextToFramePadding();
                            ImGui::TextDisabled("%s",gl_text(c,"ui.threshold.short"));
                            help(inline_limit.description);
                            ImGui::SameLine();ImGui::SetNextItemWidth(110*scale);
                            int tick=static_cast<int>(std::round((inline_limit.value-inline_limit.minimum)/inline_limit.step));
                            const int max_tick=static_cast<int>(std::round((inline_limit.maximum-inline_limit.minimum)/inline_limit.step));
                            char format[32];std::snprintf(format,sizeof(format),"%.2f",inline_limit.value);
                            if(ImGui::SliderInt("##threshold",&tick,0,max_tick,format))
                                p->result=panel_setting(p,inline_limit.id,inline_limit.minimum+tick*inline_limit.step);
                            mark(inline_limit.id,inline_limit.description);
                        }
                        if(inline_block_long_press){
                            ImGui::SameLine();ImGui::PushID(block_long_press.id);
                            ImGui::BeginDisabled(!block_long_press.available);bool enabled=block_long_press.value!=0;
                            if(ImGui::Checkbox(gl_text(c,"ui.block_long_press"),&enabled))p->result=panel_setting(p,block_long_press.id,enabled);
                            mark(block_long_press.id,block_long_press.description);ImGui::EndDisabled();ImGui::PopID();
                        }
                    }
                    if(info.type==GL_SETTING_NUMBER) {
                        const bool axis_x=std::strcmp(key,"gyro.sensitivity_x")==0;
                        const bool axis_y=std::strcmp(key,"gyro.sensitivity_y")==0;
                        const bool separate_roll=axis_x&&invert_roll.visible;
                        if(axis_x||axis_y){
                            const auto checkbox_width=[&](const char* label){return ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::CalcTextSize(label).x;};
                            const float gap=ImGui::GetStyle().ItemSpacing.x;
                            const float reserved=separate_roll?ImGui::CalcTextSize(gl_text(c,"ui.invert.short")).x+
                                checkbox_width(gl_text(c,"ui.invert.yaw.short"))+checkbox_width(gl_text(c,"ui.invert.roll.short"))+3*gap:
                                checkbox_width(gl_text(c,"ui.invert.short"))+gap;
                            const float extra=marker_width(axis_x?invert_x.id:invert_y.id)+(separate_roll?marker_width(invert_roll.id):0);
                            ImGui::SetNextItemWidth(std::max(50*scale,ImGui::GetContentRegionAvail().x-reserved-extra-15*scale-marker_width(info.id)));
                        }
                        int tick=static_cast<int>(std::round((info.value-info.minimum)/info.step));
                        int max_tick=static_cast<int>(std::round((info.maximum-info.minimum)/info.step));
                        // ImGui treats this as printf format; escape a literal percent unit.
                        const char* unit=std::strcmp(info.unit,"%")==0?"%%":info.unit;
                        char format[64];std::snprintf(format,sizeof(format),info.step>=1?"%.0f %s":"%.2f %s",info.value,unit);
                        const char* display=format;
                        if(info.value==0&&std::strcmp(key,"gyro.smoothing_ms")==0)display=gl_text(c,"off");
                        // Discrete ticks guarantee keyboard/gamepad changes match metadata steps.
                        // Display uses converted units, not the integer tick value.
                        // Keep keyboard/gamepad adjustment; do not open a raw-tick
                        // text editor with that literal display string on Tab.
                        if(ImGui::SliderInt("##value",&tick,0,max_tick,display,ImGuiSliderFlags_NoInput))p->result=panel_setting(p,info.id,info.minimum+tick*info.step);
                        mark(info.id,info.description);
                        if(axis_x||axis_y){
                            const auto& invert=axis_x?invert_x:invert_y;
                            ImGui::SameLine();ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.70f,.74f,.78f,1));
                            const auto checkbox=[&](const gl_setting_info& setting,const char* label){
                                ImGui::PushID(setting.id);ImGui::BeginDisabled(!setting.available);bool inverted=setting.value!=0;
                                if(ImGui::Checkbox(label,&inverted))p->result=panel_setting(p,setting.id,inverted);
                                mark(setting.id,setting.description);ImGui::EndDisabled();ImGui::PopID();
                            };
                            if(separate_roll){
                                ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"ui.invert.short"));ImGui::SameLine();
                                checkbox(invert,gl_text(c,"ui.invert.yaw.short"));ImGui::SameLine();
                                checkbox(invert_roll,gl_text(c,"ui.invert.roll.short"));
                            }else checkbox(invert,gl_text(c,"ui.invert.short"));
                            ImGui::PopStyleColor();tooltip=nullptr;
                        }
                    }
                }
                (void)tooltip;
                ImGui::EndDisabled();ImGui::PopID();
            };
            for(uint32_t row=0;row<total;++row){
                gl_setting_info info{};gl_menu_tab_setting_at(c,tab.id,row,&info);
                if(gl_setting_advanced_group(info.id)!=GL_ADVANCED_NONE)continue;
                if(info.visible&&gl_setting_is_activator(info.id)&&!activator_heading){
                    activator_heading=true;
                    const float gutter=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.x;
                    activator_stem=ImGui::GetCursorScreenPos().x+gutter+8*scale;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+gutter);
                    ImGui::TextDisabled("%s",gl_text(c,"ui.activators"));
                    activator_top=ImGui::GetItemRectMax().y+4*scale;
                }
                draw_row(info,false);
                const auto group=control_group(base_key(info.id),info.value);
                ImGui::PushID(info.id);const bool expanded=ImGui::GetStateStorage()->GetBool(ImGui::GetID("advanced-open"));ImGui::PopID();
                if(group&&expanded&&info.visible){
                    const float branch_top=label_bottom+4*scale,stem=parent_stem;child_labels.clear();
                    for(uint32_t n=0;n<total;++n){gl_setting_info option{};gl_menu_tab_setting_at(c,tab.id,n,&option);
                        if(gl_setting_advanced_group(option.id)==group)draw_row(option,true);}
                    if(!child_labels.empty()){
                        const float x=stem;auto* draw=ImGui::GetWindowDrawList();
                        const auto color=IM_COL32(86,137,192,220);
                        draw->AddLine(ImVec2(x,branch_top),ImVec2(x,child_labels.back().y),color,2*scale);
                        for(const auto& label:child_labels)draw->AddLine(ImVec2(x,label.y),ImVec2(label.x-6*scale,label.y),color,2*scale);
                    }
                }
            }
            if(!activator_labels.empty()){
                auto* draw=ImGui::GetWindowDrawList();const auto color=IM_COL32(86,137,192,220);
                draw->AddLine(ImVec2(activator_stem,activator_top),ImVec2(activator_stem,activator_labels.back().y),color,2*scale);
                for(const auto& label:activator_labels)draw->AddLine(ImVec2(activator_stem,label.y),ImVec2(label.x-6*scale,label.y),color,2*scale);
            }
        }
            ImGui::EndChild();ImGui::PopID();ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
        ImGui::Separator();
        draw_automatic_calibration(p,automatic,combo_width,label_above);
        ImGui::SameLine();draw_action(p,calibrate,calibrate_width);
        if(!recommended.visible)ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x-action_width);
        draw_action(p,reset,action_width,true);
        if(recommended.visible){if(!stack_actions)ImGui::SameLine();draw_action(p,recommended,action_width);}
        if(status[0])ImGui::TextWrapped("%s",status);
        if(error[0]){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.5f,.4f,1));ImGui::TextWrapped("%s",error);ImGui::PopStyleColor();}
        }
    }
    ImGui::End();gl_set_panel_open(c,open);ImGui::PopStyleColor();ImGui::PopStyleVar(3);ImGui::PopFont();
}catch(...){if(p)p->result=GL_LIMIT;}
}
