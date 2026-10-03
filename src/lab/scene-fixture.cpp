// Small observable rendering fixture; generated geometry is original OFG code and has no importer dependency.
#include "lab/scene-fixture.h"
#include "mesh-shader.h"
#include "core/engine-error.h"
#include "resources/resources.h"

namespace ofg {
std::shared_ptr<Material> createFixtureMaterial(std::shared_ptr<Texture> texture)
{
    auto material = Material::create(Shader::create("mesh.slang", meshShader));
    material->setUniform("tint", math::Vec4{1, 1, 1, 1});
    if (!texture)
    {
        std::vector<std::byte> pixels(6 * 6 * 4);
        for (size_t y = 0; y < 6; ++y)
        {
            for (size_t x = 0; x < 6; ++x)
            {
                const auto c = std::byte((x + y) % 2 ? 243 : 128);
                const auto i = (y * 6 + x) * 4;
                pixels[i] = pixels[i + 1] = pixels[i + 2] = c;
                pixels[i + 3] = std::byte{255};
            }
        }
        texture = Texture::create({6, 6, TextureFormat::RGBA8UnormSrgb, TextureMipmaps::Generate}, {pixels});
    }
    material->setTexture("baseColor", std::move(texture));
    material->setSampler("baseColorSampler", Sampler::create());
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

std::shared_ptr<Texture> createFloatFixtureTexture(bool fp32)
{
    // Exact binary values exercise floating storage without requiring half arithmetic in shaders.
    std::vector<float> floats(96 * 96 * 4);
    std::vector<uint16_t> halves(96 * 96 * 4);
    for (size_t y = 0; y < 96; ++y)
    {
        for (size_t x = 0; x < 96; ++x)
        {
            bool light = (x / 16 + y / 16) % 2;
            for (size_t c = 0; c < 4; ++c)
            {
                floats[(y * 96 + x) * 4 + c] = light || c == 3 ? 1.f : 0.25f;
                halves[(y * 96 + x) * 4 + c] = light || c == 3 ? 0x3c00 : 0x3400;
            }
        }
    }
    return Texture::create(
        {96, 96, fp32 ? TextureFormat::RGBA32Float : TextureFormat::RGBA16Float, TextureMipmaps::Generate},
        {fp32 ? std::as_bytes(std::span(floats)) : std::as_bytes(std::span(halves))}
    );
}

std::unique_ptr<Scene> createSceneFixture(const std::string& texturePath)
{
    auto texture =
        texturePath.empty() ? std::shared_ptr<Texture>{} : Resources::loadResourceAsync<Texture>(texturePath);
    return createSceneFixture(std::move(texture));
}

std::unique_ptr<Scene> createSceneFixture(std::shared_ptr<Texture> texture)
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

    auto mesh = createCubeMesh(createFixtureMaterial(std::move(texture)));
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
