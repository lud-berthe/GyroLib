# Settings and INI files

[Documentation](INDEX.md) / Integration

All frontends edit the same settings on the same `gl_context`. The library owns
validation, inheritance, controller fallback and persistence. A mod should not
keep a second copy of player settings.

## Initialize once

Register views, apply the mod's defaults and optionally capture
[recommended settings](INHERITANCE.md) before calling:

```cpp
check(gl_initialize_settings(gyro, nullptr, nullptr));
```

`check` is the host's result-code handler. Initialization loads an existing file
or creates `gyrolib.ini` beside the module containing GyroLib. For DLL builds this
is beside `gyrolib.dll`; for static linkage it is beside the executable or mod
containing the library. The working directory and runtime cache do not affect it.
The filename is available as `GL_SETTINGS_FILENAME`.

The optional directory argument selects an existing UTF-8 directory. The optional
legacy-file argument imports that file only if the destination does not exist;
the original is left intact. Parent directories are not created automatically.
A context without a configured path uses in-memory settings.

If initialization fails, automatic saving is disabled. Report the error, repair
the file or path and retry initialization. Do not bypass the failure with
`gl_set_settings_path`. Existing files are read without rewriting them on startup.

## Menu shortcut

Close the host before editing the INI and relaunch afterward:

```ini
ui.menu_key=F8
```

F1 through F24 are accepted, case-insensitively. A missing key defaults to F10.
An empty value disables the keyboard shortcut:

```ini
ui.menu_key=
```

Back + Start also toggles the panel by default (Select + Start, Xbox View +
Menu, PlayStation Create/Share + Options, or Nintendo Minus + Plus). It uses
normalized input buttons 4 and 6, when both are exposed by the controller.
Release both buttons before pressing the combination again.

```ini
ui.gamepad_menu_shortcut=0
```

This disables only the controller shortcut. A host with a native menu can call
`gl_set_gamepad_menu_shortcut(context, 0)` after loading settings and
`gl_set_menu_key(context, 0)` to disable both shortcuts. Settings remain usable.
Reset and recommended profiles keep these preferences and the language.

The controller shortcut runs in `gl_update`, using fresh input from the selected
controller while the host has focus. It also works while paused or in menus;
no camera or gyro capability is required. Held buttons after a reconnect, stale
input or focus loss must first be released. GyroLib does not intercept these
buttons in the game: the host retains its input routing and panel capture logic.
Keyboard events still need the chosen [panel frontend](MENUS.md#choose-a-frontend).

## Values and edits

| API | Value returned |
|---|---|
| `gl_setting_get` | Configured preference, with view inheritance resolved |
| `gl_setting_get_effective` | Preference adapted to currently usable controller inputs |
| `gl_setting_info.value` | Same adapted value, for display in a menu |

Call `gl_setting_set` for a player edit, not because an effective value changed.
For example, Stick or Touchpad displays Stick on a stick-only controller, then
returns to the combined choice when the fuller controller reconnects. This
fallback does not modify the saved preference or create an inheritance override.
See [menu availability](MENUS.md#controller-availability).

Setters reject nonfinite or out-of-range values and round to the advertised step. Coupled settings, such as an acceleration preset and its curve, are updated
together. Use stable setting IDs; labels and list positions may change.

## Manual calibration memory

A completed manual calibration saves the three-axis bias and measured noise in
`gyrolib.ini`, independently of view presets. Recalibrating measures them again;
only success replaces the saved result. Reset/recommended view settings preserve
these sensor measurements. Automatic drift adjustments are not written back.

The SDL reader and isolated reader identify individual sensors using a hash of
VID, PID and the hardware serial. Names, model IDs alone, Steam pairing handles,
temporary instance IDs and reusable port paths are never used as calibration
keys. No raw serial is written to the INI. Without a serial, the bias remains
session-only. If two connected endpoints claim the same calibration identity,
its saved record is removed and reuse is disabled for that context's lifetime.

Custom SDL-source producers can supply a stable individual-sensor identity with
`gl_set_endpoint_calibration_identity` after endpoint registration; zero disables
persistence. The built-in readers do this automatically. Steam-source endpoints
reject this API because they must not receive an SDL calibration offset.
Load-before-discovery and discovery-before-load are both supported.

Records use `calibration.device.<16-digit hex identity>.manual=x,y,z,variance`.
Bias is in degrees/second; variance is the summed gyro noise variance. Malformed
or nonfinite records reject the load transaction. These entries are personal
hardware data: do not include them in a recommended preset or mod release.

## Saving and errors

After initialization, changed settings, language and reset/recommended actions
save synchronously on the context owner thread. An unchanged value does not
write. Completion of a manual calibration saves its reference once. Detection
of a duplicate calibration identity removes its ambiguous record. Ordinary
input polling, automatic bias refinement and controller fallback do not write.
Slider edits
may save every changed tick; a host with slow storage can commit on edit completion.

Check the setter/action result and `gl_get_settings_save_result`. If saving fails,
the edit remains active in memory and the previous file stays intact. Show the
error and allow retry. `GL_UNAVAILABLE` means no save path is configured.

Low-level load/save/path APIs support host-managed files. Load before editing an
existing file; use an empty path for a temporary session. The hidden
`settings.save` action remains available for explicit retry/export workflows.
`gl_panel_create(context, NULL)` preserves the current persistence configuration.

Saving writes a temporary sibling and atomically replaces the destination
(`MoveFileEx` on Windows, `rename` on POSIX). One host must own a path: this is
not a cross-process lock or a guarantee against every power-loss scenario.

## File format

The current format is `schema=0.2.0`. Its shared preferences are:

```ini
schema=0.2.0
ui.language=en
ui.menu_key=F10
ui.gamepad_menu_shortcut=1
calibration.automatic=0
```

All gyro, activator, flick and camera preferences belong to a view, for example
`context.101.sensitivity_x=2.5`. There is no implicit default camera profile.
Independent views store their settings in full; inherited views store a parent
and local exceptions. [Inheritance](INHERITANCE.md#persistence) describes the
`context.<id>.inherit` key and explicit `=inherit` values.

Profiles absent from a file retain host defaults. Profiles for temporarily
unregistered views and unknown key/value pairs are preserved.
[Reset and recommendations](INHERITANCE.md#set-up-a-recommended-profile) describe
which values those actions restore.

Numbers use a locale-independent decimal point. Duplicate keys, nonfinite
values, invalid ranges and malformed lines reject the load without changing
settings. Each line is limited to 4,096 bytes; the format has no fixed view count.
A save retains unknown key/value pairs but not comments or original ordering.

Unsupported formats are neither loaded nor overwritten. Failed initialization
also disables autosave. [Versioning](VERSIONING.md) defines schema errors and
future migration policy.

## Localization

Library text is supplied in English, French, German, Spanish, Italian and
Portuguese; English is the fallback. The mod owns its view names/descriptions:
read `gl_get_language` and re-register translated strings under the same view IDs.
Controller labels come from acquisition metadata; see [input labels](INPUT.md#button-and-trigger-names).

Edit `localization/*.json`, then run `tools/generate_localization.ps1`. The script
checks key parity and punctuation and regenerates the C++ catalog and test data.
It is not required for a normal build. Keep Latin accents; use straight quotes
and apostrophes supported by the bundled font. Catalog and glyph coverage is
recorded in [validation](VALIDATION.md).

---

Previous: [View profiles](CONTEXTS.md) · Next: [Inheritance and recommendations](INHERITANCE.md)
