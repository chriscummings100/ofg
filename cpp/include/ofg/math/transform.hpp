// Transform and camera helpers for OFG renderer CPU-side math.
//
// The helpers intentionally mirror shader-language naming while keeping the
// implementation small enough to audit beside renderer tests.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"

#include <optional>
#include <string>

namespace ofg::math {

// Builds a translation matrix.
[[nodiscard]] Mat4 mat4_translation(Vec3 translation) noexcept;

// Builds a scale matrix.
[[nodiscard]] Mat4 mat4_scale(Vec3 scale) noexcept;

// Builds a right-handed Y-axis rotation matrix.
[[nodiscard]] Mat4 mat4_rotation_y(float radians) noexcept;

// Builds a right-handed perspective matrix with WebGPU depth range [0, 1].
[[nodiscard]] std::optional<Mat4> perspective_rh(
    float fovy_radians, float aspect, float near_z, float far_z, std::string& error);

// Builds a right-handed view matrix that looks from eye toward target.
[[nodiscard]] std::optional<Mat4> look_at_rh(Vec3 eye, Vec3 target, Vec3 up, std::string& error);

} // namespace ofg::math
