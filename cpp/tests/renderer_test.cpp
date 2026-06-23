// Doctest coverage for the OFG draw-list renderer and opaque pass.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/game/render_target.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/camera.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/renderer.hpp"
#include "ofg/render/webgpu_common.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/resource_arena.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include "../src/render/shaders/opaque_uber.wgsl.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr WGPUTextureFormat _test_format = WGPUTextureFormat_RGBA8Unorm;

struct RenderScene {
    ofg::ResourceArena m_resources;
    ofg::DrawList m_draw_list;
    ofg::RenderView m_render_view;
    ofg::Texture* m_texture{nullptr};
    ofg::Mesh* m_mesh{nullptr};
};

// Releases a temporary render target texture.
struct ScopedTexture {
    WGPUTexture m_value{nullptr};

    ScopedTexture() = default;
    ScopedTexture(const ScopedTexture&) = delete;
    ScopedTexture& operator=(const ScopedTexture&) = delete;

    // Moves the texture handle without duplicating ownership.
    ScopedTexture(ScopedTexture&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}

    ScopedTexture& operator=(ScopedTexture&& other) noexcept = delete;

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

    ScopedTextureView() = default;
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

    ScopedCommandEncoder() = default;
    explicit ScopedCommandEncoder(WGPUCommandEncoder value) : m_value(value) {}
    ScopedCommandEncoder(const ScopedCommandEncoder&) = delete;
    ScopedCommandEncoder& operator=(const ScopedCommandEncoder&) = delete;

    // Moves the encoder handle without duplicating ownership.
    ScopedCommandEncoder(ScopedCommandEncoder&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}

    ScopedCommandEncoder& operator=(ScopedCommandEncoder&& other) noexcept = delete;

    // Releases the command encoder handle.
    ~ScopedCommandEncoder() {
        if (m_value != nullptr) {
            wgpuCommandEncoderRelease(m_value);
        }
    }
};

// Releases a temporary command buffer.
struct ScopedCommandBuffer {
    WGPUCommandBuffer m_value{nullptr};

    ScopedCommandBuffer() = default;
    explicit ScopedCommandBuffer(WGPUCommandBuffer value) : m_value(value) {}
    ScopedCommandBuffer(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer& operator=(const ScopedCommandBuffer&) = delete;

    // Moves the command buffer handle without duplicating ownership.
    ScopedCommandBuffer(ScopedCommandBuffer&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}

    ScopedCommandBuffer& operator=(ScopedCommandBuffer&& other) noexcept = delete;

    // Releases the command buffer handle.
    ~ScopedCommandBuffer() {
        if (m_value != nullptr) {
            wgpuCommandBufferRelease(m_value);
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

// Returns the shader parameter layout used by the opaque renderer tests.
ofg::ShaderParameterLayout renderer_shader_layout() {
    return ofg::ShaderParameterLayout{{
        ofg::ShaderParameter{
            "view_projection", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Frame, 0, true},
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw, 0, false},
        ofg::ShaderParameter{
            "base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material, 0, true},
        ofg::ShaderParameter{
            "base_color_texture", ofg::ShaderParameterType::Texture, ofg::ShaderParameterScope::Material, 0, true},
    }};
}

// Builds a byte vector from RGBA8 channel values.
std::vector<std::byte> rgba_bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    for (std::uint8_t value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Returns the vertices shared by renderer resource fixtures.
std::vector<ofg::MeshVertex> triangle_vertices() {
    return {
        ofg::MeshVertex{{-0.5F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
        ofg::MeshVertex{{0.5F, -0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
        ofg::MeshVertex{{0.0F, 0.5F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.5F, 1.0F}},
    };
}

// Adds the white texture required by the always-textured opaque material layout.
ofg::Texture* add_white_texture(ofg::ResourceArena& resources, ofg::GpuContext gpu) {
    std::string error;
    std::optional<ofg::Texture> texture = ofg::Texture::from_rgba8_pixels(gpu,
        "renderer test white texture",
        1,
        1,
        ofg::TextureColorSpace::Linear,
        rgba_bytes({255, 255, 255, 255}),
        ofg::MipMapPolicy::GenerateCpuFullChain,
        error);
    REQUIRE_MESSAGE(texture.has_value(), error);
    return &resources.add_texture(std::move(*texture));
}

// Appends one draw command for a scene-owned mesh.
void add_scene_command(RenderScene& scene) {
    ofg::DrawCommand command;
    command.m_mesh = scene.m_mesh;
    command.m_model = ofg::math::mat4_identity();
    scene.m_draw_list.add(std::move(command));
}

// Builds resources with independently selectable GPU-ready mesh/material state.
RenderScene make_render_scene_with_modes(
    ofg::GpuContext shader_gpu, ofg::GpuContext material_gpu, ofg::GpuContext mesh_gpu) {
    RenderScene scene;
    std::string error;

    std::optional<ofg::Shader> shader = ofg::Shader::create(shader_gpu,
        "renderer test shader",
        ofg::render::shaders::opaque_uber_wgsl,
        renderer_shader_layout(),
        {ofg::PipelineDefinition{"renderer test pipeline"}},
        error);
    REQUIRE_MESSAGE(shader.has_value(), error);
    ofg::Shader& stored_shader = scene.m_resources.add_shader(std::move(*shader));
    scene.m_texture = add_white_texture(scene.m_resources, material_gpu);

    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    properties.set("base_color_texture", scene.m_texture);
    std::optional<ofg::Material> material =
        ofg::Material::create(material_gpu, "renderer test material", stored_shader, std::move(properties), error);
    REQUIRE_MESSAGE(material.has_value(), error);
    ofg::Material& stored_material = scene.m_resources.add_material(std::move(*material));

    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &stored_material}};
    std::optional<ofg::Mesh> mesh =
        ofg::Mesh::create(mesh_gpu, "renderer test mesh", triangle_vertices(), std::move(indices), submeshes, error);
    REQUIRE_MESSAGE(mesh.has_value(), error);
    scene.m_mesh = &scene.m_resources.add_mesh(std::move(*mesh));

    scene.m_render_view = ofg::render_view_from_matrix(ofg::math::mat4_identity());
    add_scene_command(scene);
    add_scene_command(scene);
    REQUIRE_MESSAGE(scene.m_draw_list.validate(error), error);
    return scene;
}

// Builds GPU-ready resources and two commands that force draw-uniform growth.
RenderScene make_render_scene(ofg::GpuContext gpu) {
    return make_render_scene_with_modes(gpu, gpu, gpu);
}

// Builds a one-command draw list against scene-owned resources.
ofg::DrawList make_one_command_draw_list(RenderScene& scene) {
    ofg::DrawList draw_list;
    ofg::DrawCommand command;
    command.m_mesh = scene.m_mesh;
    command.m_model = ofg::math::mat4_identity();
    draw_list.add(std::move(command));
    return draw_list;
}

// Creates a texture view suitable for null-backend renderer submission.
ScopedTextureView make_render_target_view(ofg::GpuContext gpu, ScopedTexture& texture) {
    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.label = ofg::gpu::cstring_view("OFG renderer test target");
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

// Creates a command encoder suitable for renderer tests.
ScopedCommandEncoder make_encoder(ofg::GpuContext gpu) {
    WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    descriptor.label = ofg::gpu::cstring_view("OFG renderer test encoder");
    ScopedCommandEncoder encoder{wgpuDeviceCreateCommandEncoder(gpu.m_device, &descriptor)};
    REQUIRE(encoder.m_value != nullptr);
    return encoder;
}

} // namespace

// Verifies the renderer prepares opaque pipelines once for stable draw resources.
TEST_CASE("renderer prepares draw-list pipelines once") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene(gpu.borrowed_context());

    std::string error;
    std::unique_ptr<ofg::Renderer> renderer = ofg::Renderer::create(gpu.borrowed_context(), _test_format, error);
    REQUIRE_MESSAGE(renderer != nullptr, error);
    CHECK(renderer->counters().m_pipeline_create_count == 0);
    CHECK(renderer->counters().m_buffer_create_count == 1);

    REQUIRE_MESSAGE(renderer->prepare(scene.m_draw_list, error), error);
    CHECK(renderer->counters().m_pipeline_create_count == 1);
    CHECK(renderer->counters().m_buffer_create_count == 1);

    REQUIRE_MESSAGE(renderer->prepare(scene.m_draw_list, error), error);
    CHECK(renderer->counters().m_pipeline_create_count == 1);
}

// Verifies renderer validation rejects missing device state and invalid render inputs.
TEST_CASE("renderer rejects invalid creation and render inputs") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene(gpu.borrowed_context());

    std::string error;
    CHECK(ofg::Renderer::create(ofg::GpuContext{}, _test_format, error) == nullptr);
    CHECK(error.find("WebGPU device") != std::string::npos);

    std::unique_ptr<ofg::Renderer> renderer = ofg::Renderer::create(gpu.borrowed_context(), _test_format, error);
    REQUIRE_MESSAGE(renderer != nullptr, error);
    CHECK(renderer->render(nullptr, ofg::RenderTarget{}, scene.m_render_view, scene.m_draw_list, error) == false);
    CHECK(error.find("encoder") != std::string::npos);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());
    ofg::DrawList invalid_draw_list;
    invalid_draw_list.add(ofg::DrawCommand{});
    CHECK(renderer->render(encoder.m_value,
              ofg::RenderTarget{view.m_value, _test_format, 32, 32},
              scene.m_render_view,
              invalid_draw_list,
              error) == false);
    CHECK(error.find("mesh") != std::string::npos);
}

// Verifies renderer preparation reports invalid draw lists and non-GPU-ready resources.
TEST_CASE("renderer prepare rejects invalid and non gpu ready draw resources") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    std::string error;
    std::unique_ptr<ofg::Renderer> renderer = ofg::Renderer::create(gpu.borrowed_context(), _test_format, error);
    REQUIRE_MESSAGE(renderer != nullptr, error);

    ofg::DrawList invalid_draw_list;
    invalid_draw_list.add(ofg::DrawCommand{});
    CHECK(renderer->prepare(invalid_draw_list, error) == false);
    CHECK(error.find("mesh") != std::string::npos);

    RenderScene cpu_mesh_scene = make_render_scene_with_modes(ofg::GpuContext{}, ofg::GpuContext{}, ofg::GpuContext{});
    CHECK(renderer->prepare(cpu_mesh_scene.m_draw_list, error) == false);
    CHECK(error.find("mesh buffers") != std::string::npos);

    RenderScene cpu_shader_material_scene =
        make_render_scene_with_modes(ofg::GpuContext{}, ofg::GpuContext{}, gpu.borrowed_context());
    CHECK(renderer->prepare(cpu_shader_material_scene.m_draw_list, error) == false);
    CHECK(error.find("shader") != std::string::npos);

    RenderScene cpu_bind_group_scene =
        make_render_scene_with_modes(gpu.borrowed_context(), ofg::GpuContext{}, gpu.borrowed_context());
    CHECK(renderer->prepare(cpu_bind_group_scene.m_draw_list, error) == false);
    CHECK(error.find("bind group") != std::string::npos);

    CHECK(ofg::Renderer::create(gpu.borrowed_context(), WGPUTextureFormat_Undefined, error) == nullptr);
    CHECK(error.find("defined color format") != std::string::npos);
}

// Verifies a draw list records into a null-backend render target and finishes cleanly.
TEST_CASE("renderer records opaque draw list into a render target") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene(gpu.borrowed_context());

    std::string error;
    std::unique_ptr<ofg::Renderer> renderer = ofg::Renderer::create(gpu.borrowed_context(), _test_format, error);
    REQUIRE_MESSAGE(renderer != nullptr, error);
    REQUIRE_MESSAGE(renderer->resize(32, 32, error), error);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());

    REQUIRE_MESSAGE(renderer->render(encoder.m_value,
                        ofg::RenderTarget{view.m_value, _test_format, 32, 32},
                        scene.m_render_view,
                        scene.m_draw_list,
                        error),
        error);

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = ofg::gpu::cstring_view("OFG renderer test commands");
    ScopedCommandBuffer command{wgpuCommandEncoderFinish(encoder.m_value, &command_descriptor)};
    REQUIRE(command.m_value != nullptr);
    wgpuQueueSubmit(gpu.borrowed_context().m_queue, 1, &command.m_value);

    CHECK(renderer->counters().m_pipeline_create_count == 1);

    ScopedTexture second_texture;
    ScopedTextureView second_view = make_render_target_view(gpu.borrowed_context(), second_texture);
    ScopedCommandEncoder second_encoder = make_encoder(gpu.borrowed_context());
    ofg::DrawList one_command = make_one_command_draw_list(scene);
    REQUIRE_MESSAGE(renderer->render(second_encoder.m_value,
                        ofg::RenderTarget{second_view.m_value, _test_format, 32, 32},
                        scene.m_render_view,
                        one_command,
                        error),
        error);

    REQUIRE_MESSAGE(renderer->resize(0, 0, error), error);
}
