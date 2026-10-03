// Small observable rendering fixture; generated geometry is original OFG code and has no importer dependency.
#include "lab/scene-fixture.h"
#include "mesh-shader.h"
#include "core/engine-error.h"

namespace ofg {
std::shared_ptr<Material> createFixtureMaterial()
{
    auto material = Material::create(Shader::create("mesh.slang", meshShader));
    material->setUniform("tint", math::Vec4{1, 1, 1, 1});
    return material;
}

std::shared_ptr<Mesh> createCubeMesh(std::shared_ptr<Material> material)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> parts;
    const math::Vec3 normals[]{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const math::Vec3 horizontal[]{{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    for (uint32_t face = 0; face < 6; ++face)
    {
        const auto normal = normals[face];
        const auto right = horizontal[face];
        const auto up = math::cross(right, normal);
        for (const math::Vec2 uv : {math::Vec2{0, 0}, math::Vec2{1, 0}, math::Vec2{1, 1}, math::Vec2{0, 1}})
        {
            const auto position = math::add(
                math::mul(normal, 0.5f),
                math::add(math::mul(right, uv.x - 0.5f), math::mul(up, uv.y - 0.5f))
            );
            vertices.push_back({position, normal, uv});
        }
        // Clockwise when viewed from outside in the left-handed camera convention.
        const uint32_t base = face * 4;
        indices.insert(indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        parts.push_back({face * 6, 6, material});
    }
    return Mesh::create(std::move(vertices), std::move(indices), std::move(parts));
}

std::unique_ptr<Scene> createSceneFixture()
{
    auto scene = std::make_unique<Scene>();
    auto cameraEntity = scene->createEntity(scene->getRoot());
    const math::Vec3 eye{2.4f, 1.8f, -5.0f};
    cameraEntity->setLocalPosition(eye);
    std::string error;
    auto rotation = math::quatLookAtLh(eye, {0, 0, 0}, {0, 1, 0}, error);
    if (!rotation)
    {
        throw EngineError(error);
    }
    cameraEntity->setLocalRotation(*rotation);
    scene->setActiveCamera(scene->createCamera(cameraEntity));

    auto mesh = createCubeMesh(createFixtureMaterial());
    auto parent = scene->createEntity(scene->getRoot());
    parent->setLocalPosition({-0.55f, 0, 0});
    auto first = scene->createEntity(parent);
    scene->createMeshRenderer(first)->setMesh(mesh);
    auto second = scene->createEntity(scene->getRoot());
    second->setLocalPosition({0.55f, 0.15f, 0.25f});
    auto renderer = scene->createMeshRenderer(second);
    renderer->setMesh(mesh);
    auto tint = renderer->makeMaterialUnique(0);
    tint->setUniform("tint", math::Vec4{0.2f, 0.8f, 1, 1});
    auto hidden = scene->createEntity(scene->getRoot());
    hidden->setLocalPosition({100, 0, 0});
    scene->createMeshRenderer(hidden)->setMesh(mesh);
    return scene;
}
} // namespace ofg
