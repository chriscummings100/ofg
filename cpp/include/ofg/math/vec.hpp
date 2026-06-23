// Shader-style vector value types for OFG renderer CPU-side math.
//
// These structs intentionally expose x/y/z/w fields so renderer and scene code
// reads like WGSL while remaining tiny, deterministic C++ value data.
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace ofg::math {

struct Vec2 {
    float x{0.0F};
    float y{0.0F};
};

struct Vec3 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct Vec4 {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{0.0F};

    // Returns a mutable component by shader-style numeric index.
    [[nodiscard]] float& operator[](std::size_t index) noexcept;
    // Returns an immutable component by shader-style numeric index.
    [[nodiscard]] const float& operator[](std::size_t index) const noexcept;
};

// Builds a two-component vector.
[[nodiscard]] constexpr Vec2 vec2(float x, float y) noexcept {
    return Vec2{x, y};
}

// Builds a three-component vector.
[[nodiscard]] constexpr Vec3 vec3(float x, float y, float z) noexcept {
    return Vec3{x, y, z};
}

// Builds a four-component vector.
[[nodiscard]] constexpr Vec4 vec4(float x, float y, float z, float w) noexcept {
    return Vec4{x, y, z, w};
}

// Adds two vectors component-wise.
[[nodiscard]] constexpr Vec3 add(Vec3 a, Vec3 b) noexcept {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

// Subtracts two vectors component-wise.
[[nodiscard]] constexpr Vec3 sub(Vec3 a, Vec3 b) noexcept {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

// Scales a vector by a scalar.
[[nodiscard]] constexpr Vec3 mul(Vec3 value, float scale) noexcept {
    return Vec3{value.x * scale, value.y * scale, value.z * scale};
}

// Returns the dot product of two three-component vectors.
[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Returns the cross product of two three-component vectors.
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Returns the squared vector length.
[[nodiscard]] constexpr float length_squared(Vec3 value) noexcept {
    return dot(value, value);
}

// Returns the vector length.
[[nodiscard]] float length(Vec3 value) noexcept;

// Returns a normalized vector or an error when the input is zero length.
[[nodiscard]] std::optional<Vec3> normalize(Vec3 value, std::string& error);

// Packs a vector into the array shape used by tests and uniform helpers.
[[nodiscard]] constexpr std::array<float, 4> pack_vec4(Vec4 value) noexcept {
    return {value.x, value.y, value.z, value.w};
}

} // namespace ofg::math
