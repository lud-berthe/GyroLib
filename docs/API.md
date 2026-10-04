# Host integration

[Documentation](INDEX.md) / Integration

Start with the [compilable example](QUICKSTART.md). The public headers in
`include/gyrolib` define the functions and structs; this guide explains their
lifetime, threading and data contracts.

- [Integration sequence](#integration-sequence)
- [Ownership](#ownership)
- [Threading and borrowed data](#threading-and-borrowed-data)
- [Units, axes and clocks](#units-axes-and-clocks)
- [Frame loop](#frame-loop)
- [Output and host hooks](#output-and-host-hooks)
- [Errors and diagnostics](#errors-and-diagnostics)
- [Events](#events)

## Integration sequence

1. Create a context with `gl_create(GL_ABI_VERSION)`.
2. Register the game's views, output destinations and implemented capabilities.
   Apply defaults and capture recommendations, then initialize settings.
3. Create acquisition readers and, if needed, a settings frontend.
4. Each frame: acquire inputs, report game observations, update once, apply the
   resulting movement once.
5. Stop new calls and drain callbacks. Destroy frontends and readers before the
   context, then unload the library.

Even a game with one camera must register and report a view. Without a valid
active view, camera/cursor output and input filtering stop. Orientation tracking
and calibration continue. See [view profiles](CONTEXTS.md).

## Ownership

`gyrolib::Context` owns the core handle. Declare `gyrolib::SdlInput` after it so
the reader is destroyed first, including during exception unwinding. Moving a
context transfers its handle; the new owner must outlive its readers. `reset()`
destroys the core early and requires the same shutdown order. `get()` returns a
borrowed handle; do not destroy it separately.

The C equivalents are `gl_create` / `gl_destroy` and `gl_sdl_create` /
`gl_sdl_destroy`. For the SDL reader:

| `borrowed_subsystem` | Ownership |
|---|---|
| 0 | Acquires and releases one SDL gamepad subsystem reference |
| 1 | Borrows a reference the host initialized in the **same SDL instance** |

Neither mode owns the window or calls `SDL_Quit`. Host-opened gamepads retain
their sensor state; the host must enable their sensors. The borrowed subsystem
must outlive the reader. Never exchange SDL handles between separate SDL instances.
[Input acquisition](INPUT.md) covers coexistence and recovery.

The Steam adapter borrows the host's initialized service and callbacks. It does
not initialize Steam, run its frame loop, select action sets or shut it down.
On Windows x64, `gl_steam_runtime_create` / `gl_steam_runtime_poll` /
`gl_steam_runtime_destroy` can borrow the game's loaded public flat API directly.
See [Steam acquisition](INPUT.md#borrowing-the-games-loaded-windows-runtime).

The two UI frontends have different ownership: the static panel borrows the host's
ImGui/rendering setup, while the [DX12 overlay](OVERLAY.md) owns an isolated ImGui
context and GPU resources. Follow the selected frontend's shutdown sequence.

## Threading and borrowed data

Serialize every operation for a context on one owner thread, including getters,
settings and destruction. The core has no worker thread and does not enforce all
thread misuse at runtime. SDL readers also require SDL's main thread. Marshal
input/render observations to the owner when game hooks run on different threads.
Do not initialize or destroy the library from `DllMain`.

Core callbacks run synchronously on the owner thread. They must not block,
throw, reenter, mutate or destroy the context. Keep their code and user data
alive until detached and all calls have finished. The sample observer receives
a borrowed `gl_sample*` before processing; copy it during the callback if needed.

Strings are UTF-8. Returned metadata is borrowed: copy it before changing its
owner. View strings can be invalidated by registration, removal or language
changes; controller strings by endpoint/label changes. `gl_text` and
`gl_choice_description` return static catalog text. The overlay's documented
snapshot/command interface is the exception that permits separate UI threads.

## Units, axes and clocks

| Value | Convention |
|---|---|
| Gyro samples | Degrees/second, right-handed SDL axes: X right, Y up, Z toward player |
| Acceleration | Multiples of gravity (`g`); stationary and flat is approximately (0,+1,0) |
| Camera/cursor output | Degrees; yaw right positive, pitch up positive |
| Stick axes | [-1,+1], X right, Y up |
| Trigger travel | [0,1], released 0 |
| Arrival/control/update time | One caller-owned, nonzero, nondecreasing monotonic nanosecond clock |
| Sensor time | Increasing within the endpoint's sensor epoch; may use a different epoch |
| Sensitivity | Camera degrees per projected controller degree; default X/Y 2.5 |

Normalize provider axes at acquisition and convert to engine units/signs only at
the output boundary. Submit angular velocity, not motion you have integrated
already. Duplicate/out-of-order and nonfinite samples are rejected; long gaps
are not replayed. Zero acceleration means unavailable: local spaces remain usable,
gravity-dependent spaces and calibration need valid acceleration.

Sensor-clock correction uses qualified long-term arrivals, not individual frame
intervals. [Gyro spaces](SPACES.md) and [motion processing](ADVANCED_MOTION.md)
define the projections and filters.

## Frame loop

For a single resolved active view, C++ `gyro.update(now, host, view_id)` reports
that view and returns `gl_output`. Pass 0 if no view is known active. Do not also
submit other view observations in that frame. For overlapping views, report each
with `gl_set_gameplay_context_state`, then call `gl_update` through `gyro.get()`.

With an SDL event loop, pump normally, call `input.poll(now, false)`, report game
state and update. Without one, `input.update(now, host, view_id)` combines pumping,
acquisition and processing on SDL's main thread. The C helper
`gl_sdl_update(reader, now, &host, &output)` combines the same operations after
view observations have been reported. It returns the first error, clears output
on failure and does not dispatch touchpad feedback.

Report real `focused`, `paused`, `menu_open` and `camera_allowed` states. The
reserved `aiming` and `alt_fire` fields are ignored: named views replace them.
Observe resolved aim commands before animation delays. GyroLib never issues an
aim command or modifies the game's action state.

`steam_gyro_output` is a diagnostic observation: 0 unknown, 1 verified disabled,
2 known enabled. It does not change the Steam layout.

## Output and host hooks

Choose one camera output path:

- Apply `gl_output.yaw_degrees` and `pitch_degrees` after update.
- Register `gl_set_camera_callback`, which receives the same movement during update.

Do not use both. These are integrated angular displacements, so do not multiply
by `dt` or reuse output from an earlier frame.

Cursor views never call the camera callback. Map their returned angles to local
UI coordinates and perform hit testing in the host. GyroLib does not move the OS
cursor. Cursor output requires a reported open menu, `GL_HOST_MENU_STATE`, focus,
no pause and a closed library settings panel.

The following features need additional hooks:

| Feature | Host responsibility |
|---|---|
| Stick flick | Suppress native right-stick camera rotation when `suppress_native_right_stick` is true |
| Touchpad flick | Suppress native right-pad camera rotation when `gl_suppress_native_right_touchpad` is true |
| Long-press blocking | Route eligible action events through `gl_filter_event`; never filter aim/Alt-Fire |
| Recenter | Register a callback that centers the game's camera pitch |
| Zoom compensation | Declare support and report actual/reference vertical FOV before each update |
| Menu camera | Opt the specific view into menu output and keep reporting the real menu state |

Declare a capability only after wiring its hook. Suppression applies to camera
rotation from that input, not movement or unrelated actions. Details are in
[advanced motion](ADVANCED_MOTION.md) and [views](CONTEXTS.md).

## Errors and diagnostics

| Result | Meaning |
|---|---|
| `GL_OK` | Success; no controller or zero motion can be a normal result |
| `GL_INVALID` | Invalid argument, value, ID or timestamp; some backends also validate thread use |
| `GL_UNAVAILABLE` | Required capability, backend or data is unavailable |
| `GL_IO_ERROR` | File, runtime or output I/O failed |
| `GL_NEWER_SCHEMA` | The settings format is newer than this library supports |
| `GL_LIMIT` | Resource/allocation limit or bounded operation failed |

Creation functions return NULL on failure. C++ wrappers throw `gyrolib::Error`,
a `std::runtime_error` with `code()` and the operation name. A null creation
handle maps to `GL_UNAVAILABLE`; SDL creation adds `gl_sdl_error(NULL)`'s detail.
Use the C API when the host is built without exceptions. Exceptions do not cross
the C ABI.

A successful SDL poll does not guarantee that its asynchronous reader is healthy.
Inspect changed nonempty `input.error()` / `gl_sdl_error` messages. In bundled
builds, `gl_runtime_prepare` and `gl_runtime_error` expose startup failures before
reader creation. Runtime preparation returns `GL_UNAVAILABLE` in modular/static
builds.

Use `gl_get_diagnostics`, `gl_get_input_metrics` and `gl_poll_event_ex` for logging.
Metrics report acquisition frequency, jitter, clock qualification and sample age,
not input-to-photon latency. Sample/event queues are bounded; rejected and dropped
counts help diagnose gaps. `gl_get_gyro_state.enabled` is a stable permission for
indicators, while `gl_output.gyro_active` reports processed samples and can be zero
between reports. Continue updating during menus and pause.

## Events

`gl_poll_event_ex` returns 1 for an event, 0 for an empty queue or `GL_INVALID` for
a null argument. It carries complete setting IDs in a 128-byte field including
the terminator. The queue retains the latest 128 events without heap allocation.
Refresh the full model if the host cannot drain it regularly.

Legacy `gl_poll_event` drains the same queue, not a separate subscription. If a
setting ID exceeds its 47-character limit, it returns `GL_EVENT_CONTEXT`, detail
6, value 0 and an empty key to request a full refresh. It never truncates a key.
Extended polling uses the same fallback if a future ID exceeds its buffer.

[Menu notifications](MENUS.md#notifications) lists the events relevant to widgets.
A successful `gl_forget_endpoint` emits a removal event even without a preceding
`gl_disconnect_endpoint`; removing an unknown endpoint returns `GL_INVALID`.

---

Previous: [First integration](QUICKSTART.md) · Next: [View profiles](CONTEXTS.md)
