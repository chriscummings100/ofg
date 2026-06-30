// Doctest coverage for the OFG scene entity/component graph.
//
// These tests pin the first ECS API: root creation, child/sibling traversal,
// mesh-renderer component storage, generation invalidation, and transform
// composition used by renderer draw-list extraction.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/quat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/scene/scene.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace {

// Returns a Y-axis quaternion or fails the current doctest.
ofg::math::Quat require_y_rotation(float radians) {
    std::string error;
    std::optional<ofg::math::Quat> rotation =
        ofg::math::quat_from_axis_angle(ofg::math::vec3(0.0f, 1.0f, 0.0f), radians, error);
    REQUIRE_MESSAGE(rotation.has_value(), error);
    return *rotation;
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
    scene.set_main_view(ofg::render_view_from_matrix(ofg::math::mat4_translation(ofg::math::vec3(1.0f, 2.0f, 3.0f))));

    scene.clear();

    CHECK(scene.generation() == first_generation + 1);
    REQUIRE(scene.get_root() != nullptr);
    CHECK(scene.get_root()->id() == 0);
    CHECK(scene.entity_count() == 1);
    CHECK(scene.mesh_renderer_count() == 0);
    CHECK(scene.main_view().m_view_projection[3].x == doctest::Approx(0.0f));
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
