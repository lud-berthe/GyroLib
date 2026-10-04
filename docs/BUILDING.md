# Build and consume the SDK

[Documentation](INDEX.md) / Maintenance

Use the installed CMake package to link a mod without rebuilding GyroLib.
Source-build instructions follow below. Dependencies are included; normal
configuration downloads nothing.

## Link a mod

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyMod LANGUAGES CXX)
find_package(GyroLib 1.0 CONFIG REQUIRED COMPONENTS Core)
add_library(my_mod SHARED mod.cpp)
target_link_libraries(my_mod PRIVATE GyroLib::gyrolib)
```

Configure with `-DCMAKE_PREFIX_PATH=<absolute-sdk-directory>`. Targets carry
include paths, language requirements and import definitions. C consumers can use
`LANGUAGES C`. The installed SDK is relocatable.

| Component | Target | Use |
|---|---|---|
| Core | `GyroLib::gyrolib` | C API or C++ Context |
| SDL | `GyroLib::gyrolib_sdl` | SDL adapter and direct SDL calls |
| Steam | `GyroLib::gyrolib_steam` | Borrowed callback adapter; no SDK needed for the adapter itself |
| Panel | `GyroLib::gyrolib_panel` | Static host-owned ImGui frontend |
| Overlay | `GyroLib::gyrolib` | Autonomous DX12 frontend when built in |

Request only components present in the SDK. A core-only build has no SDL/ImGui
dependency. In the default bundled SDK, hosts using only GyroLib's acquisition
API or C++ `SdlInput` can link Core. Direct SDL calls, as in the
[quickstart](QUICKSTART.md), need the SDL component and its delay-load shim.

Windows SDKs built with vendored SDL include its matching development package
under `third_party/SDL3`; `CMAKE_PREFIX_PATH` is enough. An external SDL build
(`GL_USE_BUNDLED_SDL=OFF` or an explicit external `SDL3_DIR`) keeps that dependency
external, so consumers must make the matching package discoverable. An existing
SDL target or explicit consumer `SDL3_DIR` can override discovery; it must remain
compatible with the GyroLib build. Follow [SDL instance ownership](DISTRIBUTION.md#hosts-that-call-sdl).

Source consumers can use `add_subdirectory` and the same targets. The typed
Steamworks bridge separately requires the host's licensed SDK.

## Build variants

Source builds require CMake 3.24+, C++20 and C11 for the C checks. Windows
x64/MSVC is tested. The scripts and bundled-runtime tests need PowerShell 7
(`pwsh` on PATH); Windows PowerShell 5.1 is insufficient. Prebuilt SDK consumers
do not need PowerShell. Dependency versions are in [Licenses and provenance](THIRD_PARTY.md).

```powershell
# Default Windows single DLL, panel, demo and tests:
pwsh -NoProfile -File ./tools/build.ps1
# Core and Steam adapter without SDL/UI:
pwsh -NoProfile -File ./tools/build.ps1 -BuildDirectory build-core -CoreOnly
# Full static build:
pwsh -NoProfile -File ./tools/build.ps1 -BuildDirectory build-static -Static
# Separate shared libraries:
pwsh -NoProfile -File ./tools/build.ps1 -BuildDirectory build-modular -Modular
```

The script configures, builds Release and runs CTest. It resets variant options
on each invocation; separate directories make comparisons easier. The default
Visual Studio build can also be run directly:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist/sdk
```

| Option | Purpose |
|---|---|
| `BUILD_SHARED_LIBS` | Shared or static core/acquisition libraries |
| `GL_SINGLE_DLL` | Windows/MSVC shared build bundling core, acquisition and runtime resources; requires SDL |
| `GL_BUILD_SDL` | SDL acquisition and isolated sensor reader |
| `GL_BUILD_PANEL` | Static ImGui frontend |
| `GL_BUILD_OVERLAY` | Windows DX12 frontend inside GyroLib; requires panel sources |
| `GL_BUILD_EXAMPLES` | Build/install `gyrolib_demo` |
| `GL_BUILD_TESTS` | Regression/consumer fixtures; not installed |
| `GL_USE_BUNDLED_SDL` | Checked-in Windows x64 SDL package |

Static and modular builds use `GL_SINGLE_DLL=OFF`. Distribute an installation,
not a build directory full of fixtures and intermediates. See [distribution](DISTRIBUTION.md).

## Verify an installation

Build and test `tests/install_consumer` against the installed prefix; it must not
rely on source-tree includes. [Validation](VALIDATION.md#run-the-checks) provides
the commands. Compile the quickstart too after changing its example or exports.
Preserve existing `gyrolib.ini` files when updating an SDK used for play testing.

Documentation is prepared during CMake generation to adjust installed relative
links. Build again after editing guides, then install; `cmake --install` alone
does not regenerate them.

## Dependencies and other platforms

`tools/build_sdl.ps1` reconstructs the vendored SDL binary from a checksum-verified
archive, downloading it only when absent. It applies the retained
[SDL changes](../third_party/SDL/README-GyroLib.md). Ordinary builds use the existing
binary and never download Steamworks.

A prospective Linux build needs a system SDL3 package:

```sh
cmake -S . -B build -DGL_USE_BUNDLED_SDL=OFF -DGL_SINGLE_DLL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Linux loading, Steam Deck, Wine and Proton remain unvalidated. These commands
are a starting point, not a compatibility claim.
