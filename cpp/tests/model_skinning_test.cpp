// Doctest coverage for imported model CPU skinning.
//
// These tests prove that glTF skin metadata and animation-driven joint entity
// transforms produce per-instance dynamic skinned mesh vertices.
#include "doctest.h"

#include "webgpu_test_utils.hpp"

#include "ofg/assets/gltf_document.hpp"
#include "ofg/assets/gltf_importer.hpp"
#include "ofg/assets/model_resource.hpp"
#include "ofg/core/control_input.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

// Returns the repository test asset directory supplied by CMake.
std::filesystem::path asset_dir() {
    return std::filesystem::path{OFG_TEST_ASSET_DIR};
}

class ScopedResources {
public:
    // Creates central Resources storage backed by a Dawn null device.
    ScopedResources() {
        std::string error;
        m_owned_gpu = ofg::tests::TestGpuContext::create(error);
        REQUIRE_MESSAGE(m_owned_gpu.has_value(), error);
        create_resources(m_owned_gpu->borrowed_context());
    }

    // Creates central Resources storage borrowing an externally-owned test GPU.
    explicit ScopedResources(ofg::GpuContext gpu) {
        create_resources(gpu);
    }

    ScopedResources(const ScopedResources&) = delete;
    ScopedResources& operator=(const ScopedResources&) = delete;

    // Releases Resources before any borrowed test GPU goes away.
    ~ScopedResources() {
        if (ofg::Resources::state() != ofg::ResourcesLifecycleState::Uninitialized) {
            (void)ofg::Resources::release();
            ofg::Resources::destroy();
        }
    }

private:
    void create_resources(ofg::GpuContext gpu) {
        ofg::Resources::destroy();
        ofg::Resources::create(gpu);
        REQUIRE(ofg::Resources::prepare());
    }

    std::optional<ofg::tests::TestGpuContext> m_owned_gpu;
};

// Imports one fixture model through the glTF ModelResource path.
std::unique_ptr<ofg::ModelResource> import_fixture_model(
    std::string model_name, std::string file_name, ofg::ModelResourceLoader& loader) {
    const std::filesystem::path path = asset_dir() / file_name;
    const ofg::GltfDocument document = ofg::load_gltf_document_from_path(path);
    return ofg::import_gltf_model_resource(
        document, ofg::GltfImportOptions{std::move(model_name), "assets/models/tests/" + file_name}, loader);
}

// Returns the squared distance between two vertex positions.
float vertex_position_delta_squared(const ofg::MeshVertex& a, const ofg::MeshVertex& b) noexcept {
    const ofg::math::Vec3 delta = ofg::math::vec3(
        a.m_position[0] - b.m_position[0], a.m_position[1] - b.m_position[1], a.m_position[2] - b.m_position[2]);
    return ofg::math::length_squared(delta);
}

// Returns whether any vertex position differs by more than a tiny epsilon.
bool any_vertex_position_changed(std::span<const ofg::MeshVertex> a, std::span<const ofg::MeshVertex> b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (vertex_position_delta_squared(a[index], b[index]) > 0.000001F) {
            return true;
        }
    }
    return false;
}

// Copies a mesh vertex span into stable test storage.
std::vector<ofg::MeshVertex> copy_vertices(std::span<const ofg::MeshVertex> vertices) {
    return std::vector<ofg::MeshVertex>(vertices.begin(), vertices.end());
}

} // namespace

// Verifies simple-skin.gltf produces CPU-skinned vertices from current joint entities.
TEST_CASE("CPU skinning updates imported simple skin dynamic mesh") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_fixture_model("simple-skin", "simple-skin.gltf", loader);
    REQUIRE(model != nullptr);
    REQUIRE(model->animation_clip_count() == 1);
    ofg::AnimationClip* clip = model->animation_clip(0);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->channels().size() == 1);

    ofg::Scene scene;
    ofg::ModelInstance instance = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    REQUIRE(instance.m_animation_player != nullptr);
    REQUIRE(instance.m_mesh_renderers.size() == 1);
    ofg::MeshRenderer* renderer = instance.m_mesh_renderers[0].get();
    REQUIRE(renderer != nullptr);
    ofg::SkinBinding* binding = renderer->skin_binding();
    REQUIRE(binding != nullptr);
    REQUIRE(binding->m_bind_pose_mesh != nullptr);
    REQUIRE(binding->m_dynamic_skinned_mesh != nullptr);
    REQUIRE(binding->m_dynamic_skinned_mesh->is_dynamic_vertex_mesh());
    CHECK(renderer->bind_pose_mesh() == binding->m_bind_pose_mesh.get());
    CHECK(renderer->mesh() == binding->m_dynamic_skinned_mesh.get());
    REQUIRE(binding->m_vertex_influences.size() == binding->m_bind_pose_mesh->vertices().size());
    CHECK(binding->m_vertex_influences[0].m_weights[0] + binding->m_vertex_influences[0].m_weights[1] +
              binding->m_vertex_influences[0].m_weights[2] + binding->m_vertex_influences[0].m_weights[3] ==
          doctest::Approx(1.0F));

    ofg::ControlInput controls;
    ofg::SceneUpdateContext update_context{controls, 1000.0, 0.0F, nullptr, nullptr};
    scene.update(update_context);
    const std::vector<ofg::MeshVertex> rest_vertices = copy_vertices(renderer->mesh()->vertices());
    REQUIRE(rest_vertices.size() == binding->m_bind_pose_mesh->vertices().size());
    for (std::size_t index = 0; index < rest_vertices.size(); ++index) {
        CHECK(vertex_position_delta_squared(rest_vertices[index], binding->m_bind_pose_mesh->vertices()[index]) <
              0.000001F);
    }
    const ofg::SkinningCounters rest_counters = renderer->skinning_counters();
    CHECK(rest_counters.m_vertices_skinned == rest_vertices.size());

    instance.m_animation_player->play(*clip, false);
    REQUIRE(clip->channels()[0].m_input_times_seconds.size() > 1);
    instance.m_animation_player->set_time_seconds(clip->channels()[0].m_input_times_seconds[1]);
    scene.update(update_context);
    const std::vector<ofg::MeshVertex> animated_vertices = copy_vertices(renderer->mesh()->vertices());
    CHECK(any_vertex_position_changed(rest_vertices, animated_vertices));
    const ofg::SkinningCounters animated_counters = renderer->skinning_counters();
    CHECK(animated_counters.m_vertices_skinned == rest_vertices.size() * 2U);
    CHECK(animated_counters.m_dynamic_vertex_buffer_create_count == rest_counters.m_dynamic_vertex_buffer_create_count);

    instance.m_animation_player->set_time_seconds(clip->channels()[0].m_input_times_seconds[1]);
    instance.m_animation_player->update(update_context);
    instance.m_entities_by_node_index[2]->local_transform().m_position = ofg::math::vec3(0.0F, 2.0F, 0.0F);
    renderer->update_skinning();
    const std::vector<ofg::MeshVertex> overridden_vertices = copy_vertices(renderer->mesh()->vertices());
    CHECK(any_vertex_position_changed(animated_vertices, overridden_vertices));

    ofg::Mesh* bind_pose_mesh = renderer->bind_pose_mesh();
    REQUIRE(bind_pose_mesh != nullptr);
    renderer->set_mesh(bind_pose_mesh);
    REQUIRE(renderer->skin_binding() != nullptr);
    CHECK(renderer->mesh() != bind_pose_mesh);

    const ofg::MeshRenderer* const_renderer = renderer;
    CHECK(const_renderer->skin_binding() != nullptr);
    renderer->clear_skin_binding();
    CHECK(renderer->skin_binding() == nullptr);
    CHECK(renderer->mesh() == bind_pose_mesh);
    CHECK(renderer->skinning_counters().m_vertices_skinned == 0);
    CHECK_NOTHROW(renderer->update_skinning());
}

// Verifies each skinned model instance owns separate dynamic output while sharing bind-pose resources.
TEST_CASE("CPU skinning uses per-instance dynamic meshes") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_fixture_model("simple-skin", "simple-skin.gltf", loader);
    REQUIRE(model != nullptr);

    ofg::Scene scene;
    ofg::ModelInstance first = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    ofg::ModelInstance second = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    REQUIRE(first.m_mesh_renderers.size() == 1);
    REQUIRE(second.m_mesh_renderers.size() == 1);
    ofg::SkinBinding* first_binding = first.m_mesh_renderers[0]->skin_binding();
    ofg::SkinBinding* second_binding = second.m_mesh_renderers[0]->skin_binding();
    REQUIRE(first_binding != nullptr);
    REQUIRE(second_binding != nullptr);
    CHECK(first_binding->m_bind_pose_mesh.get() == second_binding->m_bind_pose_mesh.get());
    REQUIRE(first_binding->m_dynamic_skinned_mesh != nullptr);
    REQUIRE(second_binding->m_dynamic_skinned_mesh != nullptr);
    CHECK(first_binding->m_dynamic_skinned_mesh.get() != second_binding->m_dynamic_skinned_mesh.get());

    ofg::ControlInput controls;
    ofg::SceneUpdateContext update_context{controls, 1000.0, 0.0F, nullptr, nullptr};
    scene.update(update_context);
    CHECK(first.m_mesh_renderers[0]->skinning_counters().m_vertices_skinned ==
          first_binding->m_bind_pose_mesh->vertices().size());
    CHECK(second.m_mesh_renderers[0]->skinning_counters().m_vertices_skinned ==
          second_binding->m_bind_pose_mesh->vertices().size());
}

// Verifies CPU skinning updates an existing GPU vertex buffer during ordinary frames.
TEST_CASE("CPU skinning keeps dynamic GPU vertex buffer creation flat") {
    std::string gpu_error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(gpu_error);
    REQUIRE_MESSAGE(gpu.has_value(), gpu_error);

    ScopedResources resources{gpu->borrowed_context()};
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_fixture_model("simple-skin", "simple-skin.gltf", loader);
    REQUIRE(model != nullptr);

    ofg::Scene scene;
    ofg::ModelInstance instance = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    REQUIRE(instance.m_mesh_renderers.size() == 1);
    ofg::MeshRenderer* renderer = instance.m_mesh_renderers[0].get();
    REQUIRE(renderer != nullptr);
    REQUIRE(renderer->skin_binding() != nullptr);
    ofg::Mesh* dynamic_mesh = renderer->mesh();
    REQUIRE(dynamic_mesh != nullptr);
    REQUIRE(dynamic_mesh->is_dynamic_vertex_mesh());

    ofg::ControlInput controls;
    ofg::SceneUpdateContext update_context{controls, 1000.0, 0.0F, nullptr, nullptr};
    scene.update(update_context);
    const std::uint64_t create_count = dynamic_mesh->vertex_buffer_create_count();
    const std::uint64_t upload_bytes = dynamic_mesh->vertex_upload_bytes();
    scene.update(update_context);

    CHECK(dynamic_mesh->vertex_buffer_create_count() == create_count);
    CHECK(dynamic_mesh->vertex_upload_bytes() ==
          upload_bytes + sizeof(ofg::MeshVertex) * dynamic_mesh->vertices().size());
    CHECK(renderer->skinning_counters().m_dynamic_vertex_buffer_create_count == create_count);
}

// Verifies mesh renderer skinning validation catches malformed bindings and stale caches.
TEST_CASE("CPU skinning reports invalid mesh renderer bindings") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_fixture_model("simple-skin", "simple-skin.gltf", loader);
    REQUIRE(model != nullptr);

    ofg::Scene scene;
    ofg::ModelInstance instance = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    REQUIRE(instance.m_mesh_renderers.size() == 1);
    ofg::MeshRenderer* renderer = instance.m_mesh_renderers[0].get();
    REQUIRE(renderer != nullptr);
    ofg::Mesh* bind_pose_mesh = renderer->bind_pose_mesh();
    REQUIRE(bind_pose_mesh != nullptr);

    ofg::Entity* plain_entity = scene.create_entity(scene.get_root());
    REQUIRE(plain_entity != nullptr);
    auto* plain_renderer =
        static_cast<ofg::MeshRenderer*>(plain_entity->create_component(ofg::ComponentType::MeshRenderer));
    CHECK(plain_renderer->skinning_counters().m_vertices_skinned == 0);
    CHECK_NOTHROW(plain_renderer->update_skinning());

    ofg::SkinBinding missing_mesh_binding;
    missing_mesh_binding.m_joints_in_skin_order.push_back(instance.m_entities_by_node_index[0]);
    missing_mesh_binding.m_inverse_bind_matrices.push_back(ofg::math::mat4_identity());
    missing_mesh_binding.m_vertex_influences.resize(bind_pose_mesh->vertices().size());
    CHECK_THROWS_WITH_AS(plain_renderer->set_skin_binding(std::move(missing_mesh_binding)),
        doctest::Contains("bind-pose mesh"),
        ofg::EngineError);
    plain_renderer->clear_skin_binding();

    plain_renderer->set_mesh(bind_pose_mesh);
    ofg::SkinBinding empty_joint_binding;
    empty_joint_binding.m_inverse_bind_matrices.push_back(ofg::math::mat4_identity());
    empty_joint_binding.m_vertex_influences.resize(bind_pose_mesh->vertices().size());
    CHECK_THROWS_WITH_AS(plain_renderer->set_skin_binding(std::move(empty_joint_binding)),
        doctest::Contains("at least one joint"),
        ofg::EngineError);
    plain_renderer->clear_skin_binding();

    ofg::SkinBinding mismatched_inverse_bindings;
    mismatched_inverse_bindings.m_joints_in_skin_order.push_back(instance.m_entities_by_node_index[0]);
    mismatched_inverse_bindings.m_vertex_influences.resize(bind_pose_mesh->vertices().size());
    CHECK_THROWS_WITH_AS(plain_renderer->set_skin_binding(std::move(mismatched_inverse_bindings)),
        doctest::Contains("inverse bind matrix count"),
        ofg::EngineError);
    plain_renderer->clear_skin_binding();

    ofg::SkinBinding mismatched_influences;
    mismatched_influences.m_joints_in_skin_order.push_back(instance.m_entities_by_node_index[0]);
    mismatched_influences.m_inverse_bind_matrices.push_back(ofg::math::mat4_identity());
    CHECK_THROWS_WITH_AS(plain_renderer->set_skin_binding(std::move(mismatched_influences)),
        doctest::Contains("influence count"),
        ofg::EngineError);
    plain_renderer->clear_skin_binding();

    ofg::SkinBinding outside_joint_binding;
    outside_joint_binding.m_joints_in_skin_order.push_back(instance.m_entities_by_node_index[0]);
    outside_joint_binding.m_inverse_bind_matrices.push_back(ofg::math::mat4_identity());
    outside_joint_binding.m_vertex_influences.resize(bind_pose_mesh->vertices().size());
    outside_joint_binding.m_vertex_influences[0].m_joint_indices[0] = 1;
    outside_joint_binding.m_vertex_influences[0].m_weights[0] = 1.0F;
    CHECK_THROWS_WITH_AS(plain_renderer->set_skin_binding(std::move(outside_joint_binding)),
        doctest::Contains("outside the skin binding"),
        ofg::EngineError);
    plain_renderer->clear_skin_binding();

    std::vector<ofg::math::Mat4> undersized_world_cache{ofg::math::mat4_identity()};
    CHECK_THROWS_WITH_AS(
        renderer->update_skinning(undersized_world_cache), doctest::Contains("cache"), ofg::EngineError);
}
