#include "declared_view.hpp"
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/sdl.h>
#include <gyrolib/runtime.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

#define REQUIRE(x) do { if(!(x)){std::fprintf(stderr,"%d: %s (%s)\n",__LINE__,#x,SDL_GetError());return 1;} } while(0)
struct Fixture {
    SDL_JoystickID id{};SDL_Joystick* joystick{};
    unsigned enabled{},disabled{},outputs{};bool stalled{},resume_on_enable=true;
    static bool SDLCALL sensors(void* user,bool on){
        auto& f=*static_cast<Fixture*>(user);
        if(on){++f.enabled;if(f.resume_on_enable)f.stalled=false;}else ++f.disabled;
        return true;
    }
    static bool SDLCALL rumble(void* user,Uint16,Uint16){++static_cast<Fixture*>(user)->outputs;return true;}
    bool connect(){
        SDL_VirtualJoystickSensorDesc sensors[]={{SDL_SENSOR_ACCEL,250},{SDL_SENSOR_GYRO,250}};
        SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.vendor_id=0xffff;desc.product_id=0xfffd;
        desc.naxes=6;desc.axis_mask=63;desc.nbuttons=21;desc.button_mask=(1u<<21)-1;
        desc.nsensors=2;desc.sensors=sensors;desc.SetSensorsEnabled=Fixture::sensors;
        desc.Rumble=Fixture::rumble;desc.userdata=this;desc.name="Sensor recovery fixture";
        enabled=disabled=outputs=0;stalled=false;resume_on_enable=true;
        id=SDL_AttachVirtualJoystick(&desc);joystick=SDL_OpenJoystick(id);return id&&joystick;
    }
    void disconnect(){if(id)SDL_DetachVirtualJoystick(id);if(joystick)SDL_CloseJoystick(joystick);id=0;joystick=nullptr;}
    ~Fixture(){disconnect();}
    bool sample(uint64_t timestamp){
        if(stalled)return true;
        const float accel[]={0,SDL_STANDARD_GRAVITY,0},gyro[]={0,1,0};
        return SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_ACCEL,timestamp,accel,3)&&
            SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_GYRO,timestamp,gyro,3);
    }
};
int main(){
#ifdef GL_BUNDLED_RUNTIME
    if(gl_runtime_prepare()!=GL_OK){std::fprintf(stderr,"Test runtime: %s\n",gl_runtime_error());return 1;}
#endif
    // No physical hardware or Steam configuration is touched by this fixture.
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0xffff/0xfffd",SDL_HINT_OVERRIDE);
    REQUIRE(SDL_Init(SDL_INIT_GAMEPAD));
    gyrolib::Context c;REQUIRE(declare_test_view(c.get()));
    auto* reader=gl_sdl_create(c.get(),1);REQUIRE(reader);
    REQUIRE(gl_sdl_set_sensor_worker(reader,"")==GL_OK);
    REQUIRE(gl_setting_set(c.get(),"context.1.sensitivity_x",1)==GL_OK);
    REQUIRE(gl_setting_set(c.get(),"context.1.gyro.space",GL_SPACE_LOCAL_YAW)==GL_OK);
    Fixture pad;uint64_t now=1000000000;gl_output out{};
    gl_host_state host{};host.focused=host.camera_allowed=1;
    auto step=[&](uint64_t dt=10000000){
        now+=dt;if(!pad.sample(now))return false;
        SDL_PumpEvents();return gl_sdl_poll(reader,now)==GL_OK&&update_test_view(c.get(),now,&host,&out)==GL_OK;
    };
    auto poll=[&](uint64_t dt){now+=dt;SDL_PumpEvents();return gl_sdl_poll(reader,now)==GL_OK&&update_test_view(c.get(),now,&host,&out)==GL_OK;};
    REQUIRE(pad.connect());for(int i=0;i<5;++i)REQUIRE(step());
    REQUIRE(out.source==GL_SOURCE_SDL&&std::abs(out.yaw_degrees)>.1);
    REQUIRE(pad.enabled==1&&!pad.disabled);
    // Genuine removal, then a new endpoint: Steam's reconnect setup can stop
    // the IMU shortly AFTER SDL enabled it, while buttons keep reporting.
    pad.disconnect();REQUIRE(poll(10000000));REQUIRE(!gl_get_selected_device(c.get()));
    REQUIRE(pad.connect());for(int i=0;i<5;++i)REQUIRE(step());
    const auto endpoint=gl_sdl_endpoint_for_instance(reader,pad.id),selected=gl_get_selected_device(c.get());
    REQUIRE(endpoint&&selected&&out.source==GL_SOURCE_SDL);
    REQUIRE(gl_setting_set(c.get(),"context.1.gyro.activation",GL_HOLD)==GL_OK);
    REQUIRE(gl_setting_set(c.get(),"context.1.activation.button",3)==GL_OK);
    pad.stalled=true;REQUIRE(SDL_SetJoystickVirtualButton(pad.joystick,2,true));
    REQUIRE(step(200000000));REQUIRE(out.source==GL_SOURCE_NONE);
    REQUIRE(SDL_GetGamepadButton(SDL_GetGamepadFromID(pad.id),SDL_GAMEPAD_BUTTON_WEST));
    SDL_Event marker{};marker.type=SDL_EVENT_USER;REQUIRE(SDL_PushEvent(&marker));
    for(int i=0;i<5;++i){REQUIRE(step(100000000));REQUIRE(pad.enabled==1&&!pad.disabled);}
    // Recovery must occur on data silence, despite advertised capabilities.
    for(int i=0;i<3&&pad.stalled;++i)REQUIRE(step(100000000));
    REQUIRE(!pad.stalled&&pad.enabled==2&&pad.disabled==1);
    REQUIRE(gl_sdl_endpoint_for_instance(reader,pad.id)==endpoint&&gl_get_selected_device(c.get())==selected);
    REQUIRE(step());REQUIRE(step());REQUIRE(step());
    REQUIRE(out.source==GL_SOURCE_SDL&&std::abs(out.yaw_degrees)>.1&&std::abs(out.yaw_degrees)<1);
    REQUIRE(out.gyro_active&&SDL_GetGamepadButton(SDL_GetGamepadFromID(pad.id),SDL_GAMEPAD_BUTTON_WEST));
    REQUIRE(SDL_HasEvent(SDL_EVENT_USER)); // recovery never consumes host events
    const auto enabled=pad.enabled,disabled=pad.disabled;
    for(int i=0;i<260;++i)REQUIRE(step());
    REQUIRE(pad.enabled==enabled&&pad.disabled==disabled); // healthy stream untouched
    // A frozen hardware timestamp is not fresh data, even with queued reports.
    const auto frozen=now;const float a[]={0,SDL_STANDARD_GRAVITY,0},g[]={0,1,0};
    for(int i=0;i<90;++i){
        SDL_SendJoystickVirtualSensorData(pad.joystick,SDL_SENSOR_ACCEL,frozen,a,3);
        SDL_SendJoystickVirtualSensorData(pad.joystick,SDL_SENSOR_GYRO,frozen,g,3);
        REQUIRE(poll(10000000));
    }
    REQUIRE(pad.enabled==enabled+1&&pad.disabled==disabled+1);
    REQUIRE(step());REQUIRE(step());REQUIRE(out.source==GL_SOURCE_SDL);
    // Persistent failure retries slowly rather than reconfiguring every frame.
    pad.stalled=true;pad.resume_on_enable=false;
    const auto attempts=pad.enabled;
    for(int i=0;i<10;++i)REQUIRE(step(100000000));
    REQUIRE(pad.enabled<=attempts+1);
    for(int i=0;i<25;++i)REQUIRE(step(100000000));
    REQUIRE(pad.enabled>=attempts+1&&pad.enabled<=attempts+2&&!pad.outputs);
    pad.disconnect();REQUIRE(poll(10000000));
    // Sensor flags with NO initial samples also need recovery, not a healthy
    // stream first. No Steam handle or commercial controller name is required.
    REQUIRE(pad.connect());pad.stalled=true;pad.resume_on_enable=false;
    REQUIRE(poll(10000000));REQUIRE(pad.enabled==1);
    REQUIRE(poll(700000000));REQUIRE(pad.enabled==1&&!pad.disabled);
    pad.resume_on_enable=true;REQUIRE(poll(100000000));
    REQUIRE(pad.enabled==2&&pad.disabled==1&&!pad.stalled);
    for(int i=0;i<4;++i)REQUIRE(step());REQUIRE(out.source==GL_SOURCE_SDL);
    pad.disconnect();REQUIRE(poll(10000000));
    // Pre-existing host handle, both disabled and enabled: reader never toggles it.
    REQUIRE(pad.connect());auto* borrowed=SDL_OpenGamepad(pad.id);REQUIRE(borrowed);
    REQUIRE(poll(10000000));REQUIRE(!pad.enabled&&!pad.disabled);
    REQUIRE(poll(3000000000));REQUIRE(!pad.enabled&&!pad.disabled);
    REQUIRE(SDL_SetGamepadSensorEnabled(borrowed,SDL_SENSOR_GYRO,true));
    REQUIRE(SDL_SetGamepadSensorEnabled(borrowed,SDL_SENSOR_ACCEL,true));
    REQUIRE(pad.enabled==1);pad.stalled=true;
    REQUIRE(poll(3000000000));REQUIRE(pad.enabled==1&&!pad.disabled);
    gl_sdl_destroy(reader);REQUIRE(SDL_GamepadSensorEnabled(borrowed,SDL_SENSOR_GYRO));
    REQUIRE(pad.enabled==1&&!pad.disabled&&!pad.outputs);
    SDL_CloseGamepad(borrowed);pad.disconnect();SDL_Quit();
    std::puts("SDL reconnect/stalled/frozen sensor recovery, bounded retries, controls and host sensor ownership passed.");return 0;
}

