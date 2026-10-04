#include "../src/detail/steam_runtime_api.hpp"
#include <limits>
namespace {int mode{},forbidden{};int session=1;}
#define API extern "C" __declspec(dllexport)
API void FixtureSteamMode(int value) {mode=value;}
API int FixtureSteamForbiddenCalls() {return forbidden;}
API int SteamAPI_GetHSteamUser() {return mode==1?0:session;}
API void* SteamAPI_SteamInput_v005() {return mode==2?nullptr:&session;}
API int SteamAPI_ISteamInput_GetConnectedControllers(void*,uint64_t* handles) {
    if(mode==3)return 0;
    if(mode==4)return 17;
    handles[0]=42;
    return 1;
}
API gl_detail::SteamMotionPacket SteamAPI_ISteamInput_GetMotionData(void*,uint64_t) {
    gl_detail::SteamMotionPacket result{};
    result.quaternion_w=1;result.accel_z=16384;result.yaw=1638.4f;
    if(mode==5)result.yaw=std::numeric_limits<float>::quiet_NaN();
    return result;
}
// A bridge that takes ownership would increment this counter and fail the test.
API bool SteamAPI_ISteamInput_Init(void*,bool) {++forbidden;return true;}
API bool SteamAPI_ISteamInput_Shutdown(void*) {++forbidden;return true;}
API void SteamAPI_ISteamInput_RunFrame(void*,bool) {++forbidden;}
API void SteamAPI_ISteamInput_ActivateActionSet(void*,uint64_t,uint64_t) {++forbidden;}
