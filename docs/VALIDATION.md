# Validation and acceptance

Automated checks validate software contracts and synthetic motion. They do not
establish controller feel, game-hook correctness or input-to-photon latency.
[API.md](API.md) defines integration contracts; [LIMITS.md](LIMITS.md) lists
platform, hardware and accuracy boundaries.

## Reproduce the checks

[BUILDING.md](BUILDING.md) provides bundled, static and core-only build commands.
Run each CTest suite after building it. Keep sensor fixtures separate from heavy
concurrent compilation: their bounded startup/recovery deadlines can otherwise
be exceeded. Investigate reproducible failures before relying on a rerun.

After installing the default Windows SDK, the independent consumer project checks
its C and C++ interfaces, package discovery and optional components. In PowerShell 7:

```powershell
$gyroSdk = (Resolve-Path ./dist/sdk).Path
cmake -S tests/install_consumer -B build-consumer -A x64 "-DCMAKE_PREFIX_PATH=$gyroSdk" -DGL_CONSUMER_SDL=ON -DGL_CONSUMER_PANEL=ON
cmake --build build-consumer --config Release --parallel 4
$env:PATH = "$gyroSdk/bin;$env:PATH"
ctest --test-dir build-consumer -C Release --output-on-failure
./dist/sdk/bin/gyrolib_demo.exe --smoke
```

The SDK supplies its vendored SDL development files. Test other SDK variants with
only the components they contain. Also compile the complete README example using
its supplied CMake block. Use a fresh installation when checking packaging and
preserve any existing `girolib.ini` when updating a used SDK.

The Windows desktop-shell path has a separate integration check requiring an
interactive desktop. After building the bundled tests, run
`pwsh -NoProfile -File tests/runtime_distribution.ps1 -BuildRoot build -Desktop`.
It tests real shell launch, named-pipe handshake, executable identity and shutdown;
ordinary process fixtures do not replace this check.

## Recorded automated results

The following checks passed on **2026-10-02**, using MSVC 19.51, x64, Release.
Rerun them for the revision and SDK you intend to ship.

| Configuration | Result |
|---|---|
| Bundled DLL, SDL, panel and demo | 31/31 CTest tests passed |
| Static core without SDL or panel | 9/9 passed |
| Relocated installed SDK: C/C++, SDL and Panel, only `CMAKE_PREFIX_PATH` supplied | 2/2 passed; SDL resolved inside the SDK |
| Installed bundled SDK: C/C++, Core/Steam/Panel, SDL package discovery disabled | 2/2 passed |
| Installed core-only SDK: C/C++ | 2/2 passed |
| README C++ and CMake blocks, extracted unchanged | Compiled against the relocated SDK with only `CMAKE_PREFIX_PATH` |
| README example with an unavailable runtime cache | Reports the error and exits with code 1 without another SDL load |
| Installed demo `--smoke` | Passed |
| Missing `pwsh` for bundled-runtime tests | Configuration reports the prerequisite |
| External SDL with tests disabled | Configuration passed |

Additional checks recorded on **2026-10-01** are retained separately; they were
not all repeated for the SDK packaging changes above:

| Configuration | Result |
|---|---|
| Static, SDL, panel and demo | 30/30 CTest tests passed |
| Installed full static SDK: C/C++, Core/Steam, SDL package discovery disabled | 2/2 passed |
| Requesting an absent Panel component | Correctly rejected during configuration |

These are local Windows results. The GitHub Actions workflow has not yet run on
GitHub; its hosted-run result remains unverified.

The suite covers C layout/version checks, C++ ownership/errors, view selection,
missing observations, camera/cursor separation, persistence/migration and host
capability gates. Motion checks include all nine spaces, signs/inversions,
clock correction, varying report/frame rates, calibration and advanced motion.
The recorded full-turn run included 901 synthetic cases and 128 virtual SDL cases.
These check software gain and directions, not a physical sensor's scale.

SDL/IPC fixtures cover hotplug, remapping, sensor stalls, invalid/stale transport,
bounded shutdown and coexistence with host event/subsystem ownership. UI tests
exercise real widgets with synthetic inputs. Distribution tests cover runtime
extraction, damaged-cache repair, concurrent starts and an independent host SDL.
The complete test list comes from CMake/CTest rather than a duplicated manifest.

## Hardware and visual observations

Manual Windows smoke checks on earlier builds covered the following combinations.
These are bounded observations, not certification of every feature or the latest
revision; public Steam Input fallback was not used for these acquisition checks.

| Controller / transport | Observed behavior |
|---|---|
| DualSense USB through a non-Steam shortcut | Gyro and game controls through isolated SDL; switching to another connected controller without restarting |
| Steam Controller 2026/puck through Steam | Gyro, single-pair automatic association and repeated focus-change recovery |
| Steam Controller 2026/puck, direct launch with Steam running | Hotplug recovery |
| Steam Controller 2015 USB through Steam | Gyro, left/right touchpad activators and correct absence of a physical right-stick option |

Player Space full-turn behavior was checked manually after clock qualification.
World Space retained a small alignment error; no independently measured physical
angle was available. This does not establish exact sensor accuracy. Physical
acceptance for touchpad flick/feedback, broader transports, multiple identical
controllers, public Steam Input and new host games remains outstanding.

English/French panel and demo layouts were inspected at 1024×720, 1440×900 and
3840×2160, including calibration, cursor profiles and native menus. These checks
do not validate rendering or controller navigation inside a different engine.

## Manual acceptance for a host

Test one controller, then identical/different pairs on each intended transport.
Check startup, focus loss, silent sensors, reconnect and sleep while inspecting
source/identity diagnostics. Confirm no camera jumps or doubled movement, and
compare ordinary controls and vibration with the reader enabled.

Verify actual resolved game commands select views immediately and that menu,
pause and focus observations gate movement. Cursor output must not rotate the
camera. Exercise native stick/pad suppression and short-press hooks separately;
a camera callback alone cannot validate them. Test calibration while still and
moving, slow aiming, inversions, space changes, smoothing, acceleration and real
camera recenter/zoom. Complete the platform and hardware checks in [LIMITS.md](LIMITS.md)
before describing a host/controller combination as supported.
