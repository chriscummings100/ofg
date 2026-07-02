// Doctest coverage for the static OFG Game lifecycle facade.
//
// These tests validate the public Game singleton, debug-status behavior, and
// render-target checks that browser and native frame drivers use when they
// delegate frame work to Game.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/core/control_input.hpp"
#include "ofg/game/game.hpp"
#include "ofg/game/gpu_context.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/gpu/common.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <webgpu/webgpu.h>

namespace {

constexpr WGPUTextureFormat _test_format = WGPUTextureFormat_RGBA8Unorm;

// Produces a non-null opaque WebGPU texture view for validation-only tests.
WGPUTextureView fake_texture_view() {
    return reinterpret_cast<WGPUTextureView>(static_cast<std::uintptr_t>(1));
}

// Produces a non-null opaque WebGPU device handle for create-only tests.
WGPUDevice fake_device() {
    return reinterpret_cast<WGPUDevice>(static_cast<std::uintptr_t>(2));
}

// Produces a non-null opaque WebGPU queue handle for create-only tests.
WGPUQueue fake_queue() {
    return reinterpret_cast<WGPUQueue>(static_cast<std::uintptr_t>(3));
}

// Resets the static Game singleton around each Game doctest.
struct GameGuard {
    GameGuard() {
        ofg::Game::destroy();
    }

    GameGuard(const GameGuard&) = delete;
    GameGuard& operator=(const GameGuard&) = delete;

    // Drains release before destroying the singleton at the end of a test.
    ~GameGuard() {
        try {
            while (!ofg::Game::release()) {}
        } catch (...) {}
        ofg::Game::destroy();
    }
};

// Releases a temporary render target texture.
struct ScopedTexture {
    WGPUTexture m_value{nullptr};

    ScopedTexture() = default;
    ScopedTexture(const ScopedTexture&) = delete;
    ScopedTexture& operator=(const ScopedTexture&) = delete;

    // Releases the texture handle.
    ~ScopedTexture() {
        if (m_value != nullptr) {
            wgpuTextureRelease(m_value);
        }
    }
};

// Releases a temporary render target view.
struct ScopedTextureView {
    WGPUTextureView m_value{nullptr};

    explicit ScopedTextureView(WGPUTextureView value) : m_value(value) {}
    ScopedTextureView(const ScopedTextureView&) = delete;
    ScopedTextureView& operator=(const ScopedTextureView&) = delete;

    // Moves the texture view handle without duplicating ownership.
    ScopedTextureView(ScopedTextureView&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}

    ScopedTextureView& operator=(ScopedTextureView&& other) noexcept = delete;

    // Releases the texture view handle.
    ~ScopedTextureView() {
        if (m_value != nullptr) {
            wgpuTextureViewRelease(m_value);
        }
    }
};

// Releases a temporary command encoder.
struct ScopedCommandEncoder {
    WGPUCommandEncoder m_value{nullptr};

    explicit ScopedCommandEncoder(WGPUCommandEncoder value) : m_value(value) {}
    ScopedCommandEncoder(const ScopedCommandEncoder&) = delete;
    ScopedCommandEncoder& operator=(const ScopedCommandEncoder&) = delete;

    // Moves the command encoder handle without duplicating ownership.
    ScopedCommandEncoder(ScopedCommandEncoder&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}

    ScopedCommandEncoder& operator=(ScopedCommandEncoder&& other) noexcept = delete;

    // Releases the command encoder handle.
    ~ScopedCommandEncoder() {
        if (m_value != nullptr) {
            wgpuCommandEncoderRelease(m_value);
        }
    }
};

// Creates a test GPU context or fails the current doctest.
ofg::tests::TestGpuContext make_test_gpu() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);
    return std::move(*gpu);
}

// Creates a texture view suitable for null-backend Game submission.
ScopedTextureView make_render_target_view(ofg::GpuContext gpu, ScopedTexture& texture) {
    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.label = ofg::gpu::cstring_view("OFG Game test target");
    texture_descriptor.usage = WGPUTextureUsage_RenderAttachment;
    texture_descriptor.dimension = WGPUTextureDimension_2D;
    texture_descriptor.size = WGPUExtent3D{32, 32, 1};
    texture_descriptor.format = _test_format;

    texture.m_value = wgpuDeviceCreateTexture(gpu.m_device, &texture_descriptor);
    REQUIRE(texture.m_value != nullptr);

    ScopedTextureView view{wgpuTextureCreateView(texture.m_value, nullptr)};
    REQUIRE(view.m_value != nullptr);
    return view;
}

// Creates a command encoder suitable for Game tests.
ScopedCommandEncoder make_encoder(ofg::GpuContext gpu) {
    WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    descriptor.label = ofg::gpu::cstring_view("OFG Game test encoder");
    ScopedCommandEncoder encoder{wgpuDeviceCreateCommandEncoder(gpu.m_device, &descriptor)};
    REQUIRE(encoder.m_value != nullptr);
    return encoder;
}

} // namespace

// Verifies lifecycle states expose stable diagnostic names.
TEST_CASE("Game lifecycle states have diagnostic names") {
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Uninitialized)) == "uninitialized");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Created)) == "created");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Prep_Resources)) == "prep_resources");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Prep_Scene)) == "prep_scene");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Prep_Renderer)) == "prep_renderer");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Ready)) == "ready");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Rel_Renderer)) == "rel_renderer");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Rel_Scene)) == "rel_scene");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Rel_Resources)) == "rel_resources");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Released)) == "released");
    CHECK(std::string(ofg::game_lifecycle_state_name(ofg::GameLifecycleState::Failed)) == "failed");
    CHECK(std::string(ofg::game_lifecycle_state_name(static_cast<ofg::GameLifecycleState>(100))) == "unknown");
}

// Verifies render target validation catches null and mismatched targets.
TEST_CASE("RenderTarget validation rejects invalid frame targets") {
    CHECK_THROWS_WITH_AS(ofg::validate_render_target(ofg::RenderTarget{}, _test_format, 800, 450),
        doctest::Contains("texture view"),
        ofg::EngineError);

    CHECK_THROWS_WITH_AS(
        ofg::validate_render_target(
            ofg::RenderTarget{fake_texture_view(), WGPUTextureFormat_BGRA8Unorm, 800, 450}, _test_format, 800, 450),
        doctest::Contains("does not match renderer format"),
        ofg::EngineError);

    CHECK_THROWS_WITH_AS(ofg::validate_render_target(
                             ofg::RenderTarget{fake_texture_view(), _test_format, 801, 450}, _test_format, 800, 450),
        doctest::Contains("does not match latest resize"),
        ofg::EngineError);

    CHECK_THROWS_WITH_AS(ofg::validate_render_target(
                             ofg::RenderTarget{fake_texture_view(), _test_format, 0, 450}, _test_format, 800, 450),
        doctest::Contains("dimensions must be nonzero"),
        ofg::EngineError);
}

// Verifies valid render targets pass without touching GPU resources.
TEST_CASE("RenderTarget validation accepts matching nonzero target") {
    CHECK_NOTHROW(ofg::validate_render_target(
        ofg::RenderTarget{fake_texture_view(), _test_format, 800, 450}, _test_format, 800, 450));
}

// Verifies Game create rejects invalid setup before calling WebGPU.
TEST_CASE("Game create validates color format and GPU handles") {
    GameGuard guard;

    CHECK_THROWS_WITH_AS(
        ofg::Game::create(ofg::GpuContext{}, _test_format), doctest::Contains("device and queue"), ofg::EngineError);
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Uninitialized);
    CHECK(ofg::Game::last_error().empty());

    CHECK_THROWS_WITH_AS(ofg::Game::create(ofg::GpuContext{}, WGPUTextureFormat_Undefined),
        doctest::Contains("defined color format"),
        ofg::EngineError);
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Uninitialized);
}

// Verifies the static Game lifecycle accepts one live singleton and releases it.
TEST_CASE("Game static lifecycle owns one live singleton") {
    GameGuard guard;

    ofg::Game::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"}, _test_format);
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Created);
    CHECK(ofg::Game::status().m_lifecycle_state == "created");

    ofg::Game::resize(64, 32, 1.0);
    CHECK(ofg::Game::status().m_canvas_width == 64);
    CHECK(ofg::Game::status().m_canvas_height == 32);

    CHECK_THROWS_WITH_AS(
        ofg::Game::create(
            ofg::GpuContext{fake_device(), fake_queue(), "second fake adapter", "TestBackend"}, _test_format),
        doctest::Contains("singleton is live"),
        ofg::EngineError);
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Created);

    CHECK(ofg::Game::release());
    CHECK(ofg::Game::release());
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Released);
    ofg::Game::destroy();
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Uninitialized);
}

// Verifies prepare fails clearly when Game has not been created.
TEST_CASE("Game prepare requires create") {
    GameGuard guard;

    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::Game::prepare(); }()), doctest::Contains("requires Game::create"), ofg::EngineError);
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Uninitialized);
}

// Verifies recoverable Game errors flow into status while the singleton is live.
TEST_CASE("Game records recoverable and GPU errors") {
    GameGuard guard;

    ofg::Game::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"}, _test_format);

    ofg::Game::record_error("recoverable");
    REQUIRE(ofg::Game::status().m_last_error.has_value());
    CHECK(*ofg::Game::status().m_last_error == "recoverable");
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Created);

    ofg::Game::record_gpu_error("device lost");
    REQUIRE(ofg::Game::status().m_last_error.has_value());
    CHECK(*ofg::Game::status().m_last_error == "device lost");
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Failed);
}

// Verifies control input follows the Game singleton lifecycle.
TEST_CASE("Game control input validates lifecycle and finite values") {
    GameGuard guard;

    CHECK_THROWS_WITH_AS(ofg::Game::set_control_input(ofg::ControlInput{}),
        doctest::Contains("requires Game::create"),
        ofg::EngineError);

    ofg::Game::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"}, _test_format);
    ofg::ControlInput input;
    input.m_move_z = 1.0f;
    CHECK_NOTHROW(ofg::Game::set_control_input(input));

    input.m_move_x = std::numeric_limits<float>::infinity();
    CHECK_THROWS_WITH_AS(ofg::Game::set_control_input(input), doctest::Contains("finite"), ofg::EngineError);

    ofg::Game::record_gpu_error("failed for control input test");
    CHECK_THROWS_WITH_AS(
        ofg::Game::set_control_input(ofg::ControlInput{}), doctest::Contains("failed"), ofg::EngineError);

    CHECK(ofg::Game::release());
    CHECK_THROWS_WITH_AS(
        ofg::Game::set_control_input(ofg::ControlInput{}), doctest::Contains("after Game release"), ofg::EngineError);
}

// Verifies invalid runtime inputs throw and update debug status.
TEST_CASE("Game resize and update validation record status errors") {
    GameGuard guard;

    ofg::Game::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"}, _test_format);
    ofg::Game::resize(800, 450, 1.0);

    CHECK_THROWS_WITH_AS(ofg::Game::resize(320, 200, 0.0), doctest::Contains("Device pixel ratio"), ofg::EngineError);
    REQUIRE(ofg::Game::status().m_last_error.has_value());
    CHECK(ofg::Game::status().m_last_error->find("Device pixel ratio") != std::string::npos);
    CHECK(ofg::Game::status().m_canvas_width == 800);

    CHECK_THROWS_WITH_AS(
        ofg::Game::update(std::numeric_limits<double>::infinity()), doctest::Contains("prepare"), ofg::EngineError);
    REQUIRE(ofg::Game::status().m_last_error.has_value());
    CHECK(ofg::Game::status().m_last_error->find("prepare") != std::string::npos);
}

// Verifies Game can prepare, update, render, and expose debug status using the test GPU.
TEST_CASE("Game prepares and renders through the static facade") {
    GameGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    ofg::Game::create(gpu.borrowed_context(), _test_format);
    ofg::Game::resize(32, 32, 1.0);
    REQUIRE(ofg::Game::prepare());
    REQUIRE(ofg::Game::prepare());
    CHECK(ofg::Game::state() == ofg::GameLifecycleState::Ready);

    ofg::Game::update(16.0);
    CHECK(ofg::Game::status().m_frame_count == 1);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());
    CHECK_NOTHROW(ofg::Game::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 32, 32}));

    CHECK(ofg::Game::status().m_initialized);
    CHECK(ofg::Game::status().m_camera_mode == "debug");
    CHECK(ofg::Game::status().m_surface_configure_count == 1);
    CHECK(ofg::Game::status().m_buffer_create_count >= 1);
    CHECK(ofg::Game::debug_status_json().find("\"lifecycleState\":\"ready\"") != std::string::npos);
    CHECK(ofg::Game::debug_status_json().find("\"cameraMode\":\"debug\"") != std::string::npos);
}

// Verifies one-frame camera mode cycle input is consumed after one update.
TEST_CASE("Game consumes camera mode cycle control edges") {
    GameGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    ofg::Game::create(gpu.borrowed_context(), _test_format);
    ofg::Game::resize(32, 32, 1.0);
    REQUIRE(ofg::Game::prepare());

    ofg::ControlInput input;
    input.m_cycle_camera_mode = true;
    ofg::Game::set_control_input(input);
    ofg::Game::update(16.0);
    CHECK(ofg::Game::status().m_camera_mode == "first_person");
    ofg::Game::update(32.0);
    CHECK(ofg::Game::status().m_camera_mode == "first_person");
}
