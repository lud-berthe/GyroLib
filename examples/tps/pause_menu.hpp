#pragma once
#include "host.hpp"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace tps {
// Example host-owned frontend. Only the public C menu model is used here:
// no gl_panel_draw, private GyroLib headers, copied settings or INI parser.
struct PauseMenu {
    uint64_t tab_id{};
    bool opened{},select_tab=true;
    int32_t result=GL_OK;

    static const char* text(gl_context* c,const char* en,const char* fr){
        return std::strcmp(gl_get_language(c),"fr")==0?fr:en;
    }
    static void help(const char* description){
        if(!description||!*description||(!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)&&!ImGui::IsItemFocused()))return;
        const float wrap=std::min(28*ImGui::GetFontSize(),ImGui::GetIO().DisplaySize.x-64);
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrap);
        ImGui::TextUnformatted(description);ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
    static const char* suffix(const char* id){
        if(std::strncmp(id,"context.",8))return id;
        const auto* p=std::strchr(id+8,'.');return p?p+1:id;
    }
    static uint32_t control_group(const char* key,double value){
        if(!std::strcmp(key,"gyro.activation")&&int(value)==GL_HOLD_DISABLE)return GL_ADVANCED_HOLD_DISABLE;
        if(!std::strcmp(key,"gyro.smoothing_ms")&&value>0)return GL_ADVANCED_SMOOTHING;
        if(!std::strcmp(key,"gyro.acceleration")&&value>0)return GL_ADVANCED_ACCELERATION;
        if(!std::strcmp(key,"flick.mode")&&int(value)!=GL_FLICK_OFF)return GL_ADVANCED_FLICK;
        return GL_ADVANCED_NONE;
    }
    void widget(gl_context* c,const gl_setting_info& s,float width){
        ImGui::PushID(s.id);ImGui::BeginDisabled(!s.available);ImGui::SetNextItemWidth(width);
        if(s.type==GL_SETTING_BOOL){
            bool value=s.value!=0;if(ImGui::Checkbox("##value",&value))result=gl_setting_set(c,s.id,value);
        }else if(s.type==GL_SETTING_NUMBER){
            const int count=int(std::round((s.maximum-s.minimum)/s.step));
            int tick=int(std::round((s.value-s.minimum)/s.step));
            char format[80];const auto* unit=std::strcmp(s.unit,"%")==0?"%%":s.unit;
            std::snprintf(format,sizeof(format),s.step>=1?"%.0f %s":"%.2f %s",s.value,unit);
            if(s.value==0&&std::strcmp(suffix(s.id),"gyro.smoothing_ms")==0)std::snprintf(format,sizeof(format),"%s",gl_text(c,"off"));
            if(ImGui::SliderInt("##value",&tick,0,count,format))result=gl_setting_set(c,s.id,s.minimum+tick*s.step);
        }else if(s.type==GL_SETTING_ENUM){
            const char* preview=gl_text(c,"unavailable");
            for(uint32_t n=0;n<gl_menu_choice_count(c,s.id);++n){gl_choice choice{};
                if(gl_choice_at(c,s.id,n,&choice)==GL_OK&&choice.value==s.value)preview=choice.label;}
            if(ImGui::BeginCombo("##value",preview)){
                for(uint32_t n=0;n<gl_menu_choice_count(c,s.id);++n){gl_choice choice{};
                    if(gl_choice_at(c,s.id,n,&choice)!=GL_OK||!choice.available)continue;
                    ImGui::PushID(int(n));
                    if(ImGui::Selectable(choice.label,choice.value==s.value))result=gl_setting_set(c,s.id,choice.value);
                    if(choice.value==s.value)ImGui::SetItemDefaultFocus();
                    help(gl_choice_description(c,s.id,choice.value));ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }else if(s.type==GL_SETTING_ACTION){if(ImGui::Button(s.label,ImVec2(width,0)))result=gl_action(c,s.id);}
        help(s.description);ImGui::EndDisabled();ImGui::PopID();
    }
    void controllers(gl_context* c){
        std::vector<gl_endpoint> devices,sensors;
        for(uint32_t i=0;i<gl_endpoint_count(c);++i){gl_endpoint e{};
            if(gl_get_endpoint(c,i,&e)!=GL_OK||!e.connected)continue;
            if(gl_is_motion_companion(c,e.id)){sensors.push_back(e);continue;}
            if(std::none_of(devices.begin(),devices.end(),[&](auto& d){return d.physical_id==e.physical_id;}))devices.push_back(e);
        }
        const auto selected=gl_get_selected_device(c);
        const char* preview=gl_text(c,selected?"ui.controller.disconnected":"ui.controller.none");
        for(const auto& d:devices)if(d.physical_id==selected)preview=d.name;
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"ui.controller"));ImGui::SameLine();ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##controller",preview)){
            for(const auto& d:devices){ImGui::PushID(std::to_string(d.physical_id).c_str());
                if(ImGui::Selectable(d.name,d.physical_id==selected))result=gl_select_device(c,d.physical_id);ImGui::PopID();}
            ImGui::EndCombo();
        }
        help(gl_text(c,"warning.double"));
        if(gl_motion_sensor_needs_selection(c,selected)){
            const auto bound=gl_get_motion_sensor(c,selected);const char* sensor=gl_text(c,"ui.sensor.choose");
            for(const auto& e:sensors)if(e.id==bound)sensor=e.name;
            ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"ui.sensor"));ImGui::SameLine();ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##sensor",sensor)){
                for(const auto& e:sensors){if(!gl_endpoint_motion_available(c,e.id))continue;ImGui::PushID(std::to_string(e.id).c_str());
                    if(ImGui::Selectable(e.name,e.id==bound))result=gl_bind_motion_sensor(c,selected,e.id);ImGui::PopID();}
                ImGui::EndCombo();
            }
            help(gl_text(c,"ui.sensor.help"));
        }
    }
    void rows(gl_context* c,uint64_t tab,float scale){
        // Re-query each frame: capabilities, calibration and edits from F10 may
        // have changed. Native engines can instead invalidate on gl_poll_event.
        const uint32_t count=gl_menu_tab_setting_count(c,tab);
        std::vector<gl_setting_info> rows;
        for(uint32_t i=0;i<count;++i){gl_setting_info s{};
            if(gl_menu_tab_setting_at(c,tab,i,&s)==GL_OK&&s.visible)rows.push_back(s);}
        const auto find=[&](const char* key)->const gl_setting_info*{
            for(const auto& s:rows)if(std::strcmp(suffix(s.id),key)==0)return &s;return nullptr;};
        if(ImGui::BeginTable("profile",2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg)){
            ImGui::TableSetupColumn("label",0,.43f);ImGui::TableSetupColumn("value",0,.57f);
            std::vector<ImVec2> child_labels,activator_labels;
            float label_bottom=0,parent_stem=0,activator_top=0,activator_stem=0;
            bool activator_heading=false;
            const auto draw_row=[&](const gl_setting_info& s,bool detail){
                const auto* key=suffix(s.id);const auto group=control_group(key,s.value);
                if(!std::strcmp(key,"gyro.invert_x")||!std::strcmp(key,"gyro.invert_y")||!std::strcmp(key,"gyro.invert_roll")||!std::strcmp(key,"activation.short_press")||
                    !std::strcmp(key,"activation.stick_threshold")||!std::strcmp(key,"activation.trigger_threshold"))return;
                ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();
                const float start=ImGui::GetCursorPosX();
                const float button_width=ImGui::GetFrameHeight(),gutter=button_width+ImGui::GetStyle().ItemSpacing.x;
                float parent_bottom=0;
                parent_stem=ImGui::GetCursorScreenPos().x+button_width*.5f;
                if(group){
                    ImGui::PushID(s.id);const auto id=ImGui::GetID("advanced-open");
                    auto* storage=ImGui::GetStateStorage();const bool expanded=storage->GetBool(id);
                    ImGui::BeginDisabled(!s.available);
                    if(ImGui::Button(expanded?"-###advanced-toggle":"+###advanced-toggle",ImVec2(button_width,0)))storage->SetBool(id,!expanded);
                    parent_bottom=ImGui::GetItemRectMax().y;
                    ImGui::EndDisabled();help(gl_text(c,group==GL_ADVANCED_SMOOTHING?"ui.advanced.smoothing":
                        group==GL_ADVANCED_ACCELERATION?"ui.advanced.acceleration":
                        group==GL_ADVANCED_HOLD_DISABLE?"ui.advanced.hold_disable":"ui.advanced.flick"));ImGui::PopID();
                    ImGui::SameLine(start+gutter);
                }else ImGui::SetCursorPosX(start+gutter+(detail?16*scale:gl_setting_is_activator(s.id)?24*scale:0));
                ImGui::AlignTextToFramePadding();
                ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(s.label);ImGui::PopTextWrapPos();help(s.description);
                const auto label_min=ImGui::GetItemRectMin(),label_max=ImGui::GetItemRectMax();label_bottom=std::max(label_max.y,parent_bottom);
                if(detail)child_labels.push_back(ImVec2(label_min.x,(label_min.y+label_max.y)*.5f));
                if(gl_setting_is_activator(s.id))activator_labels.push_back(ImVec2(label_min.x,(label_min.y+label_max.y)*.5f));
                ImGui::TableNextColumn();const auto width=ImGui::GetContentRegionAvail().x;
                const bool axis=!std::strcmp(key,"sensitivity_x")||!std::strcmp(key,"sensitivity_y");
                const auto* beside=axis?find(!std::strcmp(key,"sensitivity_x")?"gyro.invert_x":"gyro.invert_y"):
                    !std::strcmp(key,"activation.button")?find("activation.short_press"):nullptr;
                const auto* roll=!std::strcmp(key,"sensitivity_x")?find("gyro.invert_roll"):nullptr;
                const auto* limit=!std::strcmp(key,"activation.trigger")?find("activation.trigger_threshold"):
                    !std::strcmp(key,"activation.stick_deflection")?find("activation.stick_threshold"):nullptr;
                if(beside){
                    const auto* label=gl_text(c,axis?"ui.invert.short":"ui.short_press.short");
                    const auto checkbox_width=[&](const char* name){return ImGui::GetFrameHeight()+ImGui::CalcTextSize(name).x+ImGui::GetStyle().ItemInnerSpacing.x;};
                    const float gap=ImGui::GetStyle().ItemSpacing.x;
                    const float reserved=roll?ImGui::CalcTextSize(label).x+checkbox_width(gl_text(c,"ui.invert.yaw.short"))+
                        checkbox_width(gl_text(c,"ui.invert.roll.short"))+3*gap:checkbox_width(label)+gap;
                    widget(c,s,std::max(40.f,width-reserved));ImGui::SameLine();
                    const auto checkbox=[&](const gl_setting_info& setting,const char* name){
                        ImGui::PushID(setting.id);ImGui::BeginDisabled(!setting.available);bool inverted=setting.value!=0;
                        if(ImGui::Checkbox(name,&inverted))result=gl_setting_set(c,setting.id,inverted);
                        help(setting.description);ImGui::EndDisabled();ImGui::PopID();
                    };
                    if(roll){
                        ImGui::AlignTextToFramePadding();ImGui::TextDisabled("%s",label);ImGui::SameLine();
                        checkbox(*beside,gl_text(c,"ui.invert.yaw.short"));ImGui::SameLine();
                        checkbox(*roll,gl_text(c,"ui.invert.roll.short"));
                    }else checkbox(*beside,label);
                }else if(limit){
                    const auto* label=gl_text(c,"ui.threshold.short");
                    const float reserved=ImGui::CalcTextSize(label).x+110*scale+2*ImGui::GetStyle().ItemSpacing.x;
                    widget(c,s,std::max(40.f,width-reserved));ImGui::SameLine();ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s",label);help(limit->description);ImGui::SameLine();
                    ImGui::PushID(limit->id);ImGui::BeginDisabled(!limit->available);ImGui::SetNextItemWidth(110*scale);
                    int tick=int(std::round((limit->value-limit->minimum)/limit->step));
                    const int ticks=int(std::round((limit->maximum-limit->minimum)/limit->step));
                    char format[32];std::snprintf(format,sizeof(format),"%.2f",limit->value);
                    if(ImGui::SliderInt("##threshold",&tick,0,ticks,format))result=gl_setting_set(c,limit->id,limit->minimum+tick*limit->step);
                    help(limit->description);ImGui::EndDisabled();ImGui::PopID();
                }else widget(c,s,-1);
            };
            for(const auto& s:rows){
                if(gl_setting_advanced_group(s.id)!=GL_ADVANCED_NONE)continue;
                if(gl_setting_is_activator(s.id)&&!activator_heading){
                    activator_heading=true;ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                    const float gutter=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.x;
                    activator_stem=ImGui::GetCursorScreenPos().x+gutter+8*scale;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+gutter);
                    ImGui::TextDisabled("%s",gl_text(c,"ui.activators"));activator_top=ImGui::GetItemRectMax().y+4*scale;
                }
                draw_row(s,false);
                const auto group=control_group(suffix(s.id),s.value);
                ImGui::PushID(s.id);const bool expanded=ImGui::GetStateStorage()->GetBool(ImGui::GetID("advanced-open"));ImGui::PopID();
                if(group&&expanded){
                    const float branch_top=label_bottom+4*scale,stem=parent_stem;child_labels.clear();
                    for(const auto& option:rows)if(gl_setting_advanced_group(option.id)==group)draw_row(option,true);
                    if(!child_labels.empty()){
                        ImGui::TableSetColumnIndex(0);
                        const float x=stem;auto* draw=ImGui::GetWindowDrawList();
                        const auto color=IM_COL32(80,159,167,220);
                        draw->AddLine(ImVec2(x,branch_top),ImVec2(x,child_labels.back().y),color,2*scale);
                        for(const auto& label:child_labels)draw->AddLine(ImVec2(x,label.y),ImVec2(label.x-6*scale,label.y),color,2*scale);
                    }
                }
            }
            if(!activator_labels.empty()){
                ImGui::TableSetColumnIndex(0);auto* draw=ImGui::GetWindowDrawList();const auto color=IM_COL32(80,159,167,220);
                draw->AddLine(ImVec2(activator_stem,activator_top),ImVec2(activator_stem,activator_labels.back().y),color,2*scale);
                for(const auto& label:activator_labels)draw->AddLine(ImVec2(activator_stem,label.y),ImVec2(label.x-6*scale,label.y),color,2*scale);
            }
            ImGui::EndTable();
        }
    }
    float calibration(gl_context* c,float scale,bool measure=false){
        gl_setting_info automatic{},action{},reset{};
        for(uint32_t i=0;i<gl_menu_shared_setting_count(c);++i){gl_setting_info s{};
            if(gl_menu_shared_setting_at(c,i,&s)!=GL_OK||!s.visible)continue;
            if(!std::strcmp(s.id,"calibration.automatic"))automatic=s;
            else if(!std::strcmp(s.id,"settings.reset"))reset=s;
            else if(!std::strcmp(s.id,"calibration.begin")||!std::strcmp(s.id,"calibration.cancel"))action=s;
        }
        gl_diagnostics d{};gl_get_diagnostics(c,&d);
        const char* key=d.calibration_state==GL_CAL_COLLECTING?"calibration.collecting":d.calibration_state==GL_CAL_MOVING?"calibration.moving":
            d.calibration_state==GL_CAL_COMPLETE?"calibration.complete":d.calibration_state==GL_CAL_EXTERNAL?"calibration.steamHelp":nullptr;
        char status[512]{},error[512]{};
        if(d.calibration_state==GL_CAL_COUNTDOWN)std::snprintf(status,sizeof(status),"%s %.1f s",gl_text(c,"calibration.countdown"),d.calibration_seconds_remaining);
        else if(key)std::snprintf(status,sizeof(status),"%s",gl_text(c,key));
        const auto saved=gl_get_settings_save_result(c);
        if(saved!=GL_OK&&saved!=GL_UNAVAILABLE)std::snprintf(error,sizeof(error),"%s (%d)",gl_text(c,"ui.save_failed"),saved);
        else if(result!=GL_OK)std::snprintf(error,sizeof(error),"%s (%d)",gl_text(c,"ui.operation_failed"),result);
        const float width=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemSpacing.x;
        const float total=ImGui::CalcTextSize(gl_text(c,"ui.auto_calibration")).x+170*scale+
            ImGui::CalcTextSize(action.label).x+ImGui::CalcTextSize(reset.label).x+4*ImGui::GetStyle().FramePadding.x+3*gap;
        const bool wrap=total>width;
        const float height=(wrap?2:1)*ImGui::GetFrameHeightWithSpacing()+12*scale+
            (status[0]?ImGui::CalcTextSize(status,nullptr,false,width).y+gap:0)+
            (error[0]?ImGui::CalcTextSize(error,nullptr,false,width).y+gap:0);
        if(measure)return height;
        ImGui::Separator();ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"ui.auto_calibration"));ImGui::SameLine();
        widget(c,automatic,170*scale);
        if(!wrap)ImGui::SameLine();
        widget(c,action,0);ImGui::SameLine();widget(c,reset,0);
        if(status[0])ImGui::TextWrapped("%s",status);
        if(error[0])ImGui::TextWrapped("%s",error);
        return height;
    }
    void draw(gl_context* c,Host& host,ImVec2 view,float dpi,bool focused){
        if(!host.paused){opened=false;return;}
        const bool entering=!opened;opened=true;
        const float scale=std::clamp(std::max({1.f,dpi,view.y/1080}),.75f,std::max(.75f,std::min(2.5f,view.y/760)));
        ImGui::PushFont(nullptr,18*scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(24*scale,22*scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10*scale,7*scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(12*scale,10*scale));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(10*scale,6*scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,10*scale);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.045f,.085f,.125f,1));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.085f,.17f,.21f,1));
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.10f,.29f,.31f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.15f,.43f,.43f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.19f,.52f,.48f,1));
        ImGui::PushStyleColor(ImGuiCol_CheckMark,ImVec4(.35f,.86f,.75f,1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,ImVec4(.35f,.78f,.73f,1));
        ImGui::SetNextWindowPos(ImVec2(view.x*.5f,view.y*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
        ImGui::SetNextWindowSize(host.gyro_menu?ImVec2(std::min(view.x-40*scale,1120*scale),view.y-96*scale):ImVec2(410*scale,245*scale),ImGuiCond_Always);
        if(entering)ImGui::SetNextWindowFocus();
        ImGui::Begin("Pause###DemoPause",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginDisabled(!focused||gl_panel_open(c));
        if(!host.gyro_menu){
            ImGui::TextUnformatted("PAUSE");ImGui::Separator();
            if(ImGui::Button(text(c,"Resume","Reprendre"),ImVec2(-1,48*scale)))host.paused=false;
            if(entering)ImGui::SetItemDefaultFocus();
            if(ImGui::Button(text(c,"Gyro settings","Réglages gyro"),ImVec2(-1,48*scale))){host.gyro_menu=true;select_tab=true;}
        }else{
            if(ImGui::Button(text(c,"Back","Retour")))host.gyro_menu=false;
            ImGui::SameLine();ImGui::TextUnformatted(text(c,"GYRO SETTINGS","RÉGLAGES GYRO"));
            const char* codes[]={"en","fr","de","es","it","pt"};const char* labels[]={"English","Français","Deutsch","Español","Italiano","Português"};
            int language=0;for(int i=0;i<6;++i)if(!std::strcmp(gl_get_language(c),codes[i]))language=i;
            ImGui::SameLine(ImGui::GetWindowWidth()-190*scale);ImGui::SetNextItemWidth(165*scale);
            if(ImGui::Combo("##language",&language,labels,6))result=gl_set_language(c,codes[language]);
            ImGui::Separator();controllers(c);
            if(!gl_menu_tab_setting_count(c,tab_id)){gl_menu_tab tab{};if(gl_menu_tab_at(c,0,&tab)==GL_OK)tab_id=tab.id;select_tab=true;}
            const float footer=calibration(c,scale,true);
            if(ImGui::BeginTabBar("NativeViews",ImGuiTabBarFlags_FittingPolicyScroll)){
                for(uint32_t i=0;i<gl_menu_tab_count(c);++i){gl_menu_tab tab{};gl_menu_tab_at(c,i,&tab);
                    const auto label=std::string(tab.label)+"###native-view-"+std::to_string(tab.id);
                    const bool requested=select_tab&&tab_id==tab.id;
                    if(ImGui::BeginTabItem(label.c_str(),nullptr,requested?ImGuiTabItemFlags_SetSelected:0)){
                        tab_id=tab.id;select_tab=false;
                        if(ImGui::BeginChild("NativeRows",ImVec2(0,-footer),ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_AlwaysVerticalScrollbar))rows(c,tab.id,scale);
                        ImGui::EndChild();ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            calibration(c,scale);
        }
        ImGui::EndDisabled();ImGui::End();ImGui::PopStyleColor(7);ImGui::PopStyleVar(5);ImGui::PopFont();
    }
};
}
