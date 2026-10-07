# Release notes

[Documentation](INDEX.md) / Maintenance

## 1.3.0 — 8 October 2026

- Recalibrate gyro now accepts fresh sensor data polled before the current core
  update. Previously the action compared it with the preceding frame's clock,
  rejected the click, and the panel reverted after its optimistic first frame.
- Steam mouse Block/Convert intercepts identified injected movement before it
  moves the system cursor, including in menus. Raw counts remain available for
  conversion, without a second native mouse effect. Physical mouse input and
  the settings panel remain usable; the bridge no longer confines the cursor.
- Manual calibration tolerates a larger stationary bias and gyro noise, while
  collecting longer to reduce uncertainty in the measured offset.
- Completed manual measurements are saved per identifiable SDL sensor and
  restored after reconnect/restart. A new manual calibration remeasures the raw
  stream; cancellation or movement retains the previous completed result.
  Automatic learning uses the manual bias/noise as a reference for bounded
  residual correction, without overwriting the saved manual result. Sensors
  without a serial remain session-only; duplicate identities cannot reuse a bias.
- Steam mouse Block remains active for a reported view even when camera output
  is disallowed or paused. Convert discards movement while output is unavailable;
  it never replays it on resume. Physical HID mice and the library panel remain
  available. Hosts must report menu identity independently of camera permission.

These fixes have synthetic coverage; affected controller hardware still needs
validation. ABI 1 is retained. The additive
`gl_set_endpoint_calibration_identity` API supports custom SDL-source producers;
the built-in acquisition paths supply identity automatically.

### Update

Close the host and replace `gyrolib.dll`; keep `gyrolib.ini` and the license
notices. Existing 1.x integrations remain compatible, and INI schema 0.2.0 is
unchanged. Completed manual calibrations are saved automatically when the sensor
has a unique hardware identity. Calibrate again to replace a saved reference.

The release includes the standalone DLL, SDK, demo and SHA-256 checksums.
Windows x64; Linux and Proton remain unvalidated. Mouse interception still
requires a supported bridge and confirmation of the Steam movement stream;
see [routing and detection limits](https://github.com/lud-berthe/GyroLib/blob/v1.3.0/docs/OVERLAY.md#steam-input-mouse-movement).

## 1.2.2 — 6 October 2026

Fixes Steam Input mouse blocking not taking effect until the settings panel had been opened in an SDL host using relative mouse mode.

- The bundled SDL window bridge now allows injected mouse motion to be identified from startup, without opening F10 first. It keeps the cursor confined to the window and preserves explicit host mouse rectangles.
- Physical mouse input still passes through. The stream-identification checks remain in place.
- The demo hides the system cursor during gameplay and uses its own inventory cursor. The settings and pause menus retain the system cursor.

The user confirmed that Block works on a fresh demo launch through Steam without opening F10. Automated validation covers the three build variants, installed SDK consumers, input routing, the SDL bridge and the demo. See [validation](https://github.com/lud-berthe/GyroLib/blob/v1.2.2/docs/VALIDATION.md).

### Update

Close the host and replace `gyrolib.dll`; keep `gyrolib.ini` and the license notices. No mod-side code change is required for this fix. Public APIs, ABI 1 and INI schema 0.2.0 are unchanged. Download the updated demo archive to get its cursor-visibility change as well.

The release provides the DLL alone, SDK, demo and SHA-256 checksums. Windows x64; Linux and Proton remain unvalidated.

## 1.2.1 — 6 October 2026

Fixes Steam Input mouse routing being restricted to touchpad contact.

- Steam-managed controllers no longer need touchpad hardware or contact to
  expose the per-view mouse setting or confirm a relative mouse stream.
- Pass through, Block and Convert movement apply to the shared Steam mouse
  stream, including mappings from controls other than the touchpad.
- Touchpad Flick Stick no longer disables mouse conversion globally. Avoid
  assigning the same control to both GyroLib motion and Steam mouse conversion.
- Cursor confinement ends after inactivity; ordinary physical HID mouse input
  remains unaffected.

Existing camera/cursor integrations, public APIs, ABI 1 and INI schema 0.2.0
are unchanged. Close the host and replace `gyrolib.dll`; keep `gyrolib.ini`.
Windows does not identify the originating Steam binding in mouse messages;
see [routing and detection limits](OVERLAY.md#steam-input-mouse-movement).

Downloads: [1.2.1 release](https://github.com/lud-berthe/GyroLib/releases/tag/v1.2.1)
includes the DLL, SDK, demo and SHA-256 checksums. Windows x64; Linux and Proton
remain unvalidated.

## 1.2.0 — 5 October 2026

- **Steam touchpad mouse per view:** pass movement through, block it, or convert
  it to camera/cursor output alongside GyroLib gyro. Block is the default.
  The option appears when a supported Windows bridge and controller metadata
  are available; filtering starts after movement-source confirmation. Physical
  mouse input passes through. The supplied SDL window bridge also supports
  non-Steam shortcuts. See [setup and detection limits](OVERLAY.md#steam-input-mouse-movement).
- **Progressive recentering:** an advanced duration setting, defaulting to 0 ms,
  uses the new `gl_set_recenter_step_callback`. The original instant callback
  remains supported; adopting the duration requires registering the new callback.
- **Source recovery:** improve isolated SDL reader recovery and device metadata
  when Steam Input is enabled or disabled during a session.
- **Calibration panel:** when Steam Input supplies motion, show an explanation
  in place of calibration controls. GyroLib does not recalibrate Steam data.
- **Faster demo rendering:** the Windows D3D11 renderer moves triangle and depth
  rendering onto the GPU, supports native 4K and retains a CPU fallback.
  Repeated exploration benchmarks reduce scene CPU time from 6.65 to 0.38 ms.
  A controller session through Steam confirmed improved fluidity; see
  [measurements and their limits](PERFORMANCE.md).

Existing 1.x integrations and settings remain compatible: C/overlay ABI 1 and
INI schema 0.2.0 are unchanged. Keep `gyrolib.ini` when replacing the DLL.
New functions require the 1.2.0 DLL and matching SDK headers. Mouse conversion
requires a supported window-input integration; it is not a global mouse hook.

Downloads: [1.2.0 release](https://github.com/lud-berthe/GyroLib/releases/tag/v1.2.0)
contains the standalone DLL, SDK, demo and SHA-256 checksums. Windows x64 binaries
require the Microsoft Visual C++ x64 runtime. Linux and Proton remain unvalidated.

## 1.1.0 — 5 October 2026

The DLL-owned DX12 settings panel now supports SDR 10-bit, scRGB and HDR10
backbuffers. This fixes missing panels on supported non-8-bit swapchains and
provides color-correct HDR composition.

- Hosts can supply the actual color space or use `GL_OVERLAY_COLOR_SPACE_AUTO`.
  Automatic detection is a fallback: SDR 10-bit on an HDR desktop can be
  ambiguous, so a known host encoding takes precedence.
- HDR UI white level defaults to 203 nits and can be set from 80 to 1000 nits
  through `gl_overlay_dx12_set_hdr_white_level`.
- The overlay preserves untouched game pixels and alpha, including extended
  scRGB values. It does not change the game's HDR settings or display metadata.

Existing 1.0 integrations remain compatible: C/overlay ABI 1, unchanged public
struct layouts and INI schema 0.2.0. Keep `gyrolib.ini` when replacing the DLL.
New HDR integrations should require GyroLib 1.1 or later.

Downloads: [1.1.0 release](https://github.com/lud-berthe/GyroLib/releases/tag/v1.1.0)
provides the SDK, standalone demo, `gyrolib.dll` and `SHA256SUMS.txt`.
Windows x64 binaries require the Microsoft Visual C++ x64 runtime.

WARP pixel tests cover scRGB/HDR10 blending, luminance, color conversion and
resource lifecycle. A manual session confirmed panel opening and normal colors
with a 10-bit SDR backbuffer on a Windows HDR desktop. Native PQ/scRGB output on
physical displays, Linux and Proton remain unvalidated. See
[Validation](VALIDATION.md) for the release test matrix and
[Overlay integration](OVERLAY.md) for supported formats and limits.

## 1.0.0 — 4 October 2026

First stable GyroLib SDK for Windows x64/MSVC. Mods can use the prebuilt DLL
through the C API or C++ wrapper; static builds are available from source.

### Included

- SDL acquisition with controller capabilities, hotplug and sensor recovery.
  Optional public Steam Input fallback uses the host's initialized service.
- Nine gyro spaces, independent sensitivities and inversion, activation families,
  calibration, smoothing, acceleration, flick stick and touchpad rotation.
- Camera and cursor view profiles, per-setting inheritance and recommended presets.
- Persistent settings, six languages, a native-menu model and supplied ImGui
  frontends, including a DLL-owned DX12 SDR panel.
- GyroLib Demo: third-person shooting range, four weapons, two sniper zoom levels,
  moving targets, inventory cursor and a native settings menu.

### Downloads

Get the archives from the [1.0.0 release](https://github.com/lud-berthe/GyroLib/releases/tag/v1.0.0):

- `GyroLib-1.0.0-sdk-windows-x64.zip`: DLL, headers, import/static helper libraries,
  CMake package, documentation, examples, demo and dependency notices.
- `GyroLib-1.0.0-demo-windows-x64.zip`: standalone demo and required runtime/notices.
  Extract it and run `gyrolib_demo.exe`.
- `gyrolib.dll`: library only, for updating an existing mod installation.
- `SHA256SUMS.txt`: checksums for the archives and standalone DLL.

Release binaries require the Microsoft Visual C++ x64 runtime. GyroLib is a library
for mods, not a universal injector. The mod connects game state, camera output
and any optional input or rendering hooks.

### Compatibility

The C and overlay ABI versions remain 1. The INI format remains `schema=0.2.0`;
existing settings are preserved. Use `find_package(GyroLib 1.0 CONFIG REQUIRED)`
for this SDK. Future 1.x releases preserve existing public contracts.

Settings use `gyrolib.ini` beside the DLL.

### Validated scope

Windows x64 is the supported release target. All 135 CTest executions passed
across fresh DLL/static/core builds and installed consumers/regressions. The
installed demo, desktop reader distribution check and quickstart also passed.
Automated checks and hardware
observations are listed in [Validation](VALIDATION.md). Public Steam fallback was
observed through a complete SDL → Steam → SDL transition with a Steam Controller
2026 over its puck, without a reported interruption or camera jump.

Linux/Proton, HDR, broader controller/transport combinations and touchpad haptic
feedback remain outside the validated scope. See [Known limits](LIMITS.md).
