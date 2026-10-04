// Uploads baked IBL and incrementally builds frozen dynamic generations without exposing partial filters.
#include "render/environment-renderer.h"
#include "render/resource-gpu-data.h"
#include "render/sky-renderer.h"
#include "render/lighting-pass.h"
#include "environment-shader.h"
#include <algorithm>
#include <cmath>
namespace ofg {
using namespace rhi;
EnvironmentRenderer::EnvironmentRenderer(IDevice* device, ICommandQueue* queue)
    : m_device(device)
    , m_queue(queue)
{
}
EnvironmentRenderer::~EnvironmentRenderer()
{
    for (auto& weak : m_environments)
    {
        if (auto data = weak.lock())
        {
            data->cubes.setNull();
            data->lookup.setNull();
        }
    }
}
void EnvironmentRenderer::prepare(Environment& environment)
{
    if (environment.m_gpu && environment.m_gpu->lookup)
    {
        return;
    }
    auto data = std::make_shared<EnvironmentGpuData>();
    rhi::TextureDesc cubeDesc{};
    cubeDesc.type = TextureType::TextureCubeArray;
    cubeDesc.arrayLength = 3;
    cubeDesc.size = {environment.size(), environment.size(), 1};
    cubeDesc.format = Format::RGBA16Float;
    cubeDesc.mipCount = environment.mipCount();
    cubeDesc.usage = TextureUsage::ShaderResource;
    cubeDesc.defaultState = rhi::ResourceState::ShaderResource;
    std::vector<SubresourceData> subresources;
    for (size_t i = 0; i < 3; ++i)
    {
        size_t offset = 0;
        for (uint32_t face = 0; face < 6; ++face)
        {
            for (uint32_t mip = 0; mip < environment.mipCount(); ++mip)
            {
                size_t size = environment.size() >> mip;
                subresources.push_back({environment.cube(i).data() + offset, size * 8, size * size * 8});
                offset += size * size * 4;
            }
        }
    }
    checkLighting(
        m_device->createTexture(cubeDesc, subresources.data(), data->cubes.writeRef()),
        "Upload IBL cube array"
    );
    rhi::TextureDesc desc{};
    desc.size = {environment.lookupSize(), environment.lookupSize(), 1};
    desc.format = Format::RGBA16Float;
    desc.usage = TextureUsage::ShaderResource;
    desc.defaultState = rhi::ResourceState::ShaderResource;
    SubresourceData pixels{environment.lookup().data(), environment.lookupSize() * 8, environment.lookup().size() * 2};
    checkLighting(m_device->createTexture(desc, &pixels, data->lookup.writeRef()), "Upload BRDF lookup");
    std::erase_if(
        m_environments,
        [](const auto& weak)
        {
            return weak.expired();
        }
    );
    m_environments.push_back(data);
    environment.m_gpu = std::move(data);
}


math::Vec3 EnvironmentRenderer::blend() const
{
    if (m_first)
    {
        return {0, 0, 1};
    }
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_published).count();
    float weight = m_snapBlend ? 1.0f : float(std::clamp(elapsed / 0.1, 0.0, 1.0));
    return {float(1 - m_generation), float(m_generation), weight};
}
double EnvironmentRenderer::ageSeconds() const
{
    return m_first ? 0 : std::chrono::duration<double>(std::chrono::steady_clock::now() - m_sourceTime).count();
}
math::Vec2 EnvironmentRenderer::exposureScales(const DrawList& frame) const
{
    if (m_first)
    {
        return {1, 1};
    }
    const auto& exposure = frame.lighting.outdoor->exposure;
    float current = frame.outdoor->exposureMultiplier;
    return {
        outdoorExposureMultiplier(exposure, m_sunHeights[1 - m_generation]) / current,
        outdoorExposureMultiplier(exposure, m_sunHeights[m_generation]) / current
    };
}
void EnvironmentRenderer::initialize()
{
    if (!m_filterProgram)
    {
        m_filterProgram = lightingProgram(m_device, environmentShader, "ofg_environment_filter");
        m_filterPipeline = lightingPipeline(m_device, m_filterProgram);
        rhi::SamplerDesc desc{};
        desc.addressU = desc.addressV = desc.addressW = TextureAddressingMode::ClampToEdge;
        checkLighting(m_device->createSampler(desc, m_filterSampler.writeRef()), "Create filter sampler");
    }
    if (!m_dynamic)
    {
        auto data = std::make_shared<EnvironmentGpuData>();
        rhi::TextureDesc desc{};
        desc.type = TextureType::TextureCubeArray;
        desc.arrayLength = 6;
        desc.size = {64, 64, 1};
        desc.mipCount = 7;
        desc.format = Format::RGBA16Float;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource | TextureUsage::CopySource;
        desc.defaultState = rhi::ResourceState::ShaderResource;
        checkLighting(
            m_device->createTexture(desc, nullptr, data->cubes.writeRef()),
            "Create dynamic environment array"
        );
        m_dynamic = data;
    }
}
// Captures only values that affect disk-free radiance; exposure and shadow controls do not invalidate IBL.
static std::array<float, 20> environmentSnapshot(const DrawList& frame)
{
    const auto& s = *frame.lighting.outdoor;
    const auto& c = s.clouds;
    const auto& a = s.atmosphere;
    return {
        a.rayleigh,
        a.mie,
        a.ozone,
        a.groundAlbedo,
        a.sunIlluminance,
        a.moonIlluminance,
        c.coverage,
        c.opticalThickness,
        c.altitude,
        c.secondAltitude,
        c.scale,
        c.windSpeed,
        c.windAngle,
        s.dayCycle.noonElevation,
        s.dayCycle.azimuth,
        frame.cameraPosition.x,
        frame.cameraPosition.y,
        frame.cameraPosition.z,
        float(s.dayCycle.timeHours),
        float(s.dayCycle.elapsedSeconds)
    };
}
void EnvironmentRenderer::filterFace(uint32_t kind, uint32_t face)
{
    ComPtr<ICommandEncoder> encoder;
    checkLighting(m_queue->createCommandEncoder(encoder.writeRef()), "Create filter encoder");
    for (uint32_t mip = 0; mip < 7; ++mip)
    {
        uint32_t size = 64 >> mip;
        auto view = lightingView(m_dynamic->cubes, (m_targetGeneration * 3 + kind) * 6 + face, mip);
        ComPtr<IShaderObject> root;
        checkLighting(m_device->createRootShaderObject(m_filterProgram, root.writeRef()), "Create filter bindings");
        auto cursor = ShaderCursor(root);
        const float values[]{float(face), float(kind), mip / 6.0f, float(size)};
        checkLighting(
            cursor["filter"]["faceFilterRoughnessSize"].setData(values, sizeof(values)),
            "Bind filter parameters"
        );
        checkLighting(cursor["sourceEnvironment"].setBinding(m_source), "Bind capture source");
        checkLighting(cursor["filterSampler"].setBinding(m_filterSampler), "Bind filter sampler");
        drawLightingPass(encoder, m_filterPipeline, root, view, size, size);
    }
    submitLighting(m_queue, encoder);
}
void EnvironmentRenderer::update(const DrawList& frame, SkyRenderer& sky)
{
    m_lastPassCount = 0;
    // The BRDF lookup follows the current scene asset even while a frozen capture is in flight.
    prepare(*frame.lighting.environment);
    if (m_dynamic)
    {
        m_dynamic->lookup = frame.lighting.environment->m_gpu->lookup;
    }

    if (!m_building)
    {
        auto snapshot = environmentSnapshot(frame);
        bool force = m_first || !std::equal(snapshot.begin(), snapshot.begin() + 15, m_snapshot.begin()) ||
                     std::abs(snapshot[18] - m_snapshot[18]) > .1f;
        auto now = std::chrono::steady_clock::now();
        double sinceStart = std::chrono::duration<double>(now - m_started).count();
        // A previous generation is still sampled during blending. Never write that slot before it is released.
        if (!m_first && (snapshot == m_snapshot || blend().z < 1 || (!force && sinceStart < .25)))
        {
            return;
        }

        initialize();
        m_dynamic->lookup = frame.lighting.environment->m_gpu->lookup;
        // Do not retain scene geometry/materials for the duration of an environment job.
        m_captureFrame.lighting.outdoor = frame.lighting.outdoor;
        m_captureFrame.lighting.exposure = frame.lighting.exposure;
        m_captureFrame.outdoor = frame.outdoor;
        m_captureFrame.camera = frame.camera;
        m_captureFrame.cameraPosition = frame.cameraPosition;
        sky.beginCapture();
        m_pendingSnapshot = snapshot;
        m_started = now;
        m_pendingSnapBlend = force;
        m_targetGeneration = m_first ? 0 : 1 - m_generation;
        m_step = 0;
        m_building = true;
    }

    // Finish the frozen job even when inputs change. The next job coalesces all edits to the latest frame.
    if (m_step < 6)
    {
        m_source = sky.captureFace(m_captureFrame, m_step);
        m_lastPassCount = 1;
    }
    else
    {
        uint32_t filterFaceIndex = m_step - 6;
        filterFace(filterFaceIndex / 6, filterFaceIndex % 6);
        m_lastPassCount = 7;
    }
    ++m_step;
    if (m_step != 24)
    {
        return;
    }

    // Ordered queue submission makes every face/mip complete before subsequent material draws read this slot.
    m_generation = m_targetGeneration;
    m_sunHeights[m_generation] = m_captureFrame.outdoor->sunDirection.y;
    m_snapshot = m_pendingSnapshot;
    m_sourceTime = m_started;
    m_published = std::chrono::steady_clock::now();
    m_snapBlend = m_first || m_pendingSnapBlend;
    m_first = false;
    m_building = false;
    m_step = 0;
    ++m_publications;
}
} // namespace ofg
