// HDR bloom post-effect pass.
//
// BloomPass reads the completed HDR scene-color texture after scene rendering,
// extracts bright energy into a reduced-resolution pyramid, upsamples it into a
// blurred bloom result, and returns that result as a frame-scoped TempBufferRef
// for ToneMapPass composition.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/bloom_settings.hpp"
#include "ofg/render/renderer_counters.hpp"
#include "ofg/render/temp_buffer.hpp"
#include "ofg/render/tone_map_pass.hpp"

#include <array>
#include <cstdint>
#include <memory>

#include <webgpu/webgpu.h>

namespace ofg {

struct BloomResult {
    TempBufferRef m_buffer;
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
    float m_intensity{0.0f};
    math::Vec3 m_tint{1.0f, 1.0f, 1.0f};

    // Reports whether this result has a live bloom texture for the current frame.
    [[nodiscard]] bool valid() const noexcept;
    // Returns the live bloom texture view, or null after release/frame end.
    [[nodiscard]] WGPUTextureView view() const noexcept;
    // Converts this result into the compact ToneMapPass input contract.
    [[nodiscard]] ToneMapBloomInput tone_map_input() const noexcept;
};

struct BloomPassDiagnostics {
    std::uint32_t m_active_level_count{0};
    std::uint32_t m_encoded_pass_count{0};
    std::uint32_t m_draw_count{0};
    std::uint64_t m_estimated_read_bytes{0};
    std::uint64_t m_estimated_write_bytes{0};
    bool m_skipped{false};
};

class BloomPass {
public:
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;
    BloomPass(BloomPass&&) = delete;
    BloomPass& operator=(BloomPass&&) = delete;
    ~BloomPass();

    // Creates shader, layout, pipeline, and persistent uniform state for bloom.
    [[nodiscard]] static std::unique_ptr<BloomPass> create(GpuContext gpu, WGPUTextureFormat bloom_format);
    // Encodes prefilter, downsample, and upsample passes into the caller-owned command encoder.
    [[nodiscard]] BloomResult render(WGPUCommandEncoder encoder,
        WGPUTextureView scene_color_view,
        std::uint32_t width,
        std::uint32_t height,
        const BloomSettings& settings);
    // Reports durable and transient WebGPU resource creation counters.
    [[nodiscard]] RendererCounters counters() const noexcept;
    // Reports the most recent render call's pass-count and byte estimates.
    [[nodiscard]] BloomPassDiagnostics diagnostics() const noexcept;

private:
    // Stores already-created pass GPU state.
    BloomPass(GpuContext gpu,
        WGPUTextureFormat bloom_format,
        WGPUShaderModule prefilter_downsample_shader_module,
        WGPUShaderModule upsample_shader_module,
        WGPUBindGroupLayout prefilter_downsample_bind_group_layout,
        WGPUBindGroupLayout upsample_bind_group_layout,
        WGPUPipelineLayout prefilter_downsample_pipeline_layout,
        WGPUPipelineLayout upsample_pipeline_layout,
        WGPURenderPipeline prefilter_pipeline,
        WGPURenderPipeline downsample_pipeline,
        WGPURenderPipeline upsample_pipeline,
        WGPUBuffer uniform_buffer);

    // Releases cached per-view bind groups.
    void release_cached_bind_groups() noexcept;
    // Returns a cached prefilter/downsample bind group for one source view.
    WGPUBindGroup prefilter_downsample_bind_group(std::uint32_t index, WGPUTextureView source_view);
    // Returns a cached upsample bind group for one lower/higher view pair.
    WGPUBindGroup upsample_bind_group(std::uint32_t index, WGPUTextureView lower_view, WGPUTextureView higher_view);
    // Releases all WebGPU handles owned by this pass.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    WGPUTextureFormat m_bloom_format{WGPUTextureFormat_Undefined};
    WGPUShaderModule m_prefilter_downsample_shader_module{nullptr};
    WGPUShaderModule m_upsample_shader_module{nullptr};
    WGPUBindGroupLayout m_prefilter_downsample_bind_group_layout{nullptr};
    WGPUBindGroupLayout m_upsample_bind_group_layout{nullptr};
    WGPUPipelineLayout m_prefilter_downsample_pipeline_layout{nullptr};
    WGPUPipelineLayout m_upsample_pipeline_layout{nullptr};
    WGPURenderPipeline m_prefilter_pipeline{nullptr};
    WGPURenderPipeline m_downsample_pipeline{nullptr};
    WGPURenderPipeline m_upsample_pipeline{nullptr};
    WGPUBuffer m_uniform_buffer{nullptr};
    WGPUTextureView m_cached_scene_color_view{nullptr};
    std::array<WGPUBindGroup, max_bloom_pyramid_levels> m_prefilter_downsample_bind_groups{};
    std::array<WGPUTextureView, max_bloom_pyramid_levels> m_prefilter_downsample_source_views{};
    std::array<WGPUBindGroup, max_bloom_pyramid_levels> m_upsample_bind_groups{};
    std::array<WGPUTextureView, max_bloom_pyramid_levels> m_upsample_lower_views{};
    std::array<WGPUTextureView, max_bloom_pyramid_levels> m_upsample_higher_views{};
    RendererCounters m_counters;
    BloomPassDiagnostics m_last_diagnostics;
};

} // namespace ofg
