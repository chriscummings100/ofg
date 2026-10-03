// Quaternion helpers for OFG scene transform rotation.
#include "math/quat.h"

#include "math/mat.h"
#include "math/vec.h"

#include <cmath>
#include <optional>
#include <string>

namespace ofg::math {
namespace {

// Reports whether every quaternion component is finite.
bool finiteQuat(Quat value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

// Builds a normalized quaternion from an orthonormal row-major rotation matrix.
std::optional<Quat> quatFromRotationMatrix(
    float m00,
    float m01,
    float m02,
    float m10,
    float m11,
    float m12,
    float m20,
    float m21,
    float m22,
    std::string& error
)
{
    Quat quaternion;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f)
    {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        quaternion.w = 0.25f * scale;
        quaternion.x = (m21 - m12) / scale;
        quaternion.y = (m02 - m20) / scale;
        quaternion.z = (m10 - m01) / scale;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float scale = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        quaternion.w = (m21 - m12) / scale;
        quaternion.x = 0.25f * scale;
        quaternion.y = (m01 + m10) / scale;
        quaternion.z = (m02 + m20) / scale;
    }
    else if (m11 > m22)
    {
        const float scale = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        quaternion.w = (m02 - m20) / scale;
        quaternion.x = (m01 + m10) / scale;
        quaternion.y = 0.25f * scale;
        quaternion.z = (m12 + m21) / scale;
    }
    else
    {
        const float scale = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        quaternion.w = (m10 - m01) / scale;
        quaternion.x = (m02 + m20) / scale;
        quaternion.y = (m12 + m21) / scale;
        quaternion.z = 0.25f * scale;
    }
    return normalize(quaternion, error);
}

} // namespace

std::optional<Quat> quatFromAxisAngle(Vec3 axis, float radians, std::string& error)
{
    if (!std::isfinite(radians))
    {
        error = "Quaternion angle must be finite.";
        return std::nullopt;
    }

    std::optional<Vec3> normalizedAxis = normalize(axis, error);
    if (!normalizedAxis.has_value())
    {
        error = "Quaternion axis must be finite and nonzero.";
        return std::nullopt;
    }

    const float halfAngle = radians * 0.5f;
    const float s = std::sin(halfAngle);
    Quat quaternion{normalizedAxis->x * s, normalizedAxis->y * s, normalizedAxis->z * s, std::cos(halfAngle)};
    return normalize(quaternion, error);
}

std::optional<Quat> quatLookAtLh(Vec3 eye, Vec3 target, Vec3 up, std::string& error)
{
    std::optional<Vec3> forward = normalize(sub(target, eye), error);
    if (!forward.has_value())
    {
        error = "Quaternion left-handed look-at eye and target must be distinct.";
        return std::nullopt;
    }

    std::optional<Vec3> right = normalize(cross(up, *forward), error);
    if (!right.has_value())
    {
        error = "Quaternion left-handed look-at up vector must not be parallel to the view direction.";
        return std::nullopt;
    }
    const Vec3 cameraUp = cross(*forward, *right);

    return quatFromRotationMatrix(
        right->x,
        cameraUp.x,
        forward->x,
        right->y,
        cameraUp.y,
        forward->y,
        right->z,
        cameraUp.z,
        forward->z,
        error
    );
}

std::optional<Quat> normalize(Quat value, std::string& error)
{
    if (!finiteQuat(value))
    {
        error = "Cannot normalize a non-finite quaternion.";
        return std::nullopt;
    }

    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    const float valueLength = std::sqrt(lengthSquared);
    if (valueLength <= 0.0f || !std::isfinite(valueLength))
    {
        error = "Cannot normalize a zero-length or non-finite quaternion.";
        return std::nullopt;
    }

    const float inverseLength = 1.0f / valueLength;
    error.clear();
    return Quat{value.x * inverseLength, value.y * inverseLength, value.z * inverseLength, value.w * inverseLength};
}

Mat4 mat4FromQuat(Quat rotation) noexcept
{
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    Mat4 matrix = mat4Identity();
    matrix[0] = vec4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f);
    matrix[1] = vec4(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f);
    matrix[2] = vec4(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f);
    matrix[3] = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return matrix;
}

} // namespace ofg::math
