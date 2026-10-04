// CPU contracts for inspection geometry/camera, surface material specialization and baked environment decoding.
#include <ostream>
#include <doctest.h>
#include "lab/fly-camera.h"
#include "lab/sphere.h"
#include "scene/scene.h"
#include "core/engine-error.h"
#include <cmath>

using namespace ofg;

TEST_CASE("Fly camera normalizes motion and preserves reset and frame delta contracts")
{
    Scene scene;
    auto entity = scene.createEntity(scene.getRoot());
    FlyCamera camera;
    camera.reset(*entity);
    const auto start = entity->localTransform().position;
    FlyCameraInput input;
    input.movement = {1, 1, 1};
    camera.update(*entity, input, 0.25f);
    CHECK(math::length(math::sub(entity->localTransform().position, start)) == doctest::Approx(1));
    camera.reset(*entity);
    camera.update(*entity, input, 0.125f);
    camera.update(*entity, input, 0.125f);
    CHECK(math::length(math::sub(entity->localTransform().position, start)) == doctest::Approx(1));
    input = {};
    input.lookPixels = {0, -100000};
    camera.update(*entity, input, 0);
    auto forward = math::mul(entity->worldTransform(), math::Vec4{0, 0, 1, 0});
    CHECK(forward.y > 0.99f);
    CHECK(forward.z > 0.01f);
    input.reset = true;
    camera.update(*entity, input, 0);
    CHECK(entity->localTransform().position.z == start.z);
    CHECK(entity->localTransform().rotation.w == 1);
    CHECK_THROWS_AS(camera.update(*entity, {}, -1), EngineError);
}

TEST_CASE("Sphere has outward nondegenerate triangles and a continuous UV seam")
{
    auto material = Material::create(Shader::create("unused", "unused"));
    auto sphere = createSphereMesh(material, 16, 8);
    REQUIRE(sphere->vertices().size() == 17 * 9);
    CHECK(sphere->indices().size() == 16 * 7 * 6);
    for (const auto& vertex : sphere->vertices())
    {
        CHECK(math::length(vertex.normal) == doctest::Approx(1));
        CHECK(
            math::dot(vertex.normal, {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z}) ==
            doctest::Approx(0).epsilon(1e-5)
        );
    }
    for (size_t i = 0; i < sphere->indices().size(); i += 3)
    {
        auto a = sphere->vertices()[sphere->indices()[i]].position;
        auto b = sphere->vertices()[sphere->indices()[i + 1]].position;
        auto c = sphere->vertices()[sphere->indices()[i + 2]].position;
        CHECK(math::dot(math::cross(math::sub(b, a), math::sub(c, a)), a) > 0);
    }
    for (int row = 0; row <= 8; ++row)
    {
        auto a = sphere->vertices()[row * 17];
        auto b = sphere->vertices()[row * 17 + 16];
        CHECK(math::length(math::sub(a.position, b.position)) < 1e-6f);
        CHECK(a.uv.x == 0);
        CHECK(b.uv.x == 1);
    }
    const auto& equator = sphere->vertices()[4 * 17];
    auto b = math::mul(
        math::cross(equator.normal, {equator.tangent.x, equator.tangent.y, equator.tangent.z}),
        equator.tangent.w
    );
    CHECK(b.y < -0.99f);
    CHECK_THROWS_AS(createSphereMesh(material, 2, 8), EngineError);
}
#include "resources/pbr-material.h"
#include <limits>

TEST_CASE("PBR defaults, texture specialization and cloned values have independent contracts")
{
    PbrMaterialDesc desc;
    auto a = createPbrMaterial(desc), b = createPbrMaterial(desc);
    CHECK(a->shader() == b->shader());
    CHECK(std::get<float>(a->uniforms().at("metallic")) == 1);
    CHECK(std::get<float>(a->uniforms().at("ior")) == 1.5f);
    CHECK(std::get<float>(a->uniforms().at("clearcoat")) == 0);
    auto copy = a->clone();
    copy->setUniform("roughness", 0.25f);
    CHECK(std::get<float>(a->uniforms().at("roughness")) == 1);
    CHECK(copy->renderState() == a->renderState());
    desc.roughness = -1;
    CHECK_THROWS_AS(createPbrMaterial(desc), EngineError);
    desc.roughness = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(createPbrMaterial(desc), EngineError);
    desc = {};
    desc.ior = 0.9f;
    CHECK_THROWS_AS(createPbrMaterial(desc), EngineError);
    desc = {};
    desc.thicknessMaximum = 10;
    CHECK_THROWS_AS(createPbrMaterial(desc), EngineError);
    desc = {};
    desc.textures[static_cast<size_t>(PbrSlot::BaseColor)].uvSet = 2;
    CHECK_THROWS_AS(createPbrMaterial(desc), EngineError);
    std::array<std::byte, 4> white{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    auto texture = Texture::create({1, 1, TextureFormat::RGBA8Unorm}, {white});
    desc = {};
    desc.textures[static_cast<size_t>(PbrSlot::BaseColor)].texture = texture;
    desc.textures[static_cast<size_t>(PbrSlot::BaseColor)].rotation = 1.570796327f;
    desc.textures[static_cast<size_t>(PbrSlot::BaseColor)].scale = {2, 3};
    desc.textures[static_cast<size_t>(PbrSlot::BaseColor)].offset = {0.25f, 0.5f};
    auto textured = createPbrMaterial(desc);
    CHECK(textured->shader() != a->shader());
    auto x = std::get<math::Vec3>(textured->uniforms().at("baseColorTransformX"));
    auto y = std::get<math::Vec3>(textured->uniforms().at("baseColorTransformY"));
    CHECK(x.y == doctest::Approx(-3));
    CHECK(y.x == doctest::Approx(2));
    CHECK(x.z == 0.25f);
    for (size_t i = 0; i < 12; ++i)
    {
        desc.textures[i].texture = texture;
    }
    CHECK_NOTHROW(createPbrMaterial(desc));
    desc.textures[static_cast<size_t>(PbrSlot::Iridescence)].texture = texture;
    CHECK_THROWS_WITH(
        createPbrMaterial(desc),
        "PBR material exceeds portable budget: at most 12 material textures plus four frame textures."
    );
}

TEST_CASE("Environment decoding rejects truncated, invalid and nonfinite payloads")
{
    // One texel per face per cube, one lookup texel, four half channels per texel.
    std::vector<uint8_t> bytes(20 + (6 * 3 + 1) * 8, 0);
    const char magic[8] = "OFGIBL1";
    std::copy(magic, magic + 8, bytes.begin());
    bytes[8] = bytes[12] = bytes[16] = 1;
    auto env = Environment::fromBytes(bytes);
    CHECK(env->isLoaded());
    CHECK(env->byteSize() == bytes.size() - 20);
    auto invalid = bytes;
    invalid.pop_back();
    CHECK_THROWS_AS(Environment::fromBytes(invalid), EngineError);
    invalid = bytes;
    invalid[21] = 0x7c;
    CHECK_THROWS_AS(Environment::fromBytes(invalid), EngineError);
    invalid = bytes;
    invalid[8] = 3;
    CHECK_THROWS_AS(Environment::fromBytes(invalid), EngineError);
    invalid = bytes;
    invalid[21] = 0x80;
    CHECK_THROWS_AS(Environment::fromBytes(invalid), EngineError);
    invalid = bytes;
    invalid[12] = 9;
    CHECK_THROWS_AS(Environment::fromBytes(invalid), EngineError);
    Scene scene;
    scene.lighting.environment = env;
    scene.lighting.hdr = true;
    Scene moved(std::move(scene));
    CHECK(moved.lighting.environment == env);
    moved.clear();
    CHECK_FALSE(moved.lighting.environment);
    CHECK_FALSE(moved.lighting.hdr);
}
