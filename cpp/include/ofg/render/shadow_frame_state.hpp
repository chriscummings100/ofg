// CPU-to-WGSL shadow sampling contract for opaque rendering.
//
// ShadowFrameState is the compact per-frame bridge between renderer-owned
// cascade construction, renderer-owned shadow-map textures, and the opaque PBR
// shader. It deliberately keeps WebGPU texture handles out of the cascade math
// types so CPU tests can validate the packed uniform contract independently.
#pragma once

#include "ofg/render/shadow_cascade.hpp"
#include "ofg/render/shadow_settings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <webgpu/webgpu.h>

namespace ofg {

// Returns the number of floats in the group-3 opaque shadow uniform.
[[nodiscard]] constexpr std::size_t shadow_frame_uniform_float_count() noexcept {
    return 68;
}

// Returns the byte size of the group-3 opaque shadow uniform.
[[nodiscard]] constexpr std::uint64_t shadow_frame_uniform_byte_size() noexcept {
    return sizeof(float) * shadow_frame_uniform_float_count();
}

// Returns the matrix offset for one packed cascade clip-from-world matrix.
[[nodiscard]] constexpr std::size_t shadow_frame_uniform_matrix_offset(std::size_t cascade_index) noexcept {
    return cascade_index * 16U;
}

[[nodiscard]] constexpr std::size_t shadow_frame_uniform_cascade_end_offset() noexcept {
    return 48U;
}

[[nodiscard]] constexpr std::size_t shadow_frame_uniform_blend_width_offset() noexcept {
    return 52U;
}

[[nodiscard]] constexpr std::size_t shadow_frame_uniform_texel_size_offset() noexcept {
    return 56U;
}

[[nodiscard]] constexpr std::size_t shadow_frame_uniform_options_offset() noexcept {
    return 60U;
}

[[nodiscard]] constexpr std::size_t shadow_frame_uniform_options2_offset() noexcept {
    return 64U;
}

struct ShadowFrameUniforms {
    std::array<float, shadow_frame_uniform_float_count()> m_values{};
};

struct ShadowFrameState {
    bool m_has_cascades{false};
    ShadowCascadeSet m_cascades{};
    ShadowSettings m_settings{};
    WGPUTextureView m_sampling_view{nullptr};
    WGPUSampler m_sampler{nullptr};
    std::uint32_t m_map_size{0};
    std::uint64_t m_view_generation{0};
};

// Builds a frame state that keeps opaque shading lit and binds fallback resources.
[[nodiscard]] ShadowFrameState make_disabled_shadow_frame_state(
    const ShadowSettings& settings = ShadowSettings{}) noexcept;

// Builds a frame state that can sample a live shadow-map target when handles are valid.
[[nodiscard]] ShadowFrameState make_shadow_frame_state(const ShadowCascadeSet& cascades,
    const ShadowSettings& settings,
    WGPUTextureView sampling_view,
    WGPUSampler sampler,
    std::uint32_t map_size,
    std::uint64_t view_generation) noexcept;

// Reports whether a state has both nonzero shadow intensity and live texture handles.
[[nodiscard]] bool shadow_frame_state_has_live_sampling(const ShadowFrameState& state) noexcept;

// Packs the frame state into the WGSL ShadowUniforms layout used by opaque_uber.wgsl.
[[nodiscard]] ShadowFrameUniforms pack_shadow_frame_uniforms(const ShadowFrameState& state);

} // namespace ofg
