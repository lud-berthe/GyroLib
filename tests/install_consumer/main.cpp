#include <gyrolib/gyrolib.hpp>
#include <gyrolib/steam.h>
#ifdef GL_CONSUMER_PANEL
#include <gyrolib/panel.h>
#endif
#include <cstdio>

static uint32_t GL_CALL handles(void*,uint64_t*,uint32_t){return 0;}
static uint32_t GL_CALL motion(void*,uint64_t,gl_steam_motion*){return 0;}

int main() try {
    gyrolib::Context context;
    context.register_view(1,"Installed camera");
    gl_host_state host{};
    // No real game is attached: leave host permissions disabled.
#ifdef GL_BUNDLED_RUNTIME
    gyrolib::SdlInput input(context);
    const auto output=input.update(1000000000,host,1);
#else
    const auto output=context.update(1000000000,host,1);
#endif
    if(output.yaw_degrees||output.pitch_degrees)return 1;
    gl_steam_provider provider{nullptr,handles,motion,nullptr};
    gl_steam* steam=gl_steam_create(context.get(),&provider);
    if(!steam)return 2;
    const auto result=gl_steam_poll(steam,1000000001,1);
    gl_steam_destroy(steam);
    if(result!=GL_OK)return 3;
#ifdef GL_CONSUMER_PANEL
    gl_panel* panel=gl_panel_create(context.get(),nullptr);
    if(!panel)return 4;
    gl_panel_function_key(panel,10,1,0);
    const bool open=gl_panel_open(context.get())!=0;
    gl_panel_destroy(panel);
    if(!open)return 5;
#endif
    std::puts("Installed C++ consumer passed");
    return 0;
} catch(const std::exception& error){
    std::fprintf(stderr,"%s\n",error.what());return 6;
}
