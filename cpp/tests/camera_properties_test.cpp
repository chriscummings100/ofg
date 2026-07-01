// Doctest coverage for renderer-facing camera property snapshots.
//
// These tests pin the look-at adapter used to compare camera snapshots against
// the previous packed projection-view matrix path.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/camera_properties.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace {

constexpr float _pi = 3.14159265358979323846f;

// Checks two Mat4 values component-wise with a tight floating-point tolerance.
void check_mat4_close(ofg::math::Mat4 actual, ofg::math::Mat4 expected) {
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            CHECK(actual[column][row] == doctest::Approx(expected[column][row]).epsilon(0.0001));
        }
    }
}

} // namespace

// Verifies CameraProperties preserves the existing projection*view math.
TEST_CASE("camera properties from look-at match legacy view projection") {
    const ofg::math::Vec3 eye = ofg::math::vec3(6.2f, 4.4f, 7.6f);
    const ofg::math::Vec3 target = ofg::math::vec3(0.0f, 0.55f, 0.0f);
    const ofg::math::Vec3 up = ofg::math::vec3(0.0f, 1.0f, 0.0f);
    const float fov = 55.0f * _pi / 180.0f;
    const float aspect = 16.0f / 9.0f;
    const float near_z = 0.1f;
    const float far_z = 80.0f;

    const ofg::Camera* source_camera = reinterpret_cast<const ofg::Camera*>(static_cast<std::uintptr_t>(0x1234));
    const ofg::CameraProperties properties =
        ofg::camera_properties_from_look_at(source_camera, eye, target, up, fov, aspect, near_z, far_z);

    std::string error;
    const std::optional<ofg::math::Mat4> view = ofg::math::look_at_rh(eye, target, up, error);
    REQUIRE(view.has_value());
    const std::optional<ofg::math::Mat4> projection = ofg::math::perspective_rh(fov, aspect, near_z, far_z, error);
    REQUIRE(projection.has_value());

    CHECK(properties.camera == source_camera);
    CHECK(properties.vertical_fov_radians == doctest::Approx(fov));
    CHECK(properties.aspect == doctest::Approx(aspect));
    CHECK(properties.near_z == doctest::Approx(near_z));
    CHECK(properties.far_z == doctest::Approx(far_z));
    check_mat4_close(properties.camera_from_world, *view);
    check_mat4_close(properties.clip_from_camera, *projection);
    check_mat4_close(properties.clip_from_world, ofg::math::mul(*projection, *view));

    const ofg::math::Vec4 camera_origin =
        ofg::math::mul(properties.world_from_camera, ofg::math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(camera_origin.x == doctest::Approx(eye.x));
    CHECK(camera_origin.y == doctest::Approx(eye.y));
    CHECK(camera_origin.z == doctest::Approx(eye.z));

    const ofg::math::Vec4 camera_forward =
        ofg::math::mul(properties.world_from_camera, ofg::math::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    const std::optional<ofg::math::Vec3> expected_forward = ofg::math::normalize(ofg::math::sub(target, eye), error);
    REQUIRE(expected_forward.has_value());
    CHECK(camera_forward.x == doctest::Approx(expected_forward->x));
    CHECK(camera_forward.y == doctest::Approx(expected_forward->y));
    CHECK(camera_forward.z == doctest::Approx(expected_forward->z));
}

// Verifies invalid look-at and projection inputs fail with useful errors.
TEST_CASE("camera properties reject invalid look-at and projection inputs") {
    const ofg::math::Vec3 eye = ofg::math::vec3(0.0f, 0.0f, 0.0f);
    const ofg::math::Vec3 up = ofg::math::vec3(0.0f, 1.0f, 0.0f);

    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::camera_properties_from_look_at(nullptr, eye, eye, up, 1.0f, 1.0f, 0.1f, 10.0f); }()),
        doctest::Contains("distinct"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::camera_properties_from_look_at(nullptr,
            eye,
            ofg::math::vec3(0.0f, 0.0f, -1.0f),
            ofg::math::vec3(0.0f, 0.0f, -2.0f),
            1.0f,
            1.0f,
            0.1f,
            10.0f);
    }()),
        doctest::Contains("parallel"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::camera_properties_from_look_at(
            nullptr, eye, ofg::math::vec3(0.0f, 0.0f, -1.0f), up, 1.0f, 0.0f, 0.1f, 10.0f);
    }()),
        doctest::Contains("Perspective"),
        ofg::EngineError);
}
