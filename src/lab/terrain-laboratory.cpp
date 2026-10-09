// Main-thread terrain adapter: immutable worker values become prepared snapshots with explicit GPU lifetime.
#include "terrain/terrain-profile.h"
#include "lab/terrain-laboratory.h"
#include "lab/pbr-fixture.h"
#include "resources/pbr-material.h"
#include "render/graphics.h"
#include "core/engine-error.h"
#include "terrain/terrain-shading.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace ofg {
namespace {
// A 131 km root reaches 16 m at depth 13; refinement/view distance is independent of world extent.
terrain::StreamSettings runtimeSettings()
{
    terrain::StreamSettings settings;
    settings.rootWidth = 131072;
    settings.maximumDepth = 13;
    settings.maximumNodes = 32768;
    settings.refinementRange = 4096;
    settings.cpuBudget = 256ull << 20;
    settings.maximumPayloadBytes = 4ull << 20;
    settings.maximumJobs = 32;
    return settings;
}
} // namespace

std::unique_ptr<Scene> createTerrainScene()
{
    auto scene = std::make_unique<Scene>();
    scene->getRoot()->setName("Terrain Laboratory");
    scene->lighting.hdr = true;
    scene->lighting.environment = createStudioEnvironment();
    scene->lighting.outdoor = OutdoorLighting{};
    auto entity = scene->createEntity(scene->getRoot());
    entity->setName("Terrain camera (relative origin)");
    auto camera = scene->createCamera(entity);
    camera->setPerspective(1.04719755f, .5f, 16000);
    scene->setActiveCamera(camera);
    return scene;
}

TerrainLaboratory::TerrainLaboratory(rhi::IDevice* device, rhi::ICommandQueue* queue, TerrainLaunchSettings launch)
    : m_settings(runtimeSettings())
    , m_launch(std::move(launch))
    , m_completion(std::make_unique<QueueCompletion>(device, queue))
{
    m_generator.rootWidth = m_settings.rootWidth;
    if (m_launch.serviceUrl.empty())
        m_service = std::make_unique<terrain::TerrainStreamingService>(m_settings, m_generator);
    else
    {
        // The service adds retained source arrays and CPU texture images to the geometry working set.
        m_settings.cpuBudget += 256ull << 20;
        while (m_launch.serviceUrl.ends_with('/'))
            m_launch.serviceUrl.pop_back();
        if (m_launch.serviceUrl.ends_with("/v1"))
            m_launch.serviceUrl.resize(m_launch.serviceUrl.size() - 3);
        if (m_launch.island.empty() || m_launch.island.size() > 64 ||
            m_launch.island.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
                std::string::npos)
            throw EngineError("Invalid terrain island name.");
        requestManifest();
    }
    teleportToSurface({{}, {0, 0, -300}});
    m_observer = m_camera;
    PbrMaterialDesc desc;
    desc.baseColor = {.27f, .4f, .19f, 1};
    desc.metallic = 0;
    desc.roughness = .9f;
    m_material = createPbrMaterial(desc);
    const math::Vec4 colors[] = {
        {.8f, .2f, .2f, 1},
        {.8f, .5f, .1f, 1},
        {.7f, .8f, .1f, 1},
        {.1f, .7f, .3f, 1},
        {.1f, .5f, .9f, 1},
        {.6f, .2f, .9f, 1},
        {.9f, .2f, .7f, 1}
    };
    for (size_t i = 0; i < m_lodMaterials.size(); ++i)
    {
        desc.baseColor = colors[i % 7];
        m_lodMaterials[i] = createPbrMaterial(desc);
    }
    if (!m_launch.serviceUrl.empty())
    {
        PbrMaterialDesc sea;
        sea.baseColor = {.025f, .18f, .23f, 1};
        sea.metallic = 0;
        sea.unlit = true;
        // Camera-centred 500 m patches avoid depth interpolation artifacts from 131 km clipped triangles.
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        for (int z = 0; z <= 64; ++z)
            for (int x = 0; x <= 64; ++x)
                vertices.push_back({{float(x * 500 - 16000), 0, float(z * 500 - 16000)}, {0, 1, 0}});
        for (uint32_t z = 0; z < 64; ++z)
            for (uint32_t x = 0; x < 64; ++x)
            {
                const uint32_t a = z * 65 + x;
                indices.insert(indices.end(), {a, a + 66, a + 1, a, a + 65, a + 66});
            }
        m_seaMesh = Mesh::create(std::move(vertices), std::move(indices), {{0, 64 * 64 * 6, createPbrMaterial(sea)}});
    }
}

struct TerrainLaboratory::Shutdown
{
    std::unique_ptr<terrain::TerrainStreamingService> service;
    std::unique_ptr<QueueCompletion> completion;
    std::map<terrain::RequestId, RenderPayload> payloads;
    std::shared_ptr<Mesh> seaMesh;
    uint64_t lastSubmission = 0;

    // Runs only on the application thread, allowing CPU shutdown and GPU retirement to progress independently.
    bool step()
    {
        auto batch = service->takeBatch();
        for (auto& upload : batch.uploads)
        {
            service->uploadFailed(upload.request.id, "Terrain shutdown cancelled queued upload.");
            service->payloadReleased(upload.request.id);
        }
        for (const auto& [id, revision] : batch.retirements)
        {
            if (!payloads.contains(id))
                service->payloadReleased(id);
        }
        const auto completed = completion->completed();
        for (auto i = payloads.begin(); i != payloads.end();)
        {
            if (i->second.lastSubmission <= completed)
            {
                const auto id = i->first;
                i = payloads.erase(i);
                service->payloadReleased(id);
            }
            else
                ++i;
        }
        return batch.stopped && payloads.empty() && completed >= lastSubmission;
    }
#ifdef __EMSCRIPTEN__
    // Browser teardown retains only CPU channel/GPU retirement ownership; no callback touches a destroyed lab.
    static void tick(void* pointer)
    {
        auto* state = static_cast<Shutdown*>(pointer);
        if (state->step())
            delete state;
        else
            emscripten_async_call(tick, state, 16);
    }
#endif
};

TerrainLaboratory::~TerrainLaboratory()
{
    if (!m_service)
        return;
    m_service->stop();
    auto shutdown = std::make_unique<Shutdown>();
    shutdown->lastSubmission = m_completion->mark();
    shutdown->seaMesh = std::move(m_seaMesh);
    shutdown->service = std::move(m_service);
    shutdown->completion = std::move(m_completion);
    shutdown->payloads = std::move(m_payloads);
#ifdef __EMSCRIPTEN__
    emscripten_async_call(Shutdown::tick, shutdown.release(), 0);
#else
    for (;;)
    {
        const auto sequence = shutdown->service->wakeSequence();
        if (shutdown->step())
            break;
        shutdown->service->waitForChange(sequence);
    }
#endif
}

void TerrainLaboratory::teleport(terrain::WorldPosition position)
{
    m_camera = terrain::normalizePosition(position, m_settings.rootWidth);
    m_teleported = true;
}

void TerrainLaboratory::teleportToSurface(terrain::WorldPosition position, double clearance)
{
    position = terrain::normalizePosition(position, m_settings.rootWidth);
    position.cell.y = 0;
    position.local[1] =
        (m_manifest ? m_manifest->maximumHeight
                    : terrain::terrainHeight(position.cell, position.local[0], position.local[2], m_generator)) +
        clearance;
    teleport(position);
}

void TerrainLaboratory::traverse(double elapsedSeconds)
{
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0)
    {
        throw EngineError("Terrain traversal requires finite nonnegative elapsed seconds.");
    }
    const auto distant = int64_t(std::fmod(elapsedSeconds / 120, 2)) ? -1000000000ll : 0ll;
    teleportToSurface({{distant, 0, distant}, {elapsedSeconds * 96, 0, 2048 * std::sin(elapsedSeconds * .03)}}, 8);
}

void TerrainLaboratory::frameIsland()
{
    if (!m_manifest)
        return;
    auto position = m_manifest->sourceOrigin;
    position.local[0] += m_manifest->sourceWidth * .5;
    position.local[2] += m_manifest->sourceWidth * .25;
    teleportToSurface(position, m_manifest->sourceWidth * .25);
    m_frameIsland = true;
}

void TerrainLaboratory::receiveResults()
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_receiveResults);
    auto batch = m_service->takeBatch();
    m_diagnostics = batch.diagnostics;
    m_reconciliationMilliseconds = batch.reconciliationMilliseconds;
    m_maximumReconciliationMilliseconds = batch.maximumReconciliationMilliseconds;
    m_cacheHits = batch.cacheHits;
    m_cacheMisses = batch.cacheMisses;
    m_cacheBypasses = batch.cacheBypasses;
    if (!batch.cacheWarning.empty())
        m_cacheWarning = std::move(batch.cacheWarning);
    if (!batch.error.empty())
        m_error = batch.error;
    for (auto& result : batch.uploads)
    {
        auto& payload = m_payloads[result.request.id];
        payload.content.id = result.request.id;
        payload.content.cpuBytes = result.geometry.allocatedBytes();
        payload.content.source = result.source;
        payload.content.gpuBytes =
            result.geometry.vertices.size() * sizeof(Vertex) + result.geometry.indices.size() * sizeof(uint32_t);
        try
        {
            payload.material = m_material;
            if (result.source)
            {
                auto& weak = m_sourceTextures[result.source.get()];
                payload.texture = weak.lock();
                if (!payload.texture)
                {
                    payload.texture = std::make_shared<SourceTexture>();
                    const auto pixels = terrain::encodeTerrainTexture(*result.source);
                    payload.texture->texture = Texture::create(
                        {terrain::terrainDataSamples, terrain::terrainDataSamples, TextureFormat::RGBA16Float},
                        {std::as_bytes(std::span(pixels))}
                    );
                    payload.texture->validity = Texture::create(
                        {terrain::terrainDataSamples, terrain::terrainDataSamples, TextureFormat::R8Unorm},
                        {std::as_bytes(std::span(result.source->validity))}
                    );
                    weak = payload.texture;
                }
                payload.content.sourceGpuBytes = terrain::terrainTextureBytes;
                PbrMaterialDesc material;
                material.metallic = 0;
                material.roughness = .9f;
                material.terrainTexture = payload.texture->texture;
                material.terrainValidity = payload.texture->validity;
                const auto uv = terrain::terrainTextureMapping(*result.source, result.request.address);
                material.terrainMapping = {uv[0], uv[1], uv[2], uv[3]};
                material.terrainSpacing = float(std::ldexp(512.0, -result.source->address.depth));
                payload.material = createPbrMaterial(material);
                payload.material->setUniform("terrainChannel", uint32_t(m_terrainChannel));
            }
            std::vector<SubMesh> parts;
            for (size_t part = 0; part < 7; ++part)
            {
                const auto first = result.geometry.partOffsets[part];
                const auto count = result.geometry.partOffsets[part + 1] - first;
                if (count)
                {
                    payload.partSubMeshes[part] = int8_t(parts.size());
                    parts.push_back({first, count, payload.material});
                }
            }
            payload.mesh =
                Mesh::create(std::move(result.geometry.vertices), std::move(result.geometry.indices), std::move(parts));
            m_uploads.push_back(result.request.id);
        } catch (const std::exception& error)
        {
            m_error = error.what();
            payload.retireAfter = 0;
            m_service->uploadFailed(result.request.id, m_error);
        }
    }
    if (batch.snapshot)
    {
        m_snapshot = batch.snapshot->leaves;
        m_revision = batch.snapshot->revision;
        m_snapshotCreated = batch.snapshot->created;
    }
    for (const auto& [id, revision] : batch.retirements)
    {
        const auto record = m_payloads.find(id);
        if (record != m_payloads.end())
            record->second.retireAfter = revision;
        else
            m_service->payloadReleased(id);
    }
}

void TerrainLaboratory::upload()
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_upload);
    const auto completed = m_completion->completed();
    while (!m_staging.empty() && m_staging.front().first <= completed)
        m_staging.pop_front();
    size_t staging = 0;
    for (const auto& item : m_staging)
        staging += item.second;
    size_t allowance = std::min(size_t(4 << 20), size_t(16 << 20) - staging);
    for (auto i = m_uploads.begin(); i != m_uploads.end();)
    {
        auto& payload = m_payloads.at(*i);
        if (payload.retireAfter)
        {
            i = m_uploads.erase(i);
            continue;
        }
        if (!allowance)
        {
            // Still visit later cancelled uploads before the retirement pass destroys their records.
            ++i;
            continue;
        }
        const auto before = allowance;
        bool ready = false;
        std::string error;
        try
        {
            if (payload.texture && !payload.texture->uploaded)
            {
                if (allowance < terrain::terrainTextureUploadBytes)
                {
                    ++i;
                    continue;
                }
                allowance -= terrain::terrainTextureUploadBytes;
                Graphics::prepareMaterial(*payload.material);
                payload.texture->uploaded = true;
            }
            ready = Graphics::prepareStreamingMesh(*payload.mesh, allowance);
        } catch (const std::exception& failure)
        {
            error = failure.what();
        }
        if (before != allowance)
        {
            payload.lastSubmission = m_completion->mark();
            m_staging.push_back({payload.lastSubmission, before - allowance});
        }
        if (!error.empty())
        {
            m_error = error;
            payload.retireAfter = 0;
            m_service->uploadFailed(*i, error);
            i = m_uploads.erase(i);
        }
        else if (ready)
        {
            m_service->uploadReady(*i, payload.content);
            i = m_uploads.erase(i);
        }
        else
            ++i;
    }
    std::set<terrain::RequestId> active;
    for (const auto& entry : m_snapshot)
        active.insert(entry.payload->id);
    for (auto i = m_payloads.begin(); i != m_payloads.end();)
    {
        const auto& payload = i->second;
        if (payload.retireAfter && *payload.retireAfter <= m_revision && !active.contains(i->first) &&
            payload.lastSubmission <= completed)
        {
            const auto id = i->first;
            i = m_payloads.erase(i);
            m_service->payloadReleased(id);
        }
        else
            ++i;
    }
    std::erase_if(
        m_sourceTextures,
        [](const auto& entry)
        {
            return entry.second.expired();
        }
    );
}

void TerrainLaboratory::requestManifest()
{
    m_requestedRevision.clear();
    m_manifestRequest =
        HttpRequest::start(m_launch.serviceUrl + "/v1/islands/" + m_launch.island + "/manifest", 256 << 10, true);
}

void TerrainLaboratory::connect()
{
    if (!m_manifestRequest)
        return;
    auto response = m_manifestRequest->take();
    if (!response)
        return;
    m_manifestRequest.reset();
    try
    {
        if (!response->error.empty())
            throw EngineError(response->error);
        if (response->status != 200)
            throw EngineError("Terrain manifest HTTP status " + std::to_string(response->status));
        const std::string_view json(reinterpret_cast<const char*>(response->bytes.data()), response->bytes.size());
        if (m_requestedRevision.empty())
        {
            m_requestedRevision = terrain::decodeTerrainRevision(json);
            m_manifestRequest = HttpRequest::start(
                m_launch.serviceUrl + "/v1/islands/" + m_launch.island + "/revisions/" + m_requestedRevision +
                    "/manifest",
                256 << 10,
                m_launch.skipCache
            );
            return;
        }
        auto manifest = std::make_shared<terrain::TerrainManifest>(terrain::decodeTerrainManifest(json));
        if (manifest->revision != m_requestedRevision || manifest->island != m_launch.island)
            throw EngineError("Terrain manifest identity mismatch.");
        terrain::TerrainSourceSettings
            source{m_launch.serviceUrl, "artifacts/terrain-cache", manifest, m_launch.skipCache};
        m_service =
            std::make_unique<terrain::TerrainStreamingService>(m_settings, m_generator, 4096, std::move(source));
        m_manifest = std::move(manifest);
        auto position = m_manifest->sourceOrigin;
        position.local[0] += m_manifest->sourceWidth * .5;
        position.local[2] += m_manifest->sourceWidth * .5 - 1800;
        teleportToSurface(position, 100);
        m_error.clear();
    } catch (const std::exception& error)
    {
        m_error = error.what();
    }
}

void TerrainLaboratory::update(Scene& scene)
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_update);
    const auto updateStart = std::chrono::steady_clock::now();
    connect();
    auto* camera = scene.activeCamera();
    if (!camera)
    {
        throw EngineError("Terrain laboratory requires its inspection camera.");
    }
    if (m_frameIsland)
    {
        std::string error;
        const auto rotation = math::quatLookAtLh({}, {0, -1, 1}, {0, 1, 0}, error);
        if (!rotation)
            throw EngineError(error);
        camera->entity()->setLocalRotation(*rotation);
        m_frameIsland = false;
    }
    const auto displacement = camera->entity()->localTransform().position;
    if (!m_teleported)
    {
        m_camera.local[0] += displacement.x;
        m_camera.local[1] += displacement.y;
        m_camera.local[2] += displacement.z;
        m_camera = terrain::normalizePosition(m_camera, m_settings.rootWidth);
    }
    m_teleported = false;
    camera->entity()->setLocalPosition({});
    if (!m_freeze)
    {
        m_observer = m_camera;
    }
    if (!m_service)
        return;
    m_service->setObserver(m_observer);
    if (m_holdNext || m_failNext)
    {
        m_service->injectNext(m_holdNext, m_failNext);
        m_holdNext = m_failNext = false;
    }
    receiveResults();
    upload();
    m_updateMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
}

void TerrainLaboratory::append(DrawList& list)
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_append);
    m_clipFromWorld = list.clipFromWorld;
    list.atmosphereHeight =
        float(std::clamp(double(m_camera.cell.y) * m_settings.rootWidth + m_camera.local[1], -1000.0, 1000000.0));
    if (list.lighting.outdoor)
    {
        list.outdoor = evaluateOutdoorLighting(*list.lighting.outdoor, list.atmosphereHeight);
        list.lighting.lights[0].intensity = list.outdoor->illuminance;
        list.lighting.lights[0].color = list.outdoor->lightColor;
    }
    for (const auto& entry : m_snapshot)
    {
        if (entry.payload->empty)
        {
            continue;
        }
        const auto& payload = m_payloads.at(entry.payload->id);
        const auto minimum = terrain::relativeMinimum(entry.address, m_camera, m_settings.rootWidth);
        auto transform = math::mat4Identity();
        transform[3] = {float(minimum[0]), float(minimum[1]), float(minimum[2]), 1};
        DrawItem
            item{payload.mesh, 0, m_lodColors ? m_lodMaterials[entry.address.depth] : payload.material, transform, {}};
        const bool visible = boundsVisible(item.mesh->bounds(), math::mul(list.clipFromWorld, transform));
        for (unsigned part = 0; part < 7; ++part)
        {
            const auto subMesh = payload.partSubMeshes[part];
            if (subMesh < 0 || (part && !(entry.transitionFaces & (1u << (part - 1)))))
            {
                continue;
            }
            item.subMeshIndex = uint32_t(subMesh);
            list.shadowCasters.push_back(item);
            if (visible)
            {
                list.items.push_back(item);
            }
        }
    }
    if (m_manifest && m_seaReference && m_seaMesh && !m_snapshot.empty())
    {
        auto transform = math::mat4Identity();
        transform[3].y =
            float(m_manifest->seaLevel - double(m_camera.cell.y) * m_settings.rootWidth - m_camera.local[1]);
        if (boundsVisible(m_seaMesh->bounds(), math::mul(list.clipFromWorld, transform)))
            list.items.push_back({m_seaMesh, 0, m_seaMesh->subMeshes()[0].material, transform, {}});
    }
}

void TerrainLaboratory::submitted()
{
    const auto serial = m_completion->mark();
    for (const auto& entry : m_snapshot)
    {
        if (!entry.payload->empty)
            m_payloads.at(entry.payload->id).lastSubmission = serial;
    }
}

} // namespace ofg
