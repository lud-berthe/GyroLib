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

## Saving and errors

After initialization, changed settings, language and reset/recommended actions
save synchronously on the context owner thread. An unchanged value does not
write. Input polling and controller fallback do not save settings. Slider edits
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
