# DX12 settings panel

[Documentation](INDEX.md) / Integration

The Windows SDK can render its settings panel directly from `gyrolib.dll`.
Include `gyrolib/overlay.h`; the mod does not build ImGui or supply an ImGui
context. This frontend and the static panel use the same widgets and settings.

```cmake
find_package(GyroLib 1.0 CONFIG REQUIRED COMPONENTS Core Overlay)
target_link_libraries(my_mod PRIVATE GyroLib::gyrolib)
```

[The C example](../examples/overlay_dx12.c) shows the integration callbacks.
The mod connects them to the game's window, renderer and input lifecycle.
GyroLib does not discover a swapchain or install hooks.

## Call sequence and threads

| Thread | Calls and responsibilities |
|---|---|
| Context owner | Create with `gl_overlay_create(context, GL_OVERLAY_ABI_VERSION)`; after acquisition, call `gl_overlay_process` once before each `gl_update` |
| Render | Initialize with `gl_overlay_dx12_init`; render before Present after game backbuffer work |
| Window | Forward messages to `gl_overlay_win32_message` |

`gl_overlay_process` applies queued edits through normal setters, including saving,
and updates panel visibility. The next successful `gl_update` publishes a fresh
snapshot after resolving input/capabilities. No extra publication call is needed;
the host's diagnostic event queue is not drained.

The renderer reads snapshots rather than the live context. Edits return to the
owner as commands and take effect on a subsequent update. One overlay attaches
to a context. Other core API calls retain their owner-thread requirement.

### Initialize the renderer

Supply the HWND, an `IDXGISwapChain3` pointer and its DIRECT `ID3D12CommandQueue`,
all from the same device. Use QueryInterface for an older swapchain; a cast does
not upgrade its interface. Fill descriptor size/ABI, actual SDR color space 0
and reserved 0. The backend retains COM references until shutdown.

Each frame, submit game backbuffer work on that queue, return the backbuffer to
PRESENT state and call `gl_overlay_dx12_render(overlay, dt_seconds, dpi_scale)`
before Present. Use finite `0 < dt <= 1` and positive `window DPI / 96`. GyroLib
records its own command list, transitions to RENDER_TARGET and back, and draws
without clearing or calling Present. Initialization/render/destruction must not
run from `DllMain`.

### Route input

`gl_overlay_win32_message` copies events into a bounded queue and returns an
input-capture mask, not a Windows LRESULT. Preserve focus, close, resize and
Alt+F4 processing. The host must also gate raw/relative mouse and gameplay gamepad
paths while capture is active; Win32 forwarding alone cannot suppress those.

The [configured shortcuts](SETTINGS.md#menu-shortcut) default to F10 and
Back + Start on the controller.
Both can be disabled by a host that provides a native menu.
`gl_overlay_set_open` can request visibility from any thread; use it instead of
`gl_set_panel_open` while this frontend is attached. `gl_overlay_capture` reports
mouse/keyboard/gamepad capture. Continue acquiring controls while open: gamepad
navigation uses the selected controller's acquired state.

## Resize and shutdown

Before ResizeBuffers, call `gl_overlay_dx12_before_resize` on the render thread.
Proceed only after success: it waits for the fence and releases backbuffers.
The next render reacquires them. Changed buffer count/format or device replacement
requires shutdown and reinitialization before old renderer objects are released.

To shut down:

1. Stop new calls and drain in-flight callbacks.
2. Call `gl_overlay_detach` on the context owner thread.
3. Call `gl_overlay_dx12_shutdown` on the render thread.
4. Call `gl_overlay_destroy` after detachment and renderer shutdown.

The core context may be destroyed after detach; the overlay must survive renderer
shutdown. Destroying an attached or initialized overlay is rejected.

Check results and read thread-local `gl_overlay_error` on the failing thread.
A fence timeout on a live device leaves resources alive: resolve the stall and
retry rather than unloading the DLL. Device removal permits cleanup but remains
an error. Owner/render heartbeats older than 250 ms close the panel so stale UI
cannot retain gameplay capture indefinitely.

## Supported rendering configuration

The backend supports Windows DX12, 2–8 single-sample buffers, R8G8B8A8_UNORM or
B8G8R8A8_UNORM, and SDR G22/P709. It neither detects nor converts HDR. Zero-sized
client areas do not draw. DX11, Vulkan, OpenGL and Linux/Proton backends are not
provided.

Only one overlay GPU submission is outstanding at a time; the next open-panel
render waits for its fence. This protects buffer/font texture reuse but can add
synchronization cost. Closed panels submit no work. Real-game performance still
requires measurement.

The DLL owns private ImGui symbols and GPU resources. `GL_BUILD_OVERLAY=OFF`
omits the backend; it requires `GL_BUILD_PANEL=ON` and Windows.
`GL_OVERLAY_DX12` is exported to CMake consumers when available. The public header
has no Windows/SDL/ImGui header dependency. When upgrading ImGui, regenerate its
private symbol map with `tools/generate_overlay_namespace.ps1` and rerun coexistence
checks. [Validation](VALIDATION.md) covers WARP rendering, input, threading and
lifecycle tests. The demo itself uses the static SDL panel.
