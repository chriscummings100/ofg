// Column-major matrix value type for OFG renderer CPU-side math.
//
// Mat4 stores columns so `packMat4` can write bytes directly for WGSL
// `mat4x4<f32>` uniforms without transposing.
#pragma once

#include "math/vec.h"

#include <array>
#include <cstddef>

namespace ofg::math {

class Mat4
{
public:
    // Returns a mutable column by shader-style numeric index.
    [[nodiscard]] Vec4& operator[](std::size_t column) noexcept;
    // Returns an immutable column by shader-style numeric index.
    [[nodiscard]] const Vec4& operator[](std::size_t column) const noexcept;

private:
    std::array<Vec4, 4> m_columns{};
};

// Builds an identity matrix.
[[nodiscard]] Mat4 mat4Identity() noexcept;

// Multiplies two column-major matrices.
[[nodiscard]] Mat4 mul(Mat4 a, Mat4 b) noexcept;

// Multiplies a column-major matrix by a column vector.
[[nodiscard]] Vec4 mul(Mat4 matrix, Vec4 vector) noexcept;

// Packs a matrix into WGSL-compatible column-major float order.
[[nodiscard]] std::array<float, 16> packMat4(Mat4 matrix) noexcept;

} // namespace ofg::math
