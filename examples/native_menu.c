/* Real C consumer: replace print_widget with your engine's widget construction.
 * No UI toolkit or C++ objects cross the library boundary. */
#include <gyrolib/gyrolib.h>
#include <stdio.h>
#include <string.h>
static void apply_camera(void* user,double yaw,double pitch) {
    double* camera=(double*)user;camera[0]+=yaw;camera[1]+=pitch;
}
static void build_native_menu(gl_context* gyro) {
    uint32_t t,i;
    /* Shared controls are outside tabs. Build the controller selector above them
     * from connected gl_endpoint records, deduplicated by physical_id. Exclude
     * gl_is_motion_companion endpoints from this controller list. Show
     * gl_get_selected_device and call gl_select_device on a selection change.
     * If gl_motion_sensor_needs_selection(gyro, selected_physical_id) is true,
     * expose a fallback sensor selector. Otherwise pairing needs no widget. Display
     * gl_get_motion_sensor(gyro, selected_physical_id); on explicit user choice,
     * call gl_bind_motion_sensor(gyro, selected_physical_id, sensor_endpoint_id).
     * Offer only gl_endpoint_motion_available sensors, including the default
     * controller sensor. Retain the current binding during temporary stalls.
     * Sensor ID 0 clears the binding. This association lasts only for the
     * connected session. Never infer it from matching names or device counts. */
    printf("\nShared controls (header/footer):\n");
    for(i=0;i<gl_menu_shared_setting_count(gyro);++i){
        gl_setting_info s;
        if(gl_menu_shared_setting_at(gyro,i,&s)!=GL_OK||!s.visible)continue;
        printf("%s: %s [%s]\n",s.id,s.label,s.available?"available":"disabled");
        /* Place calibration.automatic, calibration.begin
         * or calibration.cancel together in the footer; keep settings.reset separate.
         * On action click call gl_action(gyro,s.id). Refresh visibility after
         * updates/actions: Cancel replaces Recalibrate during manual calibration.
         * gl_get_diagnostics supplies countdown, progress and movement feedback.
         * After registering all views/defaults, gl_initialize_settings(gyro,NULL,NULL)
         * loads/creates girolib.ini. Check its result before editing. This console
         * fixture deliberately uses no file. The retired ui.scale row is hidden.
         * Setting/language edits and reset then auto-save. Display errors
         * from gl_get_settings_save_result; edits remain applied in memory. */
    }
    for(t=0;t<gl_menu_tab_count(gyro);++t){
        gl_menu_tab tab;gl_menu_tab_at(gyro,t,&tab);
        printf("\n[%s] %s\n",tab.label,tab.description);
        /* Bind your tab widget to tab.id (uint64), never its translated label.
         * Selecting an editor tab does not set the host's gameplay state. */
    for(i=0;i<gl_menu_tab_setting_count(gyro,tab.id);++i) {
        gl_setting_info s;
        if(gl_menu_tab_setting_at(gyro,tab.id,i,&s)!=GL_OK||!s.visible)continue;
        printf("%s: %s = %.2f [%s]\n",s.id,s.label,s.value,s.available?"available":"disabled");
        /* Build checkbox/slider/combo/action from type, range, step, defaults.
         * For enums, use gl_menu_choice_count(gyro,s.id) and gl_choice_at.
         * Persist choice.value, never its enumeration index. Skip unavailable choices
         * in the dropdown. Acceleration Custom (4) is not a selectable preset;
         * use its label for the current value after a detailed curve edit.
         * On choice hover/focus show gl_choice_description(gyro,s.id,choice.value)
         * in a wrapped tooltip bounded to the viewport.
         * On widget edit: gl_setting_set(gyro, s.id, widget_value).
         * On reset: gl_action(gyro, "settings.reset").
         * On calibration: gl_action(gyro, "calibration.begin").
         * No Save widget: the setter persists each change to the configured path.
         * Poll GL_EVENT_SETTING to refresh widgets after other frontends edit.
         * Rebuild metadata on GL_EVENT_CONTEXT / GL_EVENT_HOST_CAPABILITIES. */
    }
    }
}
int main(void) {
    gl_context* gyro=gl_create(GL_ABI_VERSION);double camera[2]={0,0};
    gl_host_state host={0};gl_output output;gl_event event;
    if(!gyro)return 1;gl_set_camera_callback(gyro,apply_camera,camera);
    /* At least one view is required. Modes belong to this example host. IDs stay
     * unchanged across versions/languages. Localize labels in your mod. */
    {
        const gl_gameplay_context normal={1,"Free camera","Ordinary view with no special command.",0};
        const gl_gameplay_context bow={10,"Bow drawn","The demo host's bow command is held.",10};
        const gl_gameplay_context scope={42,"Scope","The demo host's scope command is held.",20};
        gl_register_gameplay_context(gyro,&normal);gl_register_gameplay_context(gyro,&bow);gl_register_gameplay_context(gyro,&scope);
        gl_set_gameplay_context_state(gyro,1,1,1);
        gl_set_gameplay_context_state(gyro,10,0,1);
        gl_set_gameplay_context_state(gyro,42,0,1);
    }
    build_native_menu(gyro);
    host.camera_allowed=host.focused=1;
    /* Playable host-owned widgets: examples/tps/pause_menu.hpp.
     * Keep menu_open true for a CURSOR view such as inventory. Menus-only
     * calibration excludes that view until paused or covered by the library panel. */
    /* Report each registered mode before EVERY update from resolved commands.
     * An omitted/unavailable state cannot select a profile.
     * Process adapters, then update. Never change a game action from this observer. */
    gl_update(gyro,1000000000,&host,&output);
    {
        gl_gyro_state state={0};gl_get_gyro_state(gyro,&state);
        /* state.enabled is stable between sensor reports. A host can use it for
         * its gyro indicator or its own aim-assist policy. state.new_samples is
         * acquisition activity, not an enable/disable command. */
    }
    /* If output.suppress_native_right_stick, skip YOUR native stick camera path.
     * Do not modify character movement or actions. */
    while(gl_poll_event(gyro,&event)==1){}
    gl_destroy(gyro);return 0;
}
