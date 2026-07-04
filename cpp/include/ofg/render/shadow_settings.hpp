// CPU-side settings and scalar helpers for cascaded sun shadows.
//
// This file deliberately contains no WebGPU handles. It defines the stable
// values that cascade math, shadow-map resources, and shader uniforms will
// share as later milestones wire the GPU path.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ofg {

enum class ShadowPcfMode {
    Hard,
    FiveTap,
    NineTap,
};

// Returns the fixed cascade count for the first OFG sun-shadow implementation.
[[nodiscard]] constexpr std::size_t shadow_cascade_count() noexcept {
    return 3;
}

struct ShadowSettings {
    bool m_enabled{true};
    std::uint32_t m_map_size{2048};
    std::array<float, shadow_cascade_count()> m_cascade_end_distances{12.0f, 32.0f, 80.0f};
    std::array<float, shadow_cascade_count()> m_cascade_blend_widths{2.0f, 4.0f, 8.0f};
    float m_intensity{0.75f};
    float m_receiver_depth_bias{0.0015f};
    float m_normal_bias{0.08f};
    ShadowPcfMode m_pcf_mode{ShadowPcfMode::NineTap};
    float m_pcf_radius_texels{1.75f};
    float m_low_sun_fade_start_radians{0.174532925f};
    float m_low_sun_fade_end_radians{0.017453292f};
    float m_min_shadow_sun_elevation_radians{0.087266462f};
    float m_caster_depth_padding{80.0f};
};

// Throws EngineError when settings contain invalid ranges or non-finite values.
void validate_shadow_settings(const ShadowSettings& settings);

// Computes practical split distances blending uniform and logarithmic spacing.
[[nodiscard]] std::array<float, shadow_cascade_count()> practical_split_distances(
    float near_z, float far_z, float lambda);

// Returns the number of comparison samples implied by a PCF mode.
[[nodiscard]] std::uint32_t shadow_pcf_sample_count(ShadowPcfMode mode) noexcept;

// Returns the stable debug-status string for a PCF mode.
[[nodiscard]] const char* shadow_pcf_mode_name(ShadowPcfMode mode) noexcept;

} // namespace ofg
