// Static reusable temporary render-target system.
//
// TempBuffer owns renderer-only WebGPU textures that are needed briefly while
// encoding post effects. Passes ask for a descriptor-matched buffer with
// TempBuffer::get(), may return it early after the final encoded GPU use, and
// otherwise rely on TempBuffer::end_frame() to make remaining active buffers
// reusable. This is not a game asset store and does not expose resources to
// TypeScript.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/renderer_counters.hpp"

#include <cstdint>
#include <string_view>

#include <webgpu/webgpu.h>

namespace ofg {

struct TempBufferDesc {
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
    WGPUTextureFormat m_format{WGPUTextureFormat_Undefined};
    WGPUTextureUsage m_usage{0};
    std::uint32_t m_mip_level_count{1};
    std::uint32_t m_array_layer_count{1};
    std::uint32_t m_sample_count{1};
};

// Compares temporary texture descriptors for exact reuse matching.
[[nodiscard]] bool operator==(const TempBufferDesc& left, const TempBufferDesc& right) noexcept;

// Validates that a temporary texture descriptor is supported by TempBuffer.
void validate_temp_buffer_desc(const TempBufferDesc& desc);

struct TempBufferStats {
    std::uint64_t m_active_bytes{0};
    std::uint64_t m_reusable_bytes{0};
    std::uint64_t m_peak_bytes{0};
    std::uint64_t m_created_count{0};
    std::uint64_t m_reused_count{0};
    std::uint64_t m_discarded_count{0};
    std::uint64_t m_active_count{0};
    std::uint64_t m_reusable_count{0};
    std::uint64_t m_early_release_count{0};
    std::uint64_t m_end_frame_return_count{0};
};

class TempBuffer;
class TempBufferStore;

class TempBufferRef {
public:
    TempBufferRef() = default;
    TempBufferRef(const TempBufferRef&) = delete;
    TempBufferRef& operator=(const TempBufferRef&) = delete;
    TempBufferRef(TempBufferRef&& other) noexcept;
    TempBufferRef& operator=(TempBufferRef&& other) noexcept;
    ~TempBufferRef() = default;

    // Reports whether this handle still refers to an active temp buffer.
    [[nodiscard]] bool valid() const noexcept;
    // Returns this temp buffer as a render target, or throws if inactive.
    [[nodiscard]] RenderTarget render_target() const;
    // Returns the active texture view, or null after release/frame end.
    [[nodiscard]] WGPUTextureView view() const noexcept;
    // Returns the width captured when this handle was issued.
    [[nodiscard]] std::uint32_t width() const noexcept;
    // Returns the height captured when this handle was issued.
    [[nodiscard]] std::uint32_t height() const noexcept;
    // Returns the format captured when this handle was issued.
    [[nodiscard]] WGPUTextureFormat format() const noexcept;

private:
    friend class TempBuffer;
    friend class TempBufferStore;

    // Creates a handle for one active temp-buffer checkout.
    TempBufferRef(std::uint32_t id,
        std::uint32_t generation,
        WGPUTextureView view,
        std::uint32_t width,
        std::uint32_t height,
        WGPUTextureFormat format) noexcept;

    // Clears this handle without touching the underlying texture.
    void invalidate() noexcept;

    std::uint32_t m_id{0};
    std::uint32_t m_generation{0};
    WGPUTextureView m_view{nullptr};
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
    WGPUTextureFormat m_format{WGPUTextureFormat_Undefined};
};

class TempBuffer {
public:
    TempBuffer(const TempBuffer&) = delete;
    TempBuffer& operator=(const TempBuffer&) = delete;
    TempBuffer(TempBuffer&&) = delete;
    TempBuffer& operator=(TempBuffer&&) = delete;
    ~TempBuffer() = delete;

    // Creates the temp-buffer singleton for one borrowed WebGPU device.
    static void create(GpuContext gpu);
    // Starts a renderer frame and performs stale inactive cleanup.
    static void begin_frame();
    // Returns a descriptor-matched temporary render target for the current frame.
    [[nodiscard]] static TempBufferRef get(const TempBufferDesc& desc, std::string_view debug_label);
    // Returns one temp buffer after its final encoded GPU use.
    static void release(TempBufferRef& buffer) noexcept;
    // Returns all still-active temp buffers to the reusable set.
    static void end_frame();
    // Releases all temp-buffer WebGPU resources for renderer teardown.
    [[nodiscard]] static bool release();
    // Destroys the singleton after lifecycle release has completed.
    static void destroy() noexcept;
    // Reports durable WebGPU creation counters.
    [[nodiscard]] static RendererCounters counters() noexcept;
    // Reports temp-buffer memory and reuse diagnostics.
    [[nodiscard]] static TempBufferStats stats() noexcept;

private:
    friend class TempBufferRef;

    // Reports whether a handle still matches an active internal entry.
    [[nodiscard]] static bool is_ref_active(const TempBufferRef& buffer) noexcept;
};

} // namespace ofg
