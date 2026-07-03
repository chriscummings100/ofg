// Doctest coverage for the OFG scene entity/component graph.
//
// These tests pin the first ECS API: root creation, child/sibling traversal,
// mesh-renderer component storage, generation invalidation, and transform
// composition used by renderer draw-list extraction.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/core/control_input.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <webgpu/webgpu.h>

namespace {

// Returns a Y-axis quaternion or fails the current doctest.
ofg::math::Quat require_y_rotation(float radians) {
    std::string error;
    std::optional<ofg::math::Quat> rotation =
        ofg::math::quat_from_axis_angle(ofg::math::vec3(0.0f, 1.0f, 0.0f), radians, error);
    REQUIRE_MESSAGE(rotation.has_value(), error);
    return *rotation;
}

// Produces a non-null opaque WebGPU device handle for resource facade tests.
WGPUDevice fake_device() {
    return reinterpret_cast<WGPUDevice>(static_cast<std::uintptr_t>(20));
}

// Produces a non-null opaque WebGPU queue handle for resource facade tests.
WGPUQueue fake_queue() {
    return reinterpret_cast<WGPUQueue>(static_cast<std::uintptr_t>(21));
}

// Returns the player asset directory supplied by CMake.
std::filesystem::path player_asset_dir() {
    return std::filesystem::path{OFG_PLAYER_ASSET_DIR};
}

// Reads one player runtime asset into byte storage.
std::vector<std::byte> read_player_asset_bytes(std::string_view filename) {
    const std::filesystem::path path = player_asset_dir() / std::filesystem::path{std::string(filename)};
    std::ifstream file(path, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "Could not open player asset " << path.string());
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    REQUIRE(size >= 0);
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    REQUIRE_MESSAGE(file.good(), "Could not read player asset " << path.string());
    return bytes;
}

// Creates and prepares Resources with a real Dawn null-backend device.
ofg::tests::TestGpuContext create_real_test_resources() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);

    ofg::Resources::destroy();
    ofg::Resources::create(gpu->borrowed_context());
    REQUIRE(ofg::Resources::prepare());
    return std::move(*gpu);
}

// Completes queued player model-resource blob requests from the checked-in assets.
void complete_player_asset_blob_requests() {
    const std::vector<ofg::PendingBlobLoad> pending(
        ofg::Resources::pending_blob_loads().begin(), ofg::Resources::pending_blob_loads().end());
    for (const ofg::PendingBlobLoad& request : pending) {
        ofg::Resources::mark_blob_loading(request.m_id);
        if (request.m_uri.ends_with("quaternius-superhero-male.glb")) {
            ofg::Resources::complete_blob_load(request.m_id, read_player_asset_bytes("quaternius-superhero-male.glb"));
        } else if (request.m_uri.ends_with("quaternius-ual1-standard.glb")) {
            ofg::Resources::complete_blob_load(request.m_id, read_player_asset_bytes("quaternius-ual1-standard.glb"));
        } else {
            FAIL("Unexpected player asset blob request: " << request.m_uri);
        }
    }
}

} // namespace

// Verifies a scene always starts with one stable root entity.
TEST_CASE("scene creates a root entity and resolves ids") {
    ofg::Scene scene;

    ofg::Entity* root = scene.get_root();
    REQUIRE(root != nullptr);
    CHECK(root->id() == 0);
    CHECK(root->parent() == nullptr);
    CHECK(scene.entity_count() == 1);
    CHECK(scene.get_entity(0) == root);
    CHECK(scene.get_entity(12) == nullptr);
    const ofg::Scene& const_scene = scene;
    CHECK(const_scene.get_root() == root);
    CHECK(const_scene.get_entity(12) == nullptr);
    CHECK(scene.mesh_renderer_count() == 0);
    CHECK(scene.get_mesh_renderer(0) == nullptr);
    CHECK(const_scene.get_mesh_renderer(0) == nullptr);
}

// Verifies entity creation links children in stable creation order.
TEST_CASE("scene links entities into a child sibling tree") {
    ofg::Scene scene;
    ofg::Entity* root = scene.get_root();
    ofg::Entity* first = scene.create_entity(root);
    ofg::Entity* second = scene.create_entity(root);
    ofg::Entity* grandchild = scene.create_entity(first);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(grandchild != nullptr);
    CHECK(first->id() == 1);
    CHECK(second->id() == 2);
    CHECK(grandchild->id() == 3);
    CHECK(root->first_child() == first);
    CHECK(first->next_sibling() == second);
    CHECK(second->next_sibling() == nullptr);
    CHECK(first->first_child() == grandchild);
    CHECK(grandchild->parent() == first);
}

// Verifies invalid parents are rejected clearly.
TEST_CASE("scene rejects invalid entity parents") {
    ofg::Scene scene;
    ofg::Scene other_scene;

    CHECK_THROWS_WITH_AS(
        ([&]() { (void)scene.create_entity(nullptr); }()), doctest::Contains("parent"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)scene.create_entity(other_scene.get_root()); }()),
        doctest::Contains("same scene"),
        ofg::EngineError);
}

// Verifies scene-owned lighting has validated defaults and authoring setters.
TEST_CASE("scene stores main directional and ambient lighting") {
    ofg::Scene scene;

    CHECK(scene.main_light().m_direction.y == doctest::Approx(-1.0f));
    CHECK(scene.ambient_light().m_intensity == doctest::Approx(0.08f));

    scene.set_main_light(
        ofg::DirectionalLight{ofg::math::vec3(0.0f, -2.0f, 0.0f), ofg::math::vec3(1.0f, 0.9f, 0.8f), 2.5f});
    CHECK(scene.main_light().m_direction.y == doctest::Approx(-1.0f));
    CHECK(scene.main_light().m_intensity == doctest::Approx(2.5f));

    scene.set_ambient_light(ofg::AmbientLight{ofg::math::vec3(0.2f, 0.3f, 0.4f), 0.15f});
    CHECK(scene.ambient_light().m_color.z == doctest::Approx(0.4f));
    CHECK(scene.ambient_light().m_intensity == doctest::Approx(0.15f));

    CHECK_THROWS_WITH_AS(([&]() {
        scene.set_main_light(
            ofg::DirectionalLight{ofg::math::vec3(0.0f, 0.0f, 0.0f), ofg::math::vec3(1.0f, 1.0f, 1.0f), 1.0f});
    }()),
        doctest::Contains("normalize"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { scene.set_ambient_light(ofg::AmbientLight{ofg::math::vec3(-1.0f, 0.0f, 0.0f), 1.0f}); }()),
        doctest::Contains("non-negative"),
        ofg::EngineError);
}

// Verifies mesh renderer components are scene-owned and exposed by index.
TEST_CASE("scene creates mesh renderer components in stable order") {
    ofg::Scene scene;
    ofg::Entity* first = scene.create_entity(scene.get_root());
    ofg::Entity* second = scene.create_entity(scene.get_root());

    ofg::Component* first_component = first->create_component(ofg::ComponentType::MeshRenderer);
    ofg::Component* second_component = second->create_component(ofg::ComponentType::MeshRenderer);

    REQUIRE(first_component != nullptr);
    REQUIRE(second_component != nullptr);
    CHECK(first_component->type() == ofg::ComponentType::MeshRenderer);
    CHECK(first_component->entity() == first);
    CHECK(first->mesh_renderer() == first_component);
    CHECK(second->mesh_renderer() == second_component);
    CHECK(scene.mesh_renderer_count() == 2);
    CHECK(scene.get_mesh_renderer(0) == first->mesh_renderer());
    CHECK(scene.get_mesh_renderer(1) == second->mesh_renderer());
    CHECK(scene.get_mesh_renderer(2) == nullptr);
    CHECK_THROWS_WITH_AS(([&]() { (void)second->create_component(static_cast<ofg::ComponentType>(99)); }()),
        doctest::Contains("unknown component"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)first->create_component(ofg::ComponentType::MeshRenderer); }()),
        doctest::Contains("MeshRenderer"),
        ofg::EngineError);
}

// Verifies mesh renderer accessors expose the intended authoring surface.
TEST_CASE("mesh renderer accessors update draw extraction state") {
    ofg::Scene scene;
    ofg::Entity* entity = scene.create_entity(scene.get_root());
    (void)entity->create_component(ofg::ComponentType::MeshRenderer);
    ofg::MeshRenderer* renderer = entity->mesh_renderer();
    REQUIRE(renderer != nullptr);

    ofg::Mesh mesh{ofg::GpuContext{}, "scene test mesh"};
    renderer->set_mesh(&mesh);
    renderer->properties().set("tint", ofg::math::vec4(0.25f, 0.5f, 0.75f, 1.0f));
    renderer->material_overrides().push_back(ofg::MaterialOverride{0, nullptr});
    renderer->set_material_overrides({ofg::MaterialOverride{2, nullptr}});
    renderer->set_sort_origin_offset(ofg::math::vec3(1.0f, 2.0f, 3.0f));

    const ofg::MeshRenderer* const_renderer = renderer;
    CHECK(const_renderer->mesh() == &mesh);
    CHECK(const_renderer->properties().size() == 1);
    REQUIRE(const_renderer->material_overrides().size() == 1);
    CHECK(const_renderer->material_overrides()[0].m_submesh_index == 2);
    CHECK(const_renderer->sort_origin_offset().x == doctest::Approx(1.0f));
    CHECK(const_renderer->sort_origin_offset().y == doctest::Approx(2.0f));
    CHECK(const_renderer->sort_origin_offset().z == doctest::Approx(3.0f));
}

// Verifies camera components are scene-owned and support first-camera fallback.
TEST_CASE("scene creates camera components and resolves main camera selection") {
    ofg::Scene scene;
    ofg::Entity* first = scene.create_entity(scene.get_root());
    ofg::Entity* second = scene.create_entity(scene.get_root());

    ofg::Component* first_component = first->create_component(ofg::ComponentType::Camera);
    ofg::Component* second_component = second->create_component(ofg::ComponentType::Camera);

    REQUIRE(first_component != nullptr);
    REQUIRE(second_component != nullptr);
    CHECK(first_component->type() == ofg::ComponentType::Camera);
    CHECK(first_component->entity() == first);
    CHECK(first->camera() == first_component);
    CHECK(second->camera() == second_component);
    CHECK(scene.camera_count() == 2);
    CHECK(scene.get_camera(0) == first->camera());
    CHECK(scene.get_camera(1) == second->camera());
    CHECK(scene.get_camera(2) == nullptr);
    CHECK(scene.main_camera() == first->camera());

    scene.set_main_camera(second->camera());
    CHECK(scene.main_camera() == second->camera());
    scene.set_main_camera(nullptr);
    CHECK(scene.main_camera() == first->camera());

    const ofg::Scene& const_scene = scene;
    CHECK(const_scene.get_camera(1) == second->camera());
    CHECK(const_scene.main_camera() == first->camera());
    CHECK_THROWS_WITH_AS(([&]() { (void)first->create_component(ofg::ComponentType::Camera); }()),
        doctest::Contains("Camera"),
        ofg::EngineError);

    ofg::Scene other_scene;
    ofg::Entity* other_entity = other_scene.create_entity(other_scene.get_root());
    (void)other_entity->create_component(ofg::ComponentType::Camera);
    CHECK_THROWS_WITH_AS(([&]() { scene.set_main_camera(other_entity->camera()); }()),
        doctest::Contains("same scene"),
        ofg::EngineError);
}

// Verifies camera selection follows moved scene storage and clear invalidation.
TEST_CASE("scene camera selection survives moves and rejects stale cameras") {
    ofg::Scene source;
    ofg::Entity* first = source.create_entity(source.get_root());
    ofg::Entity* second = source.create_entity(source.get_root());
    (void)first->create_component(ofg::ComponentType::Camera);
    (void)second->create_component(ofg::ComponentType::Camera);
    source.set_main_camera(second->camera());

    ofg::Scene moved(std::move(source));
    REQUIRE(moved.get_camera(1) != nullptr);
    CHECK(moved.main_camera() == moved.get_camera(1));
    CHECK(moved.get_camera(1)->entity() == moved.get_entity(2));
    CHECK(moved.get_entity(2)->camera() == moved.get_camera(1));
    const ofg::Entity* const_camera_entity = moved.get_entity(2);
    REQUIRE(const_camera_entity != nullptr);
    CHECK(const_camera_entity->camera() == moved.get_camera(1));

    ofg::Scene assigned;
    assigned = std::move(moved);
    REQUIRE(assigned.get_camera(1) != nullptr);
    CHECK(assigned.main_camera() == assigned.get_camera(1));
    ofg::Camera* stale_camera = assigned.get_camera(1);

    assigned.clear();
    CHECK(assigned.camera_count() == 0);
    CHECK(assigned.main_camera() == nullptr);
    CHECK_THROWS_WITH_AS(
        ([&]() { assigned.set_main_camera(stale_camera); }()), doctest::Contains("same scene"), ofg::EngineError);
}

// Verifies player components are scene-owned and exposed by index.
TEST_CASE("scene creates player components in stable order") {
    ofg::Scene scene;
    ofg::Entity* first = scene.create_entity(scene.get_root());
    ofg::Entity* second = scene.create_entity(scene.get_root());

    ofg::Component* first_component = first->create_component(ofg::ComponentType::Player);
    ofg::Component* second_component = second->create_component(ofg::ComponentType::Player);

    REQUIRE(first_component != nullptr);
    REQUIRE(second_component != nullptr);
    CHECK(first_component->type() == ofg::ComponentType::Player);
    CHECK(first_component->entity() == first);
    CHECK(first->player() == first_component);
    CHECK(second->player() == second_component);
    const ofg::Entity* const_first = first;
    const ofg::Entity* const_second = second;
    CHECK(const_first->player() == first_component);
    CHECK(const_second->player() == second_component);
    CHECK(scene.player_count() == 2);
    CHECK(scene.get_player(0) == first->player());
    CHECK(scene.get_player(1) == second->player());
    CHECK(scene.get_player(2) == nullptr);
    CHECK_THROWS_WITH_AS(([&]() { (void)first->create_component(ofg::ComponentType::Player); }()),
        doctest::Contains("Player"),
        ofg::EngineError);

    scene.clear();
    CHECK(scene.player_count() == 0);
    CHECK(scene.get_player(0) == nullptr);
}

// Verifies player updates consume controls only in player camera modes and remain grounded.
TEST_CASE("player update moves on the Y-up flat plane in player modes") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(camera != nullptr);
    CHECK(player->walk_speed() == doctest::Approx(3.5f));
    CHECK(player->height() == doctest::Approx(1.8f));

    ofg::ControlInput controls;
    controls.m_move_z = 1.0f;
    ofg::SceneUpdateContext context{controls, 1000.0, 1.0f, player, camera};
    player->update(context);
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(0.0f));
    CHECK(player_entity->local_transform().m_position.y == doctest::Approx(0.9f));

    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);
    player->update(context);
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(3.5f));
    CHECK(player_entity->local_transform().m_position.y == doctest::Approx(0.9f));

    controls.m_move_x = 1.0f;
    controls.m_move_y = 1.0f;
    player->set_walk_speed(1.0f);
    player->update(context);
    CHECK(player_entity->local_transform().m_position.x == doctest::Approx(0.7071067f).epsilon(0.0001));
    CHECK(player_entity->local_transform().m_position.y == doctest::Approx(0.9f));
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(4.2071067f).epsilon(0.0001));

    controls = ofg::ControlInput{};
    player->update(context);
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(4.2071067f).epsilon(0.0001));

    player_entity->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 0.0f);
    controls.m_move_z = 1.0f;
    controls.m_fast = true;
    player->set_walk_speed(1.0f);
    player->update(context);
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(2.0f));

    player_entity->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 0.0f);
    controls.m_fast = false;
    controls.m_slow = true;
    player->update(context);
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(0.35f));

    player->set_height(2.0f);
    CHECK(player->height() == doctest::Approx(2.0f));
    CHECK_THROWS_WITH_AS(player->set_walk_speed(-1.0f), doctest::Contains("walk speed"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(player->set_height(0.0f), doctest::Contains("height"), ofg::EngineError);
}

// Verifies player movement axes follow camera yaw after mouse look.
TEST_CASE("player movement follows camera yaw after mouse look") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(camera != nullptr);
    player->set_walk_speed(1.0f);
    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);

    ofg::ControlInput controls;
    controls.m_look_active = true;
    controls.m_look_delta_x = 1.57079632679f / 0.0025f;
    ofg::SceneUpdateContext context{controls, 1000.0, 1.0f, player, camera};
    scene.update(context);

    controls = ofg::ControlInput{};
    controls.m_move_z = 1.0f;
    scene.update(context);
    CHECK(player_entity->local_transform().m_position.x == doctest::Approx(1.0f).epsilon(0.0001));
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(0.0f).epsilon(0.0001));

    player_entity->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 0.0f);
    controls = ofg::ControlInput{};
    controls.m_move_x = 1.0f;
    scene.update(context);
    CHECK(player_entity->local_transform().m_position.x == doctest::Approx(0.0f).epsilon(0.0001));
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(-1.0f).epsilon(0.0001));
}

// Verifies player updates reject invalid direct calls and ignore non-primary players.
TEST_CASE("player update validates ownership delta and primary-player binding") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(camera != nullptr);
    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);

    ofg::ControlInput controls;
    controls.m_move_z = 1.0f;
    player_entity->local_transform().m_position.y = 7.0f;
    ofg::SceneUpdateContext non_primary_context{controls, 1000.0, 1.0f, nullptr, camera};
    player->update(non_primary_context);
    CHECK(player_entity->local_transform().m_position.y == doctest::Approx(7.0f));

    ofg::SceneUpdateContext bad_delta_context{controls, 1000.0, -1.0f, player, camera};
    CHECK_THROWS_WITH_AS(player->update(bad_delta_context), doctest::Contains("delta"), ofg::EngineError);

    ofg::Player detached_player(nullptr);
    ofg::SceneUpdateContext detached_context{controls, 1000.0, 1.0f, &detached_player, camera};
    CHECK_THROWS_WITH_AS(
        detached_player.update(detached_context), doctest::Contains("owning entity"), ofg::EngineError);
}

// Verifies player update owns default model-resource requests rather than TypeScript.
TEST_CASE("player update requests default model resources through Resources") {
    ofg::Resources::destroy();
    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    REQUIRE(ofg::Resources::prepare());

    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    ofg::Player* player = player_entity->player();
    REQUIRE(player != nullptr);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, player, nullptr, &scene, ofg::Resources::gpu_context()};
    player->update(context);

    CHECK(player->default_model_loading_state() == "queued");
    CHECK_FALSE(player->default_model_loaded());
    CHECK(ofg::Resources::model_resources().size() == 2);
    REQUIRE(ofg::Resources::pending_blob_loads().size() == 2);
    CHECK(ofg::Resources::pending_blob_loads()[0].m_uri == "assets/models/player/quaternius-superhero-male.glb");
    CHECK(ofg::Resources::pending_blob_loads()[1].m_uri == "assets/models/player/quaternius-ual1-standard.glb");

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies player model-resource failures stay on the player and keep fallback visible.
TEST_CASE("player update records default model resource load failures") {
    ofg::Resources::destroy();
    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    REQUIRE(ofg::Resources::prepare());

    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* visual_entity = scene.create_entity(player_entity);
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)visual_entity->create_component(ofg::ComponentType::MeshRenderer);
    ofg::Player* player = player_entity->player();
    ofg::MeshRenderer* fallback = visual_entity->mesh_renderer();
    REQUIRE(player != nullptr);
    REQUIRE(fallback != nullptr);
    player->bind_fallback_renderer(*fallback);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, player, nullptr, &scene, ofg::Resources::gpu_context()};
    player->update(context);
    REQUIRE(ofg::Resources::pending_blob_loads().size() == 2);
    const ofg::BlobLoadId model_id = ofg::Resources::pending_blob_loads()[0].m_id;

    ofg::Resources::mark_blob_loading(model_id);
    ofg::Resources::fail_blob_load(model_id, "missing from package");
    ofg::Resources::advance_loads();
    ofg::Resources::advance_loads();
    player->update(context);

    CHECK(player->default_model_loading_state() == "failed");
    CHECK_FALSE(player->default_model_loaded());
    CHECK(player->fallback_visible());
    CHECK(fallback->visible());
    CHECK(player->default_model_load_error().find("quaternius-superhero-male.glb") != std::string::npos);
    CHECK(player->default_model_load_error().find("missing from package") != std::string::npos);

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies animation-library resource failures also surface through Player debug state.
TEST_CASE("player update records default animation resource load failures") {
    ofg::Resources::destroy();
    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    REQUIRE(ofg::Resources::prepare());

    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* visual_entity = scene.create_entity(player_entity);
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)visual_entity->create_component(ofg::ComponentType::MeshRenderer);
    ofg::Player* player = player_entity->player();
    ofg::MeshRenderer* fallback = visual_entity->mesh_renderer();
    REQUIRE(player != nullptr);
    REQUIRE(fallback != nullptr);
    player->bind_fallback_renderer(*fallback);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, player, nullptr, &scene, ofg::Resources::gpu_context()};
    player->update(context);
    REQUIRE(ofg::Resources::pending_blob_loads().size() == 2);
    const ofg::BlobLoadId animation_id = ofg::Resources::pending_blob_loads()[1].m_id;

    ofg::Resources::mark_blob_loading(animation_id);
    ofg::Resources::fail_blob_load(animation_id, "animation library missing");
    ofg::Resources::advance_loads();
    ofg::Resources::advance_loads();
    player->update(context);

    CHECK(player->default_model_loading_state() == "failed");
    CHECK_FALSE(player->default_model_loaded());
    CHECK(player->fallback_visible());
    CHECK(fallback->visible());
    CHECK(player->default_model_load_error().find("quaternius-ual1-standard.glb") != std::string::npos);
    CHECK(player->default_model_load_error().find("animation library missing") != std::string::npos);

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies Player converts resource-system exceptions into observable load failure state.
TEST_CASE("player update reports resource system setup failures") {
    ofg::Resources::destroy();

    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* visual_entity = scene.create_entity(player_entity);
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)visual_entity->create_component(ofg::ComponentType::MeshRenderer);
    ofg::Player* player = player_entity->player();
    ofg::MeshRenderer* fallback = visual_entity->mesh_renderer();
    REQUIRE(player != nullptr);
    REQUIRE(fallback != nullptr);
    player->bind_fallback_renderer(*fallback);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, player, nullptr, &scene};
    player->update(context);

    CHECK(player->default_model_loading_state() == "failed");
    CHECK_FALSE(player->default_model_loaded());
    CHECK(player->fallback_visible());
    CHECK(fallback->visible());
    CHECK(player->default_model_load_error().find("Resources::load_model_resource") != std::string::npos);
}

// Verifies the Player can load, instantiate, and animate its default model resources.
TEST_CASE("player update binds loaded default model resources") {
    ofg::tests::TestGpuContext gpu = create_real_test_resources();

    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* visual_entity = scene.create_entity(player_entity);
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)visual_entity->create_component(ofg::ComponentType::MeshRenderer);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::MeshRenderer* fallback = visual_entity->mesh_renderer();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(fallback != nullptr);
    REQUIRE(camera != nullptr);
    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);
    player->bind_fallback_renderer(*fallback);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, player, camera, &scene, ofg::Resources::gpu_context()};
    player->update(context);
    CHECK(player->default_model_loading_state() == "queued");
    CHECK(player->fallback_visible());
    CHECK(fallback->visible());

    for (int step = 0;
        step < 20 && !player->default_model_loaded() && player->default_model_loading_state() != "failed";
        ++step) {
        complete_player_asset_blob_requests();
        ofg::Resources::advance_loads();
        player->update(context);
    }

    INFO("player model state: " << player->default_model_loading_state());
    INFO("player model error: " << player->default_model_load_error());
    CHECK(player->default_model_loaded());
    CHECK(player->default_model_loading_state() == "loaded");
    CHECK(player->default_model_load_error().empty());
    CHECK_FALSE(player->fallback_visible());
    CHECK_FALSE(fallback->visible());
    CHECK(ofg::Resources::model_resources().size() == 2);
    CHECK(scene.animation_player_count() >= 1);
    CHECK(scene.mesh_renderer_count() > 1);
    CHECK(player->idle_animation_weight() == doctest::Approx(1.0f));
    CHECK(player->walk_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->sprint_animation_weight() == doctest::Approx(0.0f));

    controls.m_move_z = 1.0f;
    player->update(context);
    CHECK(player->idle_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->walk_animation_weight() == doctest::Approx(1.0f));
    CHECK(player->sprint_animation_weight() == doctest::Approx(0.0f));

    controls.m_fast = true;
    player->update(context);
    CHECK(player->idle_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->walk_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->sprint_animation_weight() == doctest::Approx(1.0f));

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies scene update validates controls before any component can mutate transforms.
TEST_CASE("scene update rejects invalid controls before player movement") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(camera != nullptr);
    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);

    ofg::ControlInput controls;
    controls.m_move_x = std::numeric_limits<float>::infinity();
    ofg::SceneUpdateContext context{controls, 1000.0, 1.0f, player, camera};

    CHECK_THROWS_WITH_AS(scene.update(context), doctest::Contains("finite"), ofg::EngineError);
    CHECK(player_entity->local_transform().m_position.x == doctest::Approx(0.0f));
    CHECK(player_entity->local_transform().m_position.y == doctest::Approx(0.0f));
}

// Verifies camera defaults, accessors, and validation branches.
TEST_CASE("camera exposes default perspective settings and rejects invalid inputs") {
    ofg::Scene scene;
    (void)scene.get_root()->create_component(ofg::ComponentType::Camera);
    ofg::Camera* camera = scene.get_root()->camera();
    REQUIRE(camera != nullptr);

    CHECK(camera->vertical_fov_radians() == doctest::Approx(0.9599311f));
    CHECK(camera->near_z() == doctest::Approx(0.1f));
    CHECK(camera->far_z() == doctest::Approx(80.0f));

    const ofg::CameraProperties properties = camera->camera_properties(1.0f);
    CHECK(properties.camera == camera);
    CHECK(properties.world_from_camera[3].x == doctest::Approx(0.0f));
    CHECK(properties.world_from_camera[3].y == doctest::Approx(0.0f));
    CHECK(properties.world_from_camera[3].z == doctest::Approx(0.0f));

    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_WITH_AS(
        ([&]() { camera->set_perspective(nan_value, 0.1f, 10.0f); }()), doctest::Contains("finite"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { camera->set_perspective(1.0f, 0.0f, 10.0f); }()), doctest::Contains("near_z"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)camera->camera_properties(nan_value); }()), doctest::Contains("aspect"), ofg::EngineError);

    ofg::Camera detached(nullptr);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)detached.camera_properties(1.0f); }()), doctest::Contains("owning entity"), ofg::EngineError);
}

// Verifies camera projection and scale-ignored transform resolution.
TEST_CASE("camera properties resolve entity transforms without scale") {
    ofg::Scene scene;
    ofg::Entity* parent = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(parent);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(camera != nullptr);

    parent->local_transform().m_position = ofg::math::vec3(10.0f, 0.0f, 0.0f);
    parent->local_transform().m_rotation = require_y_rotation(1.57079632679f);
    parent->local_transform().m_scale = ofg::math::vec3(100.0f, 100.0f, 100.0f);
    camera_entity->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 2.0f);
    camera_entity->local_transform().m_scale = ofg::math::vec3(0.01f, 0.01f, 0.01f);

    camera->set_perspective(1.0f, 0.25f, 42.0f);
    const ofg::CameraProperties properties = camera->camera_properties(2.0f);
    CHECK(properties.camera == camera);
    CHECK(properties.vertical_fov_radians == doctest::Approx(1.0f));
    CHECK(properties.aspect == doctest::Approx(2.0f));
    CHECK(properties.near_z == doctest::Approx(0.25f));
    CHECK(properties.far_z == doctest::Approx(42.0f));

    const ofg::math::Vec4 world_origin =
        ofg::math::mul(properties.world_from_camera, ofg::math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(world_origin.x == doctest::Approx(12.0f));
    CHECK(world_origin.y == doctest::Approx(0.0f));
    CHECK(world_origin.z == doctest::Approx(0.0f).epsilon(0.0001));

    const ofg::math::Vec4 world_forward =
        ofg::math::mul(properties.world_from_camera, ofg::math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    CHECK(world_forward.x == doctest::Approx(1.0f));
    CHECK(world_forward.y == doctest::Approx(0.0f));
    CHECK(world_forward.z == doctest::Approx(0.0f).epsilon(0.0001));

    CHECK_THROWS_WITH_AS(([&]() { camera->set_perspective(0.0f, 0.1f, 10.0f); }()),
        doctest::Contains("field of view"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { camera->set_perspective(1.0f, 10.0f, 1.0f); }()), doctest::Contains("near_z"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)camera->camera_properties(0.0f); }()), doctest::Contains("aspect"), ofg::EngineError);
}

// Verifies const traversal and scene moves keep entity owner pointers usable.
TEST_CASE("scene supports const traversal after move") {
    ofg::Scene source;
    ofg::Entity* child = source.create_entity(source.get_root());
    ofg::Entity* grandchild = source.create_entity(child);
    (void)grandchild->create_component(ofg::ComponentType::MeshRenderer);

    ofg::Scene moved(std::move(source));
    const ofg::Scene& const_moved = moved;
    const ofg::Entity* const_root = const_moved.get_root();
    REQUIRE(const_root != nullptr);
    const ofg::Entity* const_child = const_root->first_child();
    REQUIRE(const_child != nullptr);
    CHECK(const_child->next_sibling() == nullptr);
    const ofg::Entity* const_grandchild = const_child->first_child();
    REQUIRE(const_grandchild != nullptr);
    CHECK(const_grandchild->mesh_renderer() == moved.get_mesh_renderer(0));

    ofg::Scene assigned;
    assigned = std::move(moved);
    ofg::Scene* self = &assigned;
    assigned = std::move(*self);
    REQUIRE(assigned.get_root() != nullptr);
    ofg::Entity* assigned_child = assigned.create_entity(assigned.get_root());
    REQUIRE(assigned_child != nullptr);
    (void)assigned_child->create_component(ofg::ComponentType::MeshRenderer);
    CHECK(assigned_child->mesh_renderer()->entity() == assigned_child);
}

// Verifies clear resets ids, root state, component storage, and generation.
TEST_CASE("scene clear resets root and invalidates component containers") {
    ofg::Scene scene;
    const std::uint32_t first_generation = scene.generation();
    ofg::Entity* entity = scene.create_entity(scene.get_root());
    (void)entity->create_component(ofg::ComponentType::MeshRenderer);

    scene.clear();

    CHECK(scene.generation() == first_generation + 1);
    REQUIRE(scene.get_root() != nullptr);
    CHECK(scene.get_root()->id() == 0);
    CHECK(scene.entity_count() == 1);
    CHECK(scene.mesh_renderer_count() == 0);
    ofg::Entity* next = scene.create_entity(scene.get_root());
    CHECK(next->id() == 1);
}

// Verifies scene transform helpers follow world_from_local naming.
TEST_CASE("scene transforms compose from local to world") {
    ofg::Scene scene;
    ofg::Entity* root = scene.get_root();
    ofg::Entity* child = scene.create_entity(root);
    ofg::Entity* grandchild = scene.create_entity(child);

    root->local_transform().m_position = ofg::math::vec3(10.0f, 0.0f, 0.0f);
    child->local_transform().m_position = ofg::math::vec3(0.0f, 0.0f, 2.0f);
    child->local_transform().m_rotation = require_y_rotation(1.57079632679f);
    grandchild->local_transform().m_position = ofg::math::vec3(1.0f, 0.0f, 0.0f);
    grandchild->local_transform().m_scale = ofg::math::vec3(2.0f, 2.0f, 2.0f);

    const ofg::math::Mat4 world_from_grandchild = ofg::world_from_local(*grandchild);
    const ofg::math::Vec4 origin = ofg::math::mul(world_from_grandchild, ofg::math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(origin.x == doctest::Approx(10.0f));
    CHECK(origin.y == doctest::Approx(0.0f));
    CHECK(origin.z == doctest::Approx(1.0f));

    const ofg::math::Vec4 local_x = ofg::math::mul(world_from_grandchild, ofg::math::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(local_x.x == doctest::Approx(10.0f));
    CHECK(local_x.z == doctest::Approx(-1.0f));
}
