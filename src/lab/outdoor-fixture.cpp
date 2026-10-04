// Small original procedural fixture; the studio/model laboratories remain separate diagnostics.
#include "lab/outdoor-fixture.h"
#include "lab/pbr-fixture.h"
#include "lab/scene-fixture.h"
#include "lab/sphere.h"
#include "resources/pbr-material.h"
namespace ofg {
std::unique_ptr<Scene> createOutdoorFixture()
{
    auto scene = std::make_unique<Scene>();
    scene->getRoot()->setName("Outdoor Laboratory");
    scene->lighting.hdr = true;
    scene->lighting.environment = createStudioEnvironment();
    scene->lighting.outdoor = OutdoorLighting{};
    auto camera = scene->createEntity(scene->getRoot());
    camera->setName("Outdoor Camera");
    camera->setLocalPosition({0, 3, -15});
    auto view = scene->createCamera(camera);
    view->setPerspective(1.04719755f, .1f, 20000);
    scene->setActiveCamera(view);
    PbrMaterialDesc desc;
    desc.metallic = 0;
    desc.roughness = .8f;
    desc.baseColor = {.25f, .3f, .2f, 1};
    auto groundMaterial = createPbrMaterial(desc);
    std::vector<Vertex> vertices{
        {{-5000, 0, -5000}, {0, 1, 0}, {0, 0}},
        {{-5000, 0, 5000}, {0, 1, 0}, {0, 1}},
        {{5000, 0, 5000}, {0, 1, 0}, {1, 1}},
        {{5000, 0, -5000}, {0, 1, 0}, {1, 0}}
    };
    auto ground = Mesh::create(std::move(vertices), {0, 1, 2, 0, 2, 3}, {{0, 6, groundMaterial}});
    auto floor = scene->createEntity(scene->getRoot());
    floor->setName("Ground");
    auto floorRenderer = scene->createMeshRenderer(floor);
    floorRenderer->setMesh(ground);
    // The flat inspection ground receives shadows but cannot occlude geometry above it.
    floorRenderer->setCastsShadows(false);
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 5; ++column)
        {
            desc.baseColor = {.8f, .45f, .15f, 1};
            desc.metallic = float(row);
            desc.roughness = .05f + column * .23f;
            auto e = scene->createEntity(scene->getRoot());
            e->setName("Material sphere");
            e->setLocalPosition({float(column - 2) * 3, 1, float(row) * 4});
            scene->createMeshRenderer(e)->setMesh(createSphereMesh(createPbrMaterial(desc)));
        }
    }
    desc.baseColor = {.45f, .45f, .45f, 1};
    desc.metallic = 0;
    desc.roughness = .7f;
    auto box = createCubeMesh(createPbrMaterial(desc));
    for (int i = 0; i < 6; ++i)
    {
        float distance = i < 3 ? float(15 + i * 20) : float(300 + (i - 3) * 1500);
        float height = i < 3 ? 8.0f : 80.0f;
        auto e = scene->createEntity(scene->getRoot());
        e->setName("Shadow and haze landmark");
        e->setLocalPosition({float(i % 2 ? -1 : 1) * (i < 3 ? 10.0f : 180.0f), height / 2, distance});
        e->setLocalScale({i < 3 ? 2.0f : 40.0f, height, i < 3 ? 2.0f : 40.0f});
        scene->createMeshRenderer(e)->setMesh(box);
    }
    // A perforated, double-sided card exercises identical alpha cutoff in color and depth passes.
    std::vector<std::byte> pixels(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            size_t offset = (y * 64 + x) * 4;
            pixels[offset] = std::byte{90};
            pixels[offset + 1] = std::byte{170};
            pixels[offset + 2] = std::byte{65};
            pixels[offset + 3] = std::byte((x % 16 < 7 || y % 16 < 7) ? 255 : 0);
        }
    }
    desc.baseColor = {1, 1, 1, 1};
    desc.alphaMode = AlphaMode::Mask;
    desc.doubleSided = true;
    desc.textures[0].texture =
        Texture::create({64, 64, TextureFormat::RGBA8UnormSrgb, TextureMipmaps::Generate}, {pixels});
    auto mask = createPbrMaterial(desc);
    std::vector<Vertex> cardVertices{
        {{-1.5f, 0, 0}, {0, 0, -1}, {0, 1}},
        {{-1.5f, 3, 0}, {0, 0, -1}, {0, 0}},
        {{1.5f, 3, 0}, {0, 0, -1}, {1, 0}},
        {{1.5f, 0, 0}, {0, 0, -1}, {1, 1}}
    };
    auto card = Mesh::create(std::move(cardVertices), {0, 1, 2, 0, 2, 3}, {{0, 6, mask}});
    for (float side : {-1.f, 1.f})
    {
        auto e = scene->createEntity(scene->getRoot());
        e->setName("Masked shadow card");
        e->setLocalPosition({side * 9, 0, 0});
        e->setLocalScale({side, 1, 1});
        scene->createMeshRenderer(e)->setMesh(card);
    }
    return scene;
}
} // namespace ofg
