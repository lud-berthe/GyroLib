# Release notes

[Documentation](INDEX.md) / Maintenance

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
