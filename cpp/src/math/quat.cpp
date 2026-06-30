// Quaternion helpers for OFG scene transform rotation.
#include "ofg/math/quat.hpp"

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

#include <cmath>
#include <optional>
#include <string>

namespace ofg::math {
namespace {

// Reports whether every quaternion component is finite.
bool finite_quat(Quat value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

} // namespace

// Builds a normalized quaternion from an axis and angle in radians.
std::optional<Quat> quat_from_axis_angle(Vec3 axis, float radians, std::string& error) {
    if (!std::isfinite(radians)) {
        error = "Quaternion angle must be finite.";
        return std::nullopt;
    }

    std::optional<Vec3> normalized_axis = normalize(axis, error);
    if (!normalized_axis.has_value()) {
        error = "Quaternion axis must be finite and nonzero.";
        return std::nullopt;
    }

    const float half_angle = radians * 0.5f;
    const float s = std::sin(half_angle);
    Quat quaternion{normalized_axis->x * s, normalized_axis->y * s, normalized_axis->z * s, std::cos(half_angle)};
    return normalize(quaternion, error);
}

// Returns a normalized quaternion or an error for zero-length/non-finite input.
std::optional<Quat> normalize(Quat value, std::string& error) {
    if (!finite_quat(value)) {
        error = "Cannot normalize a non-finite quaternion.";
        return std::nullopt;
    }

    const float length_squared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    const float value_length = std::sqrt(length_squared);
    if (value_length <= 0.0f || !std::isfinite(value_length)) {
        error = "Cannot normalize a zero-length or non-finite quaternion.";
        return std::nullopt;
    }

    const float inverse_length = 1.0f / value_length;
    error.clear();
    return Quat{value.x * inverse_length, value.y * inverse_length, value.z * inverse_length, value.w * inverse_length};
}

// Converts a finite normalized quaternion into a column-major rotation matrix.
Mat4 mat4_from_quat(Quat rotation) noexcept {
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    Mat4 matrix = mat4_identity();
    matrix[0] = vec4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f);
    matrix[1] = vec4(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f);
    matrix[2] = vec4(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f);
    matrix[3] = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return matrix;
}

} // namespace ofg::math
