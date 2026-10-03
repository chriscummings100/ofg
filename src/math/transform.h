// Transform and camera helpers for OFG renderer CPU-side math.
//
// The helpers intentionally mirror shader-language naming while keeping the
// implementation small enough to audit beside renderer tests.
#pragma once

#include "math/mat.h"
#include "math/vec.h"

#include <optional>
#include <string>

namespace ofg::math {

// Builds a translation matrix.
[[nodiscard]] Mat4 mat4Translation(Vec3 translation) noexcept;

// Builds a scale matrix.
[[nodiscard]] Mat4 mat4Scale(Vec3 scale) noexcept;

// Builds a Y-axis yaw rotation matrix for OFG's left-handed, +Z-forward space.
[[nodiscard]] Mat4 mat4RotationY(float radians) noexcept;

// Transforms a point by a matrix using homogeneous w=1.
[[nodiscard]] Vec3 transformPoint(Mat4 matrix, Vec3 point) noexcept;

// Transforms a direction by a matrix using homogeneous w=0.
[[nodiscard]] Vec3 transformDirection(Mat4 matrix, Vec3 direction) noexcept;

// Returns the inverse of an affine matrix with a non-singular upper 3x3.
[[nodiscard]] std::optional<Mat4> inverseAffine(Mat4 matrix, std::string& error);

// Builds a left-handed perspective matrix with WebGPU depth range [0, 1].
[[nodiscard]] std::optional<Mat4> perspectiveLh(
    float fovyRadians,
    float aspect,
    float nearZ,
    float farZ,
    std::string& error
);

// Builds a left-handed orthographic matrix with WebGPU depth range [0, 1].
[[nodiscard]] std::optional<Mat4> orthographicLh(
    float left,
    float right,
    float bottom,
    float top,
    float nearZ,
    float farZ,
    std::string& error
);

// Builds a left-handed view matrix that treats camera-local +Z as forward.
[[nodiscard]] std::optional<Mat4> lookAtLh(Vec3 eye, Vec3 target, Vec3 up, std::string& error);

} // namespace ofg::math
