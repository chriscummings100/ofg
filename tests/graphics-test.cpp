// Native offscreen scene tests for indexed draws, uniforms, cache identity, retirement and Game ordering.
#include "render/graphics.h"
#include "render/shadow-renderer.h"
#include "terrain/terrain-profile.h"
#include "terrain/terrain-shading.h"
#include "render/deformation.h"
#include "ui/imgui-renderer.h"
#include "ui/workspace.h"
#include "render/texture-renderer.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "render/present.h"
#include "lab/scene-fixture.h"
#include "lab/pbr-fixture.h"
#include "lab/model-fixture.h"
#include "lab/character-fixture.h"
#include "lab/outdoor-fixture.h"
#include "lab/terrain-laboratory.h"
#include "render/queue-completion.h"
#include "scene/animation-binding.h"
#include "lab/sphere.h"
#include "resources/pbr-material.h"
#include "game.h"
#include "resources/resources.h"
#include "math/transform.h"
#include <ostream>
#include <doctest.h>
#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace ofg;

// Counts validation errors independently of returned RHI status codes.
struct SceneDiagnostics : rhi::IDebugCallback
{
    std::atomic<int> errors{0};
    // Records original validation diagnostics for failing test output.
    void SLANG_MCALL handleMessage(
        rhi::DebugMessageType type,
        rhi::DebugMessageSource,
        const char* message
    ) noexcept override
    {
        std::fprintf(stderr, "RHI scene: %s\n", message);
        if (type == rhi::DebugMessageType::Error)
        {
            ++errors;
        }
    }
};

struct GraphicsFixture
{
    SceneDiagnostics diagnostics;
    rhi::ComPtr<rhi::IDevice> device;
    rhi::ComPtr<rhi::ICommandQueue> queue;
    // Creates a native device; ordinary tests retain validation, profiling may explicitly disable it.
    explicit GraphicsFixture(bool validation = true)
    {
        rhi::DeviceDesc desc{};
        desc.deviceType = rhi::DeviceType::D3D12;
        desc.enableValidation = validation;
        desc.debugCallback = &diagnostics;
        REQUIRE(SLANG_SUCCEEDED(rhi::getRHI()->createDevice(desc, device.writeRef())));
        REQUIRE(SLANG_SUCCEEDED(device->getQueue(rhi::QueueType::Graphics, queue.writeRef())));
        Graphics::initialize(device, queue);
    }
    // Drains submitted references before static/device destruction, including on test exceptions.
    ~GraphicsFixture()
    {
        Game::shutdown();
        if (queue)
        {
            CHECK(SLANG_SUCCEEDED(queue->waitOnHost()));
        }
        Graphics::shutdown();
        queue.setNull();
        device.setNull();
        CHECK(diagnostics.errors == 0);
    }
    // Creates a readable offscreen color target; odd size exercises viewport and row-pitch handling.
    rhi::ComPtr<rhi::ITexture> target(
        rhi::Format format = rhi::Format::RGBA8Unorm,
        uint32_t width = 129,
        uint32_t height = 97
    )
    {
        rhi::TextureDesc desc{};
        desc.type = rhi::TextureType::Texture2D;
        desc.size = {width, height, 1};
        desc.format = format;
        desc.usage =
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySource | rhi::TextureUsage::ShaderResource;
        desc.defaultState = rhi::ResourceState::RenderTarget;
        rhi::ComPtr<rhi::ITexture> result;
        REQUIRE(SLANG_SUCCEEDED(device->createTexture(desc, nullptr, result.writeRef())));
        return result;
    }
    // Reads one interior pixel after completion, respecting the backend's padded row pitch.
    std::array<uint8_t, 4> pixel(rhi::ITexture* target, uint32_t x, uint32_t y)
    {
        REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
        rhi::ComPtr<ISlangBlob> pixels;
        rhi::SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
        auto p = static_cast<const uint8_t*>(pixels->getBufferPointer()) + y * layout.rowPitch + x * 4;
        return {p[0], p[1], p[2], p[3]};
    }
};

static const char* flatSource = R"(
struct DrawParameters { column_major float4x4 clipFromLocal; };
struct MaterialParameters { float4 tint; };
ConstantBuffer<DrawParameters> draw;
ConstantBuffer<MaterialParameters> material;
struct Input { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
[shader("vertex")] float4 vertexMain(Input input) : SV_Position
{ return mul(draw.clipFromLocal, float4(input.position, 1)); }
[shader("fragment")] float4 fragmentMain() : SV_Target { return material.tint; }
)";

// Creates a flat material sharing a supplied shader or a new independent source resource.
static std::shared_ptr<Material> flatMaterial(math::Vec4 color, std::shared_ptr<Shader> shader = {})
{
    auto material = Material::create(shader ? shader : Shader::create("flat.slang", flatSource));
    material->setUniform("tint", color);
    return material;
}

// Creates an indexed square with two separately addressable triangular submeshes.
static std::shared_ptr<Mesh> square(std::shared_ptr<Material> material)
{
    return Mesh::create(
        {{{-0.5f, -0.5f, 0}, {}, {}},
         {{0.5f, -0.5f, 0}, {}, {}},
         {{0.5f, 0.5f, 0}, {}, {}},
         {{-0.5f, 0.5f, 0}, {}, {}}},
        {0, 1, 2, 0, 2, 3},
        {{0, 3, material}, {3, 3, material}}
    );
}

// Reads the private output after submitted work, never using a CPU deformation runtime fallback.
static std::vector<Vertex> readSkinned(GraphicsFixture& fixture, const DeformationJob& job)
{
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    rhi::ComPtr<ISlangBlob> data;
    REQUIRE(SLANG_SUCCEEDED(fixture.device->readBuffer(
        DeformationRenderer::vertices(*job.storage),
        0,
        job.mesh->vertices().size_bytes(),
        data.writeRef()
    )));
    std::vector<Vertex> vertices(job.mesh->vertices().size());
    std::memcpy(vertices.data(), data->getBufferPointer(), vertices.size() * sizeof(Vertex));
    return vertices;
}

TEST_CASE("Compute skinning feeds outdoor shadow casters from the same deformed snapshot")
{
    GraphicsFixture fixture;
    auto scene = createOutdoorFixture();
    scene->lighting.outdoor->clouds.coverage = 0;
    scene->lighting.outdoor->dayCycle.timeHours = 12;
    auto base = createCubeMesh(createPbrMaterial({}));
    std::vector<Vertex> vertices(base->vertices().begin(), base->vertices().end());
    for (auto& vertex : vertices)
    {
        vertex.position.x += 10000;
    }
    MeshDeformationData deformation;
    deformation.influenceSets = {
        std::vector<VertexJointInfluences>(base->vertices().size(), {{0, 0, 0, 0}, {1, 0, 0, 0}})
    };
    auto mesh = Mesh::create(
        std::move(vertices),
        {base->indices().begin(), base->indices().end()},
        {base->subMeshes().begin(), base->subMeshes().end()},
        deformation
    );
    auto entity = scene->createEntity(scene->getRoot());
    entity->setLocalPosition({0, 2, 0});
    auto joint = scene->createEntity(entity);
    joint->setLocalPosition({-9996, 0, 0});
    auto renderer = scene->createMeshRenderer(entity);
    renderer->setMesh(mesh);
    renderer->setSkin(Skin::create({"caster", {{0, math::mat4Identity()}}, {}}), {joint});
    scene->update();
    auto list = buildDrawList(*scene, *scene->activeCamera(), 1.6f);
    // Only the ground is visible; the cube participates solely in the shadow pass.
    list.items.resize(1);
    std::erase_if(
        list.shadowCasters,
        [&](const auto& item)
        {
            return item.mesh != mesh;
        }
    );
    REQUIRE(list.shadowCasters.size() == base->subMeshes().size());
    REQUIRE(list.shadowCasters[0].deformationJob.has_value());
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 320, 200);
    for (int step = 0; step < 24; ++step)
    {
        Graphics::render(list, target);
    }
    // Strip row padding so exact comparisons measure only rendered pixels.
    const auto capture = [&](const DrawList& draws)
    {
        Graphics::render(draws, target);
        REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
        rhi::ComPtr<ISlangBlob> pixels;
        rhi::SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
        auto data = static_cast<const uint8_t*>(pixels->getBufferPointer());
        std::vector<uint8_t> result;
        for (size_t row = 0; row < 200; ++row)
        {
            result.insert(result.end(), data + row * layout.rowPitch, data + row * layout.rowPitch + 320 * 4);
        }
        return result;
    };
    auto animated = capture(list);
    auto reference = list;
    for (auto& caster : reference.shadowCasters)
    {
        caster.deformationJob.reset();
        caster.worldFromLocal[3].x -= 9996;
    }
    CHECK(animated == capture(reference));
    for (auto& caster : reference.shadowCasters)
    {
        caster.worldFromLocal[3].x += 9996;
    }
    CHECK(animated != capture(reference));
}

TEST_CASE("Compute skinning preserves attributes and uses every influence with affine normal and tangent rules")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    auto material = flatMaterial({1, 0, 0, 1});
    std::vector<Vertex> source{
        {{-.2f, -.2f, .5f}, {1, 1, 1}, {.2f, .7f}, {1, 1, 0, -1}, {.3f, .8f}, {.1f, .2f, .3f, .4f}},
        {{.2f, -.2f, .5f}, {0, 0, 1}, {.1f, .9f}, {}, {.4f, .6f}, {.9f, .8f, .7f, .6f}},
        {{0, .2f, .5f}, {1, 0, 0}, {}, {0, 1, 0, 1}, {}, {1, 1, 1, 1}}
    };
    MeshDeformationData deformation;
    // All six joints contribute; total authored weight is six, deliberately not one.
    deformation.influenceSets = {
        std::vector<VertexJointInfluences>(3, {{0, 1, 2, 3}, {1, 1, 1, 1}}),
        std::vector<VertexJointInfluences>(3, {{4, 5, 0, 0}, {1, 1, 0, 0}})
    };
    auto mesh = Mesh::create(source, {0, 1, 2, 0, 1, 2}, {{0, 3, material}, {3, 3, material}}, deformation);
    SkinDesc skinDesc;
    Scene scene;
    std::vector<Ptr<Entity>> joints;
    // The six diagonal affine matrices average to diag(-2,3,4), translation(.25,.5,-.25).
    for (uint32_t i = 0; i < 6; ++i)
    {
        skinDesc.joints.push_back({i, math::mat4Identity()});
        auto joint = scene.createEntity(scene.getRoot());
        joint->setLocalScale({-2, 3, 4});
        joint->setLocalPosition({float(i) * .1f, .5f, -.25f});
        joints.push_back(joint);
    }
    auto renderer = scene.createMeshRenderer(scene.getRoot());
    renderer->setMesh(mesh);
    renderer->setSkin(Skin::create(skinDesc), joints);
    DrawList list;
    list.deformations.push_back(snapshotDeformation(*renderer));
    list.items = {{mesh, 0, material, math::mat4Identity(), 0}, {mesh, 1, material, math::mat4Identity(), 0}};
    Graphics::render(list, target);
    auto result = readSkinned(fixture, list.deformations[0]);
    for (size_t i = 0; i < source.size(); ++i)
    {
        const auto& a = source[i];
        const auto& b = result[i];
        CHECK(std::abs(b.position.x - (-2 * a.position.x + .25f)) < 1e-4f);
        CHECK(std::abs(b.position.y - (3 * a.position.y + .5f)) < 1e-4f);
        CHECK(std::abs(b.position.z - (4 * a.position.z - .25f)) < 1e-4f);
        const double nx = -a.normal.x / 2., ny = a.normal.y / 3., nz = a.normal.z / 4.;
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        CHECK(std::abs(b.normal.x - nx / length) < 1e-4);
        CHECK(std::abs(b.normal.y - ny / length) < 1e-4);
        CHECK(std::abs(b.normal.z - nz / length) < 1e-4);
        CHECK(std::memcmp(&a.uv, &b.uv, sizeof(a.uv)) == 0);
        CHECK(std::memcmp(&a.uv1, &b.uv1, sizeof(a.uv1)) == 0);
        CHECK(std::memcmp(&a.color, &b.color, sizeof(a.color)) == 0);
        if (a.tangent.w == 0)
        {
            CHECK(std::memcmp(&a.tangent, &b.tangent, sizeof(a.tangent)) == 0);
        }
        else
        {
            const double tx = -2 * a.tangent.x, ty = 3 * a.tangent.y;
            const double tangentLength = std::hypot(tx, ty);
            CHECK(std::abs(b.tangent.x - tx / tangentLength) < 1e-4);
            CHECK(std::abs(b.tangent.y - ty / tangentLength) < 1e-4);
            CHECK(b.tangent.z == 0);
            CHECK(b.tangent.w == -a.tangent.w);
        }
    }
    // Singular blend emits zero normals for the renderer's geometric-normal path.
    for (auto& matrix : list.deformations[0].palette)
    {
        matrix = math::mat4Scale({0, 3, 4});
    }
    Graphics::render(list, target);
    result = readSkinned(fixture, list.deformations[0]);
    CHECK(result[0].normal.x == 0);
    CHECK(result[0].normal.y == 0);
    CHECK(result[0].normal.z == 0);
    CHECK(std::memcmp(mesh->vertices().data(), source.data(), source.size() * sizeof(Vertex)) == 0);
}

TEST_CASE("Compute skinning keeps instances and ordered frames independent through scene and graphics replacement")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    auto material = flatMaterial({1, 0, 0, 1});
    auto base = square(material);
    MeshDeformationData deformation;
    deformation.influenceSets = {std::vector<VertexJointInfluences>(4, {{0, 0, 0, 0}, {1, 0, 0, 0}})};
    auto mesh = Mesh::create(
        {base->vertices().begin(), base->vertices().end()},
        {base->indices().begin(), base->indices().end()},
        {base->subMeshes().begin(), base->subMeshes().end()},
        deformation
    );
    auto skin = Skin::create({"one", {{0, math::mat4Identity()}}, {}});
    Scene scene;
    auto camera = scene.createCamera(scene.getRoot());
    for (float x : {-.5f, .5f})
    {
        auto entity = scene.createEntity(scene.getRoot());
        entity->setLocalPosition({100, 0, 3}); // Rest AABB must not hide visible animated geometry.
        auto joint = scene.createEntity(scene.getRoot());
        joint->setLocalPosition({x, 0, 3});
        auto renderer = scene.createMeshRenderer(entity);
        renderer->setMesh(mesh);
        renderer->setSkin(skin, {joint});
    }
    auto list = buildDrawList(scene, *camera, 129.f / 97);
    REQUIRE(list.deformations.size() == 2);
    REQUIRE(list.items.size() == 4);
    scene.clear(); // Snapshots contain no entity observers.
    for (int frame = 0; frame < 4; ++frame)
    {
        Graphics::render(list, target);
    }
    CHECK(
        DeformationRenderer::vertices(*list.deformations[0].storage) !=
        DeformationRenderer::vertices(*list.deformations[1].storage)
    );
    auto first = readSkinned(fixture, list.deformations[0]);
    auto second = readSkinned(fixture, list.deformations[1]);
    CHECK(std::abs(first[0].position.x + 101.f) < 1e-4f);
    CHECK(std::abs(second[0].position.x + 100.f) < 1e-4f);
    CHECK(fixture.pixel(target, 50, 48)[0] == 255);
    CHECK(fixture.pixel(target, 78, 48)[0] == 255);
    for (int frame = 0; frame < 4; ++frame)
    {
        list.deformations[0].palette[0][3].x = -100.5f + .1f * frame;
        Graphics::render(list, target); // Same allocation, changing command-ordered palette uploads without waits.
    }
    CHECK(std::abs(readSkinned(fixture, list.deformations[0])[0].position.x + 100.7f) < 1e-4f);
    CHECK(readSkinned(fixture, list.deformations[1])[0].position.x == second[0].position.x);
    list.deformations[0].palette[0][3].x = -100.5f;
    Graphics::shutdown();
    Graphics::initialize(fixture.device, fixture.queue);
    Graphics::render(list, target);
    CHECK(readSkinned(fixture, list.deformations[0])[0].position.x == first[0].position.x);
    CHECK(fixture.pixel(target, 50, 48)[0] == 255);
    std::weak_ptr<DeformationStorage> storage = list.deformations[0].storage;
    Graphics::render(list, target);
    list = {};
    CHECK(storage.expired()); // RHI commands retain allocations, without prolonging the scene/storage owner.
    Graphics::shutdown();
    CHECK(fixture.pixel(target, 50, 48)[0] == 255);
}

TEST_CASE("indexed submeshes use independent uniforms and depth while pipelines follow shader identity and format")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    auto red = flatMaterial({1, 0, 0, 1});
    auto green = flatMaterial({0, 1, 0, 1}, red->shader());
    auto mesh = square(red);
    DrawList list;
    // Draw the near square first: the later far square must not overwrite either triangle.
    for (uint32_t part = 0; part < 2; ++part)
    {
        list.items.push_back({mesh, part, green, math::mat4Translation({-0.25f, 0, 0.2f})});
        list.items.push_back({mesh, part, red, math::mat4Translation({0.25f, 0, 0.7f})});
    }
    Graphics::render(list, target);
    CHECK(fixture.pixel(target, 60, 35) == std::array<uint8_t, 4>{0, 255, 0, 255});
    CHECK(fixture.pixel(target, 60, 60) == std::array<uint8_t, 4>{0, 255, 0, 255});
    CHECK(fixture.pixel(target, 100, 48) == std::array<uint8_t, 4>{255, 0, 0, 255});
    CHECK(Graphics::pipelineCreationCount() == 1);
    green->setUniform("tint", math::Vec4{0, 0, 1, 1});
    list.items[0].material = green->clone();
    Graphics::render(list, target);
    CHECK(fixture.pixel(target, 60, 35) == std::array<uint8_t, 4>{0, 0, 255, 255});
    CHECK(Graphics::pipelineCreationCount() == 1);
    auto bgra = fixture.target(rhi::Format::BGRA8Unorm, 133);
    Graphics::render(list, bgra);
    CHECK(Graphics::pipelineCreationCount() == 2);
    list.items[0].material = flatMaterial({1, 1, 0, 1}); // Same source name, distinct Shader ownership.
    Graphics::render(list, target);
    CHECK(Graphics::pipelineCreationCount() == 3);

    auto presented = fixture.target();
    rhi::ComPtr<rhi::IRenderPipeline> presentation;
    REQUIRE(SLANG_SUCCEEDED(
        createPresentationPipeline(fixture.device, presented->getDesc().format, presentation.writeRef())
    ));
    REQUIRE(SLANG_SUCCEEDED(drawPresentation(fixture.queue, presentation, target, presented)));
    CHECK(fixture.pixel(presented, 60, 35) == fixture.pixel(target, 60, 35));
    CHECK(fixture.pixel(presented, 60, 60) == fixture.pixel(target, 60, 60));
    CHECK(fixture.pixel(presented, 100, 48) == fixture.pixel(target, 100, 48));
}

// Reusing material bindings must preserve each draw's transform and each submission's material values.
TEST_CASE("shared material bindings preserve interleaved draw and submission snapshots")
{
    GraphicsFixture fixture;
    auto first = fixture.target(), second = fixture.target();
    auto red = flatMaterial({1, 0, 0, 1});
    auto green = flatMaterial({0, 1, 0, 1}, red->shader());
    auto mesh = square(red);
    DrawList list;
    for (unsigned instance = 0; instance < 3; ++instance)
    {
        auto transform = math::mat4Translation({-.65f + .65f * instance, 0, .5f});
        transform[0].x = instance == 2 ? -.45f : .45f;
        transform[1].y = .6f;
        for (uint32_t part = 0; part < 2; ++part)
        {
            list.items.push_back({mesh, part, instance == 1 ? green : red, transform});
        }
    }
    Graphics::render(list, first);
    red->setUniform("tint", math::Vec4{0, 0, 1, 1});
    green->setUniform("tint", math::Vec4{1, 1, 0, 1});
    Graphics::render(list, second); // No host wait before editing and submitting again.
    for (uint32_t y : {40u, 56u})
    {
        for (uint32_t x : {22u, 106u})
        {
            CHECK(fixture.pixel(first, x, y) == std::array<uint8_t, 4>{255, 0, 0, 255});
            CHECK(fixture.pixel(second, x, y) == std::array<uint8_t, 4>{0, 0, 255, 255});
        }
        CHECK(fixture.pixel(first, 64, y) == std::array<uint8_t, 4>{0, 255, 0, 255});
        CHECK(fixture.pixel(second, 64, y) == std::array<uint8_t, 4>{255, 255, 0, 255});
        CHECK(fixture.pixel(first, 43, y)[0] < 20); // The gap remains clear between distinct transforms.
    }
}

TEST_CASE("material binding supports all typed values and explicit matrix storage")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    const char* source = R"(
struct DrawParameters { row_major float4x4 clipFromLocal; };
struct MaterialParameters {
 float scalar; int signedValue; uint unsignedValue; float2 pair; float3 triple; float4 color;
 column_major float4x4 transform;
};
ConstantBuffer<DrawParameters> draw;
ConstantBuffer<MaterialParameters> material;
struct Input { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
[shader("vertex")] float4 vertexMain(Input input) : SV_Position
{ return mul(draw.clipFromLocal, mul(material.transform, float4(input.position, 1))); }
[shader("fragment")] float4 fragmentMain() : SV_Target
{ return float4(material.scalar + material.pair.x + material.triple.z,
                float(material.signedValue), float(material.unsignedValue), 1) * material.color; }
)";
    auto material = Material::create(Shader::create("typed.slang", source));
    material->setUniform("scalar", 0.125f);
    material->setUniform("signedValue", int32_t{1});
    material->setUniform("unsignedValue", uint32_t{1});
    material->setUniform("pair", math::Vec2{0.125f, 9});
    material->setUniform("triple", math::Vec3{9, 9, 0.25f});
    material->setUniform("color", math::Vec4{1, 0.25f, 1, 1});
    material->setUniform("transform", math::mat4Translation({0.4f, 0, 0}));
    auto mesh = square(material);
    DrawList list;
    for (uint32_t part = 0; part < 2; ++part)
    {
        list.items.push_back({mesh, part, material, math::mat4Translation({0, 0.1f, 0.5f})});
    }
    Graphics::render(list, target);
    const auto pixel = fixture.pixel(target, 100, 48);
    // UNORM conversion may round half values by one byte; this tolerance is one quantization step.
    CHECK(std::abs(int(pixel[0]) - 128) <= 1);
    CHECK(std::abs(int(pixel[1]) - 64) <= 1);
    CHECK(pixel[2] == 255);
    CHECK(fixture.pixel(target, 30, 48)[2] < 20); // Material translation moved the square to the right.
    material->setUniform("scalar", int32_t{1});
    CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("mismatched"), EngineError);
    material->setUniform("scalar", 0.125f);
    material->setUniform("extra", 1.0f);
    CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("Unknown"), EngineError);
    list.items[0].material = Material::create(material->shader());
    CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("Missing"), EngineError);
}

TEST_CASE("submitted draws survive asset release and surviving CPU assets reprepare after shutdown")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    auto material = flatMaterial({1, 0, 0, 1});
    auto mesh = square(material);
    std::weak_ptr<Mesh> weakMesh = mesh;
    std::weak_ptr<Shader> weakShader = material->shader();
    DrawList list;
    list.items.push_back({mesh, 0, material, math::mat4Translation({0, 0, 0.5f})});
    Graphics::render(list, target);
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    Graphics::shutdown();
    target.setNull();
    fixture.queue.setNull();
    fixture.device.setNull();
    rhi::DeviceDesc desc{};
    desc.deviceType = rhi::DeviceType::D3D12;
    desc.enableValidation = true;
    desc.debugCallback = &fixture.diagnostics;
    REQUIRE(SLANG_SUCCEEDED(rhi::getRHI()->createDevice(desc, fixture.device.writeRef())));
    REQUIRE(SLANG_SUCCEEDED(fixture.device->getQueue(rhi::QueueType::Graphics, fixture.queue.writeRef())));
    Graphics::initialize(fixture.device, fixture.queue);
    target = fixture.target();
    Graphics::render(list, target);
    CHECK(Graphics::pipelineCreationCount() == 1);
    list.items.clear();
    mesh.reset();
    material.reset();
    CHECK(weakMesh.expired());
    CHECK(weakShader.expired());
    CHECK(fixture.pixel(target, 75, 55)[0] == 255);
    Graphics::render({}, target);
    CHECK(Graphics::pipelineCount() == 0);
    CHECK_THROWS_AS(Graphics::render({}, nullptr), EngineError);
}

// Resource whose single cooperative step is observed by a state in the same frame.
class FrameResource : public Resource
{
public:
    // Creates a pending resource compatible with Resources' typed loading API.
    explicit FrameResource(std::string key)
        : Resource(std::move(key))
    {
    }

private:
    // Completes during the Resources phase before State updates.
    bool loadStep() override { return true; }
};

class ReplaceSceneState : public State
{
public:
    std::shared_ptr<FrameResource> resource;
    bool ran{false};

private:
    // Checks frame ordering and replaces the scene that Game will update/render this frame.
    bool onMain() override
    {
        CHECK(resource->isLoaded());
        CHECK(Game::deltaSeconds() == 0.25f);
        auto scene = std::make_unique<Scene>();
        scene->getRoot()->setLocalPosition({7, 0, 0});
        auto child = scene->createEntity(scene->getRoot());
        auto animator = scene->createAnimator(child);
        AnimationTrack
            track{0, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 1}, 3, {0, 0, 0, 4, 0, 0}};
        animator->setBindings({Animation::create({"frame-order", {track}})}, {child});
        animator->selectAnimation(0);
        animator->play();
        Game::setScene(std::move(scene));
        CHECK_THROWS_AS(Game::frame(0, nullptr), EngineError);
        CHECK_THROWS_AS(Game::shutdown(), EngineError);
        ran = true;
        return false;
    }
};

TEST_CASE("Game updates resources then state and replacement scene without requiring a render target")
{
    Game::initialize(std::make_unique<Scene>());
    auto& state = Game::state().emplaceChild<ReplaceSceneState>();
    state.resource = Resources::loadResourceAsync<FrameResource>("frame-order");
    CHECK_FALSE(state.resource->isLoaded());
    Game::frame(0.25f, nullptr);
    CHECK(state.ran);
    CHECK(Game::scene().getRoot()->worldTransform()[3].x == 7);
    CHECK(Game::scene().getRoot()->firstChild()->worldTransform()[3].x == 8);
    CHECK(Game::scene().animators()[0]->timeSeconds() == .25);
    CHECK_THROWS_AS(Game::frame(-1, nullptr), EngineError);
    Game::shutdown();
    CHECK_THROWS_AS(Game::scene(), EngineError);
}

TEST_CASE("Game renders the procedural fixture and clears when no camera is selected")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    auto scene = createSceneFixture();
    CHECK(buildDrawList(*scene, *scene->activeCamera(), 1).items.size() == 12);
    Game::initialize(std::move(scene));
    Game::frame(0, target);
    CHECK(Graphics::pipelineCount() == 1);
    Game::scene().setActiveCamera(nullptr);
    Game::frame(0, target);
    const auto pixel = fixture.pixel(target, 64, 48);
    CHECK(pixel[0] < 10);
    CHECK(pixel[2] < 20);
}

TEST_CASE("cube faces have valid outward geometry and renderer rejects unsupported shader contracts")
{
    auto material = createFixtureMaterial();
    auto cube = createCubeMesh(material);
    REQUIRE(cube->vertices().size() == 24);
    REQUIRE(cube->indices().size() == 36);
    REQUIRE(cube->subMeshes().size() == 6);
    for (const auto& part : cube->subMeshes())
    {
        const auto& a = cube->vertices()[cube->indices()[part.firstIndex]];
        const auto& b = cube->vertices()[cube->indices()[part.firstIndex + 1]];
        const auto& c = cube->vertices()[cube->indices()[part.firstIndex + 2]];
        CHECK(
            math::dot(a.normal, math::cross(math::sub(b.position, a.position), math::sub(c.position, a.position))) > 0
        );
        CHECK(math::dot(a.position, a.normal) == 0.5f);
    }

    GraphicsFixture fixture;
    auto target = fixture.target();
    DrawList list;
    list.items.push_back({cube, 0, material, math::mat4Translation({0, 0, 0.5f})});
    SUBCASE("array uniforms are explicitly unsupported")
    {
        std::string source = flatSource;
        source.replace(source.find("float4 tint;"), std::string("float4 tint;").size(), "float4 tint[1];");
        source.replace(
            source.find("return material.tint;"),
            std::string("return material.tint;").size(),
            "return material.tint[0];"
        );
        auto array = Material::create(Shader::create("array.slang", source));
        array->setUniform("tint", math::Vec4{1, 1, 1, 1});
        list.items[0].material = array;
        CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("Unsupported"), EngineError);
    }
    SUBCASE("shader diagnostics retain source failures")
    {
        list.items[0].material = Material::create(Shader::create("invalid.slang", "this is not a shader"));
        CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("invalid.slang"), EngineError);
    }
    SUBCASE("invalid draw ranges are rejected before indexed access")
    {
        list.items[0].subMeshIndex = 6;
        CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("valid submesh"), EngineError);
    }
}

// Reads a texture subresource after submission and retains its padded layout for numeric checks.
static rhi::ComPtr<ISlangBlob> readMip(
    GraphicsFixture& fixture,
    rhi::ITexture* texture,
    uint32_t mip,
    rhi::SubresourceLayout& layout
)
{
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    rhi::ComPtr<ISlangBlob> pixels;
    REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(texture, 0, mip, pixels.writeRef(), &layout)));
    return pixels;
}

TEST_CASE("all sampled formats upload padded rows and GPU mipmaps preserve constants outside normalized range")
{
    GraphicsFixture fixture;
    TextureRenderer renderer(fixture.device, fixture.queue);
    const TextureFormat formats[]{
        TextureFormat::R8Unorm,
        TextureFormat::RG8Unorm,
        TextureFormat::RGBA8Unorm,
        TextureFormat::RGBA8UnormSrgb,
        TextureFormat::R16Float,
        TextureFormat::RG16Float,
        TextureFormat::RGBA16Float,
        TextureFormat::R32Float,
        TextureFormat::RG32Float,
        TextureFormat::RGBA32Float
    };
    for (auto format : formats)
    {
        CAPTURE(int(format));
        const size_t pixelSize = texturePixelSize(format);
        const bool half = format >= TextureFormat::R16Float && format <= TextureFormat::RGBA16Float;
        const bool full = format >= TextureFormat::R32Float;
        const size_t componentSize = full ? 4 : half ? 2 : 1;
        const size_t pitch = 5 * pixelSize + 16;
        std::vector<std::byte> bytes(pitch * 3);
        for (size_t y = 0; y < 3; ++y)
        {
            for (size_t x = 0; x < 5; ++x)
            {
                for (size_t c = 0; c < pixelSize / componentSize; ++c)
                {
                    auto dst = bytes.data() + y * pitch + x * pixelSize + c * componentSize;
                    if (full)
                    {
                        float v = c % 2 ? -2.f : 2.f;
                        std::memcpy(dst, &v, 4);
                    }
                    else if (half)
                    {
                        uint16_t v = c % 2 ? 0xc000 : 0x4000;
                        std::memcpy(dst, &v, 2);
                    }
                    else
                    {
                        *dst = std::byte{255};
                    }
                }
            }
        }
        auto image = Texture::create({5, 3, format, TextureMipmaps::Generate}, {bytes, pitch});
        auto view = renderer.prepare(image->defaultView());
        CHECK(renderer.prepare(image->defaultView()) == view);
        for (uint32_t mip = 0; mip < image->mipCount(); ++mip)
        {
            rhi::SubresourceLayout layout;
            auto pixels = readMip(fixture, view->getTexture(), mip, layout);
            const auto* data = static_cast<const std::byte*>(pixels->getBufferPointer());
            for (uint32_t y = 0; y < std::max(1u, 3u >> mip); ++y)
            {
                for (uint32_t x = 0; x < std::max(1u, 5u >> mip); ++x)
                {
                    for (size_t c = 0; c < pixelSize / componentSize; ++c)
                    {
                        const auto src = data + y * layout.rowPitch + x * pixelSize + c * componentSize;
                        if (full)
                        {
                            float v;
                            std::memcpy(&v, src, 4);
                            CHECK(v == (c % 2 ? -2.f : 2.f));
                        }
                        else if (half)
                        {
                            uint16_t v;
                            std::memcpy(&v, src, 2);
                            CHECK(v == (c % 2 ? 0xc000 : 0x4000));
                        }
                        else
                        {
                            CHECK(*src == std::byte{255});
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("GPU area reduction includes odd borders and one-dimensional tails")
{
    GraphicsFixture fixture;
    TextureRenderer renderer(fixture.device, fixture.queue);
    for (bool half : {false, true})
    {
        for (auto dimensions : {std::pair(5u, 3u), std::pair(1u, 7u), std::pair(7u, 1u)})
        {
            auto [width, height] = dimensions;
            std::vector<float> expected(width * height);
            for (uint32_t i = 0; i < expected.size(); ++i)
            {
                expected[i] = float(i) / 16.f;
            }
            std::vector<uint16_t> halfPixels(expected.size());
            for (size_t i = 0; i < expected.size(); ++i)
            {
                // These nonnegative multiples of 1/16 are exactly representable as binary16.
                int exponent = 0;
                const float fraction = std::frexp(expected[i], &exponent);
                halfPixels[i] =
                    expected[i] == 0 ? 0 : uint16_t(((exponent + 14) << 10) | int((fraction * 2 - 1) * 1024));
            }
            auto image = Texture::create(
                {width, height, half ? TextureFormat::R16Float : TextureFormat::R32Float, TextureMipmaps::Generate},
                {half ? std::as_bytes(std::span(halfPixels)) : std::as_bytes(std::span(expected))}
            );
            auto view = renderer.prepare(image->defaultView());
            for (uint32_t mip = 1; mip < image->mipCount(); ++mip)
            {
                const auto nextWidth = std::max(1u, width / 2), nextHeight = std::max(1u, height / 2);
                std::vector<float> next(nextWidth * nextHeight);
                // Independent CPU reference integrates overlaps of source/destination pixel rectangles.
                for (uint32_t y = 0; y < nextHeight; ++y)
                {
                    for (uint32_t x = 0; x < nextWidth; ++x)
                    {
                        double total = 0, sum = 0;
                        for (uint32_t sy = 0; sy < height; ++sy)
                        {
                            for (uint32_t sx = 0; sx < width; ++sx)
                            {
                                double wx = std::max(
                                    0.,
                                    std::min(double(sx + 1), double(x + 1) * width / nextWidth) -
                                        std::max(double(sx), double(x) * width / nextWidth)
                                );
                                double wy = std::max(
                                    0.,
                                    std::min(double(sy + 1), double(y + 1) * height / nextHeight) -
                                        std::max(double(sy), double(y) * height / nextHeight)
                                );
                                sum += expected[sy * width + sx] * wx * wy;
                                total += wx * wy;
                            }
                        }
                        next[y * nextWidth + x] = float(sum / total);
                    }
                }
                rhi::SubresourceLayout layout;
                auto pixels = readMip(fixture, view->getTexture(), mip, layout);
                for (uint32_t y = 0; y < nextHeight; ++y)
                {
                    for (uint32_t x = 0; x < nextWidth; ++x)
                    {
                        float actual;
                        auto address = static_cast<const std::byte*>(pixels->getBufferPointer()) + y * layout.rowPitch +
                                       x * (half ? 2 : 4);
                        if (half)
                        {
                            uint16_t bits;
                            std::memcpy(&bits, address, 2);
                            actual =
                                bits == 0 ? 0.f : std::ldexp(1.f + float(bits & 1023) / 1024, int(bits >> 10) - 15);
                        }
                        else
                        {
                            std::memcpy(&actual, address, 4);
                        }
                        CHECK(actual == doctest::Approx(next[y * nextWidth + x]).scale(1).epsilon(half ? 1e-3 : 5e-6));
                    }
                }
                expected = std::move(next);
                width = nextWidth;
                height = nextHeight;
            }
        }
    }
}

TEST_CASE("GPU sRGB mip reduction uses linear light and straight alpha weighting")
{
    GraphicsFixture fixture;
    TextureRenderer renderer(fixture.device, fixture.queue);
    std::array<uint8_t, 8> source{0, 0, 0, 255, 255, 255, 255, 255};
    SUBCASE("linear light average") {}
    SUBCASE("transparent red does not contaminate opaque blue")
    {
        source = {255, 0, 0, 0, 0, 0, 255, 255};
    }
    auto texture = Texture::create(
        {2, 1, TextureFormat::RGBA8UnormSrgb, TextureMipmaps::Generate},
        {std::as_bytes(std::span(source))}
    );
    auto view = renderer.prepare(texture->createView({1, 1}));
    rhi::SubresourceLayout layout;
    auto result = readMip(fixture, view->getTexture(), 1, layout);
    auto p = static_cast<const uint8_t*>(result->getBufferPointer());
    if (source[3] == 0)
    {
        CHECK(p[0] <= 2);
        CHECK(p[2] >= 253);
        CHECK(std::abs(int(p[3]) - 128) <= 2);
    }
    else
    {
        for (int c = 0; c < 3; ++c)
        {
            CHECK(std::abs(int(p[c]) - 188) <= 2);
        }
    }
}

static const char* sampledSource = R"(
struct DrawParameters { column_major float4x4 clipFromLocal; };
struct MaterialParameters { float2 uv; float lod; };
ConstantBuffer<DrawParameters> draw;
ConstantBuffer<MaterialParameters> material;
Texture2D<float4> image;
SamplerState imageSampler;
struct Input { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
[shader("vertex")] float4 vertexMain(Input input) : SV_Position { return mul(draw.clipFromLocal, float4(input.position, 1)); }
[shader("fragment")] float4 fragmentMain() : SV_Target { return image.SampleLevel(imageSampler, material.uv, material.lod); }
)";

TEST_CASE("material sampling honors views filters addressing clone isolation and binding failures")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    std::array<uint8_t, 8> source{255, 0, 0, 255, 0, 0, 255, 255};
    auto texture = Texture::create(
        {2, 1, TextureFormat::RGBA8Unorm, TextureMipmaps::Generate},
        {std::as_bytes(std::span(source))}
    );
    auto material = Material::create(Shader::create("sampling", sampledSource));
    material->setUniform("uv", math::Vec2{0.25f, 0.5f});
    material->setUniform("lod", 0.f);
    material->setTexture("image", texture);
    material->setSampler(
        "imageSampler",
        Sampler::create({TextureFilter::Nearest, TextureFilter::Nearest, TextureFilter::Nearest})
    );
    auto mesh = square(material);
    DrawList list;
    list.items.push_back({mesh, 0, material, math::mat4Translation({0, 0, 0.5f})});
    list.items.push_back({mesh, 1, material, math::mat4Translation({0, 0, 0.5f})});
    Graphics::render(list, target);
    CHECK(fixture.pixel(target, 64, 48) == std::array<uint8_t, 4>{255, 0, 0, 255});
    material->setUniform("uv", math::Vec2{1.25f, 0.5f});
    Graphics::render(list, target);
    CHECK(fixture.pixel(target, 64, 48)[0] == 255);
    material->setSampler(
        "imageSampler",
        Sampler::create(
            {TextureFilter::Nearest, TextureFilter::Nearest, TextureFilter::Nearest, TextureAddressMode::ClampToEdge}
        )
    );
    Graphics::render(list, target);
    CHECK(fixture.pixel(target, 64, 48)[2] == 255);
    material->setUniform("uv", math::Vec2{0.5f, 0.5f});
    material->setSampler("imageSampler", Sampler::create());
    Graphics::render(list, target);
    CHECK(std::abs(int(fixture.pixel(target, 64, 48)[0]) - 128) <= 2);
    material->setUniform("uv", math::Vec2{0.25f, 0.5f});
    material->setUniform("lod", 0.25f);
    Graphics::render(list, target);
    CHECK(std::abs(int(fixture.pixel(target, 64, 48)[0]) - 223) <= 2);
    CHECK(std::abs(int(fixture.pixel(target, 64, 48)[2]) - 32) <= 2);
    material->setSampler(
        "imageSampler",
        Sampler::create({TextureFilter::Linear, TextureFilter::Linear, TextureFilter::Nearest})
    );
    Graphics::render(list, target);
    CHECK(fixture.pixel(target, 64, 48)[0] == 255);
    CHECK(fixture.pixel(target, 64, 48)[2] == 0);
    material->setUniform("uv", math::Vec2{0.5f, 0.5f});
    material->setUniform("lod", 0.f);
    material->setSampler("imageSampler", Sampler::create());
    auto clone = material->clone();
    clone->setTexture("image", texture->createView({1, 1}));
    list.items[0].material = clone;
    list.items[1].material = clone;
    Graphics::render(list, target);
    CHECK(std::abs(int(fixture.pixel(target, 64, 48)[2]) - 128) <= 2);
    clone->setTexture("unknown", texture);
    CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("Texture2D"), EngineError);
    list.items[0].material = material;
    list.items[1].material = material;
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    Graphics::shutdown();
    Graphics::initialize(fixture.device, fixture.queue);
    Graphics::render(list, target);
    CHECK(std::abs(int(fixture.pixel(target, 64, 48)[0]) - 128) <= 2);
    // Drop every CPU owner before completion; submitted commands must retain views, textures and samplers.
    Graphics::render(list, target);
    list.items.clear();
    mesh.reset();
    material.reset();
    clone.reset();
    texture.reset();
    CHECK(std::abs(int(fixture.pixel(target, 64, 48)[2]) - 128) <= 2);
}

TEST_CASE("sampled image orientation multiple bindings and missing resource contracts")
{
    GraphicsFixture fixture;
    auto target = fixture.target();
    const std::array<uint8_t, 16> colors{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    auto texture = Texture::create({2, 2, TextureFormat::RGBA8Unorm}, {std::as_bytes(std::span(colors))});
    auto material = Material::create(Shader::create("orientation", sampledSource));
    material->setUniform("uv", math::Vec2{0.25f, 0.25f});
    material->setUniform("lod", 0.f);
    material->setTexture("image", texture);
    material->setSampler("imageSampler", Sampler::create({TextureFilter::Nearest, TextureFilter::Nearest}));
    auto mesh = square(material);
    DrawList list;
    list.items.push_back({mesh, 0, material, math::mat4Translation({0, 0, 0.5f})});
    list.items.push_back({mesh, 1, material, math::mat4Translation({0, 0, 0.5f})});
    for (uint32_t y = 0; y < 2; ++y)
    {
        for (uint32_t x = 0; x < 2; ++x)
        {
            material->setUniform("uv", math::Vec2{x * 0.5f + 0.25f, y * 0.5f + 0.25f});
            Graphics::render(list, target);
            const auto actual = fixture.pixel(target, 64, 48);
            for (size_t c = 0; c < 4; ++c)
            {
                CHECK(actual[c] == colors[(y * 2 + x) * 4 + c]);
            }
        }
    }
    std::string source = sampledSource;
    source.insert(source.find("SamplerState imageSampler;"), "Texture2D<float4> secondImage;\n");
    source.replace(
        source.find("return image.SampleLevel(imageSampler, material.uv, material.lod);"),
        std::string("return image.SampleLevel(imageSampler, material.uv, material.lod);").size(),
        "return 0.5 * (image.SampleLevel(imageSampler, material.uv, material.lod) + secondImage.Load(int3(1,0,0)));"
    );
    auto two = Material::create(Shader::create("two-images", source));
    two->setUniform("uv", math::Vec2{0.25f, 0.25f});
    two->setUniform("lod", 0.f);
    two->setTexture("image", texture);
    two->setSampler("imageSampler", Sampler::create());
    list.items[0].material = list.items[1].material = two;
    CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("secondImage"), EngineError);
    two->setTexture("secondImage", texture);
    Graphics::render(list, target);
    auto actual = fixture.pixel(target, 64, 48);
    CHECK(std::abs(int(actual[0]) - 128) <= 2);
    CHECK(std::abs(int(actual[1]) - 128) <= 2);
    CHECK(actual[2] == 0);
    auto pending = Resources::loadResourceAsync<Texture>("pending-texture.png");
    two->setTexture("secondImage", pending);
    CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("not ready"), EngineError);
    auto wrong = Material::create(Shader::create("wrong-resource", sampledSource));
    wrong->setUniform("uv", math::Vec2{});
    wrong->setUniform("lod", 0.f);
    wrong->setSampler("image", Sampler::create());
    list.items[0].material = list.items[1].material = wrong;
    CHECK_THROWS_WITH_AS(Graphics::render(list, target), doctest::Contains("SamplerState"), EngineError);
}

TEST_CASE("PBR sphere grid compiles surface shaders and presents the HDR environment pipeline")
{
    GraphicsFixture fixture;
    auto scene = createPbrFixture();
    scene->update();
    auto list = buildDrawList(*scene, *scene->activeCamera(), 960.0f / 640);
    REQUIRE(list.items.size() == 63);
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 960, 640);
    auto start = std::chrono::steady_clock::now();
    Graphics::render(list, target);
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    double cold = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    auto count = Graphics::pipelineCreationCount();
    start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 10; ++frame)
    {
        Graphics::render(list, target);
        REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    }
    double warm = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 10;
    CHECK(Graphics::pipelineCreationCount() == count);
    std::printf(
        "PBR benchmark: %s, D3D12 debug validation, 960x640, 63 spheres, 2 lights, %zu pipelines, first %.2f ms, warm "
        "submit+wait %.2f ms.\n",
        fixture.device->getInfo().adapterName,
        count,
        cold,
        warm
    );
    // Durable offscreen capture has an exact viewport, independent of OS window chrome/DPI.
    rhi::ComPtr<ISlangBlob> pixels;
    rhi::SubresourceLayout layout{};
    REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
    auto folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/pbr/native";
    std::filesystem::create_directories(folder);
    std::ofstream output(folder / "offscreen.ppm", std::ios::binary);
    output << "P6\n960 640\n255\n";
    for (size_t y = 0; y < 640; ++y)
    {
        for (size_t x = 0; x < 960; ++x)
        {
            output.write(static_cast<const char*>(pixels->getBufferPointer()) + y * layout.rowPitch + x * 4, 3);
        }
    }
    REQUIRE(bool(output));
}

// A front-facing plane with analytic normal and identity UV coordinates for lighting/readback tests.
static std::shared_ptr<Mesh> pbrPlane(const std::shared_ptr<Material>& material)
{
    return Mesh::create(
        {{{-0.8f, -0.8f, 0}, {0, 0, -1}, {0, 1}},
         {{0.8f, -0.8f, 0}, {0, 0, -1}, {1, 1}},
         {{0.8f, 0.8f, 0}, {0, 0, -1}, {1, 0}},
         {{-0.8f, 0.8f, 0}, {0, 0, -1}, {0, 0}}},
        {0, 2, 1, 0, 3, 2},
        {{0, 6, material}}
    );
}
// Reads actual scene-linear float output; no tone mapping/transfer is used on this diagnostic target.
static std::array<float, 4> floatPixel(
    GraphicsFixture& fixture,
    rhi::ITexture* target,
    uint32_t x = 64,
    uint32_t y = 48
)
{
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    rhi::ComPtr<ISlangBlob> pixels;
    rhi::SubresourceLayout layout{};
    REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
    std::array<float, 4> result;
    std::memcpy(
        result.data(),
        static_cast<const uint8_t*>(pixels->getBufferPointer()) + y * layout.rowPitch + x * 16,
        16
    );
    return result;
}

TEST_CASE("Shadow parameter blocks preserve cascade instance and edited material snapshots")
{
    GraphicsFixture fixture;
    const rhi::VertexStreamDesc stream{sizeof(Vertex), rhi::InputSlotClass::PerVertex, 0};
    rhi::InputElementDesc elements[]{
        {"POSITION", 0, rhi::Format::RGB32Float, offsetof(Vertex, position), 0},
        {"NORMAL", 1, rhi::Format::RGB32Float, offsetof(Vertex, normal), 0},
        {"TEXCOORD", 2, rhi::Format::RG32Float, offsetof(Vertex, uv), 0},
        {"TANGENT", 3, rhi::Format::RGBA32Float, offsetof(Vertex, tangent), 0},
        {"TEXCOORD", 4, rhi::Format::RG32Float, offsetof(Vertex, uv1), 0},
        {"COLOR", 5, rhi::Format::RGBA32Float, offsetof(Vertex, color), 0}
    };
    rhi::InputLayoutDesc layoutDesc{};
    layoutDesc.inputElements = elements;
    layoutDesc.inputElementCount = 6;
    layoutDesc.vertexStreams = &stream;
    layoutDesc.vertexStreamCount = 1;
    rhi::ComPtr<rhi::IInputLayout> layout;
    REQUIRE(SLANG_SUCCEEDED(fixture.device->createInputLayout(layoutDesc, layout.writeRef())));
    PbrMaterialDesc desc;
    desc.doubleSided = true;
    auto opaque = createPbrMaterial(desc);
    auto clone = opaque->clone();
    auto cloneState = clone->renderState();
    cloneState.doubleSided = false;
    clone->setRenderState(cloneState);
    std::array<std::byte, 4> clear{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{0}};
    auto transparent = Texture::create({1, 1, TextureFormat::RGBA8Unorm}, {clear});
    clear[3] = std::byte{255};
    auto solid = Texture::create({1, 1, TextureFormat::RGBA8Unorm}, {clear});
    desc.alphaMode = AlphaMode::Mask;
    desc.textures[0].texture = transparent;
    auto masked = createPbrMaterial(desc);
    auto mesh = pbrPlane(opaque);
    rhi::ComPtr<rhi::IBuffer> vertices, indices;
    rhi::BufferDesc buffer{};
    buffer.size = mesh->vertices().size_bytes();
    buffer.usage = rhi::BufferUsage::VertexBuffer;
    REQUIRE(SLANG_SUCCEEDED(fixture.device->createBuffer(buffer, mesh->vertices().data(), vertices.writeRef())));
    buffer.size = mesh->indices().size_bytes();
    buffer.usage = rhi::BufferUsage::IndexBuffer;
    REQUIRE(SLANG_SUCCEEDED(fixture.device->createBuffer(buffer, mesh->indices().data(), indices.writeRef())));
    TextureRenderer textures(fixture.device, fixture.queue);
    auto shadows = std::make_unique<ShadowRenderer>(fixture.device, fixture.queue, layout);
    DrawList frame;
    frame.camera.nearDistance = 1;
    frame.camera.farDistance = 16;
    frame.lighting.outdoor.emplace();
    frame.lighting.outdoor->shadows.resolution = 128;
    frame.lighting.outdoor->shadows.distance = 16;
    frame.lighting.outdoor->shadows.splitLambda = 0;
    frame.outdoor.emplace();
    frame.outdoor->lightDirection = {0, 0, -1};
    std::array materials{opaque, masked, clone};
    std::array<DrawItem, 3> items;
    std::array<ShadowDraw, 3> draws;
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto world =
            math::mul(math::mat4Translation({float(i) * 2 - 2, 0, 3}), math::mat4Scale({i == 2 ? -.7f : .7f, .7f, 1}));
        items[i] = {mesh, 0, materials[i], world};
        draws[i] = {&items[i], vertices, indices};
    }
    // Prepare the selected resources as Graphics does, without any visible-pass material preparation.
    const auto prepareAlpha = [&]()
    {
        draws[1].alphaTexture =
            textures.prepare(bindingTexture(masked->textures().at("baseColorTexture"))->defaultView());
        draws[1].alphaSampler = textures.prepare(*masked->samplers().at("baseColorSampler"));
    };
    // Preserve each submitted depth array before the next frame overwrites the renderer's attachment.
    const auto capture = [&]()
    {
        prepareAlpha();
        shadows->render(frame, draws);
        auto desc = shadows->texture()->getDesc();
        desc.usage = rhi::TextureUsage::CopyDestination | rhi::TextureUsage::CopySource;
        desc.defaultState = rhi::ResourceState::CopyDestination;
        rhi::ComPtr<rhi::ITexture> copy;
        REQUIRE(SLANG_SUCCEEDED(fixture.device->createTexture(desc, nullptr, copy.writeRef())));
        auto encoder = fixture.queue->createCommandEncoder();
        encoder->copyTexture(copy, {}, {}, shadows->texture(), {}, {}, rhi::Extent3D::kWholeTexture);
        auto commands = encoder->finish();
        REQUIRE(SLANG_SUCCEEDED(fixture.queue->submit(commands)));
        return copy;
    };
    auto first = capture();
    auto cascades = shadows->cascades();
    auto state = opaque->renderState();
    state.alphaMode = AlphaMode::Mask;
    opaque->setRenderState(state);
    opaque->setUniform("baseColor", math::Vec4{1, 1, 1, .1f});
    masked->setTexture("baseColorTexture", solid);
    auto second = capture(); // No host wait before editing materials and encoding all four new passes.
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    // Checks the projected interior of each caster; untouched depth is exactly 1, caster depth is well below .99.
    const auto checkCoverage = [&](rhi::ITexture* image, std::array<bool, 3> visible)
    {
        for (uint32_t layer = 0; layer < 4; ++layer)
        {
            rhi::ComPtr<ISlangBlob> pixels;
            rhi::SubresourceLayout storage{};
            REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(image, layer, 0, pixels.writeRef(), &storage)));
            for (uint32_t i = 0; i < 3; ++i)
            {
                auto p = math::transformPoint(cascades[layer].clipFromWorld, {float(i) * 2 - 2, 0, 3});
                uint32_t x = uint32_t((p.x * .5f + .5f) * 128), y = uint32_t((.5f - p.y * .5f) * 128);
                REQUIRE(x < 128);
                REQUIRE(y < 128);
                float depth;
                std::memcpy(
                    &depth,
                    static_cast<const std::byte*>(pixels->getBufferPointer()) + y * storage.rowPitch + x * 4,
                    4
                );
                INFO("cascade ", layer, " instance ", i, " depth ", depth);
                CHECK((depth < .99f) == visible[i]);
            }
        }
    };
    checkCoverage(first, {true, false, true});
    checkCoverage(second, {false, true, true});
    // Sample opposite halves of the same alpha image by editing only cached UV properties.
    std::array<std::byte, 8> splitAlpha{
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{0},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255}
    };
    masked->setTexture("baseColorTexture", Texture::create({2, 1, TextureFormat::RGBA8Unorm}, {splitAlpha}));
    masked->setUniform("baseColorTransformX", math::Vec3{0, 0, .25f});
    masked->setUniform("baseColorTransformY", math::Vec3{0, 0, .5f});
    auto uvLeft = capture();
    masked->setUniform("baseColorTransformX", math::Vec3{0, 0, .75f});
    auto uvRight = capture();
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    checkCoverage(uvLeft, {false, false, true});
    checkCoverage(uvRight, {false, true, true});
    shadows.reset();
    shadows = std::make_unique<ShadowRenderer>(fixture.device, fixture.queue, layout);
    auto restarted = capture();
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    checkCoverage(restarted, {false, true, true});
}

TEST_CASE("PBR parameter blocks preserve variants edits and queued scene snapshots")
{
    GraphicsFixture fixture;
    auto first = fixture.target(rhi::Format::RGBA32Float);
    auto second = fixture.target(rhi::Format::RGBA32Float);
    auto third = fixture.target(rhi::Format::RGBA32Float);
    PbrMaterialDesc desc;
    desc.baseColor = {.5f, 0, 0, 1};
    desc.metallic = 0;
    desc.specular = 0;
    auto red = createPbrMaterial(desc);
    auto clone = red->clone();
    std::array<float, 4> greenPixel{0, .25f, 0, 1}, bluePixel{0, 0, .5f, 1};
    auto greenTexture = Texture::create({1, 1, TextureFormat::RGBA32Float}, {std::as_bytes(std::span(greenPixel))});
    auto blueTexture = Texture::create({1, 1, TextureFormat::RGBA32Float}, {std::as_bytes(std::span(bluePixel))});
    desc.baseColor = {1, 1, 1, 1};
    desc.textures[static_cast<size_t>(PbrSlot::BaseColor)].texture = greenTexture;
    auto textured = createPbrMaterial(desc);
    REQUIRE(red->shader() != textured->shader());
    auto mesh = pbrPlane(red);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.linearOutput = true;
    list.lighting.environment = createStudioEnvironment();
    list.lighting.environmentIntensity = 0;
    list.lighting.lightCount = 1;
    list.lighting.lights[0].direction = {0, 0, 1};
    list.lighting.lights[0].intensity = 3.141592654f;
    std::array materials{red, textured, clone};
    for (uint32_t i = 0; i < materials.size(); ++i)
    {
        auto world = math::mul(math::mat4Translation({-.65f + .65f * i, 0, .5f}), math::mat4Scale({.35f, .8f, 1}));
        list.items.push_back({mesh, 0, materials[i], world});
    }
    Graphics::render(list, first);
    red->setUniform("baseColor", math::Vec4{.25f, 0, 0, 1});
    textured->setTexture("baseColorTexture", blueTexture);
    textured->setSampler("baseColorSampler", Sampler::create({}));
    list.lighting.lights[0].intensity *= .5f;
    Graphics::render(list, second); // Edits, changed globals and independent variants, with no host wait.
    auto state = clone->renderState();
    state.alphaMode = AlphaMode::Mask;
    clone->setRenderState(state);
    clone->setUniform("baseColor", math::Vec4{.5f, 0, 0, .1f});
    Graphics::render(list, third);
    // Half-float intermediate targets allow 0.2% relative error in these analytic diffuse values.
    CHECK(floatPixel(fixture, first, 22)[0] == doctest::Approx(.5).epsilon(.002));
    CHECK(floatPixel(fixture, first, 64)[1] == doctest::Approx(.25).epsilon(.002));
    CHECK(floatPixel(fixture, first, 106)[0] == doctest::Approx(.5).epsilon(.002));
    CHECK(floatPixel(fixture, second, 22)[0] == doctest::Approx(.125).epsilon(.002));
    CHECK(floatPixel(fixture, second, 64)[2] == doctest::Approx(.25).epsilon(.002));
    CHECK(floatPixel(fixture, second, 64)[1] == 0);
    CHECK(floatPixel(fixture, second, 106)[0] == doctest::Approx(.25).epsilon(.002));
    CHECK(floatPixel(fixture, third, 22)[0] == doctest::Approx(.125).epsilon(.002));
    CHECK(floatPixel(fixture, third, 106)[0] < .03f); // Masked clone reveals the clear color.
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    Graphics::shutdown();
    Graphics::initialize(fixture.device, fixture.queue);
    Graphics::render(list, third); // Surviving PBR materials rebuild against the new Graphics lifecycle.
    CHECK(floatPixel(fixture, third, 22)[0] == doctest::Approx(.125).epsilon(.002));
    CHECK(floatPixel(fixture, third, 64)[2] == doctest::Approx(.25).epsilon(.002));
}

TEST_CASE("PBR direct light has analytic dielectric and metal limits with HDR emission and unlit bypass")
{
    GraphicsFixture fixture;
    auto target = fixture.target(rhi::Format::RGBA32Float);
    PbrMaterialDesc desc;
    desc.baseColor = {0.5f, 0.5f, 0.5f, 1};
    desc.metallic = 0;
    desc.roughness = 1;
    auto material = createPbrMaterial(desc);
    auto mesh = pbrPlane(material);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.linearOutput = true;
    list.lighting.environment = createStudioEnvironment();
    list.lighting.environmentIntensity = 0;
    list.lighting.lightCount = 1;
    list.lighting.lights[0].direction = {0, 0, 1};
    list.lighting.lights[0].intensity = 3.141592654f;
    list.items.push_back({mesh, 0, material, math::mat4Translation({0, 0, 0.5f})});
    Graphics::render(list, target);
    auto pixel = floatPixel(fixture, target);
    // At N=V=L, roughness=1: diffuse .5*.96 plus GGX .04/4, with radiance pi.
    CHECK(pixel[0] == doctest::Approx(0.49).epsilon(0.002));
    material->setUniform("metallic", 1.f);
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == doctest::Approx(0.125).epsilon(0.002));
    material->setUniform("emissive", math::Vec3{4, 2, 1});
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == doctest::Approx(4.125).epsilon(0.002));
    // Environment/direct lights and exposure do not alter unlit material RGB.
    desc.unlit = true;
    desc.baseColor = {0.25f, 0.5f, 0.75f, 1};
    list.items[0].material = createPbrMaterial(desc);
    list.lighting.exposure = 3;
    list.lighting.linearOutput = false;
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == 0.25f);
    CHECK(pixel[1] == 0.5f);
    CHECK(pixel[2] == 0.75f);
    auto encoded = fixture.target();
    Graphics::render(list, encoded);
    CHECK(std::abs(int(fixture.pixel(encoded, 64, 48)[0]) - 137) <= 1);
    // Two-sided shading and determinant-corrected culling preserve the same visible surface after mirroring.
    list.items[0].worldFromLocal = math::mul(math::mat4Translation({0, 0, 0.5f}), math::mat4Scale({-1, 2, 1}));
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == 0.25f);
}

TEST_CASE("PBR alpha mask and mixed lit unlit transparency compose in display-linear space")
{
    GraphicsFixture fixture;
    auto target = fixture.target(rhi::Format::RGBA32Float);
    PbrMaterialDesc lit;
    lit.baseColor = {0, 0, 0, 1};
    lit.metallic = 0;
    lit.specular = 0;
    lit.emissive = {0, 0, 0.5f};
    auto background = createPbrMaterial(lit);
    auto mesh = pbrPlane(background);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.linearOutput = true;
    list.lighting.environment = createStudioEnvironment();
    list.lighting.environmentIntensity = 0;
    PbrMaterialDesc front;
    front.unlit = true;
    front.baseColor = {1, 0, 0, 0.5f};
    front.alphaMode = AlphaMode::Blend;
    auto overlay = createPbrMaterial(front);
    list.items = {
        {mesh, 0, overlay, math::mat4Translation({0, 0, 0.2f})},
        {mesh, 0, background, math::mat4Translation({0, 0, 0.7f})}
    };
    Graphics::render(list, target);
    auto pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == 0.5f);
    CHECK(pixel[1] == 0);
    CHECK(pixel[2] == 0.25f);
    front.alphaMode = AlphaMode::Mask;
    front.alphaCutoff = 0.5f;
    list.items[0].material = createPbrMaterial(front);
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == 1);
    list.items[0].material->setUniform("baseColor", math::Vec4{1, 0, 0, 0.49f});
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == 0);
    CHECK(pixel[2] == 0.5f);
}

TEST_CASE("PBR surface layers preserve neutral settings and analytic normal-incidence limits")
{
    GraphicsFixture fixture;
    auto target = fixture.target(rhi::Format::RGBA32Float);
    PbrMaterialDesc desc;
    desc.baseColor = {0.5f, 0.5f, 0.5f, 1};
    desc.metallic = 0;
    desc.roughness = 1;
    auto material = createPbrMaterial(desc);
    auto mesh = pbrPlane(material);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.linearOutput = true;
    list.lighting.environment = createStudioEnvironment();
    list.lighting.environmentIntensity = 0;
    list.lighting.lightCount = 1;
    list.lighting.lights[0].direction = {0, 0, 1};
    list.lighting.lights[0].intensity = 3.141592654f;
    list.items = {{mesh, 0, material, math::mat4Translation({0, 0, 0.5f})}};
    Graphics::render(list, target);
    const auto baseline = floatPixel(fixture, target);

    // Auxiliary layer parameters must have no effect while their strengths remain zero.
    material->setUniform("clearcoatRoughness", 0.7f);
    material->setUniform("sheenRoughness", 0.8f);
    material->setUniform("iridescenceIor", 2.f);
    material->setUniform("thicknessMinimum", 400.f);
    material->setUniform("thicknessMaximum", 400.f);
    material->setUniform("anisotropyRotation", 1.2f);
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target) == baseline);

    // Zero dielectric reflectance leaves Lambertian .5 under irradiance pi.
    material->setUniform("specular", 0.f);
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == doctest::Approx(0.5).epsilon(0.002));
    material->setUniform("specular", 1.f);
    material->setUniform("ior", 1.f);
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == doctest::Approx(0.5).epsilon(0.002));
    material->setUniform("ior", 1.5f);

    // A roughness-one coat has radiance .25 and Fresnel .04: .49*.96 + .25*.04.
    material->setUniform("clearcoat", 1.f);
    material->setUniform("clearcoatRoughness", 1.f);
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == doctest::Approx(0.4804).epsilon(0.002));
    material->setUniform("clearcoat", 0.f);

    // A zero-thickness film is neutral even at full interference strength.
    material->setUniform("iridescence", 1.f);
    material->setUniform("thicknessMinimum", 0.f);
    material->setUniform("thicknessMaximum", 0.f);
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target) == baseline);
    material->setUniform("thicknessMaximum", 400.f);
    Graphics::render(list, target);
    auto film = floatPixel(fixture, target);
    CHECK(std::abs(film[0] - film[2]) > 0.001f);

    // Layer combinations must stay finite at grazing view and the authored zero-roughness boundary.
    material->setUniform("roughness", 0.f);
    material->setUniform("anisotropy", 1.f);
    material->setUniform("sheenColor", math::Vec3{0.4f, 0.1f, 0.2f});
    material->setUniform("clearcoat", 1.f);
    list.cameraPosition = {3, 0, 0.49f};
    list.lighting.environmentIntensity = 1;
    Graphics::render(list, target);
    const auto grazing = floatPixel(fixture, target);
    for (int channel = 0; channel < 3; ++channel)
    {
        CHECK(std::isfinite(grazing[channel]));
        CHECK(grazing[channel] >= 0);
    }
}

// Replaces studio radiance with known per-face constants while preserving its matched lookup data.
static std::shared_ptr<Environment> constantEnvironment(bool coloredFaces)
{
    auto source = createStudioEnvironment();
    std::vector<uint8_t> bytes{'O', 'F', 'G', 'I', 'B', 'L', '1', 0};
    auto append32 = [&](uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            bytes.push_back(uint8_t(value >> (i * 8)));
        }
    };
    auto append16 = [&](uint16_t value)
    {
        bytes.push_back(uint8_t(value));
        bytes.push_back(uint8_t(value >> 8));
    };
    append32(source->size());
    append32(source->mipCount());
    append32(source->lookupSize());
    for (int cube = 0; cube < 3; ++cube)
    {
        for (int face = 0; face < 6; ++face)
        {
            for (uint32_t mip = 0; mip < source->mipCount(); ++mip)
            {
                uint32_t size = source->size() >> mip;
                for (uint32_t texel = 0; texel < size * size; ++texel)
                {
                    // Face -Z is (.25,.5,1), all others are black; uncolored is the white furnace.
                    bool visible = !coloredFaces || face == 5;
                    append16(visible ? (coloredFaces ? 0x3400 : 0x3c00) : 0);
                    append16(visible ? (coloredFaces ? 0x3800 : 0x3c00) : 0);
                    append16(visible ? 0x3c00 : 0);
                    append16(0x3c00);
                }
            }
        }
    }
    for (auto value : source->lookup())
    {
        append16(value);
    }
    return Environment::fromBytes(bytes);
}

TEST_CASE("PBR IBL preserves a white furnace and cubemap orientation across replacement and roughness limits")
{
    GraphicsFixture fixture;
    auto target = fixture.target(rhi::Format::RGBA32Float);
    PbrMaterialDesc desc;
    desc.baseColor = {1, 1, 1, 1};
    desc.metallic = 1;
    desc.roughness = 0;
    auto material = createPbrMaterial(desc);
    auto mesh = pbrPlane(material);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.linearOutput = true;
    list.lighting.environment = constantEnvironment(false);
    list.items = {{mesh, 0, material, math::mat4Translation({0, 0, 0.5f})}};
    for (float metallic : {0.f, 1.f})
    {
        for (float roughness : {0.f, 0.5f, 1.f})
        {
            material->setUniform("metallic", metallic);
            material->setUniform("roughness", roughness);
            Graphics::render(list, target);
            auto pixel = floatPixel(fixture, target);
            for (int channel = 0; channel < 3; ++channel)
            {
                CHECK(pixel[channel] == doctest::Approx(1).epsilon(0.002));
            }
        }
    }
    material->setUniform("metallic", 1.f);
    material->setUniform("roughness", 0.f);
    Graphics::render(list, target); // Replace while the previous submitted frame still owns the old cube.
    list.lighting.environment = constantEnvironment(true);
    Graphics::render(list, target);
    auto pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == doctest::Approx(0.25).epsilon(0.002));
    CHECK(pixel[1] == doctest::Approx(0.5).epsilon(0.002));
    CHECK(pixel[2] == doctest::Approx(1).epsilon(0.002));
    list.lighting.environmentRotation = 3.141592654f;
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[2] < 0.001f);
}

TEST_CASE("PBR point and spot attenuation follow distance range and cone contracts")
{
    GraphicsFixture fixture;
    auto target = fixture.target(rhi::Format::RGBA32Float);
    PbrMaterialDesc desc;
    desc.baseColor = {0.5f, 0.5f, 0.5f, 1};
    desc.metallic = 0;
    desc.roughness = 1;
    auto material = createPbrMaterial(desc);
    auto mesh = pbrPlane(material);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.linearOutput = true;
    list.lighting.environment = createStudioEnvironment();
    list.lighting.environmentIntensity = 0;
    list.lighting.lightCount = 1;
    auto& light = list.lighting.lights[0];
    light.type = LightType::Point;
    light.position = {0, 0, -1.5f};
    light.intensity = 4 * 3.141592654f;
    list.items = {{mesh, 0, material, math::mat4Translation({0, 0, 0.5f})}};
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == doctest::Approx(0.49).epsilon(0.002));
    light.type = LightType::Spot;
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == doctest::Approx(0.49).epsilon(0.002));
    light.direction = {1, 0, 0};
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == 0);
    light.type = LightType::Point;
    light.range = 2;
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == 0);
}

TEST_CASE("PBR maximum portable material layout uses exactly sixteen sampled textures")
{
    GraphicsFixture fixture;
    auto scene = createPbrFixture(true);
    scene->update();
    auto list = buildDrawList(*scene, *scene->activeCamera(), 129.f / 97);
    auto target = fixture.target();
    Graphics::render(list, target);
    CHECK(fixture.pixel(target, 64, 48)[3] == 255);
}

TEST_CASE("PBR texture channels preserve sRGB color and linear metallic roughness data")
{
    GraphicsFixture fixture;
    auto target = fixture.target(rhi::Format::RGBA32Float);
    std::array<std::byte, 4> bytes{std::byte{128}, std::byte{64}, std::byte{32}, std::byte{128}};
    auto color = Texture::create({1, 1, TextureFormat::RGBA8UnormSrgb}, {bytes});
    auto data = Texture::create({1, 1, TextureFormat::RGBA8Unorm}, {bytes});
    PbrMaterialDesc desc;
    desc.unlit = true;
    desc.textures[static_cast<size_t>(PbrSlot::BaseColor)].texture = color;
    auto material = createPbrMaterial(desc);
    auto mesh = pbrPlane(material);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.environment = createStudioEnvironment();
    list.items = {{mesh, 0, material, math::mat4Translation({0, 0, 0.5f})}};
    Graphics::render(list, target);
    auto pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == doctest::Approx(0.2158605).epsilon(0.002));
    CHECK(pixel[1] == doctest::Approx(0.05126946).epsilon(0.002));
    CHECK(pixel[2] == doctest::Approx(0.01444384).epsilon(0.002));
    desc.unlit = false;
    desc.roughness = 0.8f;
    desc.metallic = 0.7f;
    desc.textures[static_cast<size_t>(PbrSlot::MetallicRoughness)].texture = data;
    list.items[0].material = createPbrMaterial(desc);
    list.lighting.debugView = 2;
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == doctest::Approx(0.8 * 64 / 255).epsilon(0.002));
    list.lighting.debugView = 3;
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == doctest::Approx(0.7 * 32 / 255).epsilon(0.002));
    // The actual inverse-transpose shader path must preserve a slanted normal under nonuniform scale.
    auto n = math::Vec3{0.70710678f, 0, -0.70710678f};
    mesh = Mesh::create(
        {{{-0.8f, -0.8f, 0}, n, {}}, {{0.8f, -0.8f, 0}, n, {}}, {{0, 0.8f, 0}, n, {}}},
        {0, 2, 1},
        {{0, 3, list.items[0].material}}
    );
    list.items[0].mesh = mesh;
    list.items[0].worldFromLocal = math::mul(math::mat4Translation({0, 0, 0.5f}), math::mat4Scale({2, 1, 1}));
    list.lighting.debugView = 1;
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == doctest::Approx(0.5 + 0.5 / std::sqrt(5.0)).epsilon(0.002));
    CHECK(pixel[2] == doctest::Approx(0.5 - 1 / std::sqrt(5.0)).epsilon(0.005));
}

TEST_CASE("Terrain source textures shade between mesh samples and preserve unknown channels")
{
    GraphicsFixture fixture;
    auto target = fixture.target(rhi::Format::RGBA32Float);
    terrain::TerrainTile source;
    source.address.depth = 9;
    const size_t count = terrain::terrainDataSamples * terrain::terrainDataSamples;
    source.surface.resize(count);
    source.water.resize(count);
    source.validity.assign(count, 7);
    for (size_t i = 0; i < count; ++i)
    {
        const auto x = (i % terrain::terrainDataSamples + 7) % 8;
        source.surface[i] = float(std::min(x, 8 - x));
    }
    source.bedrock = source.surface;
    // Every 32-interval mesh lattice column is zero, but the texture contains a four-metre triangular ridge.
    for (size_t x = 1; x < 258; x += 8)
        CHECK(source.surface[x] == 0);
    const auto pixels = terrain::encodeTerrainTexture(source);
    PbrMaterialDesc desc;
    desc.metallic = 0;
    desc.terrainTexture = Texture::create({259, 259, TextureFormat::RGBA16Float}, {std::as_bytes(std::span(pixels))});
    desc.terrainValidity =
        Texture::create({259, 259, TextureFormat::R8Unorm}, {std::as_bytes(std::span(source.validity))});
    desc.terrainMapping = {0, 0, 3.5f / 259, 100.5f / 259};
    desc.terrainSpacing = 1;
    auto material = createPbrMaterial(desc);
    auto mesh = pbrPlane(material);
    DrawList list;
    list.cameraPosition = {0, 0, -3};
    list.lighting.hdr = true;
    list.lighting.debugView = 1;
    list.lighting.environment = createStudioEnvironment();
    list.items = {{mesh, 0, material, math::mat4Translation({0, 0, .5f})}};
    Graphics::render(list, target);
    auto pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == doctest::Approx(.5 - .5 / std::sqrt(2.0)).epsilon(.002));
    CHECK(pixel[1] == doctest::Approx(.5 + .5 / std::sqrt(2.0)).epsilon(.002));
    CHECK(pixel[2] == doctest::Approx(.5).epsilon(.002));
    // Changing only source data changes shading; the same two-triangle mesh remains in place.
    auto flat = pixels;
    std::fill(flat.begin(), flat.end(), uint16_t(0));
    material->setTexture(
        "terrainTexture",
        Texture::create({259, 259, TextureFormat::RGBA16Float}, {std::as_bytes(std::span(flat))})
    );
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == doctest::Approx(.5).epsilon(.002));
    CHECK(pixel[1] == doctest::Approx(1).epsilon(.002));
    list.lighting.debugView = 4;
    material->setUniform("terrainChannel", uint32_t(3));
    Graphics::render(list, target);
    CHECK(floatPixel(fixture, target)[0] == 0);
    std::fill(source.validity.begin(), source.validity.end(), uint8_t(0));
    material->setTexture(
        "terrainValidity",
        Texture::create({259, 259, TextureFormat::R8Unorm}, {std::as_bytes(std::span(source.validity))})
    );
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == 1);
    CHECK(pixel[1] == 0);
    CHECK(pixel[2] == 1);

    // Aerial views can expose more nodes than D3D12's 2048-entry sampler heap. Each node has
    // independent material data but the immutable linear-clamp sampler belongs to the pass.
    list.items.clear();
    for (size_t i = 0; i < 2200; ++i)
    {
        auto nodeMaterial = createPbrMaterial(desc);
        nodeMaterial->setUniform("terrainChannel", uint32_t(3));
        list.items.push_back({mesh, 0, nodeMaterial, math::mat4Translation({i == 2199 ? 0.f : 4.f, 0, .5f})});
    }
    Graphics::render(list, target);
    pixel = floatPixel(fixture, target);
    CHECK(pixel[0] == 0);
    CHECK(pixel[1] == 0);
    CHECK(pixel[2] == 1);
}

TEST_CASE("Imported glTF renders independent rest-pose instances with front-face culling")
{
    GraphicsFixture fixture;
    auto model = Resources::loadResource<Model>("assets/models/laboratory.gltf");
    REQUIRE_MESSAGE(model->isLoaded(), model->error());
    auto scene = createModelFixtureScene();
    auto a = scene->instantiateModel(model, scene->getRoot());
    auto b = scene->instantiateModel(model, scene->getRoot());
    a.root->setLocalPosition({0, .8f, 0});
    b.root->setLocalPosition({0, -.8f, 0});
    scene->update();
    auto list = buildDrawList(*scene, *scene->activeCamera(), 1.5f);
    REQUIRE(list.items.size() == 4);
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 960, 640);
    Graphics::render(list, target);
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    rhi::ComPtr<ISlangBlob> pixels;
    rhi::SubresourceLayout layout{};
    REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
    auto folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/models/native";
    std::filesystem::create_directories(folder);
    std::ofstream output(folder / "instances.ppm", std::ios::binary);
    output << "P6\n960 640\n255\n";
    size_t foreground[4]{};
    const auto* bytes = static_cast<const unsigned char*>(pixels->getBufferPointer());
    for (size_t y = 0; y < 640; ++y)
    {
        for (size_t x = 0; x < 960; ++x)
        {
            const auto* pixel = bytes + y * layout.rowPitch + x * 4;
            output.write(reinterpret_cast<const char*>(pixel), 3);
            if (std::abs(int(pixel[0]) - int(bytes[0])) > 5 || std::abs(int(pixel[2]) - int(bytes[2])) > 5)
            {
                ++foreground[(y >= 320 ? 2 : 0) + (x >= 480 ? 1 : 0)];
            }
        }
    }
    CHECK(bool(output));
    for (auto count : foreground)
    {
        CHECK(count > 1000);
    }
    auto pipelineCount = Graphics::pipelineCreationCount();
    Graphics::render(list, target);
    CHECK(Graphics::pipelineCreationCount() == pipelineCount);
}

TEST_CASE("Animated superhero renders idle walk sprint one-shot and independent paused poses")
{
    GraphicsFixture fixture;
    auto model = Resources::loadResource<Model>("assets/models/character/quaternius-superhero-male.glb");
    auto library = Resources::loadResource<Model>("assets/models/character/quaternius-ual1-standard.glb");
    REQUIRE_MESSAGE(model->isLoaded(), model->error());
    REQUIRE_MESSAGE(library->isLoaded(), library->error());
    auto scene = createCharacterFixtureScene();
    auto instance = scene->instantiateModel(model, scene->getRoot());
    auto animator = scene->createAnimator(instance.root.get());
    animator->setBindings(library->data().animations, mapAnimationNodesByName(library->data(), instance));
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 960, 640);
    auto folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/animation/native";
    std::filesystem::create_directories(folder);
    // Saves tightly packed RGB evidence and returns pixels for pose stability comparisons.
    auto capture = [&](const std::string& name)
    {
        scene->update();
        Graphics::render(buildDrawList(*scene, *scene->activeCamera(), 1.5f), target);
        REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
        rhi::ComPtr<ISlangBlob> pixels;
        rhi::SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
        const auto* bytes = static_cast<const char*>(pixels->getBufferPointer());
        std::vector<char> rgb;
        rgb.reserve(960 * 640 * 3);
        size_t foreground = 0;
        for (size_t y = 0; y < 640; ++y)
        {
            for (size_t x = 0; x < 960; ++x)
            {
                auto pixel = bytes + y * layout.rowPitch + x * 4;
                rgb.insert(rgb.end(), pixel, pixel + 3);
                if (std::abs(int(uint8_t(pixel[0])) - int(uint8_t(bytes[0]))) > 10)
                {
                    ++foreground;
                }
            }
        }
        CHECK(foreground > 5000);
        std::ofstream output(folder / (name + ".ppm"), std::ios::binary);
        output << "P6\n960 640\n255\n";
        output.write(rgb.data(), rgb.size());
        CHECK(bool(output));
        return rgb;
    };
    for (const auto& name : {"Idle_Loop", "Walk_Loop", "Sprint_Loop", "Punch_Cross"})
    {
        auto clips = animator->animations();
        auto found = std::find_if(
            clips.begin(),
            clips.end(),
            [&](const auto& clip)
            {
                return clip->desc().name == name;
            }
        );
        REQUIRE(found != clips.end());
        animator->selectAnimation(size_t(found - clips.begin()));
        animator->seek(animator->durationSeconds() * .2);
        auto first = capture(std::string(name) + "-a");
        scene->updateAnimations(10);
        CHECK(capture(std::string(name) + "-paused") == first);
        animator->play();
        scene->updateAnimations(animator->durationSeconds() * .35);
        CHECK(capture(std::string(name) + "-b") != first);
        animator->pause();
    }
    animator->setLooping(false);
    animator->play();
    scene->updateAnimations(100);
    CHECK_FALSE(animator->isPlaying());
    CHECK(animator->timeSeconds() == animator->durationSeconds());
    capture("one-shot-ended");
    auto second = scene->instantiateModel(model, scene->getRoot());
    auto other = scene->createAnimator(second.root.get());
    other->setBindings(library->data().animations, mapAnimationNodesByName(library->data(), second));
    instance.root->setLocalPosition({-.85f, 0, 0});
    second.root->setLocalPosition({.85f, 0, 0});
    animator->selectAnimation(44);
    other->selectAnimation(44);
    animator->seek(.1);
    other->seek(.6);
    capture("independent-pair");
    const double held = other->timeSeconds();
    animator->play();
    scene->updateAnimations(.25);
    CHECK(other->timeSeconds() == held);
    capture("independent-pair-advanced");
    CharacterFixture character("assets/models/character", true);
    auto replacement = createCharacterFixtureScene();
    character.update(*replacement);
    REQUIRE(character.ready());
    CHECK(replacement->animators().size() == 2);
    replacement = createCharacterFixtureScene();
    CHECK_FALSE(character.ready());
    character.update(*replacement);
    CHECK(character.ready());
    CHECK(replacement->animators().size() == 2);
}

TEST_CASE("ImGui renders clipped textures, alpha, large vertex offsets and linear colour with retained uploads")
{
    GraphicsFixture fixture;
    auto context = ImGui::CreateContext();
    {
        ImGuiRenderer renderer(fixture.device, fixture.queue);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {128, 96};
        io.DisplayFramebufferScale = {2, 2};
        auto target = renderer.createTarget(256, 192);
        auto output = fixture.target(rhi::Format::RGBA8Unorm, 256, 192);
        rhi::ComPtr<rhi::IRenderPipeline> present;
        REQUIRE(SLANG_SUCCEEDED(
            createPresentationPipeline(fixture.device, rhi::Format::RGBA8Unorm, present.writeRef(), true)
        ));
        rhi::TextureDesc desc{};
        desc.size = {1, 1, 1};
        desc.format = rhi::Format::RGBA8Unorm;
        desc.usage = rhi::TextureUsage::ShaderResource;
        desc.defaultState = rhi::ResourceState::ShaderResource;
        const uint8_t white[]{255, 255, 255, 255}, blue[]{0, 0, 255, 255};
        rhi::SubresourceData whitePixels{white, 4, 4}, bluePixels{blue, 4, 4};
        rhi::ComPtr<rhi::ITexture> whiteTexture, blueTexture;
        REQUIRE(SLANG_SUCCEEDED(fixture.device->createTexture(desc, &whitePixels, whiteTexture.writeRef())));
        REQUIRE(SLANG_SUCCEEDED(fixture.device->createTexture(desc, &bluePixels, blueTexture.writeRef())));
        for (int frame = 0; frame < 3; ++frame)
        {
            io.DeltaTime = 1.f / 60;
            ImGui::NewFrame();
            auto draw = ImGui::GetBackgroundDrawList();
            // Cross the 16-bit local index limit; the visible draws must still use their actual base vertex.
            for (int i = 0; i < 17000; ++i)
            {
                draw->AddRectFilled({-20, -20}, {-10, -10}, IM_COL32_WHITE);
            }
            draw->AddImage(
                ImTextureRef(ImGuiRenderer::textureId(whiteTexture)),
                {0, 0},
                {128, 96},
                {0, 0},
                {1, 1},
                IM_COL32(128, 128, 128, 255)
            );
            draw->PushClipRect({10, 10}, {40, 40}, true);
            draw->AddImage(ImTextureRef(ImGuiRenderer::textureId(blueTexture)), {0, 0}, {80, 80});
            draw->PopClipRect();
            draw->AddImage(
                ImTextureRef(ImGuiRenderer::textureId(whiteTexture)),
                {60, 10},
                {90, 40},
                {0, 0},
                {1, 1},
                IM_COL32(255, 0, 0, 128)
            );
            // A larger font bake on the second frame exercises the dynamic atlas update lifecycle.
            ImGui::GetForegroundDrawList()
                ->AddText(nullptr, frame == 0 ? 13.f : 24.f, {4, 60}, IM_COL32_WHITE, "Atlas 0123");
            draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            draw->AddTriangleFilled({-30, -30}, {-25, -30}, {-25, -25}, IM_COL32_WHITE);
            ImGui::Render();
            REQUIRE(ImGui::GetDrawData()->TotalIdxCount % 2 == 1);
            renderer.render(*ImGui::GetDrawData(), target);
        }
        whiteTexture.setNull();
        blueTexture.setNull(); // GPU submissions, not the source variables, keep these images alive.
        REQUIRE(SLANG_SUCCEEDED(drawPresentation(fixture.queue, present, target, output)));
        const auto gray = fixture.pixel(output, 8, 8);
        CHECK(std::abs(int(gray[0]) - 128) <= 1);
        CHECK(gray[0] == gray[1]);
        CHECK(fixture.pixel(output, 30, 30) == std::array<uint8_t, 4>{0, 0, 255, 255});
        CHECK(std::abs(int(fixture.pixel(output, 84, 30)[0]) - 128) <= 1); // Outside clip.
        auto blended = fixture.pixel(output, 140, 30);
        // Linear blend of red and decoded 128 gray, followed by one sRGB transfer.
        CHECK(std::abs(int(blended[0]) - 205) <= 2);
        CHECK(std::abs(int(blended[1]) - 92) <= 2);
        CHECK(blended[3] == 255);
    }
    ImGui::DestroyContext(context);
}

TEST_CASE("workspace layouts preserve visibility while scene replacement clears observer selection")
{
    GraphicsFixture fixture;
    Workspace workspace(fixture.device, fixture.queue);
    auto scene = createPbrFixture();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {960, 640};
    workspace.loadLayout("OFG-UI-1 0 1 1\n");
    workspace.begin(*scene, 1.f / 60);
    CHECK(workspace.sceneTarget() == nullptr);
    workspace.finish();
    auto saved = workspace.saveLayout();
    CHECK(saved.starts_with("OFG-UI-1 0 1 1 1\n"));
    workspace.loadLayout("OFG-UI-1 0 1 1 0\n");
    CHECK(workspace.saveLayout().starts_with("OFG-UI-1 0 1 1 0\n"));
    workspace.loadLayout("OFG-UI-1 0 1 1\n");
    CHECK(workspace.saveLayout().starts_with("OFG-UI-1 0 1 1 1\n"));
    scene = createSceneFixture();
    workspace.begin(*scene, 1.f / 60);
    CHECK(workspace.selection() == nullptr);
    workspace.finish();
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
}

TEST_CASE("Terrain GPU preparation stays bounded and draws only complete geometry")
{
    GraphicsFixture fixture;
    QueueCompletion completion(fixture.device, fixture.queue);
    auto mesh = square(flatMaterial({.2f, .8f, .3f, 1}));
    CHECK(Graphics::streamingMeshGpuBytes(*mesh) == 0);
    size_t allowance = 4;
    CHECK_FALSE(Graphics::prepareStreamingMesh(*mesh, allowance));
    CHECK(allowance == 0);
    CHECK(Graphics::streamingMeshGpuBytes(*mesh) == mesh->vertices().size_bytes() + mesh->indices().size_bytes());
    const auto uploadSerial = completion.mark();
    DrawList list;
    list.items.push_back({mesh, 0, mesh->subMeshes()[0].material, math::mat4Identity(), {}});
    auto target = fixture.target();
    CHECK_THROWS(Graphics::render(list, target));
    allowance = mesh->vertices().size_bytes() + mesh->indices().size_bytes() - 4;
    CHECK(Graphics::prepareStreamingMesh(*mesh, allowance));
    CHECK(allowance == 0);
    Graphics::render(list, target);
    const auto drawSerial = completion.mark();
    CHECK(drawSerial > uploadSerial);
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    CHECK(completion.completed() == drawSerial);
    auto pixel = fixture.pixel(target, 64, 48);
    CHECK(pixel[1] > pixel[0]);
}

TEST_CASE("Terrain GPU prebuilt parts toggle without further mesh uploads")
{
    GraphicsFixture fixture;
    auto material = flatMaterial({0, 1, 0, 1});
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> parts;
    // Seven disjoint patches make each selected range independently observable in the readback.
    for (uint32_t part = 0; part < 7; ++part)
    {
        const float x = -0.75f + part * 0.25f;
        const auto firstVertex = uint32_t(vertices.size());
        vertices.insert(
            vertices.end(),
            {{{x - .08f, -.2f, .5f}, {}, {}},
             {{x + .08f, -.2f, .5f}, {}, {}},
             {{x + .08f, .2f, .5f}, {}, {}},
             {{x - .08f, .2f, .5f}, {}, {}}}
        );
        parts.push_back({uint32_t(indices.size()), 6, material});
        for (uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
        {
            indices.push_back(firstVertex + index);
        }
    }
    auto mesh = Mesh::create(std::move(vertices), std::move(indices), std::move(parts));
    size_t allowance = 1 << 20;
    REQUIRE(Graphics::prepareStreamingMesh(*mesh, allowance));
    const auto allocated = Graphics::streamingMeshGpuBytes(*mesh);
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 128, 96);
    for (unsigned mask : {0u, 21u, 42u, 63u, 0u})
    {
        DrawList draws;
        draws.items.push_back({mesh, 0, material, math::mat4Identity(), {}});
        for (unsigned face = 0; face < 6; ++face)
        {
            if (mask & (1u << face))
            {
                draws.items.push_back({mesh, face + 1, material, math::mat4Identity(), {}});
            }
        }
        allowance = 0;
        CHECK(Graphics::prepareStreamingMesh(*mesh, allowance));
        Graphics::render(draws, target);
        for (unsigned part = 0; part < 7; ++part)
        {
            const auto pixel = fixture.pixel(target, 16 + part * 16, 48);
            const bool selected = part == 0 || (mask & (1u << (part - 1)));
            CHECK((pixel[1] > 200 && pixel[0] < 10) == selected);
        }
        CHECK(Graphics::streamingMeshGpuBytes(*mesh) == allocated);
    }
}

TEST_CASE("Terrain laboratory renders asynchronously generated native geometry")
{
    GraphicsFixture fixture;
    Game::initialize(createTerrainScene());
    TerrainLaunchSettings launch;
    if (const auto* url = std::getenv("OFG_TERRAIN_SERVICE"))
        launch.serviceUrl = url;
    const unsigned expectedDepth = launch.serviceUrl.empty() ? 13 : 9;
    Game::setTerrain(std::make_unique<TerrainLaboratory>(fixture.device, fixture.queue, launch));
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 960, 640);
    auto start = std::chrono::steady_clock::now();
    size_t frames = 0;
    do
    {
        const auto before = std::chrono::steady_clock::now();
        Game::frame(1.f / 60, target);
        ++frames;
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        if (frames % 1000 == 0)
        {
            const auto d = Game::terrain()->diagnostics();
            std::printf(
                "Terrain frame %zu: %.1f ms, %zu roots, %zu cut, %zu jobs; depth %u failed %zu pending refinement %zu "
                "CPU %.1f MiB budgetBlocked %d\n",
                frames,
                milliseconds,
                d.admittedRoots,
                d.selected,
                d.jobs,
                unsigned(d.deepestSurfaceDepth),
                d.failed,
                d.unresolvedRefinements,
                double(d.residentCpuBytes + d.reservedCpuBytes + d.retiredCpuBytes) / (1 << 20),
                int(d.budgetBlocked)
            );
            if (!Game::terrain()->error().empty())
                std::printf("Terrain error: %s\n", Game::terrain()->error().c_str());
        }
    }
    while ((!Game::terrain()->diagnostics().planningIdle ||
            Game::terrain()->diagnostics().deepestSurfaceDepth < expectedDepth ||
            Game::terrain()->diagnostics().unresolvedRefinements != 0) &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(90));
    const auto d = Game::terrain()->diagnostics();
    CHECK(d.admittedRoots > 0);
    CHECK(d.deepestSurfaceDepth >= expectedDepth);
    CHECK(d.unresolvedRefinements == 0);
    CHECK(d.planningIdle);
    CHECK(d.selected > d.admittedRoots);
    CHECK(d.failed == 0);
    // Frustum extraction changes only draws; the admitted spatial cut and its shadow casters remain intact.
    DrawList visible;
    Game::terrain()->append(visible);
    DrawList culled;
    culled.clipFromWorld = {};
    culled.clipFromWorld[3].w = -1;
    Game::terrain()->append(culled);
    CHECK(culled.items.empty());
    CHECK(culled.shadowCasters.size() == visible.shadowCasters.size());
    CHECK(Game::terrain()->diagnostics().selected == d.selected);
    CHECK(Game::terrain()->diagnostics().publications == d.publications);
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    rhi::ComPtr<ISlangBlob> pixels;
    rhi::SubresourceLayout layout{};
    REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
    auto folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/terrain/native";
    if (!launch.serviceUrl.empty())
        folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/terrain-service/stage-2/native";
    if (std::getenv("OFG_TERRAIN_EROSION"))
        folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/terrain-service/stage-4/native";
    if (std::getenv("OFG_TERRAIN_REGENERATE"))
        folder = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/terrain-service/stage-3/native";
    std::filesystem::create_directories(folder);
    std::ofstream output(folder / "terrain.ppm", std::ios::binary);
    output << "P6\n960 640\n255\n";
    for (size_t y = 0; y < 640; ++y)
    {
        for (size_t x = 0; x < 960; ++x)
        {
            output.write(static_cast<const char*>(pixels->getBufferPointer()) + y * layout.rowPitch + x * 4, 3);
        }
    }
    REQUIRE(bool(output));
    // Exercise actual renderer retirement after the complete depth-13 selection has been submitted.
    output.close();
    if (!launch.serviceUrl.empty())
    {
        const auto previous = Game::terrain()->diagnostics().publications;
        Game::terrain()->frameIsland();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do
        {
            Game::frame(1.f / 60, target);
        }
        while ((Game::terrain()->diagnostics().publications == previous || Game::terrain()->diagnostics().jobs ||
                !Game::terrain()->diagnostics().planningIdle) &&
               std::chrono::steady_clock::now() < deadline);
        CHECK(Game::terrain()->diagnostics().jobs == 0);
        REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
        pixels.setNull();
        REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
        std::ofstream aerial(folder / "aerial.ppm", std::ios::binary);
        aerial << "P6\n960 640\n255\n";
        for (size_t y = 0; y < 640; ++y)
            for (size_t x = 0; x < 960; ++x)
                aerial.write(static_cast<const char*>(pixels->getBufferPointer()) + y * layout.rowPitch + x * 4, 3);
        REQUIRE(bool(aerial));
        if (std::getenv("OFG_TERRAIN_EROSION"))
        {
            const auto camera = Game::terrain()->cameraPosition();
            auto* client = Game::terrain()->generation();
            Game::terrain()->startErosion();
            for (int step = 0; step < 3; ++step)
            {
                if (step)
                    client->control("step");
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
                do
                {
                    Game::frame(1.f / 60, target);
                }
                while ((client->state != "paused" || client->step != step ||
                        Game::terrain()->displayedRevision() != client->latestRevision ||
                        Game::terrain()->replacing() || Game::terrain()->diagnostics().jobs ||
                        !Game::terrain()->diagnostics().planningIdle) &&
                       std::chrono::steady_clock::now() < end);
                REQUIRE(client->error.empty());
                REQUIRE(Game::terrain()->error().empty());
                REQUIRE(client->state == "paused");
                REQUIRE(client->step == step);
                CHECK(client->years == step * client->erosion.timestepYears);
                REQUIRE(Game::terrain()->displayedRevision() == client->latestRevision);
                CHECK(Game::terrain()->cameraPosition().cell == camera.cell);
                CHECK(Game::terrain()->cameraPosition().local == camera.local);
                REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
                pixels.setNull();
                REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
                std::ofstream image(folder / ("erosion-" + std::to_string(step) + ".ppm"), std::ios::binary);
                image << "P6\n960 640\n255\n";
                for (size_t y = 0; y < 640; ++y)
                    for (size_t x = 0; x < 960; ++x)
                        image.write(
                            static_cast<const char*>(pixels->getBufferPointer()) + y * layout.rowPitch + x * 4,
                            3
                        );
                REQUIRE(bool(image));
            }
            client->control("cancel");
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (client->active() && std::chrono::steady_clock::now() < end)
                Game::frame(1.f / 60, target);
            CHECK(client->state == "cancelled");
        }
        if (std::getenv("OFG_TERRAIN_REGENERATE"))
        {
            const auto oldRevision = Game::terrain()->displayedRevision();
            const auto camera = Game::terrain()->cameraPosition();
            auto recipe = Game::terrain()->generation()->accepted;
            recipe.seed += 5;
            recipe.plateauHeight += 80;
            Game::terrain()->regenerate(recipe);
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            do
            {
                Game::frame(1.f / 60, target);
            }
            while ((Game::terrain()->displayedRevision() == oldRevision || Game::terrain()->replacing() ||
                    Game::terrain()->diagnostics().jobs || !Game::terrain()->diagnostics().planningIdle) &&
                   std::chrono::steady_clock::now() < end);
            CHECK(Game::terrain()->error().empty());
            CHECK(Game::terrain()->generation()->error.empty());
            CHECK(Game::terrain()->displayedRevision() != oldRevision);
            CHECK(Game::terrain()->displayedRevision() == Game::terrain()->generation()->latestRevision);
            CHECK_FALSE(Game::terrain()->replacing());
            CHECK(Game::terrain()->cameraPosition().cell == camera.cell);
            CHECK(Game::terrain()->cameraPosition().local == camera.local);
            REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
            pixels.setNull();
            REQUIRE(SLANG_SUCCEEDED(fixture.device->readTexture(target, 0, 0, pixels.writeRef(), &layout)));
            std::ofstream changed(folder / "regenerated.ppm", std::ios::binary);
            changed << "P6\n960 640\n255\n";
            for (size_t y = 0; y < 640; ++y)
                for (size_t x = 0; x < 960; ++x)
                    changed.write(
                        static_cast<const char*>(pixels->getBufferPointer()) + y * layout.rowPitch + x * 4,
                        3
                    );
            REQUIRE(bool(changed));
        }
    }
    Game::terrain()->teleport({{0, 1000000, 0}, {}});
    const auto draining = std::chrono::steady_clock::now();
    for (;;)
    {
        Game::frame(1.f / 60, target);
        const auto retired = Game::terrain()->diagnostics();
        if (!retired.nodes && !retired.jobs && !retired.retiredCpuBytes && !retired.retiredGpuBytes)
        {
            CHECK(retired.residentCpuBytes == 0);
            CHECK(retired.residentGpuBytes == 0);
            break;
        }
        REQUIRE(std::chrono::steady_clock::now() - draining < std::chrono::seconds(30));
    }
}

TEST_CASE("Terrain ten minute traversal retires obsolete allocations" * doctest::skip())
{
    GraphicsFixture fixture;
    Game::initialize(createTerrainScene());
    TerrainLaunchSettings launch;
    if (const auto* url = std::getenv("OFG_TERRAIN_SERVICE"))
        launch.serviceUrl = url;
    Game::setTerrain(std::make_unique<TerrainLaboratory>(fixture.device, fixture.queue, launch));
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 960, 640);
    const auto directory = std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/terrain/native";
    std::filesystem::create_directories(directory);
    std::ofstream report(directory / "traversal.csv");
    report << "seconds,frames,roots,selected,jobs,cpu_bytes,gpu_bytes,retired_bytes,failed\n";
    const auto start = std::chrono::steady_clock::now();
    double elapsed = 0, nextReport = 0, nextRegeneration = 60;
    uint64_t frames = 0;
    size_t maximumRoots = 0, regenerations = 0;
    do
    {
        if (!launch.serviceUrl.empty() && std::fmod(elapsed, 120) < 100)
            Game::terrain()->teleportToSurface(
                {{}, {2048 * std::cos(elapsed * .03), 0, 2048 * std::sin(elapsed * .03)}},
                20
            );
        else
            Game::terrain()->traverse(elapsed);
        Game::frame(1.f / 60, target);
        if (!launch.serviceUrl.empty() && elapsed >= nextRegeneration && !Game::terrain()->generation()->active())
        {
            auto recipe = Game::terrain()->generation()->accepted;
            recipe.seed += 1;
            Game::terrain()->regenerate(recipe);
            ++regenerations;
            nextRegeneration += 120;
        }
        ++frames;
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto d = Game::terrain()->diagnostics();
        REQUIRE(Game::terrain()->error().empty());
        if (auto* generation = Game::terrain()->generation())
            REQUIRE(generation->error.empty());
        maximumRoots = std::max(maximumRoots, d.admittedRoots);
        REQUIRE(d.failed == 0);
        REQUIRE(
            d.residentCpuBytes + d.reservedCpuBytes + d.retiredCpuBytes <=
            ((launch.serviceUrl.empty() ? 256ull : 512ull) << 20)
        );
        REQUIRE(d.residentGpuBytes + d.reservedGpuBytes + d.retiredGpuBytes <= (256ull << 20));
        if (elapsed >= nextReport)
        {
            report << elapsed << ',' << frames << ',' << d.admittedRoots << ',' << d.selected << ',' << d.jobs << ','
                   << d.residentCpuBytes + d.reservedCpuBytes + d.retiredCpuBytes << ','
                   << d.residentGpuBytes + d.reservedGpuBytes + d.retiredGpuBytes << ',' << d.retiredGpuBytes << ','
                   << d.failed << '\n';
            report.flush();
            nextReport += 5;
        }
    }
    while (elapsed < 600);
    CHECK(maximumRoots > 0);
    if (!launch.serviceUrl.empty())
        CHECK(regenerations >= 4);
    Game::terrain()->teleport({{0, 1000000, 0}, {}});
    const auto drainStart = std::chrono::steady_clock::now();
    for (;;)
    {
        Game::frame(1.f / 60, target);
        const auto d = Game::terrain()->diagnostics();
        if (!d.nodes && !d.jobs && !d.retiredCpuBytes && !d.retiredGpuBytes)
        {
            break;
        }
        REQUIRE(std::chrono::steady_clock::now() - drainStart < std::chrono::seconds(120));
    }
    REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
    report << "drained," << frames << ",0,0,0,0,0,0,0\n";
    CHECK(bool(report));
    std::printf(
        "Terrain traversal: %s, D3D12 Debug validation, 960x640, %llu frames in %.2f seconds; all terrain allocations "
        "drained.\n",
        fixture.device->getInfo().adapterName,
        (unsigned long long)frames,
        elapsed
    );
}

// Repeatable diagnostic workload; phase timings are evidence, not machine-dependent unit-test thresholds.
TEST_CASE("Terrain stationary profile" * doctest::skip())
{
    const bool validation = std::getenv("OFG_TERRAIN_PROFILE_VALIDATION") != nullptr;
    GraphicsFixture fixture(validation);
    Game::initialize(createTerrainScene());
    TerrainLaunchSettings launch;
    if (const auto* url = std::getenv("OFG_TERRAIN_SERVICE"))
        launch.serviceUrl = url;
    launch.skipCache = std::getenv("OFG_TERRAIN_PROFILE_SKIP_CACHE") != nullptr;
    Game::setTerrain(std::make_unique<TerrainLaboratory>(fixture.device, fixture.queue, launch));
    auto target = fixture.target(rhi::Format::RGBA8Unorm, 1440, 1200);
    auto& scene = Game::scene();
    auto& terrain = *Game::terrain();
    const auto* outputDirectory = std::getenv("OFG_TERRAIN_PROFILE_DIR");
    const auto directory = outputDirectory ? std::filesystem::path(outputDirectory)
                                           : std::filesystem::path(OFG_SOURCE_DIR) / "artifacts/terrain/profile";
    std::filesystem::create_directories(directory);
    const bool moving = std::getenv("OFG_TERRAIN_PROFILE_ROUTE") != nullptr;
    std::ofstream report(directory / (moving ? "moving.csv" : "stationary.csv"));
    report << "seconds,update_ms,extract_ms,render_ms,wait_ms,cut,jobs,cpu,blocked,draws,triangles,finest_depth,"
              "unresolved,idle,nodes,coordinator_max_ms,snapshot_age_ms,gpu,retired_gpu,source_cpu,texture_bytes,cache_"
              "hits,cache_misses,cache_bypasses\n";
#ifdef OFG_TERRAIN_FUNCTION_PROFILE
    terrainProfile::enabled = std::getenv("OFG_TERRAIN_PROFILE_FUNCTIONS") != nullptr;
    std::ofstream functions(directory / "functions.csv");
    functions << "phase,frames,seconds,function,calls,inclusive_ms,exclusive_ms\n";
    size_t frames = 0;
    bool checkpointWritten = false, convergedCheckpoint = false;
#endif
    const auto start = std::chrono::steady_clock::now();
    double elapsed = 0, nextReport = 0;
    double nearestSurfaceDistance = 0;
    bool sourcePositioned = launch.serviceUrl.empty();
    do
    {
        const auto before = std::chrono::steady_clock::now();
        const auto acquired = terrain.cacheCounts();
        if (!sourcePositioned && acquired[0] + acquired[1] + acquired[2] > 0)
        {
            terrain.teleportToSurface({{}, {1, 0, 1}}, 8);
            sourcePositioned = true;
        }
        if (moving)
        {
            terrain.traverse(elapsed);
        }
        terrain.update(scene);
        const auto updated = std::chrono::steady_clock::now();
        scene.update();
        auto list = buildDrawList(scene, *scene.activeCamera(), 1440.f / 1200);
        terrain.append(list);
        nearestSurfaceDistance = 1e30;
        for (const auto& item : list.shadowCasters)
        {
            const auto bounds = item.mesh->bounds();
            const auto translation = item.worldFromLocal[3];
            const double dx =
                std::max({double(bounds.minimum.x + translation.x), -double(bounds.maximum.x + translation.x), 0.});
            const double dy =
                std::max({double(bounds.minimum.y + translation.y), -double(bounds.maximum.y + translation.y), 0.});
            const double dz =
                std::max({double(bounds.minimum.z + translation.z), -double(bounds.maximum.z + translation.z), 0.});
            nearestSurfaceDistance = std::min(nearestSurfaceDistance, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        const auto extracted = std::chrono::steady_clock::now();
        Graphics::render(list, target);
        terrain.submitted();
        const auto rendered = std::chrono::steady_clock::now();
        {
            OFG_TERRAIN_SCOPE(GpuWait);
            REQUIRE(SLANG_SUCCEEDED(fixture.queue->waitOnHost()));
        }
        const auto waited = std::chrono::steady_clock::now();
        const double updateMs = std::chrono::duration<double, std::milli>(updated - before).count();
        const double extractMs = std::chrono::duration<double, std::milli>(extracted - updated).count();
        const double renderMs = std::chrono::duration<double, std::milli>(rendered - extracted).count();
        const double waitMs = std::chrono::duration<double, std::milli>(waited - rendered).count();
        elapsed = std::chrono::duration<double>(waited - start).count();
#ifdef OFG_TERRAIN_FUNCTION_PROFILE
        ++frames;
        if (!checkpointWritten && elapsed >= 45)
        {
            terrainProfile::checkpoint(functions, "warmup", frames, elapsed);
            checkpointWritten = true;
        }
#endif
        const auto d = terrain.diagnostics();
#ifdef OFG_TERRAIN_FUNCTION_PROFILE
        if (!convergedCheckpoint && d.deepestSurfaceDepth == 13 && d.planningIdle && d.unresolvedRefinements == 0 &&
            d.jobs == 0)
        {
            terrainProfile::checkpoint(functions, "converged", frames, elapsed);
            convergedCheckpoint = true;
        }
#endif
        REQUIRE(d.failed == 0);
        size_t triangles = 0;
        for (const auto& item : list.items)
        {
            triangles += item.mesh->subMeshes()[item.subMeshIndex].indexCount / 3;
        }
        report << elapsed << ',' << updateMs << ',' << extractMs << ',' << renderMs << ',' << waitMs << ','
               << d.selected << ',' << d.jobs << ',' << d.residentCpuBytes << ',' << d.budgetBlocked << ','
               << list.items.size() << ',' << triangles << ',' << unsigned(d.deepestSurfaceDepth) << ','
               << d.unresolvedRefinements << ',' << d.planningIdle << ',' << d.nodes << ','
               << terrain.maximumReconciliationMilliseconds() << ',' << terrain.snapshotAgeMilliseconds() << ','
               << d.residentGpuBytes << ',' << d.retiredGpuBytes << ',' << d.sourceCpuBytes << ','
               << d.sourceTextureBytes << ',' << acquired[0] << ',' << acquired[1] << ',' << acquired[2] << '\n';
        if (elapsed >= nextReport)
        {
            std::printf(
                "Terrain profile %.1fs: update %.1f, extract %.1f, render %.1f, wait %.1f ms; cut %zu jobs %zu blocked "
                "%d\n",
                elapsed,
                updateMs,
                extractMs,
                renderMs,
                waitMs,
                d.selected,
                d.jobs,
                int(d.budgetBlocked)
            );
            report.flush();
            nextReport += 5;
        }
    }
    while (elapsed < 60);
#ifdef OFG_TERRAIN_FUNCTION_PROFILE
    terrainProfile::enabled = false;
    terrainProfile::checkpoint(functions, "total", frames, elapsed);
#endif
    CHECK(terrain.diagnostics().deepestSurfaceDepth >= (moving ? 4 : 13));
    CHECK(nearestSurfaceDistance < 64); // The cut must actually follow the observer, not merely retain old fine tiles.
    if (!moving)
    {
        CHECK(terrain.diagnostics().unresolvedRefinements == 0);
        CHECK(terrain.diagnostics().jobs == 0);
        CHECK(terrain.diagnostics().planningIdle);
    }
    std::printf(
        "Terrain profile: %s, D3D12 validation %s, 1440x1200.\n",
        fixture.device->getInfo().adapterName,
        validation ? "on" : "off"
    );
}
