#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

namespace gyrolib_sdl_detail {
// A capability flag or cached reading is not evidence that the IMU is reporting.
// Use one monotonic clock per reader and call received only for a fresh pair.
struct SensorWatchdog {
    uint64_t last_motion{},last_rearm{};
    void opened(uint64_t now){last_motion=now;last_rearm=0;}
    void received(uint64_t now){last_motion=now;}
    bool attempt(uint64_t now){
        if(!last_motion||now<last_motion||now-last_motion<750000000)return false;
        if(last_rearm&&(now<last_rearm||now-last_rearm<2000000000))return false;
        last_rearm=now;return true;
    }
};
// Only for sensors this reader enabled itself. Disable BOTH before enabling
// either: SDL drivers may share one hardware switch across the two sensors.
inline bool rearm_sensors(SDL_Gamepad* pad){
    const bool gyro_off=SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_GYRO,false);
    const bool has_accel=SDL_GamepadHasSensor(pad,SDL_SENSOR_ACCEL);
    const bool accel_off=!has_accel||SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_ACCEL,false);
    const bool gyro_on=SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_GYRO,true);
    const bool accel_on=!has_accel||SDL_SetGamepadSensorEnabled(pad,SDL_SENSOR_ACCEL,true);
    return gyro_off&&accel_off&&gyro_on&&accel_on;
}
}
