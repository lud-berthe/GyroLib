#include <gyrolib/gyrolib.hpp>
#include <cstdio>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<gyrolib::Context>);
static_assert(std::is_nothrow_move_constructible_v<gyrolib::Context>);
static_assert(!std::is_copy_constructible_v<gyrolib::SdlInput>);

int main() {
    try {
        gyrolib::Context context;
        context.register_view(1, "Camera");
        context.register_view(2, "Inventory", GL_OUTPUT_CURSOR, 10);
        const auto original = context.get();
        gyrolib::Context moved(std::move(context));
        if (context.get() || moved.get() != original) return 1;

        // Reject invalid routing before adding a view or changing its metadata.
        try { moved.register_view(3, "Invalid", 42); return 2; }
        catch (const gyrolib::Error& error) { if (error.code() != GL_INVALID) return 3; }
        if (gl_gameplay_context_count(moved.get()) != 2) return 4;

        gl_host_state host{};
        host.focused = host.camera_allowed = 1;
        auto output = moved.update(1000000000, host, 1);
        if (gl_get_active_gameplay_context(moved.get()) != 1 || output.yaw_degrees || output.pitch_degrees) return 5;
        moved.update(1000000001, host, 2);
        if (gl_get_active_gameplay_context(moved.get()) != 2 || gl_get_output_target(moved.get()) != GL_OUTPUT_CURSOR) return 6;
        moved.update(1000000002, host, 0);
        if (gl_get_active_gameplay_context(moved.get())) return 7;

        try { moved.update(1000000003, host, 99); return 8; }
        catch (const gyrolib::Error& error) { if (error.code() != GL_INVALID) return 9; }
        try { moved.update(0, host, 1); return 10; }
        catch (const gyrolib::Error& error) { if (error.code() != GL_INVALID) return 11; }
        moved.update(1000000004, host, 0);
        if (gl_get_active_gameplay_context(moved.get())) return 16;
        moved.reset();
        if (moved.get()) return 12;
        try { moved.register_view(4, "Destroyed"); return 13; }
        catch (const gyrolib::Error& error) { if (error.code() != GL_INVALID) return 14; }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 15;
    }
    return 0;
}
