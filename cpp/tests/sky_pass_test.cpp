// Doctest coverage for procedural sky pass CPU packing and GPU state creation.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/depth_target.hpp"
#include "ofg/render/lighting.hpp"
#include "ofg/render/scene_color_target.hpp"
#include "ofg/render/sky_pass.hpp"
#include "ofg/scene/environment.hpp"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace {

constexpr float _pi = 3.14159265358979323846f;

// Creates a test GPU context or fails the current doctest.
ofg::tests::TestGpuContext make_test_gpu() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);
    return std::move(*gpu);
}

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

// Returns a stable camera looking toward +Z for sky pass tests.
ofg::CameraProperties make_sky_camera() {
    return ofg::camera_properties_from_look_at(nullptr,
        ofg::math::vec3(0.0f, 1.0f, -4.0f),
        ofg::math::vec3(0.0f, 1.0f, 1.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        _pi * 0.5f,
        2.0f,
        0.1f,
        80.0f);
}

} // namespace

// Verifies the CPU-side uniform layout consumed by the procedural sky shader.
TEST_CASE("sky pass uniforms pack camera environment and sun light") {
    const ofg::CameraProperties camera = make_sky_camera();
    ofg::Environment environment;
    ofg::SkyWeather weather = environment.weather();
    weather.m_haze = 0.25f;
    weather.m_cloud_coverage = 0.4f;
    weather.m_storm_intensity = 0.2f;
    weather.m_precipitation_hint = 0.1f;
    weather.m_wind_direction = ofg::math::vec3(0.25f, 0.0f, 0.75f);
    weather.m_wind_speed = 4.0f;
    weather.m_cloud_scale = 0.001f;
    weather.m_cloud_height = 1400.0f;
    environment.set_weather(weather);

    std::array<ofg::LightProperties, 1> lights{ofg::LightProperties{ofg::LightPropertiesType::Directional,
        ofg::math::vec3(0.0f, -1.0f, 0.0f),
        ofg::math::vec3(1.0f, 0.8f, 0.6f),
        5.0f}};
    const ofg::SkyPassUniforms uniforms =
        ofg::build_sky_pass_uniforms(camera, environment, std::span<const ofg::LightProperties>(lights));

    CHECK(uniforms.m_values[0] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[3] == doctest::Approx(2.0f));
    CHECK(uniforms.m_values[7] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[10] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[13] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[16] == doctest::Approx(1.0f));
    CHECK(uniforms.m_values[17] == doctest::Approx(0.8f));
    CHECK(uniforms.m_values[18] == doctest::Approx(0.6f));
    CHECK(uniforms.m_values[19] == doctest::Approx(5.0f));
    CHECK(uniforms.m_values[23] == doctest::Approx(environment.moon_phase()));
    CHECK(uniforms.m_values[24] == doctest::Approx(environment.day_factor()));
    CHECK(uniforms.m_values[26] == doctest::Approx(0.25f));
    CHECK(uniforms.m_values[28] == doctest::Approx(0.4f));
    CHECK(uniforms.m_values[29] == doctest::Approx(0.2f));
    CHECK(uniforms.m_values[31] == doctest::Approx(0.1f));
    CHECK(uniforms.m_values[32] == doctest::Approx(0.25f));
    CHECK(uniforms.m_values[33] == doctest::Approx(0.75f));
    CHECK(uniforms.m_values[34] == doctest::Approx(4.0f));
    CHECK(uniforms.m_values[35] == doctest::Approx(0.001f));
    CHECK(uniforms.m_values[36] == doctest::Approx(1400.0f));
    CHECK(uniforms.m_values[38] == doctest::Approx(1337.0f));
}

// Verifies deterministic environment presets cover the first authored sky states.
TEST_CASE("environment presets produce deterministic sky states") {
    ofg::Environment environment;

    environment.apply_preset(ofg::EnvironmentPreset::Daylight);
    CHECK(environment.preset() == ofg::EnvironmentPreset::Daylight);
    CHECK(environment.day_factor() > 0.9f);
    CHECK(environment.weather().m_cloud_coverage == doctest::Approx(0.25f));
    CHECK(environment.star_seed() == 1337U);

    environment.apply_preset(ofg::EnvironmentPreset::Sunset);
    CHECK(environment.preset() == ofg::EnvironmentPreset::Sunset);
    CHECK(environment.twilight_factor() > 0.0f);
    CHECK(environment.weather().m_haze == doctest::Approx(0.18f));
    CHECK(environment.star_seed() == 2112U);

    environment.apply_preset(ofg::EnvironmentPreset::Night);
    CHECK(environment.preset() == ofg::EnvironmentPreset::Night);
    CHECK(environment.day_factor() == doctest::Approx(0.0f));
    CHECK(environment.moon_phase() == doctest::Approx(0.82f));
    CHECK(environment.star_seed() == 4242U);

    environment.apply_preset(ofg::EnvironmentPreset::Storm);
    CHECK(environment.preset() == ofg::EnvironmentPreset::Storm);
    CHECK(environment.weather().m_cloud_coverage > 0.8f);
    CHECK(environment.weather().m_storm_intensity > 0.8f);
    CHECK(environment.weather().m_wind_speed == doctest::Approx(18.0f));
    CHECK(environment.star_seed() == 9001U);
}

// Verifies invalid camera data is rejected before it reaches GPU uniforms.
TEST_CASE("sky pass uniform packing rejects invalid camera projection data") {
    ofg::CameraProperties camera = ofg::camera_properties_from_look_at(nullptr,
        ofg::math::vec3(0.0f, 0.0f, 0.0f),
        ofg::math::vec3(0.0f, 0.0f, 1.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        _pi * 0.5f,
        1.0f,
        0.1f,
        80.0f);
    ofg::Environment environment;

    camera.vertical_fov_radians = 0.0f;
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::build_sky_pass_uniforms(camera, environment, {}); }()),
        doctest::Contains("field of view"),
        ofg::EngineError);
}

// Verifies sky pass durable WebGPU state and counters.
TEST_CASE("sky pass creates durable GPU resources") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::SkyPass::create(ofg::GpuContext{}, ofg::SceneColorTarget::format(), ofg::DepthTarget::format());
    }()),
        doctest::Contains("WebGPU device"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::SkyPass::create(gpu.borrowed_context(), WGPUTextureFormat_Undefined, ofg::DepthTarget::format());
    }()),
        doctest::Contains("defined"),
        ofg::EngineError);

    std::unique_ptr<ofg::SkyPass> pass =
        ofg::SkyPass::create(gpu.borrowed_context(), ofg::SceneColorTarget::format(), ofg::DepthTarget::format());
    REQUIRE(pass != nullptr);
    CHECK(pass->counters().m_shader_module_create_count == 1);
    CHECK(pass->counters().m_bind_group_layout_create_count == 1);
    CHECK(pass->counters().m_bind_group_create_count == 1);
    CHECK(pass->counters().m_pipeline_create_count == 1);
    CHECK(pass->counters().m_buffer_create_count == 1);

    ofg::CameraProperties camera = make_sky_camera();
    ofg::Environment environment;
    CHECK_THROWS_WITH_AS(([&]() { pass->draw(nullptr, camera, environment, {}); }()),
        doctest::Contains("render pass"),
        ofg::EngineError);

    ofg::SceneColorTarget color_target(gpu.borrowed_context());
    ofg::DepthTarget depth_target(gpu.borrowed_context());
    color_target.resize(4, 4);
    depth_target.resize(4, 4);

    ScopedCommandEncoder encoder{wgpuDeviceCreateCommandEncoder(gpu.borrowed_context().m_device, nullptr)};
    REQUIRE(encoder.m_value != nullptr);

    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = color_target.view();
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth_attachment.view = depth_target.view();
    depth_attachment.depthLoadOp = WGPULoadOp_Clear;
    depth_attachment.depthStoreOp = WGPUStoreOp_Store;
    depth_attachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color_attachment;
    descriptor.depthStencilAttachment = &depth_attachment;

    WGPURenderPassEncoder render_pass = wgpuCommandEncoderBeginRenderPass(encoder.m_value, &descriptor);
    REQUIRE(render_pass != nullptr);
    pass->draw(render_pass, camera, environment, {});
    wgpuRenderPassEncoderEnd(render_pass);
    wgpuRenderPassEncoderRelease(render_pass);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.m_value, nullptr);
    encoder.m_value = nullptr;
    REQUIRE(command != nullptr);
    ScopedCommandBuffer command_buffer{command};
}
