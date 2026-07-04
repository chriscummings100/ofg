// Final-target shadow-map visualization pass for renderer debugging.
//
// ShadowDebugPass samples the renderer-owned depth texture array and overlays
// one panel per cascade after tone mapping. It is intentionally independent of
// opaque shadow sampling so broken lighting can be separated from broken shadow
// map generation.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/renderer_counters.hpp"
#include "ofg/render/shadow_cascade.hpp"

#include <memory>

#include <webgpu/webgpu.h>

namespace ofg {

class ShadowDebugPass {
public:
    ShadowDebugPass(const ShadowDebugPass&) = delete;
    ShadowDebugPass& operator=(const ShadowDebugPass&) = delete;
    ShadowDebugPass(ShadowDebugPass&&) = delete;
    ShadowDebugPass& operator=(ShadowDebugPass&&) = delete;
    ~ShadowDebugPass();

    // Creates shader, layout, pipeline, and uniforms for depth-layer overlays.
    [[nodiscard]] static std::unique_ptr<ShadowDebugPass> create(GpuContext gpu, WGPUTextureFormat output_format);
    // Encodes an overlay of the three shadow-map cascade layers.
    void render(WGPUCommandEncoder encoder,
        WGPUTextureView shadow_map_view,
        const ShadowCascadeSet& cascades,
        RenderTarget output_target);
    // Reports durable resource creation counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    // Stores already-created pass GPU state.
    ShadowDebugPass(GpuContext gpu,
        WGPUTextureFormat output_format,
        WGPUShaderModule shader_module,
        WGPUBindGroupLayout bind_group_layout,
        WGPUPipelineLayout pipeline_layout,
        WGPURenderPipeline pipeline,
        WGPUBuffer uniform_buffer);

    // Recreates the bind group when the sampled shadow-map view changes.
    void ensure_bind_group(WGPUTextureView shadow_map_view);
    // Writes output-size data consumed by the overlay shader.
    void write_uniforms(const ShadowCascadeSet& cascades, RenderTarget output_target) const;
    // Releases all WebGPU handles owned by this pass.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    WGPUTextureFormat m_output_format{WGPUTextureFormat_Undefined};
    WGPUShaderModule m_shader_module{nullptr};
    WGPUBindGroupLayout m_bind_group_layout{nullptr};
    WGPUPipelineLayout m_pipeline_layout{nullptr};
    WGPURenderPipeline m_pipeline{nullptr};
    WGPUBuffer m_uniform_buffer{nullptr};
    WGPUBindGroup m_bind_group{nullptr};
    WGPUTextureView m_bound_shadow_map_view{nullptr};
    RendererCounters m_counters;
};

} // namespace ofg
