// Doctest coverage for the GPU shadow caster pass and per-cascade culling.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/render/bounds.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/frustum.hpp"
#include "ofg/render/shadow_cascade.hpp"
#include "ofg/render/shadow_caster_pass.hpp"
#include "ofg/render/shadow_map_target.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float _pi = 3.14159265358979323846f;

struct ScopedCommandEncoder {
    WGPUCommandEncoder m_value{nullptr};

    ScopedCommandEncoder() = default;
    explicit ScopedCommandEncoder(WGPUCommandEncoder value) : m_value(value) {}
    ScopedCommandEncoder(const ScopedCommandEncoder&) = delete;
    ScopedCommandEncoder& operator=(const ScopedCommandEncoder&) = delete;
    ScopedCommandEncoder(ScopedCommandEncoder&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    ScopedCommandEncoder& operator=(ScopedCommandEncoder&& other) noexcept = delete;

    // Releases the command encoder if ownership was not consumed by finish.
    ~ScopedCommandEncoder() {
        if (m_value != nullptr) {
            wgpuCommandEncoderRelease(m_value);
        }
    }
};

struct ScopedCommandBuffer {
    WGPUCommandBuffer m_value{nullptr};

    ScopedCommandBuffer() = default;
    explicit ScopedCommandBuffer(WGPUCommandBuffer value) : m_value(value) {}
    ScopedCommandBuffer(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer& operator=(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer(ScopedCommandBuffer&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    ScopedCommandBuffer& operator=(ScopedCommandBuffer&& other) noexcept = delete;

    // Releases the command buffer after optional queue submission.
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

// Creates a command encoder suitable for shadow pass submission.
ScopedCommandEncoder create_encoder(WGPUDevice device) {
    WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    descriptor.label = ofg::gpu::cstring_view("OFG shadow caster test encoder");
    ScopedCommandEncoder encoder{wgpuDeviceCreateCommandEncoder(device, &descriptor)};
    REQUIRE(encoder.m_value != nullptr);
    return encoder;
}

// Returns a tiny triangle mesh with one submesh.
std::unique_ptr<ofg::Mesh> make_shadow_mesh(ofg::GpuContext gpu, ofg::Material& material) {
    std::vector<ofg::MeshVertex> vertices{
        ofg::MeshVertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        ofg::MeshVertex{{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        ofg::MeshVertex{{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
    };
    std::vector<std::uint32_t> indices{0U, 1U, 2U};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"shadow triangle", 0U, 3U, &material}};
    std::unique_ptr<ofg::Mesh> mesh = std::make_unique<ofg::Mesh>(gpu, "shadow caster test mesh");
    mesh->init(std::move(vertices), std::move(indices), std::move(submeshes));
    return mesh;
}

// Builds the camera used by shadow pass tests.
ofg::CameraProperties make_shadow_camera() {
    return ofg::camera_properties_from_look_at(nullptr,
        ofg::math::vec3(0.0f, 2.0f, 0.0f),
        ofg::math::vec3(0.0f, 1.0f, 10.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        _pi / 3.0f,
        1.0f,
        0.25f,
        100.0f);
}

// Creates a render object with coherent model and world bounds.
ofg::RenderObject render_object_at(ofg::Mesh& mesh, ofg::math::Vec3 position) {
    ofg::RenderObject object;
    object.m_mesh = &mesh;
    object.m_model = ofg::math::mat4_translation(position);
    object.m_local_bounds = mesh.local_bounds();
    object.m_world_bounds = ofg::transform_bounds(object.m_local_bounds, object.m_model);
    object.m_sort_origin = position;
    return object;
}

// Creates an object inside a cascade caster volume but outside the camera frustum.
ofg::RenderObject off_camera_caster_for(ofg::Mesh& mesh, const ofg::ShadowCascade& cascade) {
    const ofg::math::Vec3 light_center =
        ofg::math::vec3((cascade.m_light_space_bounds.m_min.x + cascade.m_light_space_bounds.m_max.x) * 0.5f,
            (cascade.m_light_space_bounds.m_min.y + cascade.m_light_space_bounds.m_max.y) * 0.5f,
            cascade.m_light_space_bounds.m_min.z + 2.0f);
    const ofg::math::Vec3 world_center = ofg::math::transform_point(cascade.m_world_from_light, light_center);
    return render_object_at(mesh, world_center);
}

} // namespace

// Verifies the caster pass culls from extracted render objects and records shadow map passes.
TEST_CASE("shadow caster pass renders cascades with pass-specific culling") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::Material material(ofg::GpuContext{}, "shadow caster test material");
    std::unique_ptr<ofg::Mesh> mesh = make_shadow_mesh(gpu.borrowed_context(), material);

    ofg::ShadowSettings settings;
    settings.m_map_size = 64U;
    settings.m_cascade_end_distances = {8.0f, 24.0f, 64.0f};
    settings.m_cascade_blend_widths = {1.0f, 2.0f, 4.0f};
    settings.m_caster_depth_padding = 40.0f;
    const ofg::CameraProperties camera = make_shadow_camera();
    std::string light_error;
    const std::optional<ofg::math::Vec3> maybe_light_direction =
        ofg::math::normalize(ofg::math::vec3(-0.75f, -0.6f, 0.25f), light_error);
    REQUIRE_MESSAGE(maybe_light_direction.has_value(), light_error);
    const ofg::math::Vec3 light_direction = *maybe_light_direction;
    const ofg::ShadowCascadeSet cascades = ofg::build_shadow_cascades(camera, light_direction, settings);

    std::vector<ofg::RenderObject> render_objects;
    render_objects.push_back(render_object_at(*mesh, ofg::math::vec3(0.0f, 1.0f, 5.0f)));
    render_objects.push_back(render_object_at(*mesh, ofg::math::vec3(0.0f, 1.0f, 18.0f)));
    render_objects.push_back(render_object_at(*mesh, ofg::math::vec3(0.0f, 1.0f, 50.0f)));
    render_objects.push_back(render_object_at(*mesh, ofg::math::vec3(250.0f, 1.0f, 5.0f)));

    ofg::RenderObject off_camera_caster = off_camera_caster_for(*mesh, cascades.m_cascades[0]);
    const ofg::ViewFrustum camera_frustum = ofg::view_frustum_from_camera(camera);
    REQUIRE_FALSE(ofg::intersects_culling_planes(off_camera_caster.m_world_bounds, camera_frustum.plane_set()));
    REQUIRE(ofg::intersects_culling_planes(off_camera_caster.m_world_bounds, cascades.m_cascades[0].plane_set()));
    render_objects.push_back(off_camera_caster);

    ofg::ShadowMapTarget target(gpu.borrowed_context());
    target.resize(settings.m_map_size);
    std::unique_ptr<ofg::ShadowCasterPass> pass =
        ofg::ShadowCasterPass::create(gpu.borrowed_context(), ofg::ShadowMapTarget::format());
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    REQUIRE_NOTHROW(pass->render(encoder.m_value, target, cascades, settings, render_objects));
    const ofg::ShadowPassDiagnostics diagnostics = pass->diagnostics();
    CHECK(diagnostics.m_enabled);
    CHECK(diagnostics.m_cascade_count == 3U);
    CHECK(diagnostics.m_encoded_pass_count == 3U);
    CHECK(diagnostics.m_map_size == settings.m_map_size);
    CHECK(diagnostics.m_estimated_depth_bytes == target.estimated_depth_bytes());
    CHECK(diagnostics.m_effective_intensity > 0.0f);
    CHECK(diagnostics.m_total_tested_caster_count == 15U);
    CHECK(diagnostics.m_total_accepted_caster_count > 0U);
    CHECK(diagnostics.m_total_rejected_caster_count > 0U);
    CHECK(diagnostics.m_total_draw_count == diagnostics.m_total_accepted_caster_count);
    CHECK(diagnostics.m_total_submesh_count == diagnostics.m_total_draw_count);
    CHECK(diagnostics.m_total_index_count == diagnostics.m_total_submesh_count * 3U);
    CHECK(diagnostics.m_cascades[0].m_accepted_caster_count >= 2U);

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = ofg::gpu::cstring_view("OFG shadow caster test commands");
    ScopedCommandBuffer commands{wgpuCommandEncoderFinish(encoder.m_value, &command_descriptor)};
    encoder.m_value = nullptr;
    REQUIRE(commands.m_value != nullptr);
    wgpuQueueSubmit(gpu.borrowed_context().m_queue, 1, &commands.m_value);

    CHECK(pass->counters().m_pipeline_create_count == 1U);
    CHECK(pass->counters().m_buffer_create_count >= static_cast<std::uint32_t>(ofg::shadow_cascade_count()) * 2U);
    CHECK(pass->counters().m_bind_group_create_count >= static_cast<std::uint32_t>(ofg::shadow_cascade_count()) * 2U);
    CHECK(pass->counters().m_shader_module_create_count == 1U);
}

// Verifies construction and render validation fail before encoding invalid GPU work.
TEST_CASE("shadow caster pass validates construction, targets, settings, and mesh buffers") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::ShadowCasterPass::create(ofg::GpuContext{}, ofg::ShadowMapTarget::format()); }()),
        doctest::Contains("device and queue"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)ofg::ShadowCasterPass::create(gpu.borrowed_context(), WGPUTextureFormat_Undefined); }()),
        doctest::Contains("defined depth format"),
        ofg::EngineError);

    ofg::Material material(ofg::GpuContext{}, "shadow caster validation material");
    std::unique_ptr<ofg::Mesh> gpu_mesh = make_shadow_mesh(gpu.borrowed_context(), material);
    std::unique_ptr<ofg::Mesh> cpu_mesh = make_shadow_mesh(ofg::GpuContext{}, material);

    ofg::ShadowSettings settings;
    settings.m_map_size = 32U;
    settings.m_cascade_end_distances = {8.0f, 24.0f, 64.0f};
    settings.m_cascade_blend_widths = {1.0f, 2.0f, 4.0f};
    const ofg::ShadowCascadeSet cascades =
        ofg::build_shadow_cascades(make_shadow_camera(), ofg::math::vec3(-0.5f, -0.7f, 0.25f), settings);

    ofg::ShadowMapTarget target(gpu.borrowed_context());
    target.resize(settings.m_map_size);
    ofg::ShadowMapTarget empty_target(gpu.borrowed_context());
    std::unique_ptr<ofg::ShadowCasterPass> pass =
        ofg::ShadowCasterPass::create(gpu.borrowed_context(), ofg::ShadowMapTarget::format());
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    std::vector<ofg::RenderObject> gpu_objects{render_object_at(*gpu_mesh, ofg::math::vec3(0.0f, 1.0f, 5.0f))};
    CHECK_THROWS_WITH_AS(pass->render(nullptr, target, cascades, settings, gpu_objects),
        doctest::Contains("command encoder"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(pass->render(encoder.m_value, empty_target, cascades, settings, gpu_objects),
        doctest::Contains("live shadow map target"),
        ofg::EngineError);

    ofg::ShadowSettings invalid_settings = settings;
    invalid_settings.m_map_size = 0U;
    CHECK_THROWS_WITH_AS(pass->render(encoder.m_value, target, cascades, invalid_settings, gpu_objects),
        doctest::Contains("Shadow map size"),
        ofg::EngineError);

    std::vector<ofg::RenderObject> cpu_objects{render_object_at(*cpu_mesh, ofg::math::vec3(0.0f, 1.0f, 5.0f))};
    CHECK_THROWS_WITH_AS(pass->render(encoder.m_value, target, cascades, settings, cpu_objects),
        doctest::Contains("GPU-ready"),
        ofg::EngineError);

    ofg::RenderObject null_mesh_object;
    null_mesh_object.m_world_bounds = cascades.m_cascades[0].m_receiver_world_bounds;
    std::vector<ofg::RenderObject> null_mesh_objects{null_mesh_object};
    CHECK_THROWS_WITH_AS(pass->render(encoder.m_value, target, cascades, settings, null_mesh_objects),
        doctest::Contains("mesh must not be null"),
        ofg::EngineError);

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = ofg::gpu::cstring_view("OFG shadow validation test commands");
    ScopedCommandBuffer commands{wgpuCommandEncoderFinish(encoder.m_value, &command_descriptor)};
    encoder.m_value = nullptr;
    REQUIRE(commands.m_value != nullptr);
}

// Verifies disabled or fully faded shadows skip pass encoding while preserving diagnostics.
TEST_CASE("shadow caster pass skips disabled and faded shadow work") {
    ofg::tests::TestGpuContext gpu = make_test_gpu();
    ofg::Material material(ofg::GpuContext{}, "shadow caster disabled material");
    std::unique_ptr<ofg::Mesh> mesh = make_shadow_mesh(gpu.borrowed_context(), material);

    ofg::ShadowSettings settings;
    settings.m_enabled = false;
    settings.m_map_size = 32U;
    settings.m_cascade_end_distances = {8.0f, 24.0f, 64.0f};
    settings.m_cascade_blend_widths = {1.0f, 2.0f, 4.0f};
    const ofg::ShadowCascadeSet cascades =
        ofg::build_shadow_cascades(make_shadow_camera(), ofg::math::vec3(0.0f, -1.0f, 0.0f), settings);
    std::vector<ofg::RenderObject> render_objects{render_object_at(*mesh, ofg::math::vec3(0.0f, 1.0f, 5.0f))};

    ofg::ShadowMapTarget target(gpu.borrowed_context());
    target.resize(settings.m_map_size);
    std::unique_ptr<ofg::ShadowCasterPass> pass =
        ofg::ShadowCasterPass::create(gpu.borrowed_context(), ofg::ShadowMapTarget::format());
    ScopedCommandEncoder encoder = create_encoder(gpu.borrowed_context().m_device);

    REQUIRE_NOTHROW(pass->render(encoder.m_value, target, cascades, settings, render_objects));
    const ofg::ShadowPassDiagnostics diagnostics = pass->diagnostics();
    CHECK_FALSE(diagnostics.m_enabled);
    CHECK(diagnostics.m_encoded_pass_count == 0U);
    CHECK(diagnostics.m_total_tested_caster_count == 0U);
    CHECK(diagnostics.m_map_size == settings.m_map_size);
    CHECK(diagnostics.m_effective_intensity == 0.0f);

    settings.m_enabled = true;
    const ofg::ShadowCascadeSet faded_cascades =
        ofg::build_shadow_cascades(make_shadow_camera(), ofg::math::vec3(0.0f, 1.0f, 0.0f), settings);
    REQUIRE_NOTHROW(pass->render(encoder.m_value, target, faded_cascades, settings, render_objects));
    const ofg::ShadowPassDiagnostics faded_diagnostics = pass->diagnostics();
    CHECK_FALSE(faded_diagnostics.m_enabled);
    CHECK(faded_diagnostics.m_encoded_pass_count == 0U);
    CHECK(faded_diagnostics.m_total_tested_caster_count == 0U);
    CHECK(faded_diagnostics.m_effective_intensity == 0.0f);

    WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    command_descriptor.label = ofg::gpu::cstring_view("OFG shadow disabled test commands");
    ScopedCommandBuffer commands{wgpuCommandEncoderFinish(encoder.m_value, &command_descriptor)};
    encoder.m_value = nullptr;
    REQUIRE(commands.m_value != nullptr);
}
