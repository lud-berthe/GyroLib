#ifndef GYROLIB_SDL_H
#define GYROLIB_SDL_H
#include "gyrolib.h"
#if defined(_WIN32) && defined(GL_SDL_SHARED)
# ifdef GL_SDL_BUILD
#  define GL_SDL_API __declspec(dllexport)
# else
#  define GL_SDL_API __declspec(dllimport)
# endif
#else
# define GL_SDL_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct gl_sdl gl_sdl;
/* Optional adapter library. All functions on SDL's main thread. Context outlives
 * adapter. borrowed_subsystem=1 requires host-owned SDL_INIT_GAMEPAD; 0 acquires
 * one balanced subsystem reference. Never calls SDL_Quit or consumes events.
 * Enables sensors only on newly opened gamepads. After 750 ms without fresh
 * motion, re-arms those sensors through SDL, at most once per two seconds.
 * Pre-existing host gamepad
 * handles are observed without changing their sensor state. Close releases our
 * handle reference, never explicitly disables shared sensors.
 * Host must pump SDL events (or call gl_sdl_pump_events); disabled/filtered
 * sensor events cannot be observed.
 * Hotplug events request discovery on the next poll, with a 500 ms scan fallback.
 * UI readers (e.g. ImGui SDL manual gamepad mode) should borrow already-open
 * handles, releasing borrowed pointers before poll and reacquiring afterward. */
GL_SDL_API gl_sdl* GL_CALL gl_sdl_create(gl_context*,uint32_t borrowed_subsystem);
GL_SDL_API void GL_CALL gl_sdl_destroy(gl_sdl*);
/* Optional window input bridge, independent of rendering. Pass SDL_Window*, or
 * null to detach. Uses the supplied Windows SDL runtime; GL_UNAVAILABLE for
 * other SDL builds/platforms. No Steam SDK required, including non-Steam games.
 * All filtering/conversion and cursor confinement live in GyroLib. Consumed
 * movement never reaches SDL mouse state or events. Buttons/wheel pass through.
 * Does not replace the host's SDL event filter or Windows message hook. SDL
 * owns Raw Input registration while attached, including cursor views. Do not
 * register a competing mouse Raw Input reader in the same process.
 * Raw callbacks run on SDL's raw thread; detachment waits for them.
 * One window per reader and one mouse bridge per context/window (including
 * DX12 overlay). Detaches on window destruction or reader destruction.
 * Call on SDL main/context-owner thread. Context must outlive reader. */
GL_SDL_API int32_t GL_CALL gl_sdl_attach_window(gl_sdl*,void* sdl_window);
/* For hosts without an SDL event loop: call once before poll, on the SDL main
 * thread. Pumps our SDL instance without consuming the host's queued events.
 * Hosts already pumping this instance should continue their existing loop. */
GL_SDL_API int32_t GL_CALL gl_sdl_pump_events(gl_sdl*);
GL_SDL_API int32_t GL_CALL gl_sdl_poll(gl_sdl*,uint64_t now_ns);
/* Convenience frame for hosts without their own SDL event loop: pump events,
 * poll inputs, then gl_update using this reader's context. Declare/report views
 * first, just as for gl_update. Host state is always supplied by the game.
 * Output is cleared on failure. Camera callbacks run during this call: consume
 * either their deltas or the returned deltas, never both. Does not send haptics.
 * Hosts already pumping SDL or combining providers use poll + gl_update instead.
 * GL_OK permits no connected controller; consult gl_sdl_error for asynchronous
 * sensor-worker diagnostics. All calls remain on the owner/SDL main thread. */
GL_SDL_API int32_t GL_CALL gl_sdl_update(gl_sdl*,uint64_t now_ns,const gl_host_state*,gl_output*);
/* Optional output: call once AFTER gl_update to dispatch right-touchpad flick
 * pulses. No rumble/mapping/settings writes. Currently verified protocol only:
 * Steam Controller 2026 over USB/puck, identified by physical SDL metadata.
 * GL_UNAVAILABLE for unsupported hardware; GL_OK includes a queued worker pulse,
 * whose asynchronous failure is reported by gl_sdl_error. Repeated calls for
 * the same update do nothing. No output is enabled merely by creating a reader. */
GL_SDL_API int32_t GL_CALL gl_sdl_apply_feedback(gl_sdl*);
/* Isolated standard-SDL acquisition for motion and activators hidden by Steam's virtual pad.
 * Single-DLL Windows build: bundled worker is verified/extracted on demand in
 * the per-user cache (runtime.h). Modular/static build: default is
 * gyrolib_sensor_worker[.exe] beside the host executable. Set an absolute
 * UTF-8 path for a mod-specific directory, or "" to disable it. Never searches
 * PATH. Changing it stops our old helper and removes its motion companions.
 * Starts on demand for a Steam virtual pad without sensors. The host's SDL
 * environment/events and Steam service are unchanged; only the helper's SDL
 * discovery filters are cleared. Uses SDL's normal sensor drivers for all pads.
 * Unique vendor-matched pairs may associate after motion qualification (INPUT.md).
 * Otherwise bind explicitly using gl_bind_motion_sensor. A paired helper supplies
 * physical gyro activation buttons/contacts and separate physical flick inputs;
 * game commands stay on the host's virtual controller. No game/system input is injected.
 * The helper is stopped when the reader is destroyed.
 * All calls on owner/SDL main thread. */
GL_SDL_API int32_t GL_CALL gl_sdl_set_sensor_worker(gl_sdl*,const char* absolute_path);
/* Read-only identity information for explicit host association, no name guesses.
 * Steam handles refresh each poll (they can arrive after device opening).
 * Automatic identity changes follow the selected device; host overrides persist. */
GL_SDL_API uint64_t GL_CALL gl_sdl_endpoint_for_instance(const gl_sdl*,uint32_t sdl_instance);
GL_SDL_API uint64_t GL_CALL gl_sdl_steam_handle(const gl_sdl*,uint64_t endpoint);
/* Borrowed diagnostic text. A null reader returns the last creation error on
 * this thread (or "No SDL reader"). With a reader, nonempty text may describe
 * degraded asynchronous acquisition even when poll/update returns GL_OK. Copy
 * retained text before the next reader call; it is never owned by the host. */
GL_SDL_API const char* GL_CALL gl_sdl_error(const gl_sdl*);
#ifdef __cplusplus
}
#endif
#endif
