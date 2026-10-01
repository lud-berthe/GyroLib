# Altered SDL dependency for physical control metadata

The default x64 dependency is the altered `SDL3-3.4.16-gyrolib` package. It retains
SDL's zlib license (`LICENSE.txt`) and vendored HIDAPI notices; it is **not an
unmodified SDL binary**. Only this development package is retained; the original
SDL source archive is fetched and verified when reconstructing it.

Windows SDK installations using this dependency include the complete relocatable
package under `third_party/SDL3`. Requesting GyroLib's `SDL` CMake component finds
it automatically; Core-only consumers do not discover SDL. Builds that use an
external SDL development package retain that external dependency.

Rebuild with `tools/build_sdl.ps1`. That script verifies the official source
archive SHA256 `7322236CD12090C3EB40B9728BE4D49C76F66AD17D04369584D4ECAD5CF77C68`,
then applies the three retained patches in order:

1. SDL's upstream [touchpad correction](https://github.com/libsdl-org/SDL/commit/74a746281f2208e07a7680560fcb7ec57565228e),
   which is missing from the stable 3.4 branch: genuine left/right finger contacts
   and coordinates on the Steam Controller 2015, through the normal SDL API.
2. Physical-origin metadata for the legacy Steam controller driver: one real
   stick; right logical axes remain available to games as pad output, but are
   identified separately from a physical stick.
3. Physical-origin metadata for the Triton driver: two sticks and its existing
   genuine stick/grip contacts, plus verified button names.

The last two patches add **descriptive joystick properties**, not packet decoders
or controller configuration writes. Their helper is `gyrolib_controls.h` (MIT).
Both acquisition paths consume the same property contract. GyroLib's processing,
capability aggregation and menus contain no model-specific topology decisions.
Drivers without metadata still expose only what SDL can read; no hidden hardware
feature is invented from a controller's commercial name.

The private property prefix is `gyrolib.controls.`: `authority` is a bitmask of
physical families; `sticks` is the physical left/right mask; `left.x_axis`,
`left.y_axis`, `right.x_axis`, `right.y_axis` identify raw SDL joystick axes.
`button.N.name`, `button.N.stick_touch` and `button.N.grip_touch` describe raw
button origins. The adapter resolves them through the current SDL bindings.
`gyrolib.output.right_pad_pulse` separately describes the existing optional
output backend; it is not evidence of a sensor or input capability.

No Steam SDK or proprietary Valve binary is included. This backport and metadata
are tested on Windows x64. Linux/Proton and other transports remain unvalidated.
