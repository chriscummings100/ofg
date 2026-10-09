// Persistent CPU coordinator publishes complete immutable selections and explicit renderer retirement obligations.
#include "terrain/terrain-streaming-service.h"
#include "terrain/terrain-data-cache.h"
#include "core/engine-error.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace ofg::terrain {
struct TerrainStreamingService::State
{
    enum class Kind
    {
        Reset,
        Retry,
        Inject,
        ReleaseHeld,
        Pause,
        Ready,
        Failed,
        Released,
        Stop,
        SkipCache,
        Replace,
        CancelReplacement,
        Adopted
    };
    struct Message
    {
        Kind kind;
        RequestId id;
        ReadyContent content;
        GeneratorSettings generator;
        std::string error;
        bool hold = false, fail = false;
        std::shared_ptr<const TerrainManifest> manifest;
    };
    std::shared_ptr<StreamingWake> wake = std::make_shared<StreamingWake>();
    std::mutex mutex;
    std::optional<WorldPosition> observerInput, postedObserver;
    std::vector<Message> inbox;
    StreamingBatch outbox;
    TerrainStream stream;
    std::unique_ptr<TerrainWorkers> workers;
    std::unique_ptr<TerrainDataCache> cache;
    TerrainSourceSettings source;
    struct Acquisition
    {
        BuildRequest request;
        bool hold = false, fail = false;
    };
    std::map<RequestId, Acquisition> acquiring;
    size_t cacheHits = 0, cacheMisses = 0, cacheBypasses = 0;
    uint8_t deepestAcquisitionDepth = 0;
    StreamSettings settings;
    GeneratorSettings generator;
    double viewRange;
    WorldPosition observer;
    std::set<CellAddress> roots;
    bool stopping = false, holdNext = false, failNext = false, rootsDirty = false;
    bool paused = false;
    std::atomic<bool> abandon{false};
    uint64_t published = UINT64_MAX;
    enum class Handoff
    {
        Viewing,
        Coarsening,
        WaitingForCoarse,
        Preparing,
        Adopting
    };
    Handoff handoff = Handoff::Viewing;
    std::shared_ptr<const TerrainManifest> pendingRevision, candidateRevision, previousManifest;
    std::vector<CutEntry> previousRoots;
    std::vector<std::pair<RequestId, uint64_t>> pinnedRetirements;
    uint64_t adopted = 0, handoffSerial = 0;

    // Freezes the captured root set and redisplays its retained parents before reserving replacement memory.
    void beginReplacement()
    {
        if (handoff != Handoff::Viewing || !pendingRevision)
            return;
        if (pendingRevision->revision == source.manifest->revision)
        {
            pendingRevision.reset();
            return;
        }
        candidateRevision = std::move(pendingRevision);
        previousManifest = source.manifest;
        std::set<CellAddress> captured;
        for (const auto& entry : stream.cut())
            captured.insert(entry.address.cell);
        for (const auto& cell : roots)
            if (!captured.contains(cell))
                stream.withdrawRoot(cell);
        roots = std::move(captured);
        for (const auto& cell : roots)
            stream.setRefinement({cell}, false);
        rootsDirty = false;
        handoff = Handoff::Coarsening;
    }

    // Restores old coarse payloads while they are still charged and pinned; no network rollback is necessary.
    void cancelReplacement()
    {
        pendingRevision.reset();
        if (handoff == Handoff::Preparing)
        {
            stream.reset();
            stream.restoreRoots(previousRoots);
            source.manifest = previousManifest;
            pinnedRetirements.clear();
        }
        // Once published, a replacement is committed; await its acknowledgement before resuming demand.
        if (handoff == Handoff::Adopting)
            return;
        for (const auto& cell : roots)
            stream.setRefinement({cell}, std::nullopt);
        previousRoots.clear();
        candidateRevision.reset();
        previousManifest.reset();
        handoff = Handoff::Viewing;
        rootsDirty = true;
        stream.setObserver(observer);
    }

    // Advances only complete coarse cuts, with explicit adoption and resource-release acknowledgements.
    void advanceReplacement(StreamingBatch& batch)
    {
        const auto d = stream.diagnostics();
        const bool coarseReady = stream.cut().size() == roots.size() && std::all_of(
                                                                            stream.cut().begin(),
                                                                            stream.cut().end(),
                                                                            [](const auto& entry)
                                                                            {
                                                                                return entry.address.depth == 0;
                                                                            }
                                                                        );
        if (handoff == Handoff::Coarsening && coarseReady && !d.jobs)
        {
            previousRoots = stream.cut();
            handoffSerial = d.publications;
            handoff = Handoff::WaitingForCoarse;
            wake->signal();
        }
        else if (
            handoff == Handoff::WaitingForCoarse && adopted >= handoffSerial && !d.jobs && !d.retiredCpuBytes &&
            !d.retiredGpuBytes
        )
        {
            const auto capacity = roots.size() * settings.maximumPayloadBytes;
            if (capacity > settings.cpuBudget - d.residentCpuBytes ||
                capacity > settings.gpuBudget - d.residentGpuBytes)
            {
                batch.error = "Revision replacement blocked: both minimal root cuts do not fit the terrain budget.";
                cancelReplacement();
                return;
            }
            stream.reset();
            source.manifest = candidateRevision;
            for (const auto& cell : roots)
            {
                if (!stream.requestRoot(cell))
                    throw EngineError("Captured replacement roots exceed node budget.");
                stream.setRefinement({cell}, false);
            }
            handoff = Handoff::Preparing;
            wake->signal();
        }
        else if (handoff == Handoff::Preparing && coarseReady)
        {
            handoff = Handoff::Adopting;
            handoffSerial = d.publications;
            for (const auto& [id, serial] : pinnedRetirements)
                batch.retirements.emplace_back(id, handoffSerial);
            pinnedRetirements.clear();
        }
        else if (handoff == Handoff::Adopting && adopted >= handoffSerial)
        {
            previousRoots.clear();
            candidateRevision.reset();
            previousManifest.reset();
            handoff = Handoff::Viewing;
            for (const auto& cell : roots)
                stream.setRefinement({cell}, std::nullopt);
            stream.setObserver(observer);
            rootsDirty = true;
            wake->signal();
        }
    }

    // Constructs CPU ownership before the coordinator starts; validation precedes worker creation.
    State(
        StreamSettings settingsValue,
        GeneratorSettings generatorValue,
        double range,
        TerrainSourceSettings sourceValue
    )
        : stream(settingsValue)
        , settings(settingsValue)
        , generator(generatorValue)
        , viewRange(range)
    {
        if (!std::isfinite(range) || range < 0 || generator.rootWidth != settings.rootWidth ||
            settings.maximumJobs > 64 || settings.maximumJobs < 8)
        {
            throw EngineError("Invalid streaming service range, worker capacity or generator root width.");
        }
        workers = std::make_unique<TerrainWorkers>(wake);
        source = std::move(sourceValue);
        if (source.manifest)
        {
            if (source.manifest->rootWidth != settings.rootWidth)
                throw EngineError("Service terrain root width does not match the stream.");
            cache = std::make_unique<TerrainDataCache>(source.baseUrl, source.cacheDirectory, wake);
        }
    }

    // Publishes a control/reply value before signalling, with no callback into the controller.
    void send(Message message)
    {
        {
            std::lock_guard lock(mutex);
            if (message.kind == Kind::Reset)
            {
                observerInput.reset();
                postedObserver.reset();
            }
            inbox.push_back(std::move(message));
        }
        wake->signal();
    }

    // Reconciles root coverage near the observer using generator-certified vertical bounds and hysteresis.
    void updateRoots()
    {
        for (auto i = roots.begin(); i != roots.end();)
        {
            if (distanceToNode({*i}, observer, settings.rootWidth) > viewRange + settings.rootWidth)
            {
                stream.withdrawRoot(*i);
                i = roots.erase(i);
            }
            else
                ++i;
        }
        const int extent = int(std::ceil(viewRange / settings.rootWidth)) + 1;
        std::vector<std::pair<double, CellAddress>> candidates;
        for (int z = -extent; z <= extent; ++z)
            for (int y = -extent; y <= extent; ++y)
                for (int x = -extent; x <= extent; ++x)
                {
                    const std::array<int64_t, 3> coordinates{observer.cell.x, observer.cell.y, observer.cell.z};
                    const std::array<int, 3> offsets{x, y, z};
                    bool overflow = false;
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        const auto offset = offsets[axis];
                        overflow = overflow || (offset > 0 && coordinates[axis] > INT64_MAX - offset) ||
                                   (offset < 0 && coordinates[axis] < INT64_MIN - offset);
                    }
                    if (overflow)
                        continue;
                    const CellAddress cell{observer.cell.x + x, observer.cell.y + y, observer.cell.z + z};
                    // The present height generator's full range is bounded by offset +/- amplitude.
                    const auto lower = std::floor(
                        (source.manifest ? source.manifest->minimumHeight
                                         : generator.heightOffset - generator.amplitude) /
                        settings.rootWidth
                    );
                    const auto upper = std::floor(
                        (source.manifest ? source.manifest->maximumHeight
                                         : generator.heightOffset + generator.amplitude) /
                        settings.rootWidth
                    );
                    if (cell.y < lower || cell.y > upper || roots.contains(cell))
                        continue;
                    const auto distance = distanceToNode({cell}, observer, settings.rootWidth);
                    if (distance <= viewRange)
                        candidates.emplace_back(distance, cell);
                }
        std::sort(candidates.begin(), candidates.end());
        rootsDirty = false;
        for (const auto& [distance, cell] : candidates)
        {
            if (stream.requestRoot(cell))
                roots.insert(cell);
            else
                rootsDirty = true;
        }
    }

    // Consumes ordered input messages; shutdown/reset invalidate IDs before any late completion is considered.
    void receiveMessages(std::vector<Message>& messages)
    {
        for (auto& message : messages)
        {
            switch (message.kind)
            {
            case Kind::Reset:
                if (!stopping)
                {
                    generator = message.generator;
                    stream.reset();
                    roots.clear();
                    rootsDirty = true;
                }
                break;
            case Kind::Retry:
                stream.retryFailures();
                break;
            case Kind::Inject:
                holdNext = message.hold;
                failNext = message.fail;
                break;
            case Kind::ReleaseHeld:
                workers->releaseHeld();
                for (auto& [id, task] : acquiring)
                    task.hold = false;
                break;
            case Kind::SkipCache:
                source.skipCache = message.hold;
                break;
            case Kind::Replace:
                if (!stopping)
                    pendingRevision = std::move(message.manifest);
                break;
            case Kind::CancelReplacement:
                if (!stopping)
                    cancelReplacement();
                break;
            case Kind::Adopted:
                adopted = std::max(adopted, message.id.sequence);
                break;
            case Kind::Pause:
                paused = message.hold;
                break;
            case Kind::Ready:
                stream.complete(message.id, message.content);
                break;
            case Kind::Failed:
                stream.fail(message.id, message.error, FailureStage::Upload);
                break;
            case Kind::Released:
                stream.releasePayload(message.id);
                rootsDirty = true;
                break;
            case Kind::Stop:
                if (!stopping)
                {
                    stopping = true;
                    handoff = Handoff::Viewing;
                    pendingRevision.reset();
                    previousRoots.clear();
                    stream.reset();
                    roots.clear();
                }
                break;
            }
        }
    }

    // Validates worker values before handing CPU geometry to the render thread, preserving ownership on cancellation.
    void receiveResults(StreamingBatch& batch)
    {
        if (cache)
        {
            for (auto& result : cache->takeResults())
            {
                auto found = acquiring.find(result.id);
                if (found == acquiring.end())
                    continue;
                const auto task = std::move(found->second);
                acquiring.erase(found);
                if (result.cancelled || stream.request(task.request.address) != result.id)
                {
                    stream.acknowledgeCancellation(result.id);
                    continue;
                }
                if (!result.error.empty())
                {
                    stream.fail(result.id, result.error);
                    batch.error = result.error;
                    continue;
                }
                cacheHits += result.hit;
                cacheMisses += !result.hit && !result.bypassed;
                cacheBypasses += result.bypassed;
                if (!result.warning.empty())
                    batch.cacheWarning = result.warning;
                auto buildGenerator = generator;
                buildGenerator.source = std::move(result.tile);
                try
                {
                    workers->submit(task.request, std::move(buildGenerator), task.hold, task.fail);
                } catch (const std::exception& error)
                {
                    stream.fail(result.id, error.what());
                    batch.error = error.what();
                }
            }
        }
        for (auto& result : workers->takeResults())
        {
            const auto id = result.request.id;
            if (result.outcome == WorkerOutcome::Cancelled)
            {
                if (!stream.awaitingUpload(id))
                    stream.acknowledgeCancellation(id);
                continue;
            }
            if (result.outcome == WorkerOutcome::Failed)
            {
                if (stream.request(result.request.address) == id)
                {
                    batch.error = result.error;
                }
                stream.fail(id, result.error);
                continue;
            }
            try
            {
                validateGeometry(
                    result.geometry,
                    std::ldexp(settings.rootWidth, -result.request.address.depth),
                    result.request.byteLimit
                );
                ReadyContent content;
                content.empty = result.geometry.indices.empty();
                content.certifiedEmpty = result.geometry.certifiedEmpty;
                // Balancing may split empty space, but its children inherit the certificate without source arrays.
                if (!content.certifiedEmpty)
                    content.source = result.source;
                if (content.empty)
                {
                    stream.complete(id, content);
                }
                else if (stream.acceptGenerated(id, result.geometry.allocatedBytes()))
                {
                    batch.uploads.push_back(std::move(result));
                }
            } catch (const std::exception& error)
            {
                batch.error = error.what();
                stream.fail(id, error.what());
            }
        }
    }

    // Commits durable commands with the latest immutable selection under a short outbox lock.
    void publish(StreamingBatch batch)
    {
        {
            std::lock_guard lock(mutex);
            for (auto& upload : batch.uploads)
                outbox.uploads.push_back(std::move(upload));
            for (auto retirement : batch.retirements)
                outbox.retirements.push_back(retirement);
            if (batch.snapshot)
                outbox.snapshot = std::move(batch.snapshot);
            outbox.diagnostics = batch.diagnostics;
            outbox.reconciliationMilliseconds = batch.reconciliationMilliseconds;
            outbox.maximumReconciliationMilliseconds =
                std::max(outbox.maximumReconciliationMilliseconds, batch.reconciliationMilliseconds);
            if (!batch.error.empty())
                outbox.error = std::move(batch.error);
            outbox.stopped = batch.stopped;
            outbox.paused = batch.paused;
            outbox.cacheHits = cacheHits;
            outbox.cacheMisses = cacheMisses;
            outbox.cacheBypasses = cacheBypasses;
            outbox.deepestAcquisitionDepth = deepestAcquisitionDepth;
            outbox.replacing = handoff != Handoff::Viewing;
            outbox.replacementRevision = candidateRevision ? candidateRevision->revision : "";
            if (!batch.cacheWarning.empty())
                outbox.cacheWarning = std::move(batch.cacheWarning);
        }
        // Wakes native teardown/test consumers as well as acknowledging input publication.
        wake->signal();
    }

    // Runs complete finite reconciliations. Sequence capture before draining prevents lost wake-ups.
    void run()
    {
        for (;;)
        {
            auto sequence = wake->sequence();
            const auto start = std::chrono::steady_clock::now();
            std::vector<Message> messages;
            std::optional<WorldPosition> latest;
            {
                std::lock_guard lock(mutex);
                messages.swap(inbox);
                latest.swap(observerInput);
            }
            StreamingBatch batch;
            bool retryShutdown = false;
            try
            {
                receiveMessages(messages);
                if (latest && !stopping)
                {
                    observer = *latest;
                    if (handoff == Handoff::Viewing)
                        stream.setObserver(observer);
                    rootsDirty = true;
                }
                if (paused && !stopping)
                {
                    batch.paused = true;
                    batch.diagnostics = stream.diagnostics();
                    publish(std::move(batch));
                    wake->wait(sequence + 1);
                    continue;
                }
                if (!stopping)
                    beginReplacement();
                if (rootsDirty && !stopping && handoff == Handoff::Viewing)
                    updateRoots();
                receiveResults(batch);
                stream.update();
                if (!stopping)
                    advanceReplacement(batch);
                for (const auto id : stream.takeCancellations())
                {
                    if (stream.awaitingUpload(id))
                    {
                        batch.retirements.emplace_back(id, 0);
                        if (abandon)
                            stream.releasePayload(id);
                    }
                    else if (cache && acquiring.contains(id))
                        cache->cancel(id);
                    else
                        workers->cancel(id);
                }
                auto retirements = stream.takeRetirements();
                for (auto retirement : retirements)
                {
                    if (handoff == Handoff::Preparing && std::any_of(
                                                             previousRoots.begin(),
                                                             previousRoots.end(),
                                                             [&](const auto& entry)
                                                             {
                                                                 return entry.payload->id == retirement.first;
                                                             }
                                                         ))
                    {
                        pinnedRetirements.push_back(retirement);
                        continue;
                    }
                    batch.retirements.push_back(retirement);
                    if (abandon)
                        stream.releasePayload(retirement.first);
                }
                if (!stopping)
                {
                    for (auto request : stream.takeRequests())
                    {
                        try
                        {
                            if (request.parentCertifiedEmpty)
                            {
                                ReadyContent empty;
                                empty.empty = empty.certifiedEmpty = true;
                                stream.complete(request.id, empty);
                                wake->signal();
                            }
                            else if (cache)
                            {
                                auto parent = request.parentSource;
                                const auto width = std::ldexp(settings.rootWidth, -request.address.depth);
                                const auto y =
                                    double(request.address.cell.y) * settings.rootWidth + request.address.y * width;
                                if (parent && (y > parent->maximumHeight || y + width < parent->minimumHeight))
                                {
                                    ReadyContent empty;
                                    empty.empty = empty.certifiedEmpty = true;
                                    stream.complete(request.id, empty);
                                    wake->signal();
                                }
                                else if (parent && parent->terminal())
                                {
                                    auto buildGenerator = generator;
                                    buildGenerator.source = std::move(parent);
                                    workers->submit(request, std::move(buildGenerator), holdNext, failNext);
                                }
                                else
                                {
                                    acquiring.emplace(request.id, Acquisition{request, holdNext, failNext});
                                    try
                                    {
                                        cache->request(request.id, source.manifest, request.address, source.skipCache);
                                        deepestAcquisitionDepth =
                                            std::max(deepestAcquisitionDepth, request.address.depth);
                                    } catch (...)
                                    {
                                        acquiring.erase(request.id);
                                        throw;
                                    }
                                }
                            }
                            else
                                workers->submit(request, generator, holdNext, failNext);
                        } catch (const std::exception& error)
                        {
                            stream.fail(request.id, error.what());
                            batch.error = error.what();
                            wake->signal();
                        }
                        holdNext = failNext = false;
                    }
                }
                if (stopping && abandon)
                    stream.abandonRenderer();
                if (stopping)
                {
                    batch.retirements
                        .insert(batch.retirements.end(), pinnedRetirements.begin(), pinnedRetirements.end());
                    pinnedRetirements.clear();
                }
                batch.diagnostics = stream.diagnostics();
                if (handoff != Handoff::Preparing && published != batch.diagnostics.publications)
                {
                    auto snapshot = std::make_shared<RenderSnapshot>();
                    snapshot->revision = batch.diagnostics.publications;
                    snapshot->created = std::chrono::steady_clock::now();
                    snapshot->leaves = stream.cut();
                    snapshot->manifest = source.manifest;
                    batch.snapshot = std::move(snapshot);
                    published = batch.diagnostics.publications;
                }
                batch.stopped = stopping && !batch.diagnostics.jobs && !batch.diagnostics.retiredCpuBytes &&
                                !batch.diagnostics.retiredGpuBytes;
            } catch (const std::exception& error)
            {
                batch.error = error.what();
                // A terminal coordinator error is visible. Stop further work and drain owned requests.
                stopping = true;
                stream.reset();
                retryShutdown = true;
            }
            batch.reconciliationMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            const bool done = batch.stopped;
            const bool retryRoots = rootsDirty && !stopping && handoff == Handoff::Viewing &&
                                    batch.diagnostics.nodes < settings.maximumNodes;
            publish(std::move(batch));
            if (done)
                return;
            if (retryShutdown || retryRoots)
                continue;
            // publish signals exactly once. Any additional signal since the initial capture belongs to
            // a producer and prevents sleep, including signals racing with draining or publication.
            wake->wait(sequence + 1);
        }
    }
};

TerrainStreamingService::TerrainStreamingService(
    StreamSettings settings,
    GeneratorSettings generator,
    double viewRange,
    TerrainSourceSettings source
)
    : m_state(std::make_shared<State>(settings, generator, viewRange, std::move(source)))
    , m_thread(
          [state = m_state]
          {
              state->run();
          }
      )
{
#ifdef __EMSCRIPTEN__
    m_thread.detach();
#endif
}
TerrainStreamingService::~TerrainStreamingService()
{
    m_state->abandon = true;
    stop();
    if (m_thread.joinable())
        m_thread.join();
}
void TerrainStreamingService::setObserver(WorldPosition observer)
{
    observer = normalizePosition(observer, m_state->settings.rootWidth);
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->postedObserver && m_state->postedObserver->cell == observer.cell &&
            m_state->postedObserver->local == observer.local)
            return;
        m_state->postedObserver = observer;
        m_state->observerInput = observer;
    }
    m_state->wake->signal();
}
void TerrainStreamingService::reset(GeneratorSettings generator)
{
    if (generator.rootWidth != m_state->settings.rootWidth)
        throw EngineError("Reset cannot change the streaming service root width.");
    State::Message message{State::Kind::Reset};
    message.generator = generator;
    m_state->send(std::move(message));
}
void TerrainStreamingService::retryFailures()
{
    m_state->send({State::Kind::Retry});
}
void TerrainStreamingService::setSkipCache(bool skip)
{
    State::Message message{State::Kind::SkipCache};
    message.hold = skip;
    m_state->send(std::move(message));
}
void TerrainStreamingService::replaceRevision(std::shared_ptr<const TerrainManifest> manifest)
{
    if (!manifest || !m_state->cache || manifest->rootWidth != m_state->settings.rootWidth)
        throw EngineError("Replacement requires a compatible immutable service manifest.");
    State::Message message{State::Kind::Replace};
    message.manifest = std::move(manifest);
    m_state->send(std::move(message));
}
void TerrainStreamingService::cancelReplacement()
{
    m_state->send({State::Kind::CancelReplacement});
}
void TerrainStreamingService::selectionAdopted(uint64_t revision)
{
    State::Message message{State::Kind::Adopted};
    message.id.sequence = revision;
    m_state->send(std::move(message));
}
void TerrainStreamingService::injectNext(bool hold, bool fail)
{
    State::Message message{State::Kind::Inject};
    message.hold = hold;
    message.fail = fail;
    m_state->send(std::move(message));
}
void TerrainStreamingService::releaseHeld()
{
    m_state->send({State::Kind::ReleaseHeld});
}
void TerrainStreamingService::holdReconciliation(bool hold)
{
    State::Message message{State::Kind::Pause};
    message.hold = hold;
    m_state->send(std::move(message));
}
void TerrainStreamingService::uploadReady(RequestId id, ReadyContent content)
{
    State::Message message{State::Kind::Ready};
    message.id = id;
    message.content = content;
    m_state->send(std::move(message));
}
void TerrainStreamingService::uploadFailed(RequestId id, std::string error)
{
    State::Message message{State::Kind::Failed};
    message.id = id;
    message.error = std::move(error);
    m_state->send(std::move(message));
}
void TerrainStreamingService::payloadReleased(RequestId id)
{
    State::Message message{State::Kind::Released};
    message.id = id;
    m_state->send(std::move(message));
}
void TerrainStreamingService::stop()
{
    m_state->send({State::Kind::Stop});
}
StreamingBatch TerrainStreamingService::takeBatch()
{
    std::lock_guard lock(m_state->mutex);
    StreamingBatch batch;
    batch.uploads.swap(m_state->outbox.uploads);
    batch.retirements.swap(m_state->outbox.retirements);
    batch.snapshot.swap(m_state->outbox.snapshot);
    batch.error.swap(m_state->outbox.error);
    batch.diagnostics = m_state->outbox.diagnostics;
    batch.reconciliationMilliseconds = m_state->outbox.reconciliationMilliseconds;
    batch.maximumReconciliationMilliseconds = m_state->outbox.maximumReconciliationMilliseconds;
    batch.stopped = m_state->outbox.stopped;
    batch.paused = m_state->outbox.paused;
    batch.cacheHits = m_state->outbox.cacheHits;
    batch.cacheMisses = m_state->outbox.cacheMisses;
    batch.cacheBypasses = m_state->outbox.cacheBypasses;
    batch.deepestAcquisitionDepth = m_state->outbox.deepestAcquisitionDepth;
    batch.cacheWarning.swap(m_state->outbox.cacheWarning);
    batch.replacing = m_state->outbox.replacing;
    batch.replacementRevision = m_state->outbox.replacementRevision;
    return batch;
}
uint64_t TerrainStreamingService::wakeSequence()
{
    return m_state->wake->sequence();
}
bool TerrainStreamingService::waitForChange(uint64_t previous, std::chrono::milliseconds timeout)
{
    return m_state->wake->waitFor(previous, timeout);
}
} // namespace ofg::terrain
