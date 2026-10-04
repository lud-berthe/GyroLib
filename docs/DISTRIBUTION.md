# Distribution

[Documentation](INDEX.md) / Integration

The default Windows x64/MSVC SDK lets a mod ship this binary layout:

```text
game.exe       existing game
my_mod.dll     game hooks and integration
gyrolib.dll    gyro, acquisition, localization and optional DX12 panel
```

Include the [license notices](THIRD_PARTY.md) with the mod. Headers, import
libraries, CMake files, examples and the demo are development files, not player
requirements. Release builds require Microsoft's x64 Visual C++ runtime.

Calling `gl_initialize_settings(context, NULL, NULL)` creates `gyrolib.ini`
beside the library after host defaults are registered. Do not overwrite that
file in a mod update. [Settings](SETTINGS.md) covers its path, shortcut and errors.

## Link and initialize

Use the installed [CMake targets](BUILDING.md#link-a-mod). Core includes the
acquisition exports in the single-DLL SDK; request `Core Overlay` for the DX12
frontend. The static Panel target is an alternative for hosts owning ImGui.

Initialize outside `DllMain`, on the context/SDL owner thread. The library
installs no game or render hooks. [API](API.md) and [overlay integration](OVERLAY.md)
define the callbacks to connect.

`gl_sdl_create(context, 0)` prepares bundled SDL automatically. Optional runtime APIs:

| Function | Use |
|---|---|
| `gl_runtime_prepare` | Prepare SDL early and handle startup errors explicitly |
| `gl_runtime_error` | Read a preparation failure |
| `gl_runtime_directory` | Get the cache path for diagnostics |
| `gl_runtime_prepare_sensor_worker` | Prepare worker files early; does not start it |

When switching from separate acquisition DLLs to the single-DLL SDK, relink the
mod against that SDK. [Versioning](VERSIONING.md#binary-contracts) defines binary
compatibility.

## Hosts that call SDL

Direct SDL callers, including the demo, link `GyroLib::gyrolib_sdl` from the SDL
component. The SDK includes the matching SDL headers/import library/package and
a static MSVC delay-load shim. Calls resolve to the same verified SDL module as
the adapter, without placing `SDL3.dll` beside the game.

If the host already defines `__pfnDliNotifyHook2`, integrate the SDL case from
`src/runtime_sdl_loader.cpp` into that hook instead of linking another. Use
`/DELAYLOAD:SDL3.dll`, `delayimp`, the SDL import library and
`gl_runtime_sdl_handle()`. Prepare the runtime before SDL startup so extraction
errors are handled as API failures rather than delay-load exceptions.

A game may load a different SDL instance. GyroLib uses its own absolute verified
path and does not replace/unload the game's SDL. Never pass pointers, instance
IDs, handles or subsystem ownership between instances. Borrowing is valid only
when the host initialized this same instance. Use a modular build if integration
requires the game's SDL.

## Runtime cache

SDL and the sensor worker are embedded as Windows resources. First acquisition
initialization prepares SDL at:

```text
%LOCALAPPDATA%/GyroLib/runtime/<payload SHA-256>/SDL3.dll
```

The worker is prepared lazily in that directory when needed. It remains a separate
hidden process for Steam-filtered acquisition; bundling changes installation,
not that requirement. The SDL reader owns its lifetime. No service, scheduled
task, administrator installation, SDK download or system input change is added.
[Input acquisition](INPUT.md#isolated-sdl-reader-under-steam) explains its operation.

Before use, cache files are compared byte-for-byte with embedded resources.
Missing/damaged files are published atomically under a per-version lock. Verified
files stay open against replacement while used. New directories are restricted
to the current user/SYSTEM; reparse points and redirected directories are rejected.
Loading uses an absolute path and system dependency search, never PATH or the
working directory. Different payload hashes coexist.

`GYROLIB_RUNTIME_CACHE` can specify another absolute local cache root for portable
hosts/tests; it cannot substitute executable contents. Changes take effect in a
new process. A cache failure returns an acquisition error and leaves the core
usable; there is no fallback to an unverified DLL.

The bundled SDL module remains loaded until process exit. Destroy readers,
frontends and contexts before unloading GyroLib; never free borrowed runtime
handles. Cache repair does not modify game binaries or settings. Old cache
versions are retained; they may be deleted with all consuming games closed and
will be recreated as needed.

## Other builds

`GL_SINGLE_DLL=OFF` uses separate libraries and an explicit sensor-worker file.
Static and non-Windows builds also bypass the Windows resource loader. Modular
mods whose worker is not beside the host executable must set its absolute path.
See [build variants](BUILDING.md#build-variants) and [reader lifetime](INPUT.md#protocol-and-lifetime).

The three-file distribution test covers a host/mod/library layout in a path with
spaces and accents, cache repair, concurrent startup, worker handshake/shutdown,
separate host SDL and settings beside the DLL. [Validation](VALIDATION.md) records
results; [limits](LIMITS.md) identifies untested platforms and environments.

---

Previous: [Menus](MENUS.md)
