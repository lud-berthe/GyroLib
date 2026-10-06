# Mouse-source observation and validation

The initial observation and routing experiments below established the Windows
message path used by the per-view Steam Input mouse setting. The implementation introduced in 1.2.0 is described in [the overlay guide](../docs/OVERLAY.md#steam-input-mouse-movement).
`GL_EXPERIMENTAL_MOUSE_PROBE` now adds only the optional diagnostic recorder;
routing follows `input.steam_mouse` in regular overlay builds too. The old
`gyrolib-mouse-route.enable` marker is no longer read. The optional recorder is excluded from release binaries.

Version 1.2.1 also accepts Steam-managed mouse mappings without
pad contact or touchpad hardware. Automated routing tests cover pass/block/convert,
physical HID exclusion, inactivity and reconnection. The user confirmed the generalized route in game. The touchpad restriction
described below belongs to the prototype.

The remaining sections record the earlier prototype procedure and its evidence;
the enable marker and fixed routing mode described there are historical.

Build separately with `GL_EXPERIMENTAL_MOUSE_PROBE=ON` and `GL_BUILD_OVERLAY=ON`.
Normal builds exclude the probe. The existing host must already forward window
messages to `gl_overlay_win32_message`, including WM_INPUT, while the panel is
closed. No additional host callback is used.

Before starting the host, create `gyrolib-mouse-probe.enable` beside its initialized
`gyrolib.ini`. Each overlay writes `gyrolib-mouse-probe-<pid>.csv` there. With the
host focused and the panel closed:

1. Press F7 to start a 60-second capture. Move only the controller touchpad mapped
   to mouse for about ten seconds. Keep Steam gyro-to-mouse disabled when testing
   alongside GyroLib's gyro.
2. Press F8 and move only the physical mouse for about ten seconds.
3. Press F8 to mark the end of that phase, then leave the host running for analysis.

F7/F8 still reach the host. Records are limited to mouse-message metadata and
coordinates, plus these phase markers; no keyboard text is recorded. Each capture
is bounded to 50,000 rows and 60 seconds. Repeated F7 starts a new marked capture
in the same session. Raw packet fields and window-position messages are separate
observations: do not add their movement together.

Compare device handles/names, raw flags, message origin and extra-information
fields across phases. An injected origin alone does not identify Steam, and a
null raw-device handle can also occur for precision touchpads. These are clues
to test, not rules for capturing a user's mouse. Sources:
[Windows message origins](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ne-winuser-input_message_origin_id),
[raw input headers](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputheader).

After testing, close the host and restore its previous DLL; remove the enable
marker to prevent subsequent experimental sessions from opening a log. Keep the
capture with local test artifacts. Do not distribute the experimental DLL as a
release update.

## Optional camera route

Only after examining an observation capture, create `gyrolib-mouse-route.enable`
beside the same INI, containing `0.05` (degrees per mouse count), and restart with
the experimental DLL. Without that marker, the build remains observational.
The marker is separate from player settings and is read once at initialization.

The route starts while the selected controller reports a right-pad contact.
It consumes relative, device-less raw mouse motion, preserves packets carrying
buttons/wheels, and suppresses injected WM_MOUSEMOVE messages on the same active
path. Physical mouse devices and hardware-origin messages remain untouched.
An uninterrupted stream may continue after lift-off for Steam touchpad inertia;
150 ms without another accepted packet ends that permission. Host state must
have been updated within 100 ms. Focus loss, pause, the library panel, cursor
views, menus without explicit camera permission, controller changes and touchpad
Flick Stick stop or bypass the route. Queued
movement from an old view is discarded.

Raw deltas enter the usual camera output once, with right-positive yaw and
up-positive pitch. They retain their own counts-to-degrees scale and are not
passed through gyro calibration, spaces or gyro sensitivity. Existing optional
view zoom compensation applies before the final output. The mod's camera callback
or returned-output consumption stays unchanged; this path requires its existing
window-message forwarding to honor GyroLib's per-message capture result.

The CSV `routed` column records intercepted messages. `# CAMERA` records the
mouse contribution before zoom, in degrees. Reuse F7/F8 phases to compare pad,
physical mouse and simultaneous gyro, then check menus/focus and glyph behavior
visually. Input paths that poll independently of window-message delivery are not
proven suppressed by this experiment. Other software can inject matching input;
the signature plus pad contact is an experimental restriction, not Steam identity.

The first hardware observation on 5 October 2026 contained 2,992 device-less raw
mouse packets and injected window movements in the touchpad phase, versus 10,436
raw packets from one named HID mouse and hardware-origin movements in the physical
mouse phase. No packets of either category appeared in the other's phase. This
supports trying this route on that setup, not enabling it by default for all hosts.

The subsequent routing capture intercepted all 2,953 anonymous raw movements and
2,480 injected window movements in the pad-only phase. Its raw totals of +84 X
and +131 Y counts produced exactly +4.2 yaw and -6.55 pitch degrees before zoom.
All 11,211 raw movements and 7,383 window movements from the physical mouse in
the next phase passed through. The recorded view had gyro disabled: this capture
does not validate simultaneous pad and gyro. Visual glyph behavior and actual
camera response were subsequently confirmed by the tester, who also reported
normal physical-mouse and gyro behavior. However, the Windows cursor escaped to
the second monitor: suppressing host messages alone cannot stop desktop motion.

The experimental route now temporarily confines the cursor to the game client
rectangle on accepted touchpad motion. It releases its restriction for physical
mouse activity, menus (including camera-enabled menus), the library panel,
focus loss, inactivity and overlay shutdown. A 250 ms physical-mouse preference
prevents immediate re-confinement by the touchpad's inertia. The previous cursor
restriction is restored only if it still matches the one set by GyroLib; a tighter
host restriction is preserved. This uses ClipCursor, not an input hook or injected
movement. The desktop cursor may still move inside the game window. Recognition
happens after Windows processes input, so the very first physical-mouse movement
can still be constrained before its message releases the clip.

Ownership, restoration, narrower host rectangles, moved windows and API failures
are tested with a simulated desktop. The tester subsequently confirmed the two-monitor correction behaved as expected,
including the requested takeover and release checks. The new three-choice UI and
its automatic detection still require a fresh hardware session.
