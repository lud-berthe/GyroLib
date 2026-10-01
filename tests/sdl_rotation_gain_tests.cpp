#include <gyrolib/gyrolib.hpp>
#include <gyrolib/sdl.h>
#include <gyrolib/runtime.h>
#include <SDL3/SDL.h>
#include "../examples/tps/host.hpp"
#include <cmath>
#include <cstdio>
#include <numbers>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s (%s)\n",__LINE__,#x,SDL_GetError());return 1;}}while(0)
static bool SDLCALL enable(void*,bool){return true;}
static int revolution(int space,uint64_t sensor_step,uint64_t render_step,bool gyro_first){
    gyrolib::Context owner;auto* c=owner.get();tps::Host camera;CHECK(camera.setup(c));
    CHECK(gl_setting_set(c,"context.101.sensitivity_x",6)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.sensitivity_y",0)==GL_OK);
    CHECK(gl_setting_set(c,"context.101.gyro.space",space)==GL_OK);
    if(space==GL_SPACE_LOCAL_ADVANCED){
        CHECK(gl_setting_set(c,"context.101.gyro.local_angle_degrees",45)==GL_OK);
        CHECK(gl_setting_set(c,"context.101.gyro.local_roll_percent",0)==GL_OK);
    }
    auto* reader=gl_sdl_create(c,1);CHECK(reader);CHECK(gl_sdl_set_sensor_worker(reader,"")==GL_OK);
    const SDL_VirtualJoystickSensorDesc sensors[]={{SDL_SENSOR_GYRO,1000},{SDL_SENSOR_ACCEL,1000}};
    SDL_VirtualJoystickDesc d{};SDL_INIT_INTERFACE(&d);d.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    d.name="Known angular input";d.vendor_id=0xffff;d.product_id=0xfffd;
    d.naxes=6;d.axis_mask=63;d.nbuttons=15;d.button_mask=32767;d.nsensors=2;d.sensors=sensors;d.SetSensorsEnabled=enable;
    const auto id=SDL_AttachVirtualJoystick(&d);CHECK(id);auto* joy=SDL_OpenJoystick(id);CHECK(joy);
    uint64_t now=1000000000,elapsed=0;const tps::Input input{};
    CHECK(gl_sdl_poll(reader,now)==GL_OK);
    // Flat for Yaw, front-up 90deg for Roll, front-up 45deg for the two-axis
    // modes. Negative rotation about world up is a physical right turn.
    const float diagonal=1/std::sqrt(2.f);
    const float up_y=space==GL_SPACE_LOCAL_YAW?1:space==GL_SPACE_LOCAL_ROLL?0:diagonal;
    const float up_z=space==GL_SPACE_LOCAL_YAW?0:space==GL_SPACE_LOCAL_ROLL?-1:-diagonal;
    auto send=[&](float velocity,uint64_t dt){now+=dt;
        const float g[]={0,-velocity*up_y*std::numbers::pi_v<float>/180,-velocity*up_z*std::numbers::pi_v<float>/180};
        const float a[]={0,up_y*SDL_STANDARD_GRAVITY,up_z*SDL_STANDARD_GRAVITY};
        if(gyro_first){CHECK(SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_GYRO,now,g,3));CHECK(SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_ACCEL,now,a,3));}
        else {CHECK(SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_ACCEL,now,a,3));CHECK(SDL_SendJoystickVirtualSensorData(joy,SDL_SENSOR_GYRO,now,g,3));}
        return 0;};
    auto update=[&](){SDL_PumpEvents();CHECK(gl_sdl_poll(reader,now)==GL_OK);
        CHECK(camera.step(c,now,render_step*1e-9,input));return 0;};
    for(int i=0;i<5;++i){CHECK(send(0,sensor_step)==0);CHECK(update()==0);}
    double previous_yaw=camera.yaw,total=0;uint64_t next_render=now+render_step;
    while(elapsed<4000000000ull){const auto dt=std::min(sensor_step,4000000000ull-elapsed);elapsed+=dt;
        CHECK(send(90,dt)==0);
        if(now>=next_render||elapsed==4000000000ull){CHECK(update()==0);
            total+=std::remainder(camera.yaw-previous_yaw,360.0);previous_yaw=camera.yaw;next_render=now+render_step;}
    }
    const double expected=2160*(space==GL_SPACE_LOCAL_YAW_ROLL?std::sqrt(2.):1);
    if(std::abs(total-expected)>=.003){std::fprintf(stderr,"SDL space=%d: expected %.6f, got %.6f\n",space,expected,total);return 1;}
    CHECK(std::abs(camera.gyro_yaw_total-total)<.003);
    CHECK(std::abs(camera.camera_yaw_total-total)<.003);
    gl_diagnostics diagnostics{};CHECK(gl_get_diagnostics(c,&diagnostics)==GL_OK);CHECK(!diagnostics.dropped_samples);
    SDL_DetachVirtualJoystick(id);SDL_CloseJoystick(joy);SDL_PumpEvents();gl_sdl_destroy(reader);return 0;
}
int main(){
#ifdef GL_BUNDLED_RUNTIME
    CHECK(gl_runtime_prepare()==GL_OK);
#endif
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0xffff/0xfffd",SDL_HINT_OVERRIDE);
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    for(int space:{GL_SPACE_LOCAL_YAW,GL_SPACE_LOCAL_ROLL,GL_SPACE_LOCAL_YAW_ROLL,GL_SPACE_LOCAL_ADVANCED})
        for(uint64_t sensor:{1000000ull,4000000ull,10000000ull,20000000ull})
            for(uint64_t render:{6944444ull,16666667ull,33333333ull,80000000ull})
                for(bool gyro_first:{false,true})CHECK(revolution(space,sensor,render,gyro_first)==0);
    SDL_Quit();std::puts("128 SDL full-turn cases preserve local-axis signs and gains with batching and both sensor event orders.");return 0;
}

