// Column-major matrix implementation for OFG renderer CPU-side math.
#include "ofg/math/mat.hpp"

#include <array>
#include <cstddef>

namespace ofg::math {

// Returns a mutable column by shader-style numeric index.
Vec4& Mat4::operator[](std::size_t column) noexcept {
    return m_columns[column];
}

// Returns an immutable column by shader-style numeric index.
const Vec4& Mat4::operator[](std::size_t column) const noexcept {
    return m_columns[column];
}

// Returns the contiguous column-major float data.
const float* Mat4::data() const noexcept {
    return &m_columns[0].x;
}

// Returns a mutable component by shader-style numeric index.
float& Vec4::operator[](std::size_t index) noexcept {
    return (&x)[index];
}

// Returns an immutable component by shader-style numeric index.
const float& Vec4::operator[](std::size_t index) const noexcept {
    return (&x)[index];
}

// Builds an identity matrix.
Mat4 mat4_identity() noexcept {
    Mat4 matrix;
    matrix[0] = vec4(1.0F, 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, 1.0F, 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, 1.0F, 0.0F);
    matrix[3] = vec4(0.0F, 0.0F, 0.0F, 1.0F);
    return matrix;
}

// Multiplies two column-major matrices.
Mat4 mul(Mat4 a, Mat4 b) noexcept {
    Mat4 result;
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            result[column][row] = a[0][row] * b[column][0] + a[1][row] * b[column][1] + a[2][row] * b[column][2] +
                                  a[3][row] * b[column][3];
        }
    }
    return result;
}

// Multiplies a column-major matrix by a column vector.
Vec4 mul(Mat4 matrix, Vec4 vector) noexcept {
    return vec4(matrix[0].x * vector.x + matrix[1].x * vector.y + matrix[2].x * vector.z + matrix[3].x * vector.w,
        matrix[0].y * vector.x + matrix[1].y * vector.y + matrix[2].y * vector.z + matrix[3].y * vector.w,
        matrix[0].z * vector.x + matrix[1].z * vector.y + matrix[2].z * vector.z + matrix[3].z * vector.w,
        matrix[0].w * vector.x + matrix[1].w * vector.y + matrix[2].w * vector.z + matrix[3].w * vector.w);
}

// Packs a matrix into WGSL-compatible column-major float order.
std::array<float, 16> pack_mat4(Mat4 matrix) noexcept {
    std::array<float, 16> packed{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            packed[column * 4 + row] = matrix[column][row];
        }
    }
    return packed;
}

} // namespace ofg::math
