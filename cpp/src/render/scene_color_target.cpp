// Renderer-owned HDR scene color target implementation.
#include "ofg/render/scene_color_target.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include <utility>

namespace ofg {
namespace {

// Creates the HDR scene-color texture for render attachment and texture loads.
WGPUTexture create_scene_color_texture(
    WGPUDevice device, std::uint32_t width, std::uint32_t height, WGPUTextureFormat format) {
    if (device == nullptr) {
        throw EngineError("Scene color texture creation requires a WebGPU device.");
    }
    if (width == 0 || height == 0) {
        throw EngineError("Scene color texture creation requires non-zero dimensions.");
    }

    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG scene color texture");
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{width, height, 1};
    descriptor.format = format;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &descriptor);
    if (texture == nullptr) {
        throw EngineError("wgpuDeviceCreateTexture returned null for scene color target.");
    }
    return texture;
}

// Creates the default 2D view for the HDR scene-color texture.
WGPUTextureView create_scene_color_view(WGPUTexture texture, WGPUTextureFormat format) {
    if (texture == nullptr) {
        throw EngineError("Scene color view creation requires a WebGPU texture.");
    }

    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.label = gpu::cstring_view("OFG scene color view");
    descriptor.format = format;
    descriptor.dimension = WGPUTextureViewDimension_2D;
    descriptor.baseMipLevel = 0;
    descriptor.mipLevelCount = 1;
    descriptor.baseArrayLayer = 0;
    descriptor.arrayLayerCount = 1;
    descriptor.aspect = WGPUTextureAspect_All;

    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    if (view == nullptr) {
        throw EngineError("wgpuTextureCreateView returned null for scene color target.");
    }
    return view;
}

} // namespace

// Stores the borrowed WebGPU handles needed for target allocation.
SceneColorTarget::SceneColorTarget(GpuContext gpu) : m_gpu(std::move(gpu)) {
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("SceneColorTarget requires a WebGPU device and queue.");
    }
}

// Transfers the owned texture/view without duplicating handles.
SceneColorTarget::SceneColorTarget(SceneColorTarget&& other) noexcept
    : m_gpu(std::move(other.m_gpu)), m_texture(std::exchange(other.m_texture, nullptr)),
      m_view(std::exchange(other.m_view, nullptr)), m_width(std::exchange(other.m_width, 0)),
      m_height(std::exchange(other.m_height, 0)), m_view_generation(std::exchange(other.m_view_generation, 0)),
      m_counters(other.m_counters) {
    other.m_counters = RendererCounters{};
}

// Releases current resources, then transfers ownership from another target.
SceneColorTarget& SceneColorTarget::operator=(SceneColorTarget&& other) noexcept {
    if (this != &other) {
        release();
        m_gpu = std::move(other.m_gpu);
        m_texture = std::exchange(other.m_texture, nullptr);
        m_view = std::exchange(other.m_view, nullptr);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
        m_view_generation = std::exchange(other.m_view_generation, 0);
        m_counters = other.m_counters;
        other.m_counters = RendererCounters{};
    }
    return *this;
}

// Releases the owned texture/view.
SceneColorTarget::~SceneColorTarget() {
    release();
}

// Resizes the owned texture/view or releases them for a zero-size target.
void SceneColorTarget::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        release();
        return;
    }
    if (m_view != nullptr && width == m_width && height == m_height) {
        return;
    }

    WGPUTexture next_texture = create_scene_color_texture(m_gpu.m_device, width, height, format());
    m_counters.m_texture_create_count += 1;
    WGPUTextureView next_view = nullptr;
    try {
        next_view = create_scene_color_view(next_texture, format());
        m_counters.m_texture_view_create_count += 1;
    } catch (...) {
        wgpuTextureRelease(next_texture);
        throw;
    }

    release();
    m_texture = next_texture;
    m_view = next_view;
    m_width = width;
    m_height = height;
    m_view_generation += 1;
}

// Releases the owned texture/view while preserving the borrowed GPU context.
void SceneColorTarget::release() noexcept {
    if (m_view != nullptr) {
        wgpuTextureViewRelease(m_view);
        m_view = nullptr;
    }
    if (m_texture != nullptr) {
        wgpuTextureRelease(m_texture);
        m_texture = nullptr;
    }
    m_width = 0;
    m_height = 0;
}

// Returns this target as a render attachment descriptor.
RenderTarget SceneColorTarget::render_target() const {
    if (m_view == nullptr || m_width == 0 || m_height == 0) {
        throw EngineError("Scene color render target requires a live texture view.");
    }
    return RenderTarget{m_view, format(), m_width, m_height};
}

// Returns the current texture view, or null before a nonzero resize.
WGPUTextureView SceneColorTarget::view() const noexcept {
    return m_view;
}

// Returns the current width in pixels.
std::uint32_t SceneColorTarget::width() const noexcept {
    return m_width;
}

// Returns the current height in pixels.
std::uint32_t SceneColorTarget::height() const noexcept {
    return m_height;
}

// Returns a token incremented whenever the texture view changes.
std::uint64_t SceneColorTarget::view_generation() const noexcept {
    return m_view_generation;
}

// Reports durable texture/view creation counters.
RendererCounters SceneColorTarget::counters() const noexcept {
    return m_counters;
}

} // namespace ofg
