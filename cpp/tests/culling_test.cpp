// Doctest coverage for renderer bounds, frustum plane sets, and render-object culling.
//
// These tests exercise the CPU culling framework that opaque and future shadow
// passes share before any pass records WebGPU draw commands.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/render/camera_properties.hpp"
#include "ofg/render/frustum.hpp"
#include "ofg/render/render_object.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr float _pi = 3.14159265358979323846f;

// Builds one valid CPU-only shader for culling resource fixtures.
std::unique_ptr<ofg::Shader> make_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    auto shader = std::make_unique<ofg::Shader>(ofg::GpuContext{}, "culling shader");
    shader->init_from_wgsl("source", layout, {});
    return shader;
}

// Builds one valid CPU-only material for mesh submeshes.
std::unique_ptr<ofg::Material> make_material(ofg::Shader& shader) {
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    auto material = std::make_unique<ofg::Material>(ofg::GpuContext{}, "culling material");
    material->init(shader, properties);
    return material;
}

// Builds a mesh vertex at the requested position.
ofg::MeshVertex vertex(float x, float y, float z) {
    return ofg::MeshVertex{{x, y, z}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}};
}

// Builds one CPU-only box-ish mesh from two extreme corners plus a third vertex.
std::unique_ptr<ofg::Mesh> make_mesh(ofg::Material& material) {
    std::vector<ofg::MeshVertex> vertices{
        vertex(-1.0f, -2.0f, -0.5f),
        vertex(2.0f, 3.0f, 1.0f),
        vertex(0.0f, 0.0f, 0.0f),
    };
    std::vector<std::uint32_t> indices{0, 1, 2};
    std::vector<ofg::SubMesh> submeshes{ofg::SubMesh{"bounds", 0, 3, &material}};
    auto mesh = std::make_unique<ofg::Mesh>(ofg::GpuContext{}, "culling mesh");
    mesh->init(std::move(vertices), std::move(indices), std::move(submeshes));
    return mesh;
}

// Adds one mesh renderer entity to the supplied scene.
ofg::MeshRenderer& add_renderer(ofg::Scene& scene, ofg::Mesh& mesh, ofg::math::Vec3 position) {
    ofg::Entity* entity = scene.create_entity(scene.get_root());
    REQUIRE(entity != nullptr);
    entity->local_transform().m_position = position;
    ofg::Component* component = entity->create_component(ofg::ComponentType::MeshRenderer);
    REQUIRE(component != nullptr);
    REQUIRE(entity->mesh_renderer() != nullptr);
    entity->mesh_renderer()->set_mesh(&mesh);
    return *entity->mesh_renderer();
}

// Builds a unit-size AABB centered at the requested point.
ofg::Bounds3 unit_bounds_at(ofg::math::Vec3 center) {
    return ofg::Bounds3{ofg::math::sub(center, ofg::math::vec3(0.5f, 0.5f, 0.5f)),
        ofg::math::add(center, ofg::math::vec3(0.5f, 0.5f, 0.5f))};
}

} // namespace

// Verifies mesh local bounds follow CPU vertex mutations.
TEST_CASE("mesh local bounds are derived from CPU vertices") {
    std::unique_ptr<ofg::Shader> shader = make_shader();
    std::unique_ptr<ofg::Material> material = make_material(*shader);
    std::unique_ptr<ofg::Mesh> mesh = make_mesh(*material);

    CHECK(mesh->local_bounds().m_min.x == doctest::Approx(-1.0f));
    CHECK(mesh->local_bounds().m_min.y == doctest::Approx(-2.0f));
    CHECK(mesh->local_bounds().m_min.z == doctest::Approx(-0.5f));
    CHECK(mesh->local_bounds().m_max.x == doctest::Approx(2.0f));
    CHECK(mesh->local_bounds().m_max.y == doctest::Approx(3.0f));
    CHECK(mesh->local_bounds().m_max.z == doctest::Approx(1.0f));

    std::vector<ofg::MeshVertex> replacement{
        vertex(-4.0f, -1.0f, 2.0f),
        vertex(1.0f, 5.0f, 7.0f),
        vertex(0.0f, 0.0f, 3.0f),
    };
    mesh->replace_vertices(replacement);
    CHECK(mesh->local_bounds().m_min.x == doctest::Approx(-4.0f));
    CHECK(mesh->local_bounds().m_max.y == doctest::Approx(5.0f));
    CHECK(mesh->local_bounds().m_max.z == doctest::Approx(7.0f));
}

// Verifies transformed AABBs cover non-uniform world scale and translation.
TEST_CASE("bounds transform conservatively handles non-uniform scale") {
    const ofg::Bounds3 local{ofg::math::vec3(-1.0f, -2.0f, -3.0f), ofg::math::vec3(2.0f, 3.0f, 4.0f)};
    const ofg::math::Mat4 world_from_local =
        ofg::math::mul(ofg::math::mat4_translation(ofg::math::vec3(10.0f, 20.0f, 30.0f)),
            ofg::math::mat4_scale(ofg::math::vec3(2.0f, 3.0f, 4.0f)));

    const ofg::Bounds3 world = ofg::transform_bounds(local, world_from_local);
    CHECK(world.m_min.x == doctest::Approx(8.0f));
    CHECK(world.m_min.y == doctest::Approx(14.0f));
    CHECK(world.m_min.z == doctest::Approx(18.0f));
    CHECK(world.m_max.x == doctest::Approx(14.0f));
    CHECK(world.m_max.y == doctest::Approx(29.0f));
    CHECK(world.m_max.z == doctest::Approx(46.0f));

    const ofg::BoundingSphere sphere = ofg::bounding_sphere_from_bounds(world);
    CHECK(sphere.m_center.x == doctest::Approx(11.0f));
    CHECK(sphere.m_center.y == doctest::Approx(21.5f));
    CHECK(sphere.m_center.z == doctest::Approx(32.0f));
    CHECK(sphere.m_radius > 0.0f);
}

// Verifies bounds helpers reject invalid or non-finite data before culling can use it.
TEST_CASE("bounds helpers reject invalid geometry contracts") {
    const float infinity = std::numeric_limits<float>::infinity();
    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const ofg::Bounds3 inverted_bounds{ofg::math::vec3(2.0f, 0.0f, 0.0f), ofg::math::vec3(1.0f, 0.0f, 0.0f)};
    const ofg::Bounds3 non_finite_bounds{ofg::math::vec3(quiet_nan, 0.0f, 0.0f), ofg::math::vec3(1.0f, 0.0f, 0.0f)};

    CHECK_FALSE(ofg::bounds_is_valid(inverted_bounds));
    CHECK_FALSE(ofg::bounds_is_valid(non_finite_bounds));
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::transform_bounds(inverted_bounds, ofg::math::mat4_identity()); }()),
        doctest::Contains("finite and ordered"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::bounding_sphere_from_bounds(inverted_bounds); }()),
        doctest::Contains("finite and ordered"),
        ofg::EngineError);

    std::vector<ofg::MeshVertex> empty_vertices;
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::mesh_vertex_bounds(empty_vertices); }()),
        doctest::Contains("at least one"),
        ofg::EngineError);

    const std::vector<ofg::MeshVertex> first_non_finite_vertex{vertex(quiet_nan, 0.0f, 0.0f)};
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::mesh_vertex_bounds(first_non_finite_vertex); }()),
        doctest::Contains("finite"),
        ofg::EngineError);

    const std::vector<ofg::MeshVertex> later_non_finite_vertex{vertex(0.0f, 0.0f, 0.0f), vertex(1.0f, infinity, 0.0f)};
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::mesh_vertex_bounds(later_non_finite_vertex); }()),
        doctest::Contains("finite"),
        ofg::EngineError);

    ofg::math::Mat4 first_corner_overflow = ofg::math::mat4_identity();
    first_corner_overflow[0].x = std::numeric_limits<float>::max();
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::transform_bounds(
            ofg::Bounds3{ofg::math::vec3(2.0f, 0.0f, 0.0f), ofg::math::vec3(3.0f, 0.0f, 0.0f)}, first_corner_overflow);
    }()),
        doctest::Contains("Transformed bounds"),
        ofg::EngineError);

    ofg::math::Mat4 later_corner_overflow = ofg::math::mat4_identity();
    later_corner_overflow[0].x = std::numeric_limits<float>::max();
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::transform_bounds(
            ofg::Bounds3{ofg::math::vec3(0.0f, 0.0f, 0.0f), ofg::math::vec3(2.0f, 0.0f, 0.0f)}, later_corner_overflow);
    }()),
        doctest::Contains("Transformed bounds"),
        ofg::EngineError);
}

// Verifies plane orientation, touching behavior, and empty plane sets.
TEST_CASE("culling plane sets conservatively accept intersecting bounds") {
    const ofg::CullingPlane plane = ofg::make_culling_plane(ofg::math::vec3(1.0f, 0.0f, 0.0f), 1.0f);
    const std::array<ofg::CullingPlane, 1> planes{plane};
    const ofg::CullingPlaneSet plane_set{planes};

    CHECK(ofg::intersects_culling_planes(unit_bounds_at(ofg::math::vec3(0.0f, 0.0f, 0.0f)), plane_set));
    CHECK(ofg::intersects_culling_planes(
        ofg::Bounds3{ofg::math::vec3(-2.0f, -0.5f, -0.5f), ofg::math::vec3(-1.0f, 0.5f, 0.5f)}, plane_set));
    CHECK_FALSE(ofg::intersects_culling_planes(unit_bounds_at(ofg::math::vec3(-3.0f, 0.0f, 0.0f)), plane_set));
    CHECK(ofg::intersects_culling_planes(unit_bounds_at(ofg::math::vec3(-3.0f, 0.0f, 0.0f)), ofg::CullingPlaneSet{}));

    CHECK(ofg::intersects_culling_planes(ofg::BoundingSphere{ofg::math::vec3(-1.5f, 0.0f, 0.0f), 0.5f}, plane_set));
    CHECK_FALSE(
        ofg::intersects_culling_planes(ofg::BoundingSphere{ofg::math::vec3(-2.0f, 0.0f, 0.0f), 0.25f}, plane_set));
}

// Verifies culling plane helpers reject invalid caller-supplied volumes.
TEST_CASE("culling plane sets reject invalid contracts") {
    const ofg::CullingPlane plane = ofg::make_culling_plane(ofg::math::vec3(1.0f, 0.0f, 0.0f), 1.0f);
    const std::array<ofg::CullingPlane, 1> planes{plane};
    const ofg::CullingPlaneSet plane_set{planes};
    const float infinity = std::numeric_limits<float>::infinity();

    CHECK_THROWS_WITH_AS(([]() { (void)ofg::make_culling_plane(ofg::math::vec3(0.0f, 0.0f, 0.0f), 1.0f); }()),
        doctest::Contains("nonzero"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::make_culling_plane(ofg::math::vec3(1.0f, 0.0f, 0.0f), infinity); }()),
        doctest::Contains("finite"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::intersects_culling_planes(
            ofg::Bounds3{ofg::math::vec3(2.0f, 0.0f, 0.0f), ofg::math::vec3(1.0f, 0.0f, 0.0f)}, plane_set);
    }()),
        doctest::Contains("valid world bounds"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::intersects_culling_planes(ofg::BoundingSphere{ofg::math::vec3(0.0f, 0.0f, 0.0f), -1.0f}, plane_set);
    }()),
        doctest::Contains("sphere radius"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::intersects_culling_planes(
            ofg::BoundingSphere{ofg::math::vec3(0.0f, 0.0f, 0.0f), infinity}, plane_set);
    }()),
        doctest::Contains("sphere radius"),
        ofg::EngineError);
}

// Verifies camera frustum extraction produces inward-facing culling planes.
TEST_CASE("camera frustum culling rejects objects outside view volume") {
    const ofg::CameraProperties camera = ofg::camera_properties_from_look_at(nullptr,
        ofg::math::vec3(0.0f, 0.0f, 0.0f),
        ofg::math::vec3(0.0f, 0.0f, 1.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        _pi * 0.5f,
        1.0f,
        1.0f,
        10.0f);
    const ofg::ViewFrustum frustum = ofg::view_frustum_from_camera(camera);

    CHECK(frustum.planes().size() == 6);
    CHECK(ofg::intersects_culling_planes(unit_bounds_at(ofg::math::vec3(0.0f, 0.0f, 5.0f)), frustum.plane_set()));
    CHECK_FALSE(ofg::intersects_culling_planes(unit_bounds_at(ofg::math::vec3(0.0f, 0.0f, 0.0f)), frustum.plane_set()));
    CHECK_FALSE(
        ofg::intersects_culling_planes(unit_bounds_at(ofg::math::vec3(0.0f, 0.0f, 12.0f)), frustum.plane_set()));
    CHECK_FALSE(ofg::intersects_culling_planes(unit_bounds_at(ofg::math::vec3(8.0f, 0.0f, 4.0f)), frustum.plane_set()));
}

// Verifies render-object extraction skips invisible renderers and preserves world bounds.
TEST_CASE("render-object extraction builds bounded visible objects") {
    std::unique_ptr<ofg::Shader> shader = make_shader();
    std::unique_ptr<ofg::Material> material = make_material(*shader);
    std::unique_ptr<ofg::Mesh> mesh = make_mesh(*material);
    ofg::Scene scene;
    ofg::MeshRenderer& visible = add_renderer(scene, *mesh, ofg::math::vec3(5.0f, 0.0f, 3.0f));
    visible.entity()->local_transform().m_scale = ofg::math::vec3(2.0f, 1.0f, 1.0f);
    ofg::MeshRenderer& hidden = add_renderer(scene, *mesh, ofg::math::vec3(0.0f, 0.0f, 0.0f));
    hidden.set_visible(false);
    hidden.set_mesh(nullptr);

    std::vector<ofg::RenderObject> objects;
    ofg::RenderObjectExtractionStats stats;
    REQUIRE_NOTHROW(ofg::extract_render_objects(scene, objects, stats));
    REQUIRE(objects.size() == 1);
    CHECK(stats.m_scene_mesh_renderer_count == 2);
    CHECK(stats.m_extracted_object_count == 1);
    CHECK(stats.m_invisible_renderer_count == 1);
    CHECK(objects[0].m_scene_mesh_renderer_index == 0);
    CHECK(objects[0].m_world_bounds.m_min.x == doctest::Approx(3.0f));
    CHECK(objects[0].m_world_bounds.m_max.x == doctest::Approx(9.0f));
    CHECK(objects[0].m_world_bounds.m_min.z == doctest::Approx(2.5f));
    CHECK(objects[0].m_world_bounds.m_max.z == doctest::Approx(4.0f));
}

// Verifies plane-set culling appends accepted objects into draw lists.
TEST_CASE("append_culled_draws filters render objects into draw lists") {
    std::unique_ptr<ofg::Shader> shader = make_shader();
    std::unique_ptr<ofg::Material> material = make_material(*shader);
    std::unique_ptr<ofg::Mesh> mesh = make_mesh(*material);
    ofg::Scene scene;
    add_renderer(scene, *mesh, ofg::math::vec3(0.0f, 0.0f, 5.0f));
    add_renderer(scene, *mesh, ofg::math::vec3(100.0f, 0.0f, 5.0f));

    std::vector<ofg::RenderObject> objects;
    ofg::RenderObjectExtractionStats extraction_stats;
    ofg::extract_render_objects(scene, objects, extraction_stats);

    ofg::DrawList all_draws;
    ofg::CullingStats all_stats;
    ofg::append_culled_draws(objects, ofg::CullingPlaneSet{}, all_draws, all_stats);
    CHECK(all_draws.size() == 2);
    CHECK(all_stats.m_tested_object_count == 2);
    CHECK(all_stats.m_accepted_object_count == 2);
    CHECK(all_stats.m_rejected_object_count == 0);
    CHECK_NOTHROW(all_draws.validate());

    const ofg::CameraProperties camera = ofg::camera_properties_from_look_at(nullptr,
        ofg::math::vec3(0.0f, 0.0f, 0.0f),
        ofg::math::vec3(0.0f, 0.0f, 1.0f),
        ofg::math::vec3(0.0f, 1.0f, 0.0f),
        _pi * 0.5f,
        1.0f,
        1.0f,
        20.0f);
    const ofg::ViewFrustum frustum = ofg::view_frustum_from_camera(camera);
    ofg::DrawList camera_draws;
    ofg::CullingStats camera_stats;
    ofg::append_culled_draws(objects, frustum.plane_set(), camera_draws, camera_stats);
    CHECK(camera_draws.size() == 1);
    CHECK(camera_stats.m_tested_object_count == 2);
    CHECK(camera_stats.m_accepted_object_count == 1);
    CHECK(camera_stats.m_rejected_object_count == 1);
    CHECK_NOTHROW(camera_draws.validate());
}
