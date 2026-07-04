// CPU-side bloom settings, pyramid sizing, and uniform packing.
//
// BloomSettings is renderer-owned configuration for the HDR bloom post effect.
// This module is intentionally independent of WebGPU handles so its validation,
// threshold math, and pyramid planning can be tested before the GPU pass exists.
#pragma once

#include "ofg/math/vec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ofg {

inline constexpr std::uint32_t max_bloom_pyramid_levels = 8;

struct BloomSettings {
    bool m_enabled{true};
    float m_threshold{0.6f};
    float m_soft_knee{0.75f};
    float m_intensity{0.35f};
    float m_scatter{0.85f};
    float m_clamp{64.0f};
    math::Vec3 m_tint{1.0f, 1.0f, 1.0f};
    std::uint32_t m_initial_downscale{2};
    std::uint32_t m_max_levels{6};
    std::uint32_t m_min_level_extent{2};
};

struct BloomPyramidLevel {
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
};

struct BloomPyramidPlan {
    std::array<BloomPyramidLevel, max_bloom_pyramid_levels> m_levels{};
    std::uint32_t m_level_count{0};

    // Reports whether this frame should skip bloom rendering.
    [[nodiscard]] bool empty() const noexcept {
        return m_level_count == 0;
    }
};

struct alignas(16) BloomUniformBlock {
    std::array<float, 16> m_values{};
};

static_assert(alignof(BloomUniformBlock) == 16);
static_assert(sizeof(BloomUniformBlock) == sizeof(float) * 16U);

// Returns the first authored bloom settings.
[[nodiscard]] BloomSettings default_bloom_settings() noexcept;
// Validates settings before CPU planning or GPU uniform packing.
void validate_bloom_settings(const BloomSettings& settings);
// Builds the reduced-resolution bloom level plan for one frame size.
[[nodiscard]] BloomPyramidPlan build_bloom_pyramid_plan(
    std::uint32_t width, std::uint32_t height, const BloomSettings& settings);
// Computes a scalar soft-threshold contribution for one brightness value.
[[nodiscard]] float bloom_prefilter_contribution(float brightness, float threshold, float soft_knee);
// Packs settings into the WGSL bloom uniform layout.
[[nodiscard]] BloomUniformBlock pack_bloom_uniforms(const BloomSettings& settings);

} // namespace ofg
