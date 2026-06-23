// Transform and camera helpers for OFG renderer CPU-side math.
#include "ofg/math/transform.hpp"

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

#include <cmath>
#include <optional>
#include <string>

namespace ofg::math {

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

// Builds a right-handed Y-axis rotation matrix.
Mat4 mat4_rotation_y(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 matrix = mat4_identity();
    matrix[0] = vec4(c, 0.0F, -s, 0.0F);
    matrix[2] = vec4(s, 0.0F, c, 0.0F);
    return matrix;
}

// Builds a right-handed perspective matrix with WebGPU depth range [0, 1].
std::optional<Mat4> perspective_rh(float fovy_radians, float aspect, float near_z, float far_z, std::string& error) {
    if (!std::isfinite(fovy_radians) || !std::isfinite(aspect) || !std::isfinite(near_z) || !std::isfinite(far_z)) {
        error = "Perspective parameters must be finite.";
        return std::nullopt;
    }
    if (fovy_radians <= 0.0F || fovy_radians >= 3.1415926535F || aspect <= 0.0F || near_z <= 0.0F || far_z <= near_z) {
        error = "Perspective parameters are outside the supported right-handed range.";
        return std::nullopt;
    }

    const float f = 1.0F / std::tan(fovy_radians * 0.5F);
    Mat4 matrix;
    matrix[0] = vec4(f / aspect, 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, f, 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, far_z / (near_z - far_z), -1.0F);
    matrix[3] = vec4(0.0F, 0.0F, (far_z * near_z) / (near_z - far_z), 0.0F);
    error.clear();
    return matrix;
}

// Builds a right-handed view matrix that looks from eye toward target.
std::optional<Mat4> look_at_rh(Vec3 eye, Vec3 target, Vec3 up, std::string& error) {
    std::optional<Vec3> forward = normalize(sub(target, eye), error);
    if (!forward.has_value()) {
        error = "Look-at eye and target must be distinct.";
        return std::nullopt;
    }

    std::optional<Vec3> side = normalize(cross(*forward, up), error);
    if (!side.has_value()) {
        error = "Look-at up vector must not be parallel to the view direction.";
        return std::nullopt;
    }
    const Vec3 view_up = cross(*side, *forward);

    Mat4 matrix;
    matrix[0] = vec4(side->x, view_up.x, -forward->x, 0.0F);
    matrix[1] = vec4(side->y, view_up.y, -forward->y, 0.0F);
    matrix[2] = vec4(side->z, view_up.z, -forward->z, 0.0F);
    matrix[3] = vec4(-dot(*side, eye), -dot(view_up, eye), dot(*forward, eye), 1.0F);
    error.clear();
    return matrix;
}

} // namespace ofg::math
