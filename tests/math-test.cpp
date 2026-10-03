// Doctest coverage for the minimal OFG renderer math layer.
//
// These tests pin shader-style vector helpers, column-major Mat4 behavior, and
// camera/projection helpers before renderer code depends on them.
#include "doctest.h"

#include "math/mat.h"
#include "math/quat.h"
#include "math/transform.h"
#include "math/vec.h"

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

// Verifies vector construction, arithmetic, and normalization.
TEST_CASE("math vectors support shader-style helpers")
{
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
TEST_CASE("math matrices pack in WGSL column-major order")
{
    const ofg::math::Mat4 translation = ofg::math::mat4Translation(ofg::math::vec3(2.0f, 3.0f, 4.0f));
    const std::array<float, 16> packed = ofg::math::packMat4(translation);

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
TEST_CASE("math transforms compose as column-vector matrices")
{
    const ofg::math::Mat4 scale = ofg::math::mat4Scale(ofg::math::vec3(2.0f, 3.0f, 4.0f));
    const ofg::math::Mat4 translate = ofg::math::mat4Translation(ofg::math::vec3(1.0f, 2.0f, 3.0f));
    const ofg::math::Mat4 combined = ofg::math::mul(translate, scale);

    const ofg::math::Vec4 transformed = ofg::math::mul(combined, ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    CHECK(transformed.x == doctest::Approx(3.0f));
    CHECK(transformed.y == doctest::Approx(5.0f));
    CHECK(transformed.z == doctest::Approx(7.0f));
    CHECK(transformed.w == doctest::Approx(1.0f));
}

// Verifies shared transform helpers cover points, directions, and affine inverse.
TEST_CASE("math transforms points directions and affine inverses")
{
    const ofg::math::Mat4 translation = ofg::math::mat4Translation(ofg::math::vec3(2.0f, 3.0f, 4.0f));
    const ofg::math::Mat4 scale = ofg::math::mat4Scale(ofg::math::vec3(2.0f, 3.0f, 4.0f));
    const ofg::math::Mat4 transform = ofg::math::mul(translation, scale);

    const ofg::math::Vec3 point = ofg::math::transformPoint(transform, ofg::math::vec3(1.0f, 1.0f, 1.0f));
    CHECK(point.x == doctest::Approx(4.0f));
    CHECK(point.y == doctest::Approx(6.0f));
    CHECK(point.z == doctest::Approx(8.0f));

    const ofg::math::Vec3 direction = ofg::math::transformDirection(transform, ofg::math::vec3(1.0f, 1.0f, 1.0f));
    CHECK(direction.x == doctest::Approx(2.0f));
    CHECK(direction.y == doctest::Approx(3.0f));
    CHECK(direction.z == doctest::Approx(4.0f));

    std::string error;
    const std::optional<ofg::math::Mat4> inverse = ofg::math::inverseAffine(transform, error);
    REQUIRE(inverse.has_value());
    const ofg::math::Vec3 restored = ofg::math::transformPoint(*inverse, point);
    CHECK(restored.x == doctest::Approx(1.0f));
    CHECK(restored.y == doctest::Approx(1.0f));
    CHECK(restored.z == doctest::Approx(1.0f));

    CHECK(
        ofg::math::inverseAffine(ofg::math::mat4Scale(ofg::math::vec3(0.0f, 1.0f, 1.0f)), error).has_value() == false
    );
    CHECK(error.find("invertible") != std::string::npos);
}

// Verifies camera helpers build WebGPU-friendly left-handed matrices.
TEST_CASE("math camera helpers validate projection and view matrices")
{
    std::string error;
    const std::optional<ofg::math::Mat4> projection = ofg::math::perspectiveLh(1.0f, 16.0f / 9.0f, 0.1f, 100.0f, error);
    REQUIRE(projection.has_value());
    const ofg::math::Vec4 nearPoint = ofg::math::mul(*projection, ofg::math::vec4(0.0f, 0.0f, 0.1f, 1.0f));
    CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0f));

    const std::optional<ofg::math::Mat4> view = ofg::math::lookAtLh(
        ofg::math::vec3(0.0f, 0.0f, -5.0f),
        ofg::math::vec3(0.0f, 0.0f, 0.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        error
    );
    REQUIRE(view.has_value());
    const ofg::math::Vec4 originView = ofg::math::mul(*view, ofg::math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(originView.z == doctest::Approx(5.0f));

    CHECK(ofg::math::perspectiveLh(1.0f, 0.0f, 0.1f, 100.0f, error).has_value() == false);
    CHECK(error.find("Perspective") != std::string::npos);
    CHECK(
        ofg::math::lookAtLh(
            ofg::math::vec3(0.0f, 0.0f, 0.0f),
            ofg::math::vec3(0.0f, 0.0f, 0.0f),
            ofg::math::vec3(0.0f, 1.0f, 0.0f),
            error
        )
            .has_value() == false
    );
    CHECK(error.find("Look-at") != std::string::npos);
}

// Verifies orthographic projection maps an arbitrary light-space box to WebGPU clip space.
TEST_CASE("math orthographic projection maps to webgpu clip depth")
{
    std::string error;
    const std::optional<ofg::math::Mat4> projection =
        ofg::math::orthographicLh(-2.0f, 6.0f, -4.0f, 8.0f, -10.0f, 30.0f, error);
    REQUIRE(projection.has_value());

    const ofg::math::Vec4 leftBottomNear = ofg::math::mul(*projection, ofg::math::vec4(-2.0f, -4.0f, -10.0f, 1.0f));
    CHECK(leftBottomNear.x == doctest::Approx(-1.0f));
    CHECK(leftBottomNear.y == doctest::Approx(-1.0f));
    CHECK(leftBottomNear.z == doctest::Approx(0.0f));
    CHECK(leftBottomNear.w == doctest::Approx(1.0f));

    const ofg::math::Vec4 rightTopFar = ofg::math::mul(*projection, ofg::math::vec4(6.0f, 8.0f, 30.0f, 1.0f));
    CHECK(rightTopFar.x == doctest::Approx(1.0f));
    CHECK(rightTopFar.y == doctest::Approx(1.0f));
    CHECK(rightTopFar.z == doctest::Approx(1.0f));
    CHECK(rightTopFar.w == doctest::Approx(1.0f));

    CHECK(ofg::math::orthographicLh(1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, error).has_value() == false);
    CHECK(error.find("increasing") != std::string::npos);
    CHECK(
        ofg::math::orthographicLh(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, std::numeric_limits<float>::infinity(), error)
            .has_value() == false
    );
    CHECK(error.find("finite") != std::string::npos);
}

// Verifies const indexing, explicitly packed data, and rotation helpers.
TEST_CASE("math exposes shader-like const access and rotation")
{
    const ofg::math::Mat4 identity = ofg::math::mat4Identity();
    const auto identityData = ofg::math::packMat4(identity);
    CHECK(identity[0][0] == doctest::Approx(1.0f));
    CHECK(identityData[0] == doctest::Approx(1.0f));
    CHECK(identityData[15] == doctest::Approx(1.0f));

    const ofg::math::Vec4 color = ofg::math::vec4(0.25f, 0.5f, 0.75f, 1.0f);
    const std::array<float, 4> packedColor = ofg::math::packVec4(color);
    CHECK(color[2] == doctest::Approx(0.75f));
    CHECK(packedColor[3] == doctest::Approx(1.0f));

    const ofg::math::Mat4 rotation = ofg::math::mat4RotationY(1.57079632679f);
    const ofg::math::Vec4 rotated = ofg::math::mul(rotation, ofg::math::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(rotated.x == doctest::Approx(0.0f).epsilon(0.0001));
    CHECK(rotated.z == doctest::Approx(-1.0f));
}

// Verifies quaternion helpers create scene rotations.
TEST_CASE("math quaternions support scene rotations")
{
    std::string error;
    const std::optional<ofg::math::Quat> rotation =
        ofg::math::quatFromAxisAngle(ofg::math::vec3(0.0f, 1.0f, 0.0f), 1.57079632679f, error);
    REQUIRE(rotation.has_value());
    CHECK(error.empty());

    const ofg::math::Mat4 matrix = ofg::math::mat4FromQuat(*rotation);
    const ofg::math::Vec4 rotated = ofg::math::mul(matrix, ofg::math::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(rotated.x == doctest::Approx(0.0f).epsilon(0.0001));
    CHECK(rotated.z == doctest::Approx(-1.0f));

    const std::optional<ofg::math::Quat> normalized =
        ofg::math::normalize(ofg::math::Quat{0.0f, 0.0f, 0.0f, 2.0f}, error);
    REQUIRE(normalized.has_value());
    CHECK(normalized->w == doctest::Approx(1.0f));

    CHECK(ofg::math::quatFromAxisAngle(ofg::math::vec3(0.0f, 0.0f, 0.0f), 1.0f, error).has_value() == false);
    CHECK(error.find("axis") != std::string::npos);
    CHECK(
        ofg::math::quatFromAxisAngle(ofg::math::vec3(0.0f, 1.0f, 0.0f), std::numeric_limits<float>::infinity(), error)
            .has_value() == false
    );
    CHECK(error.find("angle") != std::string::npos);
    CHECK(ofg::math::normalize(ofg::math::Quat{std::nanf(""), 0.0f, 0.0f, 1.0f}, error).has_value() == false);
    CHECK(error.find("non-finite") != std::string::npos);
    CHECK(ofg::math::normalize(ofg::math::Quat{0.0f, 0.0f, 0.0f, 0.0f}, error).has_value() == false);
    CHECK(error.find("zero-length") != std::string::npos);
}

// Verifies look-at quaternions match the OFG left-handed player/camera convention.
TEST_CASE("math quaternions support left-handed z-forward look-at rotations")
{
    std::string error;
    const ofg::math::Vec3 eye = ofg::math::vec3(6.2f, 4.4f, 7.6f);
    const ofg::math::Vec3 target = ofg::math::vec3(0.0f, 0.55f, 0.0f);
    const ofg::math::Vec3 up = ofg::math::vec3(0.0f, 1.0f, 0.0f);

    const std::optional<ofg::math::Quat> rotation = ofg::math::quatLookAtLh(eye, target, up, error);
    REQUIRE(rotation.has_value());
    CHECK(error.empty());

    const ofg::math::Mat4 matrix = ofg::math::mat4FromQuat(*rotation);
    const std::optional<ofg::math::Vec3> expectedForward = ofg::math::normalize(ofg::math::sub(target, eye), error);
    REQUIRE(expectedForward.has_value());
    const std::optional<ofg::math::Vec3> expectedRight =
        ofg::math::normalize(ofg::math::cross(up, *expectedForward), error);
    REQUIRE(expectedRight.has_value());
    const ofg::math::Vec3 expectedUp = ofg::math::cross(*expectedForward, *expectedRight);

    const ofg::math::Vec4 forward = ofg::math::mul(matrix, ofg::math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    CHECK(forward.x == doctest::Approx(expectedForward->x).epsilon(0.0001));
    CHECK(forward.y == doctest::Approx(expectedForward->y).epsilon(0.0001));
    CHECK(forward.z == doctest::Approx(expectedForward->z).epsilon(0.0001));
    CHECK(matrix[0].x == doctest::Approx(expectedRight->x).epsilon(0.0001));
    CHECK(matrix[0].y == doctest::Approx(expectedRight->y).epsilon(0.0001));
    CHECK(matrix[0].z == doctest::Approx(expectedRight->z).epsilon(0.0001));
    CHECK(matrix[1].x == doctest::Approx(expectedUp.x).epsilon(0.0001));
    CHECK(matrix[1].y == doctest::Approx(expectedUp.y).epsilon(0.0001));
    CHECK(matrix[1].z == doctest::Approx(expectedUp.z).epsilon(0.0001));

    const std::optional<ofg::math::Quat> identityForward =
        ofg::math::quatLookAtLh(ofg::math::vec3(0.0f, 0.0f, 0.0f), ofg::math::vec3(0.0f, 0.0f, 1.0f), up, error);
    REQUIRE(identityForward.has_value());
    const ofg::math::Mat4 identityForwardMatrix = ofg::math::mat4FromQuat(*identityForward);
    CHECK(identityForwardMatrix[0].x == doctest::Approx(1.0f));
    CHECK(identityForwardMatrix[1].y == doctest::Approx(1.0f));
    CHECK(identityForwardMatrix[2].z == doctest::Approx(1.0f));

    CHECK(ofg::math::quatLookAtLh(eye, eye, up, error).has_value() == false);
    CHECK(error.find("distinct") != std::string::npos);
    CHECK(ofg::math::quatLookAtLh(eye, target, ofg::math::sub(target, eye), error).has_value() == false);
    CHECK(error.find("parallel") != std::string::npos);
}

// Verifies look-at quaternion conversion covers half-turn matrix branches.
TEST_CASE("math left-handed look-at quaternions support half-turn rotations")
{
    std::string error;
    const ofg::math::Vec3 eye = ofg::math::vec3(0.0f, 0.0f, 0.0f);

    const std::optional<ofg::math::Quat> turnX =
        ofg::math::quatLookAtLh(eye, ofg::math::vec3(0.0f, 0.0f, -1.0f), ofg::math::vec3(0.0f, -1.0f, 0.0f), error);
    REQUIRE(turnX.has_value());
    const ofg::math::Mat4 matrixX = ofg::math::mat4FromQuat(*turnX);
    CHECK(matrixX[0].x == doctest::Approx(1.0f));
    CHECK(matrixX[1].y == doctest::Approx(-1.0f));
    CHECK(matrixX[2].z == doctest::Approx(-1.0f));

    const std::optional<ofg::math::Quat> turnY =
        ofg::math::quatLookAtLh(eye, ofg::math::vec3(0.0f, 0.0f, -1.0f), ofg::math::vec3(0.0f, 1.0f, 0.0f), error);
    REQUIRE(turnY.has_value());
    const ofg::math::Mat4 matrixY = ofg::math::mat4FromQuat(*turnY);
    CHECK(matrixY[0].x == doctest::Approx(-1.0f));
    CHECK(matrixY[1].y == doctest::Approx(1.0f));
    CHECK(matrixY[2].z == doctest::Approx(-1.0f));

    const std::optional<ofg::math::Quat> turnZ =
        ofg::math::quatLookAtLh(eye, ofg::math::vec3(0.0f, 0.0f, 1.0f), ofg::math::vec3(0.0f, -1.0f, 0.0f), error);
    REQUIRE(turnZ.has_value());
    const ofg::math::Mat4 matrixZ = ofg::math::mat4FromQuat(*turnZ);
    CHECK(matrixZ[0].x == doctest::Approx(-1.0f));
    CHECK(matrixZ[1].y == doctest::Approx(-1.0f));
    CHECK(matrixZ[2].z == doctest::Approx(1.0f));
}

// Verifies invalid camera parameters produce useful errors.
TEST_CASE("math camera helpers reject non-finite and parallel inputs")
{
    std::string error;
    CHECK(
        ofg::math::perspectiveLh(std::numeric_limits<float>::infinity(), 1.0f, 0.1f, 100.0f, error).has_value() == false
    );
    CHECK(error.find("finite") != std::string::npos);

    CHECK(
        ofg::math::lookAtLh(
            ofg::math::vec3(0.0f, 0.0f, 0.0f),
            ofg::math::vec3(0.0f, 0.0f, 1.0f),
            ofg::math::vec3(0.0f, 0.0f, 2.0f),
            error
        )
            .has_value() == false
    );
    CHECK(error.find("parallel") != std::string::npos);
}
