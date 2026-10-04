# First integration

[Documentation](INDEX.md) / Integration

This example opens an SDL window and displays accumulated camera angles in its
title. It uses one camera view, in-memory defaults and no settings panel. A mod
replaces the angle additions and state observations with game hooks.

## Prepare the project

Use an installed Windows x64 SDK, CMake 3.24+ and Visual Studio C++20 tools.
If you only have the source checkout, [build and install it first](BUILDING.md#build-variants).
Create an empty project directory and save the following as `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyGyroHost LANGUAGES CXX)
find_package(GyroLib 1.0 CONFIG REQUIRED COMPONENTS SDL)
add_executable(my_gyro_host main.cpp)
target_link_libraries(my_gyro_host PRIVATE GyroLib::gyrolib_sdl)
```

Save the source in the next section as `main.cpp`. From that project directory,
configure and build, replacing the example SDK path with your own:

```powershell
$gyroSdk = (Resolve-Path 'C:/path/to/installed/sdk').Path
cmake -S . -B build -A x64 "-DCMAKE_PREFIX_PATH=$gyroSdk"
cmake --build build --config Release
Copy-Item "$gyroSdk/bin/gyrolib.dll" ./build/Release/
./build/Release/my_gyro_host.exe
```

The default bundled SDK includes the matching SDL headers and loader. It does
not require a separate SDL installation. Connect a controller and move it while
the window has focus; close the window to exit.

## Source

```cpp
#include <gyrolib/gyrolib.hpp>
#include <SDL3/SDL.h>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

int main() {
    SDL_Window* window = nullptr;
    bool sdl_ready = false;
    int result = 0;
    try {
        gyrolib::Context gyro;
        gyro.register_view(1, "Camera"); // Stable ID, also used for saved settings.
        // This minimal host has no settings panel and uses in-memory defaults.
        if (gl_set_gamepad_menu_shortcut(gyro.get(), 0) != GL_OK)
            throw std::runtime_error("Cannot disable the unused panel shortcut");
        gyrolib::SdlInput input(gyro);   // Destroyed before gyro, including on errors.
        sdl_ready = true;
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("GyroLib", 640, 360, 0);
        if (!window) throw std::runtime_error(SDL_GetError());

        bool running = true;
        double camera_yaw = 0, camera_pitch = 0;
        std::string last_warning;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
            }
            if (!running) break;
            const uint64_t now = SDL_GetTicksNS();
            input.poll(now, false); // Our SDL_PollEvent loop already pumps events.
            if (last_warning != input.error()) {
                last_warning = input.error();
                if (!last_warning.empty()) std::fprintf(stderr, "%s\n", last_warning.c_str());
            }

            gl_host_state host{};
            host.focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
            host.camera_allowed = 1; // This sample owns a controllable camera.
            host.menu_open = host.paused = 0; // Read real game states in a mod.
            const gl_output movement = gyro.update(now, host, 1);
            camera_yaw += movement.yaw_degrees;
            camera_pitch += movement.pitch_degrees; // Already integrated; no dt multiplier.
            char title[128];
            std::snprintf(title, sizeof(title), "Yaw %.2f / Pitch %.2f degrees", camera_yaw, camera_pitch);
            SDL_SetWindowTitle(window, title);
            SDL_Delay(8);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        result = 1;
    }
    if (window) SDL_DestroyWindow(window);
    if (sdl_ready) SDL_Quit();
    return result;
}
```

## Connect it to a game

Keep calls on one owner thread, also SDL's main thread. Declare readers after
their context so they are destroyed first. With no controller, acquisition can
succeed with zero movement; `input.error()` reports asynchronous reader failures.

`gyro.update(now, host, view_id)` reports one active view and returns integrated
degrees. Pass 0 when no view is known active. Apply the result once, without a
second time multiplier. If you use a camera callback instead, do not also apply
the returned movement.

The sample pumps SDL events itself. A host without its own SDL event loop can
use `input.update(now, host, view_id)` to combine pumping, polling and processing.

For a complete mod, [register the game's views](CONTEXTS.md),
[initialize persistent settings](SETTINGS.md#initialize-once) and
[connect a settings frontend](MENUS.md#choose-a-frontend). The shortcut is disabled
here because this sample does not draw that frontend.

---

Next: [Host integration](API.md)
