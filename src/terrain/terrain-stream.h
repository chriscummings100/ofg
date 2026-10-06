// Application-thread octree streaming: demand, request identity and atomic prepared display cuts.
#pragma once

#include "terrain/terrain-address.h"
#include "resources/mesh.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ofg::terrain {
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

struct ContentKey
{
    NodeAddress address;
    uint8_t transitionFaces = 0;
    // Distinguishes geometry variants by the six coarse-neighbor faces.
    auto operator<=>(const ContentKey&) const = default;
};

struct PreparedPayload
{
    std::shared_ptr<Mesh> mesh;
    size_t cpuBytes = 0, gpuBytes = 0;
    bool empty = false;
    bool certifiedEmpty = false;
    uint64_t lastSubmission = 0;
};

struct BuildRequest
{
    RequestId id;
    ContentKey key;
    size_t byteLimit = 0;
};

struct CutEntry
{
    ContentKey key;
    std::shared_ptr<PreparedPayload> payload;
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
    size_t maximumPlansPerUpdate = 64;
};

struct StreamDiagnostics
{
    size_t nodes = 0, pending = 0, loading = 0, loaded = 0, failed = 0;
    size_t selected = 0, admittedRoots = 0, loadingRoots = 0, jobs = 0;
    size_t residentCpuBytes = 0, residentGpuBytes = 0;
    size_t reservedCpuBytes = 0, reservedGpuBytes = 0, retiredCpuBytes = 0, retiredGpuBytes = 0;
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
    void retry(ContentKey key);
    // Withdraws all coverage, invalidates requests and advances the terrain epoch.
    void reset();
    // Reconciles demand, cancellation, complete-group reservations and prepared cut publication.
    void update();
    // Transfers newly dispatched immutable requests to the worker adapter.
    std::vector<BuildRequest> takeRequests();
    // Transfers cancellation notifications; reservations survive until acknowledge or completion.
    std::vector<RequestId> takeCancellations();
    // Accepts a worker's validated nonempty CPU result once; true permits upload, not display.
    bool acceptGenerated(RequestId request, size_t cpuBytes);
    // Publishes an ordered/prepared upload or explicit empty result, rejecting obsolete identities.
    bool complete(RequestId request, std::shared_ptr<PreparedPayload> payload);
    // Records a terminal generation/upload error without replacing displayed fallback.
    void fail(RequestId request, std::string error, FailureStage stage = FailureStage::Generation);
    // Retires buffers from a failed partial upload before releasing its job reservation.
    void failUpload(RequestId request, std::shared_ptr<PreparedPayload> payload, std::string error);
    // Finalizes a cancelled job; repeated acknowledgement is harmless.
    void acknowledgeCancellation(RequestId request);
    // Returns a retaining snapshot; view culling must not change its spatial coverage.
    const std::vector<CutEntry>& cut() const noexcept;
    // Records actual use of a retaining snapshot; submissions must be monotonically numbered.
    void submitted(const std::vector<CutEntry>& snapshot, uint64_t serial);
    // Advances actual GPU completion and releases retired data after other owners disappear.
    void completedSubmission(uint64_t serial);
    // Reports live content, reservations and retirement without counting the same allocation twice.
    StreamDiagnostics diagnostics() const;
    // Observes an existing slot; absent node/content returns nullopt.
    std::optional<BuildState> state(ContentKey key) const;
    // Returns a slot's currently accepted identity, if any.
    std::optional<RequestId> request(ContentKey key) const;
    // Validates coverage, ancestry, readiness, balance and accounting; throws on broken invariants.
    void validate() const;

private:
    struct Slot;
    struct Node;
    struct Job;
    struct Plan;
    // Reconciles one bounded pass; update invalidates attempted work if allocation interrupts this pass.
    void reconcile();
    // Resolves an address from its root without allocating missing nodes.
    Node* find(NodeAddress address) const;
    // Allocates all eight children together and derives their initial distance demand.
    bool createChildren(Node& node);
    // Returns a content slot, allocating only a requested boundary variant.
    Slot& slot(ContentKey key);
    // Finds a matching live owner; cancelled/obsolete identities do not resolve.
    Slot* owner(RequestId request);
    // Computes effective distance/override demand; a certificate or depth cap forbids splitting.
    bool wantsChildren(const Node& node) const;
    // Builds a complete candidate and its changed boundary dependencies for one local operation.
    std::optional<Plan> makePlan(NodeAddress seed, int operation);
    // Dispatches bounded dependency groups; sibling bases reserve together and publication still waits for all.
    bool schedule(Plan& plan);
    // Commits a prepared plan against the current cut; publication uses a no-throw swap.
    bool publish(const Plan& plan);
    // Invalidates work outside required content, retaining cancelled job accounting.
    void cancelUnused(const std::set<ContentKey>& required);
    // Retires unused variants and entire child groups after cut/dependency ownership is established.
    void prune(const std::set<ContentKey>& required);
    // Transfers a payload to retirement; a snapshot may still retain it.
    void retire(Slot& slot);
    // Calculates transition masks for a candidate using exact integer face adjacency.
    static std::vector<ContentKey> boundaryKeys(const std::vector<NodeAddress>& addresses);

    StreamSettings m_settings;
    WorldPosition m_observer;
    std::map<CellAddress, std::unique_ptr<Node>> m_roots;
    std::set<CellAddress> m_wantedRoots;
    std::map<NodeAddress, bool> m_overrides;
    std::map<RequestId, Job> m_jobs;
    std::vector<BuildRequest> m_dispatch;
    std::vector<RequestId> m_cancellations;
    std::vector<CutEntry> m_cut;
    std::vector<std::shared_ptr<PreparedPayload>> m_retired;
    std::map<std::pair<NodeAddress, int>, std::set<ContentKey>> m_pendingRequirements;
    // Wakes blocked/held planning after an input changes, preserving nearest-first order on every new pass.
    void invalidatePlans() noexcept;
    std::set<std::pair<NodeAddress, int>> m_attemptedOperations;
    uint64_t m_epoch = 1, m_sequence = 0, m_publications = 0, m_staleResults = 0;
    uint64_t m_completedSubmission = 0, m_lastSubmission = 0;
    bool m_budgetBlocked = false;
    bool m_idle = false;
};
} // namespace ofg::terrain
