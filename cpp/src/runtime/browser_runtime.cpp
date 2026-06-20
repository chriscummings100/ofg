// Portable C++ runtime state for lifecycle, validation, and debug status.
#include "ofg/runtime/browser_runtime.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace ofg {
namespace {

constexpr const char* kDisposedMessage = "Browser game runtime has been disposed.";

// Formats numeric validation failures consistently for runtime status JSON.
std::string number_message(const char* label, double value) {
  std::ostringstream out;
  out << label << " must be a non-negative integer within uint32 range, got " << value << ".";
  return out.str();
}

// Converts JavaScript numeric dimensions into the uint32 WebGPU size domain.
std::optional<std::uint32_t> parse_dimension(
  const char* label,
  double value,
  std::string& error
) {
  if (!std::isfinite(value) || value < 0.0 || std::trunc(value) != value) {
    error = number_message(label, value);
    return std::nullopt;
  }
  constexpr double max_dimension =
    static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  if (value > max_dimension) {
    error = number_message(label, value);
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

} // namespace

// Returns the current debug snapshot without serializing it.
const RuntimeDebugStatus& BrowserRuntime::status() const noexcept {
  return status_;
}

// Serializes the current debug snapshot for the TypeScript host.
std::string BrowserRuntime::debug_status_json() const {
  return status_.to_json();
}

// Reports whether dispose() has made the runtime inert.
bool BrowserRuntime::disposed() const noexcept {
  return disposed_;
}

// Accepts a new physical canvas size and device pixel ratio from TypeScript.
bool BrowserRuntime::resize(double width, double height, double device_pixel_ratio) {
  if (disposed_) {
    return fail(kDisposedMessage);
  }

  std::string error;
  const std::optional<std::uint32_t> parsed_width = parse_dimension("Canvas width", width, error);
  if (!parsed_width.has_value()) {
    return fail(error);
  }
  const std::optional<std::uint32_t> parsed_height =
    parse_dimension("Canvas height", height, error);
  if (!parsed_height.has_value()) {
    return fail(error);
  }
  if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0.0) {
    std::ostringstream out;
    out << "Device pixel ratio must be a positive finite number, got "
        << device_pixel_ratio << ".";
    return fail(out.str());
  }

  // Zero-sized canvases are valid but cannot keep a configured surface alive.
  const bool dimensions_changed =
    status_.canvas_width != *parsed_width || status_.canvas_height != *parsed_height;
  status_.canvas_width = *parsed_width;
  status_.canvas_height = *parsed_height;
  status_.device_pixel_ratio = device_pixel_ratio;
  if (dimensions_changed || status_.canvas_width == 0 || status_.canvas_height == 0) {
    surface_configured_ = false;
  }
  status_.initialized =
    webgpu_ready_ && surface_configured_ && status_.canvas_width > 0 && status_.canvas_height > 0;
  status_.last_error.reset();
  return true;
}

// Advances frame state after validating the timestamp from requestAnimationFrame.
bool BrowserRuntime::frame(double time_ms) {
  if (disposed_) {
    return fail(kDisposedMessage);
  }
  if (!std::isfinite(time_ms)) {
    std::ostringstream out;
    out << "Frame time must be finite, got " << time_ms << ".";
    return fail(out.str());
  }

  frame_state_.tick(time_ms);
  status_.frame_count = frame_state_.frame_count();
  status_.last_error.reset();
  return true;
}

// Marks WebGPU adapter/device/format discovery as ready.
bool BrowserRuntime::mark_webgpu_ready(
  std::string adapter_name,
  std::string backend,
  std::string surface_format
) {
  if (disposed_) {
    return fail(kDisposedMessage);
  }

  webgpu_ready_ = true;
  status_.adapter_name = std::move(adapter_name);
  status_.backend = std::move(backend);
  status_.surface_format = std::move(surface_format);
  status_.initialized =
    surface_configured_ && status_.canvas_width > 0 && status_.canvas_height > 0;
  status_.last_error.reset();
  return true;
}

// Records durable renderer resource counts for smoke/performance checks.
bool BrowserRuntime::mark_renderer_counters(
  std::uint32_t pipeline_create_count,
  std::uint32_t buffer_create_count
) {
  if (disposed_) {
    return fail(kDisposedMessage);
  }

  status_.pipeline_create_count = pipeline_create_count;
  status_.buffer_create_count = buffer_create_count;
  status_.last_error.reset();
  return true;
}

// Marks the browser surface as configured for the current nonzero size.
bool BrowserRuntime::mark_surface_configured() {
  if (disposed_) {
    return fail(kDisposedMessage);
  }
  if (!webgpu_ready_) {
    return fail("Browser WebGPU device is not ready.");
  }
  if (status_.canvas_width == 0 || status_.canvas_height == 0) {
    surface_configured_ = false;
    status_.initialized = false;
    status_.last_error.reset();
    return true;
  }

  surface_configured_ = true;
  status_.surface_configure_count += 1;
  status_.initialized = true;
  status_.last_error.reset();
  return true;
}

// Records a WebGPU setup/render error and clears initialized state.
bool BrowserRuntime::mark_webgpu_error(std::string message) {
  if (disposed_) {
    return fail(kDisposedMessage);
  }

  webgpu_ready_ = false;
  surface_configured_ = false;
  return fail(std::move(message));
}

// Makes the runtime inert while preserving useful diagnostic frame count.
void BrowserRuntime::dispose() {
  disposed_ = true;
  const std::uint64_t frame_count = status_.frame_count;
  status_ = RuntimeDebugStatus::uninitialized(kDisposedMessage);
  status_.frame_count = frame_count;
  webgpu_ready_ = false;
  surface_configured_ = false;
}

// Stores a recoverable failure reason and returns false for callers.
bool BrowserRuntime::fail(std::string message) {
  status_.initialized = false;
  status_.last_error = std::move(message);
  return false;
}

} // namespace ofg
