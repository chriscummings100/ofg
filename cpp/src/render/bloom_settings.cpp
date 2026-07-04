// CPU-side bloom settings, pyramid sizing, and uniform packing implementation.
#include "ofg/render/bloom_settings.hpp"

#include "ofg/core/engine_error.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace ofg {
namespace {

constexpr float _epsilon = 0.000001f;

// Returns true when a setting value is finite.
bool finite(float value) noexcept {
    return std::isfinite(value);
}

// Throws when a scalar setting is not finite.
void require_finite(float value, const char* label) {
    if (!finite(value)) {
        throw EngineError(std::string("Bloom ") + label + " must be finite.");
    }
}

// Throws when a scalar setting is negative.
void require_non_negative(float value, const char* label) {
    require_finite(value, label);
    if (value < 0.0f) {
        throw EngineError(std::string("Bloom ") + label + " must be non-negative.");
    }
}

// Returns ceil(value / divisor) for positive integers.
std::uint32_t ceil_div(std::uint32_t value, std::uint32_t divisor) noexcept {
    return (value / divisor) + ((value % divisor) == 0U ? 0U : 1U);
}

} // namespace

// Returns the first authored bloom settings.
BloomSettings default_bloom_settings() noexcept {
    return BloomSettings{};
}

// Validates settings before CPU planning or GPU uniform packing.
void validate_bloom_settings(const BloomSettings& settings) {
    require_non_negative(settings.m_threshold, "threshold");
    require_non_negative(settings.m_soft_knee, "soft knee");
    require_non_negative(settings.m_intensity, "intensity");
    require_finite(settings.m_scatter, "scatter");
    if (settings.m_scatter < 0.0f || settings.m_scatter > 1.0f) {
        throw EngineError("Bloom scatter must be in the range [0, 1].");
    }
    require_non_negative(settings.m_clamp, "clamp");
    require_non_negative(settings.m_tint.x, "tint red");
    require_non_negative(settings.m_tint.y, "tint green");
    require_non_negative(settings.m_tint.z, "tint blue");
    if (settings.m_initial_downscale != 2U && settings.m_initial_downscale != 4U) {
        throw EngineError("Bloom initial_downscale must be exactly 2 or 4.");
    }
    if (settings.m_max_levels == 0U || settings.m_max_levels > max_bloom_pyramid_levels) {
        throw EngineError("Bloom max_levels must be between 1 and " + std::to_string(max_bloom_pyramid_levels) + ".");
    }
    if (settings.m_min_level_extent == 0U) {
        throw EngineError("Bloom min_level_extent must be nonzero.");
    }
}

// Builds the reduced-resolution bloom level plan for one frame size.
BloomPyramidPlan build_bloom_pyramid_plan(std::uint32_t width, std::uint32_t height, const BloomSettings& settings) {
    validate_bloom_settings(settings);
    BloomPyramidPlan plan;
    if (!settings.m_enabled || width == 0U || height == 0U) {
        return plan;
    }

    std::uint32_t level_width = ceil_div(width, settings.m_initial_downscale);
    std::uint32_t level_height = ceil_div(height, settings.m_initial_downscale);
    for (std::uint32_t level = 0; level < settings.m_max_levels; ++level) {
        if (level_width < settings.m_min_level_extent || level_height < settings.m_min_level_extent) {
            break;
        }
        plan.m_levels[plan.m_level_count] = BloomPyramidLevel{level_width, level_height};
        plan.m_level_count += 1;
        level_width = ceil_div(level_width, 2U);
        level_height = ceil_div(level_height, 2U);
    }

    return plan;
}

// Computes a scalar soft-threshold contribution for one brightness value.
float bloom_prefilter_contribution(float brightness, float threshold, float soft_knee) {
    require_finite(brightness, "brightness");
    require_non_negative(threshold, "threshold");
    require_non_negative(soft_knee, "soft knee");

    const float safe_brightness = std::max(brightness, 0.0f);
    if (safe_brightness <= _epsilon) {
        return 0.0f;
    }

    const float knee = threshold * soft_knee;
    float contribution = 0.0f;
    if (knee <= _epsilon) {
        contribution = std::max(safe_brightness - threshold, 0.0f) / safe_brightness;
    } else {
        float soft = std::clamp(safe_brightness - threshold + knee, 0.0f, 2.0f * knee);
        soft = soft * soft / std::max(4.0f * knee, _epsilon);
        contribution = std::max(safe_brightness - threshold, soft) / safe_brightness;
    }
    return std::clamp(contribution, 0.0f, 1.0f);
}

// Packs settings into the WGSL bloom uniform layout.
BloomUniformBlock pack_bloom_uniforms(const BloomSettings& settings) {
    validate_bloom_settings(settings);
    BloomUniformBlock block;
    block.m_values[0] = settings.m_threshold;
    block.m_values[1] = settings.m_soft_knee;
    block.m_values[2] = settings.m_intensity;
    block.m_values[3] = settings.m_scatter;
    block.m_values[4] = settings.m_clamp;
    block.m_values[5] = settings.m_tint.x;
    block.m_values[6] = settings.m_tint.y;
    block.m_values[7] = settings.m_tint.z;
    block.m_values[8] = static_cast<float>(settings.m_initial_downscale);
    block.m_values[9] = static_cast<float>(settings.m_max_levels);
    block.m_values[10] = static_cast<float>(settings.m_min_level_extent);
    block.m_values[11] = settings.m_enabled ? 1.0f : 0.0f;
    return block;
}

} // namespace ofg
