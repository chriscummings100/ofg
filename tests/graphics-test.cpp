// Native offscreen scene tests for indexed draws, uniforms, cache identity, retirement and Game ordering.
#include "render/graphics.h"
#include "render/texture-renderer.h"
#include <cstring>
#include <cmath>
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
