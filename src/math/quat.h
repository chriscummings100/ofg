// Quaternion helpers for OFG scene transform rotation.
//
// Quat stores an x/y/z vector part plus w scalar part. The helpers here are
// intentionally minimal: scene transforms only need identity construction,
// axis-angle/look-at creation, normalization, and conversion to Mat4.
#pragma once

#include "math/mat.h"
#include "math/vec.h"

#include <optional>
#include <string>

namespace ofg::math {

struct Quat
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};
};

// Builds the identity rotation quaternion.
[[nodiscard]] constexpr Quat quatIdentity() noexcept
{
    return Quat{0.0f, 0.0f, 0.0f, 1.0f};
}

// Builds a normalized quaternion from an axis and angle in radians.
[[nodiscard]] std::optional<Quat> quatFromAxisAngle(Vec3 axis, float radians, std::string& error);

// Builds an entity rotation whose local +Z axis looks from eye toward target in OFG's left-handed space.
[[nodiscard]] std::optional<Quat> quatLookAtLh(Vec3 eye, Vec3 target, Vec3 up, std::string& error);

// Returns a normalized quaternion or an error for zero-length/non-finite input.
[[nodiscard]] std::optional<Quat> normalize(Quat value, std::string& error);

// Converts a finite normalized quaternion into a column-major rotation matrix.
[[nodiscard]] Mat4 mat4FromQuat(Quat rotation) noexcept;

} // namespace ofg::math
