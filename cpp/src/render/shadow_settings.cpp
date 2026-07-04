// CPU-side settings and scalar helpers for cascaded sun shadows.
#include "ofg/render/shadow_settings.hpp"

#include "ofg/core/engine_error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ofg {
namespace {

// Returns whether a float is finite and strictly positive.
bool finite_positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

// Returns whether a float is finite and non-negative.
bool finite_non_negative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

} // namespace

// Throws EngineError when settings contain invalid ranges or non-finite values.
void validate_shadow_settings(const ShadowSettings& settings) {
    if (settings.m_map_size == 0U) {
        throw EngineError("Shadow map size must be greater than zero.");
    }
    if (!finite_non_negative(settings.m_intensity) || settings.m_intensity > 1.0f) {
        throw EngineError("Shadow intensity must be finite and between zero and one.");
    }
    if (!finite_non_negative(settings.m_receiver_depth_bias) || !finite_non_negative(settings.m_normal_bias) ||
        !finite_positive(settings.m_caster_depth_padding)) {
        throw EngineError("Shadow bias and caster padding settings must be finite valid ranges.");
    }
    if (!finite_non_negative(settings.m_low_sun_fade_end_radians) ||
        !finite_positive(settings.m_low_sun_fade_start_radians) ||
        settings.m_low_sun_fade_start_radians <= settings.m_low_sun_fade_end_radians ||
        !finite_positive(settings.m_min_shadow_sun_elevation_radians)) {
        throw EngineError("Shadow low-sun fade and clamp angles must be finite increasing ranges.");
    }

    float previous_end = 0.0f;
    for (std::size_t index = 0; index < shadow_cascade_count(); ++index) {
        const float end_distance = settings.m_cascade_end_distances[index];
        const float blend_width = settings.m_cascade_blend_widths[index];
        if (!finite_positive(end_distance) || end_distance <= previous_end) {
            throw EngineError("Shadow cascade end distances must be finite, positive, and strictly increasing.");
        }
        if (!finite_non_negative(blend_width) || blend_width >= end_distance - previous_end) {
            throw EngineError("Shadow cascade blend widths must be finite and smaller than their cascade interval.");
        }
        if (!finite_non_negative(settings.m_pcf_radius_texels[index])) {
            throw EngineError("Shadow PCF radius settings must be finite non-negative values.");
        }
        previous_end = end_distance;
    }
}

// Computes practical split distances blending uniform and logarithmic spacing.
std::array<float, shadow_cascade_count()> practical_split_distances(float near_z, float far_z, float lambda) {
    if (!finite_positive(near_z) || !finite_positive(far_z) || far_z <= near_z || !std::isfinite(lambda) ||
        lambda < 0.0f || lambda > 1.0f) {
        throw EngineError("Practical shadow splits require finite near/far distances and lambda in [0, 1].");
    }

    std::array<float, shadow_cascade_count()> splits{};
    const float ratio = far_z / near_z;
    for (std::size_t index = 0; index < shadow_cascade_count(); ++index) {
        const float fraction = static_cast<float>(index + 1U) / static_cast<float>(shadow_cascade_count());
        const float uniform_split = near_z + (far_z - near_z) * fraction;
        const float logarithmic_split = near_z * std::pow(ratio, fraction);
        splits[index] = logarithmic_split * lambda + uniform_split * (1.0f - lambda);
    }
    splits.back() = far_z;
    return splits;
}

// Returns the number of comparison samples implied by a PCF mode.
std::uint32_t shadow_pcf_sample_count(ShadowPcfMode mode) noexcept {
    switch (mode) {
    case ShadowPcfMode::Hard:
        return 1U;
    case ShadowPcfMode::FiveTap:
        return 5U;
    case ShadowPcfMode::NineTap:
        return 9U;
    }
    return 1U;
}

// Returns the stable debug-status string for a PCF mode.
const char* shadow_pcf_mode_name(ShadowPcfMode mode) noexcept {
    switch (mode) {
    case ShadowPcfMode::Hard:
        return "hard";
    case ShadowPcfMode::FiveTap:
        return "five_tap";
    case ShadowPcfMode::NineTap:
        return "nine_tap";
    }
    return "hard";
}

} // namespace ofg
