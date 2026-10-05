# GyroLib documentation

Start with the integration path below. The reference guides explain individual
features; the maintenance guides cover building, testing and releases.

## Integrate a mod

| Order | Guide | What you will do |
|---|---|---|
| 1 | [First integration](QUICKSTART.md) | Compile a small host against the installed SDK |
| 2 | [Host integration](API.md) | Set up ownership, threading, the frame loop and output hooks |
| 3 | [View profiles](CONTEXTS.md) | Declare camera/cursor views and report resolved game commands |
| 4 | [Settings and INI](SETTINGS.md) | Load preferences, save edits and configure menu shortcuts |
| 5 | [Inheritance and recommendations](INHERITANCE.md) | Share settings between views and supply a recommended preset |
| 6 | [Menus](MENUS.md) | Use the supplied panel or build native widgets from the model |
| 7 | [Distribution](DISTRIBUTION.md) | Package the runtime and notices without overwriting player settings |

For a DLL-owned DX12 menu, follow [DX12 panel integration](OVERLAY.md) at step 6.
The [GyroLib Demo](TPS_DEMO.md) provides a working host and native-menu example.

## Feature reference

| Guide | Contents |
|---|---|
| [Input acquisition](INPUT.md) | SDL, Steam fallback, controller identity, capabilities and recovery |
| [Gyro spaces](SPACES.md) | Axis conventions, projections, inversion and sensitivity |
| [Motion processing](ADVANCED_MOTION.md) | Activation, calibration, filters, flick, recenter and zoom |
| [Known limits](LIMITS.md) | Supported scope and remaining platform/hardware restrictions |

The [C header](../include/gyrolib/gyrolib.h) is the function and struct reference.
The [C++ wrapper](../include/gyrolib/gyrolib.hpp) adds ownership and checked calls.
Renderer, SDL and Steam interfaces have separate headers in the same directory.

## Build and maintain

| Guide | Contents |
|---|---|
| [Building and linking](BUILDING.md) | SDK components, source builds and installation checks |
| [Architecture](ARCHITECTURE.md) | Module responsibilities and data flow |
| [Demo performance](PERFORMANCE.md) | Repeatable measurements and renderer comparison |
| [Validation](VALIDATION.md) | Test commands, current results and hardware acceptance |
| [Versioning](VERSIONING.md) | Library, ABI and INI compatibility rules |
| [Compatibility and deprecated APIs](COMPATIBILITY.md) | Replacements and migration for existing integrations |
| [Release notes](RELEASE_NOTES.md) | Published versions, downloads and compatibility |
| [Licenses and provenance](THIRD_PARTY.md) | Dependencies, local changes and notices to ship |
| [SDK audit](SDK_AUDIT.md) | Consolidated findings, fixes and historical evidence |
