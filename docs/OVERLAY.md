# DX12 settings panel

[Documentation](INDEX.md) / Integration

The Windows SDK can render its settings panel directly from `gyrolib.dll`.
Include `gyrolib/overlay.h`; the mod does not build ImGui or supply an ImGui
context. This frontend and the static panel use the same widgets and settings.

```cmake
find_package(GyroLib 1.1 CONFIG REQUIRED COMPONENTS Core Overlay)
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
not upgrade its interface. Fill descriptor size/ABI, `GL_OVERLAY_COLOR_SPACE_AUTO`
(or an explicit color space, see below) and reserved 0. The backend retains COM
references until shutdown.

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

## Steam Input mouse movement

Since 1.2.0, `context.<id>.input.steam_mouse` appears last in each view as soon as
the Windows bridge is attached and the selected controller is identified through
Steam Input with a right or single touchpad. No mouse movement is required to
configure it. SDL supplies this identity from its Steam handle; a generic virtual
controller or a commercial name does not qualify. A custom acquisition adapter
can report the same proven metadata with `gl_set_endpoint_steam_input`.
The native menu model exposes the same setting, choices and visibility.

| Value | Choice | Result |
|---|---|---|
| 0 | Pass through | Leave mouse movement to the game |
| 1 | Block | Suppress mouse movement; keep GyroLib gyro working |
| 2 | Convert movement | Send movement through the current view output |

Block is the default. **Convert movement** keeps the same label for camera and
cursor views. It uses the existing output destination declared by the host. The setting saves and inherits
like other view settings. A view or mode change discards pending old movement.

The existing window bridge must forward `WM_INPUT` and `WM_MOUSEMOVE` even while
the panel is closed and honor each message's capture result. No new camera hook
is needed. `gl_overlay_capture` remains the panel-wide capture mask: it does not
represent individual controller mouse messages. Raw-input cleanup is handled by
GyroLib for messages it consumes. Independently polled host paths, such as
DirectInput or GetRawInputBuffer, need an adapter; they do not pass through this
DX12 window-message callback.

SDL hosts can call `gl_sdl_attach_window(reader, sdl_window)` once. The supplied
Windows SDL runtime intercepts both window messages and its buffered raw mouse
stream before updating SDL mouse state or queuing events. It uses the same DLL
routing code as DX12, with no filtering code in the host. This works independently
of the renderer or settings frontend, including a non-Steam shortcut. See
[SDL window input](INPUT.md#optional-sdl-window-input). A native menu alone does
not attach either input bridge.

Detection correlates at least three relative raw movements without a device
handle, injected mouse messages and a fresh contact on the selected controller's
right or single touchpad. It runs in all three modes and while the panel is open.
Before confirmation, movement passes through. Interception confirmation is
separate from early menu visibility and is remembered for the current device
selection, then cleared when it changes or disconnects. It is not saved in the
INI. Observed controller mouse motion can also expose the option when early
Steam metadata is unavailable.

The public metadata identifies Steam Input availability, not the complete legacy
mouse mapping. The setting is therefore offered on compatible Steam-managed
controllers even if their pad is not currently assigned to mouse. Neither the
UI nor the first capture needs a movement to set its policy; only interception
waits for correlated input. See [SDL's Steam handle](https://wiki.libsdl.org/SDL3/SDL_GetGamepadSteamHandle)
and [Steam's action-based API](https://partner.steamgames.com/doc/api/ISteamInput).

Windows provides no Steam-specific identity on these messages. A device-less
packet or injected origin alone is insufficient; another injector active during
pad contact can still match. Ordinary physical mice retain their input. This
version targets the touchpad-to-mouse mapping tested on Windows, not arbitrary
controller mappings or Linux/Proton. It is separate from the Steam Input **gyro
sensor fallback** and never reinterprets mouse deltas as sensor measurements.

Conversion uses 0.05 degrees per count, right-positive yaw and up-positive pitch,
plus the view's optional FOV compensation. Gyro sensitivity, smoothing and
calibration do not apply to mouse counts. Touchpad Flick Stick takes priority:
Block/Convert movement consume the mouse path without adding a second pad rotation.
The real gyro continues normally. Buttons and wheel packets pass through.

Pause, loss of focus, an unavailable output destination or the library panel
suspend interception. Camera-enabled game menus follow their view's setting.
Cursor confinement applies only in gameplay camera views for Block/Convert
movement. It ends on physical-mouse activity, menus, F10, Alt-Tab and shutdown,
restoring the previous host restriction only if it has not changed meanwhile.
The cursor can still move within the game window. A 250 ms physical-mouse
preference prevents immediate confinement from residual touchpad inertia.

## Resize and shutdown

Before ResizeBuffers, call `gl_overlay_dx12_before_resize` on the render thread.
Proceed only after success: it waits for the fence and releases backbuffers.
The next render reacquires them. Changed buffer count/format or device replacement
requires shutdown and reinitialization before old renderer objects are released.
With an explicit color space, reinitialize when it changes, even if the pixel
format stays the same. AUTO refreshes the output while the panel is open and
recreates its renderer if its SDR/HDR decision changes.

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

The backend supports Windows DX12 with 2–8 single-sample buffers:

| Output | Backbuffer format | `color_space` (DXGI) |
|---|---|---|
| SDR | `R8G8B8A8_UNORM`, `B8G8R8A8_UNORM` or `R10G10B10A2_UNORM` | 0 — RGB full G22/P709 |
| scRGB | `R16G16B16A16_FLOAT` | 1 — RGB full G10/P709 |
| HDR10 | `R10G10B10A2_UNORM` | 12 — RGB full G2084/P2020 |

AUTO uses standard format conventions: 8-bit buffers use SDR, FP16 uses scRGB,
and RGB10 uses HDR10 when DXGI reports an HDR output, otherwise SDR. This avoids
needing game-specific HDR flags for conventional swapchains.
For hybrid adapters or WARP, it can locate the physical output from the host
window. If no Advanced Color output is available, RGB10 falls back to SDR.

This is a heuristic, not a query of the swapchain's current encoding: DXGI has
no getter for that value. In particular, a game can render 10-bit SDR on an HDR
desktop. In that case pass 0 explicitly, or report the space known by the host
renderer/`SetColorSpace1`. Explicit values always override AUTO. No swapchain
color space, HDR metadata or Windows display setting is changed by GyroLib.

In HDR, the panel renders to a transparent SDR texture, then blends with a copy
of the scene in linear light. HDR10 includes sRGB decoding, Rec.709-to-Rec.2020
conversion and PQ encoding. scRGB preserves extended and negative scene values.
Pixels outside the panel are untouched, including the backbuffer alpha.

UI white defaults to **203 nits**. A host can match its own UI brightness with
`gl_overlay_dx12_set_hdr_white_level(overlay, nits)` on the render thread after
initialization (80–1000 nits). The value resets on reinitialization and changes
only GyroLib's UI. SDR returns `GL_UNAVAILABLE`.

HDR uses two additional full-resolution textures and a composition pass while
the panel is open. Resizing releases and recreates those textures. Closed panels
submit no work. Numeric colors and transitions are tested on WARP. A manual
session confirmed opening and colors for 10-bit SDR on an HDR desktop, using
the host's explicit space. Native PQ/scRGB display appearance, Auto HDR and
third-party HDR injection are not validated. See Microsoft's [Advanced Color guidance](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
for the swapchain and luminance conventions.

Zero-sized client areas do not draw. DX11, Vulkan, OpenGL and Linux/Proton
backends are not provided.

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
