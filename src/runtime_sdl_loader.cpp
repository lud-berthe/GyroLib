// MSVC's documented delay-load hook. An SDL-using consumer links this tiny
// static shim; no additional runtime DLL is introduced. The shim and adapter
// share exactly the SDL module verified and loaded by gyrolib.dll.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <delayimp.h>
#include <cstring>
#include <gyrolib/runtime.h>
static FARPROC WINAPI gyrolib_delay_load(unsigned event,PDelayLoadInfo info){
    if(event==dliNotePreLoadLibrary&&std::strcmp(info->szDll,"SDL3.dll")==0){
        const auto module=gl_runtime_sdl_handle();
        if(module)return reinterpret_cast<FARPROC>(module);
        // Never fall through to the default loader's search for another SDL.
        // Hosts should call gl_runtime_prepare first to handle errors normally.
        RaiseException(VcppException(ERROR_SEVERITY_ERROR,ERROR_MOD_NOT_FOUND),0,0,nullptr);
    }
    return nullptr;
}
extern "C" {
const PfnDliHook __pfnDliNotifyHook2=gyrolib_delay_load;
void gyrolib_sdl_loader_anchor(){}
}
