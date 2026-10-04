# GyroLib Demo

[Documentation](INDEX.md) / Example host

`gyrolib_demo.exe` is a small third-person shooting range built as a GyroLib host.
It has four weapons, a two-level sniper scope, a local inventory cursor and two
settings frontends. The scene and sounds are procedural; no game assets or hooks
are included.

From the installed SDK:

```powershell
./bin/gyrolib_demo.exe
# No controller: arrow keys simulate angular velocity.
./bin/gyrolib_demo.exe --synthetic
```

The bundled Windows build needs `gyrolib.dll` beside the demo. SDL/reader files
are managed in the [runtime cache](DISTRIBUTION.md#runtime-cache); no console is
opened. Normal mode accepts late-connected controllers. Mouse, keyboard and
native sticks work even without a usable gyro source. Synthetic mode does not
open physical controllers.

- [Controls](#controls)
- [Views and recommendations](#views-and-recommendations)
- [The two settings menus](#the-two-settings-menus)
- [Settings and diagnostics](#settings-and-diagnostics)
- [Camera, scope and combat](#camera-scope-and-combat)
- [Integration files](#integration-files)
- [Automated captures and limits](#automated-captures-and-limits)

## Controls

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Move | WASD positions; ZQSD on AZERTY | Left stick |
| Look | Mouse; arrows outside synthetic mode | Gyro and right stick |
| Aim | Hold right mouse | Hold left trigger |
| Fire | Left mouse; hold for Rifle | Right trigger; hold for Rifle |
| Inventory | Tab or I | North face button |
| Inventory cursor | Mouse; arrows simulate gyro in synthetic mode | Gyro or right stick |
| Equip | Click or Enter | South face button |
| Reload | R | West face button |
| Sniper zoom, while scoped | V or mouse wheel | South face button |
| Recenter | Home | Configured button; R3 in the recommended profile |
| Pause/resume | Esc, closing inventory/F10 first | Start/Menu/Options |
| Gyro panel | F10 by default | Back + Start (Select + Start or equivalent) |
| Exit | Close window; Esc releases gameplay mouse capture first | — |

Hints use the selected controller's labels. Focus loss, inventory, pause and the
settings panel release relative mouse capture. Inventory draws its own cursor;
gyro does not warp the OS pointer.

## Views and recommendations

| View | ID | Recommended X/Y | Output |
|---|---|---|---|
| Exploration | 101 | 2.5 / 2.5 | Third-person camera |
| Aim Standard | 205 | 2.5 / 2.5 | Rifle, Pistol and Shotgun shoulder aim, 50° vertical FOV |
| Aim Sniper | 206 | 1 / 1 | Scope, 20° or 10° vertical FOV |
| Inventory cursor | 309 | 2.5 / 2.5 | Local cursor; camera frozen |

The recommended profile uses Player Space, smoothing/acceleration Off and Hold
to disable on the west face button with Block long press. Exploration enables Stick or
Touchpad flick and R3 recentering. Aim Standard inherits it with Flick Off; Sniper
inherits Standard with sensitivity 1 and zoom compensation enabled. Inventory
inherits Exploration but selects Laser Pointer; cursor routing excludes flick.

Use recommended settings restores that profile and its links. Existing INIs load
after defaults and are not replaced on startup. Aim Standard uses the stable view ID 205 for all three weapons.

The host reads aim from the resolved command every frame, before visual camera
transitions. Inventory takes priority; closing it resumes aim if still held.
Editing a tab changes that profile, not the active game view.

## The two settings menus

F10 or Back + Start opens the supplied panel. Pause → Gyro settings opens
host-owned widgets built from the same public model. Both expose the view tabs, inheritance, activators,
filters, flick, calibration and reset/recommendations, and both autosave.
The native screen remains accessible when `ui.menu_key=` disables F10.

Esc or the controller's east button closes a dropdown first, then returns to
Pause. Gameplay and the inventory cursor stay suspended while settings are open.
Menus-only calibration permits stillness in Pause/settings, but excludes the
unpaused gyro-driven inventory. See [calibration](ADVANCED_MOTION.md#calibration).

The demo wires Block long press to eligible controller commands. For example,
a short west-button press reloads on release; a hold controls gyro without reloading.
Gyro responds immediately to the press. Unchecked, reload starts on press. Keyboard
actions, aim/fire triggers and UI navigation remain separate. Arbitrary Steam
keyboard/mouse remappings cannot be inferred from raw controller buttons.

Right-pad flick uses a separately reported right pad: touch beyond 35% radius to
pivot, then circle to turn. Touchpad mode suppresses Steam's virtual pad-to-stick
look using physical input coordinates. Remove pad-to-mouse mappings yourself;
the demo cannot identify them among ordinary mouse events. Optional right-pad
pulses use the supported Steam Controller USB/puck backend; physical feedback
still needs acceptance testing.

## Settings and diagnostics

`gyrolib.ini` lives beside the DLL, or beside the executable in a static build.
Close the demo before editing it. F1..F24 can replace F10, or an empty `ui.menu_key`
disables the keyboard shortcut. Set `ui.gamepad_menu_shortcut=0` to disable the
controller shortcut as well. Reset preserves language and both shortcuts.
When the file is absent, the demo creates it with its defaults. Malformed/newer
`gyrolib.ini` files disable autosave until repaired and reloaded.
See [settings](SETTINGS.md).

Interactive launches replace `input-diagnostics.log` in the per-user preferences
directory, normally `%APPDATA%/GyroLib/TPSDemo` on Windows. It logs SDL/runtime
metadata, reader failures, endpoints, associations, source and sample counts.
Writes are flushed; routine state is logged at most every two seconds and the
file stops around 1 MiB. Synthetic/capture runs leave player settings/logs untouched.

For a non-Steam shortcut, Steam can expose commands without sensors. GyroLib then
uses the isolated SDL reader and pairs an unambiguous sensor automatically.
Ambiguous metadata exposes a Gyro sensor choice in F10. The demo does not initialize
public Steam Input. Disable Steam gyro-to-mouse/stick to avoid doubled rotation.
[Input acquisition](INPUT.md) explains this distinction and pairing limits.

## Camera, scope and combat

The camera orbits the upper body, with a shorter shoulder boom in standard aim.
Pitch including recoil is limited to ±80°. Ground, crates, pillars and walls
shorten the boom with near-plane clearance; the character hides when too close.

Sniper aim places the camera at the optic with a circular lens and reticle. Zoom
cycles between 20° and 10° on a press, not while holding the command. The selected
level survives aim release, inventory and reload. Pause, focus loss, inventory,
settings and reload block zoom changes.

Only Sniper declares zoom compensation. Zoom 1 is its reference, so Zoom 2 scales
gyro by `tan(5°)/tan(10°) ≈ 0.4962`. The host reports actual FOV each update without
changing saved sensitivity. Reload temporarily shows third person and bypasses
scoped compensation; the Sniper profile still follows the aim command.

| Weapon | Magazine | Fire | Reload |
|---|---|---|---|
| Rifle | 30 | Automatic while held | 1.65 s |
| Pistol | 12 | One shot per press | 1.25 s |
| Shotgun | 6 | Nine pellets per press, with pump cycle | 2.6 s |
| Sniper | 5 | One shot per press | 2.2 s |

Reload lowers the weapon, moves the support hand and refills ammo at the end.
Switching weapons cancels reload and retains magazine contents. The Shotgun uses
a magazine reload; pellets spread over a two-degree cone and collide individually.

All shots originate at the muzzle and converge on the camera's aim point. Cover
between muzzle and target blocks them, even if the camera sees past it. Flash,
tracer and collision use that same origin. Each weapon has distinct recoil,
procedural sound and recovery; player/gyro motion stays additive. Recenter clears
recoil. Recoil is demo behavior, not a GyroLib feature or a sensitivity multiplier.

Fifteen three-zone targets occupy five groups from 18 to 114 metres: five static,
three horizontal, three vertical and four moving on both axes. Hits illuminate
the struck zones and show a brief marker, without score/hit counters. Supports
stay behind target faces. The range extends 120 metres, movement to 116 metres
and shot rays to 180 metres. Menus, pause and focus loss freeze combat, target
motion and recoil recovery. The demo continues silently if audio is unavailable.

## Integration files

| File under `examples/` | Responsibility |
|---|---|
| `tps/host.hpp` | Views, observations, camera callback, suppression, menu/focus state and cursor routing |
| `tps_demo.cpp` | SDL lifecycle, commands, event loop and panel rendering |
| `tps/controller_actions.hpp` | Eligible game actions connected to the long-press blocking filter |
| `sdl_demo_input.hpp` | Selected reader handle for game commands and ImGui navigation |
| `tps/pause_menu.hpp` | Native settings widgets using public APIs |
| `tps/scene.hpp`, `raster.hpp`, `ui.hpp` | Scene, depth rendering and inventory |
| `tps/scope.hpp`, `audio.hpp` | Scope mask/reticle and procedural sounds |

Inventory reports `menu_open=1`, `camera_allowed=0` and a `GL_OUTPUT_CURSOR` view.
The host maps yaw/90 and −pitch/60 to normalized cursor coordinates, clamps them
and handles selection. The library handles motion processing once and never
calls the camera callback for that view.

## Automated captures and limits

Capture modes use a hidden window and synthetic input, without player files or
physical controllers. Add `--4k` for 3840×2160; native-menu captures also support
`--french`.

| Flag | Captures |
|---|---|
| `--capture` | Exploration, aim modes, inventory and settings |
| `--capture-native-menu` | Pause and native gyro menu |
| `--capture-cursor-settings` | Inventory profile in F10 |
| `--capture-occlusion` | Target behind cover |
| `--capture-combat` | Shots, impacts and reload poses |
| `--capture-pistol`, `--capture-shotgun` | Weapon behavior and Shotgun pump cycle |
| `--capture-calibration` | Countdown, Cancel and return to Recalibrate |
| `--capture-look-up`, `--capture-look-down` | Camera pitch limits |

CTest covers view routing, zoom, reload, recoil, targets, cover, menus and depth.
The CPU rasterizer uses per-pixel reciprocal depth and near-plane clipping, with
3D rendering capped at 1920×1080. UI remains native resolution/DPI. Player movement
has arena bounds but no crate collision; shots use simple boxes/disks. This is an
integration demo, not a complete game. [Validation](VALIDATION.md) separates
software checks from physical-controller observations.
