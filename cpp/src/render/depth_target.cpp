// Renderer-owned shared depth target implementation.
#include "ofg/render/depth_target.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include <utility>

namespace ofg {

// Stores the borrowed WebGPU handles needed for depth allocation.
DepthTarget::DepthTarget(GpuContext gpu) : m_gpu(std::move(gpu)) {
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("DepthTarget requires a WebGPU device and queue.");
    }
}

// Transfers the owned texture/view without duplicating handles.
DepthTarget::DepthTarget(DepthTarget&& other) noexcept
    : m_gpu(std::move(other.m_gpu)), m_texture(std::exchange(other.m_texture, nullptr)),
      m_view(std::exchange(other.m_view, nullptr)), m_width(std::exchange(other.m_width, 0)),
      m_height(std::exchange(other.m_height, 0)), m_view_generation(std::exchange(other.m_view_generation, 0)),
      m_counters(other.m_counters) {
    other.m_counters = RendererCounters{};
}

// Releases current resources, then transfers ownership from another target.
DepthTarget& DepthTarget::operator=(DepthTarget&& other) noexcept {
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
DepthTarget::~DepthTarget() {
    release();
}

// Resizes the owned texture/view or releases them for a zero-size target.
void DepthTarget::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        release();
        return;
    }
    if (m_view != nullptr && width == m_width && height == m_height) {
        return;
    }

    WGPUTexture next_texture =
        gpu::create_depth_texture(m_gpu.m_device, format(), width, height, "OFG shared depth texture");
    m_counters.m_texture_create_count += 1;
    WGPUTextureView next_view = nullptr;
    try {
        next_view = gpu::create_depth_view(next_texture, format(), "OFG shared depth view");
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
void DepthTarget::release() noexcept {
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

// Returns the current depth view, or null before a nonzero resize.
WGPUTextureView DepthTarget::view() const noexcept {
    return m_view;
}

// Returns the current width in pixels.
std::uint32_t DepthTarget::width() const noexcept {
    return m_width;
}

// Returns the current height in pixels.
std::uint32_t DepthTarget::height() const noexcept {
    return m_height;
}

// Returns a token incremented whenever the texture view changes.
std::uint64_t DepthTarget::view_generation() const noexcept {
    return m_view_generation;
}

// Reports durable texture/view creation counters.
RendererCounters DepthTarget::counters() const noexcept {
    return m_counters;
}

} // namespace ofg
