// Explicit lookup-generation/capture passes; each encoded draw retains its own uniform storage.
#include "render/sky-renderer.h"
#include "render/lighting-pass.h"
#include "sky-shader.h"
#include <cmath>
#include <string>
#include <cassert>
namespace ofg {
using namespace rhi;
SkyRenderer::SkyRenderer(IDevice* device, ICommandQueue* queue)
    : m_device(device)
    , m_queue(queue)
{
}
// Creates one reusable HDR lighting texture with explicit render/sample usage.
static ComPtr<ITexture> skyTexture(
    IDevice* device,
    uint32_t width,
    uint32_t height,
    TextureType type = TextureType::Texture2D,
    uint32_t layers = 1
)
{
    rhi::TextureDesc desc{};
    desc.type = type;
    desc.arrayLength = layers;
    desc.size = {width, height, 1};
    desc.format = Format::RGBA16Float;
    desc.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource | TextureUsage::CopySource;
    desc.defaultState = rhi::ResourceState::ShaderResource;
    ComPtr<ITexture> texture;
    checkLighting(device->createTexture(desc, nullptr, texture.writeRef()), "Create sky texture");
    return texture;
}
void SkyRenderer::initialize()
{
    if (m_sampler)
    {
        return;
    }
    for (uint32_t pass = 0; pass < 6; ++pass)
    {
        std::string name = "ofg_sky_" + std::to_string(pass);
        std::string source = "#define SKY_PASS " + std::to_string(pass) + "\n" + skyShader;
        m_programs[pass] = lightingProgram(m_device, source.c_str(), name.c_str());
        m_pipelines[pass] = lightingPipeline(m_device, m_programs[pass]);
    }
    m_transmission = skyTexture(m_device, 256, 64);
    m_multiple = skyTexture(m_device, 32, 32);
    m_skyView = skyTexture(m_device, 512, 256);
    auto captureDesc = m_skyView->getDesc();
    captureDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
    checkLighting(m_device->createTexture(captureDesc, nullptr, m_captureSkyView.writeRef()), "Create frozen sky view");
    m_aerial = skyTexture(m_device, 32, 32, TextureType::Texture2DArray, 32);
    m_capture = skyTexture(m_device, 128, 128, TextureType::TextureCube);
    rhi::SamplerDesc desc{};
    desc.addressU = desc.addressV = desc.addressW = TextureAddressingMode::ClampToEdge;
    checkLighting(m_device->createSampler(desc, m_sampler.writeRef()), "Create sky sampler");
}
ComPtr<IShaderObject> SkyRenderer::bindings(uint32_t pass, const DrawList& list, uint32_t slice)
{
    ComPtr<IShaderObject> root;
    checkLighting(m_device->createRootShaderObject(m_programs[pass], root.writeRef()), "Create sky bindings");
    auto cursor = ShaderCursor(root), s = cursor["sky"];
    const auto& settings = *list.lighting.outdoor;
    const auto& frame = *list.outdoor;
    // All fields are float4 or reflected matrices; no C++ aggregate padding crosses the shader boundary.
    const auto set = [&](const char* name, std::array<float, 4> value)
    {
        checkLighting(s[name].setData(value.data(), sizeof(value)), "Bind sky parameter");
    };
    set("sun", {frame.sunDirection.x, frame.sunDirection.y, frame.sunDirection.z, settings.atmosphere.sunIlluminance});
    set("moon",
        {frame.moonDirection.x, frame.moonDirection.y, frame.moonDirection.z, settings.atmosphere.moonIlluminance});
    auto sunT = atmosphereTransmission(settings.atmosphere, list.cameraPosition.y, frame.sunDirection.y);
    auto moonT = atmosphereTransmission(settings.atmosphere, list.cameraPosition.y, frame.moonDirection.y);
    set("sunTransmission", {sunT.x, sunT.y, sunT.z, 0});
    set("moonTransmission", {moonT.x, moonT.y, moonT.z, 0});
    set("atmosphere",
        {settings.atmosphere.rayleigh,
         settings.atmosphere.mie,
         settings.atmosphere.ozone,
         settings.atmosphere.groundAlbedo});
    set("camera",
        {list.cameraPosition.x,
         list.cameraPosition.y,
         list.cameraPosition.z,
         frame.exposureMultiplier * list.lighting.exposure});
    const auto& cloud = settings.clouds;
    set("clouds", {cloud.coverage, cloud.opticalThickness, cloud.scale, 0});
    float wind = float(std::fmod(settings.dayCycle.elapsedSeconds * cloud.windSpeed, 1000000.0));
    set("windLayers",
        {std::cos(cloud.windAngle) * wind, std::sin(cloud.windAngle) * wind, cloud.altitude, cloud.secondAltitude});
    set("viewport", {0, 0, list.camera.aspectRatio, float(slice)});
    set("controls",
        {std::tan(list.camera.verticalFov * .5f), settings.atmosphere.aerialDistance, float(settings.showDisks), 0});
    setLightingMatrix(s["worldFromView"], list.camera.worldFromView);
    if (pass >= 1 && pass <= 3)
    {
        checkLighting(cursor["transmittanceLut"].setBinding(m_transmission), "Bind atmosphere transmission");
    }
    if (pass >= 2 && pass <= 3)
    {
        checkLighting(cursor["multipleLut"].setBinding(m_multiple), "Bind multiple scattering");
    }
    if (pass >= 4)
    {
        checkLighting(cursor["skyViewLut"].setBinding(pass == 5 ? m_captureSkyView : m_skyView), "Bind sky view");
    }
    if (pass >= 1)
    {
        checkLighting(cursor["skySampler"].setBinding(m_sampler), "Bind sky sampler");
    }
    return root;
}
void SkyRenderer::update(const DrawList& frame)
{
    initialize();
    const auto& a = frame.lighting.outdoor->atmosphere;
    std::array<float, 4> atmosphere{a.rayleigh, a.mie, a.ozone, a.groundAlbedo};
    const auto& f = *frame.outdoor;
    std::array<float, 13> skySnapshot{
        a.rayleigh,
        a.mie,
        a.ozone,
        a.groundAlbedo,
        f.sunDirection.x,
        f.sunDirection.y,
        f.sunDirection.z,
        a.sunIlluminance,
        f.moonDirection.x,
        f.moonDirection.y,
        f.moonDirection.z,
        a.moonIlluminance,
        frame.cameraPosition.y
    };
    std::array<float, 20> aerialSnapshot{};
    for (int column = 0; column < 4; ++column)
    {
        const auto& v = frame.camera.worldFromView[column];
        aerialSnapshot[column * 4] = v.x;
        aerialSnapshot[column * 4 + 1] = v.y;
        aerialSnapshot[column * 4 + 2] = v.z;
        aerialSnapshot[column * 4 + 3] = v.w;
    }
    aerialSnapshot[16] = frame.camera.verticalFov;
    aerialSnapshot[17] = frame.camera.aspectRatio;
    aerialSnapshot[18] = a.aerialDistance;
    aerialSnapshot[19] = frame.cameraPosition.y;
    if (skySnapshot == m_skySnapshot && aerialSnapshot == m_aerialSnapshot)
    {
        return;
    }
    ComPtr<ICommandEncoder> encoder;
    checkLighting(m_queue->createCommandEncoder(encoder.writeRef()), "Create atmosphere encoder");
    if (atmosphere != m_atmosphere)
    {
        auto trans = bindings(0, frame, 0);
        drawLightingPass(encoder, m_pipelines[0], trans, m_transmission->getDefaultView(), 256, 64);
        auto multiple = bindings(1, frame, 0);
        drawLightingPass(encoder, m_pipelines[1], multiple, m_multiple->getDefaultView(), 32, 32);
    }
    if (skySnapshot != m_skySnapshot)
    {
        auto sky = bindings(2, frame, 0);
        drawLightingPass(encoder, m_pipelines[2], sky, m_skyView->getDefaultView(), 512, 256);
    }
    if (skySnapshot != m_skySnapshot || aerialSnapshot != m_aerialSnapshot)
    {
        for (uint32_t slice = 0; slice < 32; ++slice)
        {
            auto root = bindings(3, frame, slice);
            auto view = lightingView(m_aerial, slice);
            drawLightingPass(encoder, m_pipelines[3], root, view, 32, 32);
        }
    }
    submitLighting(m_queue, encoder);
    m_skySnapshot = skySnapshot;
    m_aerialSnapshot = aerialSnapshot;
    m_atmosphere = atmosphere;
}
void SkyRenderer::drawBackground(const DrawList& frame, ITexture* target)
{
    auto root = bindings(4, frame, 0);
    ComPtr<ICommandEncoder> encoder;
    checkLighting(m_queue->createCommandEncoder(encoder.writeRef()), "Create sky encoder");
    drawLightingPass(
        encoder,
        m_pipelines[4],
        root,
        target->getDefaultView(),
        target->getDesc().size.width,
        target->getDesc().size.height
    );
    submitLighting(m_queue, encoder);
}
void SkyRenderer::beginCapture()
{
    ComPtr<ICommandEncoder> encoder;
    checkLighting(m_queue->createCommandEncoder(encoder.writeRef()), "Create sky snapshot encoder");
    encoder->copyTexture(m_captureSkyView, {}, {}, m_skyView, {}, {}, Extent3D::kWholeTexture);
    submitLighting(m_queue, encoder);
}
ITexture* SkyRenderer::captureFace(const DrawList& frame, uint32_t face)
{
    assert(face < 6);
    ComPtr<ICommandEncoder> encoder;
    checkLighting(m_queue->createCommandEncoder(encoder.writeRef()), "Create capture encoder");
    auto root = bindings(5, frame, face);
    auto view = lightingView(m_capture, face);
    drawLightingPass(encoder, m_pipelines[5], root, view, 128, 128);
    submitLighting(m_queue, encoder);
    return m_capture;
}
} // namespace ofg
