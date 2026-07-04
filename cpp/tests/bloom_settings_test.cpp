// Doctest coverage for bloom settings, pyramid planning, and uniform packing.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/bloom_settings.hpp"

#include <limits>

// Verifies the first authored bloom defaults are visible and deterministic.
TEST_CASE("bloom settings defaults are visible and deterministic") {
    const ofg::BloomSettings settings = ofg::default_bloom_settings();
    CHECK(settings.m_enabled);
    CHECK(settings.m_threshold == doctest::Approx(0.6f));
    CHECK(settings.m_soft_knee == doctest::Approx(0.75f));
    CHECK(settings.m_intensity == doctest::Approx(0.35f));
    CHECK(settings.m_scatter == doctest::Approx(0.85f));
    CHECK(settings.m_clamp == doctest::Approx(64.0f));
    CHECK(settings.m_tint.x == doctest::Approx(1.0f));
    CHECK(settings.m_tint.y == doctest::Approx(1.0f));
    CHECK(settings.m_tint.z == doctest::Approx(1.0f));
    CHECK(settings.m_initial_downscale == 2U);
    CHECK(settings.m_max_levels == 6U);
    CHECK(settings.m_min_level_extent == 2U);
    CHECK_NOTHROW(ofg::validate_bloom_settings(settings));
}

// Verifies invalid bloom settings fail before GPU uniform packing.
TEST_CASE("bloom settings validation rejects invalid values") {
    ofg::BloomSettings settings = ofg::default_bloom_settings();

    settings.m_threshold = -0.1f;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("threshold"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_soft_knee = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("soft knee"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_intensity = -1.0f;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("intensity"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_scatter = 1.1f;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("scatter"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_scatter = std::numeric_limits<float>::infinity();
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("scatter"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_clamp = -1.0f;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("clamp"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_tint.y = -0.1f;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("tint green"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_tint.z = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("tint blue"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_initial_downscale = 3U;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("2 or 4"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_max_levels = 0U;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("max_levels"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_max_levels = ofg::max_bloom_pyramid_levels + 1U;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("max_levels"), ofg::EngineError);
    settings = ofg::default_bloom_settings();
    settings.m_min_level_extent = 0U;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_bloom_settings(settings); }()), doctest::Contains("min_level_extent"), ofg::EngineError);
}

// Verifies hard and soft bloom threshold math.
TEST_CASE("bloom prefilter contribution handles hard and soft thresholds") {
    CHECK(ofg::bloom_prefilter_contribution(-2.0f, 1.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(ofg::bloom_prefilter_contribution(0.5f, 1.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(ofg::bloom_prefilter_contribution(1.0f, 1.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(ofg::bloom_prefilter_contribution(2.0f, 1.0f, 0.0f) == doctest::Approx(0.5f));
    CHECK(ofg::bloom_prefilter_contribution(4.0f, 1.0f, 0.0f) == doctest::Approx(0.75f));

    const float soft_below_threshold = ofg::bloom_prefilter_contribution(0.75f, 1.0f, 0.5f);
    CHECK(soft_below_threshold > 0.0f);
    CHECK(soft_below_threshold < 1.0f);
    CHECK(ofg::bloom_prefilter_contribution(2.0f, 1.0f, 0.5f) == doctest::Approx(0.5f));

    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::bloom_prefilter_contribution(std::numeric_limits<float>::infinity(), 1.0f, 0.5f); }()),
        doctest::Contains("brightness"),
        ofg::EngineError);
}

// Verifies pyramid sizing uses ceil division, capping, and tiny viewport skips.
TEST_CASE("bloom pyramid plan is deterministic") {
    ofg::BloomSettings settings = ofg::default_bloom_settings();
    ofg::BloomPyramidPlan plan = ofg::build_bloom_pyramid_plan(1920, 1080, settings);
    REQUIRE(plan.m_level_count == 6U);
    CHECK(plan.m_levels[0].m_width == 960U);
    CHECK(plan.m_levels[0].m_height == 540U);
    CHECK(plan.m_levels[1].m_width == 480U);
    CHECK(plan.m_levels[1].m_height == 270U);
    CHECK(plan.m_levels[2].m_width == 240U);
    CHECK(plan.m_levels[2].m_height == 135U);
    CHECK(plan.m_levels[3].m_width == 120U);
    CHECK(plan.m_levels[3].m_height == 68U);
    CHECK(plan.m_levels[4].m_width == 60U);
    CHECK(plan.m_levels[4].m_height == 34U);
    CHECK(plan.m_levels[5].m_width == 30U);
    CHECK(plan.m_levels[5].m_height == 17U);

    settings.m_max_levels = 3U;
    plan = ofg::build_bloom_pyramid_plan(17, 9, settings);
    REQUIRE(plan.m_level_count == 3U);
    CHECK(plan.m_levels[0].m_width == 9U);
    CHECK(plan.m_levels[0].m_height == 5U);
    CHECK(plan.m_levels[1].m_width == 5U);
    CHECK(plan.m_levels[1].m_height == 3U);
    CHECK(plan.m_levels[2].m_width == 3U);
    CHECK(plan.m_levels[2].m_height == 2U);

    settings = ofg::default_bloom_settings();
    settings.m_initial_downscale = 4U;
    plan = ofg::build_bloom_pyramid_plan(17, 9, settings);
    REQUIRE(plan.m_level_count == 2U);
    CHECK(plan.m_levels[0].m_width == 5U);
    CHECK(plan.m_levels[0].m_height == 3U);
    CHECK(plan.m_levels[1].m_width == 3U);
    CHECK(plan.m_levels[1].m_height == 2U);

    settings = ofg::default_bloom_settings();
    settings.m_min_level_extent = 3U;
    plan = ofg::build_bloom_pyramid_plan(4, 4, settings);
    CHECK(plan.empty());
    plan = ofg::build_bloom_pyramid_plan(0, 1080, settings);
    CHECK(plan.empty());
    settings.m_enabled = false;
    plan = ofg::build_bloom_pyramid_plan(1920, 1080, settings);
    CHECK(plan.empty());
}

// Verifies the packed uniform block has a stable CPU/WGSL layout.
TEST_CASE("bloom uniforms pack settings into aligned rows") {
    ofg::BloomSettings settings = ofg::default_bloom_settings();
    settings.m_enabled = false;
    settings.m_threshold = 1.5f;
    settings.m_soft_knee = 0.25f;
    settings.m_intensity = 0.2f;
    settings.m_scatter = 0.6f;
    settings.m_clamp = 32.0f;
    settings.m_tint = ofg::math::vec3(0.9f, 0.8f, 0.7f);
    settings.m_initial_downscale = 4U;
    settings.m_max_levels = 5U;
    settings.m_min_level_extent = 3U;

    const ofg::BloomUniformBlock block = ofg::pack_bloom_uniforms(settings);
    CHECK(alignof(ofg::BloomUniformBlock) == 16U);
    CHECK(sizeof(ofg::BloomUniformBlock) == sizeof(float) * 16U);
    CHECK(block.m_values[0] == doctest::Approx(1.5f));
    CHECK(block.m_values[1] == doctest::Approx(0.25f));
    CHECK(block.m_values[2] == doctest::Approx(0.2f));
    CHECK(block.m_values[3] == doctest::Approx(0.6f));
    CHECK(block.m_values[4] == doctest::Approx(32.0f));
    CHECK(block.m_values[5] == doctest::Approx(0.9f));
    CHECK(block.m_values[6] == doctest::Approx(0.8f));
    CHECK(block.m_values[7] == doctest::Approx(0.7f));
    CHECK(block.m_values[8] == doctest::Approx(4.0f));
    CHECK(block.m_values[9] == doctest::Approx(5.0f));
    CHECK(block.m_values[10] == doctest::Approx(3.0f));
    CHECK(block.m_values[11] == doctest::Approx(0.0f));
    CHECK(block.m_values[12] == doctest::Approx(0.0f));
    CHECK(block.m_values[15] == doctest::Approx(0.0f));
}
