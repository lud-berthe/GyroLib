# Inheritance and recommended settings

[Documentation](INDEX.md) / Integration

A view can inherit settings from one other view. Parent changes reach descendants
unless the player has overridden that setting. Chains are supported; cycles and
self-links are rejected. Game observations still determine the active view.

Only player settings are inherited. Output destinations, callbacks, menu
permissions, FOV reports and capabilities belong to the host's view declaration.
A cursor view therefore excludes flick even if its parent enables it.

## Set up a recommended profile

Register all views and destinations, configure defaults, capture recommendations,
then load the player's file:

```cpp
// Views 101 (Exploration), 205 (Standard aim), 206 (Sniper) are registered.
check(gl_set_context_parent(gyro, 205, 101));
check(gl_set_context_parent(gyro, 206, 205));
check(gl_setting_set(gyro, "context.205.sensitivity_x", 1.0));
check(gl_setting_set(gyro, "context.205.sensitivity_y", 1.0));
check(gl_setting_set(gyro, "context.206.sensitivity_x", 0.5));
check(gl_setting_set(gyro, "context.206.sensitivity_y", 0.5));
check(gl_capture_recommended_settings(gyro));
check(gl_initialize_settings(gyro, nullptr, nullptr));
```

`check` handles API errors. Calls use the context owner thread. The demo's
`examples/tps/host.hpp` contains the complete setup.

Capture copies view settings, parent links and automatic calibration into a
context-owned snapshot. It neither applies nor saves it. Capture once after host
defaults, before loading player settings. A new mod release can offer different
recommendations without replacing the player's INI on startup.

| Action | Effect |
|---|---|
| `settings.recommended` / `gl_apply_recommended_settings` | Restores the captured profile and its links; appears only after capture |
| `settings.reset` | Removes inheritance and restores library defaults, including stored unregistered views |

Both actions autosave. Neither changes language, shortcut, controller selection,
game observations or calibration bias. Reset keeps the recommendation snapshot.
Views created after capture keep their own settings, although a restored ancestor
can change values they inherit.

## Edit links and exceptions

| API | Behavior |
|---|---|
| `gl_can_inherit_context` | Checks a candidate without modifying state; inactive registered views are valid parents |
| `gl_set_context_parent(view, parent)` | Links an independent view to the parent; switching parents keeps existing exceptions |
| `gl_set_context_parent(view, 0)` | Detaches and freezes effective values |
| `gl_setting_set` | Creates a local exception, even if the value equals the inherited value |
| `gl_setting_inherit` | Removes the exception for that setting |
| `gl_setting_inheritance` | Reports parent, source view, override state and parent value |

Linking an independent view replaces its values with inherited ones. Removing a
registered parent detaches its direct children and freezes their values; further
descendants retain the resulting values. Parent edits never rewrite child storage.

`gl_setting_get` resolves inheritance. Menu values and `gl_setting_get_effective`
also apply controller fallback, which does not create overrides. Each inline
checkbox or threshold has its own inheritance state.

## Acceleration curves

Choosing a preset overrides its whole curve. Restoring inheritance on
`gyro.acceleration` clears all of that curve's exceptions. Editing one advanced
component selects Custom; a composed inherited curve also displays Custom if it
no longer matches a preset. Off always disables acceleration.

Changing a base sensitivity under an inherited preset derives the corresponding
fast sensitivity without creating a hidden advanced override. An explicitly
selected local preset updates its local fast values when base sensitivity changes.
An explicitly edited advanced component remains independent.

Entering Custom preserves the curve currently in use. Derived values that would
otherwise revert to different parent values become local exceptions; unchanged
components already equal to the parent remain inherited. These exceptions are
included in saves and recommendations. Saved exceptions remain explicit until the player restores inheritance.

Release/start thresholds can combine local and parent values. Resolution keeps
the start at least 0.05 above release without rewriting either stored value.
Inheritance changes do not reset orientation or start calibration. Changed active
bindings re-prime activation/filter edges just as ordinary edits do.

## Native menu integration

Place a parent selector below the view tabs. The supplied frontends use a chain
icon for inherited values and a restore arrow for local exceptions. A disabled
hardware-dependent setting keeps its inheritance metadata. See
[menu availability](MENUS.md#controller-availability).

Use `GL_EVENT_SETTING` for changed effective values, including descendants.
`GL_EVENT_CONTEXT` detail 6 invalidates inheritance metadata even when the numeric
value is unchanged; detail 7 reports recommendation availability/replacement.
A host can also query the model each frame. The DX12 frontend carries these edits
through its owner-thread command queue.

## Persistence

Independent profiles are saved in full. An inherited view stores its parent ID
and local exceptions:

```ini
context.206.inherit=205
context.206.sensitivity_x=0.5
context.206.sensitivity_y=0.5
```

When a partial INI selects an acceleration preset, omitted curve components are
derived from that preset. A component explicitly restored to its parent is saved
with `inherit` so reloading does not recreate a local value:

```ini
context.206.gyro.slow_threshold_dps=inherit
```

This marker requires a nonzero `context.206.inherit` parent in the same file.
Numeric values remain overrides. Loading never replaces the author's captured
recommendations.

Cycles, malformed parent IDs and references to absent profiles reject the load
without changing settings. An unregistered view can still supply parent values
if its saved profile exists. [Settings](SETTINGS.md) covers file handling;
[versioning](VERSIONING.md) defines supported formats.

---

Previous: [Settings and INI](SETTINGS.md) · Next: [Menus](MENUS.md)
