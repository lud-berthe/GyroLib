# Known limits

[Documentation](INDEX.md) / Reference

Windows x64/MSVC is the tested platform. [Validation](VALIDATION.md) records
specific automated results and earlier hardware observations. Those results do
not certify every controller, transport or host game.

## Integration and platforms

- The mod supplies game hooks, active views, camera/cursor output and reliable
  focus/pause/menu observations. Calls follow the [owner-thread contract](API.md).
- Flick needs native camera-input suppression. Long-press blocking needs a
  separate event hook and must not filter aim/Alt-Fire.
- The built-in renderer supports [Windows DX12 SDR](OVERLAY.md) only. Other renderers
  can use the static panel or native model. Real-game GPU performance is unmeasured.
- Linux builds/loading, Steam Deck, Wine, Proton, HDR and other game renderers are
  not validated. The vendored SDL development package is Windows x64.
- Library translations cover six catalogs; the mod translates its own view labels.
  Six native-speaker reviews have not been performed.

## Controllers and Steam

Controller/transport support follows SDL. Hardware setup on open or sensor
enablement may alter device modes/lights. Input and vibration coexistence with
other driver stacks needs real-game testing. Host-owned sensor handles remain
under host control.

Steam-filtered acquisition may require a separate hidden reader and an available
Windows desktop shell. Session pairing is deliberately limited; ambiguous devices
need explicit selection. Reconnection without stable identity can require pairing
again. Physical contacts/origins cannot be recovered from arbitrary virtual mappings.

The public Steam adapter needs the host's initialized service; a loaded DLL alone
is insufficient. The loaded-runtime bridge has been validated on one Windows
setup with a Steam Controller 2026/puck. Other combinations remain unverified;
see [Steam Input validation](VALIDATION.md#steam-input-validation). The optional
typed example has not been compiled against a licensed SDK. Steam samples lack
a hardware timestamp, so a frozen nonzero value may be indistinguishable from
stillness.

Steam gyro-to-mouse/stick mappings and external remappers are not fully inspectable.
Disable them to avoid doubled motion. Pad-to-mouse events also cannot reliably be
distinguished from a real mouse during touchpad flick.

Right-pad feedback supports Steam Controller 2026 USB/puck only. BLE and other
devices are unsupported by that output backend. Packet/routing/rate-limit tests
exist; physical sensation and native/Steam feedback coexistence remain unverified.

## Motion accuracy

SDL clock correction requires several seconds of qualified reports. It does not
calibrate a physical sensor's angular scale or undo a chosen space's projection.
World Space depends on estimated gravity. Earlier full-turn checks retained a
small alignment error without an independent angle reference; exact physical
accuracy remains unverified. No controller-specific gain forces endpoint matches.

Local spaces work with gyro-only devices. Gravity-dependent spaces and calibration
need usable acceleration. Automatic calibration cannot distinguish every slow
intentional turn from bias; larger drift needs manual calibration or a safe idle
interval. Its active-use guard reduces, but does not eliminate, that ambiguity.

Filters, modifiers and flick have synthetic coverage. Feel, drift and end-to-end
latency need hardware acceptance. Completing a flick smoothing tail on release
can produce a small final movement. Acquisition metrics do not measure display
latency. Exact equivalence with Valve's internal processing is not claimed.

## Files and deployment

Only `schema=0.2.0` is supported; abandoned development schemas are not imported.
A settings path has one owner; simultaneous writers are not supported. The bundled
runtime has cache integrity checks, but every antivirus/hardened environment has
not been tested. See [settings](SETTINGS.md), [distribution](DISTRIBUTION.md) and
[versioning](VERSIONING.md).
