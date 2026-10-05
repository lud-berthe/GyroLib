#ifndef GYROLIB_H
#define GYROLIB_H
#include <stdint.h>
#if defined(_WIN32) && defined(GL_SHARED)
# if defined(GL_BUILD)
#  define GL_API __declspec(dllexport)
# else
#  define GL_API __declspec(dllimport)
# endif
#else
# define GL_API
#endif
#if defined(_WIN32)
# define GL_CALL __cdecl
#else
# define GL_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* ABI 1: these structs are frozen. Future extensions use new versioned functions.
 * All calls for a context must be serialized on its owner thread. See API.md.
 * Input vectors: right-handed SDL axes, X right, Y up, Z toward player.
 * Gyro deg/s; acceleration g; output yaw right / pitch up in degrees.
 * Clock: caller's monotonic nanoseconds. Sensor timestamps have a per-source epoch.
 *
 * First integration: gl_create -> gl_register_gameplay_context -> input adapter
 * creation -> [report view + acquire input + gl_update each frame] -> destroy
 * adapter -> gl_destroy. README.md has a complete C++ example; docs/API.md
 * documents C integration. All settings, diagnostics and extra hooks below are
 * optional. Never apply both callback and returned movement for the same frame.
 */
#define GL_ABI_VERSION 1u
#include "version.h"
typedef struct gl_context gl_context;
enum { GL_OK=0, GL_INVALID=-1, GL_UNAVAILABLE=-2, GL_IO_ERROR=-3,
       GL_NEWER_SCHEMA=-4, GL_LIMIT=-5 };
enum { GL_SOURCE_NONE=0, GL_SOURCE_SDL=1, GL_SOURCE_STEAM=2 };
enum { GL_OUTPUT_CAMERA=0, GL_OUTPUT_CURSOR=1 };
enum { GL_SPACE_PLAYER=0, GL_SPACE_LOCAL_YAW=1, GL_SPACE_LOCAL_ROLL=2, GL_SPACE_WORLD=3,
       GL_SPACE_LOCAL_YAW_ROLL=4, GL_SPACE_LOCAL_ADVANCED=5, GL_SPACE_LASER_POINTER=6,
       GL_SPACE_PLAYER_LEAN=7, GL_SPACE_WORLD_LEAN=8 };
enum { GL_FLICK_STYLE_FULL=0, GL_FLICK_STYLE_PIVOT_ONLY=1, GL_FLICK_STYLE_ROTATE_ONLY=2 };
/* GYRO_OFF stops gyro output, including host enable overrides; orientation and
 * settings are retained and Flick remains independent.
 * ALWAYS ignores activation bindings. HOLD_DISABLE suspends gyro while an
 * activation binding is held.
 * Optional activation.temporary_invert/activation.trackball behaviors reuse
 * those bindings and replace suspension during HOLD_DISABLE's hold only.
 * TOGGLE's live on/off state is shared by all views (including cursor views),
 * retained across other activation modes, and starts on for a new context. */
/* Per-view input.steam_mouse. Interception requires confirmed mouse motion and the
   Windows overlay or SDL window bridge; physical mouse events pass through. */
enum { GL_STEAM_MOUSE_PASSTHROUGH=0, GL_STEAM_MOUSE_BLOCK=1, GL_STEAM_MOUSE_CONVERT=2 };
enum { GL_ALWAYS=0, GL_CONTEXT_ONLY=1, GL_OUTSIDE_CONTEXT=2, GL_HOLD=3, GL_TOGGLE=4, GL_HOLD_DISABLE=5, GL_GYRO_OFF=6 };
/* Deprecated values retained for loading old files; new profiles reject them. */
enum { GL_AIM_ONLY=GL_CONTEXT_ONLY, GL_OUTSIDE_AIM=GL_OUTSIDE_CONTEXT };
enum { GL_SIDE_OFF=0, GL_SIDE_LEFT=1, GL_SIDE_RIGHT=2, GL_SIDE_EITHER=3, GL_SIDE_BOTH=4 };
enum { GL_LEFT=1, GL_RIGHT=2, GL_SINGLE=4 }; /* contact capability/state bits */
enum { GL_CONTACT_NONE=0, GL_CONTACT_TOUCHPAD=1, GL_CONTACT_STICK=2, GL_CONTACT_GRIP=3 };
enum { GL_CAL_OFF=0, GL_CAL_MENUS=1, GL_CAL_ANYTIME=2 };
enum { GL_CAL_IDLE=0, GL_CAL_COUNTDOWN=1, GL_CAL_COLLECTING=2,
       GL_CAL_MOVING=3, GL_CAL_COMPLETE=4, GL_CAL_EXTERNAL=5 };
enum { GL_FLICK_OFF=0, GL_FLICK_OUTSIDE_CONTEXT=1, GL_FLICK_ON=2, GL_FLICK_CONTEXT_ONLY=3,
       GL_FLICK_TOUCHPAD=4, GL_FLICK_BOTH=5, GL_FLICK_STICK=GL_FLICK_ON };
enum { GL_FLICK_OUTSIDE_AIM=GL_FLICK_OUTSIDE_CONTEXT }; /* deprecated alias */
enum { GL_PRESS=0, GL_RELEASE=1, GL_REPEAT=2 };
enum { GL_FORWARD=0, GL_SUPPRESS=1, GL_EMIT_TAP=2 };
enum { GL_EVENT_SOURCE=1, GL_EVENT_DEVICE=2, GL_EVENT_SETTING=3,
       GL_EVENT_CALIBRATION=4, GL_EVENT_INVALID_SAMPLE=5,
       GL_EVENT_OVERFLOW=6, GL_EVENT_DOUBLE_GYRO=7, GL_EVENT_ASSOCIATION=8,
       GL_EVENT_CONTEXT=9, GL_EVENT_HOST_CAPABILITIES=10, GL_EVENT_BUTTON_LABELS=11 };
enum { GL_LABEL_DEVICE=1, GL_LABEL_PHYSICAL=2 };
/* Control-family authority describes physical controls, not virtual game output.
 * A known absent family (capability zero) also overrides virtual controls. */
enum { GL_CONTROL_BUTTONS=1u, GL_CONTROL_TOUCHPADS=2u, GL_CONTROL_STICK_TOUCH=4u,
       GL_CONTROL_GRIP_TOUCH=8u, GL_CONTROL_STICKS=16u, GL_CONTROL_TRIGGERS=32u, GL_CONTROL_ALL=63u };
/* MENU_STATE: host.menu_open is a reliable observation of the game's menus.
 * Withdraw when the hook is unavailable. Required for menus-only calibration. */
enum { GL_HOST_NATIVE_STICK_SUPPRESSION=1u, GL_HOST_LONG_PRESS_BLOCKING=2u,
       GL_HOST_MENU_STATE=4u, GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION=8u };
/* Button bits use SDL_GamepadButton ordinal 0..31, independent of labels. */
/* activation.button choices: 0 off, 1..32 the corresponding bit + 1.
 * Legacy pair IDs 33..40 remain loadable but are unavailable in menu choices. */
enum { GL_BUTTON_SHOULDERS_EITHER=33, GL_BUTTON_SHOULDERS_BOTH=34,
       GL_BUTTON_STICKS_EITHER=35, GL_BUTTON_STICKS_BOTH=36,
       GL_BUTTON_REAR1_EITHER=37, GL_BUTTON_REAR1_BOTH=38,
       GL_BUTTON_REAR2_EITHER=39, GL_BUTTON_REAR2_BOTH=40 };
typedef struct gl_vec3 { float x,y,z; } gl_vec3;
typedef struct gl_capabilities {
    uint32_t buttons, touchpads, stick_touch, grip_touch, sticks;
    uint32_t gyro, accelerometer;
} gl_capabilities;
typedef struct gl_endpoint {
    uint64_t id;               /* nonzero, unique across both sources */
    uint64_t physical_id;      /* nonzero identity group; companions may use a documented session pairing */
    uint32_t source, connected;
    gl_capabilities caps;
    char name[128];            /* UTF-8, zero terminated */
} gl_endpoint;
typedef struct gl_controls {
    uint64_t timestamp_ns;     /* caller monotonic clock, independent of sensors */
    uint32_t buttons, touchpads, stick_touch, grip_touch;
    float left_x,left_y,right_x,right_y; /* [-1,1], right/up positive */
} gl_controls;
/* Additive ABI extension: actual trigger travel, [0,1], released=0.
 * available is LEFT/RIGHT (digital triggers may report 0/1). Submit on the
 * owner thread; expires after 150 ms. No game action is emitted or filtered. */
typedef struct gl_trigger_input {
    uint64_t timestamp_ns;
    uint32_t available;
    float left,right;
} gl_trigger_input;
GL_API int32_t GL_CALL gl_submit_trigger_input(gl_context*,uint64_t endpoint,const gl_trigger_input*);
GL_API int32_t GL_CALL gl_get_trigger_input(const gl_context*,gl_trigger_input*);
/* Copied UTF-8 physical labels, e.g. LT/RT or L2/R2. NULL clears the label. */
/* A changed trigger label emits GL_EVENT_BUTTON_LABELS with detail -1 (refresh
 * labels) and the endpoint ID. Repeating the same label emits nothing. */
GL_API int32_t GL_CALL gl_set_trigger_label(gl_context*,uint64_t endpoint,uint32_t side,const char* label);
GL_API const char* GL_CALL gl_get_trigger_label(const gl_context*,uint32_t side);
/* Optional additional input, separate from frozen ABI 1 controls. Supply real
 * right-stick axes and a separately identified right touchpad, never a virtual
 * stick already driven by that pad. Both use [-1,1], X right / Y up, centre 0.
 * A single central touchpad is not a right touchpad. No touch => touching=0.
 * Timestamp uses the caller's monotonic clock; data expires after 150 ms. */
enum { GL_FLICK_INPUT_STICK=1u, GL_FLICK_INPUT_TOUCHPAD=2u };
typedef struct gl_flick_input {
    uint64_t timestamp_ns;
    uint32_t available, touching;
    float stick_x,stick_y,touchpad_x,touchpad_y;
} gl_flick_input;
typedef struct gl_sample {
    uint64_t sensor_ns, arrival_ns;
    gl_vec3 gyro_dps, accel_g;
} gl_sample;
typedef struct gl_host_state {
    uint32_t aiming, alt_fire; /* reserved legacy fields, ignored; use named contexts */
    uint32_t menu_open, paused, focused, camera_allowed;
    uint32_t suspend_long_press_blocking; /* native interactions requiring hold */
    uint32_t steam_gyro_output;   /* 0 unknown, 1 verified disabled, 2 known enabled */
} gl_host_state;
typedef struct gl_output {
    double yaw_degrees, pitch_degrees;
    uint32_t gyro_active, suppress_native_right_stick, source;
    uint64_t physical_id, endpoint_id;
} gl_output;
/* Query after update. enabled is a stable permission with a qualified source,
 * independent of whether that frame received a sensor report. The legacy
 * output.gyro_active describes processed gyro samples, not this permission.
 * new_samples counts reports processed by the last update. */
typedef struct gl_gyro_state {
    uint32_t enabled,motion_available,new_samples,trackball_active;
} gl_gyro_state;
GL_API int32_t GL_CALL gl_get_gyro_state(const gl_context*,gl_gyro_state*);
/* Optional host modifiers, consumed by the next update, never persisted.
 * Override: -1 uses the selected activation mode, 0 suspends, 1 enables.
 * All still respect enabled setting, focus, pause, menus and calibration.
 * Inversion combines with saved inversion; trackball holds the preceding
 * gyro velocity and decays it (flick is unaffected). No host actions change. */
enum { GL_MOD_INVERT_X=1u, GL_MOD_INVERT_Y=2u, GL_MOD_TRACK_X=4u, GL_MOD_TRACK_Y=8u };
GL_API int32_t GL_CALL gl_set_gyro_modifiers(gl_context*,uint32_t flags);
GL_API int32_t GL_CALL gl_set_gyro_override(gl_context*,int32_t activation);
typedef struct gl_diagnostics {
    uint64_t accepted_samples, rejected_samples, dropped_samples;
    gl_vec3 gravity, bias, calibrated_dps;
    uint32_t calibration_state, stationary;
    float calibration_seconds_remaining;
    uint32_t source;
} gl_diagnostics;
typedef struct gl_event {
    uint32_t type;
    int32_t detail;
    uint64_t endpoint_id;
    double value;
    char setting_id[48];
} gl_event;

/* Extended notification. The original gl_event layout remains ABI-compatible. */
typedef struct gl_event_ex {
    uint32_t type;
    int32_t detail;
    uint64_t endpoint_id;
    double value;
    char setting_id[128];
} gl_event_ex;
typedef void (GL_CALL *gl_camera_callback)(void*,double yaw_degrees,double pitch_degrees);
/* Optional accepted-sample observer for diagnostics. Invoked synchronously by
 * gl_submit_sample on the context owner thread, after validation, before motion
 * processing. Units/axes are gl_sample's normalized deg/s and g, not raw HID.
 * The sample pointer is borrowed only during this call. Do not mutate/re-enter
 * the context, block or throw from the observer. NULL disables observation. */
typedef void (GL_CALL *gl_sample_observer)(void*,uint64_t endpoint,const gl_sample*);
GL_API void GL_CALL gl_set_sample_observer(gl_context*,gl_sample_observer,void*);
/* Device timestamp seconds -> monotonic seconds, estimated over a stable SDL
 * stream. Initially 1; a previous estimate is retained while requalifying after
 * wake. Steam's host-frame clock remains 1. Diagnostics
 * only: no user sensitivity multiplier or calibration of Steam Input. */
GL_API int32_t GL_CALL gl_get_sensor_clock_scale(const gl_context*,uint64_t endpoint,double* scale);
/* Acquisition diagnostics, not end-to-end camera latency. Report frequency and
 * interval jitter use up to 64 accepted sensor intervals, corrected by the
 * estimated clock scale; age uses the caller's monotonic last-update clock.
 * An unqualified clock may retain its previous estimate after wake. */
typedef struct gl_input_metrics {
    double report_hz,interval_jitter_ms,clock_scale;
    uint64_t sample_age_ns;
    uint32_t gyro_available,gravity_available,clock_qualified;
} gl_input_metrics;
GL_API int32_t GL_CALL gl_get_input_metrics(const gl_context*,uint64_t endpoint,gl_input_metrics*);
/* Deprecated: use gl_recenter_step_callback / gl_set_recenter_step_callback.
 * Retained for existing binaries; migration is in docs/COMPATIBILITY.md. */
typedef void (GL_CALL *gl_recenter_callback)(void*);
/* Instant-only camera action, called after angular deltas on the owner thread.
 * Center only host camera pitch. Never reset gyro orientation or trigger aiming.
 * NULL withdraws support and hides the button setting. Requests are consumed by
 * the next update and discarded if unfocused/paused/on a cursor view or in menus
 * unless the active camera view explicitly permits camera output there. */
GL_API void GL_CALL gl_set_recenter_callback(gl_context*,gl_recenter_callback,void*);
/* Optional progressive replacement for the instant callback. Each owner-thread
 * call follows camera deltas and supplies a fraction (0 < fraction <= 1) of the
 * remaining pitch to remove: pitch += (level_pitch - pitch) * fraction.
 * GyroLib owns duration/easing; 1 finishes exactly at level. Do not multiply by dt
 * or sensitivity. Requests restart from the current pitch. Focus/pause/panel,
 * view/device changes and withdrawing support cancel the pending transition.
 * Either callback setter replaces the other. Only this one exposes duration. */
typedef void (GL_CALL *gl_recenter_step_callback)(void*,double fraction);
GL_API void GL_CALL gl_set_recenter_step_callback(gl_context*,gl_recenter_step_callback,void*);
GL_API int32_t GL_CALL gl_request_recenter(gl_context*);
/* Declare per-view zoom support once, then report actual vertical FOV and its
 * unzoomed reference in degrees (0 < FOV < 179) before EVERY update. Missing or
 * withdrawn reports bypass compensation. Only gyro is scaled, never flick.
 * This host observation does not modify the player's saved sensitivities. */
GL_API int32_t GL_CALL gl_set_gameplay_context_zoom_available(gl_context*,uint32_t id,uint32_t available);
GL_API int32_t GL_CALL gl_set_gameplay_context_fov(gl_context*,uint32_t id,double current_degrees,double reference_degrees);
/* Optional per-update veto for games that know aiming must remain uncompensated
 * by automatic bias learning (e.g. tracking a target). Resets to allowed after
 * gl_update. Manual calibration and Steam's calibration are unaffected. */
GL_API int32_t GL_CALL gl_set_auto_calibration_allowed(gl_context*,uint32_t allowed);
/* Presentation hints; advanced settings use the same menu/change/persistence
 * API. Place each group beside its main control. Zoom is an ordinary camera
 * row. Unknown IDs return NONE. No C ABI 1 structs are changed. */
/* MODIFIERS is a reserved legacy group. Respect each child's visible flag;
 * Off features have no visible children and no expander. */
enum { GL_ADVANCED_NONE=0, GL_ADVANCED_SMOOTHING=1,
       GL_ADVANCED_ACCELERATION=2, GL_ADVANCED_FLICK=3, GL_ADVANCED_MODIFIERS=4,
       GL_ADVANCED_HOLD_DISABLE=5, GL_ADVANCED_RECENTER=6 };
GL_API uint32_t GL_CALL gl_setting_advanced_group(const char* setting_id);
/* Activator families and their inline thresholds/long-press blocking controls. Use to
 * group visible rows under a localized Activators heading in native menus. */
GL_API uint32_t GL_CALL gl_setting_is_activator(const char* setting_id);
GL_API uint32_t GL_CALL gl_setting_is_advanced(const char* setting_id);

GL_API uint32_t GL_CALL gl_abi_version(void);
GL_API gl_context* GL_CALL gl_create(uint32_t abi_version);
GL_API void GL_CALL gl_destroy(gl_context*);
GL_API void GL_CALL gl_set_camera_callback(gl_context*,gl_camera_callback,void* user);
/* Default CAMERA. CURSOR returns the same processed angular deltas, but never
 * calls the camera callback or enables flick/native-stick suppression. It requires
 * GL_HOST_MENU_STATE + host.menu_open, focus, no pause and a closed F10 panel.
 * The host converts degrees to its local UI coordinates; no OS input is emitted.
 * Changing target retains orientation and calibration. Settings still apply.
 * A target explicitly declared on the active named mode takes precedence.
 * gl_get_output_target returns the effective destination. */
GL_API int32_t GL_CALL gl_set_output_target(gl_context*,uint32_t target);
GL_API uint32_t GL_CALL gl_get_output_target(const gl_context*);
/* Optional host hooks, absent by default. Declare only implemented integrations. */
GL_API int32_t GL_CALL gl_set_host_capabilities(gl_context*,uint32_t flags);
GL_API uint32_t GL_CALL gl_get_host_capabilities(const gl_context*);
/* Stable mod-owned IDs, 1..UINT32_MAX; never derive IDs from array order or language.
 * Strings are copied. Re-register to change localized metadata; saved values survive.
 * Higher priority selects the entire independent profile; ties use the lowest ID.
 * Generated settings: context.<id>.sensitivity_x/y (legacy IDs retained),
 * context.<id>.<base-setting-id> for all other per-mode gyro/flick settings.
 * Calibration and presentation remain shared. New modes start with defaults.
 * Every integration must register at least one view, including a single camera.
 * No implicit/default view, built-in gameplay contexts or fixed context-count limit. */
typedef struct gl_gameplay_context {
    uint32_t id;
    const char *label,*description;
    int32_t priority;
} gl_gameplay_context;
GL_API int32_t GL_CALL gl_register_gameplay_context(gl_context*,const gl_gameplay_context*);
/* Declare the mode's output destination as CAMERA or CURSOR. This is host
 * integration metadata, not a saved player preference. The winning profile
 * selects its declared target automatically; CURSOR hides flick settings,
 * disables flick/native-stick suppression and never invokes the camera callback.
 * Undeclared legacy modes continue to use gl_set_output_target dynamically.
 * Re-registering localized metadata preserves this declaration. */
GL_API int32_t GL_CALL gl_set_gameplay_context_output_target(gl_context*,uint32_t id,uint32_t target);
/* Opt a registered camera view into non-pausing game menus. Default false.
 * Requires GL_HOST_MENU_STATE and camera_allowed on every update; focus, pause,
 * panel and active-view gates still apply. Enables gyro, flick and recenter,
 * never long-press blocking filtering of menu commands. Cursor routing is unaffected.
 * Integration metadata only: not a saved user preference. */
GL_API int32_t GL_CALL gl_set_gameplay_context_camera_in_menu(gl_context*,uint32_t id,uint32_t allowed);
GL_API int32_t GL_CALL gl_unregister_gameplay_context(gl_context*,uint32_t id);
/* Saved single-parent inheritance. Owner thread. First linking a view inherits
 * all its settings; switching parent keeps explicit overrides. parent=0 freezes
 * current effective values and detaches. Registered parents only, no cycles.
 * View metadata, host capabilities and runtime state are never inherited. */
GL_API int32_t GL_CALL gl_set_context_parent(gl_context*,uint32_t view,uint32_t parent);
GL_API uint32_t GL_CALL gl_get_context_parent(const gl_context*,uint32_t view);
GL_API uint32_t GL_CALL gl_can_inherit_context(const gl_context*,uint32_t view,uint32_t parent);
typedef struct gl_setting_inheritance_info {
    uint32_t parent_context,source_context,overridden;
    double parent_value;
} gl_setting_inheritance_info;
/* source_context is the actual ancestor providing the value, or this view for
 * a local setting. parent_context=0 means independent. Strings/IDs stay stable.
 * gl_setting_set makes a local override even when the number is unchanged. */
GL_API int32_t GL_CALL gl_setting_inheritance(const gl_context*,const char* setting_id,gl_setting_inheritance_info*);
GL_API int32_t GL_CALL gl_setting_inherit(gl_context*,const char* setting_id);
/* Capture the mod author's complete current view settings, inheritance and
 * automatic-calibration preference BEFORE loading the player's INI. Opt-in;
 * does not save/apply/change the player's settings. Never call on each frame.
 * The snapshot belongs to this context, is not persisted, and survives Reset.
 * Applying restores captured views; other views and language/menu key stay put.
 * Native menu action: settings.recommended, visible only after capture. */
GL_API int32_t GL_CALL gl_capture_recommended_settings(gl_context*);
GL_API uint32_t GL_CALL gl_has_recommended_settings(const gl_context*);
GL_API int32_t GL_CALL gl_apply_recommended_settings(gl_context*);
GL_API uint32_t GL_CALL gl_gameplay_context_count(const gl_context*);
/* Winning observed mode, or 0 when none is active. Zero always suspends output
 * and new filtering, including when no modes are registered. Acquisition,
 * calibration and orientation tracking continue. The host reports its normal view too.
 * Menu selection never changes this state. Query after gl_update. */
GL_API uint32_t GL_CALL gl_get_active_gameplay_context(const gl_context*);
GL_API int32_t GL_CALL gl_get_gameplay_context(const gl_context*,uint32_t index,gl_gameplay_context*);
/* Report actual resolved commands before EACH gl_update. Omitted reports become
 * unavailable, not inactive. These observations never change host actions or
 * prevent editing a registered view's settings. */
GL_API int32_t GL_CALL gl_set_gameplay_context_state(gl_context*,uint32_t id,uint32_t active,uint32_t available);
GL_API int32_t GL_CALL gl_register_endpoint(gl_context*,const gl_endpoint*);
/* Optional UTF-8 name for SDL button ordinal 0..31; copied, nullptr clears it.
 * DEVICE identifies the exposed layout; PHYSICAL requires verified physical
 * origin (e.g. Steam action origins), overriding a paired virtual layout.
 * Metadata never adds capabilities. All calls use the context's owner thread. */
GL_API int32_t GL_CALL gl_set_button_label(gl_context*,uint64_t endpoint,uint32_t button,const char* label,uint32_t provenance);
GL_API const char* GL_CALL gl_get_button_label(const gl_context*,uint32_t button);
GL_API int32_t GL_CALL gl_disconnect_endpoint(gl_context*,uint64_t endpoint);
GL_API int32_t GL_CALL gl_forget_endpoint(gl_context*,uint64_t endpoint);
GL_API uint32_t GL_CALL gl_endpoint_count(const gl_context*);
GL_API int32_t GL_CALL gl_get_endpoint(const gl_context*,uint32_t index,gl_endpoint*);
/* Qualified gyro at the last gl_update time: connected gyro,
 * at least two increasing sensor timestamps, latest arrival <150 ms old.
 * Queries do not select or bind a device. Useful for native sensor choices. */
GL_API uint32_t GL_CALL gl_endpoint_motion_available(const gl_context*,uint64_t endpoint);
/* Acquisition metadata: mark a controller reported by Steam Input. SDL fills
 * this from its Steam handle; a generic virtual controller is not sufficient.
 * Used to expose mouse settings before the first mouse movement. No action
 * mapping is inferred. Zero withdraws the metadata. */
GL_API int32_t GL_CALL gl_set_endpoint_steam_input(gl_context*,uint64_t endpoint,uint32_t present);
/* Optional acquisition metadata for automatic companion pairing. Zero vendor
 * disables the hint. Product IDs may differ between USB receivers and virtual
 * pads. A unique virtual controller / companion pair with the same vendor can
 * be associated after 300 ms of fresh motion; this is a session inference, not
 * proof of persistent identity. Multiple same-vendor devices require a choice.
 * Never derive these IDs from a display name. Existing bindings are retained. */
GL_API int32_t GL_CALL gl_set_endpoint_pairing_hint(gl_context*,uint64_t endpoint,
    uint32_t vendor_id,uint32_t product_id,uint32_t virtual_controller);
/* Show a fallback sensor picker only when automatic pairing cannot decide.
 * Query after gl_update. A bound or directly usable sensor hides the picker. */
GL_API uint32_t GL_CALL gl_motion_sensor_needs_selection(const gl_context*,uint64_t physical_id);
/* Reassign to a verified physical identity; resets source temporal state. */
GL_API int32_t GL_CALL gl_associate_endpoint(gl_context*,uint64_t endpoint,uint64_t physical_id);
/* Motion-only companions never select a player automatically. A native menu can
 * enumerate them with gl_get_endpoint + gl_is_motion_companion, then ask the user
 * which sensor belongs to their selected controller. Binding is explicit, not
 * inferred from a name/device count, and lasts for this connected session.
 * sensor_endpoint=0 removes that controller's binding. No game actions change.
 * A bound companion with nonzero caps.buttons owns gyro button activators;
 * its physical buttons replace virtual button states, even while stale (then
 * released), keeping labels and press origins consistent. Motion-only companions
 * preserve ordinary button selection. Providers can declare physical authority
 * per family through gl_set_endpoint_control_authority. See INPUT.md for pairing. */
GL_API int32_t GL_CALL gl_set_motion_companion(gl_context*,uint64_t endpoint);
GL_API uint32_t GL_CALL gl_is_motion_companion(const gl_context*,uint64_t endpoint);
GL_API int32_t GL_CALL gl_bind_motion_sensor(gl_context*,uint64_t physical_id,uint64_t sensor_endpoint);
GL_API uint64_t GL_CALL gl_get_motion_sensor(const gl_context*,uint64_t physical_id);
/* 0 selects the first connected ordinary controller. Selection stays while it is
 * connected; after disconnect another ordinary controller can take over on update.
 * A silent motion stream alone never changes the selected controller. */
GL_API int32_t GL_CALL gl_select_device(gl_context*,uint64_t physical_id);
/* Current explicit/automatic physical selection, or 0 before first selection. */
GL_API uint64_t GL_CALL gl_get_selected_device(const gl_context*);
GL_API int32_t GL_CALL gl_submit_sample(gl_context*,uint64_t endpoint,const gl_sample*);
GL_API int32_t GL_CALL gl_submit_controls(gl_context*,uint64_t endpoint,const gl_controls*);
/* Optional provider metadata. The endpoint's capabilities/states are the physical
 * authority for these families. Only applies to the selected physical group;
 * supplemental motion companions must first be associated. Zero withdraws it.
 * Known absent controls never fall back to a virtual Xbox-shaped layout, even
 * while physical input is stale (stale pressed states still expire normally).
 * Does not alter the game's command inputs or associate devices by itself. */
GL_API int32_t GL_CALL gl_set_endpoint_control_authority(gl_context*,uint64_t endpoint,uint32_t families);
GL_API int32_t GL_CALL gl_submit_flick_input(gl_context*,uint64_t endpoint,const gl_flick_input*);
/* Selected physical input, resolved independently for stick and touchpad
 * (bound companion first, then SDL), otherwise legacy controls' right stick.
 * Each family expires independently; timestamp_ns is the newest included
 * report. Processing retains each family's own clock and backlog. */
GL_API int32_t GL_CALL gl_get_flick_input(const gl_context*,gl_flick_input*);
/* Last update's request to suppress native right-touchpad camera output. Hosts
 * must wire this before declaring GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION. */
GL_API uint32_t GL_CALL gl_suppress_native_right_touchpad(const gl_context*);
/* Optional short pulse requested by actual right-touchpad flick movement in
 * the latest update. No feedback for stick-only, cursor, pause, focus loss or
 * panel open. Read on the owner thread after gl_update; dispatch each timestamp
 * at most once. The core never drives hardware. No pulse means pulse=0. */
typedef struct gl_touchpad_feedback {
    uint64_t timestamp_ns,physical_id;
    uint32_t pulse;
} gl_touchpad_feedback;
GL_API int32_t GL_CALL gl_get_touchpad_feedback(const gl_context*,gl_touchpad_feedback*);
/* Mark a button alias for a genuine contact already exposed in caps/controls.
 * The menu omits the duplicate; selected aliases resolve to the dedicated family
 * without rewriting saved bindings. family=NONE clears metadata; side is LEFT/RIGHT/SINGLE.
 * No capability or contact state is invented. ABI 1 structs stay unchanged. */
GL_API int32_t GL_CALL gl_set_button_contact(gl_context*,uint64_t endpoint,uint32_t button,uint32_t family,uint32_t side);
GL_API int32_t GL_CALL gl_update(gl_context*,uint64_t now_ns,const gl_host_state*,gl_output*);
GL_API int32_t GL_CALL gl_get_diagnostics(const gl_context*,gl_diagnostics*);
GL_API int32_t GL_CALL gl_poll_event(gl_context*,gl_event*); /* 1 event, 0 empty */
/* Same queue as gl_poll_event: use one polling API per consumer. Legacy polling
   reports GL_EVENT_CONTEXT/detail=6 (refresh all settings) for IDs over 47 bytes. */
GL_API int32_t GL_CALL gl_poll_event_ex(gl_context*,gl_event_ex*);
GL_API int32_t GL_CALL gl_begin_calibration(gl_context*);
GL_API void GL_CALL gl_cancel_calibration(gl_context*);
/* Filter only explicitly connected NON-AIM actions. EMIT_TAP means synthesize a
 * press+release of that game action, locally; never inject OS events. key 0..63.
 * eligible identifies an event from the selected button. Only enabled gyro,
 * Hold/Hold-to-disable and activation.block_long_press=1 can defer it. Physical
 * controls still feed acquisition immediately. Report current views before
 * events; update host safety regularly and pass native_hold for native holds. */
GL_API int32_t GL_CALL gl_filter_event(gl_context*,uint32_t key,uint32_t event,
    uint64_t now_ns,uint32_t eligible,uint32_t native_hold);

enum { GL_SETTING_BOOL=0, GL_SETTING_NUMBER=1, GL_SETTING_ENUM=2, GL_SETTING_ACTION=3 };
typedef struct gl_setting_info {
    const char *id,*label,*description,*unit;
    uint32_t type,visible,available;
    double value,minimum,maximum,step,default_value;
} gl_setting_info;
typedef struct gl_choice { double value; const char* label; uint32_t available; } gl_choice;
/* Acceleration Custom (4) is a derived current value, not a preset:
 * its choice remains addressable for the label with available=0.
 * Display it when selected, but omit it from the selectable choices. */
/* Shared controls live outside the view tabs: calibration and reset.
 * Legacy save/scale rows are retained in enumeration but hidden: edits auto-save
 * and the panel always scales automatically from DPI and viewport resolution.
 * Respect visible/available; calibration.cancel replaces calibration.begin during
 * manual countdown/collection, including movement retries. Refresh after actions,
 * input updates and GL_EVENT_CALIBRATION. */
GL_API uint32_t GL_CALL gl_menu_shared_setting_count(const gl_context*);
GL_API int32_t GL_CALL gl_menu_shared_setting_at(const gl_context*,uint32_t index,gl_setting_info*);
/* Stable view tabs: 1 + the host's uint32 mode ID. Zero modes means zero tabs.
 * ID 1 is reserved/retired and never enumerated. Never truncate tab IDs.
 * Labels/descriptions are borrowed under the same lifetime rules as settings.
 * active/available describe runtime observation and output selection, not
 * editability. Every registered view remains configurable while inactive,
 * unavailable or unreported. Never disable a tab using these flags; respect
 * each setting/choice's own capability-dependent visible/available metadata. */
/* Legacy GL_TAB_GENERAL getters alias shared settings; 0 is never enumerated as
 * a tab. GL_TAB_DEFAULT is the legacy name for GL_TAB_CAMERA. */
enum { GL_TAB_GENERAL=0, GL_TAB_CAMERA=1, GL_TAB_DEFAULT=GL_TAB_CAMERA };
typedef struct gl_menu_tab {
    uint64_t id;
    uint32_t context_id,active,available;
    const char *label,*description;
} gl_menu_tab;
GL_API uint32_t GL_CALL gl_menu_tab_count(const gl_context*);
GL_API int32_t GL_CALL gl_menu_tab_at(const gl_context*,uint32_t index,gl_menu_tab*);
GL_API uint32_t GL_CALL gl_menu_tab_setting_count(const gl_context*,uint64_t tab_id);
GL_API int32_t GL_CALL gl_menu_tab_setting_at(const gl_context*,uint64_t tab_id,uint32_t index,gl_setting_info*);
/* Use context-aware counts for every new menu, including native integrations.
 * Legacy counts enumerate only static rows/choices, not registered contexts. */
GL_API uint32_t GL_CALL gl_setting_count(void);
GL_API uint32_t GL_CALL gl_menu_setting_count(const gl_context*);
GL_API int32_t GL_CALL gl_setting_at(const gl_context*,uint32_t index,gl_setting_info*);
/* Per-view values require context.<id> keys. Former bare gyro/activation/flick
 * keys and ui.scale return GL_UNAVAILABLE; old metadata slots remain hidden for
 * ABI stability. calibration.automatic remains a shared numeric setting. */
GL_API int32_t GL_CALL gl_setting_get(const gl_context*,const char* id,double* value);
/* get returns the inherited preference, independent of the current controller.
 * get_effective and menu metadata return the capability-adapted runtime value.
 * Combined inputs fall back to their available member; absent inputs become Off.
 * This never changes the preference, inheritance overrides or INI. Query after
 * input polling/update on the owner thread; stale reports count as unavailable. */
GL_API int32_t GL_CALL gl_setting_get_effective(const gl_context*,const char* id,double* value);
GL_API int32_t GL_CALL gl_setting_set(gl_context*,const char* id,double value);
/* Changes auto-save synchronously on the owner thread when a path is configured.
 * Save errors leave the edit applied in memory and are returned by setters/actions.
 * Query the last persistence result independently of other operations: UNAVAILABLE
 * means no path, OK means ready/saved, other errors mean changes are not persisted.
 * Load and reset batch their edits; load never writes, reset saves once. */
GL_API int32_t GL_CALL gl_get_settings_save_result(const gl_context*);
GL_API uint32_t GL_CALL gl_choice_count(const char* setting_id);
GL_API uint32_t GL_CALL gl_menu_choice_count(const gl_context*,const char* setting_id);
GL_API int32_t GL_CALL gl_choice_at(const gl_context*,const char* id,uint32_t index,gl_choice*);
/* Localized help for one choice VALUE (not its display index). Static lifetime;
 * returns an empty string for invalid/unsupported values. ABI 1 structs unchanged. */
GL_API const char* GL_CALL gl_choice_description(const gl_context*,const char* id,double value);
GL_API int32_t GL_CALL gl_action(gl_context*,const char* id);
GL_API void GL_CALL gl_reset_settings(gl_context*);
GL_API int32_t GL_CALL gl_set_language(gl_context*,const char* language); /* en/fr/de/es/it/pt */
GL_API const char* GL_CALL gl_get_language(const gl_context*);
GL_API const char* GL_CALL gl_text(const gl_context*,const char* key); /* static lifetime */
/* Optional panel shortcut: 1..24 means F1..F24; 0 disables keyboard opening.
 * Default 10. Persisted as ui.menu_key=F10 (empty value for 0). Resetting gyro
 * settings preserves this host/user preference. No OS hooks or input capture. */
GL_API int32_t GL_CALL gl_set_menu_key(gl_context*,uint32_t function_number);
GL_API uint32_t GL_CALL gl_get_menu_key(const gl_context*);
/* Back + Start (normalized button bits 4 and 6) toggles the supplied panel.
 * Enabled by default; persisted as ui.gamepad_menu_shortcut=1 (0 disables).
 * Processed by gl_update with fresh controls from the selected controller and
 * host.focused, including in menus/while paused. Both buttons must be released
 * before each chord, including after reconnect/focus loss. No game input is
 * suppressed. A native-menu host can disable this after loading settings.
 * Independent of the keyboard shortcut; owner thread, enabled must be 0 or 1. */
GL_API int32_t GL_CALL gl_set_gamepad_menu_shortcut(gl_context*,uint32_t enabled);
GL_API uint32_t GL_CALL gl_get_gamepad_menu_shortcut(const gl_context*);
#define GL_SETTINGS_FILENAME "gyrolib.ini"
/* Call on the owner thread after registering modes/applying host defaults.
 * Loads or creates gyrolib.ini in utf8_directory (must already exist). NULL uses
 * the module containing GyroLib: gyrolib.dll/.so, or the mod/exe for static builds.
 * If absent, optionally imports utf8_legacy_path without modifying the old file.
 * An existing destination always wins. Imports must use GL_SETTINGS_SCHEMA too;
 * discarded development formats are not migrated. Existing files are read-only
 * until an actual settings edit. Register views before loading their defaults.
 * On failure automatic saving is disabled;
 * report the error and retry initialization after fixing the path/file.
 * Never call from DllMain. No settings I/O is performed by gl_create itself. */
GL_API int32_t GL_CALL gl_initialize_settings(gl_context*,const char* utf8_directory,const char* utf8_legacy_path);
/* Borrowed UTF-8 path; valid until the next persistence operation. Empty if unset. */
GL_API const char* GL_CALL gl_get_settings_path(const gl_context*);
/* Configure automatic persistence after applying host defaults/loading an existing
 * file. Empty path disables persistence. This call itself never writes. A successful
 * load/save also sets the path. Setting/language changes and reset then auto-save. */
GL_API int32_t GL_CALL gl_set_settings_path(gl_context*,const char* utf8_path);
GL_API int32_t GL_CALL gl_load_settings(gl_context*,const char* utf8_path);
GL_API int32_t GL_CALL gl_save_settings(gl_context*,const char* utf8_path);
/* Panel visibility is shared with native menus and automatically gates camera. */
GL_API void GL_CALL gl_set_panel_open(gl_context*,uint32_t open);
GL_API uint32_t GL_CALL gl_panel_open(const gl_context*);
/* Owner-thread query for frontends. Returns a serial incremented on each
 * closed-to-open transition (zero before the first opening). Optional output:
 * gameplay context active at that opening, or zero if none. Remains unchanged
 * while open, so editing another tab is not interrupted by host view changes. */
GL_API uint64_t GL_CALL gl_get_panel_opening(const gl_context*,uint32_t* gameplay_context);
#ifdef __cplusplus
}
#endif
#endif
