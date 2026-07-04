// Doctest coverage for cascaded sun-shadow CPU settings and cascade math.
//
// The tests pin split distances, low-sun fade/clamp behavior, stable
// orthographic cascade construction, and the culling-plane contract before the
// GPU shadow-map pass consumes these values.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/bounds.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/frustum.hpp"
#include "ofg/render/shadow_cascade.hpp"
#include "ofg/render/shadow_settings.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <limits>
#include <optional>
#include <string>

namespace {

constexpr float _pi = 3.14159265358979323846f;

// Converts degrees to radians for readable shadow-angle tests.
float radians(float degrees) noexcept {
    return degrees * _pi / 180.0f;
}

// Builds a light-travel direction from the surface-to-sun elevation angle.
ofg::math::Vec3 light_direction_from_sun_elevation(float elevation_radians) {
    const ofg::math::Vec3 surface_to_sun =
        ofg::math::vec3(std::cos(elevation_radians), std::sin(elevation_radians), 0.0f);
    return ofg::math::mul(surface_to_sun, -1.0f);
}

// Builds a deterministic camera used by cascade tests.
ofg::CameraProperties make_shadow_test_camera() {
    return ofg::camera_properties_from_look_at(nullptr,
        ofg::math::vec3(0.0f, 2.0f, -6.0f),
        ofg::math::vec3(0.0f, 1.5f, 10.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        radians(60.0f),
        16.0f / 9.0f,
        1.0f,
        80.0f);
}

// Recomputes split frustum corners independently from the shadow implementation.
std::array<ofg::math::Vec3, 8> camera_interval_corners(
    const ofg::CameraProperties& camera, float near_distance, float far_distance) {
    const float half_fov_tangent = std::tan(camera.vertical_fov_radians * 0.5f);
    const float near_half_height = half_fov_tangent * near_distance;
    const float near_half_width = near_half_height * camera.aspect;
    const float far_half_height = half_fov_tangent * far_distance;
    const float far_half_width = far_half_height * camera.aspect;

    const std::array<ofg::math::Vec3, 8> camera_space{{
        ofg::math::vec3(-near_half_width, -near_half_height, near_distance),
        ofg::math::vec3(near_half_width, -near_half_height, near_distance),
        ofg::math::vec3(-near_half_width, near_half_height, near_distance),
        ofg::math::vec3(near_half_width, near_half_height, near_distance),
        ofg::math::vec3(-far_half_width, -far_half_height, far_distance),
        ofg::math::vec3(far_half_width, -far_half_height, far_distance),
        ofg::math::vec3(-far_half_width, far_half_height, far_distance),
        ofg::math::vec3(far_half_width, far_half_height, far_distance),
    }};

    std::array<ofg::math::Vec3, 8> world_space{};
    for (std::size_t index = 0; index < world_space.size(); ++index) {
        world_space[index] = ofg::math::transform_point(camera.world_from_camera, camera_space[index]);
    }
    return world_space;
}

// Builds finite bounds from eight independently reconstructed points.
ofg::Bounds3 test_bounds_from_points(const std::array<ofg::math::Vec3, 8>& points) {
    ofg::math::Vec3 minimum = points.front();
    ofg::math::Vec3 maximum = points.front();
    for (std::size_t index = 1; index < points.size(); ++index) {
        minimum = ofg::math::vec3(std::min(minimum.x, points[index].x),
            std::min(minimum.y, points[index].y),
            std::min(minimum.z, points[index].z));
        maximum = ofg::math::vec3(std::max(maximum.x, points[index].x),
            std::max(maximum.y, points[index].y),
            std::max(maximum.z, points[index].z));
    }
    return ofg::Bounds3{minimum, maximum};
}

// Checks that a point lies inside WebGPU clip-space bounds after perspective divide.
void check_point_inside_clip(ofg::math::Mat4 clip_from_world, ofg::math::Vec3 point) {
    const ofg::math::Vec4 clip = ofg::math::mul(clip_from_world, ofg::math::vec4(point.x, point.y, point.z, 1.0f));
    REQUIRE(clip.w != doctest::Approx(0.0f));
    CHECK(clip.x / clip.w >= doctest::Approx(-1.0f).epsilon(0.001f));
    CHECK(clip.x / clip.w <= doctest::Approx(1.0f).epsilon(0.001f));
    CHECK(clip.y / clip.w >= doctest::Approx(-1.0f).epsilon(0.001f));
    CHECK(clip.y / clip.w <= doctest::Approx(1.0f).epsilon(0.001f));
    CHECK(clip.z / clip.w >= doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(clip.z / clip.w <= doctest::Approx(1.0f).epsilon(0.001f));
}

// Builds a small bounds around the requested world-space point.
ofg::Bounds3 small_bounds_at(ofg::math::Vec3 center) {
    return ofg::Bounds3{ofg::math::sub(center, ofg::math::vec3(0.1f, 0.1f, 0.1f)),
        ofg::math::add(center, ofg::math::vec3(0.1f, 0.1f, 0.1f))};
}

} // namespace

// Verifies defaults are valid and scalar settings reject invalid ranges.
TEST_CASE("shadow settings validate cascade and filter contracts") {
    ofg::ShadowSettings settings;
    CHECK_NOTHROW(ofg::validate_shadow_settings(settings));
    CHECK(ofg::shadow_pcf_sample_count(ofg::ShadowPcfMode::Hard) == 1U);
    CHECK(ofg::shadow_pcf_sample_count(ofg::ShadowPcfMode::FiveTap) == 5U);
    CHECK(ofg::shadow_pcf_sample_count(ofg::ShadowPcfMode::NineTap) == 9U);
    CHECK(std::string(ofg::shadow_pcf_mode_name(ofg::ShadowPcfMode::Hard)) == "hard");
    CHECK(std::string(ofg::shadow_pcf_mode_name(ofg::ShadowPcfMode::FiveTap)) == "five_tap");
    CHECK(std::string(ofg::shadow_pcf_mode_name(ofg::ShadowPcfMode::NineTap)) == "nine_tap");

    settings.m_map_size = 0;
    CHECK_THROWS_WITH_AS(ofg::validate_shadow_settings(settings), doctest::Contains("map size"), ofg::EngineError);
    settings = ofg::ShadowSettings{};
    settings.m_intensity = 1.5f;
    CHECK_THROWS_WITH_AS(ofg::validate_shadow_settings(settings), doctest::Contains("intensity"), ofg::EngineError);
    settings = ofg::ShadowSettings{};
    settings.m_cascade_end_distances[1] = settings.m_cascade_end_distances[0];
    CHECK_THROWS_WITH_AS(ofg::validate_shadow_settings(settings), doctest::Contains("end distances"), ofg::EngineError);
    settings = ofg::ShadowSettings{};
    settings.m_cascade_blend_widths[0] = settings.m_cascade_end_distances[0];
    CHECK_THROWS_WITH_AS(ofg::validate_shadow_settings(settings), doctest::Contains("blend widths"), ofg::EngineError);
    settings = ofg::ShadowSettings{};
    settings.m_low_sun_fade_start_radians = settings.m_low_sun_fade_end_radians;
    CHECK_THROWS_WITH_AS(ofg::validate_shadow_settings(settings), doctest::Contains("low-sun"), ofg::EngineError);
}

// Verifies practical split distances interpolate between uniform and logarithmic partitions.
TEST_CASE("shadow practical split distances blend uniform and logarithmic spacing") {
    const std::array<float, ofg::shadow_cascade_count()> uniform = ofg::practical_split_distances(1.0f, 100.0f, 0.0f);
    CHECK(uniform[0] == doctest::Approx(34.0f));
    CHECK(uniform[1] == doctest::Approx(67.0f));
    CHECK(uniform[2] == doctest::Approx(100.0f));

    const std::array<float, ofg::shadow_cascade_count()> logarithmic =
        ofg::practical_split_distances(1.0f, 100.0f, 1.0f);
    CHECK(logarithmic[0] == doctest::Approx(4.64159f).epsilon(0.0001));
    CHECK(logarithmic[1] == doctest::Approx(21.5443f).epsilon(0.0001));
    CHECK(logarithmic[2] == doctest::Approx(100.0f));

    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::practical_split_distances(1.0f, 100.0f, 1.5f); }()),
        doctest::Contains("lambda"),
        ofg::EngineError);
}

// Verifies low-sun fade and angle clamp preserve finite shadow directions.
TEST_CASE("shadow low-sun fade and clamp avoid horizon-length shadows") {
    ofg::ShadowSettings settings;
    settings.m_intensity = 0.8f;
    settings.m_low_sun_fade_start_radians = radians(10.0f);
    settings.m_low_sun_fade_end_radians = 0.0f;
    settings.m_min_shadow_sun_elevation_radians = radians(5.0f);

    CHECK(ofg::shadow_low_sun_visibility(radians(20.0f), settings) == doctest::Approx(1.0f));
    CHECK(ofg::shadow_low_sun_visibility(radians(5.0f), settings) == doctest::Approx(0.5f));
    CHECK(ofg::shadow_low_sun_visibility(radians(-1.0f), settings) == doctest::Approx(0.0f));

    bool was_clamped = false;
    const ofg::math::Vec3 clamped =
        ofg::clamp_shadow_light_direction(light_direction_from_sun_elevation(radians(1.0f)), settings, was_clamped);
    CHECK(was_clamped);
    const ofg::math::Vec3 clamped_surface_to_sun = ofg::math::mul(clamped, -1.0f);
    CHECK(clamped_surface_to_sun.y == doctest::Approx(std::sin(radians(5.0f))).epsilon(0.0001));
    CHECK(clamped_surface_to_sun.x > 0.0f);

    was_clamped = false;
    const ofg::math::Vec3 high_sun =
        ofg::clamp_shadow_light_direction(light_direction_from_sun_elevation(radians(45.0f)), settings, was_clamped);
    CHECK_FALSE(was_clamped);
    CHECK(ofg::shadow_sun_elevation_radians(high_sun) == doctest::Approx(radians(45.0f)).epsilon(0.0001));
}

// Verifies cascade construction creates finite intervals, matrices, texel snapping, and culling planes.
TEST_CASE("shadow cascades build stable finite culling volumes from camera and sun") {
    const ofg::CameraProperties camera = make_shadow_test_camera();
    ofg::ShadowSettings settings;
    std::string error;
    const std::optional<ofg::math::Vec3> normalized_light =
        ofg::math::normalize(ofg::math::vec3(0.35f, -1.0f, 0.2f), error);
    REQUIRE(normalized_light.has_value());
    const ofg::math::Vec3 light_direction = *normalized_light;

    const ofg::ShadowCascadeSet cascades = ofg::build_shadow_cascades(camera, light_direction, settings);
    CHECK(cascades.m_effective_intensity == doctest::Approx(settings.m_intensity));
    CHECK_FALSE(cascades.m_low_sun_clamped);

    for (std::size_t index = 0; index < ofg::shadow_cascade_count(); ++index) {
        const ofg::ShadowCascade& cascade = cascades.m_cascades[index];
        const float expected_near =
            index == 0U
                ? camera.near_z
                : std::max(camera.near_z,
                      settings.m_cascade_end_distances[index - 1U] - settings.m_cascade_blend_widths[index - 1U]);
        CHECK(cascade.m_index == index);
        CHECK(cascade.m_near_distance == doctest::Approx(expected_near));
        CHECK(cascade.m_far_distance == doctest::Approx(settings.m_cascade_end_distances[index]));
        CHECK(cascade.m_blend_start_distance ==
              doctest::Approx(settings.m_cascade_end_distances[index] - settings.m_cascade_blend_widths[index]));
        CHECK(cascade.m_blend_end_distance == doctest::Approx(settings.m_cascade_end_distances[index]));
        CHECK(cascade.m_texel_world_size > 0.0f);
        CHECK(ofg::bounds_is_valid(cascade.m_receiver_world_bounds));
        CHECK(ofg::bounds_is_valid(cascade.m_light_space_bounds));
        CHECK(cascade.plane_set().m_planes.size() == 6);

        const float snapped_center_x =
            (cascade.m_light_space_bounds.m_min.x + cascade.m_light_space_bounds.m_max.x) * 0.5f;
        const float snapped_center_y =
            (cascade.m_light_space_bounds.m_min.y + cascade.m_light_space_bounds.m_max.y) * 0.5f;
        const float texel_width = (cascade.m_light_space_bounds.m_max.x - cascade.m_light_space_bounds.m_min.x) /
                                  static_cast<float>(settings.m_map_size);
        const float texel_height = (cascade.m_light_space_bounds.m_max.y - cascade.m_light_space_bounds.m_min.y) /
                                   static_cast<float>(settings.m_map_size);
        CHECK(snapped_center_x / texel_width ==
              doctest::Approx(std::round(snapped_center_x / texel_width)).epsilon(0.0001));
        CHECK(snapped_center_y / texel_height ==
              doctest::Approx(std::round(snapped_center_y / texel_height)).epsilon(0.0001));

        const std::array<ofg::math::Vec3, 8> corners =
            camera_interval_corners(camera, cascade.m_near_distance, cascade.m_far_distance);
        std::array<ofg::math::Vec3, 8> light_corners{};
        for (std::size_t corner_index = 0; corner_index < corners.size(); ++corner_index) {
            light_corners[corner_index] = ofg::math::transform_point(cascade.m_light_from_world, corners[corner_index]);
        }
        const ofg::Bounds3 tight_light_bounds = test_bounds_from_points(light_corners);
        const float tight_width = tight_light_bounds.m_max.x - tight_light_bounds.m_min.x;
        const float tight_height = tight_light_bounds.m_max.y - tight_light_bounds.m_min.y;
        const float cascade_width = cascade.m_light_space_bounds.m_max.x - cascade.m_light_space_bounds.m_min.x;
        const float cascade_height = cascade.m_light_space_bounds.m_max.y - cascade.m_light_space_bounds.m_min.y;
        CHECK(cascade_width >= tight_width);
        CHECK(cascade_height >= tight_height);
        CHECK(cascade_width < tight_width * 1.02f);
        CHECK(cascade_height < tight_height * 1.02f);
        CHECK(cascade.m_light_space_bounds.m_min.z <=
              doctest::Approx(tight_light_bounds.m_min.z - settings.m_caster_depth_padding).epsilon(0.001f));
        CHECK(cascade.m_light_space_bounds.m_max.z >= tight_light_bounds.m_max.z);
        for (ofg::math::Vec3 corner : corners) {
            check_point_inside_clip(cascade.m_clip_from_world, corner);
        }
        CHECK(ofg::intersects_culling_planes(cascade.m_receiver_world_bounds, cascade.plane_set()));
        CHECK_FALSE(ofg::intersects_culling_planes(
            small_bounds_at(ofg::math::vec3(10000.0f, 10000.0f, 10000.0f)), cascade.plane_set()));
    }
}

// Verifies disabled shadows still build finite cascades but report zero effective intensity.
TEST_CASE("shadow cascades report zero intensity when disabled or below horizon") {
    const ofg::CameraProperties camera = make_shadow_test_camera();
    ofg::ShadowSettings settings;
    settings.m_enabled = false;
    ofg::ShadowCascadeSet cascades =
        ofg::build_shadow_cascades(camera, light_direction_from_sun_elevation(radians(45.0f)), settings);
    CHECK(cascades.m_effective_intensity == doctest::Approx(0.0f));

    settings.m_enabled = true;
    cascades = ofg::build_shadow_cascades(camera, light_direction_from_sun_elevation(radians(-2.0f)), settings);
    CHECK(cascades.m_effective_intensity == doctest::Approx(0.0f));
    CHECK(cascades.m_low_sun_clamped);
}

// Verifies cascade construction rejects invalid light and camera/settings combinations.
TEST_CASE("shadow cascades reject invalid build inputs") {
    const ofg::CameraProperties camera = make_shadow_test_camera();
    const ofg::ShadowSettings settings;
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::build_shadow_cascades(camera, ofg::math::vec3(0.0f, 0.0f, 0.0f), settings); }()),
        doctest::Contains("direction"),
        ofg::EngineError);

    ofg::ShadowSettings too_far = settings;
    too_far.m_cascade_end_distances[2] = 100.0f;
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::build_shadow_cascades(camera, light_direction_from_sun_elevation(radians(45.0f)), too_far);
    }()),
        doctest::Contains("far plane"),
        ofg::EngineError);
}
