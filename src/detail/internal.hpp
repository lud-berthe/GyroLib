#pragma once
#include "stillness.hpp"
#include "filter.hpp"
#include "sensor_clock.hpp"
#include "activation.hpp"
#include "gyrolib/gyrolib.h"
#include "flick.hpp"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4100)
#endif
#include <GamepadMotion.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <array>
#include <bitset>
#include <optional>
#include <deque>
#include <map>
#include <set>
#include <list>
#include <string>
#include <string_view>

namespace gyrolib {
enum Setting {
#define GL_SETTING(symbol,...) symbol,
#include "settings.def"
#undef GL_SETTING
    SettingCount
};
struct SettingDef { const char *id,*key,*description; uint32_t type; double def,min,max,step; const char* unit; };
extern const std::array<SettingDef,SettingCount> definitions;
using Settings=std::array<double,SettingCount>;
struct ProfileLink {uint32_t parent{};std::bitset<SettingCount> overrides;};
using Profiles=std::map<uint32_t,Settings>;
using ProfileLinks=std::map<uint32_t,ProfileLink>;
struct RecommendedSettings {Profiles profiles;ProfileLinks links;double calibration{};};
Settings resolve_profile(const Profiles&,const ProfileLinks&,uint32_t);
bool valid_links(const Profiles&,const ProfileLinks&);
Settings defaults();
bool profile_setting(int);
std::string profile_key(uint32_t,int);
int setting_index(const char*);
const char* translate(std::string_view,const char*);
bool side_available(uint32_t caps,int choice);
bool side_held(uint32_t state,int choice);
bool button_matches(uint32_t state,int choice);
inline bool gravity_space(int space){return space==GL_SPACE_PLAYER||space==GL_SPACE_WORLD||
    space==GL_SPACE_PLAYER_LEAN||space==GL_SPACE_WORLD_LEAN||space==GL_SPACE_LASER_POINTER;}

class Motion {
    GamepadMotion fusion_;
    Stillness stillness_;
    bool gravity_ready_{};
    uint64_t previous_ns_{};
    TieredFilter filter_;
    SensorClock clock_;
    double countdown_{},collect_time_{},still_time_{};
    gl_vec3 mean_{},auto_mean_{};
    unsigned count_{};
    int previous_space_{-1};
    double track_x_{},track_y_{};
public:
    gl_diagnostics diagnostics{};
    Motion();
    void resume();
    void begin();
    void cancel();
    void clear_smoothing();
    void clear_trackball(){track_x_=track_y_=0;}
    void trackball(double dt,double decay,uint32_t axes,gl_output&);
    double clock_scale()const{return clock_.scale();}
    bool clock_qualified()const{return clock_.qualified();}
    void process(const gl_sample&,const Settings&,bool steam,bool menu,bool active,bool allow_calibration,
                 gl_output&,uint32_t track_axes=0);
};
struct GameplayContext {
    std::string label,description;
    std::array<std::string,SettingCount> setting_ids,setting_labels,setting_descriptions;
    int32_t priority{};
    bool active{},available{},reported{};
    int activation_previous{-1};
    int output_target{-1}; // unannotated legacy modes use the host's dynamic target
    bool camera_in_menu{};
    bool zoom_available{},fov_reported{};
    double fov{},reference_fov{};
};
struct ButtonContact {uint32_t family{},side{};};
struct EndpointState {
    gl_endpoint info{};
    bool motion_companion{};
    uint64_t companion_identity{};
    uint32_t pairing_vendor{};
    bool virtual_controller{};
    uint32_t control_authority{};
    std::array<std::string,32> button_labels;
    std::array<uint32_t,32> label_provenance{};
    std::array<ButtonContact,32> button_contacts{};
    gl_controls controls{};
    gl_flick_input flick_input{};
    std::deque<gl_flick_input> flick_samples;
    bool flick_explicit{};
    gl_trigger_input triggers{};
    std::array<std::string,2> trigger_labels;
    std::deque<gl_sample> samples;
    uint64_t last_sensor{},last_arrival{},last_accel{},healthy_since{};
    unsigned consecutive{};
    std::array<double,64> intervals{};
    unsigned interval_count{},interval_next{};
    Motion motion;
};
}
struct gl_context {
    gyrolib::Settings settings=gyrolib::defaults();
    // GamepadMotion owns self-referencing state; endpoints must never move.
    std::list<gyrolib::EndpointState> endpoints;
    std::set<uint64_t> manual_motion_groups;
    std::array<gl_event_ex,128> events{};
    size_t event_begin{},event_count{};
    std::map<std::string,std::string> unknown_settings;
    std::map<uint32_t,gyrolib::GameplayContext> gameplay_contexts;
    // Saved independently of registration; temporarily unavailable mods keep values.
    std::map<uint32_t,gyrolib::Settings> context_settings;
    gyrolib::ProfileLinks profile_links;
    std::optional<gyrolib::RecommendedSettings> recommended;
    gyrolib::Profiles profile_snapshot() const;
    void profiles_changed(const gyrolib::Profiles&);
    int save_change();
    uint32_t previous_profile{};
    uint32_t filter_profile{}; // input events may arrive before the camera update
    uint32_t previous_output_target=GL_OUTPUT_CAMERA;
    uint32_t host_capabilities{};
    uint32_t output_target=GL_OUTPUT_CAMERA;
    std::string language="en";
    uint32_t menu_key=10;
    bool gamepad_menu_shortcut=true,menu_chord_armed=false;
    uint64_t menu_chord_device=0,menu_chord_endpoint=0,menu_chord_sample=0,menu_chord_update=0;
    std::string settings_path;
    unsigned settings_batch_depth{};
    int settings_save_result=GL_OK;
    mutable std::array<std::string,8> button_pair_labels;
    mutable std::array<std::string,2> trigger_pair_labels;
    std::array<uint32_t,9> resolved_input_caps{};
    uint64_t selected{},active{},now{},selected_at{},switched_at{},previous_frame{};
    bool panel{},held_previous{},toggle{true},controls_primed{},safe_previous{};
    uint64_t panel_opening{};
    uint32_t panel_opening_context{};
    int activation_previous{-1};
    gl_host_state host{};
    gl_output output{};
    gl_diagnostics totals{};
    // Optional owner-thread frontend publication after a complete core update.
    // Kept private so core-only builds have no renderer/link dependency.
    int32_t (*publish_overlay)(void*){};
    void* overlay_user{};
    void (*panel_changed)(void*,bool){};
    gyrolib::Flick flick;
    gyrolib::Flick flick_touchpad;
    bool suppress_touchpad{},touchpad_pulse{};
    gyrolib::AnalogActivation stick_gate,trigger_gate;
    gl_gyro_state gyro_state{};
    uint32_t modifiers{};
    int gyro_override{-1};
    struct FlickTimeline {uint64_t endpoint{},time{},report{};};
    std::array<FlickTimeline,2> flick_timelines{};
    std::array<gyrolib::LongPressBlocker,64> long_press_blockers;
    gl_camera_callback camera{};
    void* camera_user{};
    gl_sample_observer sample_observer{};
    void* sample_observer_user{};
    gl_recenter_callback recenter{};
    void* recenter_user{};
    bool recenter_requested{},recenter_held{},recenter_primed{},allow_calibration{true};
    gyrolib::EndpointState* endpoint(uint64_t);
    const gyrolib::EndpointState* endpoint(uint64_t) const;
    const gyrolib::EndpointState* button_companion() const;
    const gyrolib::EndpointState* control_authority(uint32_t family) const;
    gyrolib::ButtonContact button_contact(uint32_t) const;
    gl_capabilities capabilities() const;
    gl_flick_input flick_inputs() const;
    const gyrolib::EndpointState* flick_provider(uint32_t family) const;
    gl_flick_input flick_input(const gyrolib::EndpointState&) const;
    uint32_t flick_available(const gyrolib::EndpointState&) const;
    gl_trigger_input trigger_inputs() const;
    uint32_t winning_context() const;
    uint32_t effective_output_target(uint32_t context_id) const;
    uint32_t effective_output_target() const {return effective_output_target(winning_context());}
    gyrolib::Settings effective_settings(uint32_t context_id) const;
    gyrolib::Settings device_settings(uint32_t context_id) const;
    void emit(uint32_t type,int detail=0,uint64_t ep=0,double value=0,const char* setting=nullptr) noexcept;
    void reset_temporal();
};
