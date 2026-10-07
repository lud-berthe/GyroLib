#pragma once
#include "sdl_contact_fixture.hpp"
#include <cmath>
// Included only by the separate, uninstalled fixture build of the actual worker.
static const char* sensor_fixture_case(){auto* v=SDL_getenv("GYROLIB_SENSOR_FIXTURE");return v?v:"";}
static bool sensor_fixture_send(gyrolib_sensor::Record r){
    using namespace gyrolib_sensor;const char* mode=sensor_fixture_case();
    if(std::strcmp(mode,"calibration")==0&&r.kind==Device)r.calibration_identity=0x1122334455667788ull;
    if(std::strcmp(mode,"bad")==0)r.revision=999;
    if(std::strcmp(mode,"bad-controls")==0&&r.kind==Controls)r.flick.stick_x=2.f;
    if(std::strcmp(mode,"stale")==0&&(r.kind==Motion||r.kind==Controls))r.observed_ns=1;
    if(std::strncmp(mode,"fragment",8)==0){
        if(std::fwrite(&r,7,1,stdout)!=1||std::fflush(stdout)!=0)return false;
        SDL_Delay(1);
        return std::fwrite(reinterpret_cast<unsigned char*>(&r)+7,sizeof(r)-7,1,stdout)==1&&std::fflush(stdout)==0;
    }
    return std::fwrite(&r,sizeof(r),1,stdout)==1&&std::fflush(stdout)==0;
}
class SensorFixture {
    SDL_JoystickID id{};SDL_Joystick* joystick{};Uint64 start{},last{};bool replaced{},stalled{};
    static bool SDLCALL enable(void* user,bool enabled){auto* self=static_cast<SensorFixture*>(user);if(enabled)self->stalled=false;return true;}
    static bool SDLCALL effect(void*,const void* data,int size){
        const unsigned char expected[]={0x81,0,0x90,1,0,0,1,0};
        return size==sizeof(expected)&&std::memcmp(data,expected,sizeof(expected))==0;
    }
    void connect(){
        const bool contacts=std::strncmp(sensor_fixture_case(),"contacts",8)==0||std::strcmp(sensor_fixture_case(),"flick-stream")==0;
        SDL_VirtualJoystickSensorDesc sensors[]={{SDL_SENSOR_ACCEL,250},{SDL_SENSOR_GYRO,250}};
        SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes=6;desc.nbuttons=21;desc.axis_mask=63;desc.button_mask=(1u<<21)-1;
        desc.vendor_id=0xffff;desc.product_id=0xfffe;desc.nsensors=2;desc.sensors=sensors;desc.SetSensorsEnabled=enable;desc.userdata=this;
        desc.name="Isolated SDL fixture";id=SDL_AttachVirtualJoystick(&desc);joystick=SDL_OpenJoystick(id);
        if(contacts){
            disconnect();desc=contact_fixture_desc();desc.SetSensorsEnabled=enable;desc.SendEffect=effect;desc.userdata=this;
            id=SDL_AttachVirtualJoystick(&desc);joystick=SDL_OpenJoystick(id);contact_fixture_mapping(id);
        }
    }
    void disconnect(){SDL_DetachVirtualJoystick(id);SDL_CloseJoystick(joystick);joystick=nullptr;id=0;}
public:
    SensorFixture(){
        // Steam identity/session variables must not leak into the worker.
        if(SDL_getenv("SteamAppId")||SDL_getenv("SteamVirtualGamepadInfo"))return;
        if(std::strcmp(sensor_fixture_case(),"fragment-delayed")==0)SDL_Delay(2300);
        start=SDL_GetTicksNS();connect();
    }
    ~SensorFixture(){if(joystick)disconnect();}
    void shutdown(){if(joystick)disconnect();}
    bool valid()const{return joystick!=nullptr;}
    void tick(){
        const auto now=SDL_GetTicksNS();
        const bool flick=std::strcmp(sensor_fixture_case(),"flick-stream")==0;
        if(flick){
            const double angle=(now-start)*1e-9*2.0;
            SDL_SetJoystickVirtualAxis(joystick,2,static_cast<Sint16>(std::sin(angle)*32767));
            SDL_SetJoystickVirtualAxis(joystick,3,static_cast<Sint16>(-std::cos(angle)*32767));
            SDL_SetJoystickVirtualTouchpad(joystick,1,0,true,float(.5+.3*std::sin(angle)),float(.5-.3*std::cos(angle)),0);
        }
        if(std::strncmp(sensor_fixture_case(),"contacts",8)==0){
            const bool held=(now-start)/150000000%2==0;
            SDL_SetJoystickVirtualTouchpad(joystick,0,0,held,.5f,.5f,0);
            SDL_SetJoystickVirtualTouchpad(joystick,1,0,held,1,0,0);
            for(int b:{18,19,20,21})SDL_SetJoystickVirtualButton(joystick,b,held);
        }
        if(std::strcmp(sensor_fixture_case(),"reconnect")==0&&!replaced&&now-start>250000000){disconnect();connect();replaced=true;}
        if(std::strcmp(sensor_fixture_case(),"stall")==0&&!replaced&&now-start>250000000){stalled=true;replaced=true;}
        if(stalled)return;
        if(flick||now-last>=4000000){last=now;const float accel[]={0,SDL_STANDARD_GRAVITY,0},gyro[]={0,1,0};
            SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_ACCEL,now,accel,3);
            SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_GYRO,now,gyro,3);}
    }
};
