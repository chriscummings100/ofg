// Transform and camera helpers for OFG renderer CPU-side math.
#include "math/transform.h"

#include "math/mat.h"
#include "math/vec.h"

#include <cmath>
#include <optional>
#include <string>

namespace ofg::math {
namespace {

constexpr float matrixMinDeterminant = 0.000001F;

} // namespace

float length(Vec3 value) noexcept
{
    return std::sqrt(lengthSquared(value));
}

std::optional<Vec3> normalize(Vec3 value, std::string& error)
{
    const float valueLength = length(value);
    if (valueLength <= 0.0F || !std::isfinite(valueLength))
    {
        error = "Cannot normalize a zero-length or non-finite vector.";
        return std::nullopt;
    }
    error.clear();
    return mul(value, 1.0F / valueLength);
}

Mat4 mat4Translation(Vec3 translation) noexcept
{
    Mat4 matrix = mat4Identity();
    matrix[3] = vec4(translation.x, translation.y, translation.z, 1.0F);
    return matrix;
}

Mat4 mat4Scale(Vec3 scale) noexcept
{
    Mat4 matrix;
    matrix[0] = vec4(scale.x, 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, scale.y, 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, scale.z, 0.0F);
    matrix[3] = vec4(0.0F, 0.0F, 0.0F, 1.0F);
    return matrix;
}

Mat4 mat4RotationY(float radians) noexcept
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 matrix = mat4Identity();
    matrix[0] = vec4(c, 0.0F, -s, 0.0F);
    matrix[2] = vec4(s, 0.0F, c, 0.0F);
    return matrix;
}

Vec3 transformPoint(Mat4 matrix, Vec3 point) noexcept
{
    const Vec4 transformed = mul(matrix, vec4(point.x, point.y, point.z, 1.0F));
    return vec3(transformed.x, transformed.y, transformed.z);
}

Vec3 transformDirection(Mat4 matrix, Vec3 direction) noexcept
{
    const Vec4 transformed = mul(matrix, vec4(direction.x, direction.y, direction.z, 0.0F));
    return vec3(transformed.x, transformed.y, transformed.z);
}

std::optional<Mat4> inverseAffine(Mat4 matrix, std::string& error)
{
    const Vec3 column0 = vec3(matrix[0].x, matrix[0].y, matrix[0].z);
    const Vec3 column1 = vec3(matrix[1].x, matrix[1].y, matrix[1].z);
    const Vec3 column2 = vec3(matrix[2].x, matrix[2].y, matrix[2].z);
    const Vec3 row0 = cross(column1, column2);
    const float determinant = dot(column0, row0);
    if (!std::isfinite(determinant) || std::fabs(determinant) < matrixMinDeterminant)
    {
        error = "Affine matrix is not invertible.";
        return std::nullopt;
    }

    const float inverseDeterminant = 1.0F / determinant;
    const Vec3 inverseRow0 = mul(row0, inverseDeterminant);
    const Vec3 inverseRow1 = mul(cross(column2, column0), inverseDeterminant);
    const Vec3 inverseRow2 = mul(cross(column0, column1), inverseDeterminant);
    const Vec3 translation = vec3(matrix[3].x, matrix[3].y, matrix[3].z);

    Mat4 inverse = mat4Identity();
    inverse[0] = vec4(inverseRow0.x, inverseRow1.x, inverseRow2.x, 0.0F);
    inverse[1] = vec4(inverseRow0.y, inverseRow1.y, inverseRow2.y, 0.0F);
    inverse[2] = vec4(inverseRow0.z, inverseRow1.z, inverseRow2.z, 0.0F);
    inverse[3] =
        vec4(-dot(inverseRow0, translation), -dot(inverseRow1, translation), -dot(inverseRow2, translation), 1.0F);
    error.clear();
    return inverse;
}

std::optional<Mat4> perspectiveLh(float fovyRadians, float aspect, float nearZ, float farZ, std::string& error)
{
    if (!std::isfinite(fovyRadians) || !std::isfinite(aspect) || !std::isfinite(nearZ) || !std::isfinite(farZ))
    {
        error = "Perspective parameters must be finite.";
        return std::nullopt;
    }
    if (fovyRadians <= 0.0F || fovyRadians >= 3.1415926535F || aspect <= 0.0F || nearZ <= 0.0F || farZ <= nearZ)
    {
        error = "Perspective parameters are outside the supported left-handed range.";
        return std::nullopt;
    }

    const float f = 1.0F / std::tan(fovyRadians * 0.5F);
    Mat4 matrix;
    matrix[0] = vec4(f / aspect, 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, f, 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, farZ / (farZ - nearZ), 1.0F);
    matrix[3] = vec4(0.0F, 0.0F, -(farZ * nearZ) / (farZ - nearZ), 0.0F);
    error.clear();
    return matrix;
}

std::optional<Mat4> orthographicLh(
    float left,
    float right,
    float bottom,
    float top,
    float nearZ,
    float farZ,
    std::string& error
)
{
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) || !std::isfinite(top) ||
        !std::isfinite(nearZ) || !std::isfinite(farZ))
    {
        error = "Orthographic parameters must be finite.";
        return std::nullopt;
    }
    if (right <= left || top <= bottom || farZ <= nearZ)
    {
        error = "Orthographic parameters must have increasing horizontal, vertical, and depth ranges.";
        return std::nullopt;
    }

    Mat4 matrix;
    matrix[0] = vec4(2.0F / (right - left), 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, 2.0F / (top - bottom), 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, 1.0F / (farZ - nearZ), 0.0F);
    matrix[3] = vec4(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -nearZ / (farZ - nearZ), 1.0F);
    error.clear();
    return matrix;
}

std::optional<Mat4> lookAtLh(Vec3 eye, Vec3 target, Vec3 up, std::string& error)
{
    std::optional<Vec3> forward = normalize(sub(target, eye), error);
    if (!forward.has_value())
    {
        error = "Look-at eye and target must be distinct.";
        return std::nullopt;
    }

    std::optional<Vec3> side = normalize(cross(up, *forward), error);
    if (!side.has_value())
    {
        error = "Look-at up vector must not be parallel to the view direction.";
        return std::nullopt;
    }
    const Vec3 viewUp = cross(*forward, *side);

    Mat4 matrix;
    matrix[0] = vec4(side->x, viewUp.x, forward->x, 0.0F);
    matrix[1] = vec4(side->y, viewUp.y, forward->y, 0.0F);
    matrix[2] = vec4(side->z, viewUp.z, forward->z, 0.0F);
    matrix[3] = vec4(-dot(*side, eye), -dot(viewUp, eye), -dot(*forward, eye), 1.0F);
    error.clear();
    return matrix;
}

} // namespace ofg::math
