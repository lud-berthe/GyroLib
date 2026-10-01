# Known limits

Windows x64 with MSVC is the tested platform. The specific automated and manual
checks are recorded in [VALIDATION.md](VALIDATION.md). A working demo session
does not establish support for every controller, transport or host game.

## Platforms and host integration

- Linux native builds, Linux shared objects, Steam Deck, Wine and Proton have not
  been validated. The vendored SDL development package is for Windows x64.
- GyroLib supplies no engine hooks or camera discovery. The host must observe
  valid views, focus, pause and menu state, apply output once, and respect the
  owner-thread/SDL-main-thread rules in [API.md](API.md).
- Flick requires suppressing native camera rotation for the same stick/pad.
  Short-press filtering requires a separate action-event hook and must not filter
  aim/Alt-Fire. Leave unsupported capabilities undeclared.
- The optional ImGui panel requires the host's rendering/input integration and a
  compatible or fully isolated ImGui implementation; see [MENUS.md](MENUS.md).
  Translations outside English/French partly fall back to English.

## Acquisition and controller identity

- Controller/transport support comes from SDL. Existing host-owned sensor handles
  remain under host control. Opening or enabling a device can cause SDL hardware
  setup, including device-mode or light changes; coexistence with another driver
  stack needs a real-game check.
- The optional isolated reader remains a separate process, even in the bundled
  DLL. Windows launches under Steam need an available desktop shell. Its lifecycle
  and physical/virtual input policy are defined in [INPUT.md](INPUT.md).
- Automatic sensor association uses bounded session evidence, not universal
  device identity. Ambiguous or unknown pairs require explicit selection.
  Reconnection without a stable serial/path can require renewed association.
- Unknown physical control origins require provider metadata. GyroLib cannot
  infer reliable contacts, labels or physical axes from arbitrary virtual mappings.
- Steam Input requires the host's initialized public service. Its typed SDK bridge
  has not been compiled against a licensed Steamworks SDK; the C callback adapter
  is tested with a fake provider. There is no private-interface discovery fallback.
  Steam's API can provide a frozen nonzero sample without a hardware timestamp,
  which cannot always be distinguished from a stationary controller.
- Steam gyro-to-mouse/stick output is not fully inspectable. Verify that it is
  disabled to avoid doubled movement. Pad-to-mouse output cannot reliably be
  distinguished from real mouse input when using touchpad flick.
- Right-pad feedback currently targets Steam Controller 2026 USB/puck only.
  BLE/other devices are unsupported by that output backend. Routing, packet
  contents and rate limiting have virtual-device coverage; physical sensation,
  Steam/native-feedback coexistence and vibration coexistence remain unverified.
  Feedback is opt-in and does not change motor rumble or persistent settings.

## Motion accuracy and acceptance

Sensor-clock correction needs several seconds of qualified SDL reports after a
cold start. Accurate timestamps do not establish a physical sensor's angular scale
or compensate for motion leaving a selected local axis. World Space also depends
on estimated gravity. Manual full-turn checks have shown residual alignment error
without an independent physical-angle reference; exact hardware accuracy remains
unverified. No empirical controller-specific gain is used to force endpoint matches.

Gyro-only devices can use local spaces. Gravity-dependent spaces and calibration
require usable acceleration. Automatic calibration cannot distinguish every very
slow steady rotation from bias; larger drift needs a safe calibration interval
or manual calibration. An active-use correction guard reduces that risk without
eliminating it. See [ADVANCED_MOTION.md](ADVANCED_MOTION.md).

Smoothing, acceleration, Lean, trigger activation, temporary inversion, trackball,
recenter/zoom and stick/pad flick have synthetic coverage. Their feel, drift and
end-to-end latency require physical acceptance. Finishing a flick smoothing tail
on release preserves angle but can produce a small final motion. Input metrics
measure acquisition frequency, jitter and age, not input-to-photon latency.
Exact parity with Valve's internal conversion coefficients or feel is not claimed.

Settings migration covers GyroLib schemas, not arbitrary game-mod INI files.
View IDs, names and command observations remain host-owned. Legacy fixed aim
fields cannot establish game integration; migration requiring a view decision is
explicitly documented in [CONTEXTS.md](CONTEXTS.md).
