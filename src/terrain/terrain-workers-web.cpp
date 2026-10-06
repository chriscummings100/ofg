// Browser worker bridge copies only bounded message data during application polling, never inside a JS callback.
#include "terrain/terrain-workers.h"
#include "terrain/terrain-worker-wire.h"
#include "core/engine-error.h"

#include <emscripten.h>

namespace ofg::terrain {
namespace {
struct ResultHeader
{
    WorkerWire request;
    uint32_t vertices, indices, outcome, certifiedEmpty, errorBytes, worker;
    std::array<uint32_t, 8> partOffsets;
};
static_assert(sizeof(ResultHeader) == 160);
static_assert(sizeof(Vertex) == 72);

// Copies bounded counts and request identity before the application allocates result storage.
EM_JS(int, peekResult, (void* pointer), {
    const result = Module.terrainWorkers.results[0];
    if (!result)
    {
        return 0;
    }
    HEAPU8.set(result.wire, pointer);
    result.errorData = new TextEncoder().encode(String(result.error || "")).slice(0, 4096);
    HEAPU32.set([result.vertices?.byteLength / 72 || 0, result.indices?.byteLength / 4 || 0,
        result.outcome, result.certifiedEmpty ? 1 : 0, result.errorData.byteLength, result.worker || 0], (pointer + 104) / 4);
    HEAPU32.set(result.partOffsets || new Uint32Array(8), (pointer + 128) / 4);
    return 1;
});

// Transfers one result into application-owned buffers and releases its JavaScript transport copies.
EM_JS(void, popResult, (void* vertices, void* indices, void* error), {
    const result = Module.terrainWorkers.results.shift();
    if (result.vertices)
    {
        HEAPU8.set(result.vertices, vertices);
    }
    if (result.indices)
    {
        HEAPU8.set(result.indices, indices);
    }
    if (result.errorData.length)
    {
        HEAPU8.set(result.errorData, error);
    }
    Module.terrainWorkers.jobs.delete(result.key);
});
} // namespace

struct TerrainWorkers::State
{};

TerrainWorkers::TerrainWorkers()
    : m_state(std::make_unique<State>())
{
    EM_ASM({ Module.terrainWorkers = new globalThis.TerrainWorkerPool(); });
}
TerrainWorkers::~TerrainWorkers()
{
    EM_ASM({
        Module.terrainWorkers.stop();
        Module.terrainWorkers = null;
    });
}

void TerrainWorkers::submit(BuildRequest r, GeneratorSettings s, bool hold, bool fail)
{
    if (r.byteLimit > (16u << 20))
    {
        throw EngineError("Browser terrain request exceeds the worker output limit.");
    }
    const auto& a = r.address;
    WorkerWire wire{
        r.id.epoch,
        r.id.sequence,
        a.cell.x,
        a.cell.y,
        a.cell.z,
        s.seed,
        a.x,
        a.y,
        a.z,
        a.depth,
        s.rootWidth,
        s.heightOffset,
        s.amplitude,
        s.intervals,
        uint32_t(r.byteLimit),
        unsigned(hold) | (unsigned(fail) << 1)
    };
    EM_ASM({ Module.terrainWorkers.submit(HEAPU8.slice($0, $0 + 104)); }, &wire);
}

void TerrainWorkers::cancel(RequestId id)
{
    EM_ASM({ Module.terrainWorkers.cancel(HEAPU8.slice($0, $0 + 16)); }, &id);
}

void TerrainWorkers::releaseHeld()
{
    EM_ASM({ Module.terrainWorkers.release(); });
}

std::vector<WorkerResult> TerrainWorkers::takeResults()
{
    std::vector<WorkerResult> results;
    ResultHeader header;
    while (peekResult(&header))
    {
        const auto& r = header.request;
        if (uint64_t(header.vertices) * sizeof(Vertex) + uint64_t(header.indices) * 4 > (16u << 20) ||
            header.errorBytes > 4096)
        {
            throw EngineError("Browser terrain worker returned oversized transport data.");
        }
        WorkerResult result;
        result.request =
            {{r.epoch, r.sequence}, {{r.cellX, r.cellY, r.cellZ}, r.x, r.y, r.z, uint8_t(r.depth)}, r.byteLimit};
        result.geometry.vertices.resize(header.vertices);
        result.geometry.indices.resize(header.indices);
        result.geometry.certifiedEmpty = header.certifiedEmpty != 0;
        result.geometry.partOffsets = header.partOffsets;
        result.outcome = WorkerOutcome(header.outcome);
        result.worker = header.worker;
        result.error.resize(header.errorBytes);
        popResult(result.geometry.vertices.data(), result.geometry.indices.data(), result.error.data());
        results.push_back(std::move(result));
    }
    return results;
}
} // namespace ofg::terrain
