// CPU-side cascaded sun-shadow matrix and culling-volume construction.
#include "ofg/render/shadow_cascade.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace ofg {
namespace {

constexpr float _minimum_extent = 0.001f;

// Returns whether a vector has only finite components.
bool vec3_is_finite(math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Returns a normalized vector or throws a renderer-facing EngineError.
math::Vec3 normalized_or_throw(math::Vec3 value, const char* message) {
    std::string error;
    std::optional<math::Vec3> normalized = math::normalize(value, error);
    if (!normalized.has_value()) {
        throw EngineError(message);
    }
    return *normalized;
}

// Returns the component-wise minimum.
math::Vec3 min_vec3(math::Vec3 left, math::Vec3 right) noexcept {
    return math::vec3(std::min(left.x, right.x), std::min(left.y, right.y), std::min(left.z, right.z));
}

// Returns the component-wise maximum.
math::Vec3 max_vec3(math::Vec3 left, math::Vec3 right) noexcept {
    return math::vec3(std::max(left.x, right.x), std::max(left.y, right.y), std::max(left.z, right.z));
}

// Builds finite bounds from world or light-space points.
Bounds3 bounds_from_points(const std::array<math::Vec3, 8>& points) {
    math::Vec3 minimum = points.front();
    math::Vec3 maximum = points.front();
    if (!vec3_is_finite(minimum)) {
        throw EngineError("Shadow cascade points must be finite.");
    }
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (!vec3_is_finite(points[index])) {
            throw EngineError("Shadow cascade points must be finite.");
        }
        minimum = min_vec3(minimum, points[index]);
        maximum = max_vec3(maximum, points[index]);
    }
    return Bounds3{minimum, maximum};
}

// Computes the eight camera-frustum corner points for one cascade interval.
std::array<math::Vec3, 8> camera_interval_corners_world(
    const CameraProperties& camera, float near_distance, float far_distance) {
    const float half_fov_tangent = std::tan(camera.vertical_fov_radians * 0.5f);
    const float near_half_height = half_fov_tangent * near_distance;
    const float near_half_width = near_half_height * camera.aspect;
    const float far_half_height = half_fov_tangent * far_distance;
    const float far_half_width = far_half_height * camera.aspect;

    const std::array<math::Vec3, 8> camera_space{{
        math::vec3(-near_half_width, -near_half_height, near_distance),
        math::vec3(near_half_width, -near_half_height, near_distance),
        math::vec3(-near_half_width, near_half_height, near_distance),
        math::vec3(near_half_width, near_half_height, near_distance),
        math::vec3(-far_half_width, -far_half_height, far_distance),
        math::vec3(far_half_width, -far_half_height, far_distance),
        math::vec3(-far_half_width, far_half_height, far_distance),
        math::vec3(far_half_width, far_half_height, far_distance),
    }};

    std::array<math::Vec3, 8> world_space{};
    for (std::size_t index = 0; index < camera_space.size(); ++index) {
        world_space[index] = math::transform_point(camera.world_from_camera, camera_space[index]);
    }
    return world_space;
}

// Returns the average point for the eight cascade corners.
math::Vec3 average_point(const std::array<math::Vec3, 8>& points) noexcept {
    math::Vec3 total = math::vec3(0.0f, 0.0f, 0.0f);
    for (math::Vec3 point : points) {
        total = math::add(total, point);
    }
    return math::mul(total, 1.0f / static_cast<float>(points.size()));
}

// Chooses an up vector that is not parallel to the light direction.
math::Vec3 light_up_for(math::Vec3 light_direction) noexcept {
    const math::Vec3 world_up = math::vec3(0.0f, 1.0f, 0.0f);
    if (std::fabs(math::dot(world_up, light_direction)) < 0.95f) {
        return world_up;
    }
    return math::vec3(0.0f, 0.0f, 1.0f);
}

// Builds a light-space view matrix whose +Z axis follows the light travel direction.
math::Mat4 light_from_world_for(math::Vec3 center, math::Vec3 light_direction) {
    std::string error;
    std::optional<math::Mat4> light_from_world =
        math::look_at_lh(center, math::add(center, light_direction), light_up_for(light_direction), error);
    if (!light_from_world.has_value()) {
        throw EngineError(error.empty() ? "Shadow light view creation failed." : error);
    }
    return *light_from_world;
}

// Copies a ViewFrustum plane span into owned cascade plane storage.
std::array<CullingPlane, 6> copy_frustum_planes(const ViewFrustum& frustum) {
    std::array<CullingPlane, 6> planes{};
    const std::span<const CullingPlane> source = frustum.planes();
    std::copy(source.begin(), source.end(), planes.begin());
    return planes;
}

// Returns the cascade near distance including overlap needed by the previous cascade's blend band.
float cascade_receiver_near_distance(
    const CameraProperties& camera, const ShadowSettings& settings, std::size_t index) {
    if (index == 0U) {
        return camera.near_z;
    }
    const float previous_end = settings.m_cascade_end_distances[index - 1U];
    const float previous_blend = settings.m_cascade_blend_widths[index - 1U];
    return std::max(camera.near_z, previous_end - previous_blend);
}

// Validates camera data needed by cascade construction.
void validate_camera_for_shadows(const CameraProperties& camera, const ShadowSettings& settings) {
    if (!std::isfinite(camera.vertical_fov_radians) || !std::isfinite(camera.aspect) || !std::isfinite(camera.near_z) ||
        !std::isfinite(camera.far_z) || camera.vertical_fov_radians <= 0.0f || camera.aspect <= 0.0f ||
        camera.near_z <= 0.0f || camera.far_z <= camera.near_z) {
        throw EngineError("Shadow cascades require finite valid camera projection data.");
    }
    if (settings.m_cascade_end_distances.back() > camera.far_z) {
        throw EngineError("Shadow cascade end distances must not exceed the camera far plane.");
    }
}

// Builds one cascade interval and light-space projection.
ShadowCascade build_one_cascade(const CameraProperties& camera,
    const ShadowSettings& settings,
    math::Vec3 effective_light_direction,
    std::uint32_t index,
    float near_distance,
    float far_distance) {
    const std::array<math::Vec3, 8> world_corners = camera_interval_corners_world(camera, near_distance, far_distance);
    const math::Vec3 world_center = average_point(world_corners);

    const math::Mat4 light_from_world = light_from_world_for(world_center, effective_light_direction);
    std::string error;
    std::optional<math::Mat4> world_from_light = math::inverse_affine(light_from_world, error);
    if (!world_from_light.has_value()) {
        throw EngineError(error.empty() ? "Shadow light view inverse creation failed." : error);
    }

    std::array<math::Vec3, 8> light_corners{};
    for (std::size_t corner_index = 0; corner_index < world_corners.size(); ++corner_index) {
        light_corners[corner_index] = math::transform_point(light_from_world, world_corners[corner_index]);
    }
    const Bounds3 light_receiver_bounds = bounds_from_points(light_corners);
    const float raw_width = std::max(light_receiver_bounds.m_max.x - light_receiver_bounds.m_min.x, _minimum_extent);
    const float raw_height = std::max(light_receiver_bounds.m_max.y - light_receiver_bounds.m_min.y, _minimum_extent);
    const float raw_texel_width = raw_width / static_cast<float>(settings.m_map_size);
    const float raw_texel_height = raw_height / static_cast<float>(settings.m_map_size);
    const float margin_texels = std::ceil(settings.m_pcf_radius_texels) + 2.0f;
    const float width = std::max(raw_width + raw_texel_width * margin_texels * 2.0f, _minimum_extent);
    const float height = std::max(raw_height + raw_texel_height * margin_texels * 2.0f, _minimum_extent);
    const float texel_width = width / static_cast<float>(settings.m_map_size);
    const float texel_height = height / static_cast<float>(settings.m_map_size);
    const float receiver_center_x = (light_receiver_bounds.m_min.x + light_receiver_bounds.m_max.x) * 0.5f;
    const float receiver_center_y = (light_receiver_bounds.m_min.y + light_receiver_bounds.m_max.y) * 0.5f;
    const float snapped_center_x = std::floor(receiver_center_x / texel_width) * texel_width;
    const float snapped_center_y = std::floor(receiver_center_y / texel_height) * texel_height;
    const float left = snapped_center_x - width * 0.5f;
    const float right = snapped_center_x + width * 0.5f;
    const float bottom = snapped_center_y - height * 0.5f;
    const float top = snapped_center_y + height * 0.5f;
    const float near_z = light_receiver_bounds.m_min.z - settings.m_caster_depth_padding;
    const float far_z = light_receiver_bounds.m_max.z + std::max(texel_width, texel_height);

    std::optional<math::Mat4> clip_from_light = math::orthographic_lh(left, right, bottom, top, near_z, far_z, error);
    if (!clip_from_light.has_value()) {
        throw EngineError(error.empty() ? "Shadow orthographic projection creation failed." : error);
    }

    ShadowCascade cascade;
    cascade.m_index = index;
    cascade.m_near_distance = near_distance;
    cascade.m_far_distance = far_distance;
    cascade.m_blend_start_distance = far_distance - settings.m_cascade_blend_widths[index];
    cascade.m_blend_end_distance = far_distance;
    cascade.m_receiver_world_bounds = bounds_from_points(world_corners);
    cascade.m_light_space_bounds = Bounds3{math::vec3(left, bottom, near_z), math::vec3(right, top, far_z)};
    cascade.m_world_from_light = *world_from_light;
    cascade.m_light_from_world = light_from_world;
    cascade.m_clip_from_light = *clip_from_light;
    cascade.m_clip_from_world = math::mul(*clip_from_light, light_from_world);
    cascade.m_culling_planes = copy_frustum_planes(view_frustum_from_clip_from_world(cascade.m_clip_from_world));
    cascade.m_texel_world_size = std::max(texel_width, texel_height);
    return cascade;
}

} // namespace

// Returns the owned cascade culling planes as a non-owning view.
CullingPlaneSet ShadowCascade::plane_set() const noexcept {
    return CullingPlaneSet{m_culling_planes};
}

// Returns the sun elevation implied by a light-travel direction.
float shadow_sun_elevation_radians(math::Vec3 light_direction) {
    const math::Vec3 normalized_light = normalized_or_throw(light_direction, "Shadow light direction must be finite.");
    const math::Vec3 surface_to_sun = math::mul(normalized_light, -1.0f);
    return std::asin(std::clamp(surface_to_sun.y, -1.0f, 1.0f));
}

// Returns the 0..1 fade factor for the supplied sun elevation.
float shadow_low_sun_visibility(float sun_elevation_radians, const ShadowSettings& settings) {
    validate_shadow_settings(settings);
    if (!std::isfinite(sun_elevation_radians)) {
        throw EngineError("Shadow sun elevation must be finite.");
    }
    if (sun_elevation_radians <= settings.m_low_sun_fade_end_radians) {
        return 0.0f;
    }
    if (sun_elevation_radians >= settings.m_low_sun_fade_start_radians) {
        return 1.0f;
    }
    const float range = settings.m_low_sun_fade_start_radians - settings.m_low_sun_fade_end_radians;
    return (sun_elevation_radians - settings.m_low_sun_fade_end_radians) / range;
}

// Clamps a low sun angle while preserving the horizontal sun azimuth when possible.
math::Vec3 clamp_shadow_light_direction(math::Vec3 light_direction, const ShadowSettings& settings, bool& was_clamped) {
    validate_shadow_settings(settings);
    const math::Vec3 normalized_light = normalized_or_throw(light_direction, "Shadow light direction must be finite.");
    const math::Vec3 surface_to_sun = math::mul(normalized_light, -1.0f);
    const float elevation = std::asin(std::clamp(surface_to_sun.y, -1.0f, 1.0f));
    was_clamped = false;
    if (elevation >= settings.m_min_shadow_sun_elevation_radians) {
        return normalized_light;
    }

    const math::Vec3 horizontal = math::vec3(surface_to_sun.x, 0.0f, surface_to_sun.z);
    std::string error;
    std::optional<math::Vec3> horizontal_direction = math::normalize(horizontal, error);
    if (!horizontal_direction.has_value()) {
        return normalized_light;
    }

    const float sin_elevation = std::sin(settings.m_min_shadow_sun_elevation_radians);
    const float cos_elevation = std::cos(settings.m_min_shadow_sun_elevation_radians);
    const math::Vec3 clamped_surface_to_sun =
        math::vec3(horizontal_direction->x * cos_elevation, sin_elevation, horizontal_direction->z * cos_elevation);
    was_clamped = true;
    return math::mul(normalized_or_throw(clamped_surface_to_sun, "Clamped shadow direction must be finite."), -1.0f);
}

// Builds all CPU-side cascades from a camera snapshot and current sun light direction.
ShadowCascadeSet build_shadow_cascades(
    const CameraProperties& camera, math::Vec3 light_direction, const ShadowSettings& settings) {
    validate_shadow_settings(settings);
    validate_camera_for_shadows(camera, settings);

    const math::Vec3 normalized_light = normalized_or_throw(light_direction, "Shadow light direction must be finite.");
    const float sun_elevation = shadow_sun_elevation_radians(normalized_light);
    const float visibility = shadow_low_sun_visibility(sun_elevation, settings);
    bool low_sun_clamped = false;
    const math::Vec3 effective_light_direction =
        clamp_shadow_light_direction(normalized_light, settings, low_sun_clamped);

    ShadowCascadeSet result;
    result.m_light_direction = normalized_light;
    result.m_effective_light_direction = effective_light_direction;
    result.m_sun_elevation_radians = sun_elevation;
    result.m_effective_intensity = settings.m_enabled ? settings.m_intensity * visibility : 0.0f;
    result.m_low_sun_clamped = low_sun_clamped;

    float near_distance = camera.near_z;
    for (std::size_t index = 0; index < shadow_cascade_count(); ++index) {
        near_distance = cascade_receiver_near_distance(camera, settings, index);
        const float far_distance = settings.m_cascade_end_distances[index];
        if (far_distance <= near_distance) {
            throw EngineError("Shadow cascade intervals must increase beyond the camera near plane.");
        }
        result.m_cascades[index] = build_one_cascade(camera,
            settings,
            effective_light_direction,
            static_cast<std::uint32_t>(index),
            near_distance,
            far_distance);
    }
    return result;
}

} // namespace ofg
