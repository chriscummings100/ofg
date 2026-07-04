// Doctest coverage for deterministic C++ bootstrap scene data.
//
// The triangle is now legacy layout data, while the clear color remains the
// renderer's pre-sky clear baseline.
#include "doctest.h"

#include "ofg/render/bootstrap_scene.hpp"

#include <array>
#include <cstdint>

// Verifies vertex positions and colors stay aligned with the legacy baseline triangle.
TEST_CASE("bootstrap scene matches the baseline triangle contract") {
    const auto& vertices = ofg::bootstrap_vertices();

    REQUIRE(vertices.size() == 3);
    CHECK(vertices[0].m_position[0] == doctest::Approx(-0.72F));
    CHECK(vertices[0].m_position[1] == doctest::Approx(-0.58F));
    CHECK(vertices[0].m_color[0] == doctest::Approx(1.0F));
    CHECK(vertices[0].m_color[1] == doctest::Approx(0.05F));
    CHECK(vertices[0].m_color[2] == doctest::Approx(0.04F));

    CHECK(vertices[1].m_position[0] == doctest::Approx(0.72F));
    CHECK(vertices[1].m_position[1] == doctest::Approx(-0.58F));
    CHECK(vertices[1].m_color[0] == doctest::Approx(0.05F));
    CHECK(vertices[1].m_color[1] == doctest::Approx(0.95F));
    CHECK(vertices[1].m_color[2] == doctest::Approx(0.18F));

    CHECK(vertices[2].m_position[0] == doctest::Approx(0.0F));
    CHECK(vertices[2].m_position[1] == doctest::Approx(0.7F));
    CHECK(vertices[2].m_color[0] == doctest::Approx(0.08F));
    CHECK(vertices[2].m_color[1] == doctest::Approx(0.28F));
    CHECK(vertices[2].m_color[2] == doctest::Approx(1.0F));
}

// Verifies the C++ vertex layout matches the WGSL position/color attributes.
TEST_CASE("bootstrap vertex layout matches WGSL attributes") {
    CHECK(ofg::bootstrap_vertex_stride_bytes() == 20);
    CHECK(ofg::bootstrap_vertex_position_offset() == 0);
    CHECK(ofg::bootstrap_vertex_color_offset() == 8);
}

// Verifies the C++ clear color keeps the dark bootstrap baseline.
TEST_CASE("bootstrap clear color keeps bootstrap baseline") {
    CHECK(ofg::clear_color_rgba8() == std::array<std::uint8_t, 4>{27, 37, 50, 255});

    const ofg::ClearColor color = ofg::clear_color();
    CHECK(color.m_r == doctest::Approx(27.0 / 255.0));
    CHECK(color.m_g == doctest::Approx(37.0 / 255.0));
    CHECK(color.m_b == doctest::Approx(50.0 / 255.0));
    CHECK(color.m_a == doctest::Approx(1.0));
}
