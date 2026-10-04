# Motion processing

[Documentation](INDEX.md) / Reference

This guide explains each motion feature and the hooks it needs from the host.
Gyro, activator and flick keys use the `context.<id>.` prefix; calibration is
shared. [Gyro spaces](SPACES.md) defines projection, and [Menus](MENUS.md) covers
presentation.

- [Gyro pipeline](#gyro-pipeline)
- [Activation and modifiers](#activation-and-modifiers)
- [Blocking long-press actions](#blocking-long-press-actions)
- [Calibration](#calibration)
- [Smoothing](#smoothing)
- [Acceleration](#acceleration)
- [Flick stick and right touchpad](#flick-stick-and-right-touchpad)
- [Recenter camera](#recenter-camera)
- [Zoom compensation](#zoom-compensation)
- [References and tests](#references-and-tests)

## Gyro pipeline

```text
calibrated angular velocity → gyro space → smoothing → tightening
→ acceleration/sensitivity → inversion → optional zoom scale → angular deltas
```

Flick rotation is added separately. Output is already integrated in degrees.
Ordinary activation/view changes preserve orientation and calibration while
clearing filter history so old motion cannot leak into the new output.

## Activation and modifiers

Activation Mode offers Off, Always on, Hold to disable, Hold to enable and Toggle.
Buttons, triggers, touchpad/stick/grip contacts and analog stick deflection can
act as activators; enabled families combine with OR. Toggle's live state is shared
across views using Toggle. [View transitions](CONTEXTS.md#transitions) defines edges
and held-input behavior.

Hold to disable can replace normal suspension with either or both modifiers:

| Key | Default | Meaning |
|---|---|---|
| `activation.temporary_invert` | 0 | Reverse selected gyro axes while an activator is held |
| `gyro.temporary_invert_axes` | 2 | 0 horizontal, 1 vertical, 2 both |
| `activation.trackball` | 0 | Continue remembered gyro velocity while held |
| `gyro.trackball_axes` | 2 | 0 horizontal, 1 vertical, 2 both |
| `gyro.trackball_decay` | 1 | Half-lives per second; 0 keeps speed |

These use the existing activators, without another binding. Other activation
modes retain but ignore their preferences. Trackball averages preceding velocity
with a 40 ms exponential response and integrates decay at host update intervals.
Unselected axes retain ordinary processing. Focus/source/view/safety transitions
clear momentum. Neither modifier overrides Off, pause or calibration. Temporary
inversion combines with saved inversion before zoom compensation.

The host can also supply modifiers and an activation override through the C API;
it remains responsible for observing, rather than generating, game actions.

## Blocking long-press actions

This optional filter separates a short game action from a held gyro activator.
With a hold activation mode, an eligible individual button and
`activation.block_long_press=1`:

- Gyro reacts to the physical press immediately.
- A release before 200 ms returns `GL_EMIT_TAP`; the host emits the game action.
- Holding for 200 ms or longer suppresses that action.

The mod must route the relevant press/repeat/release events through
`gl_filter_event` and declare `GL_HOST_LONG_PRESS_BLOCKING` after wiring them.
A camera callback cannot implement this feature. Do not filter aim or Alt-Fire.
Suspend filtering for native interactions requiring a hold. The demo adapter in
`examples/tps/controller_actions.hpp` preserves keyboard actions, trigger commands
and UI navigation, and discards pending taps on focus loss or controller takeover.

## Calibration

Manual calibration gives five seconds to place the controller, then collects
one second of accepted stillness with at least 20 samples. Movement restarts
collection. Begin/Cancel actions and diagnostics expose countdown, stationarity
and progress; no debug display is imposed.

Automatic calibration defaults to Off. Any time needs no menu hook. Menus only
requires `GL_HOST_MENU_STATE` and a trustworthy `host.menu_open`. Without that
capability, the saved policy is retained but inactive, even with F10 open.

With menu observation available, game menus and the library panel are eligible
when still. An unpaused gyro-driven inventory cursor is excluded, even if its
activator is released. Pausing or opening the library panel suspends the cursor
and allows calibration again. An enabled menu camera is also excluded from idle
menu calibration.

Both paths use time-weighted gyro/acceleration statistics over 250 ms windows:

| Test | Limit |
|---|---|
| Average angular speed | Below 3 deg/s |
| Gyro noise | Below 0.9 deg/s RMS |
| Acceleration | Near 1 g; noise below 0.025 g RMS |
| Window mean relative to the initial stable window | Within 0.35 deg/s and 0.02 g |

Large impulses cancel collection immediately; stream gaps clear its window.
After one second of accepted windows, automatic bias correction uses a two-second
time constant outside active gyro use. During active use outside a safe menu,
only residual drift within 0.15 deg/s is accepted, with an eight-second constant.
The host can veto automatic learning for one update with
`gl_set_auto_calibration_allowed(context, 0)`; the veto then clears and does not
cancel manual calibration.

These checks do not add smoothing or a dead zone to output. Very slow intentional
rotation remains indistinguishable from bias in some conditions. Larger drift
requires manual calibration or a safe idle interval. Steam Input samples receive
no local bias correction; its internal calibration remains outside this API.

## Smoothing

`gyro.smoothing_ms` is an exponential time constant: 0..500 ms in 5 ms steps.
Zero bypasses both smoothing and its small-movement stabilization. A step reaches
about 63% after one time constant; this is not a fixed delay or a sample count.

The coefficient is `-expm1(-dt/tau)`. The processor integrates the analytic
interval mean to conserve displacement at variable sample rates.
`gyro.smoothing_threshold_dps` defaults to 5 deg/s: below half the threshold,
all input is filtered; above it, new input passes directly. Between those speeds,
the two paths blend continuously. The filter continues draining during fast motion.

`gyro.tightening_dps` defaults to 0, range 0..10 deg/s. Below a nonzero threshold,
it scales the vector by speed/threshold. It intentionally reduces small movement
without imposing a hard dead zone. Its saved value is retained while smoothing
is Off and resumes when smoothing is enabled.

## Acceleration

The main X/Y sensitivities are absolute ratios, from 0 to 20. When acceleration
is enabled they describe slow movement; fast X/Y sensitivities range from 0 to 60
in 0.05 steps. Speed is measured after smoothing and tightening.

| Preset | Fast sensitivity | Start / full speed |
|---|---|---|
| Off | Same as main sensitivity; acceleration bypassed | 5 / 75 deg/s |
| Low | Main × 1.5 | 5 / 75 deg/s |
| Medium | Main × 2 | 5 / 75 deg/s |
| High | Main × 3 | 5 / 75 deg/s |

The curve interpolates from main sensitivities to `gyro.fast_sensitivity_x/y`
between `gyro.slow_threshold_dps` and `gyro.fast_threshold_dps`. Equal or reversed
thresholds produce a step at the slow threshold; increasing thresholds give a
progressive curve.

Selecting a preset populates those four advanced values. Changing main
sensitivity under a preset refreshes its fast values. Editing an advanced value
selects Custom, preserving the rest of the effective curve; an unchanged edit
does not. Custom (value 4) is a current-value label, not a selectable preset.
Selecting a preset again replaces the curve. Setters can edit curve values while
Off, although its advanced menu is hidden.

Each operation emits affected setting changes and saves one configuration.
[Inheritance](INHERITANCE.md#acceleration-curves) explains local presets, derived
values and restoring curve exceptions.

## Flick stick and right touchpad

`flick.mode` offers Off, Stick, Touchpad or Stick or Touchpad according to actual
inputs and host suppression capabilities. A separately identified right touchpad
qualifies; a central single pad does not. The host must suppress native camera
rotation from each active flick input to prevent double movement.

| Key | Default | Meaning |
|---|---|---|
| `flick.duration_ms` | 150 | Pivot duration, 0..1000 ms, 5 ms steps |
| `flick.style` | 0 | 0 pivot + circle, 1 pivot only, 2 circle only |
| `flick.smoothing_ms` | 30 | Circular smoothing time; 0 bypasses |
| `flick.smoothing_threshold_dps` | 45 | Circular speed above which new input is direct |
| `flick.stick_release_threshold` | .65 | Radius inside which the stick rearms |
| `flick.stick_start_threshold` | .9 | Radius that starts a stick flick |
| `flick.touchpad_release_threshold` | .2 | Radius inside which the right pad rearms |
| `flick.touchpad_start_threshold` | .35 | Radius that starts a pad flick |
| `flick.snap` | 0 | 0 free, 1 nearest 90°, 2 nearest 45° direction |
| `flick.snap_strength` | 1 | Fraction applied toward the snapped direction, 0..1 |
| `flick.forward_deadzone_degrees` | 0 | Initial directions within ±this angle count as forward |
| `flick.duration_exponent` | 0 | Duration × (abs(pivot)/180)^exponent |

Snapping and forward tolerance affect only the initial pivot. Circular turning
uses the actual input angle. Exponent 1 halves a 90° pivot's duration relative to
180°. Pivots use cubic ease-out; duration 0 is immediate.

Circular smoothing has its own filter, using angular speed over control-report
time rather than render cadence. Providers should submit every report: the core
queues up to 512 per endpoint and consumes fresh reports chronologically, once.
Animation time advances between reports without replaying elapsed time when a
late report arrives. Stick and pad histories remain separate.

The slow circular portion is distributed exponentially. Releasing the input
completes its remaining angle, which can produce a small final movement at high
smoothing. Safety/view/source transitions discard that tail. Returning to a
flick view with the stick already deflected skips the initial pivot and permits
circular turning. Focus/menu interruptions and explicit setting changes require
neutral before another pivot.

Start must exceed release. Setters adjust the paired threshold; invalid independent
INI pairs are rejected. Inherited combinations are resolved with a minimum 0.05
gap. Circular smoothing thresholds use degrees per second.

Optional right-pad feedback is dispatched explicitly through
`gl_sdl_apply_feedback`. Its supported hardware and limitations are in
[input acquisition](INPUT.md#touchpad-feedback).

## Recenter camera

Register `gl_set_recenter_callback(context, callback, user)` when the mod can
center camera pitch. This exposes `camera.recenter_button`, default Off, using
controller-specific labels. The host may also call `gl_request_recenter` for a
keyboard or game action. The library does not suppress the button's normal action.

The callback runs after camera deltas on the owner thread. Requests are discarded
when unfocused, paused, on a cursor view, or in a menu unless that camera view
explicitly permits menu output. Holding a button through a focus/view/controller
transition cannot create an extra edge. Center the game camera, not gyro orientation.

## Zoom compensation

Declare variable zoom for a view and report its current projection before each update:

```cpp
check(gl_set_gameplay_context_zoom_available(gyro, aim_view_id, 1));
// Before every gl_update:
check(gl_set_gameplay_context_fov(gyro, aim_view_id, vertical_fov, reference_fov));
```

Both FOV values are finite degrees strictly between 0 and 179. The declaration
exposes `gyro.zoom_compensation`, default Off. Enabled, it scales gyro deltas by
`tan(currentFov/2) / tan(referenceFov/2)`, leaving flick and saved sensitivity
unchanged. Missing/invalid reports or withdrawn support bypass scaling immediately.
Use matching vertical projections and avoid applying another zoom scale in the
camera hook. Reporting FOV does not select an aim view.

The demo declares this only for Sniper: 20° reference and a second zoom at 10°.
Its recommended profile enables compensation there. [Demo controls](TPS_DEMO.md)
describes how to compare both levels.

## References and tests

Synthetic coverage includes time response, displacement, acceleration, flick,
modifiers, calibration guards, recenter and FOV. See [validation](VALIDATION.md)
for test commands and separate hardware observations.

The independently implemented filters, flick options and Lean behavior follow
Jibb Smart's [gyro guide](https://gyrowiki.jibbsmart.com/blog:good-gyro-controls-part-1:the-gyro-is-a-mouse),
[flick guide](https://gyrowiki.jibbsmart.com/blog:good-gyro-controls-part-2:the-flick-stick)
and [space guide](https://gyrowiki.jibbsmart.com/blog:player-space-gyro-and-alternatives-explained).
GamepadMotionHelpers retains its MIT notice; see [provenance](THIRD_PARTY.md).
