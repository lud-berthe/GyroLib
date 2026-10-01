# Licensing and provenance

GyroLib's adaptations retain the MIT license and copyright of ReturnalGyro
contributors in the root `LICENSE`. Source reference:
https://github.com/lud-berthe/Returnal-Native-Gyro . No exact public revision is
claimed for the initial extraction from the local development copy.

| Component | Version / origin | License retained |
|---|---|---|
| GamepadMotionHelpers | v10, Julian "Jibb" Smart, from the local reference | `third_party/GamepadMotion.LICENSE` (MIT) |
| Local gravity initialization extension | Same reference, patch retained verbatim | `third_party/patches/GamepadMotion-gravity-initialization.patch`, root MIT |
| SDL | Altered Windows x64 build of 3.4.16, upstream touchpad backport and physical-origin properties | `third_party/SDL/LICENSE.txt` (zlib), retained changes in `third_party/patches` |
| HIDAPI, included by SDL's controller backend | SDL release-3.4.16 vendored HIDAPI | `third_party/SDL/HIDAPI-LICENSE.txt` (BSD-style alternative selected) |
| Dear ImGui | 1.92.9b, core and SDL3/SDL renderer backends | `third_party/imgui/LICENSE.txt` (MIT; embedded font and stb notices also remain in source) |
| ImGui embedded fonts and stb helpers | ProggyClean / ProggyForever / stb | `third_party/imgui/FONTS-LICENSE.txt`, `third_party/imgui/STB-LICENSE.txt` |
| Relevant localization strings | Six JSON catalogs from ReturnalGyro, adapted for this API | Root MIT |

The adaptive filtering, tightening, flick options and Lean calculations added in
schema 11 are independently written implementations of mathematical behavior
described by Julian "Jibb" Smart. Their sources of inspiration are linked in
[ADVANCED_MOTION.md](ADVANCED_MOTION.md). They do not vendor or link JoyShockMapper.
The existing GamepadMotionHelpers source and its MIT notice are unchanged.

No Steamworks SDK or Valve binaries are downloaded or redistributed. The optional
borrowed bridge example is for hosts already using their own Steamworks service
and SDK, under its separate terms. Direct and isolated acquisition use the same
marked, altered SDL build. It includes SDL's upstream Steam Controller 2015
touchpad patch and descriptive physical-origin properties in the existing Steam
drivers. The latter add no packet parsing or hardware configuration writes.
The earlier in-library Triton decoder has been removed. The source archive hash,
patches and reproduction procedure are retained in
[the SDL change notice](../third_party/SDL/README-GyroLib.md).
The optional right-touchpad feedback backend is independently written and uses
protocol facts from SDL's [MsgHapticPulse output layout](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/hidapi/steam/controller_structs.h)
and the Linux driver's [right/left pulse selector convention](https://github.com/torvalds/linux/blob/0a80b4e8ec6a6e40937c7abdf8fb2ebe6cc7c1e5/drivers/hid/hid-steam.c).
No Linux driver implementation is copied or linked. It writes one output report,
with no feature-report or input-decoder implementation.
SDL's zlib notice remains in the distribution. No Returnal or
Monster Hunter Rise assets, widgets, binaries or proprietary code are copied into
this library. No MinHook, proxy loader, injection utility or game signatures are
included. Brand names identify interoperability context, not endorsement.

## Bundled runtime and redistribution

Packaging our altered SDL3.dll inside GyroLib's Windows resources remains a
redistribution of SDL. The [official SDL3 licensing FAQ](https://wiki.libsdl.org/SDL3/FAQLicensing)
permits static and shared use, including commercial applications without royalties.
The [license for our exact SDL version](https://github.com/libsdl-org/SDL/blob/release-3.4.16/LICENSE.txt)
requires accurate attribution of origin, marking altered source versions, and
retaining the notice in source distributions. We also ship it with binary packages,
along with the alteration notice, patches and metadata helper under
`share/doc/GyroLib/sdl-changes`. The original source archive is identified by its
SHA256 in the reconstruction script; our dependency is not represented as an
unmodified SDL release. SDKs built with the vendored Windows dependency also
include its headers, import library, DLL, CMake package and notices under
`third_party/SDL3`, so direct SDL consumers need no source checkout.

SDL's vendored HIDAPI [offers a choice of licenses](https://raw.githubusercontent.com/libsdl-org/SDL/release-3.4.16/src/hidapi/LICENSE.txt).
For our redistribution we retain its [BSD-style notice](https://raw.githubusercontent.com/libsdl-org/SDL/release-3.4.16/src/hidapi/LICENSE-bsd.txt),
including the copyright, conditions and disclaimer in the distributed materials.
The listed GPL alternative does not force that choice on this distribution.

MIT-covered code (GyroLib adaptations, GamepadMotionHelpers, ImGui, fonts and the
selected stb license) requires the corresponding copyright and license texts to
accompany distributed copies, even when compiled into a DLL or executable.
Keep the installed `share/doc/GyroLib/licenses` notices with the mod package;
they can be consolidated into a notices document without removing their content.
The two-file mod layout describes runtime binaries, not permission to omit notices.

The former gyrolib_sdl.dll / gyrolib_steam.dll and the sensor worker are project
components, not third-party SDL/Valve binaries. SDL3.dll is the third-party DLL
embedded by the new packaging; ImGui and GamepadMotionHelpers are compiled code.
No Windows system DLLs or MSVC runtime DLLs are embedded/copied into the package.
System DLLs are resolved from Windows. Release builds require the x64 Visual C++
runtime, whose redistribution has [separate Microsoft terms](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).
Do not copy arbitrary DLLs from System32 into a release; use the permitted official
runtime deployment mechanism if that prerequisite needs to be provided.

This records the licenses and packaging choices checked on 2026-09-30, not a grant
to redistribute arbitrary DLLs or proprietary game/Steam components.

SDL API behavior was checked against the vendored headers and the
[official SDL sensor documentation](https://wiki.libsdl.org/SDL3/SDL_GetGamepadSensorData).
Steam scaling/lifecycle references use the
[official ISteamInput documentation](https://partner.steamgames.com/doc/api/ISteamInput).
