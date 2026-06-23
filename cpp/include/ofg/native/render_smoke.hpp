// Native Dawn render-smoke contract and entry point.
//
// The smoke command builds a Clang-native executable that renders the shared
// plane-and-cubes demo scene through Dawn/WebGPU outside the browser. The caller
// passes the shared smoke contract from tools/smoke-contract.json so this
// executable can produce the same PNG and pixel report shape as browser smoke.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ofg::native {

struct SmokeContract {
    // Output width in pixels.
    std::uint32_t m_width{800};
    // Output height in pixels.
    std::uint32_t m_height{450};
    // Expected background clear color in RGBA8 order.
    std::vector<std::uint8_t> m_clear_color_rgba8{27, 37, 50, 255};
    // Pixel sampling stride used for report generation.
    std::uint32_t m_sample_step{3};
    // Maximum RGB distance for a pixel to count as background.
    double m_color_distance_tolerance{26.0};
    // Color bucket divisor used to count non-background color variety.
    std::uint32_t m_bucket_divisor{64};
    // Minimum sampled-pixel ratio that must contain non-background geometry.
    double m_min_scene_ratio{0.12};
    // Minimum sampled-pixel ratio that must look like cleared background.
    double m_min_background_ratio{0.25};
    // Minimum sampled-pixel ratio that must look like neutral checker ground.
    double m_min_ground_ratio{0.04};
    // Minimum sampled-pixel ratio that must look like saturated cube colors.
    double m_min_colored_ratio{0.01};
    // Minimum lower-half sampled-pixel ratio that must contain scene geometry.
    double m_min_lower_half_scene_ratio{0.08};
    // Minimum number of non-background color buckets expected from texture/filtering.
    std::uint32_t m_min_non_background_color_buckets{4};
};

struct RenderSmokeOptions {
    // Directory that receives opaque-demo.png and report.json.
    std::filesystem::path m_out_dir{"artifacts/render-smoke"};
    // Visual contract thresholds forwarded by the Node wrapper.
    SmokeContract m_contract{};
};

// Parses command-line options emitted by tools/smoke-render-cpp.mjs.
[[nodiscard]] RenderSmokeOptions parse_render_smoke_args(int argc, char** argv);

// Renders the demo frame, writes PNG/report artifacts, and fails on bad pixels.
void run_render_smoke(const RenderSmokeOptions& options);

} // namespace ofg::native
