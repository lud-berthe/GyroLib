# Gyro conversion spaces

GyroLib exposes the seven base categories described in Valve's
[Steam Input conversion-style announcement](https://store.steampowered.com/news/posts/?enddate=1701713393&feed=steam_blog).
Player Space Lean and World Space Lean add two choices. The projections are
independently implemented; exact parity with Valve's internal processing is not
claimed. No Steam settings or assets are incorporated into the library.

| ID | Setting choice | Horizontal / vertical behavior |
|---|---|---|
| 0 | Player Space | Gravity-aware yaw/roll combination, local pitch |
| 1 | Yaw | Local Y axis, local pitch |
| 2 | Roll | Negative local Z axis, local pitch |
| 3 | World Space | Gravity-axis turning and horizontal-plane pitch; side-held vertical reduction |
| 4 | Yaw + Roll | Local yaw plus adjustable roll contribution, local pitch |
| 5 | Local Space (Advanced) | Tilted primary axis plus adjustable perpendicular-axis contribution, local pitch |
| 6 | Laser Pointer | Azimuth/elevation velocity of a forward ray, including vertical control when side-held |
| 7 | Player Space Lean | Gravity-relative roll with local pitch |
| 8 | World Space Lean | Gravity-relative roll and pitch |

The original IDs 0..3 are unchanged. The settings model exposes all nine choices in
`gyro.space`, ordered Player Space (default), World Space, Player Space Lean,
World Space Lean, Laser Pointer, Yaw,
Roll, Yaw + Roll, Local Space (Advanced). The displayed choice indexes are not
their persisted values: use `gl_choice.value`. Smoothing, independent mode sensitivities, inversion and acceleration
run after projection for every space. Changing an activation condition does not
reset gravity estimation. Player/World Space use the existing MIT-licensed
GamepadMotionHelpers implementation.

## Full-turn sensitivity checks

Sensitivity multiplies the angular movement produced by the selected projection.
A 360-degree turn about world up at 6x gives 2160 camera degrees in Player or
World Space when their required orientation assumptions are satisfied. Local
Yaw only measures the controller's Y-axis component: with 12 degrees of pitch,
the same turn gives `2160*cos(12 degrees)`, about 2112.8 camera degrees. That is
intentional projection behavior, not a different sensitivity.

World Space requires accurate estimated gravity. Translational acceleration,
fusion error and a motion combining world yaw/pitch/roll can change the result.
The estimated vector is normalized before the World projection: temporary
changes of its magnitude during fusion cannot change angular sensitivity.
Player Space uses the combined local yaw/roll magnitude and a relaxed gravity
projection for direction, reducing that source of error. This distinction follows
[Jibb Smart's explanation](https://gyrowiki.jibbsmart.com/blog:player-space-gyro-and-alternatives-explained).
Neither space promises an exact physical endpoint for arbitrary 3D motion.

## Local controls

`gyro.local_angle_degrees` appears only for advanced Local Space: -180..180
degrees, step 1, default 0. This is a full turn around the local pitch axis;
0 selects yaw and +90 selects roll when complementary contribution is zero.
`gyro.local_roll_percent` appears for Yaw + Roll and advanced Local Space:
-100..100%, step 1, default 100%. Zero removes the secondary contribution;
negative values invert it. The presets Yaw and Roll remain independent of these
advanced settings, so switching space does not overwrite saved tuning.

With inversion disabled, using normalized input angular velocity `(x,y,z)` and offset `a`:

```
primary   =  y*cos(a) - z*sin(a)
secondary = -y*sin(a) - z*cos(a)
horizontal = primary + secondary * roll_percent/100
vertical   = x
```

Yaw + Roll fixes `a=0`. Output yaw is negated afterward to honor GyroLib's
positive-camera-yaw-right convention. Composition is continuous: it does not
switch abruptly between whichever raw axis is largest. Equal opposing angular
contributions can cancel, as expected for a sum.

Yaw + Roll exposes two inversion checkboxes beside sensitivity X: Yaw reverses
the local Y contribution, Roll reverses the local Z contribution. Sensitivity Y
keeps one inversion for local pitch; neither horizontal inversion affects it.
The per-view keys are `gyro.invert_x` (Yaw in this mode) and `gyro.invert_roll`.
In every other space `gyro.invert_x` retains whole-horizontal-output inversion,
and the saved `gyro.invert_roll` value is hidden and ignored. Local Advanced
retains its existing axis-angle and signed contribution controls.

Schema 17 initializes the Roll inversion from the old X inversion on upgrade,
so existing Yaw + Roll direction is preserved, including negative contributions.
The two flags then save independently. An explicitly supplied new Roll inversion
is honored during migration. Both native and F10 frontends use the same model.

The local signs are intentional. In SDL's right-handed convention, a right turn
with a flat controller has negative Y velocity. With its front pointing upward,
the same right turn has positive Z velocity. The final camera outputs are
`X=-gyro.Y` for Yaw and `X=+gyro.Z` for Roll. Roll therefore differs from Lean's
right-edge-lowered gesture; it must not inherit Lean's sign correction.

For a right turn about world up, these are the horizontal gains before sensitivity
(with accurate gravity, no inversion/smoothing/acceleration, and roll contribution 100%):

| Controller pose | Yaw | Roll | Yaw + Roll | Player / World |
|---|---:|---:|---:|---:|
| Flat | 1 | 0 | 1 | 1 |
| Front raised 45 degrees | 0.7071 | 0.7071 | 1.4142 | 1 |
| Front raised 90 degrees | 0 | 1 | 1 | 1 |
| Front lowered 45 degrees | 0.7071 | -0.7071 | 0 | 1 |

Yaw + Roll is a continuous sum of fixed local axes, not a unit-gain world turn.
Its gain depends on the pose; at 45 degrees front-up the two components add to
sqrt(2), while at 45 degrees front-down they cancel. Use Player or World Space
for gravity-relative turning. With inversion disabled, Local Space (Advanced) at 0 degrees / 0% matches
Yaw, at 90 degrees / 0% matches Roll, and at 0 degrees / 100% matches Yaw + Roll.

## Laser Pointer

The imaginary ray points along normalized local -Z. This produces angular camera
deltas, not an absolute cursor coordinate or an injected mouse event. The host
may interpret those deltas for its own camera/UI. Device providers must normalize
their axes to the public API convention; there is no model-name guess for mounting
orientation or a silently applied handheld tilt offset.

Let `u` be the normalized gravity-up vector and `h=sqrt(u.x²+u.y²)`:

```
pitch_rate = (x*u.y - y*u.x) / max(h, sin(5°))
yaw_rate   = (x*u.x + y*u.y) / max(h*h, sin(5°)^2)
```

Away from vertical these are the derivatives of the ray's elevation and azimuth.
At a vertical pole azimuth is undefined. The five-degree damping cone bounds
that region and fades output at the pole instead of dividing by zero. Crossing
the pole changes the ray's azimuth/elevation chart; this is not a head-tracked
orientation solution. There is no hidden recenter action or gravity reset.

Lean IDs 7 and 8 are additional Jibb-inspired choices, not extra Steam categories.
Their default horizontal direction is right lean (right edge lowered) → camera
right, left lean → camera left. `gyro.invert_x` reverses that mapping. The
gravity-relative roll projection uses the opposite intermediate yaw sign because
the common output stage converts yaw to right-positive X with `x=-yaw`.
See [advanced motion](ADVANCED_MOTION.md) for their behavior and integration.

Synthetic tests cover flat, side-held, inverted, tilted and pole poses; separate
tests exercise full processing at 1/4/10/20 ms. Physical feel, Steam equivalence,
handheld mounting and device-specific drift remain hardware acceptance work.
