// Renderer-owned depth texture array for cascaded sun shadows.
#include "ofg/render/shadow_map_target.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace ofg {
namespace {

constexpr std::uint64_t _depth32_float_bytes_per_texel = 4U;

// Creates the depth texture array used by all cascades.
WGPUTexture create_shadow_texture(WGPUDevice device, std::uint32_t size) {
    if (device == nullptr) {
        throw EngineError("Shadow map texture creation requires a WebGPU device.");
    }
    if (size == 0U) {
        throw EngineError("Shadow map texture creation requires a non-zero size.");
    }

    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow map texture array");
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{size, size, ShadowMapTarget::cascade_count()};
    descriptor.format = ShadowMapTarget::format();
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &descriptor);
    if (texture == nullptr) {
        throw EngineError("wgpuDeviceCreateTexture returned null for shadow map target.");
    }
    return texture;
}

// Creates the texture-array view sampled by the future opaque pass.
WGPUTextureView create_sampling_view(WGPUTexture texture) {
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow map sampling view");
    descriptor.format = ShadowMapTarget::format();
    descriptor.dimension = WGPUTextureViewDimension_2DArray;
    descriptor.baseMipLevel = 0;
    descriptor.mipLevelCount = 1;
    descriptor.baseArrayLayer = 0;
    descriptor.arrayLayerCount = ShadowMapTarget::cascade_count();
    descriptor.aspect = WGPUTextureAspect_All;

    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    if (view == nullptr) {
        throw EngineError("wgpuTextureCreateView returned null for shadow map sampling view.");
    }
    return view;
}

// Creates the render view for one cascade array layer.
WGPUTextureView create_render_view(WGPUTexture texture, std::uint32_t cascade_index) {
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow cascade render view");
    descriptor.format = ShadowMapTarget::format();
    descriptor.dimension = WGPUTextureViewDimension_2D;
    descriptor.baseMipLevel = 0;
    descriptor.mipLevelCount = 1;
    descriptor.baseArrayLayer = cascade_index;
    descriptor.arrayLayerCount = 1;
    descriptor.aspect = WGPUTextureAspect_All;

    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    if (view == nullptr) {
        throw EngineError("wgpuTextureCreateView returned null for shadow cascade render view.");
    }
    return view;
}

// Creates the comparison sampler consumed by future opaque shadow lookups.
WGPUSampler create_shadow_sampler(WGPUDevice device) {
    if (device == nullptr) {
        throw EngineError("Shadow map sampler creation requires a WebGPU device.");
    }

    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG shadow comparison sampler");
    descriptor.addressModeU = WGPUAddressMode_ClampToEdge;
    descriptor.addressModeV = WGPUAddressMode_ClampToEdge;
    descriptor.addressModeW = WGPUAddressMode_ClampToEdge;
    descriptor.magFilter = WGPUFilterMode_Linear;
    descriptor.minFilter = WGPUFilterMode_Linear;
    descriptor.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    descriptor.compare = WGPUCompareFunction_LessEqual;

    WGPUSampler sampler = wgpuDeviceCreateSampler(device, &descriptor);
    if (sampler == nullptr) {
        throw EngineError("wgpuDeviceCreateSampler returned null for shadow map target.");
    }
    return sampler;
}

} // namespace

// Stores the borrowed WebGPU handles needed for target allocation.
ShadowMapTarget::ShadowMapTarget(GpuContext gpu) : m_gpu(std::move(gpu)) {
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("ShadowMapTarget requires a WebGPU device and queue.");
    }
    ensure_sampler();
}

// Transfers owned WebGPU handles without duplicating them.
ShadowMapTarget::ShadowMapTarget(ShadowMapTarget&& other) noexcept
    : m_gpu(std::move(other.m_gpu)), m_texture(std::exchange(other.m_texture, nullptr)),
      m_sampling_view(std::exchange(other.m_sampling_view, nullptr)), m_render_views(other.m_render_views),
      m_sampler(std::exchange(other.m_sampler, nullptr)), m_size(std::exchange(other.m_size, 0)),
      m_view_generation(std::exchange(other.m_view_generation, 0)), m_counters(other.m_counters) {
    other.m_render_views = {};
    other.m_counters = RendererCounters{};
}

// Releases current resources, then transfers ownership from another target.
ShadowMapTarget& ShadowMapTarget::operator=(ShadowMapTarget&& other) noexcept {
    if (this != &other) {
        release();
        m_gpu = std::move(other.m_gpu);
        m_texture = std::exchange(other.m_texture, nullptr);
        m_sampling_view = std::exchange(other.m_sampling_view, nullptr);
        m_render_views = other.m_render_views;
        other.m_render_views = {};
        m_sampler = std::exchange(other.m_sampler, nullptr);
        m_size = std::exchange(other.m_size, 0);
        m_view_generation = std::exchange(other.m_view_generation, 0);
        m_counters = other.m_counters;
        other.m_counters = RendererCounters{};
    }
    return *this;
}

// Releases owned WebGPU resources.
ShadowMapTarget::~ShadowMapTarget() {
    release();
}

// Resizes the texture array, or releases texture/views for a zero size.
void ShadowMapTarget::resize(std::uint32_t size) {
    ensure_sampler();
    if (size == 0U) {
        if (release_texture_views()) {
            m_view_generation += 1U;
        }
        return;
    }
    if (m_texture != nullptr && m_size == size) {
        return;
    }

    WGPUTexture next_texture = create_shadow_texture(m_gpu.m_device, size);
    m_counters.m_texture_create_count += 1;
    WGPUTextureView next_sampling_view = nullptr;
    std::array<WGPUTextureView, shadow_cascade_count()> next_render_views{};
    try {
        next_sampling_view = create_sampling_view(next_texture);
        m_counters.m_texture_view_create_count += 1;
        for (std::uint32_t index = 0; index < cascade_count(); ++index) {
            next_render_views[index] = create_render_view(next_texture, index);
            m_counters.m_texture_view_create_count += 1;
        }
    } catch (...) {
        for (WGPUTextureView view : next_render_views) {
            if (view != nullptr) {
                wgpuTextureViewRelease(view);
            }
        }
        if (next_sampling_view != nullptr) {
            wgpuTextureViewRelease(next_sampling_view);
        }
        wgpuTextureRelease(next_texture);
        throw;
    }

    (void)release_texture_views();
    m_texture = next_texture;
    m_sampling_view = next_sampling_view;
    m_render_views = next_render_views;
    m_size = size;
    m_view_generation += 1;
}

// Releases all owned WebGPU resources.
void ShadowMapTarget::release() noexcept {
    if (release_texture_views()) {
        m_view_generation += 1U;
    }
    if (m_sampler != nullptr) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
}

// Returns the array view used by future opaque shadow sampling.
WGPUTextureView ShadowMapTarget::sampling_view() const noexcept {
    return m_sampling_view;
}

// Returns the render view for one cascade layer.
WGPUTextureView ShadowMapTarget::render_view(std::uint32_t cascade_index) const {
    if (cascade_index >= cascade_count()) {
        throw EngineError("Shadow map cascade render view index is out of range.");
    }
    return m_render_views[cascade_index];
}

// Returns the comparison sampler used by future opaque shadow sampling.
WGPUSampler ShadowMapTarget::sampler() const noexcept {
    return m_sampler;
}

// Returns the square map size in texels.
std::uint32_t ShadowMapTarget::size() const noexcept {
    return m_size;
}

// Returns a token incremented whenever texture views change.
std::uint64_t ShadowMapTarget::view_generation() const noexcept {
    return m_view_generation;
}

// Returns the estimated depth bytes for the current texture allocation.
std::uint64_t ShadowMapTarget::estimated_depth_bytes() const noexcept {
    return static_cast<std::uint64_t>(m_size) * static_cast<std::uint64_t>(m_size) * cascade_count() *
           _depth32_float_bytes_per_texel;
}

// Reports durable texture/view creation counters.
RendererCounters ShadowMapTarget::counters() const noexcept {
    return m_counters;
}

// Ensures the comparison sampler exists after construction or release.
void ShadowMapTarget::ensure_sampler() {
    if (m_sampler == nullptr) {
        m_sampler = create_shadow_sampler(m_gpu.m_device);
    }
}

// Releases texture and texture views while keeping the comparison sampler.
bool ShadowMapTarget::release_texture_views() noexcept {
    bool released = false;
    for (WGPUTextureView& view : m_render_views) {
        if (view != nullptr) {
            wgpuTextureViewRelease(view);
            view = nullptr;
            released = true;
        }
    }
    if (m_sampling_view != nullptr) {
        wgpuTextureViewRelease(m_sampling_view);
        m_sampling_view = nullptr;
        released = true;
    }
    if (m_texture != nullptr) {
        wgpuTextureRelease(m_texture);
        m_texture = nullptr;
        released = true;
    }
    m_size = 0;
    return released;
}

} // namespace ofg
