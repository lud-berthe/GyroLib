#pragma once
// Presentation example using only the public C API. No settings copies.
#include <gyrolib/gyrolib.h>
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <string>
namespace gyro_profile_widgets {
static bool controller_connected(gl_context* c){
    const auto selected=gl_get_selected_device(c);
    for(uint32_t i=0;i<gl_endpoint_count(c);++i){gl_endpoint e{};
        if(gl_get_endpoint(c,i,&e)==GL_OK&&e.connected&&e.physical_id==selected&&!gl_is_motion_companion(c,e.id))return true;}
    return false;
}
static std::string view_name(gl_context* c,uint32_t id){
    for(uint32_t i=0;i<gl_gameplay_context_count(c);++i){gl_gameplay_context view{};
        if(gl_get_gameplay_context(c,i,&view)==GL_OK&&view.id==id)return view.label;}
    return std::string(gl_text(c,"inherit.view"))+" "+std::to_string(id);
}
static float marker_width(gl_context* c,const char* key){
    gl_setting_inheritance_info info{};
    return gl_setting_inheritance(c,key,&info)==GL_OK&&info.parent_context?
        ImGui::GetFrameHeight()*.8f+ImGui::GetStyle().ItemSpacing.x:0;
}
template<class Restore> static void marker(gl_context* c,const char* key,Restore restore){
    gl_setting_inheritance_info info{};
    if(gl_setting_inheritance(c,key,&info)!=GL_OK||!info.parent_context)return;
    ImGui::SameLine();ImGui::PushID(key);const float size=ImGui::GetFrameHeight()*.8f;
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
    const bool clicked=ImGui::Button("##inherit",ImVec2(size,ImGui::GetFrameHeight()));ImGui::PopStyleColor();
    auto p=ImGui::GetItemRectMin();p.y+=(ImGui::GetFrameHeight()-size)*.5f;
    const auto color=ImGui::GetColorU32(ImVec4(96/255.f,167/255.f,240/255.f,1));auto* draw=ImGui::GetWindowDrawList();
    const float thickness=std::max(1.5f,size*.065f);
    if(info.overridden){
        ImVec2 previous{};for(int n=0;n<=22;++n){const float a=-2.3f+float(n)/22*5.25f;
            ImVec2 next(p.x+size*.52f+std::cos(a)*size*.29f,p.y+size*.53f+std::sin(a)*size*.29f);
            if(n)draw->AddLine(previous,next,color,thickness);previous=next;}
        draw->AddTriangleFilled(ImVec2(p.x+size*.24f,p.y+size*.18f),ImVec2(p.x+size*.49f,p.y+size*.17f),ImVec2(p.x+size*.27f,p.y+size*.41f),color);
    }else{
        for(int loop=0;loop<2;++loop){ImVec2 previous{};
            for(int n=0;n<=24;++n){const float a=float(n)/24*6.2831853f,x=std::cos(a)*.22f,y=std::sin(a)*.12f;
                ImVec2 next(p.x+size*(.39f+.23f*loop+(x-y)*.707f),p.y+size*(.61f-.23f*loop-(x+y)*.707f));
                if(n)draw->AddLine(previous,next,color,thickness);previous=next;}}
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)||ImGui::IsItemFocused()){
        const float wrap=std::max(40.f,std::min(25*ImGui::GetFontSize(),ImGui::GetIO().DisplaySize.x-48));
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrap);
        if(info.overridden)ImGui::TextUnformatted(gl_text(c,"inherit.restore"));
        else ImGui::Text("%s %s",gl_text(c,"inherit.from"),view_name(c,info.source_context).c_str());
        ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
    if(clicked&&info.overridden)restore(key);ImGui::PopID();
}
template<class SetParent> static void parent_selector(gl_context* c,uint32_t view,SetParent set_parent){
    if(gl_gameplay_context_count(c)<2&&!gl_get_context_parent(c,view))return;
    const auto parent=gl_get_context_parent(c,view);
    const auto label=parent?view_name(c,parent):std::string(gl_text(c,"inherit.none"));
    ImGui::BeginDisabled(!controller_connected(c));
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(gl_text(c,"inherit.parent"));ImGui::SameLine();ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##parent",label.c_str())){
        if(ImGui::Selectable(gl_text(c,"inherit.none"),!parent))set_parent(view,0);
        for(uint32_t i=0;i<gl_gameplay_context_count(c);++i){gl_gameplay_context candidate{};gl_get_gameplay_context(c,i,&candidate);
            if(!gl_can_inherit_context(c,view,candidate.id))continue;ImGui::PushID(std::to_string(candidate.id).c_str());
            if(ImGui::Selectable(candidate.label,candidate.id==parent))set_parent(view,candidate.id);ImGui::PopID();}
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered()||ImGui::IsItemFocused()){
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(25*ImGui::GetFontSize(),ImGui::GetIO().DisplaySize.x-48));
        ImGui::TextUnformatted(gl_text(c,"inherit.help"));ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}
}
