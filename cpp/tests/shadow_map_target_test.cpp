// Doctest coverage for the cascaded shadow-map texture array target.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/render/shadow_map_target.hpp"

#include <optional>
#include <string>
#include <utility>

namespace {

// Creates a test GPU context or fails the current doctest.
ofg::tests::TestGpuContext make_test_gpu() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);
    return std::move(*gpu);
}

} // namespace

// Verifies shadow map array allocation, per-cascade views, sampler lifetime, and resize reuse.
TEST_CASE("shadow map target owns stable cascade views and sampler") {
    CHECK(ofg::ShadowMapTarget::cascade_count() == 3U);
    CHECK(ofg::ShadowMapTarget::format() == WGPUTextureFormat_Depth32Float);
    CHECK_THROWS_WITH_AS(ofg::ShadowMapTarget(ofg::GpuContext{}), doctest::Contains("WebGPU device"), ofg::EngineError);

    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::ShadowMapTarget target(gpu.borrowed_context());
    CHECK(target.sampler() != nullptr);
    CHECK(target.sampling_view() == nullptr);
    CHECK(target.size() == 0U);
    CHECK(target.estimated_depth_bytes() == 0U);

    target.resize(128U);
    CHECK(target.size() == 128U);
    CHECK(target.sampling_view() != nullptr);
    CHECK(target.sampler() != nullptr);
    CHECK(target.render_view(0) != nullptr);
    CHECK(target.render_view(1) != nullptr);
    CHECK(target.render_view(2) != nullptr);
    CHECK(target.estimated_depth_bytes() == 128ULL * 128ULL * 3ULL * 4ULL);
    CHECK(target.view_generation() == 1U);
    CHECK(target.counters().m_texture_create_count == 1U);
    CHECK(target.counters().m_texture_view_create_count == 4U);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)target.render_view(3); }()), doctest::Contains("out of range"), ofg::EngineError);

    target.resize(128U);
    CHECK(target.view_generation() == 1U);
    CHECK(target.counters().m_texture_create_count == 1U);
    CHECK(target.counters().m_texture_view_create_count == 4U);

    target.resize(64U);
    CHECK(target.size() == 64U);
    CHECK(target.view_generation() == 2U);
    CHECK(target.counters().m_texture_create_count == 2U);
    CHECK(target.counters().m_texture_view_create_count == 8U);

    target.resize(0U);
    CHECK(target.size() == 0U);
    CHECK(target.sampling_view() == nullptr);
    CHECK(target.render_view(0) == nullptr);
    CHECK(target.sampler() != nullptr);
    CHECK(target.view_generation() == 3U);

    target.release();
    CHECK(target.sampler() == nullptr);
    target.resize(32U);
    CHECK(target.sampler() != nullptr);
    CHECK(target.sampling_view() != nullptr);
    CHECK(target.view_generation() == 4U);
}

// Verifies move operations transfer owned handles without double-releasing them.
TEST_CASE("shadow map target moves texture views and sampler ownership") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::ShadowMapTarget source(gpu.borrowed_context());
    source.resize(32U);
    const ofg::RendererCounters source_counters = source.counters();

    ofg::ShadowMapTarget moved(std::move(source));
    CHECK(source.size() == 0U);
    CHECK(source.sampler() == nullptr);
    CHECK(source.sampling_view() == nullptr);
    CHECK(moved.size() == 32U);
    CHECK(moved.sampler() != nullptr);
    CHECK(moved.sampling_view() != nullptr);
    CHECK(moved.render_view(0) != nullptr);
    CHECK(moved.view_generation() == 1U);
    CHECK(moved.counters().m_texture_create_count == source_counters.m_texture_create_count);
    CHECK(moved.counters().m_texture_view_create_count == source_counters.m_texture_view_create_count);

    ofg::ShadowMapTarget assigned(gpu.borrowed_context());
    assigned.resize(16U);
    assigned = std::move(moved);
    CHECK(moved.size() == 0U);
    CHECK(moved.sampler() == nullptr);
    CHECK(moved.sampling_view() == nullptr);
    CHECK(assigned.size() == 32U);
    CHECK(assigned.sampler() != nullptr);
    CHECK(assigned.sampling_view() != nullptr);
    CHECK(assigned.render_view(2) != nullptr);
    CHECK(assigned.view_generation() == 1U);
    CHECK(assigned.counters().m_texture_create_count == source_counters.m_texture_create_count);
}

// Verifies a default-constructed target cannot allocate without a borrowed GPU context.
TEST_CASE("shadow map target rejects resize before GPU context assignment") {
    ofg::ShadowMapTarget target;
    CHECK_THROWS_WITH_AS(target.resize(16U), doctest::Contains("sampler creation"), ofg::EngineError);
}
