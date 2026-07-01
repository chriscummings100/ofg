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

// Builds a normalized quaternion from an orthonormal row-major rotation matrix.
std::optional<Quat> quat_from_rotation_matrix(float m00,
    float m01,
    float m02,
    float m10,
    float m11,
    float m12,
    float m20,
    float m21,
    float m22,
    std::string& error) {
    Quat quaternion;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        quaternion.w = 0.25f * scale;
        quaternion.x = (m21 - m12) / scale;
        quaternion.y = (m02 - m20) / scale;
        quaternion.z = (m10 - m01) / scale;
    } else if (m00 > m11 && m00 > m22) {
        const float scale = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        quaternion.w = (m21 - m12) / scale;
        quaternion.x = 0.25f * scale;
        quaternion.y = (m01 + m10) / scale;
        quaternion.z = (m02 + m20) / scale;
    } else if (m11 > m22) {
        const float scale = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        quaternion.w = (m02 - m20) / scale;
        quaternion.x = (m01 + m10) / scale;
        quaternion.y = 0.25f * scale;
        quaternion.z = (m12 + m21) / scale;
    } else {
        const float scale = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        quaternion.w = (m10 - m01) / scale;
        quaternion.x = (m02 + m20) / scale;
        quaternion.y = (m12 + m21) / scale;
        quaternion.z = 0.25f * scale;
    }
    return normalize(quaternion, error);
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

// Builds a camera entity rotation that looks from eye toward target in right-handed space.
std::optional<Quat> quat_look_at_rh(Vec3 eye, Vec3 target, Vec3 up, std::string& error) {
    std::optional<Vec3> forward = normalize(sub(target, eye), error);
    if (!forward.has_value()) {
        error = "Quaternion look-at eye and target must be distinct.";
        return std::nullopt;
    }

    std::optional<Vec3> right = normalize(cross(*forward, up), error);
    if (!right.has_value()) {
        error = "Quaternion look-at up vector must not be parallel to the view direction.";
        return std::nullopt;
    }
    const Vec3 camera_up = cross(*right, *forward);
    const Vec3 camera_back = mul(*forward, -1.0f);

    return quat_from_rotation_matrix(right->x,
        camera_up.x,
        camera_back.x,
        right->y,
        camera_up.y,
        camera_back.y,
        right->z,
        camera_up.z,
        camera_back.z,
        error);
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
