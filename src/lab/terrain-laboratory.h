// Shared terrain inspection mode owns workers, uploads and cut snapshots; scene entities stay small.
#pragma once

#include "terrain/terrain-workers.h"
#include "render/queue-completion.h"
#include "render/draw-list.h"
#include "scene/scene.h"

#include <deque>

namespace ofg {
// Creates the terrain camera and outdoor lighting without fixture geometry.
std::unique_ptr<Scene> createTerrainScene();

class TerrainLaboratory
{
public:
    // Starts concrete background workers and completion tracking on the shared graphics queue.
    TerrainLaboratory(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Native caller drains the graphics queue before destruction; worker destruction joins CPU tasks.
    ~TerrainLaboratory();
    // Reconciles camera motion, demand, worker results and bounded uploads on the application thread.
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
    // Runs a reproducible moving-observer route with distant teleports every two minutes.
    void traverse(double elapsedSeconds);
    // Returns the controller's current diagnostic snapshot.
    terrain::StreamDiagnostics diagnostics() const { return m_stream.diagnostics(); }

private:
    struct Upload
    {
        terrain::BuildRequest request;
        std::shared_ptr<terrain::PreparedPayload> payload;
    };
    // Reconciles a spherical root demand with a larger retention radius.
    void updateRoots();
    // Converts terminal worker messages into validated empty payloads or queued geometry uploads.
    void receiveResults();
    // Advances at most four MiB of uploads while limiting incomplete staging to sixteen MiB.
    void upload();

    terrain::TerrainStream m_stream;
    terrain::TerrainWorkers m_workers;
    QueueCompletion m_completion;
    terrain::GeneratorSettings m_generator;
    terrain::WorldPosition m_camera{}, m_observer{};
    std::set<terrain::CellAddress> m_roots;
    std::deque<Upload> m_uploads;
    std::deque<std::pair<uint64_t, size_t>> m_staging;
    std::vector<terrain::CutEntry> m_snapshot;
    std::vector<terrain::ContentKey> m_failures;
    std::shared_ptr<Material> m_material;
    std::array<std::shared_ptr<Material>, 7> m_lodMaterials;
    bool m_freeze = false, m_lodColors = false, m_holdNext = false, m_failNext = false, m_bounds = false;
    math::Mat4 m_clipFromWorld = math::mat4Identity();
    bool m_teleported = true;
    bool m_admissionBlocked = false;
    double m_updateMilliseconds = 0;
    std::string m_error;
};
} // namespace ofg
