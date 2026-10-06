# Validation

[Documentation](INDEX.md) / Maintenance

Automated checks verify software behavior with synthetic input. Hardware sessions
verify specific controller/transport combinations. Neither alone establishes a
new game's hook correctness or input-to-photon latency.

## Latest SDK results

Release **1.2.1**, 6 October 2026: **53/53** bundled DLL, **53/53** full-static
and **21/21** core-static tests pass. Installed consumers and SDK regressions
pass **23/23** checks; the installed demo and quickstart build pass too. A Steam-managed controller no longer needs touchpad hardware or contact
for relative mouse routing. Tests cover all three policies without contact,
physical HID exclusion, inactivity, device changes and setting visibility on a
controller without touchpads. Flick Stick no longer disables conversion of the
shared mouse stream. The user confirmed the generalized route in a Windows game. Controller model
and exact mapping were not recorded for that confirmation; it is not a
validation of every Steam binding.

The **1.2.0 release**, 5 October 2026, passes **53/53** bundled DLL tests,
**53/53** full-static tests and **21/21** core-static tests on Windows x64/MSVC.
Installed consumers and SDK regressions add **23/23** passing checks across
DLL, static and core packages. The installed demo smoke test and quickstart
build also pass. All 1.1.0 DLL exports remain present.
The observations below record the incremental feature checks; outstanding
hardware cases remain explicit.

Local demo GPU renderer, 5 October 2026: **53/53** bundled tests pass. New GPU
readbacks verify draw-order independence, intersecting triangles, near clipping,
SDL state coexistence and target recreation/resizing. Twelve deterministic runs
compare CPU/GPU rendering at two window sizes. [Performance results](PERFORMANCE.md)
distinguish CPU submission, GPU work and synthetic core processing. A subsequent
Steam-shortcut session confirms low main-thread acquisition/core costs and
roughly 144 frames/s with VSync; the tester reports a substantial improvement.
The report also retains isolated event/presentation stalls whose causes are not
yet established.

Local SDL mouse bridge, 5 October 2026: **52/52** bundled DLL tests, **52/52**
full-static tests and **21/21** static-core tests pass. SDL window tests verify interception before events,
exclusive context/window ownership, preservation of the host event filter,
concurrent raw-callback detachment, window/reader destruction and explicit refusal
of an unsupported runtime. Existing DX12, routing, cursor-clip and demo tests also
pass. The SDL adapter and DX12 overlay now share the same DLL implementation.
These tests do not synthesize desktop mouse movement. A subsequent physical-controller performance session through a non-Steam
shortcut records both gyro and converted mouse output; the tester reports
improved fluidity. This does not exhaust the three-mode routing and multi-monitor
confinement checks.

Local Steam Input mouse setting, 5 October 2026: **51/51** bundled DLL tests
and **21/21** static-core tests pass. Tests cover corroborated detection, physical
mouse rejection, the three modes, view/mode transitions, inheritance, save/load,
camera and cursor output, and cursor-clip ownership. The new row is rendered in
six languages at three sizes; the French screenshot was reviewed. Early visibility
also checks Steam identity before any mouse movement, unrelated virtual devices,
other controllers and withdrawal of Steam metadata. The conversion label is the
same for camera and cursor views. The earlier
routing prototype and two-monitor confinement were confirmed by the tester.
The new automatic detection and three-choice per-view UI still need a hardware
check across the complete mode/controller matrix. These changes ship in 1.2.0.

Local recenter and calibration-menu changes, 5 October 2026: **50/50** bundled
DLL tests and **20/20** static-core tests pass. Recenter tests cover the instant
default, duration at three update intervals, interruption/restart, persistence
and inheritance. The supplied panel is rendered with synthetic SDL/Steam
endpoints in six languages at 1024×720, 1600×1100 and 3840×2160; the native demo
menu also checks the new group and Steam footer. Steam hides calibration
controls, SDL restores them. Screenshot review and physical-camera validation
of timed recenter remain separate from these automated checks. These changes
ship in 1.2.0.

Local controller-transition correction, 5 October 2026: **49/49** bundled DLL
tests, **21/21** targeted static tests and **8/8** external host tests pass.
An 85 ms delay between the host's frame timestamp and sensor polling reproduced
a fatal controls-protocol error. The reader now drops a controls report whose
converted timestamp precedes its last accepted report, without disconnecting the
sensor. A separate malformed-controls case still rejects invalid payloads.
Controller-label regression checks SDL/Steam enumeration in both orders,
disconnect/reconnect, renamed providers and names from explicitly bound sensors.
These fixes ship in 1.2.0; the live
Steam Input enable/disable/re-enable sequence still needs verification.

The **1.1.0 release**, 5 October 2026, passes the fresh Windows x64/MSVC Release
matrix below. The additional WARP test reads back scRGB and HDR10 pixels at
80, 203 and 1000 nits, checks linear alpha composition, Rec.2020 conversion,
negative/extended scRGB values, unchanged transparent pixels and alpha, target
resizing and repeated frames. The public overlay tests also exercise HDR
initialization, invalid format/space pairs and white levels, resize, close/reopen
and HDR-to-SDR reinitialization. The C integration example compiles in both
DLL and static configurations. A manual
session confirmed panel opening and normal colors with a 10-bit SDR backbuffer
on a Windows HDR desktop. The host reported DXGI color space 0; treating that
buffer as PQ had produced desaturated UI colors. Native PQ/scRGB presentation
on physical displays remains unvalidated.

Each variant was compiled in a fresh directory and checked against a separate
installed SDK:

| Configuration | Result |
|---|---:|
| Bundled DLL, SDL, panels and demo | 47/47 |
| Full static build | 47/47 |
| Static core without SDL/UI | 20/20 |
| Installed full SDK consumers, DLL and static | 4/4 + 4/4 |
| Installed core SDK consumers | 2/2 |
| Review E–F regressions against installed DLL/core SDKs | 7/7 + 6/6 |
| Total | **137/137** |

The table counts CTest executions across configurations, not distinct bugs or
hardware sessions. The [audit report](SDK_AUDIT.md) records the findings, fixes,
artifact hashes and earlier matrices. The 1.0.0 matrix passed 135/135 executions;
1.1.0 adds HDR pixel validation to both full builds.

The installed demo also passed its scripted smoke run. The desktop-shell sensor
reader passed the real shell-launch/handshake/shutdown distribution check, and
the quickstart compiled against the installed 1.1 SDK. These are software and
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

Six catalogs contain 322 texts each. Catalog tests compare all 1,932 translations
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
