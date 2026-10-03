// GPU-independent texture contracts: typed pixels, views, dependency identity and real image loading.
#include "resources/texture.h"
#include "resources/material.h"
#include "resources/resources.h"
#include "core/engine-error.h"
#include "scene/scene.h"
#include "render/draw-list.h"
#include <doctest.h>
#include <array>
#include <fstream>
#include <filesystem>

using namespace ofg;

TEST_CASE("texture validates dimensions format pitch and mip ranges")
{
    std::array<std::byte, 48> bytes{};
    auto image = Texture::create({3, 2, TextureFormat::RGBA8Unorm, TextureMipmaps::Generate}, {bytes, 16});
    CHECK(image->isLoaded());
    CHECK(image->mipCount() == 2);
    CHECK(image->defaultView().desc().mipCount == 2);
    CHECK(image->createView({1, 0}).desc().mipCount == 1);
    CHECK_THROWS_AS(image->createView({2, 1}), EngineError);
    CHECK_THROWS_AS(image->createView({1, UINT32_MAX}), EngineError);
    CHECK_THROWS_AS(Texture::create({0, 2, TextureFormat::R8Unorm}, {bytes}), EngineError);
    CHECK_THROWS_AS(Texture::create({3, 2, TextureFormat::RGBA8Unorm}, {bytes, 11}), EngineError);
    CHECK_THROWS_AS(Texture::create({3, 2, TextureFormat::RGBA8Unorm}, {bytes, SIZE_MAX}), EngineError);
    CHECK_THROWS_AS(Texture::create({UINT32_MAX, UINT32_MAX, TextureFormat::RGBA32Float}, {bytes}), EngineError);
    CHECK_THROWS_AS(Texture::create({3, 2, TextureFormat::RGBA32Float}, {bytes}), EngineError);
    CHECK_THROWS_AS(texturePixelSize(TextureFormat(99)), EngineError);
    CHECK(texturePixelSize(TextureFormat::RG16Float) == 4);
    CHECK(texturePixelSize(TextureFormat::RGBA32Float) == 16);
    CHECK(Texture::create({1, 7, TextureFormat::R8Unorm, TextureMipmaps::Generate}, {bytes})->mipCount() == 3);
}

TEST_CASE("views and cloned material bindings retain shared immutable dependencies")
{
    std::array<std::byte, 4> bytes{};
    auto image = Texture::create({1, 1, TextureFormat::RGBA8Unorm}, {bytes});
    auto view = image->defaultView();
    std::weak_ptr<Texture> weak = image;
    image.reset();
    REQUIRE_FALSE(weak.expired());
    auto material = Material::create(Shader::create("test", "source"));
    auto sampler = Sampler::create();
    material->setTexture("image", view);
    material->setSampler("imageSampler", sampler);
    auto clone = material->clone();
    CHECK(bindingTexture(clone->textures().at("image")) == view.texture());
    CHECK(clone->samplers().at("imageSampler") == sampler);
    auto replacement = Texture::create({1, 1, TextureFormat::RGBA8Unorm}, {bytes});
    clone->setTexture("image", replacement);
    CHECK(bindingTexture(material->textures().at("image")) != replacement);
    CHECK_THROWS_AS(material->setTexture("draw", replacement), EngineError);
    CHECK_THROWS_AS(material->setTexture("image", std::shared_ptr<Texture>{}), EngineError);
    CHECK_THROWS_AS(material->setSampler("image", sampler), EngineError);
    CHECK_THROWS_AS(Sampler::create({TextureFilter(99)}), EngineError);
}

TEST_CASE("image load failures are terminal and abandoned requests do not stay owned")
{
    const std::string key = "ofg-test-nonexistent-image.png";
    auto image = Resources::loadResourceAsync<Texture>(key);
    CHECK(image->state() == ResourceState::Loading);
    CHECK_THROWS_AS(image->desc(), EngineError);
    CHECK(Resources::loadResourceAsync<Texture>(key) == image);
    Resources::update();
    CHECK(image->isFailed());
    CHECK(image->error().find(key) != std::string::npos);
    std::weak_ptr<Texture> weak = image;
    image.reset();
    CHECK(weak.expired());
    auto retry = Resources::loadResourceAsync<Texture>(key);
    CHECK_FALSE(retry->isFinished());
    std::weak_ptr<Texture> pending = retry;
    retry.reset();
    Resources::update();
    CHECK(pending.expired());
}

TEST_CASE("real PNG and JPEG loads share identity and reject corrupt content")
{
    for (const auto* path : {"assets/checker.png", "assets/checker.jpg"})
    {
        auto image = Resources::loadResourceAsync<Texture>(path);
        Resources::update();
        CHECK_FALSE(image->isFinished());
        auto same = Resources::loadResource<Texture>(path);
        REQUIRE_MESSAGE(image->isLoaded(), image->error());
        CHECK(same == image);
        CHECK(image->desc().width == 96);
        CHECK(image->desc().height == 96);
        CHECK(image->desc().format == TextureFormat::RGBA8UnormSrgb);
        CHECK(image->mipCount() == 7);
    }
    auto corrupt = Resources::loadResource<Texture>("assets/README.md");
    CHECK(corrupt->isFailed());
    CHECK(corrupt->error().find("header") != std::string::npos);
    auto pending = Resources::loadResourceAsync<Texture>("assets/checker.png");
    Resources::update(); // Opens a real file before abandonment.
    std::weak_ptr<Texture> weak = pending;
    pending.reset();
    CHECK(weak.expired());
    Resources::update();
}

TEST_CASE("draw extraction skips pending textures and reports failed dependencies")
{
    Scene scene;
    auto camera = scene.createCamera(scene.createEntity(scene.getRoot()));
    auto material = Material::create(Shader::create("test", "source"));
    auto image = Resources::loadResourceAsync<Texture>("assets/checker.png");
    material->setTexture("image", image);
    auto mesh = Mesh::create(
        {{{0, 0, 1}, {}, {}}, {{0.1f, 0, 1}, {}, {}}, {{0, 0.1f, 1}, {}, {}}},
        {0, 1, 2},
        {{0, 3, material}}
    );
    scene.createMeshRenderer(scene.createEntity(scene.getRoot()))->setMesh(mesh);
    CHECK(buildDrawList(scene, *camera, 1).items.empty());
    auto loaded = Resources::loadResource<Texture>("assets/checker.png");
    REQUIRE(loaded->isLoaded());
    CHECK(buildDrawList(scene, *camera, 1).items.size() == 1);
    auto failed = Resources::loadResource<Texture>("assets/no-such-image.png");
    material->setTexture("image", failed);
    CHECK_THROWS_WITH_AS(buildDrawList(scene, *camera, 1), doctest::Contains("no-such-image"), EngineError);
}

TEST_CASE("a retained corrupt image stays failed and reloads after all owners release")
{
    const std::filesystem::path path = "texture-truncated-test.png";
    {
        std::ifstream source("assets/checker.png", std::ios::binary);
        std::array<char, 48> header;
        REQUIRE(bool(source.read(header.data(), header.size())));
        std::ofstream output(path, std::ios::binary);
        output.write(header.data(), header.size());
    }
    auto image = Resources::loadResource<Texture>(path.string());
    REQUIRE(image->isFailed());
    CHECK(image->error().find("decode") != std::string::npos);
    std::filesystem::copy_file("assets/checker.png", path, std::filesystem::copy_options::overwrite_existing);
    CHECK(Resources::loadResource<Texture>(path.string()) == image);
    image.reset();
    auto retry = Resources::loadResource<Texture>(path.string());
    CHECK(retry->isLoaded());
    std::filesystem::remove(path);
}
