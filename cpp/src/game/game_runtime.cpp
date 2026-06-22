// Shared lifecycle, frame, and debug-status state for OFG Game.
#include "ofg/game/game_runtime.hpp"

#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace ofg {

// Creates runtime state with messages tailored to the owning facade.
GameRuntime::GameRuntime(
  std::string disposed_message,
  std::string gpu_not_ready_message
)
  : disposed_message_(std::move(disposed_message)),
    gpu_not_ready_message_(std::move(gpu_not_ready_message)) {
}

// Returns the current debug snapshot without serializing it.
const RuntimeDebugStatus& GameRuntime::status() const noexcept {
  return status_;
}

// Serializes the current debug snapshot for the TypeScript host.
std::string GameRuntime::debug_status_json() const {
  return status_.to_json();
}

// Reports whether dispose() has made the runtime inert.
bool GameRuntime::disposed() const noexcept {
  return disposed_;
}

// Accepts a new physical target size and device pixel ratio.
bool GameRuntime::resize(
  std::uint32_t width,
  std::uint32_t height,
  double device_pixel_ratio,
  std::string& error
) {
  if (disposed_) {
    error = disposed_message_;
    return fail(disposed_message_);
  }
  if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0.0) {
    std::ostringstream out;
    out << "Device pixel ratio must be a positive finite number, got "
        << device_pixel_ratio << ".";
    error = out.str();
    return fail(error);
  }

  // Zero-sized targets are valid but cannot keep a configured surface alive.
  const bool dimensions_changed =
    status_.canvas_width != width || status_.canvas_height != height;
  status_.canvas_width = width;
  status_.canvas_height = height;
  status_.device_pixel_ratio = device_pixel_ratio;
  if (dimensions_changed || width == 0 || height == 0) {
    surface_configured_ = false;
  }
  status_.initialized =
    gpu_ready_ && surface_configured_ && width > 0 && height > 0;
  status_.last_error.reset();
  error.clear();
  return true;
}

// Advances frame state after validating the frame timestamp.
bool GameRuntime::tick(double time_ms, std::string& error) {
  if (disposed_) {
    error = disposed_message_;
    return fail(disposed_message_);
  }
  if (!std::isfinite(time_ms)) {
    std::ostringstream out;
    out << "Frame time must be finite, got " << time_ms << ".";
    error = out.str();
    return fail(error);
  }

  frame_state_.tick(time_ms);
  status_.frame_count = frame_state_.frame_count();
  status_.last_error.reset();
  error.clear();
  return true;
}

// Marks the shared GPU renderer path as ready.
bool GameRuntime::mark_gpu_ready(
  std::string adapter_name,
  std::string backend,
  std::string surface_format,
  std::string& error
) {
  if (disposed_) {
    error = disposed_message_;
    return fail(disposed_message_);
  }

  gpu_ready_ = true;
  status_.adapter_name = std::move(adapter_name);
  status_.backend = std::move(backend);
  status_.surface_format = std::move(surface_format);
  status_.initialized =
    surface_configured_ && status_.canvas_width > 0 && status_.canvas_height > 0;
  status_.last_error.reset();
  error.clear();
  return true;
}

// Records durable renderer resource counts for smoke/performance checks.
bool GameRuntime::mark_renderer_counters(
  std::uint32_t pipeline_create_count,
  std::uint32_t buffer_create_count,
  std::string& error
) {
  if (disposed_) {
    error = disposed_message_;
    return fail(disposed_message_);
  }

  status_.pipeline_create_count = pipeline_create_count;
  status_.buffer_create_count = buffer_create_count;
  status_.last_error.reset();
  error.clear();
  return true;
}

// Marks the platform target/surface as configured for the current nonzero size.
bool GameRuntime::mark_surface_configured(std::string& error) {
  if (disposed_) {
    error = disposed_message_;
    return fail(disposed_message_);
  }
  if (!gpu_ready_) {
    error = gpu_not_ready_message_;
    return fail(gpu_not_ready_message_);
  }
  if (status_.canvas_width == 0 || status_.canvas_height == 0) {
    surface_configured_ = false;
    status_.initialized = false;
    status_.last_error.reset();
    error.clear();
    return true;
  }

  if (!surface_configured_) {
    status_.surface_configure_count += 1;
  }
  surface_configured_ = true;
  status_.initialized = true;
  status_.last_error.reset();
  error.clear();
  return true;
}

// Records a recoverable runtime/render error while preserving ready resources.
bool GameRuntime::mark_error(std::string message) {
  if (disposed_) {
    return fail(disposed_message_);
  }

  return fail(std::move(message));
}

// Records a GPU/device setup error and requires platform reinitialization.
bool GameRuntime::mark_gpu_error(std::string message) {
  if (disposed_) {
    return fail(disposed_message_);
  }

  gpu_ready_ = false;
  surface_configured_ = false;
  return fail(std::move(message));
}

// Makes the runtime inert while preserving useful diagnostic frame count.
void GameRuntime::dispose() {
  disposed_ = true;
  const std::uint64_t frame_count = status_.frame_count;
  status_ = RuntimeDebugStatus::uninitialized(disposed_message_);
  status_.frame_count = frame_count;
  gpu_ready_ = false;
  surface_configured_ = false;
}

// Stores a recoverable failure reason and returns false for callers.
bool GameRuntime::fail(std::string message) {
  status_.initialized = false;
  status_.last_error = std::move(message);
  return false;
}

} // namespace ofg
