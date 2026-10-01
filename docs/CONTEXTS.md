# Host-defined view profiles

GyroLib has no built-in Aim or Alt-Fire action. The host registers its own named
view modes with permanent nonzero uint32 IDs. Every mode gets an independent
settings profile and menu tab. The highest-priority available, active mode selects
the **whole profile**; ties use the lowest ID. The host declares its normal view
as a mode too, even when it is the only camera. Without a valid active observation, gyro,
flick and input filtering stop; calibration and orientation tracking continue.
Looking at a menu tab only edits settings; it never changes
gameplay state or selects the processing profile.

## Registration and observation

```cpp
constexpr uint32_t Explore = 101, Aim = 205, Inventory = 309;
const gl_gameplay_context modes[] = {
    {Explore, "Exploration", "Free third-person camera.", 10},
    {Aim, "Aiming", "The resolved aim command is held.", 20},
    {Inventory, "Inventory cursor", "The inventory is open.", 30}
};
for (const auto& mode : modes)
    check(gl_register_gameplay_context(gyro, &mode));
check(gl_set_gameplay_context_output_target(gyro, Explore, GL_OUTPUT_CAMERA));
check(gl_set_gameplay_context_output_target(gyro, Aim, GL_OUTPUT_CAMERA));
check(gl_set_gameplay_context_output_target(gyro, Inventory, GL_OUTPUT_CURSOR));
check(gl_setting_set(gyro, "context.205.sensitivity_x", 1.0));
check(gl_setting_set(gyro, "context.205.sensitivity_y", 1.0));
check(gl_setting_set(gyro, "context.101.flick.mode", GL_FLICK_ON));

// Before EACH gl_update, from resolved game commands, without changing them:
check(gl_set_gameplay_context_state(gyro, Explore, !aim && !inventory, state_known));
check(gl_set_gameplay_context_state(gyro, Aim, aim && !inventory, state_known));
check(gl_set_gameplay_context_state(gyro, Inventory, inventory, state_known));
gl_update(gyro, now_ns, &host, &output);
uint32_t processing_mode = gl_get_active_gameplay_context(gyro); // 0 = no active named mode
```

Registration copies strings. Re-register the same ID to rename/localize a mode or
change its priority; settings and observations survive. Unregistering hides the
tab but retains saved values. IDs must never come from translated labels or list
positions. There is no small hard-coded mode count limit.

Report active and available as 0 or 1. Available means the observation is valid
even if currently inactive. Missing reports expire at the next update. An
unavailable mode cannot select a profile; another valid active mode applies, or
output is suspended. The library never infers aiming from animation, and never issues aim
commands. All calls use the context's serialized owner thread.

## Independent settings

Each named profile includes gyro enable, absolute X/Y sensitivity, inversion,
all nine spaces and local-axis parameters, activation and its control families,
stick threshold, optional short-press filtering, smoothing, acceleration,
flick enable and pivot duration. Calibration and UI settings remain shared.

Sensitivity IDs remain `context.<id>.sensitivity_x/y` for compatibility.
Other IDs append the base key, e.g. `context.205.gyro.invert_x`,
`context.101.activation.button`, `context.101.flick.duration_ms`.
New profiles use library defaults (sensitivity 2.5, Player Space, gyro enabled,
Always on, flick off); hosts may set their initial values before loading a file.
There is no unnamed Camera profile. Bare gyro/activation/flick setting IDs now
return `GL_UNAVAILABLE` from setters/getters; use the registered view's ID.
Static metadata slots are retained but hidden for ABI stability. Old INI values
are read only for migration, then omitted from subsequent saves.

Declare each destination with `gl_set_gameplay_context_output_target` after
registration. This integration metadata is not a player preference and is not
saved in the settings file. The winning mode selects its destination automatically.
A cursor profile hides flick and pivot duration, ignores any old saved flick On
value, never suppresses native stick rotation, and never calls the camera callback.
It still requires reliable host menu observation and the usual focus/pause gates.
Undeclared legacy modes continue using the dynamic `gl_set_output_target` API.

Within a tab, activation choices are Off, Always on, Hold to disable, Hold to enable
and Toggle. Off stops gyro while preserving independent flick processing.
Always on hides and ignores all activators and short-press filtering;
switching to another activation mode restores the saved bindings.
Flick choices select Off, the right stick, the right touchpad or both physical
controls, when the host can suppress their native camera output. There is no
"during/outside selected mode" choice. The existing global conditional enum values
and selectors remain deprecated API compatibility only; menus do not offer them.
Context-specific setters reject those conditional values.

Capabilities still control availability: flick requires suppression for each
selected native input, short press requires a filter integration, and controls appear only
when provided by the selected physical controller. Each tab uses its own space
and stick-binding settings for dependent-row visibility. An unavailable host
observation leaves the tab visible with disabled settings.

Mode changes apply on the next update without resetting orientation or bias.
Toggle's live on/off state is shared across all views using Toggle, including
cursor views. Switching through a non-Toggle view does not change that state.
It starts on when the library context is created and is not a saved preference.
Entering a mode with a held button creates no toggle edge. A short-press hold
cannot emit a delayed game action after a mode
transition. Returning to a flick-enabled mode with a deflected stick skips the
initial pivot and permits circular rotation immediately. Focus/menu interruptions
and explicit flick setting changes still require neutral before a pivot.

## Shared menu tabs and strings

Use `gl_menu_tab_count` / `gl_menu_tab_at`, then
`gl_menu_tab_setting_count` / `gl_menu_tab_setting_at`.
Tab IDs are stable **uint64** values: mode ID + 1 for named
profiles. The maximum uint32 mode ID therefore has tab ID 4294967296.
`gl_menu_tab.context_id` remains the original host ID;
`.active` indicates the processing profile, not the tab being edited.
Zero registered views means zero tabs. ID 1 (`GL_TAB_CAMERA` / `GL_TAB_DEFAULT`)
is retired and never enumerated; querying its settings returns zero rows.
Shared calibration and reset-all actions are enumerated separately
with `gl_menu_shared_setting_count` / `gl_menu_shared_setting_at`. There is no
General tab; legacy tab-0 getters still alias shared controls. The F10 frontend
places language in its title bar, controller selection above the view tabs, and
calibration/reset controls in one compact footer row. Scaling is always automatic;
the retired scale row is hidden and its old saved value is discarded on migration. Edits
persist automatically to the configured path; there is no Save widget. Respect action visibility to swap
Recalibrate for Cancel throughout manual calibration, including movement retries.

Setting IDs, bounds, types, defaults, descriptions and choice APIs are identical
for native widgets and F10. Tab rows use concise labels; flat enumeration remains
available and prefixes named-mode row labels for older menu consumers.
Use choice.value, never its list index. `GL_EVENT_SETTING` identifies edited keys;
rebuild metadata on `GL_EVENT_CONTEXT` / `GL_EVENT_HOST_CAPABILITIES` and update
control labels on `GL_EVENT_BUTTON_LABELS`.

Metadata strings are borrowed until registration/removal, language change or
destruction. Copy strings that you retain. The host localizes its tab names by
re-registering the same IDs; library labels localize independently.

## Persistence and migration

The current format writes only shared language, menu shortcut and automatic calibration,
plus `context.<id>.*` profiles and any preserved unknown extension keys. The schema
number is format metadata. Per-view values survive temporary unregistration.
Global gyro/activation/flick keys, old mode selectors, `ui.scale` and the old
migration template are no longer emitted. Existing named profiles retain their
values; unused global values in schema 6..9 files never override them.

Declare all current views and their host defaults before initialization/loading.
An old implicit-only config with no named profile keys can migrate only when
exactly one view is registered. Otherwise loading returns `GL_UNAVAILABLE`
transactionally; initialization also disables auto-save and leaves the file intact.
The host can register its intended camera first, migrate once, then register other
new views. The library never guesses a destination for an unlabelled old profile.

Earlier schemas remain readable. Old combined Always-on mode with
bindings or a short-press filter migrates to Hold to disable (5); without these it
becomes Always on (0). New Always-on profiles retain but ignore their bindings.
Their mode sensitivity IDs and values are retained;
global gyro/flick preferences are copied once into each mode as independent
values. Missing legacy mode axes default to 2.5, as in the previous schema.

Legacy conditional activation is converted by the referenced stable mode ID:
"only X" enables X and disables other profiles; "outside X" does the reverse.
Flick conditions similarly become per-profile On/Off. This matches mutually
exclusive view modes. If the old mod reported overlapping command states, the
new winning-profile model can change their combined behavior: review those
profiles after migration. Missing legacy selectors leave the affected feature
disabled. Old aim/Alt-Fire multipliers remain inert preserved data.

Pre-schema-6 shared settings and an old migration template are materialized into
the views registered at migration time and profile IDs already present in the file.
Views introduced later start with defaults; no hidden template is saved. Reset-all
resets every stored profile, including retired modes, but keeps language/shortcut.
`gl_load_settings` remains read-only. `gl_initialize_settings` additionally rewrites
a valid older file in the current schema after migration; current files are only read.
Malformed/newer files are refused transactionally; unknown keys survive.
Older DLLs refuse newer schemas instead of overwriting them. See [MENUS.md](MENUS.md) for atomic save
and ownership requirements.
