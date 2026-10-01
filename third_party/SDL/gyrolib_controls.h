/* GyroLib SDL extension, MIT (GyroLib root LICENSE). This is altered SDL source.
 * Descriptive provider properties only: no packet parsing or hardware writes.
 * Drivers declare physical origins; consumers need no VID/PID capability rules.
 * All properties are private extensions, not claimed upstream SDL API. */
#ifndef GYROLIB_SDL_CONTROLS_H
#define GYROLIB_SDL_CONTROLS_H
static void GyroLibControlTopology(SDL_Joystick *joystick, int sticks)
{
    SDL_PropertiesID p = SDL_GetJoystickProperties(joystick);
    SDL_SetNumberProperty(p, "gyrolib.controls.authority", 31);
    SDL_SetNumberProperty(p, "gyrolib.controls.sticks", sticks);
    SDL_SetNumberProperty(p, "gyrolib.controls.left.x_axis", SDL_GAMEPAD_AXIS_LEFTX);
    SDL_SetNumberProperty(p, "gyrolib.controls.left.y_axis", SDL_GAMEPAD_AXIS_LEFTY);
    SDL_SetNumberProperty(p, "gyrolib.controls.right.x_axis", SDL_GAMEPAD_AXIS_RIGHTX);
    SDL_SetNumberProperty(p, "gyrolib.controls.right.y_axis", SDL_GAMEPAD_AXIS_RIGHTY);
}
static void GyroLibButtonOrigin(SDL_Joystick *joystick, int raw_button, const char *name, int stick_touch, int grip_touch)
{
    SDL_PropertiesID p = SDL_GetJoystickProperties(joystick);
    char key[96];
    SDL_snprintf(key, sizeof(key), "gyrolib.controls.button.%d.name", raw_button);
    SDL_SetStringProperty(p, key, name);
    if (stick_touch) {
        SDL_snprintf(key, sizeof(key), "gyrolib.controls.button.%d.stick_touch", raw_button);
        SDL_SetNumberProperty(p, key, stick_touch);
    }
    if (grip_touch) {
        SDL_snprintf(key, sizeof(key), "gyrolib.controls.button.%d.grip_touch", raw_button);
        SDL_SetNumberProperty(p, key, grip_touch);
    }
}
#endif
