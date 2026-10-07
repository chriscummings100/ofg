// Main-thread terrain adapter: immutable worker values become prepared snapshots with explicit GPU lifetime.
#include "terrain/terrain-profile.h"
#include "lab/terrain-laboratory.h"
#include "lab/pbr-fixture.h"
#include "resources/pbr-material.h"
#include "render/graphics.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace ofg {
namespace {
// Fits seven noise LOD bands plus complete sibling reservations; worker/transport allowance is separate.
terrain::StreamSettings runtimeSettings()
{
    terrain::StreamSettings settings;
    settings.cpuBudget = 256ull << 20;
    settings.maximumPayloadBytes = 4ull << 20;
    settings.maximumJobs = 32;
    settings.maximumPlansPerUpdate = 2;
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

TerrainLaboratory::TerrainLaboratory(rhi::IDevice* device, rhi::ICommandQueue* queue)
    : m_stream(runtimeSettings())
    , m_completion(device, queue)
{
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
        desc.baseColor = colors[i];
        m_lodMaterials[i] = createPbrMaterial(desc);
    }
}

TerrainLaboratory::~TerrainLaboratory() = default;

void TerrainLaboratory::teleport(terrain::WorldPosition position)
{
    m_camera = terrain::normalizePosition(position, 1024);
    m_teleported = true;
}

void TerrainLaboratory::teleportToSurface(terrain::WorldPosition position, double clearance)
{
    position = terrain::normalizePosition(position, 1024);
    position.cell.y = 0;
    position.local[1] =
        terrain::terrainHeight(position.cell, position.local[0], position.local[2], m_generator) + clearance;
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

void TerrainLaboratory::updateRoots()
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_updateRoots);
    m_admissionBlocked = false;
    // Candidate offsets are small: global cell arithmetic stays exact before conversion to metres.
    for (int z = -4; z <= 4; ++z)
    {
        for (int y = -4; y <= 4; ++y)
        {
            for (int x = -4; x <= 4; ++x)
            {
                terrain::WorldPosition candidate = m_observer;
                candidate.local[0] += x * 1024;
                candidate.local[1] += y * 1024;
                candidate.local[2] += z * 1024;
                auto cell = terrain::normalizePosition(candidate, 1024).cell;
                // This height generator certifies every other root as air/solid, so the sparse grid omits them.
                if (cell.y < -1 || cell.y > 0)
                {
                    continue;
                }
                if (terrain::distanceToNode({cell}, m_observer, 1024) <= 4096 && !m_roots.contains(cell))
                {
                    if (m_stream.requestRoot(cell))
                    {
                        m_roots.insert(cell);
                    }
                    else
                    {
                        m_admissionBlocked = true;
                    }
                }
            }
        }
    }
    for (auto i = m_roots.begin(); i != m_roots.end();)
    {
        if (terrain::distanceToNode({*i}, m_observer, 1024) > 5120)
        {
            m_stream.withdrawRoot(*i);
            i = m_roots.erase(i);
        }
        else
        {
            ++i;
        }
    }
}

void TerrainLaboratory::receiveResults()
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_receiveResults);
    for (auto& result : m_workers.takeResults())
    {
        if (result.outcome == terrain::WorkerOutcome::Cancelled)
        {
            m_stream.acknowledgeCancellation(result.request.id);
            continue;
        }
        if (result.outcome == terrain::WorkerOutcome::Failed)
        {
            m_error = result.error;
            m_failures.push_back(result.request.address);
            m_stream.fail(result.request.id, result.error);
            continue;
        }
        try
        {
            const auto& address = result.request.address;
            terrain::validateGeometry(result.geometry, 1024.0 / (1u << address.depth), result.request.byteLimit);
            auto payload = std::make_shared<terrain::PreparedPayload>();
            payload->empty = result.geometry.indices.empty();
            payload->certifiedEmpty = result.geometry.certifiedEmpty;
            if (payload->empty)
            {
                m_stream.complete(result.request.id, std::move(payload));
                continue;
            }
            payload->cpuBytes = result.geometry.allocatedBytes();
            payload->gpuBytes =
                result.geometry.vertices.size() * sizeof(Vertex) + result.geometry.indices.size() * sizeof(uint32_t);
            if (!m_stream.acceptGenerated(result.request.id, payload->cpuBytes))
            {
                continue;
            }
            std::vector<SubMesh> parts;
            for (size_t part = 0; part < 7; ++part)
            {
                const auto first = result.geometry.partOffsets[part];
                const auto count = result.geometry.partOffsets[part + 1] - first;
                if (count)
                {
                    payload->partSubMeshes[part] = int8_t(parts.size());
                    parts.push_back({first, count, m_material});
                }
            }
            payload->mesh =
                Mesh::create(std::move(result.geometry.vertices), std::move(result.geometry.indices), std::move(parts));
            m_uploads.push_back({result.request, std::move(payload)});
        } catch (const std::exception& error)
        {
            m_error = error.what();
            m_failures.push_back(result.request.address);
            m_stream.fail(result.request.id, m_error, terrain::FailureStage::Upload);
        }
    }
}

void TerrainLaboratory::upload()
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_upload);
    const auto completed = m_completion.completed();
    m_stream.completedSubmission(completed);
    while (!m_staging.empty() && m_staging.front().first <= completed)
    {
        m_staging.pop_front();
    }
    size_t staging = 0;
    for (const auto& item : m_staging)
    {
        staging += item.second;
    }
    size_t allowance = std::min(size_t(4 << 20), size_t(16 << 20) - staging);
    for (auto i = m_uploads.begin(); i != m_uploads.end();)
    {
        if (m_stream.request(i->request.address) != i->request.id)
        {
            i->payload->gpuBytes = Graphics::streamingMeshGpuBytes(*i->payload->mesh);
            m_stream.complete(i->request.id, i->payload); // Obsolete payload enters retirement, never the cut.
            i = m_uploads.erase(i);
            continue;
        }
        if (!allowance)
        {
            break;
        }
        const auto before = allowance;
        bool ready = false;
        std::string error;
        try
        {
            ready = Graphics::prepareStreamingMesh(*i->payload->mesh, allowance);
        } catch (const std::exception& failure)
        {
            error = failure.what();
        }
        // Even a failed preparation can have submitted an earlier chunk.
        if (before != allowance)
        {
            i->payload->lastSubmission = m_completion.mark();
            m_stream.submitted({{i->request.address, i->payload}}, i->payload->lastSubmission);
            m_staging.push_back({i->payload->lastSubmission, before - allowance});
        }
        if (!error.empty())
        {
            i->payload->gpuBytes = Graphics::streamingMeshGpuBytes(*i->payload->mesh);
            m_error = error;
            m_failures.push_back(i->request.address);
            m_stream.failUpload(i->request.id, i->payload, error);
            i = m_uploads.erase(i);
        }
        else if (ready)
        {
            m_stream.complete(i->request.id, i->payload);
            i = m_uploads.erase(i);
        }
        else
        {
            ++i;
        }
    }
}

void TerrainLaboratory::update(Scene& scene)
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_update);
    const auto updateStart = std::chrono::steady_clock::now();
    auto* camera = scene.activeCamera();
    if (!camera)
    {
        throw EngineError("Terrain laboratory requires its inspection camera.");
    }
    const auto displacement = camera->entity()->localTransform().position;
    if (!m_teleported)
    {
        m_camera.local[0] += displacement.x;
        m_camera.local[1] += displacement.y;
        m_camera.local[2] += displacement.z;
        m_camera = terrain::normalizePosition(m_camera, 1024);
    }
    m_teleported = false;
    camera->entity()->setLocalPosition({});
    if (!m_freeze)
    {
        m_observer = m_camera;
    }
    m_stream.setObserver(m_observer);
    updateRoots();
    m_stream.update();
    for (auto id : m_stream.takeCancellations())
    {
        const auto upload = std::find_if(
            m_uploads.begin(),
            m_uploads.end(),
            [id](const Upload& value)
            {
                return value.request.id == id;
            }
        );
        if (upload == m_uploads.end())
        {
            m_workers.cancel(id);
        }
    }
    receiveResults();
    upload();
    m_stream.update();
    for (auto request : m_stream.takeRequests())
    {
        m_workers.submit(request, m_generator, m_holdNext, m_failNext);
        m_holdNext = m_failNext = false;
    }
    m_snapshot = m_stream.cut();
    m_updateMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
}

void TerrainLaboratory::append(DrawList& list)
{
    OFG_TERRAIN_SCOPE(TerrainLaboratory_append);
    m_clipFromWorld = list.clipFromWorld;
    list.atmosphereHeight = float(std::clamp(double(m_camera.cell.y) * 1024 + m_camera.local[1], -1000.0, 1000000.0));
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
        const auto minimum = terrain::relativeMinimum(entry.address, m_camera, 1024);
        auto transform = math::mat4Identity();
        transform[3] = {float(minimum[0]), float(minimum[1]), float(minimum[2]), 1};
        DrawItem
            item{entry.payload->mesh, 0, m_lodColors ? m_lodMaterials[entry.address.depth] : m_material, transform, {}};
        const bool visible = boundsVisible(item.mesh->bounds(), math::mul(list.clipFromWorld, transform));
        for (unsigned part = 0; part < 7; ++part)
        {
            const auto subMesh = entry.payload->partSubMeshes[part];
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
}

void TerrainLaboratory::submitted()
{
    m_stream.submitted(m_snapshot, m_completion.mark());
}

} // namespace ofg
