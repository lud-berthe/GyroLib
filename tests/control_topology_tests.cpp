#include "declared_view.hpp"
#include <gyrolib/gyrolib.hpp>
#include <gyrolib/sdl.h>
#include <gyrolib/runtime.h>
#include <SDL3/SDL.h>
#include "../third_party/SDL/gyrolib_controls.h"
#include <cstdio>
#include <cstring>

#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s (%s)\n",__LINE__,#x,SDL_GetError());return 1;}}while(0)
static bool SDLCALL sensors(void*,bool){return true;}
static bool choice(gl_context* c,const char* setting,uint32_t value){
    for(unsigned i=0;i<gl_menu_choice_count(c,setting);++i){gl_choice v{};gl_choice_at(c,setting,i,&v);if(v.value==value)return v.available!=0;}return false;
}
int main(){
#ifdef GL_BUNDLED_RUNTIME
    if(gl_runtime_prepare()!=GL_OK){std::fprintf(stderr,"Runtime: %s\n",gl_runtime_error());return 1;}
#endif
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0xffff/0xfffc",SDL_HINT_OVERRIDE);
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    // Same unknown name/VID/PID, different physical inputs. The descriptor alone
    // controls the menu, not an assumed Xbox shape or a controller-name table.
    struct Layout{int sticks,pads;};
    for(const auto layout:{Layout{GL_LEFT,2},Layout{0,2},Layout{3,1},Layout{3,2},Layout{GL_LEFT,0}}){
        gyrolib::Context c;CHECK(declare_test_view(c.get()));
        gl_set_host_capabilities(c.get(),GL_HOST_NATIVE_STICK_SUPPRESSION|GL_HOST_NATIVE_TOUCHPAD_SUPPRESSION);
        gl_setting_set(c.get(),"context.1.gyro.activation",GL_HOLD);
        auto* reader=gl_sdl_create(c.get(),1);CHECK(reader);gl_sdl_set_sensor_worker(reader,"");
        const SDL_VirtualJoystickSensorDesc ss[]={{SDL_SENSOR_GYRO,100},{SDL_SENSOR_ACCEL,100}};
        const SDL_VirtualJoystickTouchpadDesc tt[]={{1,{}},{1,{}}};
        SDL_VirtualJoystickDesc d{};SDL_INIT_INTERFACE(&d);d.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        d.name="Unknown same-name controller";d.vendor_id=0xffff;d.product_id=0xfffc;d.naxes=6;d.axis_mask=63;
        d.nbuttons=21;d.button_mask=(1u<<21)-1;d.nsensors=2;d.sensors=ss;d.SetSensorsEnabled=sensors;
        d.ntouchpads=Uint16(layout.pads);d.touchpads=tt;
        const auto id=SDL_AttachVirtualJoystick(&d);CHECK(id);auto* joy=SDL_OpenJoystick(id);CHECK(joy);
        GyroLibControlTopology(joy,layout.sticks);GyroLibButtonOrigin(joy,8,"Pad click",0,0);
        uint64_t now=1000000000;gl_host_state host{};host.focused=host.camera_allowed=1;gl_output out{};
        auto step=[&](){now+=10000000;const float a[]={0,SDL_STANDARD_GRAVITY,0},g[]={0,1,0};
            SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_ACCEL,now,a,3);SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_GYRO,now,g,3);
            SDL_PumpEvents();gl_sdl_poll(reader,now);update_test_view(c.get(),now,&host,&out);};
        for(int n=0;n<5;++n)step();
        CHECK(choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT)==bool(layout.sticks&GL_RIGHT));
        CHECK(choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_LEFT)==bool(layout.sticks&GL_LEFT));
        CHECK(choice(c.get(),"context.1.flick.mode",GL_FLICK_ON)==bool(layout.sticks&GL_RIGHT));
        CHECK(choice(c.get(),"context.1.flick.mode",GL_FLICK_TOUCHPAD)==(layout.pads==2));
        CHECK(choice(c.get(),"context.1.flick.mode",GL_FLICK_BOTH)==bool((layout.sticks&GL_RIGHT)&&layout.pads==2));
        CHECK(std::strcmp(gl_get_button_label(c.get(),8),"Pad click")==0);
        if(layout.pads==1){CHECK(!choice(c.get(),"context.1.activation.touchpad",GL_SIDE_LEFT));
            CHECK(!choice(c.get(),"context.1.activation.touchpad",GL_SIDE_RIGHT));
            CHECK(choice(c.get(),"context.1.activation.touchpad",GL_SIDE_EITHER));}
        if(layout.pads==2){
            gl_setting_set(c.get(),"context.1.activation.touchpad",GL_SIDE_RIGHT);
            SDL_SetJoystickVirtualTouchpad(joy,1,0,true,.8f,.2f,0);step();CHECK(out.gyro_active);
            SDL_SetJoystickVirtualTouchpad(joy,1,0,false,.8f,.2f,0);step();CHECK(!out.gyro_active);
        }
        // Right logical axes are populated even when there is no physical stick.
        gl_setting_set(c.get(),"context.1.activation.touchpad",0);
        gl_setting_set(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT);
        SDL_SetJoystickVirtualAxis(joy,2,32767);step();CHECK(bool(out.gyro_active)==bool(layout.sticks&GL_RIGHT));
        SDL_DetachVirtualJoystick(id);SDL_CloseJoystick(joy);SDL_PumpEvents();gl_sdl_destroy(reader);
    }
    // Logical stick axes backed by a hat or by the same raw axis are not two
    // physical analog dimensions. Remapping to genuine axes makes them available.
    {
        gyrolib::Context c;CHECK(declare_test_view(c.get()));
        gl_setting_set(c.get(),"context.1.gyro.activation",GL_HOLD);
        auto* reader=gl_sdl_create(c.get(),1);CHECK(reader);gl_sdl_set_sensor_worker(reader,"");
        SDL_VirtualJoystickDesc d{};SDL_INIT_INTERFACE(&d);d.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        d.name="Unknown axis origins";d.vendor_id=0xffff;d.product_id=0xfffc;
        d.naxes=6;d.axis_mask=63;d.nhats=1;d.nbuttons=15;d.button_mask=32767;
        const auto id=SDL_AttachVirtualJoystick(&d);CHECK(id);auto* joy=SDL_OpenJoystick(id);CHECK(joy);
        CHECK(SDL_SetGamepadMapping(id,"a:b0,b:b1,x:b2,y:b3,leftx:a0,lefty:a0,rightx:h0.2,righty:h0.4,"));
        uint64_t now=1000000000;gl_host_state h{};h.focused=h.camera_allowed=1;gl_output out{};
        auto step=[&](){now+=500000001;SDL_PumpEvents();CHECK(gl_sdl_poll(reader,now)==GL_OK);
            CHECK(update_test_view(c.get(),now,&h,&out)==GL_OK);return 0;};
        CHECK(step()==0);
        CHECK(!choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_LEFT));
        CHECK(!choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT));
        CHECK(SDL_SetGamepadMapping(id,"a:b0,b:b1,x:b2,y:b3,leftx:a0,lefty:a1,rightx:a2,righty:a3,"));
        CHECK(step()==0);
        CHECK(choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_LEFT));
        CHECK(choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT));
        GyroLibControlTopology(joy,GL_LEFT);
        SDL_SetNumberProperty(SDL_GetJoystickProperties(joy),"gyrolib.controls.left.y_axis",99);
        CHECK(step()==0);
        CHECK(!choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_LEFT));
        CHECK(!choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT));
        SDL_DetachVirtualJoystick(id);SDL_CloseJoystick(joy);SDL_PumpEvents();gl_sdl_destroy(reader);
    }
    SDL_Quit();
    // Physical companion overrides only declared families, including known zero.
    // A virtual right axis must not become a stick activator on a pad-only device.
    gyrolib::Context c;CHECK(declare_test_view(c.get()));
    gl_endpoint game{};game.id=1;game.physical_id=10;game.source=GL_SOURCE_SDL;game.connected=1;
    game.caps.sticks=3;game.caps.stick_touch=3;game.caps.buttons=3;
    CHECK(gl_register_endpoint(c.get(),&game)==GL_OK);
    gl_endpoint physical{};physical.id=2;physical.physical_id=20;physical.source=GL_SOURCE_SDL;physical.connected=1;
    physical.caps.sticks=GL_LEFT;physical.caps.touchpads=3;physical.caps.buttons=1;
    physical.caps.gyro=physical.caps.accelerometer=1;
    CHECK(gl_register_endpoint(c.get(),&physical)==GL_OK);CHECK(gl_set_motion_companion(c.get(),2)==GL_OK);
    CHECK(gl_set_endpoint_control_authority(c.get(),2,GL_CONTROL_ALL)==GL_OK);
    CHECK(gl_bind_motion_sensor(c.get(),10,2)==GL_OK);CHECK(gl_select_device(c.get(),10)==GL_OK);
    gl_setting_set(c.get(),"context.1.gyro.activation",GL_HOLD);gl_setting_set(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT);
    gl_setting_set(c.get(),"context.1.gyro.space",GL_SPACE_LOCAL_YAW);
    uint64_t now=1000000000;gl_host_state h{};h.focused=h.camera_allowed=1;gl_output out{};
    auto update=[&](bool fresh_physical){
        now+=10000000;gl_controls v{};v.timestamp_ns=now;v.right_x=1;v.stick_touch=3;v.buttons=2;gl_submit_controls(c.get(),1,&v);
        if(fresh_physical){gl_controls p{};p.timestamp_ns=now;p.left_x=1;p.touchpads=GL_RIGHT;gl_submit_controls(c.get(),2,&p);
            gl_sample sample{now,now,{0,20,0},{0,1,0}};gl_submit_sample(c.get(),2,&sample);}
        return update_test_view(c.get(),now,&h,&out)==GL_OK;
    };
    for(int i=0;i<5;++i)CHECK(update(true));CHECK(out.source==GL_SOURCE_SDL&&!out.gyro_active);
    CHECK(!choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT));
    CHECK(!choice(c.get(),"context.1.activation.stick_touch",GL_SIDE_EITHER));
    CHECK(!choice(c.get(),"context.1.activation.button",2));
    gl_setting_set(c.get(),"context.1.activation.stick_deflection",GL_SIDE_LEFT);CHECK(update(true));CHECK(out.gyro_active);
    gl_setting_set(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT);
    for(int i=0;i<20;++i)CHECK(update(false));CHECK(!out.gyro_active);
    CHECK(!choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT));
    // Withdrawal restores legacy supplemental behavior; unbound descriptors
    // cannot affect other players. Invalid flags do not modify the descriptor.
    CHECK(gl_set_endpoint_control_authority(c.get(),2,64)==GL_INVALID);
    CHECK(gl_set_endpoint_control_authority(c.get(),2,0)==GL_OK);
    for(int i=0;i<3;++i)CHECK(update(true));CHECK(out.gyro_active);
    CHECK(gl_set_endpoint_control_authority(c.get(),2,GL_CONTROL_STICKS)==GL_OK);CHECK(update(true));CHECK(!out.gyro_active);
    CHECK(choice(c.get(),"context.1.activation.stick_touch",GL_SIDE_EITHER));
    CHECK(gl_bind_motion_sensor(c.get(),10,0)==GL_OK);CHECK(update(true));
    CHECK(choice(c.get(),"context.1.activation.stick_deflection",GL_SIDE_RIGHT));
    std::puts("Generic physical topology, pad versus stick, known absence, stale input and virtual/physical authority passed.");return 0;
}

