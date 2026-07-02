// Opaque WebGPU render pass for resolved OFG draw lists.
//
// OpaquePass owns pass-level GPU state: frame uniforms, dynamic draw uniforms,
// depth target state, and the pipeline cache used by opaque mesh draws.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/pipeline_cache.hpp"

#include <cstdint>
#include <memory>

#include <webgpu/webgpu.h>

namespace ofg {

struct RendererCounters {
    std::uint32_t m_pipeline_create_count{0};
    std::uint32_t m_buffer_create_count{0};
};

class OpaquePass {
public:
    OpaquePass(const OpaquePass&) = delete;
    OpaquePass& operator=(const OpaquePass&) = delete;
    OpaquePass(OpaquePass&&) = delete;
    OpaquePass& operator=(OpaquePass&&) = delete;
    ~OpaquePass();

    // Creates pass-level bind group layouts and uniform buffers.
    [[nodiscard]] static std::unique_ptr<OpaquePass> create(GpuContext gpu, WGPUTextureFormat color_format);
    // Ensures pipelines exist for every valid draw-list material.
    void prepare(const DrawList& draw_list);
    // Resizes or releases the pass depth target.
    void resize(std::uint32_t width, std::uint32_t height);
    // Encodes opaque draws into the caller-owned command encoder.
    void render(
        WGPUCommandEncoder encoder, RenderTarget target, const CameraProperties& camera, const DrawList& draw_list);
    // Reports durable renderer resource counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    // Stores already-created pass GPU state.
    OpaquePass(GpuContext gpu,
        WGPUTextureFormat color_format,
        WGPUBindGroupLayout frame_layout,
        WGPUBuffer frame_buffer,
        WGPUBindGroup frame_bind_group,
        WGPUBindGroupLayout draw_layout,
        WGPUBuffer draw_buffer,
        WGPUBindGroup draw_bind_group,
        std::uint32_t draw_capacity);

    // Recreates the dynamic draw uniform buffer for a larger command count.
    void ensure_draw_capacity(std::uint32_t draw_count);
    // Releases the current depth texture and view.
    void release_depth_state() noexcept;
    // Releases pass-level layouts, buffers, bind groups, and depth state.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    WGPUTextureFormat m_color_format{WGPUTextureFormat_Undefined};
    WGPUTextureFormat m_depth_format{WGPUTextureFormat_Depth24Plus};
    WGPUBindGroupLayout m_frame_layout{nullptr};
    WGPUBuffer m_frame_buffer{nullptr};
    WGPUBindGroup m_frame_bind_group{nullptr};
    WGPUBindGroupLayout m_draw_layout{nullptr};
    WGPUBuffer m_draw_buffer{nullptr};
    WGPUBindGroup m_draw_bind_group{nullptr};
    std::uint32_t m_draw_capacity{0};
    WGPUTexture m_depth_texture{nullptr};
    WGPUTextureView m_depth_view{nullptr};
    std::uint32_t m_depth_width{0};
    std::uint32_t m_depth_height{0};
    std::uint32_t m_buffer_create_count{0};
    PipelineCache m_pipeline_cache;
};

} // namespace ofg
