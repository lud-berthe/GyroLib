// Isolated SDL acquisition: all controller protocols stay in SDL, not GyroLib.
#include "detail/sensor_wire.hpp"
#include "sdl_identity.hpp"
#include "detail/sdl_controls.hpp"
#include "detail/sensor_watchdog.hpp"
#include "detail/touchpad_haptic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <numbers>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif
#ifdef GL_SENSOR_FIXTURE
#include "../tests/sensor_fixture.hpp"
#endif
using namespace gyrolib_sensor;
namespace {
#ifdef _WIN32
HANDLE named_pipe=INVALID_HANDLE_VALUE;
#endif
struct Pad {
    SDL_Gamepad* handle{};uint64_t id{},identity{},accel_stamp{};gl_vec3 accel{};uint64_t gyro_stamp{};
    gyrolib_sdl_detail::SensorWatchdog motion;
    gyrolib_sdl_detail::ControlLayout layout{};
    gyrolib_sdl_detail::ControlSnapshot state{};
    uint64_t controls_sent{},metadata_sent{};
    gyrolib_sdl_detail::TouchpadHaptic haptic;
};
bool send(Record r){
    r.observed_ns=r.observed_ns?r.observed_ns:clock_ns();
#ifdef GL_SENSOR_FIXTURE
    return sensor_fixture_send(r);
#else
#ifdef _WIN32
    if(named_pipe!=INVALID_HANDLE_VALUE){DWORD written=0;return WriteFile(named_pipe,&r,sizeof(r),&written,nullptr)&&written==sizeof(r);}
#endif
    return std::fwrite(&r,sizeof(r),1,stdout)==1&&std::fflush(stdout)==0;
#endif
}
// Main-thread polling, no detached worker threads. EOF closes the helper.
bool alive(std::map<SDL_JoystickID,Pad>& pads){
    static std::array<unsigned char,sizeof(Command)> pending{};static size_t used=0;
    for(unsigned n=0;n<8;++n){
#ifdef _WIN32
    DWORD count=0;auto h=GetStdHandle(STD_INPUT_HANDLE);
    if(!PeekNamedPipe(h,nullptr,0,nullptr,&count,nullptr))return false;
    if(!count)return true;DWORD read=0;
    if(!ReadFile(h,pending.data()+used,std::min<DWORD>(count,static_cast<DWORD>(pending.size()-used)),&read,nullptr))return false;
#else
    pollfd fd{STDIN_FILENO,POLLIN|POLLHUP,0};if(poll(&fd,1,0)<=0)return true;
    auto read=::read(STDIN_FILENO,pending.data()+used,pending.size()-used);if(read<=0)return false;
#endif
    used+=read;if(used!=pending.size())continue;
    Command command{};std::memcpy(&command,pending.data(),sizeof(command));used=0;
    if(command.signature!=magic||command.kind==Quit)return false;
    if(command.kind!=RightPadPulse)return false;
    const auto current=clock_ns();if(!fresh(command.observed_ns,current))continue;
    for(auto& [id,p]:pads)if(p.id==command.endpoint){
        int result=GL_OK; // release/stale reports silently cancel a queued pulse
        auto state=gyrolib_sdl_detail::read_controls(p.handle,p.layout);
        const auto ticks=SDL_GetTicksNS();
        if(p.gyro_stamp&&ticks>=p.motion.last_motion&&ticks-p.motion.last_motion<100000000&&state.flick.touching&&
           std::hypot(state.flick.touchpad_x,state.flick.touchpad_y)>=.2)
            result=p.haptic.pulse(p.handle,current);
        if(!send(Record{.kind=Feedback,.hardware=static_cast<uint32_t>(result),.endpoint=p.id}))return false;
        break;
    }
    }
    return true;
}
}
int main(int argc,char** argv){
    // Never run as an independent interactive program or initialize Steam.
#ifdef _WIN32
    if(argc==3&&std::strcmp(argv[1],"--named-pipe-v1")==0){
        if(std::strncmp(argv[2],"\\\\.\\pipe\\GyroLib-sensor-",24)!=0)return 2;
        named_pipe=CreateFileA(argv[2],GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(named_pipe==INVALID_HANDLE_VALUE)return 5;
        SetStdHandle(STD_INPUT_HANDLE,named_pipe);
#ifdef GL_SENSOR_FIXTURE
        // Fixture writes through stdio to exercise precisely the same receiver.
        HANDLE duplicate{};DuplicateHandle(GetCurrentProcess(),named_pipe,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS);
        const int fd=_open_osfhandle(reinterpret_cast<intptr_t>(duplicate),_O_BINARY);_dup2(fd,_fileno(stdout));_close(fd);
#endif
    }else
#endif
    if(argc!=2||std::strcmp(argv[1],"--pipe-v1")!=0)return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES,"",SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"",SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1",SDL_HINT_OVERRIDE);
#ifdef GL_SENSOR_FIXTURE
    // Protocol tests use only virtual devices. Do not enumerate/open the user's
    // real controllers or wait on their Windows driver services in a fixture.
    for(const char* hint:{SDL_HINT_JOYSTICK_HIDAPI,SDL_HINT_JOYSTICK_RAWINPUT,SDL_HINT_JOYSTICK_DIRECTINPUT,
        SDL_HINT_JOYSTICK_WGI,SDL_HINT_JOYSTICK_GAMEINPUT,SDL_HINT_XINPUT_ENABLED})
        SDL_SetHintWithPriority(hint,"0",SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
        (std::strncmp(sensor_fixture_case(),"contacts",8)==0||std::strcmp(sensor_fixture_case(),"flick-stream")==0)?"0x28de/0x1304":"0xffff/0xfffe",SDL_HINT_OVERRIDE);
#endif
    if(!SDL_Init(SDL_INIT_GAMEPAD))return 3;
#ifdef GL_SENSOR_FIXTURE
    SensorFixture fixture;
    if(!fixture.valid())return 9;
#endif
    if(!send(Record{.kind=Hello}))return 4;
    std::map<SDL_JoystickID,Pad> pads;uint64_t scan=0,heartbeat=0,next=1;
    bool running=true;
    while(running&&alive(pads)){
#ifdef GL_SENSOR_FIXTURE
        fixture.tick();
#endif
        auto now=SDL_GetTicksNS();
        if(!scan||now-scan>=500000000){
            scan=now;
            for(auto i=pads.begin();i!=pads.end();){
                if(!SDL_GamepadConnected(i->second.handle)){
                    running&=send(Record{.kind=Removed,.endpoint=i->second.id});
                    i->second.haptic.reset();SDL_CloseGamepad(i->second.handle);i=pads.erase(i);
                }else ++i;
            }
            int count=0;auto* ids=SDL_GetGamepads(&count);
            for(int n=0;n<count&&pads.size()<32;++n)if(!pads.count(ids[n])){
#ifdef GL_SENSOR_FIXTURE
                if(!SDL_IsJoystickVirtual(ids[n]))continue;
#endif
                auto* g=SDL_OpenGamepad(ids[n]);if(!g)continue;
                // Virtual and sensorless pads never produce a motion endpoint.
                if(SDL_GetGamepadSteamHandle(g)||!SDL_GamepadHasSensor(g,SDL_SENSOR_GYRO)){
                    SDL_CloseGamepad(g);continue;
                }
                if(!SDL_SetGamepadSensorEnabled(g,SDL_SENSOR_GYRO,true)||(SDL_GamepadHasSensor(g,SDL_SENSOR_ACCEL)&&!SDL_SetGamepadSensorEnabled(g,SDL_SENSOR_ACCEL,true))){
                    SDL_CloseGamepad(g);continue;
                }
                const char* serial=SDL_GetGamepadSerial(g);const char* path=SDL_GetGamepadPath(g);
                std::string key=std::to_string(SDL_GetGamepadVendor(g))+":"+std::to_string(SDL_GetGamepadProduct(g))+":";
                key+=serial&&*serial?std::string("serial:")+serial:(path&&*path?std::string("path:")+path:std::to_string(ids[n]));
                Pad p{g,next++,identity(key.c_str())};p.motion.opened(now);
                Record r{.kind=Device,.hardware=uint32_t(SDL_GetGamepadVendor(g))|(uint32_t(SDL_GetGamepadProduct(g))<<16),.endpoint=p.id,.identity=p.identity};
                r.calibration_identity=gyrolib_sdl_detail::calibration_identity(SDL_GetGamepadVendor(g),SDL_GetGamepadProduct(g),serial);
                r.caps.gyro=1;r.caps.accelerometer=SDL_GamepadHasSensor(g,SDL_SENSOR_ACCEL);
                std::snprintf(r.name,sizeof(r.name),"%s",SDL_GetGamepadName(g));running&=send(r);
                pads.emplace(ids[n],std::move(p));
            }
            SDL_free(ids);
        }
        SDL_Event e{};
        while(running&&SDL_PollEvent(&e)){
            if(e.type==SDL_EVENT_GAMEPAD_ADDED||e.type==SDL_EVENT_GAMEPAD_REMOVED)scan=0;
            if(e.type==SDL_EVENT_GAMEPAD_REMAPPED){auto i=pads.find(e.gdevice.which);if(i!=pads.end())i->second.metadata_sent=0;}
            if(e.type!=SDL_EVENT_GAMEPAD_SENSOR_UPDATE)continue;
            auto i=pads.find(e.gsensor.which);if(i==pads.end())continue;auto& p=i->second;
            const auto stamp=e.gsensor.sensor_timestamp?e.gsensor.sensor_timestamp:e.gsensor.timestamp;
            if(e.gsensor.sensor==SDL_SENSOR_ACCEL){p.accel={e.gsensor.data[0]/SDL_STANDARD_GRAVITY,e.gsensor.data[1]/SDL_STANDARD_GRAVITY,e.gsensor.data[2]/SDL_STANDARD_GRAVITY};p.accel_stamp=stamp;continue;}
            if(e.gsensor.sensor!=SDL_SENSOR_GYRO)continue;
            if(stamp==p.gyro_stamp)continue;
            const auto ticks=SDL_GetTicksNS(),wall=clock_ns();const auto age=ticks>=e.gsensor.timestamp?ticks-e.gsensor.timestamp:0;
            if(age>=100000000||age>=wall)continue;
            p.gyro_stamp=stamp;p.motion.received(ticks-age);
            constexpr float deg=180/std::numbers::pi_v<float>;
            Record r{.kind=Motion,.endpoint=p.id,.observed_ns=wall-age,.sensor_ns=stamp};
            r.gyro={e.gsensor.data[0]*deg,e.gsensor.data[1]*deg,e.gsensor.data[2]*deg};
            if(p.accel_stamp&&stamp>=p.accel_stamp&&stamp-p.accel_stamp<=50000000)r.accel=p.accel;
            running=send(r);
        }
        // PollEvent can receive a report newer than the loop's initial clock.
        // Compare freshness AFTER pumping, or every changing axis is skipped.
        now=SDL_GetTicksNS();
        for(auto& [id,p]:pads){
            const bool metadata=!p.metadata_sent||now-p.metadata_sent>=500000000;
            if(metadata)p.layout=gyrolib_sdl_detail::control_layout(p.handle);
            auto state=gyrolib_sdl_detail::read_controls(p.handle,p.layout);
            // Physical controls are used only by GyroLib's activators/flick.
            // They never replace or inject the host's virtual game commands.
            if(metadata){
                const auto& layout=p.layout;
                Record r{.kind=Capabilities,.hardware=layout.authority|GL_CONTROL_TRIGGERS,.endpoint=p.id};r.caps=layout.caps;
                for(int side=0;side<2;++side)std::snprintf(r.trigger_labels[side],32,"%s",layout.trigger_labels[side]?layout.trigger_labels[side]:"");
                running&=send(r);
                for(uint32_t b=0;b<32;++b){
                    Record label_record{.kind=ButtonLabel,.hardware=b,.endpoint=p.id};
                    const auto family=layout.stick[b]?GL_CONTACT_STICK:layout.grip[b]?GL_CONTACT_GRIP:GL_CONTACT_NONE;
                    label_record.sensor_ns=family|(uint64_t(layout.stick[b]|layout.grip[b])<<32);
                    std::snprintf(label_record.name,sizeof(label_record.name),"%s",layout.labels[b].c_str());running&=send(label_record);
                }
                p.metadata_sent=now;
            }
            // Cached pressed controls must expire if physical reports stop.
            // Send changes immediately and refresh held/released state at 20Hz.
            if(p.gyro_stamp&&now>=p.motion.last_motion&&now-p.motion.last_motion<100000000&&
               (!p.controls_sent||now-p.controls_sent>=50000000||std::memcmp(&state.controls,&p.state.controls,sizeof(gl_controls))!=0||
                std::memcmp(&state.flick,&p.state.flick,sizeof(gl_flick_input))!=0||std::memcmp(&state.triggers,&p.state.triggers,sizeof(gl_trigger_input))!=0)){
                Record r{.kind=Controls,.endpoint=p.id};r.controls=state.controls;r.flick=state.flick;r.triggers=state.triggers;running&=send(r);p.controls_sent=now;p.state=state;
            }
            // A launcher may stop the physical IMU when focus/layout changes,
            // while SDL still considers reporting enabled. Re-arm through the
            // standard SDL API; never a per-controller packet or game action.
            if(p.motion.attempt(now)&&SDL_GamepadConnected(p.handle)){
                p.accel_stamp=0;
                gyrolib_sdl_detail::rearm_sensors(p.handle);
            }
        }
        if(now-heartbeat>=250000000){heartbeat=now;running&=send(Record{.kind=Heartbeat});}
        SDL_Delay(1);
    }
    for(auto& [id,p]:pads){p.haptic.reset();SDL_CloseGamepad(p.handle);}
#ifdef GL_SENSOR_FIXTURE
    fixture.shutdown();
#endif
    SDL_Quit();return 0;
}
