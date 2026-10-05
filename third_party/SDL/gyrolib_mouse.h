/* GyroLib window input extension, MIT (GyroLib root LICENSE).
 * Altered SDL: optional per-window callbacks, before mouse state/event updates.
 * Registration and messages use SDL main thread; raw packets use SDL raw thread.
 * SDL holds the window property lock through each callback (safe detach). */
#ifndef GYROLIB_SDL_MOUSE_H
#define GYROLIB_SDL_MOUSE_H
#define GL_SDL_MOUSE_VERSION "gyrolib.mouse.version"
#define GL_SDL_MOUSE_SUPPORT "gyrolib.mouse.support"
typedef struct GyroLibSDLMouseSupport {
    bool (SDLCALL *refresh)(void);
} GyroLibSDLMouseSupport;
#define GL_SDL_MOUSE_FILTER "gyrolib.mouse.filter"
typedef struct GyroLibSDLMouseFilter {
    void *user;
    bool (SDLCALL *raw)(void *, Uint64, bool, Uint16, Sint32, Sint32);
    bool (SDLCALL *message)(void *, void *, Uint32, Uint64, Sint64);
} GyroLibSDLMouseFilter;
#endif
