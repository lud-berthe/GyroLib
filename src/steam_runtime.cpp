#include "gyrolib/steam.h"
#include <array>
#include <algorithm>
#include <initializer_list>
#include <new>
#include "detail/steam_runtime_api.hpp"
#if defined(_WIN32) && defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

struct gl_steam_runtime {
    gl_context* context{};
    gl_steam* reader{};
    HMODULE module{};
    gl_detail::SteamRuntimeApi api{};
    void* input{};
    int user{};
    uint64_t last_frame{},last_time{},retry_at{};
    bool polled{};
    uint32_t connected{};
    const char* error="Waiting for the game's Steam Input service";

    static uint32_t GL_CALL handles(void* data,uint64_t* output,uint32_t capacity) {
        auto& self=*static_cast<gl_steam_runtime*>(data);
        std::array<uint64_t,16> values{}; // STEAM_INPUT_MAX_COUNT
        const int count=self.api.handles(self.input,values.data());
        self.connected=count>=0 && count<=16?static_cast<uint32_t>(count):0;
        if(count<0 || count>16)return 17; // Rejected by the callback adapter.
        if(static_cast<uint32_t>(count)>capacity)return 17;
        std::copy_n(values.data(),count,output);
        return static_cast<uint32_t>(count);
    }
    static uint32_t GL_CALL motion(void* data,uint64_t handle,gl_steam_motion* output) {
        auto& self=*static_cast<gl_steam_runtime*>(data);
        const auto m=self.api.motion(self.input,handle);
        *output={m.accel_x,m.accel_y,m.accel_z,m.pitch,m.roll,m.yaw};
        return 1;
    }
    void retire() {
        gl_steam_destroy(reader);reader=nullptr;input=nullptr;user=0;
    }
    ~gl_steam_runtime() {retire();if(module)FreeLibrary(module);}
};
extern "C" {
gl_steam_runtime* GL_CALL gl_steam_runtime_create(gl_context* context) {
    if(!context)return nullptr;
    auto* result=new(std::nothrow) gl_steam_runtime;
    if(result)result->context=context;
    return result;
}
void GL_CALL gl_steam_runtime_destroy(gl_steam_runtime* self) {delete self;}
int32_t GL_CALL gl_steam_runtime_poll(gl_steam_runtime* self,uint64_t now,uint64_t frame) {
    if(!self || !now || (self->polled && (frame<=self->last_frame || now<=self->last_time)))return GL_INVALID;
    self->polled=true;self->last_frame=frame;self->last_time=now;
    if(!self->module) {
        if(now<self->retry_at)return GL_UNAVAILABLE;
        self->retry_at=now+1000000000ull;
        HMODULE module{};
        // Balanced reference to a loaded module only; never LoadLibrary/search PATH.
        if(!GetModuleHandleExW(0,L"steam_api64.dll",&module)) {
            self->error="The game has not loaded steam_api64.dll";return GL_UNAVAILABLE;
        }
        if(!self->api.bind([module](const char* name){return GetProcAddress(module,name);})) {
            FreeLibrary(module);self->error="The game's Steam DLL has no compatible public Input exports";
            return GL_UNAVAILABLE;
        }
        self->module=module;
    }
    const int user=self->api.user();
    void* input=user?self->api.input():nullptr;
    if(!user || !input) {
        self->retire();self->error="Waiting for the game's Steam Input service";
        return GL_UNAVAILABLE;
    }
    if(!self->reader || user!=self->user || input!=self->input) {
        self->retire();self->user=user;self->input=input;
        const gl_steam_provider provider{self,gl_steam_runtime::handles,gl_steam_runtime::motion,nullptr};
        self->reader=gl_steam_create(self->context,&provider);
    }
    if(!self->reader) {
        self->error="Cannot create the Steam Input reader";return GL_LIMIT;
    }
    const int32_t result=gl_steam_poll(self->reader,now,frame);
    self->error=result!=GL_OK?"Steam Input returned invalid controller data":
        self->connected?"":"No controllers reported by the game's Steam Input service";
    return result;
}
const char* GL_CALL gl_steam_runtime_error(const gl_steam_runtime* self) {
    return self?self->error:"No Steam runtime reader";
}
}
#else
extern "C" {
gl_steam_runtime* GL_CALL gl_steam_runtime_create(gl_context*) {return nullptr;}
void GL_CALL gl_steam_runtime_destroy(gl_steam_runtime*) {}
int32_t GL_CALL gl_steam_runtime_poll(gl_steam_runtime*,uint64_t,uint64_t) {return GL_UNAVAILABLE;}
const char* GL_CALL gl_steam_runtime_error(const gl_steam_runtime*) {return "Loaded Steam runtime bridge requires Windows x64";}
}
#endif
