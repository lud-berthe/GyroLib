#include "gyrolib/sdl.h"
#include "gyrolib/runtime.h"
#include "sdl_identity.hpp"
#include "detail/sdl_controls.hpp"
#include "detail/sensor_process.hpp"
#include "detail/sensor_watchdog.hpp"
#include "detail/touchpad_haptic.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <memory>
#include <new>
#include <numbers>
#include <string>
#include <vector>
namespace {
thread_local char create_error[512]{};
gl_sdl* creation_failed(const char* reason){std::snprintf(create_error,sizeof(create_error),"%s",reason);return nullptr;}
struct Pad {
    SDL_Gamepad* handle{};SDL_JoystickID instance{};gl_endpoint info{};
    gyrolib_sdl_detail::ControlLayout layout{};
    uint64_t steam{},native_identity{},accel_sensor{},gyro_sensor{};gl_vec3 accel{};
    bool sensor_owned{};
    gyrolib_sdl_detail::SensorWatchdog motion;
    gyrolib_sdl_detail::TouchpadHaptic haptic;
};
struct SensorEvent {SDL_GamepadSensorEvent event;uint64_t observed;};
uint64_t identity(const std::string& value) {
    uint64_t hash=14695981039346656037ull;
    for(unsigned char ch:value){hash^=ch;hash*=1099511628211ull;}
    return hash|0x8000000000000000ull;
}

}
struct gl_sdl {
    gl_context* context{};bool owned{};uint64_t enumerated{};
    uint64_t feedback_update{},feedback_last{};
    SensorProcess* sensor_process{};
    std::atomic<bool> devices_changed{true};
    std::vector<Pad> pads;
    std::mutex mutex;
    std::deque<SensorEvent> sensors;
    bool overflow{};
    std::string error;
    static bool SDLCALL watch(void* user,SDL_Event* event) noexcept {
        auto* self=static_cast<gl_sdl*>(user);
        if(event->type==SDL_EVENT_GAMEPAD_ADDED||event->type==SDL_EVENT_GAMEPAD_REMOVED||event->type==SDL_EVENT_GAMEPAD_REMAPPED){
            // Watches may run off-thread. Open/close only during the owner poll.
            self->devices_changed.store(true,std::memory_order_relaxed);return true;
        }
        if(event->type!=SDL_EVENT_GAMEPAD_SENSOR_UPDATE)return true;
        try {
            std::lock_guard lock(self->mutex);
            if(self->sensors.size()>=4096){self->sensors.pop_front();self->overflow=true;}
            self->sensors.push_back({event->gsensor,SDL_GetTicksNS()});
        }catch(...){/* Do not propagate exceptions into SDL. */}
        return true;
    }
    void close(Pad& p) {
        p.haptic.reset();
        gl_disconnect_endpoint(context,p.info.id);
        if(p.handle) {
            // Do not disable sensors: another owner may have acquired the same
            // SDL handle since we opened it. SDL releases them on the last close.
            SDL_CloseGamepad(p.handle);p.handle=nullptr;
        }
        gl_forget_endpoint(context,p.info.id);
    }
};
extern "C" {
gl_sdl* GL_CALL gl_sdl_create(gl_context* c,uint32_t borrowed) try {
    create_error[0]=0;
    if(!c)return creation_failed("SDL reader requires a GyroLib context");
#ifdef GL_SINGLE_DLL
    if(gl_runtime_prepare()!=GL_OK)return creation_failed(gl_runtime_error());
#endif
    if(!SDL_IsMainThread())return creation_failed("SDL reader must be created on the SDL main thread");
    std::unique_ptr<gl_sdl,decltype(&gl_sdl_destroy)> self(new gl_sdl,gl_sdl_destroy);self->context=c;
    if(borrowed){if(!(SDL_WasInit(SDL_INIT_GAMEPAD)&SDL_INIT_GAMEPAD))return creation_failed("Borrowed SDL gamepad subsystem is not initialized");}
    else {if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD))return creation_failed(SDL_GetError());self->owned=true;}
    if(!SDL_AddEventWatch(gl_sdl::watch,self.get()))return creation_failed(SDL_GetError());
    self->sensor_process=sensor_process_create(c);
    if(!self->sensor_process)return creation_failed("Cannot allocate isolated sensor reader state");
    return self.release();
}catch(const std::bad_alloc&){return creation_failed("Cannot allocate SDL reader");}
catch(...){return creation_failed("Cannot create SDL reader");}
void GL_CALL gl_sdl_destroy(gl_sdl* self) {
    if(!self)return;SDL_RemoveEventWatch(gl_sdl::watch,self);
    for(auto& p:self->pads)self->close(p);
    sensor_process_destroy(self->sensor_process);
    if(self->owned)SDL_QuitSubSystem(SDL_INIT_GAMEPAD);delete self;
}
int32_t GL_CALL gl_sdl_pump_events(gl_sdl* self){
    if(!self||!SDL_IsMainThread())return GL_INVALID;
    SDL_PumpEvents();return GL_OK;
}
int32_t GL_CALL gl_sdl_poll(gl_sdl* self,uint64_t now) try {
    if(!self||!now||!SDL_IsMainThread())return GL_INVALID;
    SDL_UpdateGamepads();
    for(auto i=self->pads.begin();i!=self->pads.end();) {
        if(!SDL_GamepadConnected(i->handle)){self->close(*i);i=self->pads.erase(i);self->enumerated=0;}
        else ++i;
    }
    const bool devices_changed=self->devices_changed.exchange(false,std::memory_order_relaxed);
    if(devices_changed || !self->enumerated || now-self->enumerated>=500000000) {
        self->enumerated=now;int count=0;
        std::unique_ptr<SDL_JoystickID,decltype(&SDL_free)> owned_ids(SDL_GetGamepads(&count),SDL_free);
        SDL_JoystickID* ids=owned_ids.get();
        for(int i=0;i<count;++i) {
            auto instance=ids[i];
            if(std::any_of(self->pads.begin(),self->pads.end(),[&](const Pad& p){return p.instance==instance;}))continue;
            bool shared_handle=SDL_GetGamepadFromID(instance)!=nullptr;
            auto* handle=SDL_OpenGamepad(instance);if(!handle)continue;
            std::unique_ptr<SDL_Gamepad,decltype(&SDL_CloseGamepad)> handle_guard(handle,SDL_CloseGamepad);
            Pad p;p.handle=handle;p.instance=instance;p.info.id=0x1000000000000000ull|instance;
            p.sensor_owned=!shared_handle;p.motion.opened(now);
            p.info.source=GL_SOURCE_SDL;p.info.connected=1;p.steam=SDL_GetGamepadSteamHandle(handle);
            const char* serial=SDL_GetGamepadSerial(handle);const char* path=SDL_GetGamepadPath(handle);
            std::string key=std::to_string(SDL_GetGamepadVendor(handle))+":"+std::to_string(SDL_GetGamepadProduct(handle))+":";
            key+=serial&&*serial?std::string("serial:")+serial:(path&&*path?std::string("path:")+path:std::to_string(instance));
            p.native_identity=identity(key);
            p.info.physical_id=p.steam?p.steam:p.native_identity;
            const char* name=SDL_GetGamepadName(handle);std::strncpy(p.info.name,name?name:"SDL controller",127);
            p.info.caps.gyro=SDL_GamepadHasSensor(handle,SDL_SENSOR_GYRO);
            p.info.caps.accelerometer=SDL_GamepadHasSensor(handle,SDL_SENSOR_ACCEL);
            auto& cap=p.info.caps;
            if(!shared_handle) {
                if(cap.gyro)SDL_SetGamepadSensorEnabled(handle,SDL_SENSOR_GYRO,true);
                if(cap.accelerometer)SDL_SetGamepadSensorEnabled(handle,SDL_SENSOR_ACCEL,true);
            } else if(cap.gyro&&!SDL_GamepadSensorEnabled(handle,SDL_SENSOR_GYRO)) {
                self->error="Host owns this SDL handle: enable its available motion sensors in the host to permit observation";
            }
            if(const auto result=gl_register_endpoint(self->context,&p.info);result!=GL_OK){
                self->devices_changed.store(true,std::memory_order_relaxed);return result;
            }
            try{self->pads.push_back(std::move(p));}catch(...){gl_forget_endpoint(self->context,p.info.id);throw;}
            handle_guard.release();
        }
    }
    for(auto& p:self->pads) {
        const bool metadata=devices_changed||self->enumerated==now;
        if(metadata)p.layout=gyrolib_sdl_detail::control_layout(p.handle);
        const auto state=gyrolib_sdl_detail::read_controls(p.handle,p.layout);
        auto c=state.controls;c.timestamp_ns=now;
        // Keep host-proven SDL/Steam association when refreshing metadata.
        for(uint32_t n=0;n<gl_endpoint_count(self->context);++n){gl_endpoint known{};gl_get_endpoint(self->context,n,&known);
            if(known.id==p.info.id){p.info.physical_id=known.physical_id;break;}}
        // Steam may publish its exact association after SDL opened the pad.
        // Refresh every poll; preserve associations explicitly supplied by hosts.
        const uint64_t steam=SDL_GetGamepadSteamHandle(p.handle);
        const bool steam_changed=steam!=p.steam;
        if(steam_changed){
            gyrolib_sdl_detail::refresh_steam_identity(self->context,p.info,p.native_identity,p.steam,steam);
            p.steam=steam;
        }
        if(metadata||steam_changed){
            const auto& layout=p.layout;p.info.caps=layout.caps;
            const char* name=SDL_GetGamepadName(p.handle);
            // Display cleanup only, never a capability or physical pairing decision.
            if(name&&std::strcmp(name,"#SettingsController_SteamController")==0)name="Steam Controller";
            std::snprintf(p.info.name,sizeof(p.info.name),"%s",name?name:"SDL controller");
            if(const auto result=gl_register_endpoint(self->context,&p.info);result!=GL_OK)return result;
            for(uint32_t b=0;b<32;++b){
                gl_set_button_label(self->context,p.info.id,b,layout.labels[b].c_str(),GL_LABEL_DEVICE);
                gl_set_button_contact(self->context,p.info.id,b,
                    layout.stick[b]?GL_CONTACT_STICK:layout.grip[b]?GL_CONTACT_GRIP:GL_CONTACT_NONE,layout.stick[b]|layout.grip[b]);
            }
            gl_set_endpoint_control_authority(self->context,p.info.id,layout.authority|((layout.caps.gyro&&!steam)?GL_CONTROL_TRIGGERS:0));
            gl_set_endpoint_pairing_hint(self->context,p.info.id,SDL_GetGamepadVendor(p.handle),SDL_GetGamepadProduct(p.handle),p.steam!=0);
            for(int side=0;side<2;++side)gl_set_trigger_label(self->context,p.info.id,side?GL_RIGHT:GL_LEFT,layout.trigger_labels[side]);
        }
        if(const auto result=gl_submit_controls(self->context,p.info.id,&c);result!=GL_OK)return result;
        auto flick=state.flick;flick.timestamp_ns=now;
        if(const auto result=gl_submit_flick_input(self->context,p.info.id,&flick);result!=GL_OK)return result;
        auto triggers=state.triggers;triggers.timestamp_ns=now;
        if(const auto result=gl_submit_trigger_input(self->context,p.info.id,&triggers);result!=GL_OK)return result;
    }
    std::deque<SensorEvent> events;
    {std::lock_guard lock(self->mutex);events.swap(self->sensors);if(self->overflow){self->error="Sensor queue overflow; stale samples discarded";self->overflow=false;}}
    uint64_t ticks=SDL_GetTicksNS();
    for(const auto& wrapped:events) {
        const auto& ev=wrapped.event;
        auto p=std::find_if(self->pads.begin(),self->pads.end(),[&](const Pad& p){return p.instance==ev.which;});
        if(p==self->pads.end())continue;
        auto timestamp=ev.sensor_timestamp?ev.sensor_timestamp:ev.timestamp;
        if(ev.sensor==SDL_SENSOR_ACCEL){p->accel={ev.data[0]/SDL_STANDARD_GRAVITY,ev.data[1]/SDL_STANDARD_GRAVITY,ev.data[2]/SDL_STANDARD_GRAVITY};p->accel_sensor=timestamp;continue;}
        if(ev.sensor!=SDL_SENSOR_GYRO)continue;
        if(timestamp==p->gyro_sensor)continue; // repeated hardware timestamps are not recovery
        uint64_t age=ticks>=wrapped.observed?ticks-wrapped.observed:0;
        if(age>=100000000||age>=now)continue;
        constexpr float degrees=180/std::numbers::pi_v<float>;
        const bool accel_fresh=p->accel_sensor&&timestamp>=p->accel_sensor&&timestamp-p->accel_sensor<=50000000;
        gl_sample s{timestamp,now-age,{ev.data[0]*degrees,ev.data[1]*degrees,ev.data[2]*degrees},accel_fresh?p->accel:gl_vec3{}};
        if(gl_submit_sample(self->context,p->info.id,&s)==GL_OK){
            p->gyro_sensor=timestamp;
            p->motion.received(s.arrival_ns);
            if(self->error=="SDL sensor recovery failed")self->error.clear();
        }
    }
    // Steam can change physical reporting shortly after a hotplug, even when
    // this process was launched directly and SDL still reports sensors enabled.
    // Recover on actual data silence, without reopening/merging the controller.
    for(auto& p:self->pads)if(p.sensor_owned&&p.info.caps.gyro&&
        p.motion.attempt(now)&&SDL_GamepadConnected(p.handle)){
        p.accel_sensor=0; // no old acceleration paired with a restarted IMU
        if(!gyrolib_sdl_detail::rearm_sensors(p.handle))self->error="SDL sensor recovery failed";
    }
    const bool needs_motion=std::any_of(self->pads.begin(),self->pads.end(),[](const auto& p){return p.steam&&!p.info.caps.gyro;});
    sensor_process_poll(self->sensor_process,now,needs_motion);
    return GL_OK;
} catch (...) {return GL_LIMIT;}
int32_t GL_CALL gl_sdl_update(gl_sdl* self,uint64_t now,const gl_host_state* host,gl_output* output){
    if(output)*output={};
    if(!self||!now||!host||!output)return GL_INVALID;
    const auto pumped=gl_sdl_pump_events(self);if(pumped!=GL_OK)return pumped;
    const auto polled=gl_sdl_poll(self,now);if(polled!=GL_OK)return polled;
    return gl_update(self->context,now,host,output);
}
uint64_t GL_CALL gl_sdl_endpoint_for_instance(const gl_sdl* self,uint32_t instance) {
    if(self)for(const auto& p:self->pads)if(p.instance==instance)return p.info.id;return 0;
}
int32_t GL_CALL gl_sdl_apply_feedback(gl_sdl* self)try{
    if(!self||!SDL_IsMainThread())return GL_INVALID;
    gl_touchpad_feedback feedback{};gl_get_touchpad_feedback(self->context,&feedback);
    if(!feedback.pulse||feedback.timestamp_ns==self->feedback_update)return GL_OK;
    self->feedback_update=feedback.timestamp_ns;
    if(self->feedback_last&&feedback.timestamp_ns>=self->feedback_last&&feedback.timestamp_ns-self->feedback_last<50000000)return GL_OK;
    self->feedback_last=feedback.timestamp_ns;
    if(const auto sensor=gl_get_motion_sensor(self->context,feedback.physical_id))return sensor_process_feedback(self->sensor_process,sensor);
    for(auto& pad:self->pads)if(pad.info.physical_id==feedback.physical_id){
        const auto result=pad.haptic.pulse(pad.handle,feedback.timestamp_ns);
        if(result==GL_IO_ERROR)self->error="Right-touchpad feedback write failed";
        else if(result==GL_OK&&self->error=="Right-touchpad feedback write failed")self->error.clear();
        return result;
    }
    return GL_UNAVAILABLE;
}catch(...){return GL_LIMIT;}
uint64_t GL_CALL gl_sdl_steam_handle(const gl_sdl* self,uint64_t id) {
    if(self)for(const auto& p:self->pads)if(p.info.id==id)return p.steam;return 0;
}
int32_t GL_CALL gl_sdl_set_sensor_worker(gl_sdl* self,const char* path) try {
    if(!self||!SDL_IsMainThread())return GL_INVALID;return sensor_process_path(self->sensor_process,path);
}catch(...){return GL_LIMIT;}
const char* GL_CALL gl_sdl_error(const gl_sdl* self){
    if(!self)return create_error[0]?create_error:"No SDL reader";
    const char* sensor_error=sensor_process_error(self->sensor_process);
    return *sensor_error?sensor_error:self->error.c_str();
}
}
