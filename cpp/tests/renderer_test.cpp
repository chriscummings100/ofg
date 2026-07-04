// Doctest coverage for the OFG draw-list renderer and opaque pass.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/game/render_target.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/render/opaque_pbr_shader.hpp"
#include "ofg/render/renderer.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"
#include "ofg/scene/light.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"

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
constexpr float _pi = 3.14159265358979323846f;

struct RenderResources {
    std::vector<std::unique_ptr<ofg::Texture>> m_textures;
    std::vector<std::unique_ptr<ofg::Shader>> m_shaders;
    std::vector<std::unique_ptr<ofg::Material>> m_materials;
    std::vector<std::unique_ptr<ofg::Mesh>> m_meshes;

    // Creates a texture in stable test-fixture storage.
    ofg::Texture& create_texture(ofg::GpuContext gpu, std::string label) {
        m_textures.push_back(std::make_unique<ofg::Texture>(gpu, std::move(label)));
        return *m_textures.back();
    }

    // Creates a shader in stable test-fixture storage.
    ofg::Shader& create_shader(ofg::GpuContext gpu, std::string label) {
        m_shaders.push_back(std::make_unique<ofg::Shader>(gpu, std::move(label)));
        return *m_shaders.back();
    }

    // Creates a material in stable test-fixture storage.
    ofg::Material& create_material(ofg::GpuContext gpu, std::string label) {
        m_materials.push_back(std::make_unique<ofg::Material>(gpu, std::move(label)));
        return *m_materials.back();
    }

    // Creates a mesh in stable test-fixture storage.
    ofg::Mesh& create_mesh(ofg::GpuContext gpu, std::string label) {
        m_meshes.push_back(std::make_unique<ofg::Mesh>(gpu, std::move(label)));
        return *m_meshes.back();
    }
};

struct RenderScene {
    RenderResources m_resources;
    ofg::Scene m_scene;
    ofg::Texture* m_base_color_texture{nullptr};
    ofg::Texture* m_metallic_roughness_texture{nullptr};
    ofg::Texture* m_normal_texture{nullptr};
    ofg::Mesh* m_mesh{nullptr};
};

// Resets the static renderer singleton around each renderer doctest.
struct RendererGuard {
    RendererGuard() {
        ofg::Renderer::destroy();
    }

    RendererGuard(const RendererGuard&) = delete;
    RendererGuard& operator=(const RendererGuard&) = delete;

    // Releases any live renderer singleton at the end of a test.
    ~RendererGuard() {
        ofg::Renderer::destroy();
    }
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
        ofg::MeshVertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        ofg::MeshVertex{{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        ofg::MeshVertex{{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
    };
}

// Adds a texture required by the PBR material layout.
ofg::Texture* add_test_texture(RenderResources& resources,
    ofg::GpuContext gpu,
    std::string label,
    ofg::TextureColorSpace color_space,
    std::initializer_list<std::uint8_t> pixel) {
    ofg::Texture& texture = resources.create_texture(gpu, std::move(label));
    texture.init_from_rgba8_pixels(1, 1, color_space, rgba_bytes(pixel), ofg::MipMapPolicy::GenerateCpuFullChain);
    return &texture;
}

// Appends one mesh-renderer entity for a scene-owned mesh.
void add_scene_object(RenderScene& scene) {
    ofg::Entity* entity = scene.m_scene.create_entity(scene.m_scene.get_root());
    REQUIRE(entity != nullptr);
    entity->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 4.0f);
    ofg::Component* component = entity->create_component(ofg::ComponentType::MeshRenderer);
    REQUIRE(component != nullptr);
    REQUIRE(entity->mesh_renderer() != nullptr);
    entity->mesh_renderer()->set_mesh(scene.m_mesh);
}

// Adds the default scene camera required by renderer pass submission.
void add_scene_camera(ofg::Scene& scene) {
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    REQUIRE(camera_entity != nullptr);
    ofg::Component* component = camera_entity->create_component(ofg::ComponentType::Camera);
    REQUIRE(component != nullptr);
    REQUIRE(camera_entity->camera() != nullptr);
}

// Adds a current sun light to exercise renderer-owned shadow passes.
void add_scene_sun(ofg::Scene& scene) {
    ofg::Entity* sun_entity = scene.create_entity(scene.get_root());
    REQUIRE(sun_entity != nullptr);
    ofg::Component* component = sun_entity->create_component(ofg::ComponentType::Light);
    REQUIRE(component != nullptr);
    REQUIRE(sun_entity->light() != nullptr);
    sun_entity->light()->set_color_intensity(ofg::math::vec3(1.0f, 0.92f, 0.78f), 3.0f);
    scene.environment().set_main_directional_light(sun_entity->light());

    std::string error;
    const std::optional<ofg::math::Vec3> sun_direction =
        ofg::math::normalize(ofg::math::vec3(-0.35f, -1.0f, -0.25f), error);
    REQUIRE_MESSAGE(sun_direction.has_value(), error);
    const std::optional<ofg::math::Quat> sun_rotation =
        ofg::math::quat_look_at_lh(sun_entity->local_transform().m_position,
            ofg::math::add(sun_entity->local_transform().m_position, *sun_direction),
            ofg::math::vec3(0.0f, 1.0f, 0.0f),
            error);
    REQUIRE_MESSAGE(sun_rotation.has_value(), error);
    sun_entity->local_transform().m_rotation = *sun_rotation;
}

// Builds resources with independently selectable GPU-ready mesh/material state.
RenderScene make_render_scene_with_modes(
    ofg::GpuContext shader_gpu, ofg::GpuContext material_gpu, ofg::GpuContext mesh_gpu) {
    RenderScene scene;

    ofg::Shader& stored_shader = scene.m_resources.create_shader(shader_gpu, "renderer test shader");
    stored_shader.init_from_wgsl(ofg::render::shaders::opaque_uber_wgsl,
        ofg::opaque_pbr_shader_layout(),
        {ofg::PipelineDefinition{"renderer test pipeline"}});
    scene.m_base_color_texture = add_test_texture(scene.m_resources,
        material_gpu,
        "renderer test white texture",
        ofg::TextureColorSpace::Srgb,
        {255, 255, 255, 255});
    scene.m_metallic_roughness_texture = add_test_texture(scene.m_resources,
        material_gpu,
        "renderer test metallic roughness texture",
        ofg::TextureColorSpace::Linear,
        {255, 255, 0, 255});
    scene.m_normal_texture = add_test_texture(scene.m_resources,
        material_gpu,
        "renderer test normal texture",
        ofg::TextureColorSpace::Linear,
        {128, 128, 255, 255});

    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    properties.set("pbr_factors", ofg::math::vec4(0.0f, 1.0f, 1.0f, 0.0f));
    properties.set("base_color_texture", scene.m_base_color_texture);
    properties.set("metallic_roughness_texture", scene.m_metallic_roughness_texture);
    properties.set("normal_texture", scene.m_normal_texture);
    ofg::Material& stored_material = scene.m_resources.create_material(material_gpu, "renderer test material");
    stored_material.init(stored_shader, std::move(properties));

    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"triangle", 0, 3, &stored_material}};
    ofg::Mesh& mesh = scene.m_resources.create_mesh(mesh_gpu, "renderer test mesh");
    mesh.init(triangle_vertices(), std::move(indices), submeshes);
    scene.m_mesh = &mesh;

    add_scene_camera(scene.m_scene);
    add_scene_object(scene);
    add_scene_object(scene);
    return scene;
}

// Builds GPU-ready resources and two commands that force draw-uniform growth.
RenderScene make_render_scene(ofg::GpuContext gpu) {
    return make_render_scene_with_modes(gpu, gpu, gpu);
}

// Builds a one-object scene against scene-owned resources.
ofg::Scene make_one_object_scene(RenderScene& scene) {
    ofg::Scene one_object_scene;
    add_scene_camera(one_object_scene);
    ofg::Entity* entity = one_object_scene.create_entity(one_object_scene.get_root());
    REQUIRE(entity != nullptr);
    ofg::Component* component = entity->create_component(ofg::ComponentType::MeshRenderer);
    REQUIRE(component != nullptr);
    REQUIRE(entity->mesh_renderer() != nullptr);
    entity->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 4.0f);
    entity->mesh_renderer()->set_mesh(scene.m_mesh);
    return one_object_scene;
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

// Creates and prepares the static renderer for tests.
void init_prepared_renderer(ofg::GpuContext gpu) {
    ofg::Renderer::create(gpu, _test_format);
    CHECK(ofg::Renderer::state() == ofg::RendererLifecycleState::Created);
    REQUIRE(ofg::Renderer::prepare());
    CHECK(ofg::Renderer::state() == ofg::RendererLifecycleState::Ready);
}

} // namespace

// Verifies renderer lifecycle names and static preparation behavior.
TEST_CASE("renderer static lifecycle prepares pass resources") {
    RendererGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    CHECK(
        std::string(ofg::renderer_lifecycle_state_name(ofg::RendererLifecycleState::Uninitialized)) == "uninitialized");
    CHECK(std::string(ofg::renderer_lifecycle_state_name(ofg::RendererLifecycleState::Created)) == "created");
    CHECK(std::string(ofg::renderer_lifecycle_state_name(ofg::RendererLifecycleState::Preparing)) == "preparing");
    CHECK(std::string(ofg::renderer_lifecycle_state_name(ofg::RendererLifecycleState::Ready)) == "ready");
    CHECK(std::string(ofg::renderer_lifecycle_state_name(ofg::RendererLifecycleState::Releasing)) == "releasing");
    CHECK(std::string(ofg::renderer_lifecycle_state_name(ofg::RendererLifecycleState::Released)) == "released");
    CHECK(std::string(ofg::renderer_lifecycle_state_name(ofg::RendererLifecycleState::Failed)) == "failed");
    CHECK(std::string(ofg::renderer_lifecycle_state_name(static_cast<ofg::RendererLifecycleState>(99))) == "unknown");
    CHECK(ofg::Renderer::state() == ofg::RendererLifecycleState::Uninitialized);
    CHECK(ofg::Renderer::bloom_diagnostics().m_active_level_count == 0);
    CHECK(ofg::Renderer::temp_buffer_stats().m_peak_bytes == 0);

    ofg::Renderer::create(gpu.borrowed_context(), _test_format);
    CHECK(ofg::Renderer::state() == ofg::RendererLifecycleState::Created);
    CHECK(ofg::Renderer::counters().m_pipeline_create_count == 0);
    CHECK(ofg::Renderer::counters().m_buffer_create_count == 0);

    REQUIRE(ofg::Renderer::prepare());
    CHECK(ofg::Renderer::state() == ofg::RendererLifecycleState::Ready);
    CHECK(ofg::Renderer::counters().m_pipeline_create_count == 7);
    CHECK(ofg::Renderer::counters().m_buffer_create_count == 13);
    CHECK(ofg::Renderer::counters().m_shader_module_create_count == 6);

    REQUIRE(ofg::Renderer::prepare());
    CHECK(ofg::Renderer::counters().m_buffer_create_count == 13);
    CHECK(ofg::Renderer::release());
    CHECK(ofg::Renderer::state() == ofg::RendererLifecycleState::Released);
    CHECK(ofg::Renderer::release());
}

// Verifies renderer validation rejects missing device state and invalid render inputs.
TEST_CASE("renderer rejects invalid lifecycle and render inputs") {
    RendererGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene(gpu.borrowed_context());

    CHECK(ofg::Renderer::release());
    CHECK(ofg::Renderer::counters().m_pipeline_create_count == 0);
    CHECK(ofg::Renderer::counters().m_buffer_create_count == 0);
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::Renderer::prepare(); }()),
        doctest::Contains("requires Renderer::create"),
        ofg::EngineError);
    ofg::Renderer::destroy();

    CHECK_THROWS_WITH_AS(
        ofg::Renderer::create(ofg::GpuContext{}, _test_format), doctest::Contains("WebGPU device"), ofg::EngineError);
    ofg::Renderer::destroy();
    CHECK_THROWS_WITH_AS(ofg::Renderer::create(gpu.borrowed_context(), WGPUTextureFormat_Undefined),
        doctest::Contains("defined color format"),
        ofg::EngineError);
    ofg::Renderer::destroy();

    ofg::Renderer::create(gpu.borrowed_context(), _test_format);
    CHECK_THROWS_WITH_AS(ofg::Renderer::create(gpu.borrowed_context(), _test_format),
        doctest::Contains("Renderer::create cannot be called"),
        ofg::EngineError);
    const ofg::Scene& render_scene = scene.m_scene;
    CHECK_THROWS_WITH_AS(ofg::Renderer::resize(32, 32), doctest::Contains("prepare to complete"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(ofg::Renderer::render(nullptr, ofg::RenderTarget{}, render_scene),
        doctest::Contains("prepare to complete"),
        ofg::EngineError);

    REQUIRE(ofg::Renderer::prepare());

    CHECK_THROWS_WITH_AS(ofg::Renderer::render(nullptr, ofg::RenderTarget{}, render_scene),
        doctest::Contains("encoder"),
        ofg::EngineError);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Renderer::render(
            encoder.m_value, ofg::RenderTarget{view.m_value, WGPUTextureFormat_BGRA8Unorm, 32, 32}, render_scene);
    }()),
        doctest::Contains("does not match renderer format"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Renderer::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 0, 32}, render_scene);
    }()),
        doctest::Contains("dimensions must be nonzero"),
        ofg::EngineError);

    ofg::Scene invalid_scene;
    add_scene_camera(invalid_scene);
    ofg::Entity* invalid_entity = invalid_scene.create_entity(invalid_scene.get_root());
    REQUIRE(invalid_entity != nullptr);
    (void)invalid_entity->create_component(ofg::ComponentType::MeshRenderer);
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Renderer::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 32, 32}, invalid_scene);
    }()),
        doctest::Contains("mesh"),
        ofg::EngineError);

    ofg::Scene no_camera_scene;
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Renderer::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 32, 32}, no_camera_scene);
    }()),
        doctest::Contains("scene camera"),
        ofg::EngineError);

    CHECK(ofg::Renderer::release());
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::Renderer::prepare(); }()), doctest::Contains("after Renderer release"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::Renderer::prepare(); }()), doctest::Contains("after Renderer release"), ofg::EngineError);
}

// Verifies renderer command recording reports non-GPU-ready resources.
TEST_CASE("renderer render rejects non gpu ready draw resources") {
    RendererGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();

    init_prepared_renderer(gpu.borrowed_context());

    RenderScene cpu_mesh_scene = make_render_scene_with_modes(ofg::GpuContext{}, ofg::GpuContext{}, ofg::GpuContext{});
    ScopedTexture mesh_texture;
    ScopedTextureView mesh_view = make_render_target_view(gpu.borrowed_context(), mesh_texture);
    ScopedCommandEncoder mesh_encoder = make_encoder(gpu.borrowed_context());
    const ofg::Scene& cpu_mesh_render_scene = cpu_mesh_scene.m_scene;
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Renderer::render(
            mesh_encoder.m_value, ofg::RenderTarget{mesh_view.m_value, _test_format, 32, 32}, cpu_mesh_render_scene);
    }()),
        doctest::Contains("mesh buffers"),
        ofg::EngineError);

    RenderScene cpu_shader_material_scene =
        make_render_scene_with_modes(ofg::GpuContext{}, ofg::GpuContext{}, gpu.borrowed_context());
    ScopedTexture shader_texture;
    ScopedTextureView shader_view = make_render_target_view(gpu.borrowed_context(), shader_texture);
    ScopedCommandEncoder shader_encoder = make_encoder(gpu.borrowed_context());
    const ofg::Scene& cpu_shader_render_scene = cpu_shader_material_scene.m_scene;
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Renderer::render(shader_encoder.m_value,
            ofg::RenderTarget{shader_view.m_value, _test_format, 32, 32},
            cpu_shader_render_scene);
    }()),
        doctest::Contains("shader"),
        ofg::EngineError);

    RenderScene cpu_bind_group_scene =
        make_render_scene_with_modes(gpu.borrowed_context(), ofg::GpuContext{}, gpu.borrowed_context());
    ScopedTexture bind_group_texture;
    ScopedTextureView bind_group_view = make_render_target_view(gpu.borrowed_context(), bind_group_texture);
    ScopedCommandEncoder bind_group_encoder = make_encoder(gpu.borrowed_context());
    const ofg::Scene& cpu_bind_group_render_scene = cpu_bind_group_scene.m_scene;
    CHECK_THROWS_WITH_AS(([&]() {
        ofg::Renderer::render(bind_group_encoder.m_value,
            ofg::RenderTarget{bind_group_view.m_value, _test_format, 32, 32},
            cpu_bind_group_render_scene);
    }()),
        doctest::Contains("bind group"),
        ofg::EngineError);
}

// Verifies invisible mesh renderers are skipped before draw-list resource validation.
TEST_CASE("renderer skips invisible scene mesh renderers") {
    RendererGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene_with_modes(ofg::GpuContext{}, ofg::GpuContext{}, ofg::GpuContext{});

    for (std::size_t index = 0; index < scene.m_scene.mesh_renderer_count(); ++index) {
        ofg::MeshRenderer* renderer = scene.m_scene.get_mesh_renderer(index);
        REQUIRE(renderer != nullptr);
        renderer->set_visible(false);
    }

    init_prepared_renderer(gpu.borrowed_context());
    ofg::Renderer::resize(32, 32);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());
    const ofg::Scene& render_scene = scene.m_scene;

    CHECK_NOTHROW(
        ofg::Renderer::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 32, 32}, render_scene));

    CHECK(ofg::Renderer::counters().m_pipeline_create_count == 7);
    CHECK(ofg::Renderer::counters().m_buffer_create_count == 13);
    CHECK(ofg::Renderer::counters().m_texture_create_count == 11);
    CHECK(ofg::Renderer::counters().m_texture_view_create_count == 11);
}

// Verifies renderer culling stats distinguish extracted, visible, and rejected objects.
TEST_CASE("renderer culls scene mesh renderers against the camera frustum") {
    RendererGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene(gpu.borrowed_context());
    REQUIRE(scene.m_scene.mesh_renderer_count() == 2);
    REQUIRE(scene.m_scene.get_mesh_renderer(1) != nullptr);
    REQUIRE(scene.m_scene.get_mesh_renderer(1)->entity() != nullptr);
    scene.m_scene.get_mesh_renderer(1)->entity()->local_transform().m_position = ofg::math::vec3(100.0f, 0.0f, 4.0f);

    init_prepared_renderer(gpu.borrowed_context());
    ofg::Renderer::resize(32, 32);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());
    const ofg::Scene& render_scene = scene.m_scene;

    CHECK_NOTHROW(
        ofg::Renderer::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 32, 32}, render_scene));

    const ofg::RendererCullingStats stats = ofg::Renderer::culling_stats();
    CHECK(stats.m_extracted_object_count == 2);
    CHECK(stats.m_camera_visible_object_count == 1);
    CHECK(stats.m_camera_culled_object_count == 1);
}

// Verifies renderer-owned shadow resources consume the current sun and encode all cascades.
TEST_CASE("renderer encodes shadow caster passes for the current sun") {
    RendererGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene(gpu.borrowed_context());
    add_scene_sun(scene.m_scene);

    init_prepared_renderer(gpu.borrowed_context());
    ofg::Renderer::set_shadow_debug_overlay_enabled(true);
    ofg::Renderer::set_overhead_sun_debug_enabled(true);
    ofg::Renderer::resize(32, 32);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());
    const ofg::Scene& render_scene = scene.m_scene;

    CHECK_NOTHROW(
        ofg::Renderer::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 32, 32}, render_scene));

    const ofg::ShadowPassDiagnostics diagnostics = ofg::Renderer::shadow_diagnostics();
    CHECK(ofg::Renderer::shadow_debug_overlay_enabled());
    CHECK(ofg::Renderer::overhead_sun_debug_enabled());
    CHECK(diagnostics.m_enabled);
    CHECK(diagnostics.m_cascade_count == 3U);
    CHECK(diagnostics.m_encoded_pass_count == 3U);
    CHECK(diagnostics.m_map_size == 1024U);
    CHECK(diagnostics.m_estimated_depth_bytes == 1024ULL * 1024ULL * 3ULL * 4ULL);
    CHECK(diagnostics.m_sun_elevation_radians == doctest::Approx(_pi * 0.5f).epsilon(0.001));
    CHECK(diagnostics.m_effective_intensity > 0.0f);
    CHECK(diagnostics.m_total_tested_caster_count == 6U);
    CHECK(diagnostics.m_total_accepted_caster_count > 0U);
    CHECK(diagnostics.m_total_draw_count == diagnostics.m_total_accepted_caster_count);

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = ofg::gpu::cstring_view("OFG renderer shadow debug commands");
    ScopedCommandBuffer command{wgpuCommandEncoderFinish(encoder.m_value, &command_descriptor)};
    encoder.m_value = nullptr;
    REQUIRE(command.m_value != nullptr);
    wgpuQueueSubmit(gpu.borrowed_context().m_queue, 1, &command.m_value);
}

// Verifies scene mesh renderers record into a null-backend render target and finish cleanly.
TEST_CASE("renderer records scene mesh renderers into render targets without steady-state growth") {
    RendererGuard guard;
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    RenderScene scene = make_render_scene(gpu.borrowed_context());

    init_prepared_renderer(gpu.borrowed_context());
    ofg::Renderer::resize(32, 32);

    ScopedTexture texture;
    ScopedTextureView view = make_render_target_view(gpu.borrowed_context(), texture);
    ScopedCommandEncoder encoder = make_encoder(gpu.borrowed_context());
    const ofg::Scene& render_scene = scene.m_scene;

    CHECK_NOTHROW(
        ofg::Renderer::render(encoder.m_value, ofg::RenderTarget{view.m_value, _test_format, 32, 32}, render_scene));

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = ofg::gpu::cstring_view("OFG renderer test commands");
    ScopedCommandBuffer command{wgpuCommandEncoderFinish(encoder.m_value, &command_descriptor)};
    REQUIRE(command.m_value != nullptr);
    wgpuQueueSubmit(gpu.borrowed_context().m_queue, 1, &command.m_value);

    CHECK(ofg::Renderer::counters().m_pipeline_create_count == 8);
    CHECK(ofg::Renderer::counters().m_buffer_create_count == 14);
    CHECK(ofg::Renderer::counters().m_texture_create_count == 11);
    CHECK(ofg::Renderer::counters().m_texture_view_create_count == 11);
    CHECK(ofg::Renderer::counters().m_bind_group_create_count == 19);
    CHECK(ofg::Renderer::bloom_diagnostics().m_active_level_count > 0);
    CHECK(ofg::Renderer::bloom_diagnostics().m_encoded_pass_count > 0);
    CHECK(ofg::Renderer::bloom_diagnostics().m_draw_count == ofg::Renderer::bloom_diagnostics().m_encoded_pass_count);
    CHECK(ofg::Renderer::bloom_diagnostics().m_estimated_read_bytes > 0);
    CHECK(ofg::Renderer::bloom_diagnostics().m_estimated_write_bytes > 0);
    CHECK(ofg::Renderer::bloom_diagnostics().m_skipped == false);
    CHECK(ofg::Renderer::temp_buffer_stats().m_peak_bytes > 0);
    CHECK(ofg::Renderer::temp_buffer_stats().m_created_count > 0);
    CHECK(ofg::Renderer::temp_buffer_stats().m_reusable_count > 0);

    ScopedTexture second_texture;
    ScopedTextureView second_view = make_render_target_view(gpu.borrowed_context(), second_texture);
    ScopedCommandEncoder second_encoder = make_encoder(gpu.borrowed_context());
    ofg::Scene one_command_scene = make_one_object_scene(scene);
    CHECK_NOTHROW(ofg::Renderer::render(
        second_encoder.m_value, ofg::RenderTarget{second_view.m_value, _test_format, 32, 32}, one_command_scene));

    CHECK(ofg::Renderer::counters().m_pipeline_create_count == 8);
    CHECK(ofg::Renderer::counters().m_buffer_create_count == 14);
    CHECK(ofg::Renderer::counters().m_texture_create_count == 11);
    CHECK(ofg::Renderer::counters().m_texture_view_create_count == 11);
    CHECK(ofg::Renderer::counters().m_bind_group_create_count == 19);
    CHECK_NOTHROW(ofg::Renderer::resize(0, 0));
}
