// Single-owner streaming coordinator. Only CPU values cross its mailboxes; graphics remain on the caller thread.
#pragma once
#include "terrain/terrain-workers.h"
#include "terrain/terrain-content.h"
#include <thread>

namespace ofg::terrain {
struct TerrainSourceSettings
{
    std::string baseUrl, cacheDirectory;
    std::shared_ptr<const TerrainManifest> manifest;
    bool skipCache = false;
};
struct RenderSnapshot
{
    uint64_t revision = 0;
    std::chrono::steady_clock::time_point created;
    std::vector<CutEntry> leaves;
    std::shared_ptr<const TerrainManifest> manifest;
};
struct StreamingBatch
{
    std::vector<WorkerResult> uploads;
    std::vector<std::pair<RequestId, uint64_t>> retirements;
    std::shared_ptr<const RenderSnapshot> snapshot;
    StreamDiagnostics diagnostics;
    double reconciliationMilliseconds = 0;
    double maximumReconciliationMilliseconds = 0;
    std::string error;
    bool stopped = false;
    bool paused = false;
    size_t cacheHits = 0, cacheMisses = 0, cacheBypasses = 0;
    uint8_t deepestAcquisitionDepth = 0;
    std::string cacheWarning;
    bool replacing = false;
    std::string replacementRevision;
};

class TerrainStreamingService
{
public:
    // Starts one coordinator and two meshers. Root/view dimensions are metres; generation agrees with rootWidth.
    TerrainStreamingService(
        StreamSettings settings,
        GeneratorSettings generator,
        double viewRange = 4096,
        TerrainSourceSettings source = {}
    );
    // Captures the new cache behavior for future acquisitions, leaving loaded terrain untouched.
    void setSkipCache(bool skip);
    // Queues an immutable replacement; one latest-pending revision follows any active handoff.
    void replaceRevision(std::shared_ptr<const TerrainManifest> manifest);
    // Discards an uncommitted candidate and restores the retained old roots without HTTP.
    void cancelReplacement();
    // Acknowledges frame-boundary selection adoption, allowing coarsening/replacement retirement to progress.
    void selectionAdopted(uint64_t revision);
    // Cancels CPU work; the caller retains/drains renderer resources independently of this CPU-only lifetime.
    ~TerrainStreamingService();
    TerrainStreamingService(const TerrainStreamingService&) = delete;
    TerrainStreamingService& operator=(const TerrainStreamingService&) = delete;
    // Coalesces observer updates; a changed value wakes complete demand discovery.
    void setObserver(WorldPosition observer);
    // Invalidates the previous epoch and clears any unconsumed observer value before accepting newer input.
    void reset(GeneratorSettings generator);
    // Retries current failures with fresh request identities.
    void retryFailures();
    // Arms explicit laboratory fault injection for the next dispatched job.
    void injectNext(bool hold, bool fail);
    // Releases all laboratory-held worker jobs.
    void releaseHeld();
    // Explicit laboratory/test gate before reconciliation; mailboxes and shutdown remain responsive.
    void holdReconciliation(bool hold);
    // Acknowledges a renderer record installed and ready for selection.
    void uploadReady(RequestId id, ReadyContent content);
    // Reports failed GPU preparation; its reservation survives until payloadReleased.
    void uploadFailed(RequestId id, std::string error);
    // Acknowledges actual resource release after snapshot adoption and GPU completion.
    void payloadReleased(RequestId id);
    // Begins asynchronous shutdown; continue draining batches and acknowledging renderer retirement.
    void stop();
    // Swaps commands and latest immutable snapshot together without waiting for traversal or meshing.
    StreamingBatch takeBatch();
    // Native/test shutdown convenience; capture before takeBatch and never use on the browser UI thread.
    uint64_t wakeSequence();
    // Waits for a published batch or input/result event after a previously captured sequence.
    bool waitForChange(uint64_t previous, std::chrono::milliseconds timeout = std::chrono::seconds(30));

private:
    struct State;
    std::shared_ptr<State> m_state;
    std::thread m_thread;
};
} // namespace ofg::terrain
