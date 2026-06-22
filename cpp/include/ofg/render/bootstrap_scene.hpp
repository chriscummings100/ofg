// Deterministic bootstrap triangle scene data shared by C++ render paths.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ofg {

struct BootstrapVertex {
    // Clip-space XY position consumed by the bootstrap vertex shader.
    std::array<float, 2> m_position;
    // Linear RGB color consumed by the bootstrap fragment shader.
    std::array<float, 3> m_color;
};

struct ClearColor {
    // Normalized red channel.
    double m_r;
    // Normalized green channel.
    double m_g;
    // Normalized blue channel.
    double m_b;
    // Normalized alpha channel.
    double m_a;
};

// Returns the deterministic RGB triangle used by browser and native smokes.
[[nodiscard]] const std::array<BootstrapVertex, 3>& bootstrap_vertices() noexcept;
// Returns the byte stride of BootstrapVertex for WebGPU vertex buffers.
[[nodiscard]] constexpr std::size_t bootstrap_vertex_stride_bytes() noexcept {
    return sizeof(BootstrapVertex);
}
// Returns the byte offset of the position attribute inside BootstrapVertex.
[[nodiscard]] constexpr std::size_t bootstrap_vertex_position_offset() noexcept {
    return offsetof(BootstrapVertex, m_position);
}
// Returns the byte offset of the color attribute inside BootstrapVertex.
[[nodiscard]] constexpr std::size_t bootstrap_vertex_color_offset() noexcept {
    return offsetof(BootstrapVertex, m_color);
}
// Returns the clear color in byte form for smoke-contract comparison.
[[nodiscard]] constexpr std::array<std::uint8_t, 4> clear_color_rgba8() noexcept {
    return {27, 37, 50, 255};
}
// Returns the clear color in normalized double form for WebGPU descriptors.
[[nodiscard]] constexpr ClearColor clear_color() noexcept {
    return ClearColor{27.0 / 255.0, 37.0 / 255.0, 50.0 / 255.0, 1.0};
}

} // namespace ofg
