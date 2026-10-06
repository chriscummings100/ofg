// Bounded native/browser worker adapters exchange immutable requests and owned CPU results with the application.
#pragma once

#include "terrain/terrain-generator.h"
#include "terrain/terrain-stream.h"

#include <chrono>

namespace ofg::terrain {
enum class WorkerOutcome : uint32_t
{
    Geometry,
    Cancelled,
    Failed
};
struct WorkerResult
{
    BuildRequest request;
    TerrainGeometry geometry;
    WorkerOutcome outcome = WorkerOutcome::Geometry;
    std::string error;
    uint32_t worker = 0;
};

class TerrainWorkers
{
public:
    // Starts two persistent CPU workers; queued plus running plus unclaimed results are limited to 64.
    TerrainWorkers();
    // Cancels outstanding work and stops owned workers; native destruction joins outside frame submission.
    ~TerrainWorkers();
    TerrainWorkers(const TerrainWorkers&) = delete;
    TerrainWorkers& operator=(const TerrainWorkers&) = delete;
    // Queues a value request; hold/fail are explicit laboratory fault-injection controls.
    void submit(BuildRequest request, GeneratorSettings generator, bool hold = false, bool fail = false);
    // Cancels a queued/running request; a terminal result acknowledges its released worker ownership.
    void cancel(RequestId id);
    // Releases laboratory-held jobs without altering their identity or normal ordering.
    void releaseHeld();
    // Transfers completed results to the application thread without waiting for generation.
    std::vector<WorkerResult> takeResults();
#ifndef __EMSCRIPTEN__
    // Native test/offline convenience only; application frames must use nonblocking takeResults.
    bool waitForResult(std::chrono::milliseconds timeout);
#endif

private:
    struct State;
    std::unique_ptr<State> m_state;
};
} // namespace ofg::terrain
