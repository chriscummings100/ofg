// Sphere-grid laboratory: shared geometry and independent material values, with no file-import dependency.
#include "lab/pbr-fixture.h"
#include "lab/fly-camera.h"
#include "lab/sphere.h"
#include "resources/pbr-material.h"
#include "pbr-environment.h"

namespace ofg {
std::shared_ptr<Environment> createStudioEnvironment()
{
    return Environment::fromBytes(pbrEnvironment);
}

std::unique_ptr<Scene> createPbrFixture(bool maximumLayout)
{
    auto scene = std::make_unique<Scene>();
    auto camera = scene->createEntity(scene->getRoot());
    FlyCamera{}.reset(*camera);
    scene->setActiveCamera(scene->createCamera(camera));
    scene->lighting.hdr = true;
    scene->lighting.environment = createStudioEnvironment();
    scene->lighting.lightCount = 2;
    scene->lighting.lights[0].direction = {0.4f, -0.5f, 1};
    scene->lighting.lights[0].intensity = 2;
    scene->lighting.lights[1].type = LightType::Point;
    scene->lighting.lights[1].position = {-4, 4, -4};
    scene->lighting.lights[1].color = {1, 0.85f, 0.65f};
    scene->lighting.lights[1].intensity = 45;
    PbrMaterialDesc desc;
    desc.baseColor = {0.8f, 0.55f, 0.25f, 1};
    auto base = createPbrMaterial(desc);
    auto mesh = createSphereMesh(base);
    for (int row = 0; row < 7; ++row)
    {
        for (int column = 0; column < 7; ++column)
        {
            auto entity = scene->createEntity(scene->getRoot());
            entity->setLocalPosition({(column - 3) * 1.65f, (row - 3) * 1.65f, 0});
            entity->setLocalScale({0.65f, 0.65f, 0.65f});
            auto renderer = scene->createMeshRenderer(entity);
            renderer->setMesh(mesh);
            auto material = renderer->makeMaterialUnique(0);
            material->setUniform("roughness", column / 6.0f);
            material->setUniform("metallic", row / 6.0f);
        }
    }
    // Supplementary swatches below the sweep: baseline, coat, sheen, film, anisotropy, emission, unlit.
    for (int column = 0; column < 7; ++column)
    {
        PbrMaterialDesc swatch;
        swatch.baseColor = {0.25f, 0.08f, 0.025f, 1};
        swatch.metallic = 0;
        swatch.roughness = 0.35f;
        if (column == 1)
        {
            swatch.clearcoat = 1;
            swatch.clearcoatRoughness = 0.08f;
        }
        if (column == 2)
        {
            swatch.sheenColor = {0.8f, 0.12f, 0.05f};
            swatch.sheenRoughness = 0.5f;
        }
        if (column == 3)
        {
            swatch.iridescence = 1;
            swatch.metallic = 1;
        }
        if (column == 4)
        {
            swatch.anisotropy = 0.9f;
            swatch.metallic = 1;
        }
        if (column == 5)
        {
            swatch.emissive = {0.1f, 0.7f, 1};
            swatch.emissiveStrength = 3;
        }
        if (column == 6)
        {
            swatch.unlit = true;
            swatch.baseColor = {0.1f, 0.7f, 1, 1};
        }
        auto entity = scene->createEntity(scene->getRoot());
        entity->setLocalPosition({(column - 3) * 1.65f, -6.6f, 0});
        entity->setLocalScale({0.65f, 0.65f, 0.65f});
        auto renderer = scene->createMeshRenderer(entity);
        renderer->setMesh(mesh);
        renderer->setMaterialOverride(0, createPbrMaterial(swatch));
    }
    // A second row exercises shared texture storage, independent UV transforms and coverage.
    std::vector<std::byte> colorPixels(32 * 32 * 4), dataPixels(32 * 32 * 4), normalPixels(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            size_t i = (y * 32 + x) * 4;
            bool light = (x / 4 + y / 4) % 2;
            colorPixels[i] = std::byte(light ? 230 : 50);
            colorPixels[i + 1] = std::byte(light ? 160 : 95);
            colorPixels[i + 2] = std::byte(light ? 60 : 180);
            colorPixels[i + 3] = std::byte(light ? 255 : 60);
            dataPixels[i] = std::byte(light ? 255 : 80);
            dataPixels[i + 1] = std::byte(light ? 220 : 70);
            dataPixels[i + 2] = std::byte(light ? 255 : 0);
            dataPixels[i + 3] = std::byte(light ? 220 : 100);
            normalPixels[i] = std::byte(light ? 170 : 85);
            normalPixels[i + 1] = std::byte{128};
            normalPixels[i + 2] = std::byte{245};
            normalPixels[i + 3] = std::byte{255};
        }
    }
    auto color = Texture::create({32, 32, TextureFormat::RGBA8UnormSrgb, TextureMipmaps::Generate}, {colorPixels});
    auto data = Texture::create({32, 32, TextureFormat::RGBA8Unorm, TextureMipmaps::Generate}, {dataPixels});
    auto normal = Texture::create({32, 32, TextureFormat::RGBA8Unorm, TextureMipmaps::Generate}, {normalPixels});
    for (int column = 0; column < 7; ++column)
    {
        PbrMaterialDesc swatch;
        swatch.metallic = 0;
        swatch.roughness = 0.4f;
        // Per-slot transforms apply independently even when AO/MR share one image allocation.
        swatch.textures[0].texture = color;
        if (column == 0)
        {
            swatch.textures[1].texture = data;
            swatch.textures[3].texture = data;
        }
        if (column == 1)
        {
            swatch.textures[2].texture = normal;
            swatch.textures[2].rotation = 0.7f;
        }
        if (column == 2)
        {
            swatch.ior = 1.8f;
            swatch.specularColor = {0.4f, 0.7f, 1};
            swatch.textures[5].texture = data;
            swatch.textures[6].texture = color;
        }
        if (column == 3)
        {
            swatch.clearcoat = 1;
            swatch.textures[7].texture = data;
            swatch.textures[8].texture = data;
            swatch.textures[9].texture = normal;
            swatch.textures[9].rotation = -0.5f;
            swatch.textures[9].uvSet = 1;
        }
        if (column == 4)
        {
            swatch.sheenColor = {0.8f, 0.8f, 0.8f};
            swatch.sheenRoughness = 0.6f;
            swatch.textures[10].texture = color;
            swatch.textures[11].texture = data;
        }
        if (column == 5)
        {
            swatch.iridescence = 1;
            swatch.anisotropy = 0.8f;
            swatch.metallic = 1;
            swatch.textures[12].texture = data;
            swatch.textures[13].texture = data;
            swatch.textures[14].texture = normal;
        }
        if (column == 6)
        {
            swatch.alphaMode = AlphaMode::Mask;
            swatch.doubleSided = true;
            swatch.emissive = {0.1f, 0.1f, 0.1f};
            swatch.textures[4].texture = color;
        }
        if (maximumLayout && column == 0)
        {
            for (size_t slot = 0; slot < 12; ++slot)
            {
                swatch.textures[slot].texture = data;
            }
            for (size_t slot : {size_t(0), size_t(4), size_t(6), size_t(10)})
            {
                swatch.textures[slot].texture = color;
            }
            swatch.textures[2].texture = normal;
            swatch.textures[9].texture = normal;
            swatch.clearcoat = 0.5f;
            swatch.sheenColor = {0.2f, 0.1f, 0.05f};
        }
        auto entity = scene->createEntity(scene->getRoot());
        entity->setLocalPosition({(column - 3) * 1.65f, -8.25f, 0});
        entity->setLocalScale({0.65f, 0.65f, 0.65f});
        auto renderer = scene->createMeshRenderer(entity);
        renderer->setMesh(mesh);
        renderer->setMaterialOverride(0, createPbrMaterial(swatch));
    }
    return scene;
}
} // namespace ofg
