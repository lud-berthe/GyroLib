# GyroLib Demo

`gyrolib_demo.exe` is the sole demo, a self-contained host application. It reuses
the public library, SDL reader and F10 panel. It contains a procedural third-person
character, a small training arena, three-zone targets and an inventory with four weapons.
There are no assets, hooks or dependencies from an existing game.

Both F10 and Pause -> Gyro settings have small + / - buttons beside Smoothing,
Acceleration (all presets) and Flick Stick for their respective options. The
buttons follow each value; Spin Duration is the first expanded Flick control. There is no
combined Advanced section or zoom compensation option in the demo. Home or the
configured recenter button (initially R3) restores horizontal camera pitch. These
are public C API integrations; see [advanced motion](ADVANCED_MOTION.md).

The default Windows SDK's `bin` directory contains `gyrolib_demo.exe` and
`gyrolib.dll`. SDL and the optional Steam-isolated reader are embedded in that DLL
and managed in `%LOCALAPPDATA%/GyroLib/runtime`; settings are created on first
interactive launch. See [distribution](DISTRIBUTION.md).
The Windows MSVC build opens the demo without a console window. Its optional
sensor reader also runs without allocating a console; input diagnostics remain
in the diagnostic log described below. From the installed SDK directory:

```powershell
./bin/gyrolib_demo.exe
# No controller needed: arrow keys submit simulated angular velocity.
./bin/gyrolib_demo.exe --synthetic
```

Normal operation reads real SDL controllers, including late connection. Mouse,
keyboard and native sticks work independently of whether a motion source is
usable. `--synthetic` does not open physical controllers or claim hardware
validation.

## Four host-owned modes

| Mode | Stable ID | Initial X/Y sensitivity | Output |
|---|---|---|---|
| Exploration | 101 | 2.5 / 2.5 | Third-person camera |
| Aim Standard | 205 | 1.0 / 1.0 | Rifle, Pistol and Shotgun shoulder camera, 50-degree vertical FOV |
| Aim Sniper | 206 | 0.5 / 0.5 | First-person scope, 20-degree vertical FOV |
| Inventory cursor | 309 | 0.8 / 0.8 | Local inventory cursor; world camera frozen |

The HUD shows all four sensitivities and highlights the current mode. F10 has
Exploration, Aim Standard, Aim Sniper and Inventory cursor tabs, each with independent gyro/flick
settings. Controller selection is above the tabs; a shared calibration group and
separate reset action are in the footer. Edits auto-save. Cancel replaces
Recalibrate while calibration is running.
There is no General or extra Default tab.
This example always reports one named mode, whose values take
precedence. These settings affect gyro, not ordinary mouse or stick speed.

Aim is read from the resolved command each frame. Its sensitivity changes
immediately on press/release; the short visual shoulder/field-of-view transition
does not delay it. Inventory takes priority over a held aim command. Closing
inventory resumes aiming if that command is still held.
The third-person camera orbits the upper body in yaw and pitch, with a shorter
shoulder boom while aiming. Pitch, including recoil, is limited to ±80 degrees.
Ground, crates, pillars and walls shorten the boom with near-plane clearance;
the local character is hidden if the camera gets too close to the body.
Rifle, Pistol and Shotgun activate the same Aim Standard profile. Editing its
sensitivity, activators or flick settings affects all three weapons; equipping
one does not create or copy a separate profile.

Sniper aim looks through the optic with a circular lens, dark surround and fine
graduated reticle. The camera sits at the optic. Its center ray selects the aim
point; the projectile travels from the muzzle to that point and can hit cover
along the way. The local character/weapon is hidden in this view.
Only the active Aim Sniper sensitivity card remains beside the lens. Releasing
aim restores third person immediately. Reloading temporarily leaves the scope to
show the animation, then returns if aim is still held; the observed Aim Sniper
gyro profile continues to follow the command throughout reload. Inventory, pause,
F10 and focus loss freeze the scoped world view just like the shoulder camera.

## Native gyro menu

Open Pause with Esc or the controller's Start/Menu/Options button, then choose
**Gyro settings**. This is a host-owned screen, implemented in
`examples/tps/pause_menu.hpp` using only GyroLib's public C menu model.
It contains the four view tabs, device/language selection, available activation
families, sensitivities, spaces, smoothing, flick, calibration and reset. Settings
and per-choice help are localized by the library. The host owns its pause layout,
widgets and navigation; F10 remains an independent frontend to the same values.

Mouse, keyboard and controller can edit it. Esc or the controller's east face
button returns to Pause, then resumes; a dropdown closes first. The Back button
returns to Pause and Resume resumes gameplay. Gameplay and the inventory cursor
remain suspended while this screen is open. Selecting a settings tab never
switches the actual gameplay view. The native screen also works when
`ui.menu_key=` disables the library shortcut. Changes auto-save to `girolib.ini`.

**Menus only calibration excludes the inventory**, where motion controls the
cursor. Pause (including this native gyro screen) and the F10 panel are eligible
while the controller is still. Closing F10 onto the inventory stops bias updates
again and preserves the correction already calculated. Any time remains the
explicit unrestricted auto-calibration policy.

`--capture-native-menu` captures Pause and its gyro screen without hardware or
saved user settings. Add `--french` and/or `--4k` for localized/high-resolution
captures. The independent `demo_native_menu` test exercises real ImGui widgets
and synthetic mouse/keyboard/gamepad events; it does not link `gyrolib_panel`.

## Controls

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Move | WASD physical positions (ZQSD on AZERTY) | Left stick |
| Look | Mouse; arrows when not simulating gyro | Gyro and right stick |
| Aim | Hold right mouse button | Hold left trigger |
| Fire | Left mouse button; hold for Rifle | Right trigger; hold for Rifle |
| Open/close inventory | Tab or I | North face button, labeled from the controller |
| Inventory cursor | Mouse; arrows simulate gyro with `--synthetic` | Gyro or right stick |
| Equip weapon | Click or Enter | South face button |
| Reload | R | West face button |
| Pause/resume | Esc (closes inventory or F10 first) | Start/Menu/Options button |
| Gyro configuration | F10 | Keyboard opens it; controller navigates it |
| Exit | Close the window; Esc first releases gameplay mouse capture | — |

The demo captures relative mouse movement only while its gameplay window is
focused. Inventory, pause, F10 and focus loss release it. The inventory draws
its own pointer: gyro never warps the OS cursor or emits system input. The reload,
equip and inventory hints use the connected controller's button labels.

Hold to enable / Hold to disable expose a Tap only checkbox beside Buttons, in
both F10 and Pause → Gyro settings. The demo connects the public input filter via
`examples/tps/controller_actions.hpp`, translating the selected controller's
normalized button states into its ordinary game commands. For example, select
the west face button (X on Xbox/Steam Controller, Square on PlayStation): checked,
a press under 200 ms reloads on release; a hold does not reload. Gyro activation
changes immediately on the physical press. Unchecked, reload starts on press.
Always on and Toggle do not expose or apply the filter. The adapter preserves
keyboard actions, aim/fire triggers and controller UI navigation, supports a
native-hold veto, and discards pending taps at focus loss or controller takeover.
Real mods must connect their resolved game actions and controller origins;
arbitrary Steam keyboard/mouse remappings cannot be inferred from raw buttons.

| Weapon | Capacity | Firing | Reload |
|---|---|---|---|
| Rifle | 30 | Automatic while held | 1.65 s |
| Pistol | 12 | One shot per press | 1.25 s |
| Shotgun | 6 | One nine-pellet blast per press, with pump cycle | 2.6 s |
| Sniper | 5 | One scoped or unscoped shot per press | 2.2 s |

The weapon lowers, the support hand follows the magazine,
and ammo refills only at the end. Firing is blocked during reload. Switching weapons
cancels reload and retains each magazine's ammo. Pause, focus loss, inventory and
F10 freeze the animation and combat simulation.
Each weapon has a distinct procedural model, inventory icon and firing sound.
The Pistol slide moves on fire. Shotgun pellets spread in a two-degree cone,
individually collide with targets/cover, and illuminate each struck zone. Its
fore-end and support hand cycle after a shot. The Shotgun uses a magazine reload
animation, like the other weapons. Its reticle ring shows the pellet spread.

Shots produce a brief muzzle flash, a thin tracer, recovering visual recoil and a
short procedural sound. A hit marker and the struck target zone light up briefly.
There are no score or hit counters. Each of the three concentric zones lights
independently, including multiple zones struck by Shotgun pellets. One target
moves horizontally. Targets are fixed vertical world geometry with supports behind
their faces. Crates and arena walls/pillars block shots. The example synthesizes all
sounds locally and continues silently if no audio device is available.
All weapons fire from the muzzle towards the camera's aim point. The barrel
converges on that point in third person. Cover between muzzle and target stops
the shot even when the camera sees over it; a barrel protruding into an obstacle
cannot bypass it. The trace, tracer and muzzle flash use the same firing origin.

`--capture-look-up` and `--capture-look-down` render the ±80-degree camera limits
in exploration, shoulder aim and scope. Like other automated capture modes,
they use a hidden window, synthetic inputs and no user settings.

Settings live in `girolib.ini` beside `gyrolib.dll` (beside the executable for a
static demo). Each edit, including language and reset, saves automatically.
If the new file is absent, settings are imported from SDL's historical
`GyroLib/TPSDemo/settings.ini` per-user directory without modifying the original.
An existing new file always takes precedence; malformed/newer configurations
report an error and disable automatic saving until repaired and reloaded.
The file is created on first startup even before a menu edit. Save failures are
shown in the panel. Diagnostics stay in the per-user directory. All four views
retain independent values; only language, menu shortcut and automatic calibration
are shared. Existing valid files are upgraded at startup; migration rules are in
[CONTEXTS.md](CONTEXTS.md).
All references to F10 in this guide mean the default shortcut: change
`ui.menu_key=F10` to any of F1..F24, or use `ui.menu_key=` to disable opening.
Close the demo before editing the INI, then relaunch. The header/HUD follow the
chosen key, and Reset all settings retains it. Scripted smoke/capture modes use
temporary in-memory defaults and never read or write either user settings file.
The old Aiming/Aim Rifle profile ID 205 is now Aim Standard, keeping its saved
settings for all three standard weapons. Aim Sniper retains its own ID 206.
Existing Steam shortcuts must point to `gyrolib_demo.exe`.

## What the modder implements

- `examples/tps/host.hpp`: mode registration, resolved command observation,
  camera callback, native stick suppression, menu/pause/focus state and local
  cursor routing. It has no SDL or rendering dependencies.
- `examples/tps_demo.cpp`: SDL lifecycle, controller/keyboard/mouse commands,
  event pumping, F10 rendering and the executable's loop.
- `examples/sdl_demo_input.hpp`: borrow the selected reader-owned SDL gamepad
  for host commands and ImGui navigation, without racing acquisition at hotplug.
- `examples/tps/scene.hpp`, `raster.hpp` and `ui.hpp`: procedural scene,
  per-pixel depth rendering and demo inventory.
- `examples/tps/scope.hpp`: resolution-independent lens mask and scope reticle.
- `examples/tps/audio.hpp`: optional procedural shot/reload sounds, confined to the demo.

Inventory reports the real state: `host.menu_open=1`, `host.camera_allowed=0`,
and declares the implemented `GL_HOST_MENU_STATE` observation. At registration,
the host declares Inventory as `GL_OUTPUT_CURSOR` and the three camera profiles as
`GL_OUTPUT_CAMERA` through `gl_set_gameplay_context_output_target`. The library
automatically selects that destination on each update. The Inventory tab never
offers flick stick or pivot duration, while camera tabs can offer both.
The library still performs source selection, calibration, fusion, filtering,
activation, inversion and the selected mode's sensitivity once. It returns
angular deltas without calling the camera callback or applying flick.

The host maps yaw/90 and -pitch/60 into normalized UI coordinates (90 angular
degrees span the inventory width, 60 its height), clamps the cursor to the
viewport, performs hit testing and handles click/confirm. These are host UI
decisions, not game-specific behavior in the library. Gyro activation and other
settings from the inventory profile apply to the cursor; the initial Always mode demonstrates all
four contexts. F10, pause and loss of focus block both output destinations.

With a separately reported right touchpad (including the Steam Controller), the
camera tabs offer Flick `Off / Stick / Touchpad / Either`, with `SPIN DURATION` on
the same row. Other controllers retain `Off / On`. Touch beyond 35% of the
right pad's radius to pivot, then circle to turn; lift and touch again for
another pivot. In Touchpad mode, native stick look uses physical stick coordinates
to exclude Steam's virtual touchpad-to-stick output. Keyboard and mouse look remain
available. If Steam maps that pad to mouse motion, remove that mapping from the
shortcut's layout: the demo cannot identify which mouse events came from the pad.
The Steam Controller USB/puck output backend adds brief right-pad pulses when
starting a flick or circling in Touchpad/Either mode. It is silent when stationary,
in menus or out of focus. Physical feedback still requires user validation.
Unplugging the selected controller lets another connected controller take over
automatically; adding one while the current controller remains connected does
not steal selection.

## Validation and scope

`ctest` runs `demo_modes_inventory` using actual SDL command events and simulated
gyro samples. It verifies immediate Standard/Sniper aim gain, release, inventory camera
freeze, cursor output, selection of all four weapons, camera resumption, F10 blocking and pause.
`demo_combat` verifies reload timing and gating, per-weapon ammo, trigger behavior,
target zones, feedback decay, target motion and cover blocking shots. Scope checks
cover camera position, center-ray impacts, immediate entry/release, temporary
reload exit, held-aim recovery and a frozen view in inventory/F10/focus loss.
Additional checks exercise actual gyro output after a single shared sensitivity
edit across Rifle/Pistol/Shotgun, independent Sniper gain, weapon ammunition,
reloads and press-only firing, Shotgun pellet spread and fore-end cycle.
Core/C ABI tests independently verify cursor permission gates, no camera-callback
leakage, no flick or native-stick suppression in cursor mode, and ABI exports.

`--capture` writes exploration, both aim modes, inventory and settings BMPs; `--4k` uses
3840×2160. Captures and smoke runs do not read/write user preferences or open
physical controllers. `--capture-occlusion` places a target behind a crate;
`--capture-cursor-settings` captures F10 on the inventory tab. These use separate
`demo-occlusion-*` / `demo-cursor-*` filenames and never save fixture preferences.
`--capture-combat` adds shot/impact and reload poses as `demo-combat-*`.
`--capture-pistol` and `--capture-shotgun` exercise the added weapons with separate
`demo-pistol-*` and `demo-shotgun-*` output; the Shotgun capture also shows its pump cycle.
`--capture-calibration` writes `demo-calibration-*` captures, including the F10
countdown with Cancel and the restored Recalibrate button after cancellation.

The scene uses a small CPU triangle rasterizer with reciprocal-depth testing at
each pixel, including targets, and near-plane clipping before projection. This
avoids face-sort errors and target overlays through scenery. Its 3D resolution
is capped at 1920x1080 to bound CPU cost; the HUD, inventory and F10 panel still
render at native viewport/DPI resolution, including 4K. The core library has no
dependency on this demo renderer. It is not a full game: player movement has arena
bounds but no collision with crates. Shot hit tests use simple boxes and disks.
Combat feel/audio and controller reload hints require hands-on validation.
[VALIDATION.md](VALIDATION.md) records acquisition checks; [LIMITS.md](LIMITS.md)
defines the remaining hardware and platform boundaries.

## Steam launch and diagnostics

Steam's virtual gamepad can supply movement/buttons without SDL gyro sensors.
Its presence is not proof of usable motion. When this happens, the SDL reader
starts an isolated standard-SDL sensor reader, without a Steamworks SDK.

Launch the demo through the non-Steam shortcut. A unique virtual controller and
fresh physical sensor with matching SDL vendor metadata are paired automatically;
there is no extra sensor row in the normal case. Movement and buttons stay on
Steam's virtual gamepad. If metadata is missing or multiple candidates exist,
F10 shows a **Gyro sensor** fallback choice. Bind the correct physical sensor,
then close F10 to test camera motion. Automatic associations are reevaluated
after restart or removal, not saved as permanent identity. See [INPUT.md](INPUT.md).

Normal interactive launches replace `input-diagnostics.log` in the legacy
preferences directory (Windows: `%APPDATA%/GyroLib/TPSDemo`). The current
`girolib.ini` remains beside the library. The log records:

- SDL runtime version, relevant Steam/SDL filters and enumerated HID interfaces.
- Isolated-reader errors and recovery, if any.
- SDL/Steam endpoints, sensor capabilities and exact association handles.
- Selected motion source (0 = waiting, 1 = SDL, 2 = Steam), gyro activity and
  accepted/rejected sample counts, at most once every two seconds unless the source changes.

It flushes on writes and stops at approximately 1 MiB. Synthetic/test runs do not
touch this log. The demo does not initialize a Steam service or require a
Steamworks SDK/runtime. No global Steam configuration is changed.
Disable gyro-to-mouse and gyro-to-stick in the shortcut's Steam layout to avoid
double rotation when motion acquisition is working; detection remains incomplete.
