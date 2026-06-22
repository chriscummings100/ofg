// WebGPU renderer for the deterministic bootstrap triangle.
//
// This renderer owns the durable shader pipeline and vertex buffer used by both
// browser C++/WASM smoke and native Dawn smoke. Callers provide a device/queue
// and a render-target format; the renderer encodes a single clear+triangle pass
// into the command encoder supplied by the caller.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

struct RendererCounters {
  std::uint32_t pipeline_create_count{0};
  std::uint32_t buffer_create_count{0};
};

// Owns bootstrap GPU resources and encodes the deterministic triangle pass.
class BootstrapRenderer {
public:
  BootstrapRenderer(const BootstrapRenderer&) = delete;
  BootstrapRenderer& operator=(const BootstrapRenderer&) = delete;
  BootstrapRenderer(BootstrapRenderer&&) = delete;
  BootstrapRenderer& operator=(BootstrapRenderer&&) = delete;
  ~BootstrapRenderer();

  // Creates shader, pipeline, and vertex buffer resources once.
  [[nodiscard]] static std::unique_ptr<BootstrapRenderer> create(
    WGPUDevice device,
    WGPUQueue queue,
    WGPUTextureFormat format,
    std::string& error
  );

  // Encodes a render pass that clears `view` and draws the bootstrap triangle.
  [[nodiscard]] bool render_to_view(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    std::string& error
  ) const;
  // Reports durable resource creation counts for smoke/performance checks.
  [[nodiscard]] RendererCounters counters() const noexcept;

private:
  // Stores already-created WebGPU handles; call create() for validation.
  BootstrapRenderer(WGPURenderPipeline pipeline, WGPUBuffer vertex_buffer);

  WGPURenderPipeline pipeline_{nullptr};
  WGPUBuffer vertex_buffer_{nullptr};
  RendererCounters counters_{};
};

} // namespace ofg
