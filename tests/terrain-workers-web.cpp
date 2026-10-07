// Focused browser proof of the shared C++ worker contract; the page polls without blocking its UI thread.
#include "terrain/terrain-workers.h"
#include "core/engine-error.h"

#include <emscripten.h>

using namespace ofg::terrain;

namespace {
std::unique_ptr<TerrainWorkers> workers;
GeneratorSettings settings;
unsigned stage = 0;
uint32_t firstWorker = 0;

// Fails the browser proof with an observable diagnostic rather than continuing after a broken contract.
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw ofg::EngineError(message);
    }
}

// Advances only after actual terminal results, exercising independent progress and the real geometry buffers.
void poll()
{
    try
    {
        auto results = workers->takeResults();
        if (results.empty())
        {
            return;
        }
        require(results.size() == 1, "Unexpected result count");
        const auto& result = results.front();
        require(result.request.id.epoch == 0x123456789abcdef0ull, "Request identity lost precision");
        if (stage == 0)
        {
            require(
                result.request.id.sequence == 2 && result.outcome == WorkerOutcome::Geometry,
                "Held job blocked independent completion"
            );
            require(!result.geometry.indices.empty(), "No generated geometry");
            validateGeometry(result.geometry, settings.rootWidth, 4 << 20);
            require(
                result.geometry.partOffsets.back() > result.geometry.partOffsets[1],
                "Prebuilt transition faces are missing"
            );
            firstWorker = result.worker;
            EM_ASM(
                {
                    Module.workerProof = {};
                    Module.workerProof.stage = 1;
                    Module.workerProof.shared = HEAPU8.buffer instanceof SharedArrayBuffer;
                    Module.workerProof.indices = $0;
                    Module.workerProof.vertices = $1;
                },
                result.geometry.indices.size(),
                result.geometry.vertices.size()
            );
            workers->cancel({0x123456789abcdef0ull, 1});
        }
        else if (stage == 1)
        {
            require(
                result.request.id.sequence == 1 && result.outcome == WorkerOutcome::Cancelled,
                "Cancellation did not acknowledge held job"
            );
            require(
                result.worker != firstWorker && result.geometry.allocatedBytes() == 0,
                "Independent workers or cancelled ownership failed"
            );
            workers->submit({{0x123456789abcdef0ull, 3}, {}, 4 << 20}, settings, false, true);
        }
        else if (stage == 2)
        {
            require(
                result.request.id.sequence == 3 && result.outcome == WorkerOutcome::Failed && !result.error.empty(),
                "Worker exception did not produce diagnostic"
            );
            workers->submit({{0x123456789abcdef0ull, 4}, {}, 4 << 20}, settings, true);
            workers->releaseHeld();
        }
        else if (stage == 3)
        {
            require(
                result.request.id.sequence == 4 && result.outcome == WorkerOutcome::Geometry,
                "Held release failed"
            );
            workers->submit({{0x123456789abcdef0ull, 5}, {}, 4 << 20}, settings, true);
            // Old workers retain their CPU state while cancellation drains, without joining on the page thread.
            workers.reset();
            workers = std::make_unique<TerrainWorkers>();
            workers->submit({{0x123456789abcdef0ull, 6}, {}, 4 << 20}, settings);
        }
        else
        {
            require(
                result.request.id.sequence == 6 && result.outcome == WorkerOutcome::Geometry,
                "Restart received stale result or failed"
            );
            workers.reset();
            emscripten_cancel_main_loop();
            EM_ASM({ Module.workerProof.done = true; });
        }
        ++stage;
        EM_ASM({ Module.workerProof.stage = $0; }, stage);
    } catch (const std::exception& error)
    {
        emscripten_cancel_main_loop();
        workers.reset();
        EM_ASM({ Module.workerProof = {error : UTF8ToString($0)}; }, error.what());
    }
}
} // namespace

// Starts persistent C++ workers, then yields to the browser so responsiveness is part of the proof.
int main()
{
    settings.intervals = 32;
    workers = std::make_unique<TerrainWorkers>();
    workers->submit({{0x123456789abcdef0ull, 1}, {}, 4 << 20}, settings, true);
    workers->submit({{0x123456789abcdef0ull, 2}, {}, 4 << 20}, settings);
    emscripten_set_main_loop(poll, 0, false);
    return 0;
}
