# View profiles

[Documentation](INDEX.md) / Integration

The mod defines its views: exploration, weapon aim, inventory cursor, or any other
mode the game needs. The API calls a view a gameplay context. Each has a
permanent nonzero uint32 ID, a name, a
settings profile and a menu tab. There is no built-in Aim, Alt-Fire or unnamed
camera profile.

Before every update, report which views are active and whether those observations
are reliable. The highest-priority available active view supplies the whole
profile; the lowest ID breaks a tie. Missing observations expire on update.
Without a valid active view, movement and input filtering stop, while orientation
tracking and calibration continue.

## Register and report

```cpp
constexpr uint32_t Explore = 101, Aim = 205, Inventory = 309;
const gl_gameplay_context views[] = {
    {Explore, "Exploration", "Look around while exploring.", 10},
    {Aim, "Aiming", "Aim your weapon.", 20},
    {Inventory, "Inventory cursor", "Move the inventory cursor.", 30}
};
for (const auto& view : views)
    check(gl_register_gameplay_context(gyro, &view));
check(gl_set_gameplay_context_output_target(gyro, Explore, GL_OUTPUT_CAMERA));
check(gl_set_gameplay_context_output_target(gyro, Aim, GL_OUTPUT_CAMERA));
check(gl_set_gameplay_context_output_target(gyro, Inventory, GL_OUTPUT_CURSOR));
check(gl_setting_set(gyro, "context.205.sensitivity_x", 1.0));
check(gl_setting_set(gyro, "context.205.sensitivity_y", 1.0));

// Before each update, using the game's resolved commands:
check(gl_set_gameplay_context_state(gyro, Explore, !aim && !inventory, state_known));
check(gl_set_gameplay_context_state(gyro, Aim, aim && !inventory, state_known));
check(gl_set_gameplay_context_state(gyro, Inventory, inventory, state_known));
check(gl_update(gyro, now_ns, &host, &output));
```

This is an integration fragment; `check` handles result codes and the host supplies
command observations and `gl_host_state`. Use 0 or 1 for active/available.
Available means the observation is valid, even when inactive. Read commands before
animation delays so aim transitions affect sensitivity immediately.

Registration copies strings. Re-registering an ID changes its name, description
or priority while preserving settings and observations. Unregistering removes
the tab but retains saved values. Use fixed IDs, never translated names or list
positions. The API has no small fixed limit on the number of views.

## Settings per view

View keys begin with `context.<id>.`, for example:

```text
context.205.sensitivity_x
context.205.gyro.invert_x
context.205.activation.button
context.205.flick.duration_ms
```

New views default to Player Space, X/Y sensitivity 2.5, Always on and Flick Off.
Apply mod defaults before loading the player's settings. Profiles can
[inherit another view](INHERITANCE.md); calibration, language and the shortcut
are shared preferences.

Activation Mode offers Off, Always on, Hold to disable, Hold to enable and Toggle.
Off stops gyro but leaves independent flick processing available. Always on
ignores activators and long-press blocking while retaining their saved bindings.
Bare gyro/activation/flick setting IDs return
`GL_UNAVAILABLE`; use a registered view's keys.

## Camera and cursor destinations

Declare the destination after registration. It is host metadata, not a saved
player setting. The winning view selects it automatically. Views without an
explicit destination follow the legacy dynamic `gl_set_output_target` default.

| Destination | Output and gates |
|---|---|
| Camera | Camera callback or returned angular deltas; requires `camera_allowed`, focus, no pause and a closed settings panel |
| Cursor | Returned angular deltas for local UI; requires an observed open menu, `GL_HOST_MENU_STATE`, focus, no pause and a closed settings panel |

Cursor views exclude flick, native camera-stick suppression, recenter and zoom
compensation. A saved flick preference remains intact but has no effect there.
The host maps cursor angles to UI coordinates and handles selection. It must
continue reporting the actual menu state.

Camera output normally stops in menus. For a movable background camera, call
`gl_set_gameplay_context_camera_in_menu(gyro, view_id, 1)` and declare reliable
`GL_HOST_MENU_STATE` support. Ordinary camera gates still apply. This opt-in
survives metadata re-registration and is cleared when the view is unregistered.
Long-press blocking remains disabled in menus. Menus-only calibration does not
treat an enabled menu camera as idle.

## Transitions

View changes apply on the next update without resetting orientation or bias.
Toggle's live on/off state is shared across all Toggle views, including cursor
views. It starts on, is not saved and survives a visit to a non-Toggle view.
Entering a view with an activator already held creates no toggle edge. Pending
taps cannot trigger a game action after a view transition.

Returning to a flick-enabled view while the stick is deflected skips the initial
pivot and allows circular turning immediately. Focus/menu interruptions and
explicit flick-setting changes require neutral before a new pivot.

## Menu tabs and saved profiles

A settings tab edits a view; it never activates it. Registered views remain
editable even when inactive, unavailable or not yet observed. `tab.active` and
`tab.available` describe runtime observations, not widget availability. Use each
setting's hardware/host availability metadata to disable widgets.

Tab IDs are uint64 values equal to view ID + 1. View 4294967295 therefore has tab
4294967296. With no registered views there are no tabs. Query shared settings
separately; [Menus](MENUS.md#enumerate-the-model) describes iteration and notifications.

View metadata strings are borrowed until registration/removal, language changes
or destruction. Copy retained strings. To localize a view, re-register its name
and description under the same ID.

Saved profiles for temporarily unregistered views are retained. Independent
profiles store their settings; inherited ones store a parent and exceptions.
See [settings](SETTINGS.md) for loading/saving and [inheritance](INHERITANCE.md)
for detachment, recommendations and per-setting overrides.

---

Previous: [Host integration](API.md) · Next: [Settings and INI](SETTINGS.md)
