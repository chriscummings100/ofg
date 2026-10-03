// Native offscreen scene tests for indexed draws, uniforms, cache identity, retirement and Game ordering.
#include "render/graphics.h"
#include "render/present.h"
#include "lab/scene-fixture.h"
#include "game.h"
#include "resources/resources.h"
#include "math/transform.h"
#include <ostream>
#include <doctest.h>
#include <atomic>
#include <array>

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
    // Creates a validated native device and the shared Graphics service.
    GraphicsFixture()
    {
        rhi::DeviceDesc desc{};
        desc.deviceType = rhi::DeviceType::D3D12;
        desc.enableValidation = true;
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
    rhi::ComPtr<rhi::ITexture> target(rhi::Format format = rhi::Format::RGBA8Unorm, uint32_t width = 129)
    {
        rhi::TextureDesc desc{};
        desc.type = rhi::TextureType::Texture2D;
        desc.size = {width, 97, 1};
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
