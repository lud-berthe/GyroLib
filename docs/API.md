# API and host integration

Start with the complete, compilable [README example](../README.md#first-integration).
The public headers in `include/gyrolib` are the function/struct reference. This
page defines the shared contracts; feature behavior belongs in the linked guides.

## Lifecycle and ownership

1. Create a context. Register the game's views, their output destinations and any
   host defaults before loading settings. IDs are stable nonzero integers owned
   by the host; a camera also needs an explicit view.
2. Initialize settings if persistence is wanted. A context without initialization
   uses in-memory defaults. Create acquisition readers after the context.
3. Each frame, acquire input, report the actual game state and active views,
   then update once and consume that update's movement once.
4. Stop producers/hooks and detach callbacks, destroy panels/readers, then destroy
   the context. Unload the library only after every call and callback has stopped.

`gyrolib::Context` owns the core handle. `gyrolib::SdlInput` owns an SDL reader and
must be destroyed before its context. Declaring them in that order gives correct
stack unwinding on failure. Moving `Context` preserves its native handle, but the
new owner must still outlive every reader. `reset()` destroys the core early and
requires the same shutdown order. `get()` returns a borrowed native handle for
advanced C APIs; never destroy that handle separately. The core-only wrapper
has no SDL link dependency even though both classes share one header.

For C, `gl_create(GL_ABI_VERSION)` / `gl_destroy` and `gl_sdl_create` /
`gl_sdl_destroy` express the same ownership. `gl_sdl_create(context, 0)` acquires
one balanced SDL gamepad subsystem reference; `1` borrows an already initialized
subsystem of the **same SDL instance**. Neither case calls `SDL_Quit` or owns your
window. Existing host gamepad handles are observed without changing their sensor
state. A borrowed host subsystem must outlive its reader.

The Steam adapter borrows the host's initialized public service and callbacks.
It never initializes Steam, runs its frame loop, selects action sets or shuts it
down. Call `gl_steam_poll` once per host Steam frame with a monotonically advancing
frame serial. SDL and Steam can coexist; the core selects one qualified motion
stream. Physical identity and Steam layout handling are specified in [INPUT.md](INPUT.md).

The panel owns neither ImGui nor the renderer/window. Its context, ImGui context,
backends and callback data must outlive it. [MENUS.md](MENUS.md) is the source for
panel and native-menu integration, including shortcut/event routing.

## One update, one movement

With a resolved single active view, C++ `gyro.update(now, host, view_id)` reports
that view and returns `gl_output`. Pass `0` when no view is known active. Do not
also submit other view observations in the same frame. For overlapping views,
use `gl_set_gameplay_context_state` for each actual observation and `gl_update`
through `gyro.get()`; the highest priority wins, with lowest ID breaking ties.
Missing observations become unavailable at each successful update. No active
available view means zero output. See [CONTEXTS.md](CONTEXTS.md) for selection,
independent profiles and migration.

For an existing SDL loop, pump events normally, call `input.poll(now, false)`,
report game state and call `gyro.update`. This avoids pumping twice. A host without
an SDL loop can use `input.update(now, host, view_id)` to combine pumping,
acquisition and update; this still requires the SDL main thread. The C equivalent
is `gl_sdl_update(reader, now, &host, &output)` after reporting view observations.
It returns the first pump/poll/update error and clears output on failure. It does
not dispatch feedback. The separate C pump/poll/update calls remain available
for hosts coordinating Steam, input hooks or their own event loop.

`host.focused`, `paused`, `menu_open` and `camera_allowed` must reflect the game.
The reserved `aiming` and `alt_fire` fields are ignored. Observe resolved game
commands before animation delays; GyroLib never triggers aiming or other game
actions. `steam_gyro_output` is 0 when unknown, 1 only when verified disabled,
2 when known enabled. It is a diagnostic observation, not a layout switch.

Choose one camera output path:

- Consume `gl_output.yaw_degrees` / `pitch_degrees` after update, as in the README.
- Install `gl_set_camera_callback`; it receives the same movement synchronously
  during update. The returned angles are then diagnostic, not another movement
  to apply.

The angles are **already integrated displacements**, not velocities. Do not
multiply by frame duration or reuse the previous frame's output. Cursor views
never call the camera callback: map their returned angular deltas into your local
UI coordinates. No operating-system cursor movement is generated.

A cursor view requires observed open game menus, `GL_HOST_MENU_STATE`, focus,
no pause and a closed GyroLib panel. Flick is disabled in cursor views. For a
camera view, `suppress_native_right_stick` and
`gl_suppress_native_right_touchpad` gate only their respective native camera
rotations, never character movement or unrelated controls. Declare each suppression
capability only after implementing that gate. Short-press filtering likewise
requires a separate host action-event hook; never connect aim/Alt-Fire to it.
See [advanced motion](ADVANCED_MOTION.md), [menus](MENUS.md) and [input](INPUT.md).

## Threads, callback data and errors

Serialize **every** operation for a context on its owner thread, including reads,
settings and destruction. The core has no worker thread. SDL readers require
SDL's main thread; Steam and ImGui additionally follow their host's thread rules.
If game camera/input/render hooks differ, use the game's own queue to marshal
observations and output. Do not call initialization or destruction from `DllMain`.

Callbacks run synchronously on the owner thread. They must not block, throw,
reenter, mutate or destroy the context. Keep callback code and user data alive
until detached and all calls finish. The accepted-sample observer borrows its
`gl_sample*` only during the call and runs before processing; copy values you need
later. Input strings are UTF-8 and copied where documented. Returned strings and
metadata are borrowed: copy them before changing their owner. `gl_text` and
`gl_choice_description` return static catalog text; view metadata can change on
registration, language changes or removal.

| Result | Meaning |
|---|---|
| `GL_OK` | Operation succeeded. Zero movement/no controller is also a normal result. |
| `GL_INVALID` | Invalid pointer, ID, value, thread or timestamp for that operation. |
| `GL_UNAVAILABLE` | Capability, backend or requested data is unavailable. |
| `GL_IO_ERROR` | File/runtime/output I/O failed. |
| `GL_NEWER_SCHEMA` | Settings belong to a newer schema; do not overwrite them. |
| `GL_LIMIT` | Resource/allocation limit or bounded operation failed. |

Creation functions return `NULL` on failure. C++ operations throw `gyrolib::Error`,
a `std::runtime_error` carrying `code()` and the operation name. Creation maps a
null handle to `GL_UNAVAILABLE` because those C functions do not expose a result
code; SDL creation also includes the last creation diagnostic from
`gl_sdl_error(NULL)`. Exceptions are local C++ behavior, never passed through the C ABI. Hosts
built without exceptions should use the C API and check every result.

A successful SDL poll does not guarantee a healthy asynchronous helper. Inspect
`input.error()` / `gl_sdl_error` and report changed nonempty messages; automatic
fallback may still be working. For bundled-runtime startup diagnostics, call
`gl_runtime_prepare` before creating the reader and inspect `gl_runtime_error`
on failure. `GL_UNAVAILABLE` is expected for that call in modular/static builds.
Diagnostics (`gl_get_diagnostics`, `gl_get_input_metrics`) and `gl_poll_event`
allow host-owned logging without imposing a console. Sample queues and event
storage are bounded; inspect rejected/dropped counts when diagnosing gaps.

## Units, axes and time

| Value | Contract |
|---|---|
| Gyro samples | Degrees/second; right-handed SDL axes: X right, Y up, Z toward player |
| Acceleration | Multiples of standard gravity (`g`); flat stationary approximately (0,+1,0) |
| Camera/cursor output | Degrees; yaw right positive, pitch up positive |
| Stick axes | [-1,+1], X right / Y up; adapters normalize SDL Y |
| Trigger travel | [0,1], released 0 |
| Arrival, controls and update time | One caller-owned monotonic nanosecond clock, nonzero and nondecreasing |
| Sensor time | Increasing within that endpoint's sensor epoch; it need not share the caller's epoch |
| Sensitivity | Camera degrees per controller degree; X/Y independent, default 2.5 |

Adapters normalize input; convert engine units and signs only at your boundary.
Do not integrate sensor motion yourself before submission. Duplicate/out-of-order
samples and nonfinite values are rejected. Long gaps are not replayed as camera
jumps. Zero acceleration means unavailable: local gyro spaces keep working,
gravity-dependent spaces and calibration require usable acceleration.

Sensor clock correction uses qualified long-term arrival history, not individual
frame intervals. `gl_get_sensor_clock_scale` and input metrics describe acquisition;
they do not measure input-to-photon latency. `gl_get_gyro_state.enabled` is a stable
permission for indicators/policy, whereas frozen `gl_output.gyro_active` reports
processed samples and can be zero between reports. Keep updating in menus/pause
so freshness, calibration and safety gates stay current. [SPACES.md](SPACES.md)
and [ADVANCED_MOTION.md](ADVANCED_MOTION.md) define motion behavior.

## Settings and optional runtime

`gl_initialize_settings` loads/creates `girolib.ini` beside the module containing
GyroLib, independent of the working directory/cache. With static linkage that is
the executable/mod. An optional existing UTF-8 directory chooses another location;
an optional legacy file imports only if the destination is absent. Register all
views and defaults first. Parent directories are not created implicitly.
The spelling `girolib.ini` is retained for existing installations and is exposed
as `GL_SETTINGS_FILENAME`; do not substitute `gyrolib.ini` when locating the file.

Invalid/newer files and failed initialization disable autosaving. Report the
error, repair the file/location, and retry initialization. Do not bypass this
protection with `gl_set_settings_path`. After successful initialization, menu
edits save automatically. `gl_get_settings_path` gives the active location.
The settings schema is independent of the binary ABI; migration details belong
in [CONTEXTS.md](CONTEXTS.md), shortcut integration in [MENUS.md](MENUS.md).

The Windows single-DLL runtime prepares embedded SDL and an optional isolated
reader in a verified per-user cache. `gl_sdl_create` prepares SDL automatically;
worker preparation is lazy. There is no runtime shutdown function: destroy the
readers, panels and context before unloading GyroLib. Borrowed runtime module
handles must not be freed, and SDL handles must never cross different SDL
instances. [DISTRIBUTION.md](DISTRIBUTION.md) owns packaging and cache details.

## Version 0.2 migration

The C ABI remains **1**: existing struct layouts, calling convention (`cdecl` on
Windows), enum values and core/acquisition exports are retained. New
`gl_sdl_update` is additive and requires a 0.2 or newer library. Handles must be
created and destroyed by the same loaded library; no C++ allocator or container
ownership crosses it.

The header-only C++ API deliberately changes: replace
`auto gyro = gyrolib::create()` with `gyrolib::Context gyro;`.
`Context` is now an owning class, not a `unique_ptr` alias; `get()`, `reset()`, move
and boolean tests remain available. Direct `Deleter`, raw-pointer construction,
`release()` and custom unique-pointer operations should use the C API where
needed. The repository's consumers have been updated. New lifecycle/view/update
methods check errors and make manual destruction unnecessary.

The static panel's redundant `gl_panel_f10(panel, pressed, repeat)` is removed;
replace it with `gl_panel_function_key(panel, 10, pressed, repeat)`. Existing
remapping/disabled-shortcut behavior is preserved. No public DLL export is removed.
