#ifndef GYROLIB_STEAM_H
#define GYROLIB_STEAM_H
#include "gyrolib.h"
#if defined(_WIN32) && defined(GL_STEAM_SHARED)
# ifdef GL_STEAM_BUILD
#  define GL_STEAM_API __declspec(dllexport)
# else
#  define GL_STEAM_API __declspec(dllimport)
# endif
#else
# define GL_STEAM_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct gl_steam gl_steam;
/* Borrowed service: the host owns initialization, frame updates and shutdown.
 * Callback implementation must query the initialized public Steamworks API, and
 * return connected handles. Never use a guessed private vtable.
 * GetMotionData has no hardware timestamp: poll only once per host Steam frame.
 */
typedef struct gl_steam_motion {
    float accel_x,accel_y,accel_z; /* Steam signed-short scale, +/-32768 = +/-2g */
    float pitch,roll,yaw;         /* +/-32768 = +/-2000 deg/s */
} gl_steam_motion;
typedef struct gl_steam_provider {
    void* user;
    uint32_t (GL_CALL *handles)(void*,uint64_t* handles,uint32_t capacity);
    uint32_t (GL_CALL *motion)(void*,uint64_t handle,gl_steam_motion*);
    /* Optional: report only controls actually readable from the current host
     * actions. Does NOT create action sets, mappings or emulate touch sensors. */
    uint32_t (GL_CALL *controls)(void*,uint64_t handle,gl_capabilities*,gl_controls*);
} gl_steam_provider;
GL_STEAM_API gl_steam* GL_CALL gl_steam_create(gl_context*,const gl_steam_provider*);
/* Optional names from verified physical action origins. UTF-8 strings are
 * borrowed only during the callback and copied by the adapter; nullptr means
 * unknown. Buttons use the same normalized ordinals as the controls callback.
 * The adapter calls this during poll, including when motion is unavailable.
 * This is separate to preserve the original provider struct's ABI. */
typedef const char* (GL_CALL *gl_steam_button_label_callback)(void*,uint64_t handle,uint32_t button);
GL_STEAM_API int32_t GL_CALL gl_steam_set_button_label_provider(gl_steam*,gl_steam_button_label_callback,void* user);
GL_STEAM_API void GL_CALL gl_steam_destroy(gl_steam*);
/* Once per host Steam frame, on the context owner thread. A repeated frame
 * serial is rejected with GL_INVALID. now_ns must increase between new frames. Invalid provider
 * handles, non-finite motion, and out-of-range controls return a C error code;
 * do not ignore it. All callbacks and their user data outlive the adapter. */
GL_STEAM_API int32_t GL_CALL gl_steam_poll(gl_steam*,uint64_t now_ns,uint64_t host_frame_serial);
GL_STEAM_API uint64_t GL_CALL gl_steam_endpoint_for_handle(const gl_steam*,uint64_t handle);

/* Optional Windows x64 bridge to an ALREADY loaded steam_api64.dll. It borrows
 * the game's initialized/updated Steam Input service; no SDK or Valve binary is
 * bundled. It never calls Init, RunFrame, Shutdown or changes action sets.
 * The host must arrange polling after its Steam frame update on the context
 * owner thread. SDL should be polled first, then this reader, then gl_update.
 * Do not also attach a gl_steam provider to the same context.
 * Creation does not load or initialize Steam. Poll retries when Steam is absent;
 * GL_UNAVAILABLE is a normal degraded state, not a reason to stop SDL/update.
 * Context outlives reader. Destroy before the host shuts its Steam service down.
 * Other platforms currently return NULL. Linux/Proton support is unverified. */
typedef struct gl_steam_runtime gl_steam_runtime;
GL_STEAM_API gl_steam_runtime* GL_CALL gl_steam_runtime_create(gl_context*);
GL_STEAM_API void GL_CALL gl_steam_runtime_destroy(gl_steam_runtime*);
GL_STEAM_API int32_t GL_CALL gl_steam_runtime_poll(gl_steam_runtime*,uint64_t now_ns,uint64_t host_frame_serial);
/* Borrowed diagnostic text, valid until the next call on this reader. */
GL_STEAM_API const char* GL_CALL gl_steam_runtime_error(const gl_steam_runtime*);
#ifdef __cplusplus
}
#endif
#endif
