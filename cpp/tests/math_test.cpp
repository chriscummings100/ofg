// Doctest coverage for the minimal OFG renderer math layer.
//
// These tests pin shader-style vector helpers, column-major Mat4 behavior, and
// camera/projection helpers before renderer code depends on them.
#include "doctest.h"

#include "ofg/math/mat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"

#include <array>
#include <limits>
#include <optional>
#include <string>

// Verifies vector construction, arithmetic, and normalization.
TEST_CASE("math vectors support shader-style helpers") {
    const ofg::math::Vec3 a = ofg::math::vec3(1.0F, 0.0F, 0.0F);
    const ofg::math::Vec3 b = ofg::math::vec3(0.0F, 1.0F, 0.0F);

    CHECK(ofg::math::dot(a, b) == doctest::Approx(0.0F));
    const ofg::math::Vec3 cross = ofg::math::cross(a, b);
    CHECK(cross.x == doctest::Approx(0.0F));
    CHECK(cross.y == doctest::Approx(0.0F));
    CHECK(cross.z == doctest::Approx(1.0F));

    std::string error;
    const std::optional<ofg::math::Vec3> normalized = ofg::math::normalize(ofg::math::vec3(0.0F, 3.0F, 4.0F), error);
    REQUIRE(normalized.has_value());
    CHECK(normalized->x == doctest::Approx(0.0F));
    CHECK(normalized->y == doctest::Approx(0.6F));
    CHECK(normalized->z == doctest::Approx(0.8F));
    CHECK(error.empty());

    CHECK(ofg::math::normalize(ofg::math::vec3(0.0F, 0.0F, 0.0F), error).has_value() == false);
    CHECK(error.find("Cannot normalize") != std::string::npos);
}

// Verifies matrices store and pack column-major data for WGSL uniforms.
TEST_CASE("math matrices pack in WGSL column-major order") {
    const ofg::math::Mat4 translation = ofg::math::mat4_translation(ofg::math::vec3(2.0F, 3.0F, 4.0F));
    const std::array<float, 16> packed = ofg::math::pack_mat4(translation);

    CHECK(packed[12] == doctest::Approx(2.0F));
    CHECK(packed[13] == doctest::Approx(3.0F));
    CHECK(packed[14] == doctest::Approx(4.0F));
    CHECK(packed[15] == doctest::Approx(1.0F));

    const ofg::math::Vec4 transformed = ofg::math::mul(translation, ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    CHECK(transformed.x == doctest::Approx(3.0F));
    CHECK(transformed.y == doctest::Approx(4.0F));
    CHECK(transformed.z == doctest::Approx(5.0F));
    CHECK(transformed.w == doctest::Approx(1.0F));
}

// Verifies transform composition order stays deterministic.
TEST_CASE("math transforms compose as column-vector matrices") {
    const ofg::math::Mat4 scale = ofg::math::mat4_scale(ofg::math::vec3(2.0F, 3.0F, 4.0F));
    const ofg::math::Mat4 translate = ofg::math::mat4_translation(ofg::math::vec3(1.0F, 2.0F, 3.0F));
    const ofg::math::Mat4 combined = ofg::math::mul(translate, scale);

    const ofg::math::Vec4 transformed = ofg::math::mul(combined, ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    CHECK(transformed.x == doctest::Approx(3.0F));
    CHECK(transformed.y == doctest::Approx(5.0F));
    CHECK(transformed.z == doctest::Approx(7.0F));
    CHECK(transformed.w == doctest::Approx(1.0F));
}

// Verifies camera helpers build WebGPU-friendly right-handed matrices.
TEST_CASE("math camera helpers validate right-handed projection and view") {
    std::string error;
    const std::optional<ofg::math::Mat4> projection =
        ofg::math::perspective_rh(1.0F, 16.0F / 9.0F, 0.1F, 100.0F, error);
    REQUIRE(projection.has_value());

    const ofg::math::Vec4 near_point = ofg::math::mul(*projection, ofg::math::vec4(0.0F, 0.0F, -0.1F, 1.0F));
    CHECK(near_point.z / near_point.w == doctest::Approx(0.0F));

    const std::optional<ofg::math::Mat4> view = ofg::math::look_at_rh(
        ofg::math::vec3(0.0F, 0.0F, 5.0F), ofg::math::vec3(0.0F, 0.0F, 0.0F), ofg::math::vec3(0.0F, 1.0F, 0.0F), error);
    REQUIRE(view.has_value());
    const ofg::math::Vec4 origin_view = ofg::math::mul(*view, ofg::math::vec4(0.0F, 0.0F, 0.0F, 1.0F));
    CHECK(origin_view.z == doctest::Approx(-5.0F));

    CHECK(ofg::math::perspective_rh(1.0F, 0.0F, 0.1F, 100.0F, error).has_value() == false);
    CHECK(error.find("Perspective") != std::string::npos);
    CHECK(ofg::math::look_at_rh(ofg::math::vec3(0.0F, 0.0F, 0.0F),
              ofg::math::vec3(0.0F, 0.0F, 0.0F),
              ofg::math::vec3(0.0F, 1.0F, 0.0F),
              error)
              .has_value() == false);
    CHECK(error.find("Look-at") != std::string::npos);
}

// Verifies const indexing, raw data, and rotation helpers.
TEST_CASE("math exposes shader-like const access and rotation") {
    const ofg::math::Mat4 identity = ofg::math::mat4_identity();
    const float* identity_data = identity.data();
    CHECK(identity[0][0] == doctest::Approx(1.0F));
    CHECK(identity_data[0] == doctest::Approx(1.0F));
    CHECK(identity_data[15] == doctest::Approx(1.0F));

    const ofg::math::Vec4 color = ofg::math::vec4(0.25F, 0.5F, 0.75F, 1.0F);
    const std::array<float, 4> packed_color = ofg::math::pack_vec4(color);
    CHECK(color[2] == doctest::Approx(0.75F));
    CHECK(packed_color[3] == doctest::Approx(1.0F));

    const ofg::math::Mat4 rotation = ofg::math::mat4_rotation_y(1.57079632679F);
    const ofg::math::Vec4 rotated = ofg::math::mul(rotation, ofg::math::vec4(1.0F, 0.0F, 0.0F, 1.0F));
    CHECK(rotated.x == doctest::Approx(0.0F).epsilon(0.0001));
    CHECK(rotated.z == doctest::Approx(-1.0F));
}

// Verifies invalid camera parameters produce useful errors.
TEST_CASE("math camera helpers reject non-finite and parallel inputs") {
    std::string error;
    CHECK(ofg::math::perspective_rh(std::numeric_limits<float>::infinity(), 1.0F, 0.1F, 100.0F, error).has_value() ==
          false);
    CHECK(error.find("finite") != std::string::npos);

    CHECK(ofg::math::look_at_rh(ofg::math::vec3(0.0F, 0.0F, 0.0F),
              ofg::math::vec3(0.0F, 0.0F, -1.0F),
              ofg::math::vec3(0.0F, 0.0F, -2.0F),
              error)
              .has_value() == false);
    CHECK(error.find("parallel") != std::string::npos);
}
