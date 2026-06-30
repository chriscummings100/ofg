// Quaternion helpers for OFG scene transform rotation.
//
// Quat stores an x/y/z vector part plus w scalar part. The helpers here are
// intentionally minimal: scene transforms only need identity construction,
// axis-angle creation, normalization, and conversion to Mat4.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

#include <optional>
#include <string>

namespace ofg::math {

struct Quat {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};
};

// Builds the identity rotation quaternion.
[[nodiscard]] constexpr Quat quat_identity() noexcept {
    return Quat{0.0f, 0.0f, 0.0f, 1.0f};
}

// Builds a normalized quaternion from an axis and angle in radians.
[[nodiscard]] std::optional<Quat> quat_from_axis_angle(Vec3 axis, float radians, std::string& error);

// Returns a normalized quaternion or an error for zero-length/non-finite input.
[[nodiscard]] std::optional<Quat> normalize(Quat value, std::string& error);

// Converts a finite normalized quaternion into a column-major rotation matrix.
[[nodiscard]] Mat4 mat4_from_quat(Quat rotation) noexcept;

} // namespace ofg::math
