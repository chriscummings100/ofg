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

// Transforms a point by a matrix using homogeneous w=1.
[[nodiscard]] Vec3 transform_point(Mat4 matrix, Vec3 point) noexcept;

// Transforms a direction by a matrix using homogeneous w=0.
[[nodiscard]] Vec3 transform_direction(Mat4 matrix, Vec3 direction) noexcept;

// Returns the inverse of an affine matrix with a non-singular upper 3x3.
[[nodiscard]] std::optional<Mat4> inverse_affine(Mat4 matrix, std::string& error);

// Builds a left-handed perspective matrix with WebGPU depth range [0, 1].
[[nodiscard]] std::optional<Mat4> perspective_lh(
    float fovy_radians, float aspect, float near_z, float far_z, std::string& error);

// Builds a left-handed orthographic matrix with WebGPU depth range [0, 1].
[[nodiscard]] std::optional<Mat4> orthographic_lh(
    float left, float right, float bottom, float top, float near_z, float far_z, std::string& error);

// Builds a left-handed view matrix that treats camera-local +Z as forward.
[[nodiscard]] std::optional<Mat4> look_at_lh(Vec3 eye, Vec3 target, Vec3 up, std::string& error);

} // namespace ofg::math
