// Renderer-owned HDR scene color target.
//
// SceneColorTarget owns the half-precision linear texture that opaque and sky
// passes render into before the final tone-map pass writes to the platform
// target. It is resized only when the frame dimensions change and releases GPU
// resources on zero-size resize or destruction.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/renderer_counters.hpp"

#include <cstdint>

#include <webgpu/webgpu.h>

namespace ofg {

class SceneColorTarget {
public:
    SceneColorTarget() = default;
    explicit SceneColorTarget(GpuContext gpu);
    SceneColorTarget(const SceneColorTarget&) = delete;
    SceneColorTarget& operator=(const SceneColorTarget&) = delete;
    SceneColorTarget(SceneColorTarget&& other) noexcept;
    SceneColorTarget& operator=(SceneColorTarget&& other) noexcept;
    ~SceneColorTarget();

    // Returns the HDR scene-color format used by the first sky milestone.
    [[nodiscard]] static constexpr WGPUTextureFormat format() noexcept {
        return WGPUTextureFormat_RGBA16Float;
    }

    // Resizes the owned texture/view or releases them for a zero-size target.
    void resize(std::uint32_t width, std::uint32_t height);
    // Releases the owned texture/view while preserving the borrowed GPU context.
    void release() noexcept;
    // Returns this target as a render attachment descriptor.
    [[nodiscard]] RenderTarget render_target() const;
    // Returns the current texture view, or null before a nonzero resize.
    [[nodiscard]] WGPUTextureView view() const noexcept;
    // Returns the current width in pixels.
    [[nodiscard]] std::uint32_t width() const noexcept;
    // Returns the current height in pixels.
    [[nodiscard]] std::uint32_t height() const noexcept;
    // Returns a token incremented whenever the texture view changes.
    [[nodiscard]] std::uint64_t view_generation() const noexcept;
    // Reports durable texture/view creation counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    GpuContext m_gpu;
    WGPUTexture m_texture{nullptr};
    WGPUTextureView m_view{nullptr};
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
    std::uint64_t m_view_generation{0};
    RendererCounters m_counters;
};

} // namespace ofg
