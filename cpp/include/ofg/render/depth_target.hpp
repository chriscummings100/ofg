// Renderer-owned depth target shared by scene-space passes.
//
// DepthTarget owns the depth texture formerly hidden inside OpaquePass. Keeping
// it at renderer scope lets opaque, sky, and later distance-fade passes share a
// single depth attachment for a frame.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/render/renderer_counters.hpp"

#include <cstdint>

#include <webgpu/webgpu.h>

namespace ofg {

class DepthTarget {
public:
    DepthTarget() = default;
    explicit DepthTarget(GpuContext gpu);
    DepthTarget(const DepthTarget&) = delete;
    DepthTarget& operator=(const DepthTarget&) = delete;
    DepthTarget(DepthTarget&& other) noexcept;
    DepthTarget& operator=(DepthTarget&& other) noexcept;
    ~DepthTarget();

    // Returns the depth format used by opaque rendering and sky depth tests.
    [[nodiscard]] static constexpr WGPUTextureFormat format() noexcept {
        return WGPUTextureFormat_Depth24Plus;
    }

    // Resizes the owned texture/view or releases them for a zero-size target.
    void resize(std::uint32_t width, std::uint32_t height);
    // Releases the owned texture/view while preserving the borrowed GPU context.
    void release() noexcept;
    // Returns the current depth view, or null before a nonzero resize.
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
