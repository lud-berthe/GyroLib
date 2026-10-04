// Review F: compare the actual tab selected by keyboard and controller opening.
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/panel.h>
#include <gyrolib/runtime.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
int main()try{
#ifdef GL_BUNDLED_RUNTIME
    if(gl_runtime_prepare()!=GL_OK)return 2;
#endif
    SDL_SetMainReady();if(!SDL_Init(SDL_INIT_VIDEO))return 2;
    auto* w=SDL_CreateWindow("Audit F",1024,720,SDL_WINDOW_HIDDEN);auto* renderer=SDL_CreateRenderer(w,nullptr);if(!renderer)return 2;
    gyrolib::Context context;auto* c=context.get();
    const gl_gameplay_context views[]={{1,"Explore","First view",0},{2,"Aim","Active view",1}};
    for(const auto& view:views)gl_register_gameplay_context(c,&view);
    gl_endpoint e{};e.id=e.physical_id=1;e.source=GL_SOURCE_SDL;e.connected=1;e.caps.buttons=(1u<<4)|(1u<<6);gl_register_endpoint(c,&e);
    auto* panel=gl_panel_create(c,nullptr);ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1024,720};io.DeltaTime=1.f/60;
    io.Fonts->AddFontDefaultVector();ImGui_ImplSDLRenderer3_Init(renderer);
    uint32_t active=2;
    uint64_t now=1000000000;const auto update=[&](unsigned buttons){now+=16000000;gl_controls ctl{};ctl.timestamp_ns=now;ctl.buttons=buttons;CHECK(gl_submit_controls(c,1,&ctl)==GL_OK);
        CHECK(gl_set_gameplay_context_state(c,1,active==1,1)==GL_OK);CHECK(gl_set_gameplay_context_state(c,2,active==2,1)==GL_OK);
        gl_host_state host{};host.focused=1;gl_output out{};CHECK(gl_update(c,now,&host,&out)==GL_OK);};
    const auto draw=[&]{ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();gl_panel_draw(panel,1024,720,1);ImGui::Render();
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);};
    const auto selected=[&]{auto* root=ImGui::FindWindowByName("GyroLib###GyroLibPanel");if(!root)return ImGuiID(0);
        auto* bar=GImGui->TabBars.GetByKey(root->GetID("View modes"));return bar?bar->SelectedTabId:ImGuiID(0);};
    update(0);update((1u<<4)|(1u<<6));draw();draw();draw();const auto from_chord=selected();
    const auto tab_bar=[&]{auto* root=ImGui::FindWindowByName("GyroLib###GyroLibPanel");CHECK(root);
        auto* bar=GImGui->TabBars.GetByKey(root->GetID("View modes"));CHECK(bar&&bar->Tabs.Size==2);return bar;};
    const auto first=tab_bar()->Tabs[0].ID,second=tab_bar()->Tabs[1].ID;
    CHECK(from_chord==second); // Explicit expected view, not just equality of two wrong results.
    update(0);update((1u<<4)|(1u<<6));draw(); // close
    gl_panel_function_key(panel,10,1,0);draw();draw();draw();const auto from_keyboard=selected();
    std::cout<<"active_view="<<gl_get_active_gameplay_context(c)<<" chord_tab="<<from_chord<<" keyboard_tab="<<from_keyboard<<" expected=same\n";
    CHECK(from_keyboard==second);
    // A user's edited tab survives host view changes while the panel stays open.
    tab_bar()->NextSelectedTabId=first;draw();draw();CHECK(selected()==first);
    update(0);draw();draw();CHECK(selected()==first);
    // Close/reopen without a closed render frame still selects the active view.
    gl_set_panel_open(c,0);gl_set_panel_open(c,1);draw();draw();CHECK(selected()==second);
    // Remember the view at opening even if the host switches to a menu before draw.
    gl_set_panel_open(c,0);active=1;update(0);gl_set_panel_open(c,1);
    active=2;update(0);draw();draw();CHECK(selected()==first);
    // A frontend created after the opening also gets the right view.
    gl_set_panel_open(c,0);gl_set_panel_open(c,1);gl_panel_destroy(panel);panel=gl_panel_create(c,nullptr);
    CHECK(panel);draw();draw();CHECK(selected()==second);
    // An opening without an active view uses the first exposed view.
    gl_set_panel_open(c,0);active=0;update(0);gl_set_panel_open(c,1);draw();draw();CHECK(selected()==first);
    ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();gl_panel_destroy(panel);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(w);SDL_Quit();
    return from_chord&&from_chord==from_keyboard?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
