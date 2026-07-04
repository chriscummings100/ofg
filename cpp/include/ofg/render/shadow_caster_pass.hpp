// Depth-only shadow caster pass for cascaded sun shadows.
//
// The pass owns durable GPU state for rendering mesh positions into each shadow
// map cascade layer. It performs pass-specific culling from the full extracted
// render-object list so off-camera objects can still cast into visible regions.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/render/render_object.hpp"
#include "ofg/render/renderer_counters.hpp"
#include "ofg/render/shadow_cascade.hpp"
#include "ofg/render/shadow_diagnostics.hpp"
#include "ofg/render/shadow_map_target.hpp"
#include "ofg/render/shadow_settings.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <webgpu/webgpu.h>

namespace ofg {

class ShadowCasterPass {
public:
    ShadowCasterPass(const ShadowCasterPass&) = delete;
    ShadowCasterPass& operator=(const ShadowCasterPass&) = delete;
    ShadowCasterPass(ShadowCasterPass&&) = delete;
    ShadowCasterPass& operator=(ShadowCasterPass&&) = delete;
    ~ShadowCasterPass();

    // Creates shader, pipeline, layouts, bind groups, and uniform buffers.
    [[nodiscard]] static std::unique_ptr<ShadowCasterPass> create(GpuContext gpu, WGPUTextureFormat depth_format);
    // Encodes one depth-only render pass per active cascade.
    void render(WGPUCommandEncoder encoder,
        ShadowMapTarget& target,
        const ShadowCascadeSet& cascades,
        const ShadowSettings& settings,
        std::span<const RenderObject> render_objects);
    // Reports the most recent shadow pass diagnostics.
    [[nodiscard]] ShadowPassDiagnostics diagnostics() const noexcept;
    // Reports durable renderer resource counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    // Stores already-created pass GPU state.
    ShadowCasterPass(GpuContext gpu,
        WGPUShaderModule shader_module,
        WGPUBindGroupLayout frame_layout,
        std::array<WGPUBuffer, shadow_cascade_count()> frame_buffers,
        std::array<WGPUBindGroup, shadow_cascade_count()> frame_bind_groups,
        WGPUBindGroupLayout draw_layout,
        std::array<WGPUBuffer, shadow_cascade_count()> draw_buffers,
        std::array<WGPUBindGroup, shadow_cascade_count()> draw_bind_groups,
        WGPUPipelineLayout pipeline_layout,
        WGPURenderPipeline pipeline,
        std::uint32_t draw_capacity);

    // Recreates the dynamic draw uniform buffer for a larger caster count.
    void ensure_draw_capacity(std::uint32_t draw_count);
    // Releases pass-owned GPU handles.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    WGPUShaderModule m_shader_module{nullptr};
    WGPUBindGroupLayout m_frame_layout{nullptr};
    std::array<WGPUBuffer, shadow_cascade_count()> m_frame_buffers{};
    std::array<WGPUBindGroup, shadow_cascade_count()> m_frame_bind_groups{};
    WGPUBindGroupLayout m_draw_layout{nullptr};
    std::array<WGPUBuffer, shadow_cascade_count()> m_draw_buffers{};
    std::array<WGPUBindGroup, shadow_cascade_count()> m_draw_bind_groups{};
    WGPUPipelineLayout m_pipeline_layout{nullptr};
    WGPURenderPipeline m_pipeline{nullptr};
    std::uint32_t m_draw_capacity{0};
    std::uint32_t m_buffer_create_count{0};
    std::uint32_t m_bind_group_create_count{0};
    ShadowPassDiagnostics m_diagnostics;
    std::vector<const RenderObject*> m_culled_casters;
};

} // namespace ofg
