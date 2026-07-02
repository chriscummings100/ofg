// Transform and camera helpers for OFG renderer CPU-side math.
#include "ofg/math/transform.hpp"

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

#include <cmath>
#include <optional>
#include <string>

namespace ofg::math {
namespace {

constexpr float _matrix_min_determinant = 0.000001F;

} // namespace

// Returns the vector length.
float length(Vec3 value) noexcept {
    return std::sqrt(length_squared(value));
}

// Returns a normalized vector or an error when the input is zero length.
std::optional<Vec3> normalize(Vec3 value, std::string& error) {
    const float value_length = length(value);
    if (value_length <= 0.0F || !std::isfinite(value_length)) {
        error = "Cannot normalize a zero-length or non-finite vector.";
        return std::nullopt;
    }
    error.clear();
    return mul(value, 1.0F / value_length);
}

// Builds a translation matrix.
Mat4 mat4_translation(Vec3 translation) noexcept {
    Mat4 matrix = mat4_identity();
    matrix[3] = vec4(translation.x, translation.y, translation.z, 1.0F);
    return matrix;
}

// Builds a scale matrix.
Mat4 mat4_scale(Vec3 scale) noexcept {
    Mat4 matrix;
    matrix[0] = vec4(scale.x, 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, scale.y, 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, scale.z, 0.0F);
    matrix[3] = vec4(0.0F, 0.0F, 0.0F, 1.0F);
    return matrix;
}

// Builds a Y-axis yaw rotation matrix for OFG's left-handed, +Z-forward space.
Mat4 mat4_rotation_y(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 matrix = mat4_identity();
    matrix[0] = vec4(c, 0.0F, -s, 0.0F);
    matrix[2] = vec4(s, 0.0F, c, 0.0F);
    return matrix;
}

// Transforms a point by a matrix using homogeneous w=1.
Vec3 transform_point(Mat4 matrix, Vec3 point) noexcept {
    const Vec4 transformed = mul(matrix, vec4(point.x, point.y, point.z, 1.0F));
    return vec3(transformed.x, transformed.y, transformed.z);
}

// Transforms a direction by a matrix using homogeneous w=0.
Vec3 transform_direction(Mat4 matrix, Vec3 direction) noexcept {
    const Vec4 transformed = mul(matrix, vec4(direction.x, direction.y, direction.z, 0.0F));
    return vec3(transformed.x, transformed.y, transformed.z);
}

// Returns the inverse of an affine matrix with a non-singular upper 3x3.
std::optional<Mat4> inverse_affine(Mat4 matrix, std::string& error) {
    const Vec3 column0 = vec3(matrix[0].x, matrix[0].y, matrix[0].z);
    const Vec3 column1 = vec3(matrix[1].x, matrix[1].y, matrix[1].z);
    const Vec3 column2 = vec3(matrix[2].x, matrix[2].y, matrix[2].z);
    const Vec3 row0 = cross(column1, column2);
    const float determinant = dot(column0, row0);
    if (!std::isfinite(determinant) || std::fabs(determinant) < _matrix_min_determinant) {
        error = "Affine matrix is not invertible.";
        return std::nullopt;
    }

    const float inverse_determinant = 1.0F / determinant;
    const Vec3 inverse_row0 = mul(row0, inverse_determinant);
    const Vec3 inverse_row1 = mul(cross(column2, column0), inverse_determinant);
    const Vec3 inverse_row2 = mul(cross(column0, column1), inverse_determinant);
    const Vec3 translation = vec3(matrix[3].x, matrix[3].y, matrix[3].z);

    Mat4 inverse = mat4_identity();
    inverse[0] = vec4(inverse_row0.x, inverse_row1.x, inverse_row2.x, 0.0F);
    inverse[1] = vec4(inverse_row0.y, inverse_row1.y, inverse_row2.y, 0.0F);
    inverse[2] = vec4(inverse_row0.z, inverse_row1.z, inverse_row2.z, 0.0F);
    inverse[3] =
        vec4(-dot(inverse_row0, translation), -dot(inverse_row1, translation), -dot(inverse_row2, translation), 1.0F);
    error.clear();
    return inverse;
}

// Builds a left-handed perspective matrix with WebGPU depth range [0, 1].
std::optional<Mat4> perspective_lh(float fovy_radians, float aspect, float near_z, float far_z, std::string& error) {
    if (!std::isfinite(fovy_radians) || !std::isfinite(aspect) || !std::isfinite(near_z) || !std::isfinite(far_z)) {
        error = "Perspective parameters must be finite.";
        return std::nullopt;
    }
    if (fovy_radians <= 0.0F || fovy_radians >= 3.1415926535F || aspect <= 0.0F || near_z <= 0.0F || far_z <= near_z) {
        error = "Perspective parameters are outside the supported left-handed range.";
        return std::nullopt;
    }

    const float f = 1.0F / std::tan(fovy_radians * 0.5F);
    Mat4 matrix;
    matrix[0] = vec4(f / aspect, 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, f, 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, far_z / (far_z - near_z), 1.0F);
    matrix[3] = vec4(0.0F, 0.0F, -(far_z * near_z) / (far_z - near_z), 0.0F);
    error.clear();
    return matrix;
}

// Builds a left-handed view matrix that treats camera-local +Z as forward.
std::optional<Mat4> look_at_lh(Vec3 eye, Vec3 target, Vec3 up, std::string& error) {
    std::optional<Vec3> forward = normalize(sub(target, eye), error);
    if (!forward.has_value()) {
        error = "Look-at eye and target must be distinct.";
        return std::nullopt;
    }

    std::optional<Vec3> side = normalize(cross(up, *forward), error);
    if (!side.has_value()) {
        error = "Look-at up vector must not be parallel to the view direction.";
        return std::nullopt;
    }
    const Vec3 view_up = cross(*forward, *side);

    Mat4 matrix;
    matrix[0] = vec4(side->x, view_up.x, forward->x, 0.0F);
    matrix[1] = vec4(side->y, view_up.y, forward->y, 0.0F);
    matrix[2] = vec4(side->z, view_up.z, forward->z, 0.0F);
    matrix[3] = vec4(-dot(*side, eye), -dot(view_up, eye), -dot(*forward, eye), 1.0F);
    error.clear();
    return matrix;
}

} // namespace ofg::math
