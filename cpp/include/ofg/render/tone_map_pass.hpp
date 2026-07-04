// Final HDR-to-platform tone mapping pass.
//
// ToneMapPass reads the renderer-owned RGBA16Float scene color texture with
// textureLoad, applies exposure plus an ACES-fitted curve, and writes to the
// platform render target with the correct output encoding for sRGB and non-sRGB
// target formats.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/renderer_counters.hpp"

#include <cstdint>
#include <memory>

#include <webgpu/webgpu.h>

namespace ofg {

enum class ToneMapOutputEncoding {
    LinearOutput,
    ManualSrgb,
};

struct ToneMapBloomInput {
    WGPUTextureView m_view{nullptr};
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
    float m_intensity{0.0f};
    math::Vec3 m_tint{1.0f, 1.0f, 1.0f};
};

// Returns the output encoding mode required by a platform texture format.
[[nodiscard]] ToneMapOutputEncoding tone_map_output_encoding_for(WGPUTextureFormat output_format);
// Returns a disabled bloom input that makes tone mapping match the old path.
[[nodiscard]] ToneMapBloomInput disabled_tone_map_bloom_input() noexcept;

class ToneMapPass {
public:
    ToneMapPass(const ToneMapPass&) = delete;
    ToneMapPass& operator=(const ToneMapPass&) = delete;
    ToneMapPass(ToneMapPass&&) = delete;
    ToneMapPass& operator=(ToneMapPass&&) = delete;
    ~ToneMapPass();

    // Creates shader, layout, pipeline, and persistent uniforms for tone mapping.
    [[nodiscard]] static std::unique_ptr<ToneMapPass> create(
        GpuContext gpu, WGPUTextureFormat output_format, ToneMapOutputEncoding encoding);
    // Encodes the full-screen tone-map draw into the caller-owned command encoder.
    void render(WGPUCommandEncoder encoder, WGPUTextureView scene_color_view, RenderTarget output_target);
    // Encodes the full-screen tone-map draw with an optional bloom contribution.
    void render(WGPUCommandEncoder encoder,
        WGPUTextureView scene_color_view,
        ToneMapBloomInput bloom_input,
        RenderTarget output_target);
    // Updates the exposure multiplier used before tone mapping.
    void set_exposure(float exposure);
    // Returns the current exposure multiplier.
    [[nodiscard]] float exposure() const noexcept;
    // Reports durable resource creation counters.
    [[nodiscard]] RendererCounters counters() const noexcept;

private:
    // Stores already-created pass GPU state.
    ToneMapPass(GpuContext gpu,
        WGPUTextureFormat output_format,
        ToneMapOutputEncoding encoding,
        WGPUShaderModule shader_module,
        WGPUBindGroupLayout bind_group_layout,
        WGPUPipelineLayout pipeline_layout,
        WGPURenderPipeline pipeline,
        WGPUBuffer uniform_buffer,
        WGPUTexture fallback_bloom_texture,
        WGPUTextureView fallback_bloom_view);

    // Recreates the scene-color/bloom bind group when texture views change.
    void ensure_bind_group(WGPUTextureView scene_color_view, WGPUTextureView bloom_view);
    // Writes exposure, output encoding, and bloom composite data into the uniform buffer.
    void write_uniforms(ToneMapBloomInput bloom_input) const;
    // Releases all WebGPU handles owned by this pass.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    WGPUTextureFormat m_output_format{WGPUTextureFormat_Undefined};
    ToneMapOutputEncoding m_encoding{ToneMapOutputEncoding::ManualSrgb};
    WGPUShaderModule m_shader_module{nullptr};
    WGPUBindGroupLayout m_bind_group_layout{nullptr};
    WGPUPipelineLayout m_pipeline_layout{nullptr};
    WGPURenderPipeline m_pipeline{nullptr};
    WGPUBuffer m_uniform_buffer{nullptr};
    WGPUTexture m_fallback_bloom_texture{nullptr};
    WGPUTextureView m_fallback_bloom_view{nullptr};
    WGPUBindGroup m_bind_group{nullptr};
    WGPUTextureView m_bound_scene_color_view{nullptr};
    WGPUTextureView m_bound_bloom_view{nullptr};
    float m_exposure{1.0f};
    RendererCounters m_counters;
};

} // namespace ofg
