# Menus and settings panel

[Documentation](INDEX.md) / Integration

Native widgets and the supplied panel use the same public model and settings.
The model provides stable IDs, localized labels/help, types, values, ranges,
steps, defaults, visibility and availability. Edits go through `gl_setting_set`;
actions go through `gl_action`. [Settings](SETTINGS.md) covers saving and localization.

- [Choose a frontend](#choose-a-frontend)
- [Enumerate the model](#enumerate-the-model)
- [Layout](#layout)
- [Controller availability](#controller-availability)
- [Controller and sensor selection](#controller-and-sensor-selection)
- [Help and notifications](#help-and-notifications)
- [Static ImGui panel integration](#static-imgui-panel-integration)

## Choose a frontend

| Integration | What the mod supplies |
|---|---|
| Native game menu | Widgets and navigation built from the public menu model |
| [DX12 panel in the DLL](OVERLAY.md) | Owner/render/window callbacks; GyroLib owns ImGui and GPU resources |
| Static `GyroLib::gyrolib_panel` | Compatible ImGui context, platform/renderer backends and event routing |

The demo's Pause → Gyro settings is a native-menu example in
`examples/tps/pause_menu.hpp`. It uses public APIs and its own widgets, without
calling `gl_panel_draw`. `examples/native_menu.c` shows model enumeration in C.
F10 or Back + Start opens the separate supplied panel. Both edit the same context.

Every opening selects the view active at that moment, whether triggered by the
keyboard, controller or API. Changing tabs while the panel is open keeps that
selection until the next opening. Custom frontends can use
`gl_get_panel_opening(context, &view_id)`: its serial changes on each
closed-to-open transition, and `view_id` identifies the view at that opening
(zero when none was active). Before the first opening, both values are zero.

## Enumerate the model

1. Iterate `gl_menu_tab_count` / `gl_menu_tab_at`.
2. For each tab, iterate `gl_menu_tab_setting_count` / `gl_menu_tab_setting_at`.
3. Query shared controls with `gl_menu_shared_setting_count` /
   `gl_menu_shared_setting_at`.
4. For enums, use `gl_menu_choice_count(context, id)` and `gl_choice_at`.

Use IDs for widget identity and `choice.value` for selection, never list indexes
or translated labels. Skip invisible settings. Disable unavailable settings and
omit unavailable choices from dropdowns. An unavailable current choice can still
supply its preview label, as with Custom acceleration. Do not hard-code counts.

Tabs have uint64 IDs equal to view ID + 1. There is no implicit camera or General
tab. Use the tab and shared-setting APIs above for new menus. The flat
`gl_menu_setting_count` / `gl_setting_at` API also works, with view names prefixed
to labels. Compatibility aliases remain: tab 0 queries shared controls and
tab 1 has no rows. Context-free choice counts cannot enumerate custom views.

A tab edits a profile, not the game's active view. `tab.active` and `tab.available`
are runtime observations and must not disable configuration. Hardware and host
capabilities use the setting's own `available` flag.

## Layout

The supplied frontends arrange controls as follows:

- Enabled shortcuts, language and close/back controls in the header; controller
  selection above tabs. The gamepad shortcut uses the selected controller's
  button labels and is hidden when no connected controller exposes both buttons.
- Optional [inheritance selector](INHERITANCE.md) below the tabs.
- Activation Mode, Gyro Space, X/Y sensitivities, then the remaining settings.
- Activators grouped together: Buttons, Triggers, Touchpad, Stick sensor,
  Grip sensor and Stick.
- Shared calibration controls in the footer, followed by reset/recommended actions.
  The recommended action appears only after the host captures a profile.

`gl_setting_is_activator(id)` identifies activator families and their inline
controls. `gl_setting_advanced_group(id)` identifies Smoothing, Acceleration,
Flick and Hold-to-disable children. The examples place a +/− to the left of the
parent label and indent children with branch lines. Expansion is UI state per
view, not a saved setting. Hide feature expanders and children while Off.

Activation Mode includes Off; there is no separate Gyroscope checkbox. Off hides
gyro-space, sensitivities, activators, smoothing and acceleration while retaining
their values. Flick and recenter are independent. Hold to disable's expanded
options contain temporary inversion and trackball using its existing activators.

Place inversion beside each sensitivity. Yaw + Roll uses Yaw and Roll inversion
beside X and a single Pitch inversion beside Y. The `gyro.invert_roll` metadata
is visible only for Yaw + Roll. Stick/trigger thresholds fit beside their bindings.
Flick's expanded options begin with Spin Duration and show thresholds only for
its selected input families.

Place the Block long press checkbox (`activation.block_long_press`) beside
Buttons. It is available only in camera views using a hold activation mode, with an eligible
individual button and a wired `GL_HOST_LONG_PRESS_BLOCKING`. A native menu may use
a separate boolean row instead. [Input filtering](ADVANCED_MOTION.md#blocking-long-press-actions)
defines the required action hook.

## Controller availability

Build or refresh the model after input acquisition and `gl_update`.
With no connected selected controller, settings/actions report `available=0`.
The supplied panel and native demo hide the source, tabs, settings and footer
actions until a connected non-companion endpoint belongs to
`gl_get_selected_device`. Keep controller status visible, with language and
close/back controls usable. On reconnection, restore the same tab and settings.

For a connected controller without a gyro-capable endpoint in its physical group,
Activation Mode is disabled. A bound motion companion counts; an unrelated device
does not. Temporary sensor silence does not erase declared gyro capability,
although actual output still requires fresh motion. Configuration APIs remain
usable without hardware so the mod can set defaults before acquisition starts.

Effective values adapt to available controls without rewriting preferences:

| Saved choice | Current capabilities | Displayed/effective choice |
|---|---|---|
| Stick or Touchpad | Stick only | Stick |
| Left or Right / Left and Right | Left only | Left |
| Either-side contact | Single central touchpad | On |
| Explicit Right | No right control | Off |
| Combined input | Neither member usable | Off |

A central touchpad offers Off/On, not invented left/right controls. Flick requires
a separately identified right touchpad and a corresponding host suppression hook;
a single central surface does not qualify. Query choice metadata rather than
checking a controller name. Stored `GL_FLICK_BOTH` is displayed as Stick or Touchpad;
`GL_SIDE_EITHER` represents On for a single surface.

Only individual buttons are offered. Legacy button combinations remain readable
but unavailable for new selection. Verified contact aliases are hidden as buttons
and resolve to their dedicated family without changing the stored binding.
Reconnecting a fuller controller restores the original preference automatically.

## Controller and sensor selection

Enumerate connected endpoints, exclude `gl_is_motion_companion` endpoints and
deduplicate by physical ID for the controller list. Sensorless virtual gamepads
remain valid command controllers.

Show a separate sensor selector only when `gl_motion_sensor_needs_selection` is
true after update. Ordinary unique pairs associate automatically. For an explicit
choice, display `gl_get_motion_sensor` and call `gl_bind_motion_sensor`; sensor
ID 0 clears the association. Offer only endpoints passing
`gl_endpoint_motion_available`, including the controller's own default sensor.

Retain the bound sensor's name during a temporary interruption. Do not add
UI-specific pairing heuristics or switch to another controller. Bindings last
for the connected session and are requalified after removal/reconnection.
[Input acquisition](INPUT.md) defines the pairing policy.

## Help and notifications

Use the setting description as an overview and
`gl_choice_description(context, setting_id, choice_value)` for individual options.
Show help on hover or keyboard/gamepad focus, wrapped to the viewport and font
size. A view description belongs only to `gl_menu_tab.description`; do not repeat
it in every setting tooltip.

### Notifications

A menu can query every frame or invalidate cached widgets through `gl_poll_event_ex`:

| Event | Refresh |
|---|---|
| `GL_EVENT_SETTING` | The complete setting ID, including affected descendants |
| `GL_EVENT_DEVICE` | Device list/capabilities: detail 0/1 endpoint changes, 2 selection, 3 effective capabilities |
| `GL_EVENT_BUTTON_LABELS` | Names: detail 0..31 button ordinal; −1 all labels for that endpoint, including triggers |
| `GL_EVENT_HOST_CAPABILITIES` | Availability dependent on host hooks |
| `GL_EVENT_CONTEXT` | View/model metadata; details below |
| `GL_EVENT_CALIBRATION` | Status and begin/cancel action visibility |

DEVICE details 2/3 are not tied to one endpoint. CONTEXT detail 0 reports runtime
view state, 3 destination changes, 6 inheritance metadata, 7 recommendations and
8 the winning view after update (`value` is its ID, or 0). Registration/removal
also invalidates the model. Changing the global destination emits detail 3 for
each view that follows it, including inactive views; explicit destinations keep
precedence. Identical repeated states/labels do not generate redundant events.

Copy borrowed metadata you retain. The [API event contract](API.md#events)
explains queue limits and legacy polling. Requery visibility after actions:
manual calibration exposes Begin at rest and Cancel throughout countdown,
collection and movement retries. Begin stays disabled without a usable SDL source.
The [motion guide](ADVANCED_MOTION.md#calibration) covers calibration policy/status.

## Static ImGui panel integration

The static frontend owns neither renderer nor ImGui context. Its ImGui headers,
implementation and configuration must match the host. If the host uses another
version, rebuild against one version or isolate the full implementation, symbols,
allocators and backends. A second context alone does not isolate incompatible ABIs.
The autonomous [DX12 frontend](OVERLAY.md) already provides that isolation.

1. Create the ImGui context, platform/renderer backends and scalable font. The demo
   uses `io.Fonts->AddFontDefaultVector()`; retain its font notices.
2. Enable keyboard and gamepad navigation.
3. Create the panel after settings initialization with `gl_panel_create(context, NULL)`.
   Forward UI events to the ImGui backend and F1..F24 events to
   `gl_panel_function_key(panel, number, down, repeat)`. The number is 1..24,
   not a platform keycode. Only the configured key's initial press toggles it.
4. Run backend NewFrame and ImGui NewFrame, call `gl_panel_draw` with logical
   viewport size and display DPI scale, then render its draw data.
5. Honor `io.WantCaptureKeyboard/Mouse` in local gameplay routing. Continue
   observing real game state; panel visibility gates gyro/flick.
6. Destroy the panel before its backends, ImGui context and library are unloaded.

The panel scales automatically for DPI/resolution with a small-window fit limit.
It leaves no global font/style scaling behind. Calibration progress and save
errors add status only when present; controls remain accessible while profiles
scroll. Footer rows wrap when localized labels need room.

When GyroLib owns SDL acquisition, put ImGui's SDL backend in manual gamepad mode.
Clear its borrowed handle list before polling the reader, then provide the
selected reader-owned handle before backend NewFrame. This avoids opening a
late-connected controller before acquisition with sensors disabled. See
`demo_ui_gamepad` in `examples/sdl_demo_input.hpp`. If the host owns the handle,
it must enable its sensors.

---

Previous: [Inheritance and recommendations](INHERITANCE.md) · Next: [Distribution](DISTRIBUTION.md)
