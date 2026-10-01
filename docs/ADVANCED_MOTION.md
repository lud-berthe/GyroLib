# Motion processing and advanced integration

The F10 panel and the host-owned pause menu expose the same settings. Small + / -
buttons to the left of Smoothing, Acceleration and Flick Stick expand their own options
directly below the corresponding row. Groups open independently in each view;
there is no combined Advanced section. The expanders and their children are
hidden while the corresponding feature is Off. Retained expansion/preferences
return when it is enabled again. Flick's options start with Spin Duration and
only show stick/pad thresholds for the selected input (Either exposes both).
Indented branch lines link child settings to their parent. Capability and camera/cursor restrictions
still apply. Recenter is an ordinary camera row after Flick Stick.
Native adapters can use `gl_setting_advanced_group(id)` (NONE, SMOOTHING,
ACCELERATION, FLICK, HOLD_DISABLE; MODIFIERS is a reserved legacy group).
Hold to disable has its own + for temporary inversion and
trackball; the other activation modes do not expose that group.
`gl_setting_is_advanced(id)` is true for these groups;
recenter and optional zoom are ordinary rows. These hints do not affect processing.
Each setting still has the normal localized metadata, events and automatic INI saving.
Cursor views never expose flick, camera recenter or zoom compensation.

## Filtering and acceleration

The pipeline is: calibrated angular velocity -> selected gyro space -> adaptive
smoothing -> tightening -> acceleration -> per-axis inversion -> optional host
zoom compensation -> angular camera/cursor deltas. Flick is added afterwards.

`gyro.smoothing_ms` retains its 0..500 ms range and 5 ms step. Zero bypasses it.
Milliseconds are the exponential response time constant, not a fixed added delay
or a sample count: a slow step reaches about 63% after one time constant. This
unit stays meaningful at different update rates; fast inputs still bypass it.
`gyro.smoothing_threshold_dps` defaults to 5 deg/s. Below half this threshold the
whole input is filtered; above it new input is immediate. Intermediate speeds
split continuously between direct and filtered paths. The filtered state always
drains, including while fast inputs pass through, to preserve slow displacement.
The exponential coefficient remains `-expm1(-dt/tau)`. We integrate its analytic
interval mean, rather than the endpoint approximation, to conserve angular
displacement at variable sample rates. Activation and view transitions clear
filter history, without resetting orientation or calibration.

`gyro.tightening_dps` defaults to zero (off), range 0..10 deg/s. Below a nonzero
threshold the vector is scaled by speed/threshold. Nonzero input remains nonzero:
this is not a hard gyro dead zone. It changes the low-speed response intentionally.

Acceleration Off/Low (1.5x)/Medium (2x)/High (3x) are presets that populate
`gyro.fast_sensitivity_x/y` from the current main sensitivities, and set the start
and full speeds to 5 and 75 deg/s. Off copies the main sensitivities to the fast
values. Custom (value 4) is a derived current-value label after a detailed edit,
not a selectable menu preset. Its choice metadata remains addressable for the
label but has `available=0`; the numeric API and saved files retain value 4.
All choices use the same curve, including Custom, with no
separate preset algorithm. The curve uses main X/Y sensitivities for slow input,
`gyro.fast_sensitivity_x/y` for
fast input, and interpolates using `gyro.slow_threshold_dps` and
`gyro.fast_threshold_dps` (defaults 5 and 75 deg/s). Equal or reversed thresholds
produce a defined step at the slow threshold. Choose increasing thresholds for
a gradual curve. Acceleration now measures the filtered/tightened vector.
All sensitivities are absolute ratios. Main sensitivities remain 0..20. Fast
sensitivities cover 0..60 in 0.05 steps so High at a 20x base remains exactly 60x;
old preset output is not clipped by the editor. Changing the main sensitivities
while a preset is selected refreshes its fast values and retains its selection.
Editing any of the four detailed acceleration values selects Custom; an
unchanged value does not. Selecting a preset again replaces those values. Each
operation emits changes for the affected setting IDs and saves one complete
configuration. Additional controls are shown for enabled presets or Custom.
The numeric settings API can still edit saved curve values while the preset is Off.
With acceleration selected, the main rows are labeled Slow Sensitivity X/Y.

## Flick options

The existing `flick.mode` selects Off, Stick, Touchpad or Either when supported.
Advanced options apply equally to stick and right-touchpad processing:

| Key after `context.<id>.` | Default | Meaning |
|---|---|---|
| `flick.duration_ms` | 150 | Spin duration, 0..1000 ms; first expanded Flick option |
| `flick.style` | 0 | 0 pivot + circle, 1 pivot only, 2 circle only |
| `flick.smoothing_ms` | 30 | Small circular changes are smoothed; 0 bypasses |
| `flick.smoothing_threshold_dps` | 45 | Circular speed above which new input is direct, deg/s |
| `flick.stick_release_threshold` | .65 | Stick returns inside this radius to rearm |
| `flick.stick_start_threshold` | .9 | Stick reaches this radius to start a flick |
| `flick.touchpad_release_threshold` | .2 | Right-pad contact returns inside this radius to rearm |
| `flick.touchpad_start_threshold` | .35 | Right-pad contact reaches this radius to start a flick |
| `flick.snap` | 0 | 0 free, 1 nearest 90-degree direction, 2 nearest 45-degree direction |
| `flick.snap_strength` | 1 | Fraction applied toward the snapped direction, 0..1 |
| `flick.forward_deadzone_degrees` | 0 | Initial directions within +/- this angle count as straight ahead |
| `flick.duration_exponent` | 0 | Actual duration = configured duration * (abs(pivot)/180)^exponent |

Snapping and forward tolerance affect only the initial pivot; circular motion
still uses the actual stick/pad angle. Exponent 1 halves the duration of a 90-degree
pivot compared with a 180-degree pivot. The default remains 150 ms and cubic
ease-out. Circular smoothing has its own filter and does not use gyro smoothing.
Its threshold is angular speed over actual control-report time, independent of
the rendering cadence. The core queues up to 512 reports per endpoint and
processes them chronologically; providers should submit every available report.
The slow portion is an angular displacement distributed exponentially over time.
Releasing the stick/pad completes the remaining angle, preserving the intended
turn. This may produce a small final movement with high smoothing. Safety,
view/source transitions discard the tail. Initial-pivot timing still advances
between reports. Held-stick resume behavior remains unchanged. Start must be
greater than release: setters adjust its paired threshold, while invalid INI
pairs are rejected transactionally. The legacy degree-limit key remains readable
but hidden; schema 13 converts its value to deg/s with a 30 Hz reference.

## Temporary gyro modifiers

Both menus place temporary inversion and trackball under the + beside Activation
Mode when Hold to disable is selected. Each is an optional checkbox, disabled by
default. They share the existing activation buttons, touch contacts, stick
deflection and triggers; there is no additional command assignment. While an
activator is held, checked behaviors replace the normal gyro suspension. With
neither checked, the hold still disables gyro. Other activation modes retain
these preferences but do not apply them. Both can be combined.

| Key after `context.<id>.` | Default | Meaning |
|---|---|---|
| `activation.temporary_invert` | 0 | Invert gyro while an existing activator is held |
| `gyro.temporary_invert_axes` | 2 | 0 horizontal, 1 vertical, 2 both |
| `activation.trackball` | 0 | Retain gyro velocity while an existing activator is held |
| `gyro.trackball_axes` | 2 | 0 horizontal, 1 vertical, 2 both |
| `gyro.trackball_decay` | 1 | Half-lives per second; 0 preserves speed |

Trackball remembers the preceding velocity using a 40 ms exponential average,
then integrates its decay analytically at host update intervals. Unselected axes
continue their normal gyro processing. Focus/source/view/safety transitions clear
momentum. A held modifier never overrides gyro disabled, pause or calibration.
Temporary inversion combines with the saved axis inversion and precedes optional
zoom. Stick activation uses the ordinary activation family and its release
hysteresis. There is no separate right-stick effect setting.
The host can supply these modifiers and an activation override through the
additive C API; it retains full ownership of game actions.

## Lean spaces

Public/persisted IDs 7 and 8 add Player Space Lean and World Space Lean. IDs 0..6
are unchanged; Player Space stays the default. Lean projects rotation onto the
gravity-relative roll axis. Player Lean uses local pitch and a 1.15 roll relaxation
factor; World Lean uses gravity-relative pitch. Both fade horizontal response
near a side-on singularity. These are additional Jibb-inspired options, not new
claims about Steam Input conversion categories or measured equivalence.

## Host camera actions

Register `gl_set_recenter_callback(context, callback, user)` only when the adapter
can center its own camera vertically. This reveals `camera.recenter_button`,
with the same controller-specific button labels as activation. Default 0 is off.
The DLL observes the configured button; it does not suppress that button's game
action. The host can call `gl_request_recenter` for a keyboard/game action too.
Requests run after the next update's camera delta, on the context owner thread,
and are discarded while unfocused, paused, in menus or on a cursor view. Holding
a button through a focus/view/controller transition cannot produce an extra edge.
No gyro orientation reset, game aiming action or OS input is involved.

For optional zoom scaling:

```cpp
gl_set_gameplay_context_zoom_available(gyro, aim_view_id, 1);
// Before EACH gl_update, on the same owner thread:
gl_set_gameplay_context_fov(gyro, aim_view_id, actual_vertical_fov, unzoomed_vertical_fov);
```

Both FOVs are finite degrees strictly between 0 and 179. The declaration reveals
`gyro.zoom_compensation`, off by default. Enabling it scales gyro deltas by
`tan(currentFov/2) / tan(referenceFov/2)`; it never scales flick. Missing/invalid
reports or withdrawn support bypass scaling immediately. The saved per-view
sensitivities remain unchanged. Use matching vertical projections and avoid
applying another zoom scale in the camera hook. Changes in magnification do not
constitute an aim command and never switch views themselves.

The demo supplies the recenter hook. Home or the configured controller button
recenters the camera (R3 is the demo's initial binding). It does not declare zoom
support or expose compensation: its separate standard/sniper views have their
own sensitivities. The optional zoom API remains available for hosts with
variable magnification within a view. Old saved demo zoom values are preserved
but have no effect without declared host support and a fresh FOV report.

## Conservative automatic calibration

Manual calibration is unchanged: five-second placement countdown followed by
one second of accepted stillness windows. Steam samples receive no local bias.
Menus-only calibration still excludes the active inventory cursor.

While gyro output is active outside a safe menu, automatic calibration now accepts
only window means within 0.15 deg/s (vector magnitude) of the existing bias, and
uses an eight-second correction time constant. Elsewhere it uses the existing
two-second constant and noise-tolerant stillness detector. Larger drift during
play requires manual calibration or a safe calibration interval. The synthetic
2 deg/s steady-yaw case no longer gets absorbed while aiming. Very slow motion
below the guard remains inherently ambiguous; disabling auto-calibration is still
available and remains the default.

An adapter that knows the player is tracking a target can call
`gl_set_auto_calibration_allowed(context, 0)` before each affected update.
This is a per-update veto, automatically released after update; it does not
cancel manual calibration or attempt to control Steam calibration.

## Compatibility and validation

Schema 15 preserves existing IDs, unknown keys and registered/retired
view settings from schemas 1..14. Legacy `gyro.look_stick_effect` is retired:
it has no processing effect, is hidden, cannot be set through the API and is
omitted on save. Stick activators and explicit host activation overrides remain.
Schema 14 replaced legacy `gyro.temporary_invert_button` and
`gyro.trackball_button` assignments with the corresponding enable flags;
existing activation commands, axes and decay remain unchanged. The retired
bindings are hidden, cannot be set through the API and are omitted on save.
Their behavior is now limited to Hold to disable. Older acceleration presets are materialized
into their detailed values, preserving their output; Custom curves are retained.
Schema 12 files whose detailed curve differs from the indicated preset are
loaded as Custom (including manual INI edits). New options use their defaults. Existing
nonzero smoothing now uses the adaptive policy; the saved duration is unchanged.
Schema 16 replaces the enable checkbox with Activation Mode Off, retaining gyro
preferences. Legacy disabled profiles migrate to Off; the old enable key is no
longer written. Old loaders reject schema 16 rather than overwriting new settings. C ABI 1 structs
remain unchanged; the extension consists of new functions and enum values.

`advanced_motion` checks analytic time response, variable intervals, displacement,
tightening, custom acceleration, Lean poses, flick modes/snapping/smoothing,
calibration guards, camera callback gates, fresh FOV handling, metadata and migration.
Core, native-menu, F10-help, combat and acquisition regressions run alongside it.
These are synthetic/software checks, not a new real-controller acceptance test.
Physical micro-aim, Steam circular smoothness, drift and end-to-end latency still
need user testing. Linux/Proton remains untested.

Algorithms were independently implemented from the mathematical descriptions in
Jibb Smart's [gyro guide](https://gyrowiki.jibbsmart.com/blog:good-gyro-controls-part-1:the-gyro-is-a-mouse),
[flick guide](https://gyrowiki.jibbsmart.com/blog:good-gyro-controls-part-2:the-flick-stick)
and [space guide](https://gyrowiki.jibbsmart.com/blog:player-space-gyro-and-alternatives-explained).
Existing GamepadMotionHelpers MIT code and notices remain intact.
