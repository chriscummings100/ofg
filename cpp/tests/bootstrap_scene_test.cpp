// Doctest coverage for deterministic C++ bootstrap scene data.
//
// These tests make the browser C++ smoke and native Dawn smoke share the same
// triangle geometry, vertex layout, and clear color expectations.
#include "doctest.h"

#include "ofg/render/bootstrap_scene.hpp"

#include <array>
#include <cstdint>

// Verifies vertex positions and colors stay aligned with the baseline triangle.
TEST_CASE("bootstrap scene matches the baseline triangle contract") {
  const auto& vertices = ofg::bootstrap_vertices();

  REQUIRE(vertices.size() == 3);
  CHECK(vertices[0].position[0] == doctest::Approx(-0.72F));
  CHECK(vertices[0].position[1] == doctest::Approx(-0.58F));
  CHECK(vertices[0].color[0] == doctest::Approx(1.0F));
  CHECK(vertices[0].color[1] == doctest::Approx(0.05F));
  CHECK(vertices[0].color[2] == doctest::Approx(0.04F));

  CHECK(vertices[1].position[0] == doctest::Approx(0.72F));
  CHECK(vertices[1].position[1] == doctest::Approx(-0.58F));
  CHECK(vertices[1].color[0] == doctest::Approx(0.05F));
  CHECK(vertices[1].color[1] == doctest::Approx(0.95F));
  CHECK(vertices[1].color[2] == doctest::Approx(0.18F));

  CHECK(vertices[2].position[0] == doctest::Approx(0.0F));
  CHECK(vertices[2].position[1] == doctest::Approx(0.7F));
  CHECK(vertices[2].color[0] == doctest::Approx(0.08F));
  CHECK(vertices[2].color[1] == doctest::Approx(0.28F));
  CHECK(vertices[2].color[2] == doctest::Approx(1.0F));
}

// Verifies the C++ vertex layout matches the WGSL position/color attributes.
TEST_CASE("bootstrap vertex layout matches WGSL attributes") {
  CHECK(ofg::bootstrap_vertex_stride_bytes() == 20);
  CHECK(ofg::bootstrap_vertex_position_offset() == 0);
  CHECK(ofg::bootstrap_vertex_color_offset() == 8);
}

// Verifies the C++ clear color matches the shared smoke-contract bytes.
TEST_CASE("bootstrap clear color matches smoke contract") {
  CHECK(ofg::clear_color_rgba8() == std::array<std::uint8_t, 4>{27, 37, 50, 255});

  const ofg::ClearColor color = ofg::clear_color();
  CHECK(color.r == doctest::Approx(27.0 / 255.0));
  CHECK(color.g == doctest::Approx(37.0 / 255.0));
  CHECK(color.b == doctest::Approx(50.0 / 255.0));
  CHECK(color.a == doctest::Approx(1.0));
}
