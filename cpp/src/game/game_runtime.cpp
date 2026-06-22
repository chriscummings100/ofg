// Shared lifecycle, frame, and debug-status state for OFG Game.
#include "ofg/game/game_runtime.hpp"

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace ofg {

// Creates runtime state with messages tailored to the owning facade.
GameRuntime::GameRuntime(std::string disposed_message, std::string gpu_not_ready_message)
    : m_disposed_message(std::move(disposed_message)), m_gpu_not_ready_message(std::move(gpu_not_ready_message)) {}

// Returns the current debug snapshot without serializing it.
const RuntimeDebugStatus& GameRuntime::status() const noexcept {
    return m_status;
}

// Serializes the current debug snapshot for the TypeScript host.
std::string GameRuntime::debug_status_json() const {
    return m_status.to_json();
}

// Reports whether dispose() has made the runtime inert.
bool GameRuntime::disposed() const noexcept {
    return m_disposed;
}

// Accepts a new physical target size and device pixel ratio.
bool GameRuntime::resize(std::uint32_t width, std::uint32_t height, double device_pixel_ratio, std::string& error) {
    if (m_disposed) {
        error = m_disposed_message;
        return fail(m_disposed_message);
    }
    if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0.0) {
        std::ostringstream out;
        out << "Device pixel ratio must be a positive finite number, got " << device_pixel_ratio << ".";
        error = out.str();
        return fail(error);
    }

    // Zero-sized targets are valid but cannot keep a configured surface alive.
    const bool dimensions_changed = m_status.m_canvas_width != width || m_status.m_canvas_height != height;
    m_status.m_canvas_width = width;
    m_status.m_canvas_height = height;
    m_status.m_device_pixel_ratio = device_pixel_ratio;
    if (dimensions_changed || width == 0 || height == 0) {
        m_surface_configured = false;
    }
    m_status.m_initialized = m_gpu_ready && m_surface_configured && width > 0 && height > 0;
    m_status.m_last_error.reset();
    error.clear();
    return true;
}

// Advances frame state after validating the frame timestamp.
bool GameRuntime::tick(double time_ms, std::string& error) {
    if (m_disposed) {
        error = m_disposed_message;
        return fail(m_disposed_message);
    }
    if (!std::isfinite(time_ms)) {
        std::ostringstream out;
        out << "Frame time must be finite, got " << time_ms << ".";
        error = out.str();
        return fail(error);
    }

    m_frame_state.tick(time_ms);
    m_status.m_frame_count = m_frame_state.frame_count();
    m_status.m_last_error.reset();
    error.clear();
    return true;
}

// Marks the shared GPU renderer path as ready.
bool GameRuntime::mark_gpu_ready(
    std::string adapter_name, std::string backend, std::string surface_format, std::string& error) {
    if (m_disposed) {
        error = m_disposed_message;
        return fail(m_disposed_message);
    }

    m_gpu_ready = true;
    m_status.m_adapter_name = std::move(adapter_name);
    m_status.m_backend = std::move(backend);
    m_status.m_surface_format = std::move(surface_format);
    m_status.m_initialized = m_surface_configured && m_status.m_canvas_width > 0 && m_status.m_canvas_height > 0;
    m_status.m_last_error.reset();
    error.clear();
    return true;
}

// Records durable renderer resource counts for smoke/performance checks.
bool GameRuntime::mark_renderer_counters(
    std::uint32_t pipeline_create_count, std::uint32_t buffer_create_count, std::string& error) {
    if (m_disposed) {
        error = m_disposed_message;
        return fail(m_disposed_message);
    }

    m_status.m_pipeline_create_count = pipeline_create_count;
    m_status.m_buffer_create_count = buffer_create_count;
    m_status.m_last_error.reset();
    error.clear();
    return true;
}

// Marks the platform target/surface as configured for the current nonzero size.
bool GameRuntime::mark_surface_configured(std::string& error) {
    if (m_disposed) {
        error = m_disposed_message;
        return fail(m_disposed_message);
    }
    if (!m_gpu_ready) {
        error = m_gpu_not_ready_message;
        return fail(m_gpu_not_ready_message);
    }
    if (m_status.m_canvas_width == 0 || m_status.m_canvas_height == 0) {
        m_surface_configured = false;
        m_status.m_initialized = false;
        m_status.m_last_error.reset();
        error.clear();
        return true;
    }

    if (!m_surface_configured) {
        m_status.m_surface_configure_count += 1;
    }
    m_surface_configured = true;
    m_status.m_initialized = true;
    m_status.m_last_error.reset();
    error.clear();
    return true;
}

// Records a recoverable runtime/render error while preserving ready resources.
bool GameRuntime::mark_error(std::string message) {
    if (m_disposed) {
        return fail(m_disposed_message);
    }

    return fail(std::move(message));
}

// Records a GPU/device setup error and requires platform reinitialization.
bool GameRuntime::mark_gpu_error(std::string message) {
    if (m_disposed) {
        return fail(m_disposed_message);
    }

    m_gpu_ready = false;
    m_surface_configured = false;
    return fail(std::move(message));
}

// Makes the runtime inert while preserving useful diagnostic frame count.
void GameRuntime::dispose() {
    m_disposed = true;
    const std::uint64_t frame_count = m_status.m_frame_count;
    m_status = RuntimeDebugStatus::uninitialized(m_disposed_message);
    m_status.m_frame_count = frame_count;
    m_gpu_ready = false;
    m_surface_configured = false;
}

// Stores a recoverable failure reason and returns false for callers.
bool GameRuntime::fail(std::string message) {
    m_status.m_initialized = false;
    m_status.m_last_error = std::move(message);
    return false;
}

} // namespace ofg
