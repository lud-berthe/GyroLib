#include <gyrolib/gyrolib.h>
#include <gyrolib/sdl.h>
#include <gyrolib/runtime.h>
#ifdef GL_CONSUMER_SDL
#include <SDL3/SDL.h>
#endif
#include <stdio.h>
#include <math.h>
int main(void){
    gl_context* c=gl_create(GL_ABI_VERSION);double value=0;
    if(!c)return 1;
    if(gl_get_settings_save_result(c)!=GL_UNAVAILABLE)return 14;
    {
        gl_gameplay_context mode={501,"Installed host mode","Host-localized description",30};gl_choice choice;
        if(gl_register_gameplay_context(c,&mode)!=GL_OK)return 4;
        if(gl_setting_set(c,"context.501.sensitivity_x",3.5)!=GL_OK)return 2;
        if(gl_setting_get(c,"context.501.sensitivity_x",&value)!=GL_OK||value!=3.5)return 3;
        if(gl_set_gameplay_context_output_target(c,501,GL_OUTPUT_CURSOR)!=GL_OK)return 9;
        if(gl_set_gameplay_context_state(c,501,1,1)!=GL_OK||gl_get_output_target(c)!=GL_OUTPUT_CURSOR)return 10;
        if(gl_setting_set(c,"context.501.sensitivity_y",1.7)!=GL_OK)return 5;
        if(gl_setting_get(c,"context.501.sensitivity_y",&value)!=GL_OK||fabs(value-1.7)>1e-9)return 6;
        if(gl_menu_setting_count(c)!=gl_setting_count()+gl_menu_tab_setting_count(c,502)||gl_menu_tab_count(c)!=1)return 7;
        if(gl_menu_shared_setting_count(c)!=6)return 13;
        if(gl_choice_at(c,"gyro.context",1,&choice)!=GL_OK||choice.value!=501)return 8;
    }
#ifdef GL_BUNDLED_RUNTIME
    {
        gl_sdl* reader;
        if(gl_runtime_prepare()!=GL_OK){puts(gl_runtime_error());return 15;}
#ifdef GL_CONSUMER_SDL
        if(!SDL_Init(SDL_INIT_GAMEPAD))return 17;
        reader=gl_sdl_create(c,1);
#else
        reader=gl_sdl_create(c,0);
#endif
        if(!reader||gl_sdl_pump_events(reader)!=GL_OK||gl_sdl_poll(reader,1000000000)!=GL_OK)return 16;
        gl_sdl_destroy(reader);
#ifdef GL_CONSUMER_SDL
        SDL_Quit();
#endif
    }
#endif
    gl_destroy(c);puts("Installed C consumer passed");return 0;
}
