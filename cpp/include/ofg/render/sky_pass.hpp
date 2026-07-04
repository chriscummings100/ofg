// Procedural sky render pass.
//
// SkyPass draws a full-screen triangle into the HDR scene-color target after
// opaque geometry has populated shared depth. Its pipeline uses depth compare
// Equal at depth 1.0 so only untouched background pixels receive sky color.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/lighting.hpp"
#include "ofg/render/renderer_counters.hpp"

#include <array>
#include <memory>
#include <span>

#include <webgpu/webgpu.h>

namespace ofg {

class Environment;

struct SkyPassUniforms {
    std::array<float, 48> m_values{};
};

// Packs camera, sun, and environment state into the WGSL sky uniform layout.
[[nodiscard]] SkyPassUniforms build_sky_pass_uniforms(
    const CameraProperties& camera, const Environment& environment, std::span<const LightProperties> lights);

class SkyPass {
public:
    SkyPass(const SkyPass&) = delete;
    SkyPass& operator=(const SkyPass&) = delete;
    SkyPass(SkyPass&&) = delete;
    SkyPass& operator=(SkyPass&&) = delete;
    ~SkyPass();

    // Creates shader, layout, pipeline, and persistent uniforms for sky rendering.
    [[nodiscard]] static std::unique_ptr<SkyPass> create(
        GpuContext gpu, WGPUTextureFormat color_format, WGPUTextureFormat depth_format);
    // Encodes the sky fullscreen draw into the caller-owned scene render pass.
    void draw(WGPURenderPassEncoder pass,
        const CameraProperties& camera,
        const Environment& environment,
        std::span<const LightProperties> lights);
    // Reports durable resource creation counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    // Stores already-created pass GPU state.
    SkyPass(GpuContext gpu,
        WGPUShaderModule shader_module,
        WGPUBindGroupLayout bind_group_layout,
        WGPUPipelineLayout pipeline_layout,
        WGPURenderPipeline pipeline,
        WGPUBuffer uniform_buffer,
        WGPUBindGroup bind_group);

    // Releases all WebGPU handles owned by this pass.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    WGPUShaderModule m_shader_module{nullptr};
    WGPUBindGroupLayout m_bind_group_layout{nullptr};
    WGPUPipelineLayout m_pipeline_layout{nullptr};
    WGPURenderPipeline m_pipeline{nullptr};
    WGPUBuffer m_uniform_buffer{nullptr};
    WGPUBindGroup m_bind_group{nullptr};
    RendererCounters m_counters;
};

} // namespace ofg
