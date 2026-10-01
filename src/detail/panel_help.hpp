#pragma once
#include <imgui.h>
#include <algorithm>
namespace gyrolib_panel_detail {
inline void help(const char* description){
    if(!description||!*description||!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))return;
    // Font-relative width scales with DPI, but can never exceed the viewport.
    const auto* viewport=ImGui::GetMainViewport();
    const float wrap=std::max(40.f,std::min(28*ImGui::GetFontSize(),viewport->WorkSize.x-40.f-2*ImGui::GetStyle().WindowPadding.x));
    ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrap);
    ImGui::TextUnformatted(description);ImGui::PopTextWrapPos();ImGui::EndTooltip();
}
}
