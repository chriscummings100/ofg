// Doctest coverage for opaque pipeline cache key behavior.
#include "doctest.h"

#include "ofg/render/pipeline_cache.hpp"

#include <cstdint>
#include <string>

namespace {

// Produces a non-null fake bind group layout handle that is never dereferenced.
WGPUBindGroupLayout fake_bind_group_layout(std::uintptr_t value) noexcept {
    return reinterpret_cast<WGPUBindGroupLayout>(value);
}

// Produces a non-null fake device handle that is never dereferenced for invalid-input tests.
WGPUDevice fake_device(std::uintptr_t value) noexcept {
    return reinterpret_cast<WGPUDevice>(value);
}

// Produces a non-null fake shader module handle that is never dereferenced for invalid-input tests.
WGPUShaderModule fake_shader_module(std::uintptr_t value) noexcept {
    return reinterpret_cast<WGPUShaderModule>(value);
}

} // namespace

// Verifies pipeline keys compare every render-state component.
TEST_CASE("pipeline cache key compares target formats material layout and shader revision") {
    const ofg::PipelineKey key{
        WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Depth24Plus, fake_bind_group_layout(1), 7};

    CHECK(key ==
          ofg::PipelineKey{WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Depth24Plus, fake_bind_group_layout(1), 7});
    CHECK((key ==
              ofg::PipelineKey{
                  WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_Depth24Plus, fake_bind_group_layout(1), 7}) == false);
    CHECK(
        (key ==
            ofg::PipelineKey{
                WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Depth32Float, fake_bind_group_layout(1), 7}) == false);
    CHECK((key ==
              ofg::PipelineKey{
                  WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Depth24Plus, fake_bind_group_layout(2), 7}) == false);
    CHECK((key ==
              ofg::PipelineKey{
                  WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Depth24Plus, fake_bind_group_layout(1), 8}) == false);
}

// Verifies invalid pipeline creation inputs are rejected before WebGPU calls.
TEST_CASE("pipeline cache rejects incomplete creation inputs") {
    ofg::PipelineCache cache;
    std::string error;
    const ofg::PipelineKey key{
        WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Depth24Plus, fake_bind_group_layout(1), 1};

    CHECK(cache.get_or_create(
              nullptr, key, fake_bind_group_layout(2), fake_bind_group_layout(3), fake_shader_module(4), error) ==
          nullptr);
    CHECK(error.find("requires device") != std::string::npos);
    CHECK(cache.counters().m_pipeline_create_count == 0);

    const ofg::PipelineKey bad_format{
        WGPUTextureFormat_Undefined, WGPUTextureFormat_Depth24Plus, fake_bind_group_layout(1), 1};
    CHECK(cache.get_or_create(fake_device(5),
              bad_format,
              fake_bind_group_layout(2),
              fake_bind_group_layout(3),
              fake_shader_module(4),
              error) == nullptr);
    CHECK(error.find("defined color") != std::string::npos);
}

// Verifies moving empty caches transfers counters without duplicating ownership.
TEST_CASE("pipeline cache supports move construction and assignment") {
    ofg::PipelineCache source;
    ofg::PipelineCache moved(std::move(source));
    CHECK(moved.counters().m_pipeline_create_count == 0);

    ofg::PipelineCache assigned;
    assigned = std::move(moved);
    CHECK(assigned.counters().m_pipeline_create_count == 0);
}
