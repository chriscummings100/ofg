// Shared lifecycle, frame, and debug-status state for OFG Game.
//
// GameRuntime contains platform-neutral resize validation after platform
// parsing, frame counting, renderer counters, readiness flags, errors,
// disposal, and status JSON.
#pragma once

#include "ofg/core/frame_state.hpp"
#include "ofg/runtime/runtime_debug_status.hpp"

#include <cstdint>
#include <string>

namespace ofg {

class GameRuntime {
public:
  // Creates runtime state with messages tailored to the owning facade.
  explicit GameRuntime(
    std::string disposed_message = "Game runtime has been disposed.",
    std::string gpu_not_ready_message = "Game GPU device is not ready."
  );

  // Returns the current debug snapshot without serializing it.
  [[nodiscard]] const RuntimeDebugStatus& status() const noexcept;
  // Serializes the current debug snapshot for the TypeScript host.
  [[nodiscard]] std::string debug_status_json() const;
  // Reports whether dispose() has made the runtime inert.
  [[nodiscard]] bool disposed() const noexcept;

  // Accepts a new physical target size and device pixel ratio.
  bool resize(
    std::uint32_t width,
    std::uint32_t height,
    double device_pixel_ratio,
    std::string& error
  );
  // Advances frame state after validating the frame timestamp.
  bool tick(double time_ms, std::string& error);
  // Marks the shared GPU renderer path as ready.
  bool mark_gpu_ready(
    std::string adapter_name,
    std::string backend,
    std::string surface_format,
    std::string& error
  );
  // Records durable renderer resource counts for smoke/performance checks.
  bool mark_renderer_counters(
    std::uint32_t pipeline_create_count,
    std::uint32_t buffer_create_count,
    std::string& error
  );
  // Marks the platform target/surface as configured for the current nonzero size.
  bool mark_surface_configured(std::string& error);
  // Records a recoverable runtime/render error while preserving ready resources.
  bool mark_error(std::string message);
  // Records a GPU/device setup error and requires platform reinitialization.
  bool mark_gpu_error(std::string message);
  // Makes the runtime inert while preserving useful diagnostic frame count.
  void dispose();

private:
  // Stores a recoverable failure reason and returns false for callers.
  bool fail(std::string message);

  FrameState frame_state_;
  RuntimeDebugStatus status_;
  std::string disposed_message_;
  std::string gpu_not_ready_message_;
  bool disposed_{false};
  bool gpu_ready_{false};
  bool surface_configured_{false};
};

} // namespace ofg
