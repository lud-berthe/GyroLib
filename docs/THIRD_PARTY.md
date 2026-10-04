# Licenses and provenance

[Documentation](INDEX.md) / Maintenance

GyroLib is distributed under the MIT license in the root `LICENSE`.
Third-party components and their notices are listed below.

## Notices to ship

Keep `share/doc/GyroLib/licenses` with the mod package, along with SDL's alteration
notice and relevant distribution materials. Notices may be consolidated without
removing copyright, conditions or disclaimers. The compact binary layout does not
remove these obligations.

Embedding SDL inside `gyrolib.dll` is still redistribution. SDL's
[licensing FAQ](https://wiki.libsdl.org/SDL3/FAQLicensing) covers static/shared and
commercial use. The [exact version's license](https://github.com/libsdl-org/SDL/blob/release-3.4.16/LICENSE.txt)
requires accurate origin, marking altered source versions and retaining the notice
in source distributions. This project also includes it with binary packages.

HIDAPI [offers alternative licenses](https://raw.githubusercontent.com/libsdl-org/SDL/release-3.4.16/src/hidapi/LICENSE.txt).
This distribution retains its [BSD-style notice](https://raw.githubusercontent.com/libsdl-org/SDL/release-3.4.16/src/hidapi/LICENSE-bsd.txt).
MIT-covered components keep their corresponding copyright/license text even when
compiled into a DLL or executable.

GyroLib adapter DLLs and its sensor worker are project components. SDL3.dll is the
embedded third-party DLL; ImGui and GamepadMotionHelpers are compiled code. No
Windows system DLL or MSVC runtime is embedded/copied. Release builds require the
x64 Visual C++ runtime, distributed under [Microsoft's separate terms](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).
Use its permitted deployment mechanism rather than copying arbitrary System32 DLLs.

This records the dependency/licensing choices checked on 30 September 2026. It
is not permission to redistribute arbitrary proprietary game or Steam components.

## Included components

| Component | Version / origin | Retained notice |
|---|---|---|
| GamepadMotionHelpers | v10, Julian “Jibb” Smart | `third_party/GamepadMotion.LICENSE` (MIT) |
| Gravity initialization extension | GyroLib extension | `third_party/patches/GamepadMotion-gravity-initialization.patch`, root MIT |
| SDL | Altered Windows x64 3.4.16 build with touchpad backport and physical-origin properties | `third_party/SDL/LICENSE.txt` (zlib), changes in `third_party/patches` |
| HIDAPI in SDL | Vendored with release-3.4.16 | `third_party/SDL/HIDAPI-LICENSE.txt` (BSD-style alternative) |
| Dear ImGui | 1.92.9b, core and SDL3/SDL renderer/DX12 backends | `third_party/imgui/LICENSE.txt` (MIT) |
| Embedded fonts / stb | ProggyClean, ProggyForever, stb | `third_party/imgui/FONTS-LICENSE.txt`, `STB-LICENSE.txt` |
| Localization | GyroLib catalogs for English, French, German, Spanish, Italian and Portuguese | Root MIT |

The independently written adaptive filters, tightening, flick options and Lean
calculations follow mathematical descriptions linked in [motion processing](ADVANCED_MOTION.md#references-and-tests).
JoyShockMapper is not linked or vendored. GamepadMotionHelpers and its existing
MIT notice are retained.

No proprietary game assets/widgets/code, Steamworks SDK, Valve binaries, MinHook,
proxy loader or injection utility are included. Brand names describe interoperability,
not endorsement. The optional typed Steam bridge is for hosts supplying their own
licensed SDK/service under its separate terms.

The loaded Steam runtime bridge contains independently written declarations for
four public flat functions and the ten-float motion record. Their signatures and
axis comments were checked against Valve's public Steam Input headers and
documentation.
Those headers are not copied into this SDK. The bridge uses the game's existing
Valve binary; GyroLib does not distribute one. The test `steam_api64.dll` is a
local stub built from project source, never installed or shipped with a mod.

## SDL modifications and feedback

Direct and isolated acquisition use the same marked altered SDL build. Its Steam
Controller 2015 touchpad backport comes from upstream. Physical-origin properties
in the existing Steam drivers add metadata, not packet parsing or configuration
writes. Controller input parsing stays in SDL.

The archive hash, patches and reconstruction steps are in
[the SDL change notice](../third_party/SDL/README-GyroLib.md). Installed SDKs retain
them under `share/doc/GyroLib/sdl-changes`. SDKs built with vendored SDL also carry
its development package and notices under `third_party/SDL3`.

The optional right-pad feedback code independently implements one output report
from SDL's [MsgHapticPulse layout](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/hidapi/steam/controller_structs.h)
and the Linux driver's [pulse selector convention](https://github.com/torvalds/linux/blob/0a80b4e8ec6a6e40937c7abdf8fb2ebe6cc7c1e5/drivers/hid/hid-steam.c).
No Linux driver implementation is copied or linked; it includes no feature-report
or input-decoder implementation.

## ImGui backend provenance

The unmodified DX12 backend comes from the official
[v1.92.9b tag](https://github.com/ocornut/imgui/tree/v1.92.9b/backends):

| File | SHA-256 |
|---|---|
| `imgui_impl_dx12.cpp` | `364096C20FB3B873666207947F4B4890BF8233629A678396EC01290D1DD33E0F` |
| `imgui_impl_dx12.h` | `6E11AD5BAE5B5B6339EA09545C3B4BC21F9028D7F5D1AF6B508181EB43F4323C` |

The autonomous frontend isolates ImGui symbols with generated compile-time
configuration, without editing vendor sources. DX12/DXGI and shader compiler
libraries are Windows dependencies, not redistributed Microsoft DLLs.
