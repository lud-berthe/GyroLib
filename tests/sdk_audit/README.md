# SDK audit reproductions

Findings, fixes and historical results are kept in
[docs/SDK_AUDIT.md](../../docs/SDK_AUDIT.md).

The source checkout provides these public-API checks:

- `probes.cpp`: review A, including allocation failure injection (static core only).
- `reaudit.cpp`: review B, settings, inheritance, input families and notifications.
- `audit3.cpp`: review C, delayed flick reports, inherited curves and device removal.
- `audit4.cpp`: review D, global output notifications and 600 settings roundtrips.
- `../sdk_audit_loop_tests.cpp`: review E, asserted regressions for trigger-label
  events, uninterrupted flick in an explicitly targeted view, and persistence of
  per-field inheritance. It runs both in the main suites and against installed SDKs.

- `audit_f.cpp` / `audit_f_panel.cpp`: review F, controller shortcut continuity
  and active-view selection across panel opening paths.

The A–D probes print observations; exit 0 does not assert correctness. Reviews E
and F use assertions and run through CTest. None uses a real controller, game or
player settings file.

## Installed static core

The core build must already be up to date. Installing it to the separate audit
prefix does not replace the player's distributed SDK.

```powershell
cmake --install build-core-overlay-disabled --config Release --prefix build-sdk-audit/core-sdk
cmake -S tests/sdk_audit -B build-sdk-audit/probes -A x64 `
  -DCMAKE_PREFIX_PATH="$PWD/build-sdk-audit/core-sdk"
cmake --build build-sdk-audit/probes --config Release
./build-sdk-audit/probes/Release/sdk_audit_probes.exe
./build-sdk-audit/probes/Release/sdk_reaudit_probes.exe
./build-sdk-audit/probes/Release/sdk_audit3_probes.exe
./build-sdk-audit/probes/Release/sdk_audit4_probes.exe
ctest --test-dir build-sdk-audit/probes -C Release --output-on-failure
```

The allocation probe uses `/EHa`: MSVC `/EHsc` assumes `extern "C"` functions
cannot throw, which is the contract being checked. Its injected allocator applies
only to that statically linked test process. Synthetic Steam endpoints do not
invoke Steamworks or validate a real Steam Input integration.

## Installed DLL

```powershell
cmake -S tests/sdk_audit -B build-sdk-audit/reaudit-dll -A x64 `
  -DCMAKE_PREFIX_PATH="$PWD/dist/sdk" -DGL_AUDIT_PUBLIC_ONLY=ON
cmake --build build-sdk-audit/reaudit-dll --config Release
$env:PATH="$PWD/dist/sdk/bin;"+$env:PATH
./build-sdk-audit/reaudit-dll/Release/sdk_reaudit_probes.exe
./build-sdk-audit/reaudit-dll/Release/sdk_audit3_probes.exe
./build-sdk-audit/reaudit-dll/Release/sdk_audit4_probes.exe
ctest --test-dir build-sdk-audit/reaudit-dll -C Release --output-on-failure
```

Use a scratch working directory for reviews B and D: they create and remove
`reaudit-preset.ini` and `audit4-roundtrip.ini` respectively. On Windows, normalize duplicate Path/PATH
entries as in `tools/build.ps1` if MSBuild rejects the inherited environment.

## Review F regressions

`audit_f.cpp` asserts that reconnecting with the same identity and resuming after
an input/update gap cannot trigger a held menu chord. `audit_f_panel.cpp` compares
the active tab after controller and keyboard opening in the actual ImGui panel.
These tests reproduced the two findings before correction and now also run in
the main suite. They cover provider changes, capability loss, release/rearm,
opening state, API opening and tab preservation while editing. DX12 consumers
verify the selected view through a real slider edit on the render thread.

For the bundled SDK, from the repository root (PowerShell):

```powershell
cmake -S tests/sdk_audit -B build-audit-f -A x64 -DGL_AUDIT_PUBLIC_ONLY=ON -DGL_AUDIT_PANEL=ON -DGyroLib_DIR="$PWD/dist/sdk/lib/cmake/GyroLib"
cmake --build build-audit-f --config Release
$env:PATH="$PWD/dist/sdk/bin;$env:PATH"
ctest --test-dir build-audit-f -C Release -R '^audit_f_' --output-on-failure
```

The optional panel probe uses the repository's ImGui internal headers and SDL
renderer backend for inspection. The supplied SDK must be built from this same
checkout. Its window is hidden; it does not open controllers or games. Use
`GL_AUDIT_PANEL=OFF` for a core-only static SDK.
