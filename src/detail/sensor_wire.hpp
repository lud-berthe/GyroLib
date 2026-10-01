#pragma once
#include <gyrolib/gyrolib.h>
#include <SDL3/SDL.h>
#include <cstdint>
#include <type_traits>

// Private same-build IPC; not the public C ABI. Fixed records, bounded OS pipe.
namespace gyrolib_sensor {
constexpr uint32_t magic=0x47595331,version=8;
enum Kind:uint32_t {Hello=1,Device,Removed,Motion,Heartbeat,Controls,Capabilities,ButtonLabel,Feedback};
enum CommandKind:uint32_t {Quit=1,RightPadPulse};
struct Command {uint32_t signature=magic,kind{};uint64_t endpoint{},observed_ns{};};
static_assert(sizeof(Command)==24&&std::is_trivially_copyable_v<Command>);
struct Record {
    uint32_t signature=magic,revision=version,kind{},hardware{}; // Device: VID low 16, PID high 16
    uint64_t endpoint{},identity{},observed_ns{},sensor_ns{};
    gl_vec3 gyro{},accel{};
    char name[128]{};
    gl_capabilities caps{};
    gl_controls controls{};
    gl_flick_input flick{};
    gl_trigger_input triggers{};
    char trigger_labels[2][32]{};
};
static_assert(std::is_trivially_copyable_v<Record>);
static_assert(sizeof(Record)==392);
inline uint64_t clock_ns(){
    const auto t=SDL_GetPerformanceCounter(),f=SDL_GetPerformanceFrequency();
    return (t/f)*1000000000ull+(t%f)*1000000000ull/f;
}
inline uint64_t identity(const char* text){
    uint64_t h=14695981039346656037ull;
    for(;*text;++text){h^=static_cast<unsigned char>(*text);h*=1099511628211ull;}
    return h|0x8000000000000000ull;
}
inline bool fresh(uint64_t observed,uint64_t current){return observed&&observed<=current&&current-observed<100000000;}
}
