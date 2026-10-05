# Compatibility and deprecated APIs

[Documentation](INDEX.md) / Maintenance

This page is for maintaining existing integrations. Deprecated APIs remain
available so older mods can load newer compatible DLLs. Use their replacements
for new code. Removing a published API requires a major release, as described
in [Versioning](VERSIONING.md).

## Instant-only recenter callback

| Deprecated API | Replacement |
|---|---|
| `gl_set_recenter_callback` / `gl_recenter_callback` | `gl_set_recenter_step_callback` / `gl_recenter_step_callback` |

The replacement is available starting with 1.2.0. No removal of the original
callback is scheduled.

The original callback asks the host to level camera pitch in one operation.
It keeps that behavior and does not expose a duration setting. Replacing only
the DLL does not enable progressive recentering in a mod using this callback.

To migrate, register the step callback instead. It receives the fraction of
remaining pitch to remove on each update; GyroLib handles timing and progression.
The default duration is **0 ms**, which requests a full recenter immediately.
The existing camera access, button binding and `gl_request_recenter` calls stay
the same. See [Recenter camera](ADVANCED_MOTION.md#recenter-camera) for the callback
formula and output ordering.

Either registration replaces the other. Passing NULL to either setter withdraws
recenter support and cancels any running transition. A mod using the replacement
must ship or require a DLL that exports it.
