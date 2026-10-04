#ifndef GYROLIB_OVERLAY_H
#define GYROLIB_OVERLAY_H
#include "gyrolib.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct gl_overlay gl_overlay;
#define GL_OVERLAY_ABI_VERSION 1u
enum { GL_OVERLAY_CAPTURE_MOUSE=1u, GL_OVERLAY_CAPTURE_KEYBOARD=2u,
       GL_OVERLAY_CAPTURE_GAMEPAD=4u };
/* Optional Windows DX12 frontend, compiled into the prebuilt DLL. No ImGui/SDL
 * objects cross this interface. No automatic injection, window subclassing or
 * Present hooks. Host supplies its existing renderer callbacks.
 * create/process/detach: context owner thread, outside DllMain. Context must live
 * until detach. process BEFORE every gl_update: applies queued edits (including
 * auto-save) and gates output. The following successful gl_update automatically
 * publishes an immutable UI snapshot with the completed frame's clock and input
 * capabilities. No second process call is needed. Does not consume the host's
 * diagnostic event queue. Never reads motion fusion on render thread.
 * init/render/before_resize/shutdown: one render thread. Forward window messages
 * from the window thread; copied messages are consumed on the render thread.
 * destroy only after detach + shutdown and after ALL producers/callbacks stop.
 * Exactly one overlay per context. Do not link this frontend to host ImGui.
 */
GL_API gl_overlay* GL_CALL gl_overlay_create(gl_context*,uint32_t abi_version);
GL_API int32_t GL_CALL gl_overlay_process(gl_overlay*);
GL_API int32_t GL_CALL gl_overlay_detach(gl_overlay*);
GL_API int32_t GL_CALL gl_overlay_destroy(gl_overlay*);
/* Thread-safe visibility request; process commits it to the owner context.
 * While attached, use this instead of gl_set_panel_open for this frontend. */
GL_API int32_t GL_CALL gl_overlay_set_open(gl_overlay*,uint32_t open);
GL_API uint32_t GL_CALL gl_overlay_capture(const gl_overlay*);
/* HWND / IDXGISwapChain3* / ID3D12CommandQueue* (DIRECT, same device).
 * COM references are retained until shutdown. Own heaps, allocators, command
 * lists and fences; never change the host's graphics pipeline/descriptor heaps.
 * Supports 2..8 single-sample buffers: R8G8B8A8_UNORM or B8G8R8A8_UNORM.
 * HDR/color-space conversion is not supported. UINT64 handles are never pointers. */
typedef struct gl_overlay_dx12_desc {
    uint32_t size,abi_version;
    uint32_t color_space,reserved; /* DXGI_COLOR_SPACE_TYPE, SDR G22/P709=0; reserved=0 */
    void *window,*swapchain,*command_queue;
} gl_overlay_dx12_desc;
GL_API int32_t GL_CALL gl_overlay_dx12_init(gl_overlay*,const gl_overlay_dx12_desc*);
/* Call AFTER the game's last backbuffer work is submitted on the SAME queue,
 * BEFORE Present. Current buffer MUST be PRESENT. Transitions it to RT and back.
 * GyroLib does not clear, Present, or resize. delta_seconds >0, <=1; DPI >0.
 * No render work while closed. Owner snapshots older than 250ms close the UI.
 * Zero-sized/minimized client areas are ignored. Device loss returns IO_ERROR.
 */
GL_API int32_t GL_CALL gl_overlay_dx12_render(gl_overlay*,double delta_seconds,float dpi_scale);
/* Call BEFORE ResizeBuffers: drains our fence and releases backbuffer refs.
 * Next render lazily reacquires buffers; changed count/format requires shutdown
 * and init. On full device replacement, shutdown before releasing host objects. */
GL_API int32_t GL_CALL gl_overlay_dx12_before_resize(gl_overlay*);
GL_API int32_t GL_CALL gl_overlay_dx12_shutdown(gl_overlay*);
/* HWND, Windows message, WPARAM and LPARAM. Fixed-width C ABI, no windows.h
 * required. Return capture bits (NOT an LRESULT): host decides what to suppress.
 * Always preserve lifecycle/focus messages and unrelated game actions.
 * Relative/raw mouse and gamepad gameplay must also respect capture(), even if
 * not delivered through this function. No synthetic OS input is emitted. */
GL_API uint32_t GL_CALL gl_overlay_win32_message(gl_overlay*,void* window,
    uint32_t message,uint64_t wparam,int64_t lparam);
/* Thread-local diagnostic string, valid until next diagnostic on this thread. */
GL_API const char* GL_CALL gl_overlay_error(void);
#ifdef __cplusplus
}
#endif
#endif
