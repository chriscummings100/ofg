// CPU-side cascaded sun-shadow matrix and culling-volume construction.
//
// These values are produced once per frame from the resolved camera, current sun
// light direction, and ShadowSettings. Later GPU milestones will render and
// sample shadow maps from these matrices and plane sets.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/bounds.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/frustum.hpp"
#include "ofg/render/shadow_settings.hpp"

#include <array>
#include <cstdint>

namespace ofg {

struct ShadowCascade {
    std::uint32_t m_index{0};
    float m_near_distance{0.0f};
    float m_far_distance{0.0f};
    float m_blend_start_distance{0.0f};
    float m_blend_end_distance{0.0f};
    Bounds3 m_receiver_world_bounds{};
    Bounds3 m_light_space_bounds{};
    math::Mat4 m_world_from_light{math::mat4_identity()};
    math::Mat4 m_light_from_world{math::mat4_identity()};
    math::Mat4 m_clip_from_light{math::mat4_identity()};
    math::Mat4 m_clip_from_world{math::mat4_identity()};
    std::array<CullingPlane, 6> m_culling_planes{};
    float m_texel_world_size{0.0f};

    // Returns the owned cascade culling planes as a non-owning view.
    [[nodiscard]] CullingPlaneSet plane_set() const noexcept;
};

struct ShadowCascadeSet {
    std::array<ShadowCascade, shadow_cascade_count()> m_cascades{};
    math::Vec3 m_light_direction{};
    math::Vec3 m_effective_light_direction{};
    float m_sun_elevation_radians{0.0f};
    float m_effective_intensity{0.0f};
    bool m_low_sun_clamped{false};
};

// Returns the sun elevation implied by a light-travel direction.
[[nodiscard]] float shadow_sun_elevation_radians(math::Vec3 light_direction);

// Returns the 0..1 fade factor for the supplied sun elevation.
[[nodiscard]] float shadow_low_sun_visibility(float sun_elevation_radians, const ShadowSettings& settings);

// Clamps a low sun angle while preserving the horizontal sun azimuth when possible.
[[nodiscard]] math::Vec3 clamp_shadow_light_direction(
    math::Vec3 light_direction, const ShadowSettings& settings, bool& was_clamped);

// Builds all CPU-side cascades from a camera snapshot and current sun light direction.
[[nodiscard]] ShadowCascadeSet build_shadow_cascades(
    const CameraProperties& camera, math::Vec3 light_direction, const ShadowSettings& settings);

} // namespace ofg
