// Portable C++ runtime state for the browser-facing game facade.
//
// BrowserRuntime owns validation, frame counting, lifecycle state, and the
// debug-status contract without depending on Emscripten or WebGPU. Browser and
// native wrappers can therefore test the behavioral contract through doctest.
#pragma once

#include "ofg/core/frame_state.hpp"
#include "ofg/runtime/runtime_debug_status.hpp"

#include <string>

namespace ofg {

class BrowserRuntime {
public:
  // Returns the current debug snapshot without serializing it.
  [[nodiscard]] const RuntimeDebugStatus& status() const noexcept;
  // Serializes the current debug snapshot for the TypeScript host.
  [[nodiscard]] std::string debug_status_json() const;
  // Reports whether dispose() has made the runtime inert.
  [[nodiscard]] bool disposed() const noexcept;

  // Accepts a new physical canvas size and device pixel ratio from TypeScript.
  bool resize(double width, double height, double device_pixel_ratio);
  // Advances frame state after validating the timestamp from requestAnimationFrame.
  bool frame(double time_ms);
  // Marks WebGPU adapter/device/format discovery as ready.
  bool mark_webgpu_ready(
    std::string adapter_name,
    std::string backend,
    std::string surface_format
  );
  // Records durable renderer resource counts for smoke/performance checks.
  bool mark_renderer_counters(
    std::uint32_t pipeline_create_count,
    std::uint32_t buffer_create_count
  );
  // Marks the browser surface as configured for the current nonzero size.
  bool mark_surface_configured();
  // Records a WebGPU setup/render error and clears initialized state.
  bool mark_webgpu_error(std::string message);
  // Makes the runtime inert while preserving useful diagnostic frame count.
  void dispose();

private:
  // Stores a recoverable failure reason and returns false for callers.
  bool fail(std::string message);

  FrameState frame_state_;
  RuntimeDebugStatus status_;
  bool disposed_{false};
  bool webgpu_ready_{false};
  bool surface_configured_{false};
};

} // namespace ofg
