// Doctest coverage for shared GPU/WebGPU helper functions.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/gpu/common.hpp"

#include "ofg/core/engine_error.hpp"

#include <optional>
#include <string>

// Verifies WebGPU string-view conversion rules used by labels and callbacks.
TEST_CASE("gpu common string helpers handle literals, bounded strings, and null views") {
    const WGPUStringView literal = ofg::gpu::cstring_view("hello");
    CHECK(std::string(literal.data) == "hello");
    CHECK(literal.length == WGPU_STRLEN);
    CHECK(ofg::gpu::string_from_view(literal) == "hello");

    const std::string owned = "abcdef";
    const WGPUStringView bounded = ofg::gpu::string_view(owned);
    CHECK(bounded.data == owned.c_str());
    CHECK(bounded.length == owned.size());

    CHECK(ofg::gpu::string_from_view(WGPUStringView{owned.c_str(), 3}) == "abc");
    CHECK(ofg::gpu::string_from_view(WGPUStringView{nullptr, 0}) == "");
}

// Verifies public report labels for formats and Dawn backend values.
TEST_CASE("gpu common enum labels cover known formats and backends") {
    CHECK(ofg::gpu::texture_format_name(WGPUTextureFormat_BGRA8Unorm) == "Bgra8Unorm");
    CHECK(ofg::gpu::texture_format_name(WGPUTextureFormat_BGRA8UnormSrgb) == "Bgra8UnormSrgb");
    CHECK(ofg::gpu::texture_format_name(WGPUTextureFormat_RGBA8Unorm) == "Rgba8Unorm");
    CHECK(ofg::gpu::texture_format_name(WGPUTextureFormat_RGBA8UnormSrgb) == "Rgba8UnormSrgb");
    CHECK(ofg::gpu::texture_format_name(WGPUTextureFormat_RGBA16Float) == "Rgba16Float");
    CHECK(ofg::gpu::texture_format_name(WGPUTextureFormat_Undefined) == "Unknown");

    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_Null) == "Null");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_WebGPU) == "WebGPU");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_D3D11) == "D3D11");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_D3D12) == "D3D12");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_Metal) == "Metal");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_Vulkan) == "Vulkan");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_OpenGL) == "OpenGL");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_OpenGLES) == "OpenGLES");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_Undefined) == "Unknown");
    CHECK(ofg::gpu::backend_type_name(WGPUBackendType_Force32) == "Unknown");
}

// Verifies generic depth target helpers create ordinary WebGPU handles.
TEST_CASE("gpu common creates depth texture and view") {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);

    WGPUTexture texture =
        ofg::gpu::create_depth_texture(gpu->borrowed_context().m_device, WGPUTextureFormat_Depth24Plus, 8, 8, "depth");
    REQUIRE(texture != nullptr);
    WGPUTextureView view = ofg::gpu::create_depth_view(texture, WGPUTextureFormat_Depth24Plus, "depth view");
    REQUIRE(view != nullptr);

    wgpuTextureViewRelease(view);
    wgpuTextureRelease(texture);
}

// Verifies depth helpers fail early for invalid caller-owned handles.
TEST_CASE("gpu common depth helpers reject invalid inputs before WebGPU calls") {
    const auto create_with_null_device = []() {
        (void)ofg::gpu::create_depth_texture(nullptr, WGPUTextureFormat_Depth24Plus, 8, 8, "depth");
    };
    const auto create_with_zero_width = []() {
        (void)ofg::gpu::create_depth_texture(
            reinterpret_cast<WGPUDevice>(1), WGPUTextureFormat_Depth24Plus, 0, 8, "depth");
    };
    const auto create_with_undefined_format = []() {
        (void)ofg::gpu::create_depth_texture(
            reinterpret_cast<WGPUDevice>(1), WGPUTextureFormat_Undefined, 8, 8, "depth");
    };
    const auto view_with_null_texture = []() {
        (void)ofg::gpu::create_depth_view(nullptr, WGPUTextureFormat_Depth24Plus, "depth view");
    };
    const auto view_with_undefined_format = []() {
        (void)ofg::gpu::create_depth_view(reinterpret_cast<WGPUTexture>(1), WGPUTextureFormat_Undefined, "depth view");
    };

    CHECK_THROWS_AS(create_with_null_device(), ofg::EngineError);
    CHECK_THROWS_AS(create_with_zero_width(), ofg::EngineError);
    CHECK_THROWS_AS(create_with_undefined_format(), ofg::EngineError);
    CHECK_THROWS_AS(view_with_null_texture(), ofg::EngineError);
    CHECK_THROWS_AS(view_with_undefined_format(), ofg::EngineError);
}
