#include <gyrolib/gyrolib.h>
#include <gyrolib/sdl.h>
#include <gyrolib/runtime.h>
#ifdef GL_CONSUMER_SDL
#include <SDL3/SDL.h>
#endif
#include <stdio.h>
#include <math.h>
#include <string.h>
int main(void){
    gl_context* c=gl_create(GL_ABI_VERSION);double value=0;
    if(!c)return 1;
    if(gl_get_settings_save_result(c)!=GL_UNAVAILABLE)return 14;
    {
        gl_gameplay_context mode={501,"Installed host mode","Host-localized description",30};gl_choice choice;
        if(gl_register_gameplay_context(c,&mode)!=GL_OK)return 4;
        if(gl_set_gameplay_context_camera_in_menu(c,501,1)!=GL_OK)return 19;
        if(gl_setting_set(c,"context.501.sensitivity_x",3.5)!=GL_OK)return 2;
        if(gl_setting_get(c,"context.501.sensitivity_x",&value)!=GL_OK||value!=3.5)return 3;
        if(gl_setting_get_effective(c,"context.501.sensitivity_x",&value)!=GL_OK||value!=3.5)return 31;
        if(gl_set_gameplay_context_output_target(c,501,GL_OUTPUT_CURSOR)!=GL_OK)return 9;
        if(gl_set_gameplay_context_state(c,501,1,1)!=GL_OK||gl_get_output_target(c)!=GL_OUTPUT_CURSOR)return 10;
        {
            uint32_t opening_view=0;
            if(gl_get_panel_opening(c,&opening_view)!=0||opening_view!=0)return 35;
            gl_set_panel_open(c,1);
            if(gl_get_panel_opening(c,&opening_view)!=1||opening_view!=501)return 36;
            gl_set_panel_open(c,0);
        }
        if(gl_setting_set(c,"context.501.sensitivity_y",1.7)!=GL_OK)return 5;
        if(gl_setting_get(c,"context.501.sensitivity_y",&value)!=GL_OK||fabs(value-1.7)>1e-9)return 6;
        if(gl_menu_setting_count(c)!=gl_setting_count()+gl_menu_tab_setting_count(c,502)||gl_menu_tab_count(c)!=1)return 7;
        if(gl_menu_shared_setting_count(c)!=7)return 13;
        if(gl_choice_at(c,"gyro.context",1,&choice)!=GL_OK||choice.value!=501)return 8;
    }
    {
        gl_gameplay_context child={502,"Child view","",20};gl_setting_inheritance_info inheritance;
        if(gl_has_recommended_settings(c))return 20;
        if(gl_register_gameplay_context(c,&child)!=GL_OK||!gl_can_inherit_context(c,502,501))return 21;
        if(gl_set_context_parent(c,502,501)!=GL_OK||gl_get_context_parent(c,502)!=501)return 22;
        if(gl_setting_get(c,"context.502.sensitivity_x",&value)!=GL_OK||value!=3.5)return 23;
        if(gl_capture_recommended_settings(c)!=GL_OK||!gl_has_recommended_settings(c))return 24;
        if(gl_setting_set(c,"context.502.sensitivity_x",6)!=GL_OK)return 25;
        if(gl_setting_inheritance(c,"context.502.sensitivity_x",&inheritance)!=GL_OK||!inheritance.overridden)return 26;
        if(gl_setting_inherit(c,"context.502.sensitivity_x")!=GL_OK)return 27;
        if(gl_setting_get(c,"context.502.sensitivity_x",&value)!=GL_OK||value!=3.5)return 28;
        gl_reset_settings(c);
        if(gl_apply_recommended_settings(c)!=GL_OK||gl_get_context_parent(c,502)!=501)return 29;
        if(gl_setting_get(c,"context.502.sensitivity_x",&value)!=GL_OK||value!=3.5)return 30;
    }
    {
        gl_gameplay_context view={UINT32_MAX,"Long event ID","",0};gl_event_ex event;int found=0;
        const char* key="context.4294967295.flick.touchpad_release_threshold";
        if(gl_register_gameplay_context(c,&view)!=GL_OK)return 32;
        while(gl_poll_event_ex(c,&event)>0){}
        if(gl_setting_set(c,key,.25)!=GL_OK)return 33;
        while(gl_poll_event_ex(c,&event)>0)if(event.type==GL_EVENT_SETTING&&!strcmp(event.setting_id,key))found=1;
        if(!found)return 34;
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
