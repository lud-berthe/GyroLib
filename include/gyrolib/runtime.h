#ifndef GYROLIB_RUNTIME_H
#define GYROLIB_RUNTIME_H
#include "gyrolib.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Windows single-DLL distribution only. Call outside DllMain, on the host's
 * main thread before using SDL. gl_sdl_create also prepares it automatically.
 * Extracts and verifies the bundled SDL in a per-user, content-versioned cache;
 * the isolated reader is extracted later, only when required. No downloads.
 * GL_UNAVAILABLE in modular/static builds. Failure leaves the core usable.
 * Thread-safe initialization; errors are thread-local. Returned strings remain
 * valid until the next runtime call on that thread. No explicit shutdown needed:
 * destroy readers/panels/contexts before unloading GyroLib as usual.
 * GYROLIB_RUNTIME_CACHE may specify an absolute local cache root for portable
 * hosts/tests; otherwise uses LocalAppData/GyroLib/runtime. No PATH search. */
GL_API int32_t GL_CALL gl_runtime_prepare(void);
GL_API const char* GL_CALL gl_runtime_error(void);
GL_API const char* GL_CALL gl_runtime_directory(void);
/* Borrowed Windows HMODULE, for hosts using SDL directly (e.g. renderer backend).
 * NULL on preparation failure or non-bundled builds. Never FreeLibrary it.
 * Only use the shipped SDL headers/version with this module. A game's own SDL
 * may be a different instance; do not pass handles between them. SDL itself
 * stays loaded until process exit. GyroLib never replaces a host's SDL module. */
GL_API void* GL_CALL gl_runtime_sdl_handle(void);
/* Optional startup prewarming: verifies/extracts the isolated reader but does
 * not launch it. Normally the adapter does this on demand. GL_UNAVAILABLE in
 * non-bundled builds. Error and cache location are available above. */
GL_API int32_t GL_CALL gl_runtime_prepare_sensor_worker(void);
#ifdef __cplusplus
}
#endif
#endif
