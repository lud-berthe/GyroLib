# Settings, native menus and the F10 panel

The playable host-owned integration is `examples/tps/pause_menu.hpp`:
open the demo's Pause → Gyro settings. It builds its own widgets from the public
`gl_menu_tab_*`, `gl_menu_shared_setting_*`, `gl_choice_at` and help APIs; it never
calls `gl_panel_draw` or reads private core headers. Edits call `gl_setting_set`,
actions call `gl_action`, and language/controller selection uses the corresponding
public setters. Both frontends share the same context and `girolib.ini`.
Use `gl_setting_advanced_group(id)` to associate extra controls with Smoothing,
Acceleration, Flick Stick or Hold to disable. Both examples expand each family with a small + / -
button to the left of its label. Expansion state is UI-only, independent per view,
and does not change settings. Recenter and optional host-declared zoom are normal
camera rows; there is no global Advanced section or separate right-stick policy.
Activation Mode is the first row and includes Off (`GL_GYRO_OFF`, value 6).
Gyro Space follows, then X/Y sensitivities with inline inversion. In Yaw + Roll,
X has independent Yaw and Roll checkboxes, while Y keeps its Pitch inversion.
The additional `gyro.invert_roll` metadata is visible only in this space; its
checkbox belongs beside X, rather than on a standalone row. There is no
Gyroscope checkbox. Off hides gyro-space, sensitivity, smoothing, acceleration
and activator settings while retaining their values; Flick and camera recenter
remain independent. The legacy `gyro.enabled` API key is a hidden compatibility
alias: 0 selects Off; 1 changes Off to Always on, leaving other modes unchanged.
`gl_setting_is_activator(id)` identifies the six command families plus their
inline thresholds and short-press control. Both frontends group their visible
rows beneath the localized `ui.activators` heading with a shared branch line.
The + beside Activation Mode appears only for Hold to disable
and opens temporary inversion and trackball checkboxes. They use the already
selected activation commands. Optional axes and decay appear when enabled.
Smoothing, Acceleration and Flick expanders and children are hidden while Off.
Children are indented and linked by branch lines. Flick thresholds follow the
selected Stick/Touchpad/Either input. Triggers follow Buttons in the model order;
both examples show stick/trigger thresholds inline. Capability and cursor
restrictions still apply to Flick. Presets populate Fast X/Y plus the
start/full speeds; editing a detailed value selects Custom. The core owns this
behavior, including setting events and automatic saving.
Custom is a derived current label, not a selectable preset: its choice has
`available=0`. Use its label when it matches the current acceleration value,
while omitting it from the dropdown along with other unavailable choices.
The native frontend refreshes metadata each frame, so edits from F10, capability
changes and calibration actions are visible immediately without a settings copy.
A native engine can instead invalidate cached widgets on the public events.
The pause entry remains usable with `ui.menu_key=` (the library shortcut disabled).

Both frontends put `activation.short_press` on the right of the Buttons row,
using the compact localized Tap only / Appui court checkbox. The setting is
visible in camera views only for Hold to enable or Hold to disable when the host declares a wired
`GL_HOST_SHORT_PRESS_FILTER`; it is disabled until an available individual button
is selected. Its per-view ID/value remains unchanged in the INI. Hide the
standalone row when using this inline presentation. Native menus can read the
same metadata or present it as a separate boolean. Checked, a press below 200 ms
triggers the button's game action on release; a longer hold suppresses it.
Acquisition and gyro activation remain immediate. See [input filtering](API.md)
for the required game-event hook; the camera callback alone cannot implement it.

Keep controller selection above the view tabs. Enumerate connected endpoints,
exclude `gl_is_motion_companion` endpoints, and deduplicate by physical ID.
Show a separate sensor selector only if `gl_motion_sensor_needs_selection` is
true after the update. Ordinary unique SDL pairs are associated automatically;
the selector is only a fallback. For that selector, show
`gl_get_motion_sensor(context, selected_physical_id)` and call
`gl_bind_motion_sensor(context, selected_physical_id, sensor_endpoint_id)` only
after an explicit user choice. A sensor ID of zero clears the association.
Offer only sensors for which `gl_endpoint_motion_available` is true after the
latest update. Offer the controller's default sensor only if a non-companion
endpoint in its physical group passes the same check. A Steam virtual controller
without motion remains in the controller list for its buttons and sticks.
Keep the bound sensor's name and association during a temporary interruption;
do not silently switch to another controller or clear the binding.
Do not add UI-side name/count guessing; reuse the core policy and acquisition hints.
These associations last for the connected session, not across application
restarts or sensor removal. Automatic pairs qualify again after reconnect.
[INPUT.md](INPUT.md) describes discovery and limits.

Flick offers `Off / Stick / Touchpad / Either` when fresh input exposes a separately
identified right touchpad (including the Steam Controller's two-pad layout) and
the host wires suppression. Otherwise the original stick-only `Off / On` remains.
Use choice availability, not a controller-name check. The right pad is used;
single central touchpads do not gain this capability. Both example menus place
`SPIN DURATION` first in the expanded Flick options; native menus receive the same independent
duration setting (0..1000 ms, 5 ms steps, 150 ms default).
The displayed `Either` keeps the existing `GL_FLICK_BOTH` value (5); settings
remain compatible. Bilateral activators use `Either` without a `/ single` suffix.
When the touchpad capability is exactly `GL_SINGLE` (for example DualSense), only
Off and On are available, with On retaining `GL_SIDE_EITHER` (3). Its description
refers to touching that one surface. Native menus must use the returned labels
and availability rather than hard-coding left/right choices.

Offer individual buttons only: skip choices whose `available` flag is false.
Legacy button combinations and verified touch-contact aliases are omitted by
the shared model. Contacts belong to their dedicated touchpad/stick/grip rows;
physical clicks remain separate buttons. Selected contact aliases migrate to
the equivalent family automatically. Previously saved combinations retain their
meaning and a readable current-value label until changed, without appearing in
the list of selectable choices. F10 and native menus use the same availability.

For enumeration help, call `gl_choice_description(context, setting_id, value)`
with each choice's stable value, not its presentation index. This exposes the
same localized per-option help as the F10 panel without changing `gl_choice`'s
ABI layout. The general setting description is a short overview. Show the
specific explanation on option hover or keyboard/gamepad focus. Wrap tooltips
to the viewport and font scale; do not render long help as one unbounded line.
The host's view description belongs to `gl_menu_tab.description` only; it is
not appended to every setting tooltip, including after a language change.
There is no active-profile/editing-tab reminder below it. A status line is shown
only when the host is not reporting that mode. Setting help omits redundant
"in this view" wording; the selected tab already provides that context.
Help describes the player's action or the effect of a setting in one or two
short sentences. Smoothing explains the steadiness/response tradeoff; activation
choices avoid repeating ignored bindings or shared state rules. Integration
requirements and detailed algorithms stay in the technical documentation.

Use **`gl_menu_tab_count(context)`** / `gl_menu_tab_at` to build
one tab per registered view mode. Enumerate each tab's settings with
`gl_menu_tab_setting_count(context, tab.id)` / `gl_menu_tab_setting_at`.
Tabs have stable uint64 IDs: mode ID + 1 for named profiles.
Tab 1 (Camera) is retired; there is no implicit profile. A host with one camera
still declares and reports that view. Zero registered views means zero tabs;
no active mode means no output. Switching tabs does not change gameplay.
Enumerate shared calibration and actions separately with
`gl_menu_shared_setting_count` / `gl_menu_shared_setting_at`. There is no General
tab. Legacy getters with `GL_TAB_GENERAL` (0) still alias these shared controls,
but 0 is never returned by tab enumeration. Declaring a cursor mode through
`gl_set_gameplay_context_output_target` hides flick mode and pivot duration in that
tab, independently of the currently active mode. Camera tabs keep those controls.
The legacy flat `gl_menu_setting_count` / `gl_setting_at` still enumerates all
settings, including the full profiles. Both interfaces expose stable IDs, localized labels/descriptions,
type, value, bounds, step, default and current visibility/availability.
Use **`gl_menu_choice_count(context, id)`** with `gl_choice_at` for enum choices.
The old context-free count functions cannot enumerate custom modes.
`gl_choice_at` returns stable numeric values and availability. A choice's value
is not necessarily its index: find a selected preview by comparing `.value`. Filter
unavailable choices instead of renumbering them. Preserve a saved binding whose
controller is absent; show it as unavailable, never silently replace it.
Capabilities are refreshed during input/update calls, so build or refresh the
native model after that step. `gl_setting_set` validates finite/range values and
quantizes to the advertised step, then emits `GL_EVENT_SETTING` when it changes.

The native C example builds a console representation of the same widget metadata
and explains callback wiring. Replace its widget construction with engine calls.
Use settings IDs for widget identity, not translated text or row order. Re-query
availability when devices/sources change. Refresh button names on
`GL_EVENT_BUTTON_LABELS`, device selection/disconnect and physical association.
Rebuild on `GL_EVENT_CONTEXT` or
`GL_EVENT_HOST_CAPABILITIES`; mode registration/removal can change the row count.
Copy borrowed strings before retaining metadata; their lifetime is documented in
[CONTEXTS.md](CONTEXTS.md). `gl_action` supports calibration begin,
cancel and reset. Call `gl_initialize_settings(context, NULL, NULL)` after applying
host defaults to load/create `girolib.ini` beside the library. Setting/language
changes and reset then save automatically. Optional arguments select a directory
or import a legacy file only when the destination is absent; the original remains
intact. Initialization failures disable automatic saving until initialization is
retried successfully. Low-level `gl_set_settings_path`, load and save APIs remain
available for explicit host-managed paths (UTF-8). The legacy `settings.save` action remains callable
for explicit retry/export workflows, but its menu metadata is hidden.

Manual calibration provides a five-second placement countdown and then requires
one second of stationary samples (at least 20). Movement restarts collection.
Automatic calibration offers Off and Any time. Menus only is available only when
the mod declares `GL_HOST_MENU_STATE`, meaning that `host.menu_open` reliably
observes the game's menus. Withdraw that capability if the hook becomes unavailable.
An existing saved Menus only preference is retained but has no effect without the
capability, even if the F10 panel is open. With the capability, the host menu or
the F10 panel permits stationary calibration. An unpaused `GL_OUTPUT_CURSOR`
view, such as a gyro-driven inventory, is excluded from Menus only even though
`host.menu_open` must remain true for cursor routing. Pausing that view or opening
the library panel suspends its cursor and makes the menu eligible again.
This rule is independent of the activation button/toggle state, so releasing an
activator inside an inventory does not make it a calibration screen.
Off and Any time require no menu
hook, and Steam never receives local bias correction. Both calibration paths
measure time-weighted gyro/accelerometer means and variance over 250 ms windows.
Ordinary sensor jitter no longer restarts calibration for each individual sample.
Average speed must remain below 3°/s, gyro noise below 0.9°/s RMS, acceleration
near 1 g with noise below 0.025 g RMS, and window means must stay close to the
initial stable window (0.35°/s and 0.02 g). Large impulses cancel immediately.
After one second of accepted windows, automatic calibration slowly adjusts bias
with a two-second time constant outside active gyro use. During active gyro use,
only residual drift within 0.15 degrees/s is accepted, with an eight-second time
constant. The host can also veto automatic learning for each update; see
[advanced motion](ADVANCED_MOTION.md). Manual calibration averages one second of accepted
windows after its countdown. Pauses in the sensor stream clear the measurement window.
These checks do not add a dead zone or smoothing to the host's gyro output.
This cannot mathematically distinguish all slow intentional turns from sensor
bias: leave it off if unsuitable for the device. Steam calibration is external.
Native widgets can display `gl_diagnostics.calibration_state`, remaining countdown,
stationarity and `GL_EVENT_CALIBRATION`. No debug overlay is forced.
The action metadata exposes only `calibration.begin` at rest and only
`calibration.cancel` during a manual countdown, collection or movement retry.
Re-query visibility after actions and updates, including `GL_EVENT_CALIBRATION`;
completion or cancellation restores the Recalibrate action. Recalibration stays
visible but disabled without a usable SDL source, including on Steam Input.

## Non-native panel integration

The `gyrolib_panel` frontend contains no renderer or system hooks. Its ImGui
headers, compiled implementation and configuration must agree. A host already
using another ImGui version should rebuild the panel and UI integration against
one compatible version. Alternatively, isolate the complete ImGui implementation
(for example in a separate module or namespace), with its own context, allocators
and backends. Creating a second context alone does **not** isolate incompatible
ImGui ABIs or symbols. Never pass contexts, widgets or draw data between different
versions. The demo shows a complete SDL3 + SDL renderer integration; another
engine can use its appropriate backend. The bundled version and licenses are
listed in [THIRD_PARTY.md](THIRD_PARTY.md).

1. Create/choose an ImGui context on the rendering owner thread, initialize the
   platform/renderer backends and a scalable font. The demo uses the embedded vector
   font (`io.Fonts->AddFontDefaultVector()`). Retain ImGui's bundled font licenses.
2. Enable `ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad`.
3. Forward host UI events to the ImGui platform backend. Pass F1..F24 events to
   `gl_panel_function_key(panel, function_number, is_down, is_repeat)`, where the
   number is 1..24, not an SDL/Windows keycode. Only the configured shortcut toggles
   on key-down; key-up and auto-repeat do nothing. The demo maps both SDL ranges
   F1..F12 and F13..F24. For an F10 event, pass `function_number=10`; this replaces
   the removed `gl_panel_f10` shortcut in version 0.2.
4. Backend NewFrame, ImGui NewFrame, then `gl_panel_draw(panel, viewport logical
   width, height, display DPI scale)`, Render, and submit the draw data through
   your renderer. The panel scales font/widgets using DPI and viewport resolution
   (normally 2x at 4K), with a fit limit for small windows. Scaling is always
   automatic; the blue title bar contains language and close controls. The legacy
   `ui.scale` key is accepted from old files but discarded on migration; it is
   hidden/unavailable in menu metadata and has no effect on the panel.
   The selected controller is shown above the view tabs, with the Steam double-gyro
   reminder in its tooltip. The fixed footer groups automatic calibration,
   Recalibrate (or Cancel calibration) and Reset on one compact row. Reset wraps
   if the localized labels cannot fit. Calibration progress and save errors add
   a line only when present. There is no empty calibration box, automatic-save
   reminder, Save button or idle input-hint line. These controls stay accessible while
   scrolling any view profile.
   The panel background is opaque so underlying inventory text does not bleed
   through. No global font/style scaling is left behind.
   Every view tab has its own inversion checkboxes beside its X/Y sensitivity.
   Each checkbox writes that profile's inversion ID; it cannot affect other tabs.
   The core menu model keeps separate IDs so native menus can group them similarly.
   Stick deflection threshold shares the Stick row and appears only for an
   enabled, available stick-deflection binding. The shared model also hides this
   threshold otherwise; its saved value is retained when the binding is off.
5. Respect `io.WantCaptureKeyboard/Mouse` in your **local game UI input routing**.
   Do not block system inputs. The panel visibility itself gates gyro and flick.
   Aim states remain observed; never send or edit aim commands on the panel's behalf.
6. Destroy panel, backends and its ImGui context before unloading the frontend.

Inputs from mouse, keyboard and controller navigate the same widgets. For a host
using ImGui's SDL backend, use its `ImGui_ImplSDL3_GamepadMode_Manual` when GyroLib
owns acquisition. Start with an empty list. Clear borrowed handles before polling
the reader, then pass its already-open handle for the selected physical device
before backend NewFrame (see `demo_ui_gamepad` in `examples/sdl_demo_input.hpp`). Do not let ImGui's auto
mode open a late-connected controller first with disabled sensors. If the game
itself owns the handle, explicitly enable its sensors there instead. Physical
controller UI navigation still needs hardware testing.

## Persistence and localization

Edits auto-save in the core, so native menus and F10 behave identically. Apply host
defaults before loading an existing file or setting its path. A settings setter,
language change or reset writes synchronously on the owner thread when a path is
configured; unchanged values do not write. Reset batches all profiles into one
save, including retired modes. Loading validates before applying and never writes.
Use an empty path for temporary sessions without persistence.

Check setter/action return values and `gl_get_settings_save_result`. On write
failure the change remains active in memory, the previous file stays intact, and
the panel shows the failure until a later successful save. Native menus should
show that status too. `GL_UNAVAILABLE` indicates a temporary session with no path.
Routine input processing does not write settings. A discovered contact alias can
trigger a one-time migration and save from `gl_update`. Slider edits can write each
changed tick; hosts with slow storage can submit their widget value on edit completion.

The default file is `girolib.ini`, beside `gyrolib.dll`. Edit it while the host is
closed and relaunch to load changes. For example:

```ini
ui.menu_key=F8
```

F1..F24 are accepted (case-insensitive); a missing key defaults to F10. Disable
all panel shortcuts by leaving the value empty:

```ini
ui.menu_key=
```

The panel header and demo hint reflect the configured key; the demo hides its hint
when disabled. This does not disable the menu model, saving, or the host's native
menu visibility/camera gate (`gl_set_panel_open`). A native-only mod can omit the
panel frontend entirely. Reset preserves the shortcut, so it never unexpectedly
re-enables the panel. Use `gl_panel_create(context, NULL)` after initialization to
retain the chosen file without changing its persistence state.

The text configuration is `key=value`, locale-independent numbers, schema 17.
Only `ui.language`, `ui.menu_key` and `calibration.automatic` are shared preferences.
All gyro/activator/flick values belong to explicit `context.<id>.*` profiles.
The old global duplicates, mode selectors and manual scale are no longer written.
Register all views before initialization. A valid older file is migrated and
rewritten by `gl_initialize_settings`; low-level `gl_load_settings` stays read-only.
An implicit-only old file requires exactly one registered target view, otherwise
initialization returns `GL_UNAVAILABLE` and preserves the file unchanged.
Schema 16 replaces the separate gyro enable flag with Activation Mode Off.
Legacy disabled profiles migrate to Off; existing sensitivities and bindings are
retained. `gyro.enabled` is accepted on load but omitted on save.
Schema 15 retires the separate right-stick policy, leaving ordinary stick
activation bindings unchanged. Schema 14 replaces separate inversion/trackball button assignments with optional
Hold-to-disable behaviors, preserving the existing activators and axis/decay settings.
Schema 9 adds `ui.menu_key`, including its empty/disabled meaning.
Schema 12 materializes the old acceleration presets into the editable curve,
retaining their motion output and preserving existing Custom values. Manual
curve edits in a schema 12 INI select Custom on load. Unknown keys remain intact.
Schema 8 adds Touchpad/Both flick modes; old Off/On values keep their meaning.
Schema 7 separates Always on from Hold to disable and persists `ui.language`.
Schema 1..6 combined Always-on profiles with bindings or short-press filtering
migrate to Hold to disable; otherwise they remain Always on. Ignored bindings in
new Always-on profiles are retained, and do not migrate again on reload.
Schema 6 introduced complete independent mode profiles. Legacy absolute sensitivities
retain their IDs, and shared gyro/flick values migrate into each profile. Old
conditional activation becomes per-profile enable states by the selected mode ID;
overlapping legacy command contexts should be reviewed after migration.
Schema 5 raised sensitivities to 20, retaining the .1 step and 2.5 defaults.
Schema 1..9 files remain readable. Schema 4 added the extended space range. The schema
prevents older libraries from overwriting configurations they cannot represent.
Schema 1's `gyro.smoothing_seconds` migrates to `gyro.smoothing_ms`; new settings
keep defaults while existing valid settings persist. Schema 2's fixed aim conditions
require an explicit new mode selection; old multipliers are retained as migration
data and never applied automatically. All named profile settings are
saved by stable ID, including when that mode is temporarily unregistered. See
[the migration contract](CONTEXTS.md#persistence-and-migration).
Unknown key/value pairs are
retained after load and re-emitted on save. Duplicate keys, nonfinite values,
invalid ranges and malformed lines fail transactionally. Individual lines are
limited to 4,096 bytes; no fixed number of mode records is imposed. Future schema versions
are refused for both load and overwrite. Save writes a temporary sibling and
atomically replaces the destination (Windows MoveFileEx, POSIX rename). This
protects against ordinary interrupted writes; it is not a cross-process lock or
a universal power-loss durability guarantee. One host owns a config path. Always
load before editing/saving an existing file. Comments and source ordering are not
preserved. Keep backup/version control for manual config editing.

English is the fallback catalog. Relevant Returnal translations are included for
French, German, Spanish, Italian and Portuguese. New general labels have French
translations, with English fallback for other languages. Button names come from
SDL's device metadata or verified Steam origins; unknown buttons use localized
positional descriptions (English/French). `tools/generate_localization.ps1` regenerates the
checked-in C++ catalog from JSON; it is not needed to build. The one-time importer
records extraction provenance; editing catalogs is the normal maintenance path.
Custom mode names and descriptions belong to the host. Use `gl_get_language` and
re-register translated metadata under the same IDs when language changes. The
library localizes its own X/Y suffixes and common descriptions automatically.
