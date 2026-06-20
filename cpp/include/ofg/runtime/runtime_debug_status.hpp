// Public debug-status snapshot for the C++ browser runtime.
//
// The TypeScript host, browser smoke, and native tests all read this shape
// through JSON. Keep field names and semantics aligned with
// src/app/wasmRuntime.ts while the implementation evolves in C++.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ofg {

struct RuntimeDebugStatus {
  bool initialized{false};
  std::uint64_t frame_count{0};
  std::uint32_t canvas_width{0};
  std::uint32_t canvas_height{0};
  double device_pixel_ratio{1.0};
  std::string surface_format{"Unavailable"};
  std::string adapter_name{"Unavailable"};
  std::string backend{"CppWasm"};
  std::uint32_t pipeline_create_count{0};
  std::uint32_t buffer_create_count{0};
  std::uint32_t surface_configure_count{0};
  std::optional<std::string> last_error;

  // Serializes the status using the browser-facing debug-status field names.
  [[nodiscard]] std::string to_json() const;

  // Creates a non-initialized status with a human-readable failure reason.
  [[nodiscard]] static RuntimeDebugStatus uninitialized(std::string message);
};

} // namespace ofg
