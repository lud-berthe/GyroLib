# Input acquisition and source policy

## Identity and controller selection

The core tracks endpoints separately from physical devices. Endpoint IDs must be
unique and nonzero, including across sources; ordinary physical IDs are equal
only when identity is proven. Motion companions can additionally use the bounded
session pairing policy below. SDL auto-associates using `SDL_GetGamepadSteamHandle` when
available. Without that handle it uses a vendor/product + serial identity, falling
back to the SDL device path. Names do not decide capabilities or association.
Two identical controllers remain separate when their serials/paths differ.
The SDL Steam handle is refreshed every poll: late metadata can associate a
previously opened pad and preserve its selection. Loss of an adapter-owned handle
restores its SDL identity. A different explicit host pairing is not overwritten.
The untranslated `#SettingsController_SteamController` display token is normalized
to `Steam Controller`; that cosmetic change is never used to infer capabilities.

The first connected ordinary controller is selected automatically and stays
selected while any ordinary endpoint in its physical group remains connected.
After the last one disconnects, the next connected controller takes over on
`gl_update`; with none present, selection becomes zero until one arrives. Motion
companions cannot keep a disconnected game controller selected or become the
player controller. Orphaned companion bindings are released for fresh pairing.
Use `gl_select_device` (also exposed by the panel) to change controllers explicitly.
Calling it with zero requests a new first-device selection. A device with no gyro
may be selected: it does not become a usable motion source by being connected.

If SDL exposes a physical and a virtual device without an exact Steam handle,
automatic universal matching is not possible. The host may establish a trusted
mapping, then call `gl_associate_endpoint`. A valid alternative is a **verified
XInput endpoint** plus Steam's forward AND reverse slot-to-handle mapping. A
player index alone, a familiar product name, or merely one connected controller
is insufficient evidence. Never merge ambiguous devices. Associations survive
normal polling; re-establish explicit associations after endpoints are removed
and recreated. Port/path identity without a serial cannot prove that a different
identical controller has not replaced the original device at that port.

Only one SDL adapter and one Steam adapter may own endpoints in a given context.
Raw providers can add endpoints through the same C API, using noncolliding IDs.
There are at most 32 simultaneous registered endpoints; retired adapter endpoints
are forgotten. A temporary motion-data stall alone never changes controller selection.

## Selection policy

| Rule | Implementation |
|---|---|
| SDL qualification | Gyro capability, finite plausible data and at least two increasing sensor timestamps; successive arrivals at most 100 ms apart. Gravity spaces additionally require fresh acceleration. |
| Initial preference | SDL wins immediately after qualification; Steam waits 250 ms after selecting the device |
| No data / sleep | Stream stale at 150 ms; stale samples never move camera |
| Steam fallback | Only a qualified Steam endpoint belonging to the selected physical device |
| Recovery | Healthy SDL for at least 1 s and current source retained at least 2 s before switching back from healthy Steam |
| Current source dies | Fail over as soon as a qualified alternative exists; don't wait for recovery hysteresis |
| Transition | Clear alternate backlog, restart dt/flick state, retain separate per-endpoint calibration ownership |

Every frame uses exactly one motion stream. Alternate-source samples contribute
to health qualification, then are discarded. They are never added to camera
rotation or replayed later. Controls/contacts may supplement from proven paired
endpoints; SDL analog sticks take priority. All controls expire after 150 ms.

## SDL coexistence

### Physical controls versus virtual game output

The processing/menu model consumes input families, not controller model names.
`gl_set_endpoint_control_authority` lets a provider declare which families its
capabilities and states describe physically. A known-zero family excludes virtual
controls too: a pad emulating an Xbox right stick does not gain a physical right
stick. Authority applies only after physical identity or a companion association
has been established. Freshness still gates pressed states; losing reports never
falls back to a differently mapped virtual button or a nonexistent physical stick.
Providers can declare only the families they know, leaving others supplemental.
Game command events remain on the host's chosen input source.

The bundled SDL dependency backports SDL's own legacy Steam touchpad support.
Small driver-side properties describe physical axis origins, contact aliases and
names; both direct and isolated acquisition read them through the same generic
adapter. The core and menus have no controller-specific capability tables.
Mapped axes alone do not prove stick origin: for unknown mappings the adapter
requires distinct raw analog axes, and uses explicit driver metadata when logical
axes represent a touchpad. A central touchpad remains a single Off/On surface;
separate left/right pads expose bilateral activators and right-pad flick.
No touch state is inferred from clicks or nonzero pad coordinates.

Some devices/drivers do not expose all hardware capabilities. Missing metadata
cannot be reconstructed universally from an Xbox-compatible name. Such features
remain unavailable until a provider actually supplies them; see LIMITS.md.

The adapter uses SDL3, without commercial-name allowlists or Sony exclusions.
It observes sensor events through `SDL_AddEventWatch`, copies them under a mutex,
and processes them on the main/owner thread. It never calls PollEvent, PeepEvents,
FlushEvents, SetEventFilter, global hints, player-index setters, rumble or LED APIs.
The host owns event pumping and ordinary event delivery. Disabled or host-filtered
sensor events cannot be observed; in that case SDL cannot qualify and fallback
may be used. Reading the same cached `SDL_GetGamepadSensorData` value repeatedly
is deliberately not evidence of incoming sensor packets.

Device add/remove/remap events request discovery on the next owner-thread poll;
there is also a 500 ms periodic scan if those events are unavailable. The event
watch only marks discovery pending and copies sensor packets; it never opens a
controller on SDL's callback thread or consumes the host's event queue.

`borrowed_subsystem=0` acquires one balanced SDL gamepad-subsystem reference;
`1` borrows an existing reference. It never calls SDL_Quit. For a gamepad the host
already opened, it does not change sensor state: the host must explicitly enable
available motion sensors to permit observation. For newly opened gamepads it enables
available sensors. Destroying the reader closes only its own references, without
explicitly disabling sensors that a later host reader may now use. Final SDL handle
close releases the device normally. Coordinate ownership if the host uses a
different statically linked SDL instance; independent SDL/HID clients are not
guaranteed to coexist with every driver. SDL itself can perform device setup when
opening/enabling sensors, so real host input/vibration coexistence remains a
hardware acceptance test, not an absolute promise about driver behavior.

The direct reader and isolated reader share the same motion watchdog. A connected
pad can still advertise sensors and deliver buttons after Steam changes its IMU
reporting during reconnect. Only fresh gyro reports keep that watchdog
alive; duplicate hardware timestamps and stale packets do not. After 750 ms of
silence, sensors enabled by the reader are disabled then re-enabled through SDL,
with attempts spaced by at least two seconds. The existing endpoint, controller
selection and game event queue are retained; normal controls are never suppressed.
Pre-existing host-opened handles are excluded even if their sensors are enabled:
the host owns their recovery. Coordinate sensor ownership if another host reader
subsequently acquires a handle originally opened by GyroLib.

The standalone demo puts ImGui's SDL backend in **manual gamepad mode** and gives
it a borrowed handle from this reader for the selected physical device. It clears
that borrowed list before polling (which may close a disconnected handle), and
refreshes it before ImGui's NewFrame. This prevents the UI from opening a newly
attached controller before acquisition and leaving its sensors disabled. An
external host that opens handles first still needs to enable its own sensors;
the adapter does not take over their state.

## Steam Input

`gl_steam_provider` borrows read callbacks. The host must initialize and update
its own public Steamworks interface. `gl_steam_poll` accepts a strictly increasing
host frame serial and refuses duplicate polls. It never calls Init, RunFrame,
Shutdown, changes action sets, alters Steam settings, or attempts Steam internal
calibration. Manual calibration is unavailable for this source and automatic
local bias correction is disabled. Orientation fusion still runs with zero local
bias. Controls/touch contacts are optional callbacks based on existing host actions
whose origins really expose them; click/pressure is never substituted for touch.

The typed [Steamworks bridge](../examples/steamworks_bridge.hpp) is an integration
example requiring the host's licensed SDK. It was not compiled against a Steam SDK
in this validation. The actual borrowed C adapter was compiled and tested with a
fake provider. Valve reports motion as signed-short-scaled floating values, and
does not expose a hardware timestamp in GetMotionData. Consequently Steam freshness
means connected-handle/frame-level availability; an internally frozen nonzero
Steam sample cannot always be distinguished from a stationary controller.
See [Valve's motion-data API](https://partner.steamgames.com/doc/api/ISteamInput#GetMotionData).

### Steam virtual gamepads and isolated SDL acquisition

Steam can populate `SDL_GAMECONTROLLER_IGNORE_DEVICES` and hide physical devices
from the game process. The virtual gamepad still supplies commands but may have
no sensors. When the adapter observes a Steam handle on a sensorless pad, it starts
`gyrolib_sensor_worker[.exe]`. This separate process uses ordinary SDL gamepad
discovery, `SDL_GamepadHasSensor`, sensor enablement and actual sensor events.
There are no Sony/Valve-specific packet decoders in GyroLib. Controller and
transport support comes from SDL; this does not guarantee every controller works.

For an ordinary parent, the helper receives its own environment copy without
Steam session/virtual-pad identifiers or inherited gamepad exclusion filters.
On Windows, Steam also injects its module into ordinary child processes regardless
of those variables. When that module is present in the host, the adapter asks the
existing desktop shell to launch the reader independently, hidden, using the
documented `IShellDispatch2::ShellExecute` API. A randomly named local pipe,
restricted to the current user/SYSTEM, connects it to this adapter. The server
verifies the client executable path before accepting samples or obtaining its
shutdown handle. No persistent service, scheduled task, admin request or system
input modification is used. This path requires an available Windows desktop shell.

Only the helper's SDL discovery hints are changed. The game's environment,
filters, event queue and Steam service remain untouched. Neither Steam's overlay
code nor the game's input hooks are patched or unloaded. No injection, Steam
initialization, fake AppID or SDK is involved. The helper returns normalized motion,
physical activator buttons/contacts, capabilities, origins and button labels. They are read
only by GyroLib; no game buttons/sticks are injected or duplicated.

These endpoints are marked as motion companions. They cannot select a player
automatically. A selected virtual controller can acquire a companion automatically
when SDL reports matching nonzero vendor IDs, exactly one connected ordinary
controller group and one connected companion have that vendor, and the companion
has supplied 300 ms of qualified motion. Product IDs can differ for USB receivers;
names never participate. Even silent candidates count against uniqueness. This
is a session convenience, not a claim of universal physical/virtual identity.
Missing metadata or multiple candidates leave a fallback **Gyro sensor** choice
in F10. An established binding is never replaced because of a temporary stall or
a new arrival. The host may override through `gl_bind_motion_sensor`; native
menus query `gl_motion_sensor_needs_selection` and `gl_get_motion_sensor`.
Exactly one companion can be bound to each controller. Steam's virtual
buttons/sticks remain the game command source. A companion exposing buttons owns
GyroLib's button activators, so physical labels refer to physical presses rather
than a Steam layout's remapped virtual button. This ownership remains during a
stall, with pressed state expiring after 150 ms; it does not silently fall back
to a differently mapped virtual button. Motion-only companions preserve the old
control policy. When the provider declares physical-family authority, its
capabilities and controls replace virtual data for those families, including
known absence. The SDL worker supplies physical sticks for gyro deflection.
Flick uses the separate physical right-stick/right-touchpad input when supplied,
avoiding duplicated Steam virtual axes. Other providers can leave unknown
families supplemental. Core gyro selection processes one motion source, never sums.

IPC uses versioned fixed-size records and a bounded pipe. The host performs
nonblocking reads on its owner thread, accepting fragmented records and rejecting
unknown versions. Sensor timestamps are retained; an OS monotonic clock also
measures transport age, so a blocked/backlogged helper cannot replay old motion
as fresh data. Samples older than 100 ms are discarded. Source qualification and
hysteresis still happen in the core. Missing heartbeats/exits remove the helper's
endpoints; restart attempts are limited to once per five seconds. Destroying the
adapter requests a graceful stop, then terminates only its own verified unresponsive helper.
The child also exits on pipe closure when the host exits.

Control freshness is measured after SDL event pumping. Changed axes are compared
with the last transmitted state, so a newly received sensor report cannot skip
that frame's stick/pad update. Unchanged contacts refresh every 50 ms; changed
positions are sent immediately. IPC also carries bounded, timestamped
right-pad pulse commands for explicitly enabled feedback (see API.md).

The same-build IPC is version 7, adding physical-family authority and physical
stick states without changing the public ABI layouts. Controls and separate physical flick coordinates
are sent on changes and refreshed every
50 ms while physical motion reports are arriving. No fresh motion for 100 ms
stops control refresh too, preventing cached held contacts from surviving a
sleeping/stalled device. Controls use the same transport-age rejection as motion;
capabilities and labels refresh on remap and periodically. Older worker versions
are rejected; deploy the adapter and worker together.

If a connected sensor stops providing fresh gyro reports for 750 ms,
the helper uses the same watchdog as the direct reader to re-enable its sensors
using SDL's standard API.
Retries are limited to once per two seconds. This handles a launcher changing
the physical reporting state while SDL still considers it enabled, without
recreating the endpoint or losing its explicit association. Stale samples remain
rejected. The automated fixture verifies recovery on the same endpoint.
[VALIDATION.md](VALIDATION.md) records the separate hardware checks.

GyroLib acquisition does not call rumble, LED, mapping or haptic APIs. An explicit
`gl_sdl_apply_feedback` call can request a bounded right-pad pulse; its contract is
in `gyrolib/sdl.h`. SDL's
drivers can configure devices while opening/enabling sensors, including mode or
light changes; this is not a passive HID tap. Actual vibration/input coexistence
still requires validation for each device/transport. No Steam calibration data
is used or recalibrated by this SDL path.

Bindings last for the connected session. Disconnect/removal retires the sensor
and clears its association. Automatic pairs qualify again after reconnect or
restart; ambiguous setups need another explicit choice. Serial/path metadata identifies the physical sensor on the SDL side,
but cannot prove its association with an opaque Steam virtual handle. These
bindings are deliberately not saved as persistent physical/virtual identity.

The Windows single-DLL build prepares its embedded worker in the per-user runtime
cache automatically; no path setup is required. In modular/static builds the
default worker location is beside the host executable. Those mods shipping in a
different directory call `gl_sdl_set_sensor_worker(reader, absolute_utf8_path)`;
an empty path disables it. It never searches PATH. `gl_sdl_error` reports missing
workers and protocol/process failures. Direct SDL and a borrowed public Steam
provider remain usable without this optional acquisition component. Linux/Proton
code paths are architectural only until tested on those platforms.
See [distribution](DISTRIBUTION.md) for cache verification, diagnostics and the
distinction between files to install and the isolated process still used at runtime.

Windows IPC reads the pipe handle directly. The bundled SDL buffered stream can
lose partial reads on `ERROR_NO_DATA`; the fragmented-record regression reproduces
that failure and covers the direct pipe implementation. Other platforms use SDL
process streams. The Windows launch mechanism follows Microsoft's
[desktop-shell launch guidance](https://devblogs.microsoft.com/oldnewthing/20131118-00/?p=2643).

Disable **both** gyro-to-mouse and gyro-to-stick in the Steam layout before using
camera gyro. `host.steam_gyro_output=2` reports known enabled output as a diagnostic;
the panel always explains the risk. Neither the core nor public Steam motion data
can reliably inspect all legacy/community layouts, desktop mappings or external
remappers. Unknown is not proof of disabled output. No automatic Steam-profile
modification or global mouse/stick suppression is attempted.

## Controls

Capability bits come from runtime APIs, not names. Buttons use SDL button ordinals
0..31. Choices 1..32 select these bits. The menu offers individual buttons only;
legacy combination values 33..40 remain loadable but are no longer offered.
Touchpad, thumbstick contact,
grip contact and analog deflection are independently configurable. Bilateral
families offer left/right/either/both only when those capabilities exist. A single
touchpad offers either/single, not a fictional left/right split. Families combine
with OR. Deflection is a magnitude threshold and never a stick-click alias.

Both direct SDL acquisition and the isolated companion use
`src/detail/sdl_controls.hpp` for ordinary buttons, touchpad contacts and labels.
The companion adds these capabilities only after it is paired with the selected
controller; unrelated controllers cannot supply its activators.

Bundled SDL 3.4.16 represents Steam Controller 2026 capacitive contacts as extra
gamepad buttons. GyroLib validates numeric VID/PID (28de:1302/1304/1305), the
physical SDL mapping's extended button bindings and actual button availability
before translating them into stick/grip contact families. It does not infer
touch from stick tilt, stick click, grip buttons or touchpad pressure. An unknown
device or a remapping that no longer identifies these contacts gains no fabricated
contact capability. Providers can expose other devices' contacts through the C API.

The existing SDL button ordinals remain readable as aliases, but contact duplicates
are hidden from the button menu. Existing selected aliases migrate to their
dedicated family on update, preserving the OR of enabled activators and saving
once when a settings path exists. The dedicated families provide left/right/either/both.
Physical stick/touchpad clicks remain individual buttons, distinct from touch.
`gl_set_button_contact` lets providers declare a verified alias family/side; the
core checks current capabilities before hiding or migrating it. Display names
alone never create this relationship. Legacy combination selections stay readable
in the panel until replaced, without reintroducing them into the drop-down.
SDL's source mappings are documented in
[SDL_gamepad.c](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/SDL_gamepad.c#L1163-L1165)
and its [Triton driver](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/hidapi/SDL_hidapi_steam_triton.c).
This is interpretation of SDL controls, not a HID packet decoder. Other SDL
versions/drivers, including future dedicated capacitive APIs, require validation.

## Controller button names

Buttons keep their stable SDL ordinals; their displayed names follow the selected
physical controller. SDL supplies face-button labels through
[SDL_GetGamepadButtonLabel](https://wiki.libsdl.org/SDL3/SDL_GetGamepadButtonLabel)
and its real device type supplies L1/R1, LB/RB, L/R, Create/Share, View/Menu and
other known labels. Exact extra-button labels are used only where metadata proves
the variant (for example Sony's DualSense Edge VID/PID, with available buttons).
Names never create capabilities, change bindings or select a gyro source.

`gl_set_button_label(context, endpoint, ordinal, utf8_name, provenance)` copies
a provider's label; nullptr clears it. `GL_LABEL_DEVICE` describes the exposed
layout. `GL_LABEL_PHYSICAL` is reserved for a verified physical origin, such as
Steam's public action origins, and takes precedence over a paired virtual Xbox
label. Endpoints must already have the same proven physical ID to share names.
At equal provenance, SDL wins, then the lowest endpoint ID. Disconnected endpoints
cannot supply labels; changing controller never borrows another device's names.

`gl_steam_set_button_label_provider` adds an optional borrowed label callback
without changing the original provider struct's ABI. The typed SDK example uses
[GetActionOriginFromXboxOrigin](https://partner.steamgames.com/doc/api/ISteamInput#GetActionOriginFromXboxOrigin)
and `GetStringForActionOrigin` for standard normalized buttons. The host supplies
explicit origins for extra/remapped controls through `set_button_origin`, keeping
these aligned with its controls callback. No action sets are created or changed.
Do not infer a physical identity from a commercial device name.

Unknown buttons fall back to positional labels instead of guessing a manufacturer.
Legacy combined values retain readable names, such as `L1 or R1` / `L1 + R1`, but
are marked unavailable in menu enumeration. Both native
menus and F10 read these names through `gl_choice_at`; a host can also call
`gl_get_button_label`. Returned pointers are borrowed and must be copied before
retaining them across label/endpoint updates, same-choice re-query (pair labels),
language changes or context destruction. `GL_EVENT_BUTTON_LABELS` notifies edits.
