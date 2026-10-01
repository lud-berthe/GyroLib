# Distributing a mod with one GyroLib DLL

The default Windows x64/MSVC shared build enables `GL_SINGLE_DLL`. The player's
binary layout is:

```text
game.exe              existing game
my_mod.dll            the game's hooks and renderer/input integration
gyrolib.dll           core, acquisition, localization and bundled runtime
```

Distribute license notices too, for example in the mod's existing notices folder.
User settings are generated as `girolib.ini` beside `gyrolib.dll` when the mod
calls `gl_initialize_settings(context, NULL, NULL)` after its initial defaults.
Do not overwrite a player's INI in a mod update. `ui.menu_key=F10` can be changed
to F1..F24 or left empty for a native-only menu. See [settings and panel integration](MENUS.md).
The SDK's
headers, `.lib` files, CMake metadata, examples and demo are for developers; they
do not accompany a game's mod. The optional ImGui panel and its renderer backend
are linked statically into `my_mod.dll`, so they add no runtime files.
Microsoft's x64 Visual C++ runtime is a prerequisite of these Release builds.

## Consuming the SDK

For a mod using only the C API (including acquisition), SDL development files are
not required in the consuming project:

```cmake
find_package(GyroLib CONFIG REQUIRED)
add_library(my_mod SHARED my_mod.cpp)
target_link_libraries(my_mod PRIVATE GyroLib::gyrolib)
# Optional F10 panel, compiled into the mod:
target_link_libraries(my_mod PRIVATE GyroLib::gyrolib_panel)
```

Include `gyrolib/gyrolib.h`, `gyrolib/sdl.h` and optionally `gyrolib/runtime.h`.
Initialize on the game's main thread, outside `DllMain`. `gl_sdl_create(context, 0)`
prepares the bundled SDL; use `gl_sdl_pump_events`, `gl_sdl_poll`, then `gl_update`
once per frame with the game's resolved states. The library does not install hooks.
The [API guide](API.md) covers units, callbacks, lifecycle and input filtering.

`gl_runtime_prepare()` permits an explicit startup error check before creating
the reader. `gl_runtime_error()` reports failures such as an unwritable cache.
`gl_runtime_directory()` reports the cache location for diagnostics.
`gl_runtime_prepare_sensor_worker()` can prewarm the reader files during startup
to avoid disk I/O during its first use; it does not start a process.

The existing adapter target names remain available, but clients built against the
old `gyrolib_sdl.dll` or `gyrolib_steam.dll` must be relinked against this SDK.
ABI 1 structs and existing C function signatures have not changed.

## SDL-using hosts and the demo

Hosts that call SDL directly, such as the demo's rendering backend, use:

```cmake
find_package(GyroLib CONFIG REQUIRED COMPONENTS SDL)
target_link_libraries(my_mod PRIVATE GyroLib::gyrolib_sdl)
```

Configure with `CMAKE_PREFIX_PATH` pointing to the installed SDK. The default
Windows SDK includes and discovers the matching SDL development package; no
source checkout or separate `SDL3_DIR` is needed. The target links a small static
MSVC delay-load shim; SDL calls resolve to the **same verified module** as the
adapter. No `SDL3.dll` is installed beside the game. The demo uses this path.
If a host already defines `__pfnDliNotifyHook2`, integrate the equivalent SDL case
from `src/runtime_sdl_loader.cpp` in that hook instead of linking a second hook.
Use `/DELAYLOAD:SDL3.dll`, `delayimp`, the SDL import library, and
`gl_runtime_sdl_handle()` for that case. Call `gl_runtime_prepare()` before SDL
startup so extraction failures can be handled normally instead of as delay-load
exceptions. No preparation occurs in `DllMain`.

A game may already load a different SDL. GyroLib loads its own absolute, verified
path and does not replace or unload the game's SDL. Do not pass SDL pointers,
instance IDs, renderer/gamepad handles or subsystem ownership between instances.
`borrowed_subsystem=1` is appropriate only when the host initialized **this same
bundled instance**. Engine commands still come from the game, not its SDL handles.
Use the modular build when integration specifically requires the game's SDL.

## Internal runtime cache

The DLL contains our marked SDL3 build and sensor worker as Windows resources.
The SDL changes and retained licenses are documented in [THIRD_PARTY.md](THIRD_PARTY.md).
At first acquisition initialization it prepares:

```text
%LOCALAPPDATA%/GyroLib/runtime/<payload SHA-256>/SDL3.dll
```

When a Steam virtual controller requires the isolated reader, it also prepares
`gyrolib_sensor_worker.exe` in that directory. The user does not install or launch
it. Steam-filtered acquisition still uses a separate process; bundling does not
remove that technical requirement. It starts hidden, uses the existing bounded
IPC, and stops when its SDL reader is destroyed. No service, scheduled task,
administrator installation, Steam SDK, download or global input change is added.

Cache files are compared byte-for-byte with the embedded resources before use.
Missing or damaged files are published atomically under a per-version file lock.
Verified files remain open against writes/replacement while the runtime uses them.
New directories are restricted to the current user and SYSTEM, and redirected
directories/reparse points are rejected. Loading uses an absolute path and system
dependency search, never a DLL found via PATH or the working directory. Different
payload hashes coexist, so one game's update cannot overwrite another's reader.
Runtime-cache repair never modifies the game's binaries. Settings initialization
and edits write `girolib.ini` separately, beside the library.

The absolute local path in `GYROLIB_RUNTIME_CACHE` optionally replaces the cache
root for portable hosts and tests. It cannot substitute executable contents.
Changes take effect in a new process. A cache failure leaves the core usable and
returns an acquisition error; there is no fallback to an unverified DLL.
The bundled SDL module stays loaded until process exit; destroy all GyroLib
readers and panels before unloading the mod/library.

Old cache versions are retained to avoid deleting another game's runtime.
The GyroLib runtime cache may be removed when all consuming games are closed;
the next launch recreates the required version. Configuration is stored separately
and is not removed or migrated by runtime-cache maintenance.

## Other builds and validation

`-DGL_SINGLE_DLL=OFF` preserves separate shared libraries and the explicit worker
path. Static builds and non-Windows targets use that modular arrangement too.
The single-DLL loader is Windows-specific; it makes no Linux/Proton compatibility
claim. Existing Linux/core boundaries remain independent of this packaging.

`runtime_three_file_distribution` builds a host and mod, stages exactly the three
files shown above in a path containing spaces and accents, and checks lazy worker
preparation, actual worker handshake/shutdown, damaged-cache recovery, concurrent
startup, rejected cache redirects and graceful failures. The separate desktop
launch test exercises Explorer activation and executable-identity validation.
Another case preloads a host-owned SDL before the mod and checks independent
module paths, unchanged host hints and unchanged host subsystem state.
The settings case calls the public initializer from the mod with a different
working directory and confirms `girolib.ini` is beside `gyrolib.dll`, retains a
disabled shortcut and restores saved values after recreating the context.
The demo and virtual-controller tests use the bundled SDL as well. See
[validation](VALIDATION.md) for recorded automated and hardware checks, and
[known limits](LIMITS.md) before declaring a new host/controller combination supported.
