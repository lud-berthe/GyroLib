# Release notes

[Documentation](INDEX.md) / Maintenance

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
