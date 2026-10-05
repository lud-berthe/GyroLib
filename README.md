# GyroLib

GyroLib is a C++ library for adding controller gyro to game mods. It provides a
C API and a C++ wrapper, with a core independent of the game engine and renderer.
Integrate the supplied `gyrolib.dll` using the SDK headers and examples.

**Version 1.1.0.** Tested on Windows x64/MSVC.
Linux and Proton remain unvalidated.

[Download DLL, SDK or demo](https://github.com/lud-berthe/GyroLib/releases/tag/v1.1.0)
· [Release notes](docs/RELEASE_NOTES.md)

## What GyroLib handles

- **Controller input:** discovery, capabilities, hotplug and motion-source
  selection. SDL has priority; an optional Steam Input reader can supply a
  fallback through the host's initialized Steam Input service.
- **Motion:** gyro spaces, sensitivity, activation, calibration, smoothing,
  acceleration and flick stick or touchpad rotation.
- **View profiles:** separate settings for cameras and cursors, inheritance
  between views and optional recommended presets supplied by the mod.
- **Configuration:** saved settings, localized text, an ImGui panel and a shared
  menu model for native game widgets. The DX12 overlay supports SDR, scRGB and
  HDR10 buffers; see [overlay integration](docs/OVERLAY.md) for color-space setup.

## What the mod connects

Register the game's camera and cursor views, report which view is active and
provide focus, pause and menu states. Each frame, pass input to GyroLib and apply
its output to the active camera or cursor. Aiming comes from the game's resolved
commands; GyroLib observes it and never triggers or changes it.

Some features need additional integration: flick stick needs the game's native
stick rotation suppressed, long-press blocking needs an action-filter hook, and
camera recentering needs a host callback. The supplied panel needs rendering and
window integration, or you can build a native menu from the public model.
GyroLib does not install these hooks for you.

## Get started

Follow [First integration](docs/QUICKSTART.md) to compile a small host against the
SDK. Then use the [integration guides](docs/INDEX.md#integrate-a-mod) to connect
the frame loop, views, settings and menus in that order.

The default Windows runtime layout is:

```text
game.exe
my_mod.dll
gyrolib.dll
```

GyroLib creates `gyrolib.ini` beside its DLL when the mod initializes settings.
Bundled SDL and the optional sensor reader are extracted to a per-user cache;
the reader runs in a separate process when needed. See
[Distribution](docs/DISTRIBUTION.md) for packaging and the
[license notices](docs/THIRD_PARTY.md) to include.

## Try the demo

Run `bin/gyrolib_demo.exe` from an installed SDK. The third-person shooting range
demonstrates weapon aiming, a two-level sniper scope and an inventory cursor,
each using view profiles. Open the gyro panel with **F10** or **Back + Start**
(the equivalent buttons on your controller). The pause menu also includes a
native gyro menu using the same settings.

The [demo guide](docs/TPS_DEMO.md) lists controls and explains its integration.

## Build from source

You need CMake 3.24+, Visual Studio C++20 tools and PowerShell 7. Normal builds
use the included dependencies and download nothing.

To build the default Windows SDK from this checkout:

```powershell
pwsh -NoProfile -File ./tools/build.ps1
cmake --install build --config Release --prefix dist/sdk
./dist/sdk/bin/gyrolib_demo.exe
```

[Building and linking](docs/BUILDING.md) covers SDK components, static and modular
variants, and test commands.

## Documentation

- [Documentation index](docs/INDEX.md) — integration path and feature reference.
- [Known limits](docs/LIMITS.md) — platform, hardware and integration constraints.
- [Validation](docs/VALIDATION.md) — automated tests and real-controller results.
- [SDK audit](docs/SDK_AUDIT.md) — findings, fixes and supporting evidence.
