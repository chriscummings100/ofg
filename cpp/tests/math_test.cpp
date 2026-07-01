// Doctest coverage for the minimal OFG renderer math layer.
//
// These tests pin shader-style vector helpers, column-major Mat4 behavior, and
// camera/projection helpers before renderer code depends on them.
#include "doctest.h"

#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

// Verifies vector construction, arithmetic, and normalization.
TEST_CASE("math vectors support shader-style helpers") {
    const ofg::math::Vec3 a = ofg::math::vec3(1.0f, 0.0f, 0.0f);
    const ofg::math::Vec3 b = ofg::math::vec3(0.0f, 1.0f, 0.0f);

    CHECK(ofg::math::dot(a, b) == doctest::Approx(0.0f));
    const ofg::math::Vec3 cross = ofg::math::cross(a, b);
    CHECK(cross.x == doctest::Approx(0.0f));
    CHECK(cross.y == doctest::Approx(0.0f));
    CHECK(cross.z == doctest::Approx(1.0f));

    std::string error;
    const std::optional<ofg::math::Vec3> normalized = ofg::math::normalize(ofg::math::vec3(0.0f, 3.0f, 4.0f), error);
    REQUIRE(normalized.has_value());
    CHECK(normalized->x == doctest::Approx(0.0f));
    CHECK(normalized->y == doctest::Approx(0.6f));
    CHECK(normalized->z == doctest::Approx(0.8f));
    CHECK(error.empty());

    CHECK(ofg::math::normalize(ofg::math::vec3(0.0f, 0.0f, 0.0f), error).has_value() == false);
    CHECK(error.find("Cannot normalize") != std::string::npos);
}

// Verifies matrices store and pack column-major data for WGSL uniforms.
TEST_CASE("math matrices pack in WGSL column-major order") {
    const ofg::math::Mat4 translation = ofg::math::mat4_translation(ofg::math::vec3(2.0f, 3.0f, 4.0f));
    const std::array<float, 16> packed = ofg::math::pack_mat4(translation);

    CHECK(packed[12] == doctest::Approx(2.0f));
    CHECK(packed[13] == doctest::Approx(3.0f));
    CHECK(packed[14] == doctest::Approx(4.0f));
    CHECK(packed[15] == doctest::Approx(1.0f));

    const ofg::math::Vec4 transformed = ofg::math::mul(translation, ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK(transformed.x == doctest::Approx(3.0f));
    CHECK(transformed.y == doctest::Approx(4.0f));
    CHECK(transformed.z == doctest::Approx(5.0f));
    CHECK(transformed.w == doctest::Approx(1.0f));
}

// Verifies transform composition order stays deterministic.
TEST_CASE("math transforms compose as column-vector matrices") {
    const ofg::math::Mat4 scale = ofg::math::mat4_scale(ofg::math::vec3(2.0f, 3.0f, 4.0f));
    const ofg::math::Mat4 translate = ofg::math::mat4_translation(ofg::math::vec3(1.0f, 2.0f, 3.0f));
    const ofg::math::Mat4 combined = ofg::math::mul(translate, scale);

    const ofg::math::Vec4 transformed = ofg::math::mul(combined, ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK(transformed.x == doctest::Approx(3.0f));
    CHECK(transformed.y == doctest::Approx(5.0f));
    CHECK(transformed.z == doctest::Approx(7.0f));
    CHECK(transformed.w == doctest::Approx(1.0f));
}

// Verifies camera helpers build WebGPU-friendly right-handed matrices.
TEST_CASE("math camera helpers validate right-handed projection and view") {
    std::string error;
    const std::optional<ofg::math::Mat4> projection =
        ofg::math::perspective_rh(1.0f, 16.0f / 9.0f, 0.1f, 100.0f, error);
    REQUIRE(projection.has_value());

    const ofg::math::Vec4 near_point = ofg::math::mul(*projection, ofg::math::vec4(0.0f, 0.0f, -0.1f, 1.0f));
    CHECK(near_point.z / near_point.w == doctest::Approx(0.0f));

    const std::optional<ofg::math::Mat4> view = ofg::math::look_at_rh(
        ofg::math::vec3(0.0f, 0.0f, 5.0f), ofg::math::vec3(0.0f, 0.0f, 0.0f), ofg::math::vec3(0.0f, 1.0f, 0.0f), error);
    REQUIRE(view.has_value());
    const ofg::math::Vec4 origin_view = ofg::math::mul(*view, ofg::math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(origin_view.z == doctest::Approx(-5.0f));

    CHECK(ofg::math::perspective_rh(1.0f, 0.0f, 0.1f, 100.0f, error).has_value() == false);
    CHECK(error.find("Perspective") != std::string::npos);
    CHECK(ofg::math::look_at_rh(ofg::math::vec3(0.0f, 0.0f, 0.0f),
              ofg::math::vec3(0.0f, 0.0f, 0.0f),
              ofg::math::vec3(0.0f, 1.0f, 0.0f),
              error)
              .has_value() == false);
    CHECK(error.find("Look-at") != std::string::npos);
}

// Verifies const indexing, raw data, and rotation helpers.
TEST_CASE("math exposes shader-like const access and rotation") {
    const ofg::math::Mat4 identity = ofg::math::mat4_identity();
    const float* identity_data = identity.data();
    CHECK(identity[0][0] == doctest::Approx(1.0f));
    CHECK(identity_data[0] == doctest::Approx(1.0f));
    CHECK(identity_data[15] == doctest::Approx(1.0f));

    const ofg::math::Vec4 color = ofg::math::vec4(0.25f, 0.5f, 0.75f, 1.0f);
    const std::array<float, 4> packed_color = ofg::math::pack_vec4(color);
    CHECK(color[2] == doctest::Approx(0.75f));
    CHECK(packed_color[3] == doctest::Approx(1.0f));

    const ofg::math::Mat4 rotation = ofg::math::mat4_rotation_y(1.57079632679f);
    const ofg::math::Vec4 rotated = ofg::math::mul(rotation, ofg::math::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(rotated.x == doctest::Approx(0.0f).epsilon(0.0001));
    CHECK(rotated.z == doctest::Approx(-1.0f));
}

// Verifies quaternion helpers create right-handed scene rotations.
TEST_CASE("math quaternions support scene rotations") {
    std::string error;
    const std::optional<ofg::math::Quat> rotation =
        ofg::math::quat_from_axis_angle(ofg::math::vec3(0.0f, 1.0f, 0.0f), 1.57079632679f, error);
    REQUIRE(rotation.has_value());
    CHECK(error.empty());

    const ofg::math::Mat4 matrix = ofg::math::mat4_from_quat(*rotation);
    const ofg::math::Vec4 rotated = ofg::math::mul(matrix, ofg::math::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(rotated.x == doctest::Approx(0.0f).epsilon(0.0001));
    CHECK(rotated.z == doctest::Approx(-1.0f));

    const std::optional<ofg::math::Quat> normalized =
        ofg::math::normalize(ofg::math::Quat{0.0f, 0.0f, 0.0f, 2.0f}, error);
    REQUIRE(normalized.has_value());
    CHECK(normalized->w == doctest::Approx(1.0f));

    CHECK(ofg::math::quat_from_axis_angle(ofg::math::vec3(0.0f, 0.0f, 0.0f), 1.0f, error).has_value() == false);
    CHECK(error.find("axis") != std::string::npos);
    CHECK(ofg::math::quat_from_axis_angle(
              ofg::math::vec3(0.0f, 1.0f, 0.0f), std::numeric_limits<float>::infinity(), error)
              .has_value() == false);
    CHECK(error.find("angle") != std::string::npos);
    CHECK(ofg::math::normalize(ofg::math::Quat{std::nanf(""), 0.0f, 0.0f, 1.0f}, error).has_value() == false);
    CHECK(error.find("non-finite") != std::string::npos);
    CHECK(ofg::math::normalize(ofg::math::Quat{0.0f, 0.0f, 0.0f, 0.0f}, error).has_value() == false);
    CHECK(error.find("zero-length") != std::string::npos);
}

// Verifies look-at quaternions produce the same camera basis as the view helpers.
TEST_CASE("math quaternions support right-handed camera look-at rotations") {
    std::string error;
    const ofg::math::Vec3 eye = ofg::math::vec3(6.2f, 4.4f, 7.6f);
    const ofg::math::Vec3 target = ofg::math::vec3(0.0f, 0.55f, 0.0f);
    const ofg::math::Vec3 up = ofg::math::vec3(0.0f, 1.0f, 0.0f);

    const std::optional<ofg::math::Quat> rotation = ofg::math::quat_look_at_rh(eye, target, up, error);
    REQUIRE(rotation.has_value());
    CHECK(error.empty());

    const ofg::math::Mat4 matrix = ofg::math::mat4_from_quat(*rotation);
    const std::optional<ofg::math::Vec3> expected_forward = ofg::math::normalize(ofg::math::sub(target, eye), error);
    REQUIRE(expected_forward.has_value());
    const std::optional<ofg::math::Vec3> expected_right =
        ofg::math::normalize(ofg::math::cross(*expected_forward, up), error);
    REQUIRE(expected_right.has_value());
    const ofg::math::Vec3 expected_up = ofg::math::cross(*expected_right, *expected_forward);

    const ofg::math::Vec4 forward = ofg::math::mul(matrix, ofg::math::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    CHECK(forward.x == doctest::Approx(expected_forward->x).epsilon(0.0001));
    CHECK(forward.y == doctest::Approx(expected_forward->y).epsilon(0.0001));
    CHECK(forward.z == doctest::Approx(expected_forward->z).epsilon(0.0001));
    CHECK(matrix[0].x == doctest::Approx(expected_right->x).epsilon(0.0001));
    CHECK(matrix[0].y == doctest::Approx(expected_right->y).epsilon(0.0001));
    CHECK(matrix[0].z == doctest::Approx(expected_right->z).epsilon(0.0001));
    CHECK(matrix[1].x == doctest::Approx(expected_up.x).epsilon(0.0001));
    CHECK(matrix[1].y == doctest::Approx(expected_up.y).epsilon(0.0001));
    CHECK(matrix[1].z == doctest::Approx(expected_up.z).epsilon(0.0001));

    CHECK(ofg::math::quat_look_at_rh(eye, eye, up, error).has_value() == false);
    CHECK(error.find("distinct") != std::string::npos);
    CHECK(ofg::math::quat_look_at_rh(eye, target, ofg::math::sub(target, eye), error).has_value() == false);
    CHECK(error.find("parallel") != std::string::npos);
}

// Verifies look-at quaternion conversion covers the 180-degree matrix branches.
TEST_CASE("math look-at quaternions support half-turn camera rotations") {
    std::string error;
    const ofg::math::Vec3 eye = ofg::math::vec3(0.0f, 0.0f, 0.0f);

    const std::optional<ofg::math::Quat> turn_x =
        ofg::math::quat_look_at_rh(eye, ofg::math::vec3(0.0f, 0.0f, 1.0f), ofg::math::vec3(0.0f, -1.0f, 0.0f), error);
    REQUIRE(turn_x.has_value());
    const ofg::math::Mat4 matrix_x = ofg::math::mat4_from_quat(*turn_x);
    CHECK(matrix_x[0].x == doctest::Approx(1.0f));
    CHECK(matrix_x[1].y == doctest::Approx(-1.0f));
    CHECK(matrix_x[2].z == doctest::Approx(-1.0f));

    const std::optional<ofg::math::Quat> turn_y =
        ofg::math::quat_look_at_rh(eye, ofg::math::vec3(0.0f, 0.0f, 1.0f), ofg::math::vec3(0.0f, 1.0f, 0.0f), error);
    REQUIRE(turn_y.has_value());
    const ofg::math::Mat4 matrix_y = ofg::math::mat4_from_quat(*turn_y);
    CHECK(matrix_y[0].x == doctest::Approx(-1.0f));
    CHECK(matrix_y[1].y == doctest::Approx(1.0f));
    CHECK(matrix_y[2].z == doctest::Approx(-1.0f));

    const std::optional<ofg::math::Quat> turn_z =
        ofg::math::quat_look_at_rh(eye, ofg::math::vec3(0.0f, 0.0f, -1.0f), ofg::math::vec3(0.0f, -1.0f, 0.0f), error);
    REQUIRE(turn_z.has_value());
    const ofg::math::Mat4 matrix_z = ofg::math::mat4_from_quat(*turn_z);
    CHECK(matrix_z[0].x == doctest::Approx(-1.0f));
    CHECK(matrix_z[1].y == doctest::Approx(-1.0f));
    CHECK(matrix_z[2].z == doctest::Approx(1.0f));
}

// Verifies invalid camera parameters produce useful errors.
TEST_CASE("math camera helpers reject non-finite and parallel inputs") {
    std::string error;
    CHECK(ofg::math::perspective_rh(std::numeric_limits<float>::infinity(), 1.0f, 0.1f, 100.0f, error).has_value() ==
          false);
    CHECK(error.find("finite") != std::string::npos);

    CHECK(ofg::math::look_at_rh(ofg::math::vec3(0.0f, 0.0f, 0.0f),
              ofg::math::vec3(0.0f, 0.0f, -1.0f),
              ofg::math::vec3(0.0f, 0.0f, -2.0f),
              error)
              .has_value() == false);
    CHECK(error.find("parallel") != std::string::npos);
}
