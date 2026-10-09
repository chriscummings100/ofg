// Naive asynchronous persistent terrain cache: local bytes first, HTTP on miss, explicit read/write bypass.
#pragma once
#include "terrain/terrain-content.h"
#include "terrain/terrain-stream.h"
#include "terrain/streaming-wake.h"
#include <chrono>

namespace ofg::terrain {
struct TerrainDataResult
{
    RequestId id;
    std::shared_ptr<const TerrainTile> tile;
    std::string error, warning;
    bool cancelled = false, hit = false, bypassed = false;
};

class TerrainDataCache
{
public:
    // Starts two I/O workers separate from mesh workers; browser storage/network work is proxied to its UI event loop.
    TerrainDataCache(std::string baseUrl, std::string directory, std::shared_ptr<StreamingWake> wake = {});
    // Cancels requests; native joins, browser workers retain only their CPU state until asynchronous completion.
    ~TerrainDataCache();
    // Queues one immutable request, bounded to 64 queued/running/completed entries. Skip bypasses reads and writes.
    void request(RequestId id, std::shared_ptr<const TerrainManifest> manifest, NodeAddress node, bool skipCache);
    // Cancels one request without affecting other misses for the same tile.
    void cancel(RequestId id);
    // Transfers terminal outcomes without waiting; every queued request produces exactly one outcome.
    std::vector<TerrainDataResult> takeResults();
    // Native/test convenience; never wait on the browser main thread.
    bool waitForResult(std::chrono::milliseconds timeout);

private:
    struct State;
    std::shared_ptr<State> m_state;
};
} // namespace ofg::terrain
