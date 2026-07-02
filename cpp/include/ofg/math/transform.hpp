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

// Builds a Y-axis yaw rotation matrix for OFG's left-handed, +Z-forward space.
[[nodiscard]] Mat4 mat4_rotation_y(float radians) noexcept;

// Builds a left-handed perspective matrix with WebGPU depth range [0, 1].
[[nodiscard]] std::optional<Mat4> perspective_lh(
    float fovy_radians, float aspect, float near_z, float far_z, std::string& error);

// Builds a left-handed view matrix that treats camera-local +Z as forward.
[[nodiscard]] std::optional<Mat4> look_at_lh(Vec3 eye, Vec3 target, Vec3 up, std::string& error);

} // namespace ofg::math
