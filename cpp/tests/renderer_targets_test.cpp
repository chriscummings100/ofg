// Doctest coverage for renderer-owned scene targets and tone-map helpers.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/render/depth_target.hpp"
#include "ofg/render/scene_color_target.hpp"
#include "ofg/render/tone_map_pass.hpp"

#include <limits>
#include <memory>
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

struct ScopedTexture {
    WGPUTexture m_value{nullptr};

    // Releases the temporary render texture.
    ~ScopedTexture() {
        if (m_value != nullptr) {
            wgpuTextureRelease(m_value);
        }
    }
};

struct ScopedTextureView {
    WGPUTextureView m_value{nullptr};

    // Releases the temporary render texture view.
    ~ScopedTextureView() {
        if (m_value != nullptr) {
            wgpuTextureViewRelease(m_value);
        }
    }
};

struct ScopedCommandEncoder {
    WGPUCommandEncoder m_value{nullptr};

    // Releases the encoder when a test exits before finish.
    ~ScopedCommandEncoder() {
        if (m_value != nullptr) {
            wgpuCommandEncoderRelease(m_value);
        }
    }
};

struct ScopedCommandBuffer {
    WGPUCommandBuffer m_value{nullptr};

    // Releases the finished command buffer.
    ~ScopedCommandBuffer() {
        if (m_value != nullptr) {
            wgpuCommandBufferRelease(m_value);
        }
    }
};

// Creates a tiny output texture for direct pass encoding tests.
ScopedTexture create_output_texture(WGPUDevice device, std::uint32_t width, std::uint32_t height) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = WGPUExtent3D{width, height, 1};
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;

    ScopedTexture texture{wgpuDeviceCreateTexture(device, &descriptor)};
    REQUIRE(texture.m_value != nullptr);
    return texture;
}

// Creates a default view for a test texture.
ScopedTextureView create_output_view(WGPUTexture texture) {
    ScopedTextureView view{wgpuTextureCreateView(texture, nullptr)};
    REQUIRE(view.m_value != nullptr);
    return view;
}

// Creates a command encoder for direct render-pass tests.
ScopedCommandEncoder create_encoder(WGPUDevice device) {
    ScopedCommandEncoder encoder{wgpuDeviceCreateCommandEncoder(device, nullptr)};
    REQUIRE(encoder.m_value != nullptr);
    return encoder;
}

} // namespace

// Verifies the HDR scene color target resize/release contract and counters.
TEST_CASE("scene color target resizes, reuses, and releases") {
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::SceneColorTarget(ofg::GpuContext{}); }()),
        doctest::Contains("WebGPU device"),
        ofg::EngineError);

    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::SceneColorTarget target(gpu.borrowed_context());

    CHECK(target.view() == nullptr);
    CHECK(target.width() == 0);
    CHECK(target.height() == 0);
    CHECK(target.view_generation() == 0);
    CHECK(ofg::SceneColorTarget::format() == WGPUTextureFormat_RGBA16Float);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)target.render_target(); }()), doctest::Contains("texture view"), ofg::EngineError);

    target.resize(16, 8);
    CHECK(target.view() != nullptr);
    CHECK(target.width() == 16);
    CHECK(target.height() == 8);
    CHECK(target.view_generation() == 1);
    CHECK(target.render_target().m_format == WGPUTextureFormat_RGBA16Float);
    CHECK(target.counters().m_texture_create_count == 1);
    CHECK(target.counters().m_texture_view_create_count == 1);

    target.resize(16, 8);
    CHECK(target.view_generation() == 1);
    CHECK(target.counters().m_texture_create_count == 1);
    CHECK(target.counters().m_texture_view_create_count == 1);

    target.resize(0, 8);
    CHECK(target.view() == nullptr);
    CHECK(target.width() == 0);
    CHECK(target.height() == 0);

    target.resize(4, 4);
    CHECK(target.view() != nullptr);
    CHECK(target.view_generation() == 2);
    CHECK(target.counters().m_texture_create_count == 2);
    CHECK(target.counters().m_texture_view_create_count == 2);

    ofg::SceneColorTarget moved(std::move(target));
    CHECK(target.view() == nullptr);
    CHECK(moved.view() != nullptr);
    CHECK(moved.width() == 4);
    CHECK(moved.height() == 4);
    CHECK(moved.view_generation() == 2);

    ofg::SceneColorTarget assigned(gpu.borrowed_context());
    assigned = std::move(moved);
    CHECK(moved.view() == nullptr);
    CHECK(assigned.view() != nullptr);
    assigned.release();
    CHECK(assigned.view() == nullptr);
    CHECK(assigned.width() == 0);
    CHECK(assigned.height() == 0);
}

// Verifies the shared depth target resize/release contract and counters.
TEST_CASE("depth target resizes, reuses, and releases") {
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::DepthTarget(ofg::GpuContext{}); }()), doctest::Contains("WebGPU device"), ofg::EngineError);

    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::DepthTarget target(gpu.borrowed_context());

    CHECK(target.view() == nullptr);
    CHECK(target.width() == 0);
    CHECK(target.height() == 0);
    CHECK(target.view_generation() == 0);
    CHECK(ofg::DepthTarget::format() == WGPUTextureFormat_Depth24Plus);

    target.resize(16, 8);
    CHECK(target.view() != nullptr);
    CHECK(target.width() == 16);
    CHECK(target.height() == 8);
    CHECK(target.view_generation() == 1);
    CHECK(target.counters().m_texture_create_count == 1);
    CHECK(target.counters().m_texture_view_create_count == 1);

    target.resize(16, 8);
    CHECK(target.view_generation() == 1);
    CHECK(target.counters().m_texture_create_count == 1);
    CHECK(target.counters().m_texture_view_create_count == 1);

    target.resize(16, 0);
    CHECK(target.view() == nullptr);
    CHECK(target.width() == 0);
    CHECK(target.height() == 0);

    target.resize(4, 4);
    CHECK(target.view() != nullptr);
    CHECK(target.view_generation() == 2);
    CHECK(target.counters().m_texture_create_count == 2);
    CHECK(target.counters().m_texture_view_create_count == 2);

    ofg::DepthTarget moved(std::move(target));
    CHECK(target.view() == nullptr);
    CHECK(moved.view() != nullptr);
    CHECK(moved.width() == 4);
    CHECK(moved.height() == 4);
    CHECK(moved.view_generation() == 2);

    ofg::DepthTarget assigned(gpu.borrowed_context());
    assigned = std::move(moved);
    CHECK(moved.view() == nullptr);
    CHECK(assigned.view() != nullptr);
    assigned.release();
    CHECK(assigned.view() == nullptr);
    CHECK(assigned.width() == 0);
    CHECK(assigned.height() == 0);
}

// Verifies platform formats pick the correct final color encoding mode.
TEST_CASE("tone map output encoding follows platform target format") {
    CHECK(ofg::tone_map_output_encoding_for(WGPUTextureFormat_RGBA8Unorm) == ofg::ToneMapOutputEncoding::ManualSrgb);
    CHECK(ofg::tone_map_output_encoding_for(WGPUTextureFormat_BGRA8Unorm) == ofg::ToneMapOutputEncoding::ManualSrgb);
    CHECK(ofg::tone_map_output_encoding_for(WGPUTextureFormat_RGBA8UnormSrgb) ==
          ofg::ToneMapOutputEncoding::LinearOutput);
    CHECK(ofg::tone_map_output_encoding_for(WGPUTextureFormat_BGRA8UnormSrgb) ==
          ofg::ToneMapOutputEncoding::LinearOutput);
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::tone_map_output_encoding_for(WGPUTextureFormat_Undefined); }()),
        doctest::Contains("does not support"),
        ofg::EngineError);
}

// Verifies tone-map pass resource creation, validation, and bind-group reuse.
TEST_CASE("tone map pass creates validates and renders") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::ToneMapPass::create(
            ofg::GpuContext{}, WGPUTextureFormat_RGBA8Unorm, ofg::ToneMapOutputEncoding::ManualSrgb);
    }()),
        doctest::Contains("WebGPU device"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::ToneMapPass::create(
            gpu.borrowed_context(), WGPUTextureFormat_Undefined, ofg::ToneMapOutputEncoding::ManualSrgb);
    }()),
        doctest::Contains("does not support"),
        ofg::EngineError);

    std::unique_ptr<ofg::ToneMapPass> pass = ofg::ToneMapPass::create(
        gpu.borrowed_context(), WGPUTextureFormat_RGBA8Unorm, ofg::ToneMapOutputEncoding::ManualSrgb);
    REQUIRE(pass != nullptr);
    CHECK(pass->counters().m_shader_module_create_count == 1);
    CHECK(pass->counters().m_bind_group_layout_create_count == 1);
    CHECK(pass->counters().m_pipeline_create_count == 1);
    CHECK(pass->counters().m_buffer_create_count == 1);
    CHECK(pass->counters().m_bind_group_create_count == 0);

    pass->set_exposure(1.25f);
    CHECK(pass->exposure() == doctest::Approx(1.25f));
    CHECK_THROWS_WITH_AS(([&]() { pass->set_exposure(std::numeric_limits<float>::quiet_NaN()); }()),
        doctest::Contains("finite"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { pass->set_exposure(-0.1f); }()), doctest::Contains("non-negative"), ofg::EngineError);

    ofg::SceneColorTarget scene_color(gpu.borrowed_context());
    scene_color.resize(4, 4);
    ScopedTexture output_texture = create_output_texture(gpu.borrowed_context().m_device, 4, 4);
    ScopedTextureView output_view = create_output_view(output_texture.m_value);
    ofg::RenderTarget output_target{output_view.m_value, WGPUTextureFormat_RGBA8Unorm, 4, 4};
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    CHECK_THROWS_WITH_AS(([&]() { pass->render(nullptr, scene_color.view(), output_target); }()),
        doctest::Contains("requires"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { pass->render(encoder.m_value, nullptr, output_target); }()),
        doctest::Contains("requires"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        pass->render(
            encoder.m_value, scene_color.view(), ofg::RenderTarget{nullptr, WGPUTextureFormat_RGBA8Unorm, 4, 4});
    }()),
        doctest::Contains("requires"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        pass->render(encoder.m_value,
            scene_color.view(),
            ofg::RenderTarget{output_view.m_value, WGPUTextureFormat_RGBA8Unorm, 0, 4});
    }()),
        doctest::Contains("nonzero"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        pass->render(encoder.m_value,
            scene_color.view(),
            ofg::RenderTarget{output_view.m_value, WGPUTextureFormat_BGRA8Unorm, 4, 4});
    }()),
        doctest::Contains("does not match"),
        ofg::EngineError);

    pass->render(encoder.m_value, scene_color.view(), output_target);
    CHECK(pass->counters().m_bind_group_create_count == 1);
    pass->render(encoder.m_value, scene_color.view(), output_target);
    CHECK(pass->counters().m_bind_group_create_count == 1);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.m_value, nullptr);
    encoder.m_value = nullptr;
    REQUIRE(command != nullptr);
    ScopedCommandBuffer command_buffer{command};
}
