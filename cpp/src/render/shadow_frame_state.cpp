// CPU-to-WGSL shadow sampling contract for opaque rendering.
#include "ofg/render/shadow_frame_state.hpp"

#include "ofg/math/mat.hpp"

#include <algorithm>
#include <array>

namespace ofg {
namespace {

// Converts the CPU enum into the compact scalar consumed by WGSL.
float pcf_mode_uniform_value(ShadowPcfMode mode) noexcept {
    switch (mode) {
    case ShadowPcfMode::Hard:
        return 0.0f;
    case ShadowPcfMode::FiveTap:
        return 1.0f;
    case ShadowPcfMode::NineTap:
        return 2.0f;
    }
    return 0.0f;
}

// Copies one column-major matrix into the packed uniform array.
void pack_matrix(ShadowFrameUniforms& uniforms, std::size_t offset, math::Mat4 matrix) noexcept {
    const std::array<float, 16> packed = math::pack_mat4(matrix);
    std::copy(packed.begin(), packed.end(), uniforms.m_values.begin() + static_cast<std::ptrdiff_t>(offset));
}

} // namespace

// Builds a frame state that keeps opaque shading lit and binds fallback resources.
ShadowFrameState make_disabled_shadow_frame_state(const ShadowSettings& settings) noexcept {
    ShadowFrameState state;
    state.m_settings = settings;
    return state;
}

// Builds a frame state that can sample a live shadow-map target when handles are valid.
ShadowFrameState make_shadow_frame_state(const ShadowCascadeSet& cascades,
    const ShadowSettings& settings,
    WGPUTextureView sampling_view,
    WGPUSampler sampler,
    std::uint32_t map_size,
    std::uint64_t view_generation) noexcept {
    ShadowFrameState state;
    state.m_has_cascades = true;
    state.m_cascades = cascades;
    state.m_settings = settings;
    state.m_sampling_view = sampling_view;
    state.m_sampler = sampler;
    state.m_map_size = map_size;
    state.m_view_generation = view_generation;
    return state;
}

// Reports whether a state has both nonzero shadow intensity and live texture handles.
bool shadow_frame_state_has_live_sampling(const ShadowFrameState& state) noexcept {
    return state.m_has_cascades && state.m_settings.m_enabled && state.m_cascades.m_effective_intensity > 0.0f &&
           state.m_sampling_view != nullptr && state.m_sampler != nullptr && state.m_map_size > 0U;
}

// Packs the frame state into the WGSL ShadowUniforms layout used by opaque_uber.wgsl.
ShadowFrameUniforms pack_shadow_frame_uniforms(const ShadowFrameState& state) {
    validate_shadow_settings(state.m_settings);

    ShadowFrameUniforms uniforms;
    for (std::size_t index = 0; index < shadow_cascade_count(); ++index) {
        const math::Mat4 matrix =
            state.m_has_cascades ? state.m_cascades.m_cascades[index].m_clip_from_world : math::mat4_identity();
        pack_matrix(uniforms, shadow_frame_uniform_matrix_offset(index), matrix);
    }

    const bool live_sampling = shadow_frame_state_has_live_sampling(state);
    const std::uint32_t map_size = state.m_map_size == 0U ? 1U : state.m_map_size;
    const float enabled = live_sampling ? 1.0f : 0.0f;
    const float intensity = live_sampling ? state.m_cascades.m_effective_intensity : 0.0f;

    const std::size_t end_offset = shadow_frame_uniform_cascade_end_offset();
    const std::size_t blend_offset = shadow_frame_uniform_blend_width_offset();
    const std::size_t texel_offset = shadow_frame_uniform_texel_size_offset();
    for (std::size_t index = 0; index < shadow_cascade_count(); ++index) {
        uniforms.m_values[end_offset + index] = state.m_has_cascades ? state.m_cascades.m_cascades[index].m_far_distance
                                                                     : state.m_settings.m_cascade_end_distances[index];
        uniforms.m_values[blend_offset + index] = state.m_settings.m_cascade_blend_widths[index];
        uniforms.m_values[texel_offset + index] =
            state.m_has_cascades ? state.m_cascades.m_cascades[index].m_texel_world_size : 0.0f;
    }
    uniforms.m_values[end_offset + 3U] = uniforms.m_values[end_offset + 2U];
    uniforms.m_values[blend_offset + 3U] = 0.0f;
    uniforms.m_values[texel_offset + 3U] = 1.0f / static_cast<float>(map_size);

    const std::size_t options_offset = shadow_frame_uniform_options_offset();
    uniforms.m_values[options_offset + 0U] = enabled;
    uniforms.m_values[options_offset + 1U] = intensity;
    uniforms.m_values[options_offset + 2U] = state.m_settings.m_receiver_depth_bias;
    uniforms.m_values[options_offset + 3U] = state.m_settings.m_normal_bias;

    const std::size_t options2_offset = shadow_frame_uniform_options2_offset();
    uniforms.m_values[options2_offset + 0U] = pcf_mode_uniform_value(state.m_settings.m_pcf_mode);
    uniforms.m_values[options2_offset + 1U] = state.m_settings.m_pcf_radius_texels;
    uniforms.m_values[options2_offset + 2U] = static_cast<float>(map_size);
    uniforms.m_values[options2_offset + 3U] = uniforms.m_values[end_offset + 2U];
    return uniforms;
}

} // namespace ofg
