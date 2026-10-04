// GPU-independent scene rendering contracts: transforms, typed ownership, resources and homogeneous culling.
#include "scene/scene.h"
#include "render/draw-list.h"
#include "core/engine-error.h"
#include "math/transform.h"
#include <ostream>
#include <doctest.h>
#include <limits>

using namespace ofg;

// Creates a source-only material without invoking any graphics dependency.
static std::shared_ptr<Material> testMaterial()
{
    auto material = Material::create(Shader::create("test", "source"));
    material->setUniform("tint", math::Vec4{1, 1, 1, 1});
    return material;
}

// Creates two triangle submeshes sharing one default material.
static std::shared_ptr<Mesh> testMesh()
{
    auto material = testMaterial();
    return Mesh::create(
        {{{-0.5f, -0.5f, 0}, {}, {}}, {{0.5f, -0.5f, 0}, {}, {}}, {{0, 0.5f, 0}, {}, {}}},
        {0, 1, 2, 0, 1, 2},
        {{0, 3, material}, {3, 3, material}}
    );
}

TEST_CASE("world cache follows root and ancestor edits before and after scene update")
{
    Scene scene;
    auto parent = scene.createEntity(scene.getRoot());
    auto child = scene.createEntity(parent);
    child->setLocalPosition({1, 2, 3});
    CHECK(child->worldTransform()[3].x == 1);
    parent->setLocalScale({2, 3, 4});
    scene.getRoot()->setLocalPosition({10, 20, 30});
    CHECK(child->worldTransform()[3].x == 12);
    CHECK(child->worldTransform()[3].y == 26);
    CHECK(child->worldTransform()[3].z == 42);
    parent->setLocalPosition({2, 3, 4});
    scene.update();
    CHECK(child->worldTransform()[3].x == 14);
    CHECK(child->worldTransform()[3].x == worldFromLocal(*child)[3].x);
    CHECK_THROWS_AS(child->setLocalRotation({0, 0, 0, 0}), EngineError);
    CHECK_THROWS_AS(child->setLocalPosition({INFINITY, 0, 0}), EngineError);
}

TEST_CASE("typed components preserve stable ownership across move and invalidate on clear")
{
    Scene scene, other;
    auto entity = scene.createEntity(scene.getRoot());
    auto camera = scene.createCamera(entity);
    auto renderer = scene.createMeshRenderer(entity);
    scene.setActiveCamera(camera);
    Ptr<Camera> cameraObserver{camera};
    Ptr<MeshRenderer> rendererObserver{renderer};
    CHECK(entity->camera() == camera);
    CHECK(entity->meshRenderer() == renderer);
    CHECK_THROWS_AS(scene.createCamera(entity), EngineError);
    CHECK_THROWS_AS(scene.createMeshRenderer(entity), EngineError);
    CHECK_THROWS_AS(other.createCamera(entity), EngineError);
    CHECK_THROWS_AS(other.setActiveCamera(camera), EngineError);
    CHECK_THROWS_AS(scene.createMeshRenderer(nullptr), EngineError);
    Scene moved(std::move(scene));
    CHECK(moved.activeCamera() == camera);
    CHECK(scene.activeCamera() == nullptr);
    auto displaced = other.createCamera(other.getRoot());
    Ptr<Camera> displacedObserver{displaced};
    other = std::move(moved);
    CHECK(displacedObserver == nullptr);
    CHECK(other.activeCamera() == camera);
    CHECK(renderer->entity() == entity);
    other.setActiveCamera(nullptr);
    CHECK(other.activeCamera() == nullptr);
    other.clear();
    CHECK(cameraObserver == nullptr);
    CHECK(rendererObserver == nullptr);
}

TEST_CASE("procedural assets validate geometry and clones isolate values while sharing dependencies")
{
    auto mesh = testMesh();
    CHECK(mesh->isLoaded());
    CHECK(mesh->key().empty());
    CHECK(mesh->bounds().minimum.x == -0.5f);
    auto material = mesh->subMeshes()[0].material;
    auto clone = material->clone();
    CHECK(clone != material);
    CHECK(clone->shader() == material->shader());
    clone->setUniform("tint", math::Vec4{1, 0, 0, 1});
    CHECK(std::get<math::Vec4>(material->uniforms().at("tint")).y == 1);
    CHECK_THROWS_AS(clone->setUniform("a.b", 1.0f), EngineError);
    CHECK_THROWS_AS(Material::create(nullptr), EngineError);
    CHECK_THROWS_AS(Shader::create("", "source"), EngineError);
    std::vector<Vertex> vertices(mesh->vertices().begin(), mesh->vertices().end());
    CHECK_THROWS_AS(Mesh::create({}, {}, {}), EngineError);
    CHECK_THROWS_AS(Mesh::create(vertices, {0, 1, 5}, {{0, 3, material}}), EngineError);
    CHECK_THROWS_AS(Mesh::create(vertices, {0, 1, 2}, {{UINT32_MAX, 3, material}}), EngineError);
    CHECK_THROWS_AS(Mesh::create(vertices, {0, 1, 2}, {{0, 0, material}}), EngineError);
    CHECK_THROWS_AS(Mesh::create(vertices, {0, 1, 2}, {{1, 2, material}}), EngineError);
    CHECK_THROWS_AS(Mesh::create(vertices, {0, 1, 2}, {{0, 3, nullptr}}), EngineError);
    vertices[0].uv.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(Mesh::create(vertices, {0, 1, 2}, {{0, 3, material}}), EngineError);
}

TEST_CASE("renderer overrides resolve per slot and reset only when mesh changes")
{
    Scene scene;
    auto renderer = scene.createMeshRenderer(scene.getRoot());
    CHECK_THROWS_AS(renderer->material(0), EngineError);
    auto mesh = testMesh();
    renderer->setMesh(mesh);
    CHECK(renderer->materialOverride(0) == nullptr);
    auto clone = renderer->makeMaterialUnique(0);
    CHECK(renderer->material(0) == clone);
    CHECK(renderer->material(1) == mesh->subMeshes()[1].material);
    renderer->setMesh(mesh);
    CHECK(renderer->material(0) == clone);
    CHECK(renderer->makeMaterialUnique(0) != clone);
    renderer->setMaterialOverride(0, nullptr);
    CHECK(renderer->material(0) == mesh->subMeshes()[0].material);
    CHECK_THROWS_AS(renderer->setMaterialOverride(2, clone), EngineError);
    renderer->setMaterialOverride(0, clone);
    renderer->setMesh(testMesh());
    CHECK(renderer->materialOverride(0) == nullptr);
    renderer->setMesh(nullptr);
    CHECK(renderer->mesh() == nullptr);
}

TEST_CASE("homogeneous culling retains intersections and rejects each outside clip plane")
{
    const auto identity = math::mat4Identity();
    CHECK(boundsVisible({{-1, -1, 0}, {1, 1, 1}}, identity));
    for (const Bounds bounds :
         {Bounds{{-3, 0, 0.2f}, {-2, 1, 0.8f}},
          Bounds{{2, 0, 0.2f}, {3, 1, 0.8f}},
          Bounds{{0, -3, 0.2f}, {1, -2, 0.8f}},
          Bounds{{0, 2, 0.2f}, {1, 3, 0.8f}},
          Bounds{{0, 0, -2}, {1, 1, -1}},
          Bounds{{0, 0, 2}, {1, 1, 3}}})
    {
        CHECK_FALSE(boundsVisible(bounds, identity));
    }
    CHECK(boundsVisible({{-2, -2, -1}, {2, 2, 2}}, identity));
    CHECK(boundsVisible({{1, 0, 0}, {2, 1, 1}}, identity));
    CHECK(boundsVisible({{-2, 0, 0}, {-1, 1, 1}}, identity));
    CHECK(boundsVisible({{0, 1, 0}, {1, 2, 1}}, identity));
    CHECK(boundsVisible({{0, -2, 0}, {1, -1, 1}}, identity));
    CHECK(boundsVisible({{0, 0, -1}, {1, 1, 0}}, identity));
    CHECK(boundsVisible({{0, 0, 1}, {1, 1, 2}}, identity));
}

TEST_CASE("draw list snapshots visible instances and survives source scene clear")
{
    Scene scene, other;
    auto camera = scene.createCamera(scene.getRoot());
    CHECK_THROWS_AS(camera->setPerspective(0, 0.1f, 10), EngineError);
    CHECK_THROWS_AS(camera->setPerspective(1, 10, 1), EngineError);
    CHECK_THROWS_AS(camera->projectionMatrix(0), EngineError);
    CHECK_THROWS_AS(buildDrawList(other, *camera, 1), EngineError);
    auto entity = scene.createEntity(scene.getRoot());
    entity->setLocalPosition({0, 0, 3});
    auto renderer = scene.createMeshRenderer(entity);
    auto mesh = testMesh();
    renderer->setMesh(mesh);
    auto material = renderer->makeMaterialUnique(1);
    auto list = buildDrawList(scene, *camera, 1);
    REQUIRE(list.items.size() == 2);
    CHECK(list.items[1].material == material);
    CHECK(list.items[1].subMeshIndex == 1);
    entity->setLocalPosition({0, 0, -3});
    CHECK(buildDrawList(scene, *camera, 1).items.empty());
    entity->setLocalPosition({100, 0, 3});
    CHECK(buildDrawList(scene, *camera, 1).items.empty());
    entity->setLocalPosition({0, 0, 3});
    scene.getRoot()->setLocalScale({0, 1, 1});
    CHECK_THROWS_AS(buildDrawList(scene, *camera, 1), EngineError);
    scene.clear();
    CHECK(list.items[0].worldFromLocal[3].z == 3);
    CHECK(list.items[0].mesh == mesh);
    CHECK(list.items[1].material == material);
}

TEST_CASE("camera motion and scaled parented bounds change visibility without a scene update")
{
    Scene scene;
    auto cameraEntity = scene.createEntity(scene.getRoot());
    auto camera = scene.createCamera(cameraEntity);
    auto parent = scene.createEntity(scene.getRoot());
    auto entity = scene.createEntity(parent);
    parent->setLocalPosition({0, 0, 3});
    parent->setLocalScale({2, 3, 1});
    scene.createMeshRenderer(entity)->setMesh(testMesh());
    CHECK(buildDrawList(scene, *camera, 1).items.size() == 2);
    cameraEntity->setLocalPosition({100, 0, 0});
    CHECK(buildDrawList(scene, *camera, 1).items.empty());
    parent->setLocalPosition({100, 0, 3});
    CHECK(buildDrawList(scene, *camera, 1).items.size() == 2);
    std::string error;
    const auto turn = math::quatFromAxisAngle({0, 1, 0}, 3.14159265f, error);
    REQUIRE(turn.has_value());
    cameraEntity->setLocalRotation(*turn);
    CHECK(buildDrawList(scene, *camera, 1).items.empty());
}

TEST_CASE("skin palettes cancel placement and mesh transforms and share one snapshot across submeshes")
{
    Scene scene;
    auto camera = scene.createCamera(scene.getRoot());
    auto base = testMesh();
    MeshDeformationData deformation;
    deformation.influenceSets = {std::vector<VertexJointInfluences>(3, {{0, 0, 0, 0}, {1, 0, 0, 0}})};
    auto mesh = Mesh::create(
        {base->vertices().begin(), base->vertices().end()},
        {base->indices().begin(), base->indices().end()},
        {base->subMeshes().begin(), base->subMeshes().end()},
        deformation
    );
    auto skin = Skin::create({"joint", {{0, math::mat4Translation({-1, 0, 0})}}, {}});
    MeshRenderer* first = nullptr;
    for (float placement : {100.f, -100.f})
    {
        auto root = scene.createEntity(scene.getRoot());
        root->setLocalPosition({placement, 0, 3});
        root->setLocalScale({2, 3, 4});
        auto entity = scene.createEntity(root);
        entity->setLocalPosition({2, 0, 0});
        entity->setLocalScale({2, 1, 1});
        auto parent = scene.createEntity(root);
        parent->setLocalPosition({3, 0, 0});
        auto joint = scene.createEntity(parent);
        joint->setLocalPosition({4, 0, 0});
        auto renderer = scene.createMeshRenderer(entity);
        renderer->setMesh(mesh);
        renderer->setSkin(skin, {joint});
        if (!first)
        {
            first = renderer;
        }
        auto palette = snapshotDeformation(*renderer).palette;
        // Mesh inverse takes x from root domain to (x - 2)/2; joint +7 and bind -1 give x/2 +2.
        CHECK(palette[0][0].x == .5f);
        CHECK(palette[0][3].x == 2);
        CHECK(palette[0][3].y == 0);
        CHECK(palette[0][3].z == 0);
    }
    auto list = buildDrawList(scene, *camera, 1);
    REQUIRE(list.deformations.size() == 2);
    REQUIRE(list.items.size() == 4); // Both static bounds are outside the view.
    CHECK(list.items[0].deformationJob == list.items[1].deformationJob);
    CHECK(list.items[2].deformationJob == list.items[3].deformationJob);
    CHECK(list.deformations[0].storage != list.deformations[1].storage);
    auto storage = first->deformationStorage();
    first->entity()->setLocalScale({0, 1, 1});
    CHECK_THROWS_AS(snapshotDeformation(*first), EngineError);
    first->setMesh(testMesh());
    CHECK_FALSE(first->deformationStorage());
    scene.clear();
    CHECK(list.deformations[0].palette[0][3].x == 2);
    CHECK(list.deformations[0].storage == storage);
}
