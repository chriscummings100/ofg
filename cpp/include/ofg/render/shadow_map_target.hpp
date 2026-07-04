// Renderer-owned depth texture array for cascaded sun shadows.
//
// ShadowMapTarget owns one depth texture with one array layer per cascade, a
// sampling view for future opaque shading, per-cascade render views for the
// shadow caster pass, and one comparison sampler kept stable across texture
// resize events.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/render/renderer_counters.hpp"
#include "ofg/render/shadow_settings.hpp"

#include <array>
#include <cstdint>

#include <webgpu/webgpu.h>

namespace ofg {

class ShadowMapTarget {
public:
    ShadowMapTarget() = default;
    explicit ShadowMapTarget(GpuContext gpu);
    ShadowMapTarget(const ShadowMapTarget&) = delete;
    ShadowMapTarget& operator=(const ShadowMapTarget&) = delete;
    ShadowMapTarget(ShadowMapTarget&& other) noexcept;
    ShadowMapTarget& operator=(ShadowMapTarget&& other) noexcept;
    ~ShadowMapTarget();

    // Returns the fixed cascade layer count for the first shadow implementation.
    [[nodiscard]] static constexpr std::uint32_t cascade_count() noexcept {
        return static_cast<std::uint32_t>(shadow_cascade_count());
    }

    // Returns the depth format used by shadow maps.
    [[nodiscard]] static constexpr WGPUTextureFormat format() noexcept {
        return WGPUTextureFormat_Depth32Float;
    }

    // Resizes the texture array, or releases texture/views for a zero size.
    void resize(std::uint32_t size);
    // Releases all owned WebGPU resources.
    void release() noexcept;
    // Returns the array view used by future opaque shadow sampling.
    [[nodiscard]] WGPUTextureView sampling_view() const noexcept;
    // Returns the render view for one cascade layer.
    [[nodiscard]] WGPUTextureView render_view(std::uint32_t cascade_index) const;
    // Returns the comparison sampler used by future opaque shadow sampling.
    [[nodiscard]] WGPUSampler sampler() const noexcept;
    // Returns the square map size in texels.
    [[nodiscard]] std::uint32_t size() const noexcept;
    // Returns a token incremented whenever texture views change.
    [[nodiscard]] std::uint64_t view_generation() const noexcept;
    // Returns the estimated depth bytes for the current texture allocation.
    [[nodiscard]] std::uint64_t estimated_depth_bytes() const noexcept;
    // Reports durable texture/view creation counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    // Ensures the comparison sampler exists after construction or release.
    void ensure_sampler();
    // Releases texture and texture views while keeping the comparison sampler.
    [[nodiscard]] bool release_texture_views() noexcept;

    GpuContext m_gpu;
    WGPUTexture m_texture{nullptr};
    WGPUTextureView m_sampling_view{nullptr};
    std::array<WGPUTextureView, shadow_cascade_count()> m_render_views{};
    WGPUSampler m_sampler{nullptr};
    std::uint32_t m_size{0};
    std::uint64_t m_view_generation{0};
    RendererCounters m_counters;
};

} // namespace ofg
