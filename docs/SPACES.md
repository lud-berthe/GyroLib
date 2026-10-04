# Gyro spaces

[Documentation](INDEX.md) / Reference

A space projects controller angular velocity into horizontal and vertical
movement. Sensitivity, filtering and inversion then apply to that projection.
Player Space is the default. Ordinary activation changes preserve gravity estimation.

| Saved ID | Choice | Projection |
|---|---|---|
| 0 | Player Space | Gravity-aware yaw/roll combination, local pitch |
| 1 | Yaw | Local Y axis, local pitch |
| 2 | Roll | Negative local Z axis, local pitch |
| 3 | World Space | Gravity-axis turning, horizontal-plane pitch with side-held reduction |
| 4 | Yaw + Roll | Local yaw plus adjustable roll contribution, local pitch |
| 5 | Local Space (Advanced) | Tilted primary axis plus adjustable perpendicular contribution, local pitch |
| 6 | Laser Pointer | Azimuth/elevation velocity of a forward ray |
| 7 | Player Space Lean | Gravity-relative roll, local pitch |
| 8 | World Space Lean | Gravity-relative roll and pitch |

Menus order these as Player, World, Player Lean, World Lean, Laser Pointer, Yaw,
Roll, Yaw + Roll and Local Advanced. Use `gl_choice.value`, not its displayed index.
Local spaces can work without acceleration; gravity-dependent choices require
fresh usable acceleration.

The seven base categories follow Valve's
[conversion-style announcement](https://store.steampowered.com/news/posts/?enddate=1701713393&feed=steam_blog).
Lean adds two Jibb-inspired choices. Exact equivalence with Valve's internal
processing is not claimed. Player/World use the MIT-licensed GamepadMotionHelpers.

## Sensitivity and full turns

Sensitivity multiplies projected angular movement. A 360° world-up turn at ×6
gives 2160 camera degrees in Player/World when their orientation assumptions hold.
Yaw measures only local Y: with 12° of pitch, the same turn gives
`2160*cos(12°) ≈ 2112.8°`. That difference follows from projection.

World Space needs accurate estimated gravity. Translation, fusion error and mixed
3D rotation can change its result. Gravity is normalized before projection so
changes of its magnitude cannot change gain. Player Space combines local yaw/roll
magnitude with a relaxed gravity projection for direction, reducing that source
of error. [Jibb Smart's explanation](https://gyrowiki.jibbsmart.com/blog:player-space-gyro-and-alternatives-explained)
describes the distinction. Neither promises an exact physical endpoint for arbitrary
3D motion.

## Yaw, Roll and Local Advanced

All keys are per-view. `gyro.local_angle_degrees`, visible in Local Advanced,
ranges −180..180°, step 1, default 0. With zero complementary contribution,
0° selects yaw and +90° selects roll.

`gyro.local_roll_percent` ranges −100..100%, step 1, default 100%. It appears for
Yaw + Roll and Local Advanced. Zero removes the secondary contribution; negative
values reverse it. Standalone Yaw/Roll ignore these controls without overwriting them.

With inversion disabled, normalized gyro velocity `(x,y,z)` and offset `a`:

```text
primary    =  y*cos(a) - z*sin(a)
secondary  = -y*sin(a) - z*cos(a)
horizontal = primary + secondary * roll_percent/100
vertical   = x
```

Yaw + Roll fixes `a=0`. The output stage negates horizontal yaw for GyroLib's
right-positive camera convention. Contributions add continuously and can cancel.

In SDL axes, a flat right turn has negative Y velocity; with the controller's
front pointing up, it has positive Z. Camera X is therefore `-gyro.Y` for Yaw and
`+gyro.Z` for Roll. This differs from the Lean gesture described below.

For a world-up right turn, with accurate gravity, no filtering/inversion/acceleration
and roll contribution 100%, the gain before sensitivity is:

| Pose | Yaw | Roll | Yaw + Roll | Player / World |
|---|---:|---:|---:|---:|
| Flat | 1 | 0 | 1 | 1 |
| Front raised 45° | 0.7071 | 0.7071 | 1.4142 | 1 |
| Front raised 90° | 0 | 1 | 1 | 1 |
| Front lowered 45° | 0.7071 | −0.7071 | 0 | 1 |

Yaw + Roll is a sum of fixed local axes, not a unit-gain world turn. Local Advanced
at 0°/0% matches Yaw, 90°/0% matches Roll, and 0°/100% matches Yaw + Roll when
inversion is disabled.

### Inversion

Yaw + Roll has two checkboxes beside sensitivity X: `gyro.invert_x` reverses Yaw,
`gyro.invert_roll` reverses Roll. Sensitivity Y keeps its Pitch inversion. In other
spaces, `gyro.invert_x` reverses the whole horizontal output and the saved Roll
inversion is hidden/ignored. Local Advanced retains its angle and signed contribution.
Roll inversion defaults to Off.

## Laser Pointer

An imaginary ray points along local −Z. Its angular motion drives camera/cursor
deltas; it is not an absolute cursor position. Providers must normalize axes;
there is no controller-name mounting guess or hidden handheld tilt offset.

With normalized gravity-up `u` and `h=sqrt(u.x²+u.y²)`:

```text
pitch_rate = (x*u.y - y*u.x) / max(h, sin(5°))
yaw_rate   = (x*u.x + y*u.y) / max(h*h, sin(5°)^2)
```

Away from vertical, these are the ray's elevation/azimuth derivatives, including
vertical control while side-held. At the vertical pole, azimuth is undefined. A
five-degree damping cone bounds and fades the response there. Crossing the pole
changes the coordinate chart; this is not a head-tracked orientation solution.
No recenter action or gravity reset is implied.

## Lean

Lean projects rotation onto the gravity-relative roll axis. With inversion Off,
lowering the right edge turns the camera right; lowering the left turns it left.
`gyro.invert_x` reverses this mapping. The intermediate sign is opposite to Roll
because the common output stage uses `X=-yaw`.

Player Lean uses local pitch and a 1.15 roll relaxation factor. World Lean uses
gravity-relative pitch. Both fade horizontal response near a side-on singularity.
They are additional options inspired by Jibb's work, not extra Steam categories.

Synthetic tests cover flat, side-held, inverted, tilted and pole poses, plus full
processing at 1/4/10/20 ms. Physical mounting, sensor scale and feel remain separate
acceptance work; see [validation](VALIDATION.md).
