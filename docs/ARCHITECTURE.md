# Architecture

[Documentation](INDEX.md) / Maintenance

GyroLib separates acquisition, motion processing, settings/presentation and the
game adapter. The core contains no engine, SDL or GUI types.

```text
SDL reader ──────┐
Steam provider ──┼─ physical identity + capabilities ─ stable motion source
Raw provider ───┘                                      │
                                  calibration + gravity fusion
                                                      │
Game observations ─ view selection + activation ─ motion processing
                                                      │
                                           angular output + flick
                                                      │
Game adapter ───────── camera / local cursor / native-input suppression

Native widgets ─┐
Static panel ───┼─ shared settings model ─ inheritance + persistence
DX12 overlay ───┘
```

## Module boundaries

| Module | Responsibility |
|---|---|
| `src/core.cpp` | Endpoints, queues, freshness, selection, activation, host gates and output |
| `src/motion.cpp` | Calibration, gravity fusion and time-based motion processing |
| `src/detail/flick.hpp` | Flick and long-press blocking state machines |
| `src/contexts.cpp` | Registered views, observations, priorities and destinations |
| `src/inheritance.cpp` | Profile resolution, local exceptions and recommendations |
| `src/settings.cpp` | Validated edits, menu metadata and INI persistence |
| `src/sdl.cpp` | SDL references, event observation and control acquisition |
| `src/steam.cpp` | Borrowed public-service callbacks |
| `src/steam_runtime.cpp` | Windows bridge to the host's loaded public Steam Input API |
| `src/panel.cpp` | Shared ImGui widgets |
| `src/overlay.cpp` | Windows DX12 renderer and owner/render thread bridge |

Game adapters translate commands/axes and apply output. They provide cursor hit
testing, camera recentering and native-input gates; they do not reimplement gyro
math. `examples/tps/host.hpp` demonstrates those boundaries.

## Acquisition

SDL's event callback only copies bounded packets under a mutex. The owner thread
drains them and updates the core synchronously. The optional isolated reader
(`sensor_process.cpp`, `sensor_worker.cpp`) uses the same SDL controls/metadata
and the versioned protocol in `detail/sensor_wire.hpp`. No worker/IPC thread calls
the game camera. [INPUT.md](INPUT.md) defines source policy, ownership and pairing.

Physical stick/pad reports are resolved by family, separate from Steam's virtual
game commands. Independent flick histories avoid duplicate representations. The
optional `detail/touchpad_haptic.hpp` dispatches one documented right-pad pulse;
the core itself emits hardware-independent feedback requests.

Both Steam readers borrow the host's service without initializing, advancing or
shutting it down. The loaded-runtime bridge resolves its public exports; the
callback adapter accepts a host provider. `examples/steamworks_bridge.hpp` is an
optional typed bridge for hosts that have their own Steamworks SDK.

## Motion lifetime and timekeeping

GamepadMotionHelpers v10 supplies gravity fusion and Player/World projections,
with the retained gravity-initialization patch. Endpoint addresses stay stable
because its math object contains pointers to its own settings.

Ordinary activation, view and menu changes keep fusion alive. Source loss,
sensor-clock restart or controller transition restarts timing and seeds gravity
without integrating the missing interval. `detail/sensor_clock.hpp` qualifies
SDL clock frequency from long-term arrival history; agreement between both window
halves prevents batching/delay steps from becoming sensitivity changes. There
are no controller-name gain coefficients. Steam uses the host clock and receives
no local bias calibration.

View/effective settings are resolved once per update. Controller metadata refreshes
on discovery/remap and a 500 ms fallback; per-poll work reads live controls.
[Motion processing](ADVANCED_MOTION.md) specifies filters and calibration.

## Presentation and threads

The native menu and ImGui panel use the same C metadata, setters and actions.
`localization/*.json` is the translation source; `tools/generate_localization.ps1`
generates the checked-in binary-search catalog with English fallback.

The DX12 frontend owns an isolated ImGui implementation. `gl_overlay_process`
applies queued UI edits before motion processing; a successful `gl_update`
publishes a UI-only snapshot after resolving input. Render callbacks never read
live fusion state. Detach removes the publication callback. The static frontend
instead uses the host's ImGui/renderer. See [menus](MENUS.md) and [overlay](OVERLAY.md).

## Packaging

The default Windows DLL combines core/acquisition exports and the optional overlay.
`src/runtime.cpp` prepares embedded SDL/worker resources in a content-versioned
user cache; `runtime_sdl_loader.cpp` resolves delayed SDL imports to that module.
No file work or process creation occurs in `DllMain`. Modular/static builds bypass
this packaging layer. [Distribution](DISTRIBUTION.md) defines runtime files and
[third-party provenance](THIRD_PARTY.md) records retained code and notices.
