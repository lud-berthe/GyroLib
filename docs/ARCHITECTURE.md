# Architecture

```text
Host named command contexts / menu / pause / focus states
                            |
SDL event observer ----> physical-device grouping ----> stable source selector
Steam borrowed reader -->           |                       |
                                    |              one motion stream only
                              capabilities                   |
                                    |        bias + gravity fusion (always alive)
                                    |        activation -> space projection
                                    |        adaptive exponential smoothing
                                    |        tightening -> acceleration / sensitivity
                                    |        optional reported zoom -> angular delta
                                    |                       |
Native widgets <--> shared settings / menu model       camera delta callback
F10 ImGui panel <->            |                        + flick-stick delta
                       versioned persistence           + native stick/pad gates
```

`src/core.cpp` owns controller identity, queues, freshness, selection, activation,
host gates and synchronous output. `src/motion.cpp` owns calibration/fusion and
time-based processing. `src/detail/flick.hpp` contains independent flick and
short-press state machines. No engine or SDL headers enter these modules.
`src/contexts.cpp` owns mod-defined names, stable IDs, freshness, priority and
selection of complete independent view profiles. The core has no built-in game actions.

`src/sdl.cpp` owns only its SDL references and an event-watch queue. It does not
steal the host's events. `src/sensor_process.cpp` manages an optional isolated
reader (`src/sensor_worker.cpp`) using standard SDL sensor drivers. The helper
owns its own SDL environment/lifecycle and sends normalized, timestamped motion and activators
through the versioned protocol in `src/detail/sensor_wire.hpp`. Input decoding
stays in SDL. A companion requires explicit or qualified
automatic association to a controller before it can drive gyro or its activators.
`src/detail/sdl_controls.hpp` shares direct/worker control interpretation, verified
contact mappings and device labels. Physical buttons govern gyro activation;
the host's Steam virtual gamepad continues to govern game actions. Flick uses
separate physical right-stick/right-touchpad coordinates when available, avoiding
duplicate rotation from Steam's touchpad-to-stick mapping. Independent processors
allow either or both controls; the host applies the corresponding native-input gates.
An optional output backend in `src/detail/touchpad_haptic.hpp` handles a single
documented Steam Controller right-pad pulse report. Core feedback requests stay
hardware-independent; SDL dispatch is opt-in, including through the worker.
`src/steam.cpp` calls borrowed callbacks and cannot
initialize Steam, run its event loop, change action sets, vibrate or shut down the
service. A public typed SDK bridge is in `examples/steamworks_bridge.hpp`.

`src/settings.cpp`, the checked-in localization catalog and stable C metadata
form the presentation boundary. `src/panel.cpp` renders that same metadata into
Dear ImGui. SDL renderer integration lives only in the demo. Engine adapters
translate game commands/axes and apply output; they do not reimplement gyro math.

The default destination is the camera callback plus optional flick output.
An explicit `GL_OUTPUT_CURSOR` destination permits the same processed gyro stream
inside a verified host menu, without calling that callback or applying flick.
The host maps angular deltas into its own UI coordinates and performs selection.
`examples/tps/host.hpp` demonstrates this routing while keeping the real menu
and camera-availability flags intact. No engine, inventory widget or OS cursor
control enters the core.

GamepadMotionHelpers v10 provides gravity fusion and Player/World Space projection.
The Returnal gravity-initialization patch is retained. Endpoint objects have stable
addresses because the upstream math object contains pointers to its own settings.
Ordinary activation, context and menu changes continue fusion. A source loss, sensor
clock restart or controller transition restarts timing and seeds current gravity;
it does not integrate the missing interval.

`src/detail/sensor_clock.hpp` compares the SDL device clock to monotonic arrivals
over seconds, preserving hardware report intervals within each frame. Consistent
frequency estimates correct dt before fusion, smoothing and angular integration.
Both halves of the window must agree so batching or a transport-delay step does
not become a sensitivity change. This has no controller-name coefficients and
never recalibrates Steam Input data, whose clock already comes from the host.

The isolated reader is an owned child process; its normalized samples are drained
by the host's existing poll. No IPC thread calls the core or game camera.
On Windows with Steam's module loaded, `src/detail/sensor_desktop.hpp` uses
desktop-shell activation and a restricted local named pipe, because ordinary
child processes also inherit Steam's input interception. Other launches use SDL's
process API. Both paths share sensor protocol, source policy and camera processing.
The core is deliberately single-owner-thread, making host state changes and camera
output synchronous and avoiding hidden scheduling latency. SDL's callback may run
elsewhere: it only copies bounded sensor events under a mutex; the owner drains
them. Hosts with separate input/render threads must marshal calls to one owner.
The SDL adapter and isolated reader refresh controller metadata on discovery,
remapping and a 500 ms fallback; per-poll reads contain only live control values.
View selection and effective settings are resolved once per core update, with
no persistent cache to invalidate when the host changes observations.

`localization/*.json` is the translation source. The checked-in
`src/localization.cpp` is generated by `tools/generate_localization.ps1`; its
ordinally sorted table uses binary lookup and falls back to English. Edit the JSON
catalogs and regenerate this file; do not edit its table manually.

Packaging is independent of these module boundaries. The Windows single-DLL
build combines core and acquisition exports in `gyrolib.dll`. `src/runtime.cpp`
prepares its embedded SDL/reader resources in a content-versioned per-user cache;
`src/runtime_sdl_loader.cpp` resolves delayed SDL imports to that verified module.
No filesystem work or process creation occurs in `DllMain`. The panel remains
static with the host's render integration. Modular/static builds bypass this
Windows packaging layer. See [distribution](DISTRIBUTION.md).
