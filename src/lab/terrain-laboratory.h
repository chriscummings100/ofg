// Shared terrain inspection mode owns workers, uploads and cut snapshots; scene entities stay small.
#pragma once

#include "terrain/terrain-streaming-service.h"
#include "render/queue-completion.h"
#include "render/draw-list.h"
#include "scene/scene.h"
#include "platform/http-client.h"

#include <deque>

namespace ofg {
struct TerrainLaunchSettings
{
    std::string serviceUrl, island = "demo";
    bool skipCache = false;
};
// Creates the terrain camera and outdoor lighting without fixture geometry.
std::unique_ptr<Scene> createTerrainScene();

class TerrainLaboratory
{
public:
    // Starts concrete background workers and completion tracking on the shared graphics queue.
    TerrainLaboratory(rhi::IDevice* device, rhi::ICommandQueue* queue, TerrainLaunchSettings launch = {});
    // Native caller drains its graphics queue; browser teardown retains retirement state asynchronously.
    ~TerrainLaboratory();
    // Posts the live observer, adopts complete service selections and prepares bounded uploads.
    void update(Scene& scene);
    // Appends retaining, camera-relative terrain draws and independent shadow casters.
    void append(DrawList& list);
    // Marks all terrain draws submitted by this frame for real-completion retirement.
    void submitted();
    // Draws explicit fault controls and live residency diagnostics in the laboratory UI.
    void panel();
    // Projects the previous rendered cut's node boxes into the ImGui scene rectangle.
    void boundsOverlay(math::Vec4 rectangle);
    // Teleports the global camera; the next update resets its local displacement.
    void teleport(terrain::WorldPosition position);
    // Places the inspection camera above the noise surface at this X/Z address; clearance is in metres.
    void teleportToSurface(terrain::WorldPosition position, double clearance = 8);
    // Frames the published island from above; ordinary regeneration preserves the camera instead.
    void frameIsland();
    // Runs a reproducible moving-observer route with distant teleports every two minutes.
    void traverse(double elapsedSeconds);
    // Returns the controller's current diagnostic snapshot.
    terrain::StreamDiagnostics diagnostics() const { return m_diagnostics; }
    // Reports acquisition/render failures without clearing the last usable terrain selection.
    const std::string& error() const { return m_error; }
    // Reports completed persistent hits, HTTP misses and cache-bypassing acquisitions.
    std::array<size_t, 3> cacheCounts() const { return {m_cacheHits, m_cacheMisses, m_cacheBypasses}; }
    // Returns the worst completed coordinator reconciliation since service startup, in host milliseconds.
    double maximumReconciliationMilliseconds() const { return m_maximumReconciliationMilliseconds; }
    // Returns the age of the adopted selection, including intentional reuse while stationary, in milliseconds.
    double snapshotAgeMilliseconds() const
    {
        return m_snapshotCreated == std::chrono::steady_clock::time_point{}
                   ? 0
                   : std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_snapshotCreated)
                         .count();
    }

private:
    struct SourceTexture
    {
        std::shared_ptr<Texture> texture;
        std::shared_ptr<Texture> validity;
        bool uploaded = false;
    };
    struct RenderPayload
    {
        terrain::ReadyContent content;
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        std::shared_ptr<SourceTexture> texture;
        std::array<int8_t, 7> partSubMeshes{-1, -1, -1, -1, -1, -1, -1};
        uint64_t lastSubmission = 0;
        std::optional<uint64_t> retireAfter;
    };
    struct Shutdown;
    // Converts service output into renderer-owned mesh records, then adopts the latest complete selection.
    void receiveResults();
    // Advances bounded uploads and releases retired resources after actual GPU completion.
    void upload();
    // Advances bounded startup HTTP without blocking the render thread; failures remain visible until retry.
    void connect();
    // Starts/retries fetching the mutable revision pointer through the same asynchronous transport.
    void requestManifest();

    terrain::StreamSettings m_settings;
    TerrainLaunchSettings m_launch;
    std::shared_ptr<const terrain::TerrainManifest> m_manifest;
    std::unique_ptr<HttpRequest> m_manifestRequest;
    std::string m_requestedRevision;
    std::map<const terrain::TerrainTile*, std::weak_ptr<SourceTexture>> m_sourceTextures;
    size_t m_cacheHits = 0, m_cacheMisses = 0, m_cacheBypasses = 0;
    std::string m_cacheWarning;
    int m_terrainChannel = 0;
    std::unique_ptr<terrain::TerrainStreamingService> m_service;
    std::unique_ptr<QueueCompletion> m_completion;
    std::map<terrain::RequestId, RenderPayload> m_payloads;
    std::deque<terrain::RequestId> m_uploads;
    terrain::StreamDiagnostics m_diagnostics;
    uint64_t m_revision = 0;
    double m_reconciliationMilliseconds = 0;
    double m_maximumReconciliationMilliseconds = 0;
    std::chrono::steady_clock::time_point m_snapshotCreated;
    terrain::GeneratorSettings m_generator;
    terrain::WorldPosition m_camera{}, m_observer{};
    std::deque<std::pair<uint64_t, size_t>> m_staging;
    std::vector<terrain::CutEntry> m_snapshot;
    std::shared_ptr<Material> m_material;
    std::shared_ptr<Mesh> m_seaMesh;
    bool m_seaReference = true;
    std::array<std::shared_ptr<Material>, 14> m_lodMaterials;
    bool m_freeze = false, m_lodColors = false, m_holdNext = false, m_failNext = false, m_bounds = false;
    math::Mat4 m_clipFromWorld = math::mat4Identity();
    bool m_teleported = true;
    bool m_frameIsland = false;
    double m_updateMilliseconds = 0;
    std::string m_error;
};
} // namespace ofg
