// Native Dawn render-smoke contract and entry point.
//
// The smoke command builds a Clang-native executable that renders the same
// bootstrap triangle through Dawn/WebGPU outside the browser. The caller passes
// the shared smoke contract from tools/smoke-contract.json so this executable
// can produce the same PNG and pixel report shape as the original native harness.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ofg::native {

struct SmokeContract {
  // Output width in pixels.
  std::uint32_t width{800};
  // Output height in pixels.
  std::uint32_t height{450};
  // Expected background clear color in RGBA8 order.
  std::vector<std::uint8_t> clear_color_rgba8{27, 37, 50, 255};
  // Pixel sampling stride used for report generation.
  std::uint32_t sample_step{3};
  // Maximum RGB distance for a pixel to count as background.
  double color_distance_tolerance{26.0};
  // Color bucket divisor used to count non-background color variety.
  std::uint32_t bucket_divisor{64};
  // Minimum sampled-pixel ratio that must look like triangle geometry.
  double min_triangle_ratio{0.05};
  // Minimum sampled-pixel ratio that must look like cleared background.
  double min_background_ratio{0.4};
  // Minimum number of non-background color buckets expected from interpolation.
  std::uint32_t min_non_background_color_buckets{3};
};

struct RenderSmokeOptions {
  // Directory that receives bootstrap.png and report.json.
  std::filesystem::path out_dir{"artifacts/render-smoke"};
  // Visual contract thresholds forwarded by the Node wrapper.
  SmokeContract contract{};
};

// Parses command-line options emitted by tools/smoke-render-cpp.mjs.
[[nodiscard]] RenderSmokeOptions parse_render_smoke_args(int argc, char** argv);

// Renders the bootstrap frame, writes PNG/report artifacts, and fails on bad pixels.
void run_render_smoke(const RenderSmokeOptions& options);

} // namespace ofg::native
