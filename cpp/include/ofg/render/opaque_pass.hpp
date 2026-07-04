// Opaque WebGPU render pass for resolved OFG draw lists.
//
// OpaquePass owns pass-level GPU state: frame uniforms, dynamic draw uniforms,
// and the pipeline cache used by opaque mesh draws. The renderer owns the
// shared depth target so later scene-space passes can use the same depth view.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/lighting.hpp"
#include "ofg/render/pipeline_cache.hpp"
#include "ofg/render/renderer_counters.hpp"

#include <cstdint>
#include <memory>
#include <span>

#include <webgpu/webgpu.h>

namespace ofg {

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
    // Encodes opaque draw commands into the caller-owned scene render pass.
    void draw(WGPURenderPassEncoder pass,
        const CameraProperties& camera,
        std::span<const LightProperties> lights,
        AmbientLight ambient_light,
        const DrawList& draw_list);
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
    // Releases pass-level layouts, buffers, and bind groups.
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
    std::uint32_t m_buffer_create_count{0};
    std::uint32_t m_bind_group_create_count{0};
    PipelineCache m_pipeline_cache;
};

} // namespace ofg
