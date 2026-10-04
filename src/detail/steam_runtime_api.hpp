#pragma once
#include <cstdint>
#include <cstddef>
#include <initializer_list>

namespace gl_detail {
// Minimal public flat-API contract; not an ISteamInput vtable. Verified against
// Valve's InputMotionData_t / steam_api_flat.h declarations already available
// locally. A struct returned by VALUE is important for the Windows x64 ABI.
struct SteamMotionPacket {
    float quaternion_x, quaternion_y, quaternion_z, quaternion_w;
    float accel_x, accel_y, accel_z;
    float pitch, roll, yaw;
};
static_assert(sizeof(SteamMotionPacket)==40);
static_assert(offsetof(SteamMotionPacket,pitch)==28);
struct SteamRuntimeApi {
    int (*user)(){};
    void* (*input)(){};
    int (*handles)(void*,uint64_t*){};
    SteamMotionPacket (*motion)(void*,uint64_t){};

    template<class Resolver> bool bind(Resolver resolve) {
        *this={};
        user=reinterpret_cast<decltype(user)>(resolve("SteamAPI_GetHSteamUser"));
        handles=reinterpret_cast<decltype(handles)>(resolve("SteamAPI_ISteamInput_GetConnectedControllers"));
        motion=reinterpret_cast<decltype(motion)>(resolve("SteamAPI_ISteamInput_GetMotionData"));
        // Use a versioned accessor exported alongside these flat wrappers.
        // Never obtain a different interface through SteamInternal or a vtable.
        for(const char* name:{"SteamAPI_SteamInput_v007","SteamAPI_SteamInput_v006",
            "SteamAPI_SteamInput_v005","SteamAPI_SteamInput_v004",
            "SteamAPI_SteamInput_v003","SteamAPI_SteamInput_v002","SteamAPI_SteamInput_v001"}) {
            if(auto address=resolve(name)) {input=reinterpret_cast<decltype(input)>(address);break;}
        }
        return user && input && handles && motion;
    }
};
}
