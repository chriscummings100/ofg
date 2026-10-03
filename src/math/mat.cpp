// Column-major matrix implementation for OFG renderer CPU-side math.
#include "math/mat.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace ofg::math {

Vec4& Mat4::operator[](std::size_t column) noexcept
{
    return m_columns[column];
}

const Vec4& Mat4::operator[](std::size_t column) const noexcept
{
    return m_columns[column];
}

float& Vec4::operator[](std::size_t index) noexcept
{
    assert(index < 4);
    // Named members are not a C++ array; do not index by pointer arithmetic on x.
    switch (index)
    {
    case 0:
        return x;
    case 1:
        return y;
    case 2:
        return z;
    default:
        return w;
    }
}

const float& Vec4::operator[](std::size_t index) const noexcept
{
    assert(index < 4);
    switch (index)
    {
    case 0:
        return x;
    case 1:
        return y;
    case 2:
        return z;
    default:
        return w;
    }
}

Mat4 mat4Identity() noexcept
{
    Mat4 matrix;
    matrix[0] = vec4(1.0F, 0.0F, 0.0F, 0.0F);
    matrix[1] = vec4(0.0F, 1.0F, 0.0F, 0.0F);
    matrix[2] = vec4(0.0F, 0.0F, 1.0F, 0.0F);
    matrix[3] = vec4(0.0F, 0.0F, 0.0F, 1.0F);
    return matrix;
}

Mat4 mul(Mat4 a, Mat4 b) noexcept
{
    Mat4 result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result[column][row] = a[0][row] * b[column][0] + a[1][row] * b[column][1] + a[2][row] * b[column][2] +
                                  a[3][row] * b[column][3];
        }
    }
    return result;
}

Vec4 mul(Mat4 matrix, Vec4 vector) noexcept
{
    return vec4(
        matrix[0].x * vector.x + matrix[1].x * vector.y + matrix[2].x * vector.z + matrix[3].x * vector.w,
        matrix[0].y * vector.x + matrix[1].y * vector.y + matrix[2].y * vector.z + matrix[3].y * vector.w,
        matrix[0].z * vector.x + matrix[1].z * vector.y + matrix[2].z * vector.z + matrix[3].z * vector.w,
        matrix[0].w * vector.x + matrix[1].w * vector.y + matrix[2].w * vector.z + matrix[3].w * vector.w
    );
}

std::array<float, 16> packMat4(Mat4 matrix) noexcept
{
    std::array<float, 16> packed{};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            packed[column * 4 + row] = matrix[column][row];
        }
    }
    return packed;
}

} // namespace ofg::math
