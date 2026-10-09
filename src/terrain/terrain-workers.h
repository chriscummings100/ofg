// Shared C++ workers exchange immutable requests and owned CPU results on native and browser hosts.
#pragma once

#include "terrain/terrain-generator.h"
#include "terrain/terrain-stream.h"
#include "terrain/streaming-wake.h"

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
    explicit TerrainWorkers(std::shared_ptr<StreamingWake> wake = {});
    // Cancels work; native joins, browser workers finish independently while retaining their shared CPU state.
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
    // Test/offline convenience; never call on the browser main thread. Frames use nonblocking takeResults.
    bool waitForResult(std::chrono::milliseconds timeout);

private:
    struct State;
    std::shared_ptr<State> m_state;
};
} // namespace ofg::terrain
