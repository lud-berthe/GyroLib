# Validation

[Documentation](INDEX.md) / Maintenance

Automated checks verify software behavior with synthetic input. Hardware sessions
verify specific controller/transport combinations. Neither alone establishes a
new game's hook correctness or input-to-photon latency.

## Latest SDK results

The **1.0.0 release build**, 4 October 2026, Windows x64/MSVC Release, was
compiled in fresh build directories and checked against separate installed SDKs:

| Configuration | Result |
|---|---:|
| Bundled DLL, SDL, panels and demo | 46/46 |
| Full static build | 46/46 |
| Static core without SDL/UI | 20/20 |
| Installed full SDK consumers, DLL and static | 4/4 + 4/4 |
| Installed core SDK consumers | 2/2 |
| Review E–F regressions against installed DLL/core SDKs | 7/7 + 6/6 |
| Total | **135/135** |

The table counts CTest executions across configurations, not distinct bugs or
hardware sessions. The [audit report](SDK_AUDIT.md) records the findings, fixes,
artifact hashes and earlier matrices. Documentation edits do not constitute a
new runtime validation.

The installed demo also passed its scripted smoke run. The desktop-shell sensor
reader passed the real shell-launch/handshake/shutdown distribution check, and
the quickstart compiled against the installed 1.0 SDK. These are software and
deployment checks; the hardware observations below were not repeated for this
release preparation.

## Automated coverage

| Area | What is exercised |
|---|---|
| API/lifetime | C layout/version, C++ ownership/errors, allocation-failure containment, event IDs/queues and installed consumers |
| Views | Priority, missing observations, camera/cursor separation, inactive-view editability, menu-camera opt-in and host gates |
| Motion | Nine spaces, signs/inversion, clock correction, variable report/frame intervals, calibration and advanced filters |
| Flick/input | Independent stick/pad sources, freshness, delayed/batched reports, authority, transitions, hotplug and sensor recovery |
| Settings | Range/format errors, atomic failed loads, partial presets, inheritance chains, recommendations and controller fallback |
| Menus | Real ImGui widgets driven by synthetic mouse/keyboard/gamepad input, help bounds, availability and both frontends |
| DX12 | Hidden WARP rendering, pixel readback, owner/render queues, resize, shortcuts, stale-state closure and host ImGui coexistence |
| Distribution | Cache preparation/repair, concurrent startup, worker lifetime, paths with spaces/accents, separate host SDL and INI location |
| Demo | View routing, inventory, scope zoom, reload/recoil, ammunition, target motion, cover and depth |

Settings coverage includes equal-valued overrides, detachment/removal, cycles,
parent changes, reset/recommendations and saved unregistered profiles. The menu
boundary test covers 47 settings and 214 edits; review E adds 800 deterministic
operations over 46 persisted view fields, comparing values and inheritance
metadata after each save/load. These operations belong to tests, not separate
CTest entries.

Six catalogs contain 319 texts each. Catalog tests compare all 1,914 translations
with the public API; font checks cover every character. F10/native layouts run in
all six languages at 1024×720 and 3840×2160. View names supplied by a mod are outside
those catalogs. Availability checks disable settings without a controller, preserve
preferences, restore them on reconnect and exclude unrelated gyro companions.

Motion gain coverage includes 901 synthetic cases and 128 virtual SDL cases.
They test software gain/direction, not a physical sensor's scale. Calibration
checks cover stillness windows, active-use drift guards and inventory exclusion.
Long-press blocking tests cover immediate gyro activation, short actions emitted
on release and discarded pending actions on transitions.

Protocol fixtures use virtual devices with physical drivers disabled. Production
reader and SDL/runtime tests retain their backends. IPC cases include fragmented,
stale and invalid records, delayed startup and bounded shutdown. No fixture
result is counted as a new physical-controller session.

Demo zoom tests cover same-frame 20°/10° FOV scaling on both axes, retained sensitivity,
command gating and sniper-only visibility. Recoil recovery is compared at
30/60/144 Hz; tests cover additive input, bounded automatic fire and muzzle-to-aim
convergence. Recoil remains example-game behavior.

## Hardware and visual observations

Earlier manual Windows smoke sessions observed the following. Public Steam Input
fallback was not used; the non-Steam cases use isolated SDL acquisition.

| Controller / transport | Observed behavior |
|---|---|
| DualSense USB through a non-Steam shortcut | Gyro and game controls; controller replacement without restarting |
| Steam Controller 2026/puck through Steam | Gyro, automatic single-pair association and repeated focus recovery |
| Steam Controller 2026/puck, direct launch with Steam running | Hotplug recovery |
| Steam Controller 2015 USB through Steam | Gyro, both touchpad activators and no nonexistent right-stick option |

Player Space full-turn behavior was checked after clock qualification. World Space
retained a small alignment error without an independently measured angle. Broader
transports, identical controllers, touchpad feedback and new host games still need
acceptance. English/French layouts were visually inspected at 1024×720, 1440×900
and 3840×2160, including calibration, cursor profiles and native menus.

## Steam Input validation

Public Steam Input was validated on 4 October 2026, Windows x64, with a Steam
Controller 2026 over its puck and a host-owned, initialized Steam Input service.
The library's borrowed reader never initializes or shuts down that service.

An interrupted SDL reader produced a complete SDL → Steam → SDL round trip.
The 30-second capture contains 348 observations: 92 from Steam (91 with nonzero
camera output) and 256 from SDL. Steam became active at second 4; SDL resumed
priority at second 11 and remained active. Measured rates at the end were about
60 Hz for Steam and 243 Hz for SDL. The user reported no interruption, camera
jump or change in direction/speed.

The exercise exposed a companion-reassociation bug, reproduced by the
`steam_loaded_runtime` regression and fixed before the successful round trip.
That fixture also checks borrowed lifecycle, unavailable sessions, reconnects,
invalid motion, and source selection without adding the two streams.

These results cover this controller/transport and host setup. They do not
validate other combinations, arbitrary host hooks or input-to-photon latency.
Consumer-specific initialization and gameplay checks belong in the consumer's
own validation report.

## Run the checks

[BUILDING.md](BUILDING.md) gives the build commands. Build each variant before
running its CTest suite. Keep bounded process/sensor fixtures separate from heavy
parallel compilation; investigate failures rather than treating a successful
rerun as proof of their cause.

After installing the default SDK, test an independent consumer:

```powershell
$gyroSdk = (Resolve-Path ./dist/sdk).Path
cmake -S tests/install_consumer -B build-consumer -A x64 "-DCMAKE_PREFIX_PATH=$gyroSdk" -DGL_CONSUMER_SDL=ON -DGL_CONSUMER_PANEL=ON -DGL_CONSUMER_OVERLAY=ON
cmake --build build-consumer --config Release --parallel 4
$env:PATH = "$gyroSdk/bin;$env:PATH"
ctest --test-dir build-consumer -C Release --output-on-failure
./dist/sdk/bin/gyrolib_demo.exe --smoke
```

Request only components present in the SDK variant. Compile the
[quickstart](QUICKSTART.md) after changing examples/exports. Use a fresh prefix for
packaging tests and preserve existing player INIs when updating a play-test SDK.
The audit's independent probes are documented in `tests/sdk_audit/README.md` in
the source checkout.

The Windows desktop-shell reader path needs a separate interactive-desktop test:

```powershell
pwsh -NoProfile -File tests/runtime_distribution.ps1 -BuildRoot build -Desktop
```

It checks real shell launch, named-pipe handshake, executable identity and shutdown;
ordinary process fixtures do not replace it. Available tests come from CMake/CTest,
not a second hand-maintained manifest.

## Accepting a new host

Test one controller, then identical/different pairs on each intended transport.
Exercise startup, focus loss, silent sensors, reconnect and sleep while checking
source/identity diagnostics. Compare ordinary inputs and vibration with acquisition
enabled and disabled; check for camera jumps or doubled movement.

Verify that resolved commands select views immediately, cursor output leaves the
camera still, and pause/menu/focus gates follow the game. Exercise suppression and
long-press blocking through their own hooks. Check still/moving calibration, slow aim,
spaces, inversion, filters, flick, recenter and zoom. Measure feel and latency in
the actual host before declaring the combination supported. [LIMITS.md](LIMITS.md)
lists the remaining platform/hardware boundaries.
