# Input acquisition

[Documentation](INDEX.md) / Reference

SDL is the preferred motion source. Steam Input is a fallback when the selected
controller has no usable SDL motion. The core selects one motion stream; it never
adds SDL and Steam rotations from the same controller.

- [Choose the readers](#choose-the-readers)
- [Endpoints and physical identity](#endpoints-and-physical-identity)
- [Controller selection and source health](#controller-selection-and-source-health)
- [Physical controls and capabilities](#physical-controls-and-capabilities)
- [SDL ownership and event handling](#sdl-ownership-and-event-handling)
- [Steam Input provider](#steam-input-provider)
- [Isolated SDL reader under Steam](#isolated-sdl-reader-under-steam)
- [Flick reports](#flick-reports)
- [Touchpad feedback](#touchpad-feedback)
- [Button and trigger names](#button-and-trigger-names)
- [Avoiding doubled movement](#avoiding-doubled-movement)
- [Source references](#source-references)

## Choose the readers

| Reader | Host requirement |
|---|---|
| SDL (`gl_sdl_create`) | Pump SDL events on the owner/main thread; the reader handles discovery and sensors |
| Loaded Steam runtime (`gl_steam_runtime_create`) | Windows x64 host with an already initialized public Steam Input service |
| Steam callbacks (`gl_steam_create`) | Supply read callbacks from the host's public Steam Input integration |
| Raw endpoints | Normalize identity, capabilities, units and timestamps before submitting data |

Poll SDL, poll an optional Steam reader, then call `gl_update` once. Use at most
one SDL reader and one Steam reader per context. The loaded-runtime and callback
Steam readers are alternatives.

A non-Steam shortcut can expose a sensorless virtual controller. The
[isolated SDL reader](#isolated-sdl-reader-under-steam) may recover its physical
sensor; that path is distinct from the public Steam Input fallback.

## Endpoints and physical identity

An endpoint is one provider's representation of a controller. Endpoint IDs are
unique and nonzero across providers. Endpoints share a physical ID only after
identity is established; names and player indexes alone are not evidence.

SDL uses `SDL_GetGamepadSteamHandle` when available. Otherwise it derives identity
from vendor/product and serial, or the SDL path if no serial exists. Different
serials/paths keep identical controllers separate. Steam-handle metadata is
refreshed every poll, so a late handle can associate an opened pad without losing
selection. Loss of an adapter-owned handle restores its SDL identity; an explicit
host pairing is not overwritten.

For virtual/physical pairs without that handle, the host may supply a trusted
mapping through `gl_associate_endpoint`. One valid mapping uses a verified XInput
endpoint plus Steam's forward and reverse slot-to-handle lookup. An ambiguous
pair must remain separate. Port/path identity without a serial cannot distinguish
an identical replacement at the same port. Re-establish explicit associations
after endpoint removal/recreation.

One SDL adapter and one Steam adapter may own endpoints in a context. Raw providers
can add endpoints with noncolliding IDs, up to 32 simultaneously registered
endpoints. Adapters forget retired endpoints.

## Controller selection and source health

The first connected ordinary controller is selected automatically. It stays
selected while any ordinary endpoint in its physical group is connected. After
the last disconnects, `gl_update` selects another connected controller or 0 if none
remain. Motion companions cannot become the command controller or keep a removed
one selected. `gl_select_device` overrides selection; passing 0 requests a new
first-device selection. A sensorless controller can still supply game controls.

| Condition | Policy |
|---|---|
| SDL qualification | Gyro capability, plausible finite samples and at least two increasing sensor timestamps; arrivals at most 100 ms apart |
| Initial selection | Qualified SDL wins immediately; Steam waits 250 ms after device selection |
| Stale stream | No usable motion after 150 ms; stale samples cannot move the camera |
| Steam fallback | Qualified endpoint in the selected physical group only |
| Return from healthy Steam to SDL | SDL healthy for 1 s and current source retained for at least 2 s |
| Current source fails | Switch as soon as a qualified alternative exists |
| Transition | Discard alternate backlog, restart timing/flick, retain per-endpoint calibration ownership |

Gravity-dependent spaces also need fresh acceleration. Alternate motion samples
qualify source health but are not applied or replayed. Paired endpoints can
supplement controls; control states expire after 150 ms. A data stall alone does
not change the selected controller.

## Physical controls and capabilities

`gl_set_endpoint_control_authority` declares which families a provider describes
physically. After association, that authority overrides virtual representations,
including known absence: an emulated Xbox right stick does not create a physical
right stick. Stale authoritative controls do not fall back to differently mapped
virtual buttons/axes. Providers can leave unknown families supplemental.

Capabilities come from APIs and verified driver metadata. Separate analog axes
or physical-origin metadata establish sticks; contacts require actual touch data,
not clicks, pressure or nonzero coordinates. A central touchpad is one surface;
separate left/right pads can provide bilateral activators and right-pad flick.
The selected command source still belongs to the game.

Activator families combine with OR. Buttons use SDL ordinals 0..31, selected by
setting values 1..32. Menus offer individual buttons; legacy combinations 33..40
remain readable. Bilateral families expose Left, Right, Left or Right and Left
and Right when available. A single touchpad exposes Off/On. Analog deflection is
separate from stick click. Triggers use normalized travel and their own threshold.

The direct reader and worker share `src/detail/sdl_controls.hpp`. Bundled SDL
backports legacy Steam touchpad support and adds driver metadata for origins,
contact aliases and labels. Known Steam Controller 2026 capacitive button mappings
are accepted only after checking numeric VID/PID (28de:1302/1304/1305), physical
mapping and button availability. Unknown/remapped devices do not gain invented
contacts. The core/menu has no commercial-name capability table.

`gl_set_button_contact` lets other providers declare verified aliases. The menu
hides duplicates, while effective settings resolve them to the dedicated family.
The saved binding and inheritance remain intact for controllers where that ordinal
is a physical button. Physical clicks remain distinct from contact. Missing
provider metadata is a limitation, not something a display name can repair.

## SDL ownership and event handling

The reader observes `SDL_AddEventWatch`, copies sensor packets under a mutex and
drains them on the main/owner thread. The callback never opens a controller.
Add/remove/remap events request discovery; a 500 ms scan covers missing events.

The adapter does not consume the host's event queue or call PollEvent, PeepEvents,
FlushEvents, SetEventFilter, global hints, player-index setters, rumble or LED APIs.
The host pumps events. Filtered/disabled sensor events cannot qualify SDL motion;
repeated reads of cached sensor data do not prove fresh packets.

`gl_sdl_create(context, 0)` acquires one balanced gamepad subsystem reference;
`1` borrows an initialized reference from the same SDL instance. Neither calls
`SDL_Quit`. Sensors are enabled for newly opened gamepads. For an already-open
host handle, the host owns sensor enablement and recovery. Reader destruction
closes only its references and does not explicitly disable sensors a later reader
may use. Final SDL close releases the device.

SDL can configure hardware while opening/enabling sensors, including mode or light
changes. Coexistence with another SDL/HID stack needs a real device/game check.
If another host reader later acquires a GyroLib-opened handle, coordinate ownership.

### Optional SDL window input

Unreleased: attach the demo/host window once to enable the per-view
[Steam mouse setting](OVERLAY.md#steam-input-mouse-movement):

```cpp
int result = gl_sdl_attach_window(reader, sdl_window);
```

The supplied Windows SDL runtime exposes a per-window input extension. GyroLib
handles identification, filtering, conversion and cursor confinement inside the
DLL. The host continues consuming its usual camera/cursor output and SDL events.
Neither its event filter nor its Windows message hook is replaced. SDL owns raw
mouse registration while the window is attached, including outside relative mode
for cursor views. A separate Raw Input owner in the same process needs coordination.

Attach/detach runs on the context-owner/SDL main thread. Raw mouse callbacks run
on SDL's raw-input thread under a window-property lock; detachment waits for them.
Movement queues use a separate mutex and are consumed by `gl_update` on the owner
thread. Buttons and wheel events pass through. Call `gl_sdl_attach_window(reader,
nullptr)` to detach, or destroy the reader. Destroying the window releases its
binding automatically. The context must outlive the reader.

One bridge may own a context or window, including the DX12 bridge. The call returns
`GL_UNAVAILABLE` for an occupied context/window, an external SDL without the
extension, a non-Windows runtime or SDL's GameInput mouse backend. Ordinary
controller acquisition remains usable when attachment fails. A non-Steam shortcut
does not require a Steamworks SDK for this path.

### Sensor recovery

Direct and isolated readers share a watchdog. After 750 ms without fresh gyro
reports, reader-owned sensors are disabled/re-enabled through SDL, at most once
per two seconds. Duplicate timestamps do not keep the watchdog alive. Endpoint
identity, selection and event delivery are retained; stale samples remain rejected.
Pre-existing host-opened handles are excluded, even if their sensors are enabled.

When ImGui uses the selected reader-owned gamepad, use manual gamepad mode and
clear its borrowed list before polling. Restore the current handle before
NewFrame. This prevents ImGui opening a late-connected device first with sensors
disabled; see [panel integration](MENUS.md#static-imgui-panel-integration).

## Steam Input provider

`gl_steam_provider` borrows the host's public read callbacks. The host initializes
and updates its own Steamworks service. Call `gl_steam_poll` once per frame with a
strictly increasing serial; duplicate polls are rejected. The adapter does not
initialize/shut down Steam, call RunFrame, change action sets or modify layouts.

Manual calibration and local automatic bias correction are disabled for Steam
motion. Orientation fusion still runs with zero local bias. Optional control
callbacks must report actual action origins; pressure/clicks cannot stand in for
touch contacts.

[The typed bridge](../examples/steamworks_bridge.hpp) requires the host's licensed
SDK and has not been compiled against Steamworks here. The C callback adapter is
tested with a fake provider. Valve's [GetMotionData](https://partner.steamgames.com/doc/api/ISteamInput#GetMotionData)
uses signed-short-scaled float values without a hardware timestamp. Steam freshness
therefore relies on connected handles and frame availability; an internally frozen
nonzero sample cannot always be distinguished from a stationary controller.

### Borrowing the game's loaded Windows runtime

`gl_steam_runtime_create` provides that callback adapter without requiring the mod
to build against a Steamworks SDK. On Windows x64, it resolves the public flat
functions and matching versioned accessor in the already loaded `steam_api64.dll`.
It does not load a Valve DLL, call private interfaces, or initialize/update/shut
down Steam Input. The host must already own and update that service.

Poll SDL first, call `gl_steam_runtime_poll(reader, now, frame_serial)`, then update
the core once. Keep both the timestamp and frame serial strictly increasing.
Destroy the runtime reader before its context and before the host shuts Steam
down. Do not attach another `gl_steam` provider to the same context.

Missing exports/session return `GL_UNAVAILABLE` and retry without interrupting
SDL. `gl_steam_runtime_error` supplies diagnostic text. A successful poll can have
zero connected controllers or no usable motion; only qualified samples establish
source health. Session loss retires this reader's endpoints. SDL's Steam-handle
metadata supplies the common physical identity; an unrelated Steam pad cannot
replace the selected controller's motion.

This reader supplies motion only. It does not invent physical contacts from
Steam's virtual buttons. SDL or host callbacks still supply controls and their
verified origins. The Windows bridge has simulated-DLL and real-controller checks; see
[Steam Input validation](VALIDATION.md#steam-input-validation) for the tested setup.

An exported interface alone does not prove that the host has initialized Steam
Input. Empty enumeration must not silently trigger initialization. The library
always borrows the service; initialization remains a host decision. Non-Steam
shortcut tests below exercise isolated SDL, not the public Steam bridge.

## Isolated SDL reader under Steam

Steam may hide physical devices from the game process while exposing a sensorless
virtual gamepad. A Steam handle on such a pad starts the optional sensor worker.
The worker uses ordinary SDL discovery, capability queries and sensor events;
controller/transport coverage still depends on SDL.

Only the worker's environment/discovery hints change. An ordinary child receives
an environment copy without inherited Steam session/virtual-pad IDs or gamepad
exclusion filters. On Windows, when Steam's module intercepts child launches,
the adapter uses the desktop shell's `IShellDispatch2::ShellExecute` to start the
reader independently and hidden. A local random named pipe restricted to the
user/SYSTEM connects it; the server verifies the executable path before accepting
samples or controlling shutdown. This requires an available desktop shell.

The game keeps its environment, event queue, Steam service and ordinary virtual
gamepad commands. The helper returns normalized motion, physical activators,
capabilities, origins and labels for GyroLib. It injects no game/system inputs and
requires no Steam SDK, fake AppID, service or administrator installation.

### Pairing a motion companion

Worker endpoints are motion companions. Automatic session pairing requires:

- Matching nonzero SDL vendor IDs.
- Exactly one connected ordinary controller group and one companion of that vendor.
- At least 300 ms of qualified companion motion; silent candidates still count
  against uniqueness.

Product IDs may differ for receivers; names are not used. This is bounded session
evidence, not universal physical identity. Missing metadata or multiple candidates
require explicit `gl_bind_motion_sensor`. Menus use
`gl_motion_sensor_needs_selection` / `gl_get_motion_sensor` rather than guessing.

One companion binds to each controller. An established pair survives a temporary
stall or a new arrival. Removal clears the binding; reconnect/restart requires
qualification again. Bindings are not saved as permanent identity because an
opaque virtual handle cannot always be traced to a physical serial/path.

A companion exposing physical buttons owns GyroLib's button activators, preserving
physical labels even when Steam remaps commands. Authority remains during stalls,
with pressed state expiring after 150 ms. Motion-only companions leave controls
supplemental. Physical stick/pad coordinates prevent double use of Steam's virtual
pad-to-stick mapping.

### Protocol and lifetime

The same-build IPC protocol is version 7. Fixed-size records and pipes are bounded;
the owner thread reads without blocking and accepts fragmented records. Unknown
versions are rejected. Deploy adapter and worker together in modular builds.

Sensor timestamps are retained and an OS monotonic clock measures transport age.
Records older than 100 ms are discarded before core source qualification. Changed
controls are sent immediately and unchanged ones refreshed every 50 ms while
motion is arriving. No motion for 100 ms stops control refresh too, so a sleeping
device cannot keep a cached contact held. Labels/capabilities refresh on remap
and periodically. Windows reads pipe handles directly to preserve fragments on
`ERROR_NO_DATA`; other platforms use SDL process streams.

Startup allows ten seconds before the first protocol message, then at most two
seconds of stream silence. Errors distinguish startup timeout, stream timeout and
process exit. Exits/timeouts remove worker endpoints; restart attempts are spaced
by five seconds. Destruction requests a graceful stop, then may terminate only its
own verified unresponsive worker. The worker exits on pipe closure too.

Bundled Windows builds prepare the worker in the runtime cache automatically.
For modular/static builds its default location is beside the host executable;
`gl_sdl_set_sensor_worker(reader, absolute_utf8_path)` selects another location,
or an empty path disables it. PATH is never searched. `gl_sdl_error` reports
worker failures; direct SDL and a supplied Steam provider can still be usable.
[Distribution](DISTRIBUTION.md) covers the cache and binaries.

## Flick reports

Stick and touchpad providers are resolved independently inside the selected
physical group, using authority and reports younger than 150 ms. A stale preferred
provider cannot hide a fresh eligible one; authoritative absence/staleness still
blocks a remapped virtual substitute. Histories and clocks stay separate, and
duplicate representations of a family are not added.

The consumed-report timestamp is distinct from animation time. Fresh reports
newer than the last consumed report are processed once even if they precede the
last rendered frame. Batches use their own intervals for circular smoothing;
expired/duplicate reports and elapsed animation time are not replayed. A provider
change resets its flick history.

## Touchpad feedback

Acquisition alone does not request haptics. Explicit `gl_sdl_apply_feedback` calls
can send bounded, timestamped right-pad pulses, including through the worker.
The current backend supports Steam Controller 2026 USB/puck only and does not
alter motor rumble or persistent settings. BLE/other devices are unsupported by
that output backend. Physical feel and coexistence with native/Steam feedback
remain unverified; see [limits](LIMITS.md).

## Button and trigger names

Stable button ordinals keep their binding meaning. SDL provides face labels via
`SDL_GetGamepadButtonLabel`; device metadata supplies known shoulder/system names.
Exact extra-button names require verified variant metadata. Unknown buttons use
positional/extra-button labels. Names never create capabilities or select a source.
The untranslated Steam Controller display token is normalized cosmetically only.

`gl_set_button_label(context, endpoint, ordinal, name, provenance)` copies UTF-8;
NULL clears it. `GL_LABEL_PHYSICAL` is for a verified origin and wins over
`GL_LABEL_DEVICE`. Within a proven physical group, equal provenance prefers SDL,
then the lowest endpoint ID. Disconnected/unrelated devices cannot supply labels.
Trigger names use `gl_set_trigger_label` with left/right side and up to 127 bytes.

`gl_steam_set_button_label_provider` adds a borrowed callback without changing the
provider struct. The bridge uses public action-origin APIs for normalized buttons;
the host supplies explicit origins for extra/remapped controls. It creates no
action sets. Physical identity must be established independently.

Menus obtain labels through `gl_choice_at`; direct getters are also available.
Returned pointers are borrowed: copy before label/endpoint updates, language
changes, pair-label re-query or destruction. `GL_EVENT_BUTTON_LABELS` signals
changes; [menu notifications](MENUS.md#notifications) defines its details.

## Avoiding doubled movement

Disable both gyro-to-mouse and gyro-to-stick in the Steam layout. A known enabled
mapping can be reported with `host.steam_gyro_output=2`. Public motion data cannot
inspect every community/legacy layout, desktop mapping or external remapper;
unknown is not proof of disabled output.

Touchpad-to-mouse mappings also need attention when using pad flick: their mouse
events cannot reliably be distinguished from a real mouse. GyroLib does not edit
Steam profiles or globally suppress mouse/stick inputs.

## Source references

- [SDL sensor API](https://wiki.libsdl.org/SDL3/SDL_GetGamepadSensorData)
- [SDL 3.4.16 gamepad mappings](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/SDL_gamepad.c)
  and [Triton driver](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/hidapi/SDL_hidapi_steam_triton.c)
- [Steam action origins](https://partner.steamgames.com/doc/api/ISteamInput#GetActionOriginFromXboxOrigin)
- [Desktop-shell launch guidance](https://devblogs.microsoft.com/oldnewthing/20131118-00/?p=2643)

Local modifications and retained notices are in [third-party provenance](THIRD_PARTY.md).
