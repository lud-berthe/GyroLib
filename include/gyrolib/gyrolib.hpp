#pragma once
#include "gyrolib.h"
#include "sdl.h"
#include <memory>
#include <stdexcept>
#include <string>

namespace gyrolib {

// C++ errors retain the C result. Creation failures use GL_UNAVAILABLE because
// the C creation functions return only a handle. No exceptions cross the C ABI.
class Error : public std::runtime_error {
public:
    Error(const std::string& operation, int32_t result)
        : std::runtime_error(operation + " failed (" + std::to_string(result) + ")"), result_(result) {}
    int32_t code() const noexcept { return result_; }
private:
    int32_t result_;
};

namespace detail {
inline void check(int32_t result, const char* operation) {
    if (result != GL_OK) throw Error(operation, result);
}
inline void report_view(gl_context* context, uint32_t active_view) {
    if (active_view) check(gl_set_gameplay_context_state(context, active_view, 1, 1), "Report view");
}
inline void check_frame(int32_t result, const char* operation, gl_context* context, uint32_t active_view) {
    if (result == GL_OK) return;
    // Failed updates do not consume C view observations. Withdraw this frame's
    // report so retrying with view 0 cannot activate a failed frame's view.
    if (active_view) gl_set_gameplay_context_state(context, active_view, 0, 0);
    throw Error(operation, result);
}
} // namespace detail

// Owns only the core. Merely including this header or using Context does not
// require linking the SDL adapter. All operations stay on the owner thread.
class Context {
    struct Deleter { void operator()(gl_context* value) const noexcept { gl_destroy(value); } };
    std::unique_ptr<gl_context, Deleter> context_;
public:
    Context() : context_(gl_create(GL_ABI_VERSION)) {
        if (!context_) throw Error("Create GyroLib context", GL_UNAVAILABLE);
    }
    Context(Context&&) noexcept = default;
    Context& operator=(Context&&) noexcept = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Borrowed handle for all advanced C APIs; never gl_destroy this pointer.
    gl_context* get() const noexcept { return context_.get(); }
    explicit operator bool() const noexcept { return bool(context_); }
    // Destroy readers/panels and stop callbacks before reset or destruction.
    void reset() noexcept { context_.reset(); }

    void register_view(uint32_t id, const char* label, uint32_t target = GL_OUTPUT_CAMERA,
                       int32_t priority = 0, const char* description = "") {
        if (target > GL_OUTPUT_CURSOR) throw Error("Register view target", GL_INVALID);
        const gl_gameplay_context view{id, label, description, priority};
        detail::check(gl_register_gameplay_context(get(), &view), "Register view");
        detail::check(gl_set_gameplay_context_output_target(get(), id, target), "Set view target");
    }

    // Register views and host defaults first. A failure disables automatic
    // saving in the core; fix the file/path and retry rather than bypassing it.
    void initialize_settings(const char* directory = nullptr, const char* legacy_path = nullptr) {
        detail::check(gl_initialize_settings(get(), directory, legacy_path), "Initialize settings");
    }

    // Convenience for one resolved active view per frame. Pass 0 when none is
    // known. Do not also report other view states for this frame. For overlapping
    // observations/priorities use gl_set_gameplay_context_state + gl_update.
    // Deltas are degrees, already integrated. An installed C camera callback
    // receives them during this call: consume either callback OR returned deltas.
    gl_output update(uint64_t now_ns, const gl_host_state& host, uint32_t active_view) {
        detail::report_view(get(), active_view);
        gl_output output{};
        detail::check_frame(gl_update(get(), now_ns, &host, &output), "Update GyroLib", get(), active_view);
        return output;
    }
};

// Declare after Context, so the reader is destroyed first. Context (including a
// moved-to owner) must outlive this reader. SDL operations require its main thread.
// The wrapper never owns the host's window, event loop or Steam service.
class SdlInput {
    struct Deleter { void operator()(gl_sdl* value) const noexcept { gl_sdl_destroy(value); } };
    gl_context* context_;
    std::unique_ptr<gl_sdl, Deleter> reader_;
public:
    explicit SdlInput(Context& context, bool borrowed_subsystem = false)
        : context_(context.get()), reader_(gl_sdl_create(context_, borrowed_subsystem ? 1u : 0u)) {
        if (!reader_) throw Error(std::string("Create SDL reader: ") + gl_sdl_error(nullptr), GL_UNAVAILABLE);
    }
    SdlInput(const SdlInput&) = delete;
    SdlInput& operator=(const SdlInput&) = delete;
    SdlInput(SdlInput&&) = delete;
    SdlInput& operator=(SdlInput&&) = delete;

    gl_sdl* get() const noexcept { return reader_.get(); }
    // Borrowed diagnostic text, valid until the next reader call. Nonempty text
    // may describe degraded asynchronous acquisition even when poll succeeds.
    const char* error() const noexcept { return gl_sdl_error(get()); }

    // Pass false when the host already pumps this same SDL instance (PollEvent
    // also pumps). Sensor data is acquired here; no camera movement is applied.
    void poll(uint64_t now_ns, bool pump_events = true) {
        if (pump_events) detail::check(gl_sdl_pump_events(get()), "Pump SDL events");
        detail::check(gl_sdl_poll(get(), now_ns), "Poll SDL input");
    }

    // Short path for a host without its own SDL loop: pump, poll and update.
    // The caller still supplies real host state and a resolved view each frame.
    gl_output update(uint64_t now_ns, const gl_host_state& host, uint32_t active_view) {
        detail::report_view(context_, active_view);
        gl_output output{};
        detail::check_frame(gl_sdl_update(get(), now_ns, &host, &output), "Update SDL input and GyroLib", context_, active_view);
        return output;
    }
};

} // namespace gyrolib
