#include <gyrolib/gyrolib.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(gl_vec3)==12,"C ABI vector layout");
_Static_assert(sizeof(gl_sample)==40,"C ABI sample layout");
_Static_assert(offsetof(gl_sample,gyro_dps)==16,"C ABI sample offset");
_Static_assert(sizeof(gl_host_state)==32,"C ABI host layout");
_Static_assert(sizeof(gl_controls)==40,"C ABI controls layout unchanged");
_Static_assert(sizeof(gl_output)==48,"C ABI output layout unchanged");
_Static_assert(sizeof(gl_flick_input)==32,"Additional C ABI flick input layout");
_Static_assert(sizeof(gl_touchpad_feedback)==24,"Additional C ABI touchpad feedback layout");
_Static_assert(sizeof(gl_trigger_input)==24,"Additional trigger ABI layout");
_Static_assert(sizeof(gl_gyro_state)==16,"Additional activity ABI layout");
_Static_assert(sizeof(gl_input_metrics)==48,"Additional metrics ABI layout");
int main(void) {
    gl_context* c=gl_create(GL_ABI_VERSION);gl_setting_info info;gl_host_state host={0};gl_output out;
    if(!c||gl_abi_version()!=GL_ABI_VERSION)return 1;
    if(gl_create(999)!=NULL)return 2;
    gl_set_sample_observer(c,NULL,NULL);
    {
        gl_endpoint physical={0};physical.id=physical.physical_id=123;physical.source=GL_SOURCE_SDL;physical.connected=1;
        if(gl_register_endpoint(c,&physical)!=GL_OK||gl_set_endpoint_control_authority(c,123,GL_CONTROL_ALL)!=GL_OK)return 23;
        double clock_scale=0;
        if(gl_get_sensor_clock_scale(c,123,&clock_scale)!=GL_OK||clock_scale!=1)return 25;
        if(gl_set_endpoint_control_authority(c,123,64)!=GL_INVALID||gl_forget_endpoint(c,123)!=GL_OK)return 24;
    }
    if(gl_setting_at(c,0,&info)!=GL_OK)return 3;
    if(gl_get_settings_save_result(c)!=GL_UNAVAILABLE)return 19;
    if(gl_get_menu_key(c)!=10||gl_set_menu_key(c,24)!=GL_OK||gl_get_menu_key(c)!=24)return 20;
    if(gl_set_menu_key(c,0)!=GL_OK||gl_get_menu_key(c)!=0||gl_set_menu_key(c,25)!=GL_INVALID)return 21;
    if(gl_initialize_settings(NULL,NULL,NULL)!=GL_INVALID||*gl_get_settings_path(c))return 22;
    if(gl_get_selected_device(c)!=0||gl_get_output_target(c)!=GL_OUTPUT_CAMERA)return 12;
    if(gl_set_output_target(c,GL_OUTPUT_CURSOR)!=GL_OK||gl_get_output_target(c)!=GL_OUTPUT_CURSOR)return 13;
    if(gl_set_output_target(c,GL_OUTPUT_CAMERA)!=GL_OK)return 14;
    {
        gl_gameplay_context mode={57,"Host C mode","Resolved command",20};double x=0;gl_choice choice;
        if(gl_register_gameplay_context(c,&mode)!=GL_OK)return 5;
        if(gl_set_gameplay_context_output_target(c,57,GL_OUTPUT_CAMERA)!=GL_OK)return 18;
        gl_menu_tab tab;
        if(gl_menu_tab_count(c)!=1||gl_menu_tab_at(c,0,&tab)!=GL_OK||tab.id!=58||tab.context_id!=57)return 6;
        if(gl_menu_shared_setting_count(c)!=6||gl_menu_shared_setting_at(c,0,&info)!=GL_OK)return 17;
        if(gl_menu_setting_count(c)!=gl_setting_count()+gl_menu_tab_setting_count(c,58))return 15;
        if(gl_menu_tab_setting_at(c,58,0,&info)!=GL_OK)return 16;
        if(gl_setting_set(c,"context.57.sensitivity_x",3.2)!=GL_OK)return 7;
        if(gl_setting_get(c,"context.57.sensitivity_x",&x)!=GL_OK||x!=3.2)return 8;
        if(gl_menu_choice_count(c,"gyro.context")!=2)return 9;
        if(gl_set_gameplay_context_state(c,57,1,1)!=GL_OK)return 10;
        if(gl_choice_at(c,"gyro.context",1,&choice)!=GL_OK||choice.value!=57||!choice.available)return 11;
    }
    if(gl_update(c,1,&host,&out)!=GL_OK)return 4;
    if(gl_get_active_gameplay_context(c)!=57)return 17;
    gl_destroy(c);puts("C ABI consumer passed");return 0;
}
