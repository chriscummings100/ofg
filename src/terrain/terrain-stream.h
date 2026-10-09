// Single-owner CPU octree streaming: complete demand, immutable readiness and balanced display selection.
#pragma once

#include "terrain/terrain-address.h"
#include <memory>

#include <map>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace ofg::terrain {
struct TerrainTile;
struct AddressHash
{
    // Hashes exact root/local integers without converting large coordinates to floating point.
    size_t operator()(const NodeAddress& address) const noexcept;
};
enum class BuildState
{
    Pending,
    Loading,
    Loaded,
    Failed
};
enum class FailureStage
{
    Generation,
    Upload
};

struct RequestId
{
    uint64_t epoch = 0, sequence = 0;
    // Compares full asynchronous identities, never just spatial addresses.
    auto operator<=>(const RequestId&) const = default;
};

struct ReadyContent
{
    RequestId id;
    size_t cpuBytes = 0, gpuBytes = 0;
    bool empty = false;
    bool certifiedEmpty = false;
    std::shared_ptr<const TerrainTile> source;
    size_t sourceGpuBytes = 0; // Shared allocation, counted once across resident/retired descriptors.
};

struct BuildRequest
{
    RequestId id;
    NodeAddress address;
    size_t byteLimit = 0;
    std::shared_ptr<const TerrainTile> parentSource;
    bool parentCertifiedEmpty = false;
};

struct CutEntry
{
    NodeAddress address;
    std::shared_ptr<const ReadyContent> payload;
    uint8_t transitionFaces = 0; // Draw selection only; never part of a build identity.
};

struct StreamSettings
{
    double rootWidth = 1024;
    uint8_t maximumDepth = 6;
    size_t cpuBudget = 256ull << 20;
    size_t gpuBudget = 256ull << 20;
    size_t maximumPayloadBytes = 16ull << 20;
    size_t maximumJobs = 64;
    size_t maximumNodes = 8192;
    double refinementRange = std::numeric_limits<double>::infinity();
};

struct StreamDiagnostics
{
    size_t nodes = 0, pending = 0, loading = 0, loaded = 0, failed = 0;
    size_t selected = 0, admittedRoots = 0, loadingRoots = 0, jobs = 0;
    size_t residentCpuBytes = 0, residentGpuBytes = 0;
    size_t reservedCpuBytes = 0, reservedGpuBytes = 0, retiredCpuBytes = 0, retiredGpuBytes = 0;
    size_t sourceCpuBytes = 0, sourceTextureBytes = 0; // Live shared allocations, including retirement.
    uint64_t publications = 0, staleResults = 0;
    size_t unresolvedRefinements = 0;
    uint8_t deepestSurfaceDepth = 0;
    bool planningIdle = false;
    bool budgetBlocked = false;
};

class TerrainStream
{
public:
    // Creates a controller without threads or graphics; validates finite sizes and bounded depth.
    explicit TerrainStream(StreamSettings settings = {});
    // Releases controller ownership; adapters must stop callbacks before destruction.
    ~TerrainStream();
    TerrainStream(const TerrainStream&) = delete;
    TerrainStream& operator=(const TerrainStream&) = delete;
    // Marks a root required; repeated calls are idempotent. False defers admission at the node cap; caller retries.
    bool requestRoot(CellAddress cell);
    // Withdraws a root; its coverage is explicitly removed on update.
    void withdrawRoot(CellAddress cell);
    // Uses point-to-AABB distances with 2/2.5-width hysteresis for every existing node.
    void setObserver(WorldPosition observer);
    // Explicit laboratory demand override; null restores distance demand for this node.
    void setRefinement(NodeAddress node, std::optional<bool> refine);
    // Clears a terminal content failure; future dispatch receives a fresh request ID.
    void retry(NodeAddress address);
    // Retries every current terminal failure, without reviving cancelled identities.
    void retryFailures();
    // Withdraws all coverage, invalidates requests and advances the terrain epoch.
    void reset();
    // Restores a cancelled handoff's retained coarse cut after reset, before any new update/dispatch.
    // Nonempty payloads must still be charged in retirement; the renderer must not have released them.
    void restoreRoots(const std::vector<CutEntry>& roots);
    // Reconciles demand, cancellation, complete-group reservations and prepared cut publication.
    void update();
    // Transfers newly dispatched immutable requests to the worker adapter.
    std::vector<BuildRequest> takeRequests();
    // Transfers cancellation notifications; reservations survive until acknowledge or completion.
    std::vector<RequestId> takeCancellations();
    // Accepts a worker's validated nonempty CPU result once; true permits upload, not display.
    bool acceptGenerated(RequestId request, size_t cpuBytes);
    // Publishes an ordered/prepared upload or explicit empty result, rejecting obsolete identities.
    bool complete(RequestId request, ReadyContent payload);
    // Records a terminal generation/upload error without replacing displayed fallback.
    void fail(RequestId request, std::string error, FailureStage stage = FailureStage::Generation);
    // Finalizes a cancelled job; repeated acknowledgement is harmless.
    void acknowledgeCancellation(RequestId request);
    // Returns the selected immutable CPU descriptors; copying entries never retains graphics objects.
    const std::vector<CutEntry>& cut() const noexcept;
    // Transfers retirement obligations; renderer acknowledges only after adoption and actual GPU completion.
    std::vector<std::pair<RequestId, uint64_t>> takeRetirements();
    // Releases renderer-owned payload charges exactly once, including cancelled or failed partial uploads.
    void releasePayload(RequestId id);
    // Distinguishes renderer-owned requests from worker-owned requests when routing cancellation.
    bool awaitingUpload(RequestId id) const;
    // Drops CPU accounting only after shutdown when the renderer has taken independent retirement ownership.
    void abandonRenderer();
    // Reports live content, reservations and retirement without counting the same allocation twice.
    StreamDiagnostics diagnostics() const;
    // Observes a node's build state; an absent node returns nullopt.
    std::optional<BuildState> state(NodeAddress address) const;
    // Returns a slot's currently accepted identity, if any.
    std::optional<RequestId> request(NodeAddress address) const;
    // Validates coverage, ancestry, readiness, balance and accounting; throws on broken invariants.
    void validate() const;

private:
    using LeafSet = std::unordered_set<NodeAddress, AddressHash>;
    struct Slot;
    struct Node;
    struct Job;
    // Reconciles complete demand and ready cuts; no partial selection escapes.
    void reconcile();
    // Resolves exact addresses through a non-owning hash index without creating nodes.
    Node* find(NodeAddress address) const;
    // Allocates all eight children together and derives their initial distance demand.
    bool createChildren(Node& node);
    // Returns the single build slot of an existing node.
    Slot& slot(NodeAddress address);
    // Finds a matching live owner; cancelled/obsolete identities do not resolve.
    Slot* owner(RequestId request);
    // Computes effective distance/override demand; a certificate or depth cap forbids splitting.
    bool wantsChildren(const Node& node) const;
    // Computes complete distance demand and closes target face boundaries within the metadata cap.
    void discoverDemand();
    // Repairs every face violation; desired cuts may refine, ready cuts only coarsen.
    void balance(LeafSet& leaves, bool allowRefinement);
    // Derives and publishes available coverage, retaining every needed ancestor payload.
    void selectReady();
    // Reserves whole eligible sibling groups, coarse first and nearest within a level.
    void schedule();
    // Removes non-owning index entries before destroying a complete unused subtree.
    void unindex(Node& node);
    // Invalidates work outside required content, retaining cancelled job accounting.
    void cancelUnused(const std::set<NodeAddress>& required);
    // Retires unused payloads and entire child groups after cut/dependency ownership is established.
    void prune(const std::set<NodeAddress>& required);
    // Transfers renderer residency charges to retirement; snapshots retain only immutable CPU metadata.
    void retire(Slot& slot);

    StreamSettings m_settings;
    WorldPosition m_observer;
    std::map<CellAddress, std::unique_ptr<Node>> m_roots;
    std::unordered_map<NodeAddress, Node*, AddressHash> m_index;
    std::set<CellAddress> m_wantedRoots;
    std::map<NodeAddress, bool> m_overrides;
    std::map<RequestId, Job> m_jobs;
    std::vector<BuildRequest> m_dispatch;
    std::vector<RequestId> m_cancellations;
    std::vector<CutEntry> m_cut;
    std::map<RequestId, std::shared_ptr<const ReadyContent>> m_retired;
    std::vector<std::pair<RequestId, uint64_t>> m_retirements;
    LeafSet m_desired;
    std::set<NodeAddress> m_required;
    // Marks availability dirty; demand is independently invalidated by observer/topology inputs.
    void invalidateSelection() noexcept;
    uint64_t m_epoch = 1, m_sequence = 0, m_publications = 0, m_staleResults = 0;
    bool m_budgetBlocked = false;
    bool m_metadataBlocked = false;
    bool m_idle = false;
    bool m_demandDirty = true;
};
} // namespace ofg::terrain
