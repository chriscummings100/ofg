// Doctest coverage for the static temporary render-target system.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/render/temp_buffer.hpp"

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

// Cleans the static TempBuffer singleton after each test.
struct TempBufferReset {
    // Releases and destroys the TempBuffer singleton on scope exit.
    ~TempBufferReset() {
        while (!ofg::TempBuffer::release()) {}
        ofg::TempBuffer::destroy();
    }
};

// Returns the default bloom-compatible temporary target descriptor.
ofg::TempBufferDesc make_desc(std::uint32_t width = 8, std::uint32_t height = 4) {
    ofg::TempBufferDesc desc;
    desc.m_width = width;
    desc.m_height = height;
    desc.m_format = WGPUTextureFormat_RGBA16Float;
    desc.m_usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    return desc;
}

} // namespace

// Verifies TempBuffer lifecycle errors and no-op teardown behavior.
TEST_CASE("temp buffer lifecycle validates singleton use") {
    TempBufferReset reset;

    CHECK(ofg::TempBuffer::release());
    ofg::TempBuffer::destroy();
    CHECK_THROWS_WITH_AS(
        ([]() { ofg::TempBuffer::begin_frame(); }()), doctest::Contains("create first"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(([]() { (void)ofg::TempBuffer::get(make_desc(), "missing"); }()),
        doctest::Contains("create first"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([]() { ofg::TempBuffer::end_frame(); }()), doctest::Contains("create first"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([]() { ofg::TempBuffer::create(ofg::GpuContext{}); }()), doctest::Contains("WebGPU device"), ofg::EngineError);

    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());
    CHECK_THROWS_WITH_AS(([&]() { ofg::TempBuffer::create(gpu.borrowed_context()); }()),
        doctest::Contains("singleton"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([]() { (void)ofg::TempBuffer::get(make_desc(), "before frame"); }()),
        doctest::Contains("begin_frame"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([]() { ofg::TempBuffer::end_frame(); }()), doctest::Contains("active"), ofg::EngineError);

    ofg::TempBuffer::begin_frame();
    CHECK_THROWS_WITH_AS(([]() { ofg::TempBuffer::begin_frame(); }()), doctest::Contains("active"), ofg::EngineError);
    ofg::TempBuffer::end_frame();
    CHECK(ofg::TempBuffer::release());
    CHECK_THROWS_WITH_AS(
        ([]() { ofg::TempBuffer::begin_frame(); }()), doctest::Contains("after TempBuffer::release"), ofg::EngineError);
}

// Verifies descriptor validation rejects unsupported temporary targets.
TEST_CASE("temp buffer descriptor validation is explicit") {
    ofg::TempBufferDesc desc = make_desc();
    CHECK_NOTHROW(ofg::validate_temp_buffer_desc(desc));

    desc = make_desc(0, 4);
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("nonzero"), ofg::EngineError);
    desc = make_desc();
    desc.m_format = WGPUTextureFormat_Undefined;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("defined"), ofg::EngineError);
    desc = make_desc();
    desc.m_usage = 0;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("usage"), ofg::EngineError);
    desc = make_desc();
    desc.m_usage = WGPUTextureUsage_TextureBinding;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("RenderAttachment"), ofg::EngineError);
    desc = make_desc();
    desc.m_usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_StorageBinding;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("unsupported"), ofg::EngineError);
    desc = make_desc();
    desc.m_mip_level_count = 0;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("mip"), ofg::EngineError);
    desc = make_desc();
    desc.m_array_layer_count = 0;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("array"), ofg::EngineError);
    desc = make_desc();
    desc.m_sample_count = 0;
    CHECK_THROWS_WITH_AS(
        ([&]() { ofg::validate_temp_buffer_desc(desc); }()), doctest::Contains("sample"), ofg::EngineError);
}

// Verifies early return makes a descriptor-matched texture immediately reusable.
TEST_CASE("temp buffer get supports explicit early return and same-frame reuse") {
    TempBufferReset reset;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());

    const ofg::TempBufferDesc desc = make_desc();
    ofg::TempBuffer::begin_frame();
    ofg::TempBufferRef first = ofg::TempBuffer::get(desc, "first");
    REQUIRE(first.valid());
    CHECK(first.width() == 8);
    CHECK(first.height() == 4);
    CHECK(first.format() == WGPUTextureFormat_RGBA16Float);
    CHECK(first.render_target().m_view == first.view());
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 1);
    CHECK(ofg::TempBuffer::counters().m_texture_view_create_count == 1);
    CHECK(ofg::TempBuffer::stats().m_active_count == 1);
    CHECK(ofg::TempBuffer::stats().m_active_bytes == 8U * 4U * 8U);

    ofg::TempBufferRef second = ofg::TempBuffer::get(desc, "second");
    REQUIRE(second.valid());
    CHECK(second.view() != first.view());
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 2);
    CHECK(ofg::TempBuffer::stats().m_active_count == 2);

    WGPUTextureView first_view = first.view();
    ofg::TempBuffer::release(first);
    CHECK_FALSE(first.valid());
    CHECK(first.view() == nullptr);
    CHECK(ofg::TempBuffer::stats().m_active_count == 1);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 1);
    CHECK(ofg::TempBuffer::stats().m_early_release_count == 1);
    ofg::TempBuffer::release(first);
    CHECK(ofg::TempBuffer::stats().m_early_release_count == 1);

    ofg::TempBufferRef reused = ofg::TempBuffer::get(desc, "reused");
    REQUIRE(reused.valid());
    CHECK(reused.view() == first_view);
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 2);
    CHECK(ofg::TempBuffer::stats().m_reused_count == 1);
    CHECK_FALSE(first.valid());

    ofg::TempBuffer::release(second);
    ofg::TempBuffer::release(reused);
    ofg::TempBuffer::end_frame();
    CHECK(ofg::TempBuffer::stats().m_active_count == 0);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 2);
}

// Verifies frame-end return invalidates handles and permits next-frame reuse.
TEST_CASE("temp buffer end frame returns active buffers automatically") {
    TempBufferReset reset;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());

    ofg::TempBuffer::begin_frame();
    ofg::TempBufferRef first = ofg::TempBuffer::get(make_desc(), "auto");
    REQUIRE(first.valid());
    WGPUTextureView first_view = first.view();
    ofg::TempBuffer::end_frame();
    CHECK_FALSE(first.valid());
    CHECK(first.view() == nullptr);
    CHECK(ofg::TempBuffer::stats().m_end_frame_return_count == 1);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 1);

    ofg::TempBuffer::begin_frame();
    ofg::TempBufferRef reused = ofg::TempBuffer::get(make_desc(), "next frame");
    CHECK(reused.view() == first_view);
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 1);
    CHECK(ofg::TempBuffer::stats().m_reused_count == 1);
    ofg::TempBuffer::release(reused);
    ofg::TempBuffer::end_frame();
}

// Verifies exact descriptor matching and stale cleanup behavior.
TEST_CASE("temp buffer exact reuse and stale cleanup are deterministic") {
    TempBufferReset reset;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());

    ofg::TempBuffer::begin_frame();
    ofg::TempBufferRef first = ofg::TempBuffer::get(make_desc(8, 4), "old size");
    ofg::TempBuffer::release(first);
    ofg::TempBufferRef different = ofg::TempBuffer::get(make_desc(4, 4), "new size");
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 2);
    ofg::TempBuffer::release(different);
    ofg::TempBuffer::end_frame();

    for (int frame = 0; frame < 9; ++frame) {
        ofg::TempBuffer::begin_frame();
        CHECK(ofg::TempBuffer::stats().m_discarded_count == 0);
        CHECK(ofg::TempBuffer::stats().m_reusable_count == 2);
        ofg::TempBuffer::end_frame();
    }

    ofg::TempBuffer::begin_frame();
    CHECK(ofg::TempBuffer::stats().m_discarded_count == 2);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 0);
    ofg::TempBuffer::end_frame();
}

// Verifies lifecycle release clears live resources while preserving counters.
TEST_CASE("temp buffer lifecycle release clears resources and preserves diagnostics") {
    TempBufferReset reset;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::TempBuffer::create(gpu.borrowed_context());

    ofg::TempBuffer::begin_frame();
    ofg::TempBufferRef active = ofg::TempBuffer::get(make_desc(), "release");
    REQUIRE(active.valid());
    CHECK(ofg::TempBuffer::release());
    CHECK_FALSE(active.valid());
    CHECK(ofg::TempBuffer::stats().m_active_count == 0);
    CHECK(ofg::TempBuffer::stats().m_reusable_count == 0);
    CHECK(ofg::TempBuffer::stats().m_created_count == 1);
    CHECK(ofg::TempBuffer::stats().m_peak_bytes == 8U * 4U * 8U);
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 1);

    ofg::TempBufferRef invalid;
    ofg::TempBuffer::release(invalid);
    CHECK_FALSE(invalid.valid());

    ofg::TempBuffer::destroy();
    CHECK(ofg::TempBuffer::stats().m_created_count == 0);
    CHECK(ofg::TempBuffer::counters().m_texture_create_count == 0);
}
