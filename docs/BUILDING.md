# Build and consume

Requires CMake 3.24+, C++20, and C11 for the C consumer checks. Windows x64 with
MSVC is tested. The Windows build scripts and bundled-runtime tests
require **PowerShell 7**, available as `pwsh` on PATH; Windows PowerShell 5.1
(`powershell.exe`) is insufficient. Check with `pwsh --version` before configuring.
Consuming a prebuilt SDK does not itself require PowerShell.

Configuration performs no downloads. SDL, Dear ImGui and GamepadMotionHelpers
are vendored; [THIRD_PARTY.md](THIRD_PARTY.md) owns their versions and notices.

## Build variants

```powershell
# Default Windows single DLL, panel, demo and tests:
pwsh -NoProfile -File ./tools/build.ps1
# Core and Steam adapter without SDL/UI:
pwsh -NoProfile -File ./tools/build.ps1 -BuildDirectory build-core -CoreOnly
# Full static build:
pwsh -NoProfile -File ./tools/build.ps1 -BuildDirectory build-static -Static
# Separate shared core/acquisition DLLs:
pwsh -NoProfile -File ./tools/build.ps1 -BuildDirectory build-modular -Modular
```

The script configures, builds Release and runs CTest. It explicitly resets its
variant options on each invocation, so an earlier CoreOnly/Static run does not
silently change a later default build. Separate directories remain convenient
for comparing variants. Equivalent commands for the default Visual Studio build are:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist/sdk
```

| Option | Purpose |
|---|---|
| `BUILD_SHARED_LIBS` | Shared or static core/acquisition libraries |
| `GL_SINGLE_DLL` | Bundle core, acquisition and runtime resources into one Windows/MSVC DLL; default on for supported shared builds |
| `GL_BUILD_SDL` | Build SDL acquisition and its isolated sensor reader |
| `GL_BUILD_PANEL` | Build the static ImGui settings frontend |
| `GL_BUILD_EXAMPLES` | Build/install the sole demo, `gyrolib_demo` |
| `GL_BUILD_TESTS` | Build regression/consumer fixtures; these are not installed |
| `GL_USE_BUNDLED_SDL` | Use the checked-in Windows x64 SDL development package |

Single-DLL builds require Windows, MSVC, shared linkage and SDL. Static and modular
builds set `GL_SINGLE_DLL=OFF`. The panel remains static code in the host. Build
directories contain fixtures and intermediates: distribute an installation,
not the whole build tree. [DISTRIBUTION.md](DISTRIBUTION.md) owns runtime payloads,
SDL instance ownership and cache behavior.

## Consume the installed SDK

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyMod LANGUAGES CXX)
find_package(GyroLib CONFIG REQUIRED COMPONENTS Core)
add_library(my_mod SHARED mod.cpp)
target_link_libraries(my_mod PRIVATE GyroLib::gyrolib)
```

Configure with `-DCMAKE_PREFIX_PATH=<absolute-sdk-directory>`. The exported targets
carry includes, language requirements and import definitions. C-only consumers
can use `LANGUAGES C`; no C++ types or exceptions cross the C ABI.
The SDK can be moved to another directory; its package files resolve dependencies
relative to the installation.

| Component / target | Use |
|---|---|
| `Core` / `GyroLib::gyrolib` | Core C ABI or C++ `Context`; no SDL headers needed |
| `SDL` / `GyroLib::gyrolib_sdl` | SDL adapter and direct SDL calls; the default Windows SDK includes the matching SDL development package |
| `Steam` / `GyroLib::gyrolib_steam` | Borrowed callback adapter; no Steam SDK required for this adapter itself |
| `Panel` / `GyroLib::gyrolib_panel` | Static frontend; host supplies ImGui frame, events and renderer |

For the default bundled SDK, a host using only GyroLib's C acquisition functions
or `SdlInput` can link `GyroLib::gyrolib` without an SDL development package.
Direct SDL calls, as in the README window example, require the `SDL` component;
its target supplies the delay-load shim. The installed Windows SDK automatically
finds its SDL development files under `third_party/SDL3`. `CMAKE_PREFIX_PATH` is
the only package path required; do not point it back into the GyroLib checkout.
Windows modular/static SDKs built with vendored SDL include the same development
package and their consumers also link the SDL target.

A build using external SDL (`GL_USE_BUNDLED_SDL=OFF`, or an explicitly supplied
external `SDL3_DIR`) keeps that dependency external: its SDK consumers must make
the matching SDL package discoverable. An explicit consumer `SDL3_DIR` or existing
SDL target can select another installation, which must be compatible with the
GyroLib build. SDL instance ownership is described in [DISTRIBUTION.md](DISTRIBUTION.md).

A source consumer can instead use `add_subdirectory` and the same `GyroLib::`
target names. Request only the components built into your SDK. A core-only
configuration has no SDL/ImGui dependency. The typed Steamworks bridge example
requires the host's licensed SDK; the callback adapter does not.

The complete minimal C++ program is in the [README](../README.md#first-integration).
The `tests/install_consumer` project checks package discovery independently of
source-tree includes. Verify an installed SDK after changing exports or packaging;
a successful source-tree build alone does not establish that contract.

## Dependency reconstruction and other platforms

Run `pwsh -NoProfile -File ./tools/build_sdl.ps1` only when rebuilding the vendored SDL binary. It uses
a checksum-verified source archive, downloading it only if absent, and applies
the patches documented in [SDL changes](../third_party/SDL/README-GyroLib.md).
Installed SDKs retain these notices/patches under `share/doc/GyroLib/sdl-changes`.
Normal GyroLib builds use the existing dependency; no Steam SDK is downloaded.

For a prospective Linux build, supply a system SDL3 package and use:

```sh
cmake -S . -B build -DGL_USE_BUNDLED_SDL=OFF -DGL_SINGLE_DLL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Linux compilation/loading, Steam Deck, Wine and Proton have not been validated.
Windows binaries and tests do not establish support there. The current evidence
and manual acceptance boundaries are in [VALIDATION.md](VALIDATION.md).
