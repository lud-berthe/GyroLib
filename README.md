# GyroLib

GyroLib turns controller motion into camera or local UI movement. It handles
sensor acquisition, calibration, gyro spaces, sensitivity, smoothing and flick
stick. Your game supplies its real view, focus, pause and menu states, then
consumes angular movement in **degrees**. No engine hooks or game actions are
created by the library.

The core has a C ABI and a small C++ RAII interface. SDL acquisition, a borrowed
Steam Input adapter and an ImGui settings panel are optional. Windows x64 is the
tested platform; [validation and hardware limits](docs/VALIDATION.md) distinguish
automated checks from controller acceptance.

## Build and install

The Windows build requires CMake 3.24+, Visual Studio C++ tools with C++20
support, and **PowerShell 7** (`pwsh` on PATH). Windows PowerShell 5.1 does not
replace `pwsh`. Dependencies are vendored; configuration performs no download.

```powershell
pwsh -NoProfile -File ./tools/build.ps1
cmake --install build --config Release --prefix dist/sdk
./dist/sdk/bin/gyrolib_demo.exe
```

The default Windows SDK bundles acquisition in `gyrolib.dll`. A player's mod
ships its own DLL, `gyrolib.dll` and the notices; the SDK and demo are development
files. [Build variants and dependencies](docs/BUILDING.md) ·
[Distribution](docs/DISTRIBUTION.md) · [Demo controls](docs/TPS_DEMO.md)

## First integration

Save this complete example as `main.cpp`. It opens a window and shows accumulated
camera angles in its title. Close the window to exit. It has one camera view and
no pause or menus, so those two states are explicitly false. A real game replaces
these observations and the two angle additions with its own state and camera
integration. GyroLib never guesses them.

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
        gyro.initialize_settings();    // Loads/creates girolib.ini beside the module.
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

Use this `CMakeLists.txt` with the installed SDK:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyGyroHost LANGUAGES CXX)
find_package(GyroLib 0.2 CONFIG REQUIRED COMPONENTS SDL)
add_executable(my_gyro_host main.cpp)
target_link_libraries(my_gyro_host PRIVATE GyroLib::gyrolib_sdl)
```

Configure with `-DCMAKE_PREFIX_PATH=<absolute-sdk-directory>`, build, then place
the installed `gyrolib.dll` beside the executable. The default Windows SDK includes
the matching SDL headers, import library and CMake package; no source checkout or
separate `SDL3_DIR` is needed. Its SDL component loads the bundled runtime.
See [BUILDING.md](docs/BUILDING.md) for other variants and prerequisites.

Keep all calls on one owner thread, also SDL's main thread. Declare readers
**after** their context. Without a controller, acquisition succeeds and returns
zero motion; asynchronous reader diagnostics remain available through `error()`.
Without your own SDL loop, `input.update(now, host, active_view_id)` combines
pumping, polling and processing. Pass view ID `0` when no view is known active.

This example consumes returned movement. If you instead install a camera callback,
that callback receives it during update: **do not apply the returned angles again**.

## Go further

- [API contracts, C integration, errors and C++ 0.2 migration](docs/API.md)
- [Named views, priorities and settings migration](docs/CONTEXTS.md)
- [Input ownership, Steam and physical controller identity](docs/INPUT.md)
- [Gyro spaces and axes](docs/SPACES.md) · [Advanced motion](docs/ADVANCED_MOTION.md)
- [Native settings menus and optional ImGui panel](docs/MENUS.md)
- [Architecture](docs/ARCHITECTURE.md) · [Third-party notices](docs/THIRD_PARTY.md)
