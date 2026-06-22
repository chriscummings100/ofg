// Per-frame render target supplied by browser and native frame drivers.
//
// The target view changes per frame in the browser and is offscreen in native
// smoke. Shared game code validates the target before recording commands, but
// platform code owns acquisition, presentation, readback, finish, and submit.
#pragma once

#include <cstdint>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

struct RenderTarget {
  WGPUTextureView view{nullptr};
  WGPUTextureFormat format{WGPUTextureFormat_Undefined};
  std::uint32_t width{0};
  std::uint32_t height{0};
};

// Validates a target against the renderer format and latest accepted size.
[[nodiscard]] bool validate_render_target(
  RenderTarget target,
  WGPUTextureFormat expected_format,
  std::uint32_t expected_width,
  std::uint32_t expected_height,
  std::string& error
);

} // namespace ofg
