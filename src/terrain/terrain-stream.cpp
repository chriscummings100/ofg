// Complete demand and availability passes select balanced coverage while retaining needed ancestor payloads.
#include "terrain/terrain-profile.h"
#include "terrain/terrain-stream.h"
#include "core/engine-error.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace ofg::terrain {
size_t AddressHash::operator()(const NodeAddress& address) const noexcept
{
    uint64_t value = uint64_t(address.cell.x) * 0x9e3779b97f4a7c15ull;
    value ^= uint64_t(address.cell.y) * 0xbf58476d1ce4e5b9ull;
    value ^= uint64_t(address.cell.z) * 0x94d049bb133111ebull;
    value ^= uint64_t(address.x) * 0xd6e8feb86659fd93ull;
    value ^= uint64_t(address.y) * 0xa0761d6478bd642full;
    value ^= uint64_t(address.z) * 0xe7037ed1a0b428dbull;
    value ^= uint64_t(address.depth) * 0x8ebc6af09c88c6e3ull;
    return size_t(value ^ (value >> 32));
}
namespace {
using LeafSet = std::unordered_set<NodeAddress, AddressHash>;

// Finds the leaf across a face at equal/coarser depth. Finer neighbors find us in their own traversal.
std::optional<NodeAddress> faceNeighbor(NodeAddress a, unsigned face, const LeafSet& leaves)
{
    uint32_t* coordinate = face / 2 == 0 ? &a.x : face / 2 == 1 ? &a.y : &a.z;
    int64_t* cell = face / 2 == 0 ? &a.cell.x : face / 2 == 1 ? &a.cell.y : &a.cell.z;
    const uint32_t side = 1u << a.depth;
    if (face & 1)
    {
        if (++*coordinate == side)
        {
            if (*cell == INT64_MAX)
            {
                return {};
            }
            ++*cell;
            *coordinate = 0;
        }
    }
    else if (*coordinate)
    {
        --*coordinate;
    }
    else
    {
        if (*cell == INT64_MIN)
        {
            return {};
        }
        --*cell;
        *coordinate = side - 1;
    }
    for (;;)
    {
        if (leaves.contains(a))
        {
            return a;
        }
        if (!a.depth)
        {
            return {};
        }
        a = parentAddress(a);
    }
}
// Selects prebuilt fine-to-coarse faces using only the topology being published.
uint8_t transitionFaces(NodeAddress address, const LeafSet& leaves)
{
    uint8_t mask = 0;
    for (unsigned face = 0; face < 6; ++face)
    {
        const auto neighbor = faceNeighbor(address, face, leaves);
        if (neighbor && address.depth == neighbor->depth + 1)
        {
            mask |= uint8_t(1u << face);
        }
    }
    return mask;
}
} // namespace

struct TerrainStream::Slot
{
    BuildState state = BuildState::Pending;
    std::optional<RequestId> request;
    std::shared_ptr<const ReadyContent> payload;
    std::string error;
};

struct TerrainStream::Node
{
    NodeAddress address;
    Slot content;
    std::array<std::unique_ptr<Node>, 8> children;
    bool distanceWantsChildren = false;
    bool admitted = false;
};

struct TerrainStream::Job
{
    BuildRequest request;
    bool cancelled = false;
    bool generated = false;
};

TerrainStream::TerrainStream(StreamSettings settings)
    : m_settings(settings)
{
    if (!std::isfinite(settings.rootWidth) || settings.rootWidth <= 0 || settings.maximumDepth > 16 ||
        !settings.maximumPayloadBytes || !settings.maximumJobs || !settings.maximumNodes ||
        std::isnan(settings.refinementRange) || settings.refinementRange < 0)
    {
        throw EngineError("Invalid terrain stream settings.");
    }
}

TerrainStream::~TerrainStream() = default;

void TerrainStream::invalidateSelection() noexcept
{
    m_idle = false;
    m_budgetBlocked = m_metadataBlocked;
}

bool TerrainStream::requestRoot(CellAddress cell)
{
    if (m_wantedRoots.contains(cell))
    {
        return true;
    }
    invalidateSelection();
    if (!m_roots.contains(cell))
    {
        if (m_index.size() >= m_settings.maximumNodes)
        {
            m_budgetBlocked = true;
            return false;
        }
        auto node = std::make_unique<Node>();
        node->address.cell = cell;
        auto* pointer = node.get();
        m_index.emplace(node->address, pointer);
        try
        {
            m_roots.emplace(cell, std::move(node));
        } catch (...)
        {
            m_index.erase(NodeAddress{cell});
            throw;
        }
    }
    m_demandDirty = true;
    m_wantedRoots.insert(cell);
    return true;
}

void TerrainStream::withdrawRoot(CellAddress cell)
{
    if (m_wantedRoots.erase(cell))
    {
        m_demandDirty = true;
        invalidateSelection();
    }
}

void TerrainStream::setObserver(WorldPosition observer)
{
    observer = normalizePosition(observer, m_settings.rootWidth);
    if (observer.cell != m_observer.cell || observer.local != m_observer.local)
    {
        m_observer = observer;
        m_demandDirty = true;
        invalidateSelection();
    }
}

void TerrainStream::setRefinement(NodeAddress node, std::optional<bool> refine)
{
    validateAddress(node);
    m_demandDirty = true;
    invalidateSelection();
    if (refine)
    {
        m_overrides[node] = *refine;
    }
    else
    {
        m_overrides.erase(node);
    }
}

TerrainStream::Node* TerrainStream::find(NodeAddress address) const
{
    const auto found = m_index.find(address);
    return found == m_index.end() ? nullptr : found->second;
}

bool TerrainStream::createChildren(Node& node)
{
    if (node.children[0])
    {
        return true;
    }
    if (m_settings.maximumNodes - m_index.size() < 8)
    {
        m_metadataBlocked = true;
        m_budgetBlocked = true;
        return false;
    }
    const auto addresses = childAddresses(node.address);
    std::array<std::unique_ptr<Node>, 8> children;
    for (size_t i = 0; i < 8; ++i)
    {
        children[i] = std::make_unique<Node>();
        children[i]->address = addresses[i];
        children[i]->distanceWantsChildren = distanceToNode(addresses[i], m_observer, m_settings.rootWidth) <
                                             2 * std::ldexp(m_settings.rootWidth, -addresses[i].depth);
    }
    try
    {
        for (const auto& child : children)
        {
            m_index.emplace(child->address, child.get());
        }
    } catch (...)
    {
        for (const auto& address : addresses)
        {
            m_index.erase(address);
        }
        throw;
    }
    node.children.swap(children);
    return true;
}

TerrainStream::Slot& TerrainStream::slot(NodeAddress address)
{
    auto* node = find(address);
    if (!node)
    {
        throw EngineError("Terrain content owner does not exist.");
    }
    return node->content;
}

TerrainStream::Slot* TerrainStream::owner(RequestId id)
{
    const auto job = m_jobs.find(id);
    if (job == m_jobs.end() || job->second.cancelled)
    {
        return nullptr;
    }
    auto* node = find(job->second.request.address);
    if (!node || node->content.request != id || node->content.state != BuildState::Loading)
    {
        return nullptr;
    }
    return &node->content;
}

bool TerrainStream::wantsChildren(const Node& node) const
{
    const auto& base = node.content;
    if (node.address.depth >= m_settings.maximumDepth || (base.payload && base.payload->certifiedEmpty))
    {
        return false;
    }
    const auto override = m_overrides.find(node.address);
    return override == m_overrides.end() ? node.distanceWantsChildren : override->second;
}

void TerrainStream::discoverDemand()
{
    OFG_TERRAIN_SCOPE(TerrainStream_discoverDemand);
    m_metadataBlocked = false;
    m_budgetBlocked = false;
    LeafSet desired;
    std::vector<Node*> level;
    for (const auto& cell : m_wantedRoots)
    {
        level.push_back(find(NodeAddress{cell}));
    }
    while (!level.empty())
    {
        std::sort(
            level.begin(),
            level.end(),
            [&](const auto* a, const auto* b)
            {
                const double da = distanceToNode(a->address, m_observer, m_settings.rootWidth);
                const double db = distanceToNode(b->address, m_observer, m_settings.rootWidth);
                return da != db ? da < db : a->address < b->address;
            }
        );
        std::vector<Node*> next;
        for (auto* node : level)
        {
            const double distance = distanceToNode(node->address, m_observer, m_settings.rootWidth);
            const double width = std::ldexp(m_settings.rootWidth, -node->address.depth);
            if (distance < 2 * width)
            {
                node->distanceWantsChildren = true;
            }
            else if (distance > 2.5 * width)
            {
                node->distanceWantsChildren = false;
            }
            if (distance <= m_settings.refinementRange && wantsChildren(*node) && createChildren(*node))
            {
                for (auto& child : node->children)
                {
                    next.push_back(child.get());
                }
            }
            else
            {
                desired.insert(node->address);
            }
        }
        level.swap(next);
    }
    balance(desired, true);
    std::set<NodeAddress> required;
    for (auto address : desired)
    {
        for (;;)
        {
            required.insert(address);
            if (!address.depth)
            {
                break;
            }
            address = parentAddress(address);
        }
    }
    m_desired.swap(desired);
    m_required.swap(required);
    m_demandDirty = false;
}

void TerrainStream::balance(LeafSet& leaves, bool allowRefinement)
{
    // Collect a complete batch before mutating the cut; fine leaves discover coarse neighbours.
    for (;;)
    {
        std::vector<std::pair<NodeAddress, NodeAddress>> violations;
        for (const auto& fine : leaves)
        {
            for (unsigned face = 0; face < 6; ++face)
            {
                auto coarse = faceNeighbor(fine, face, leaves);
                if (coarse && fine.depth > coarse->depth + 1)
                {
                    violations.emplace_back(fine, *coarse);
                }
            }
        }
        if (violations.empty())
        {
            return;
        }
        std::sort(
            violations.begin(),
            violations.end(),
            [&](const auto& a, const auto& b)
            {
                return std::tuple(
                           a.second.depth,
                           distanceToNode(a.second, m_observer, m_settings.rootWidth),
                           a.second,
                           a.first
                       ) <
                       std::tuple(
                           b.second.depth,
                           distanceToNode(b.second, m_observer, m_settings.rootWidth),
                           b.second,
                           b.first
                       );
            }
        );
        for (const auto& [fine, coarse] : violations)
        {
            if (!leaves.contains(fine) || !leaves.contains(coarse))
            {
                continue;
            }
            if (allowRefinement && createChildren(*find(coarse)))
            {
                leaves.erase(coarse);
                for (const auto& child : childAddresses(coarse))
                {
                    leaves.insert(child);
                }
            }
            else
            {
                allowRefinement = false;
                auto ancestor = fine;
                while (ancestor.depth > coarse.depth + 1)
                {
                    ancestor = parentAddress(ancestor);
                }
                std::erase_if(
                    leaves,
                    [&](const auto& address)
                    {
                        return isAncestor(ancestor, address);
                    }
                );
                leaves.insert(ancestor);
            }
        }
    }
}

void TerrainStream::selectReady()
{
    OFG_TERRAIN_SCOPE(TerrainStream_selectReady);
    LeafSet selected;
    std::vector<Node*> queue;
    for (const auto& cell : m_wantedRoots)
    {
        auto* root = find(NodeAddress{cell});
        if (root->content.state == BuildState::Loaded)
        {
            queue.push_back(root);
        }
    }
    for (size_t i = 0; i < queue.size(); ++i)
    {
        auto* node = queue[i];
        if (!m_desired.contains(node->address) && node->children[0] &&
            std::all_of(
                node->children.begin(),
                node->children.end(),
                [](const auto& child)
                {
                    return child->content.state == BuildState::Loaded;
                }
            ))
        {
            for (auto& child : node->children)
            {
                queue.push_back(child.get());
            }
        }
        else
        {
            selected.insert(node->address);
        }
    }
    balance(selected, false);
    std::vector<CutEntry> next;
    next.reserve(selected.size());
    for (const auto& address : selected)
    {
        next.push_back({address, slot(address).payload, transitionFaces(address, selected)});
    }
    std::sort(
        next.begin(),
        next.end(),
        [](const auto& a, const auto& b)
        {
            return a.address < b.address;
        }
    );
    const bool same = next.size() == m_cut.size() && std::equal(
                                                         next.begin(),
                                                         next.end(),
                                                         m_cut.begin(),
                                                         [](const auto& a, const auto& b)
                                                         {
                                                             return a.address == b.address && a.payload == b.payload &&
                                                                    a.transitionFaces == b.transitionFaces;
                                                         }
                                                     );
    if (!same)
    {
        m_cut.swap(next);
        ++m_publications;
    }
    for (auto& [cell, root] : m_roots)
    {
        root->admitted = m_wantedRoots.contains(cell) && root->content.state == BuildState::Loaded;
    }
}

void TerrainStream::schedule()
{
    OFG_TERRAIN_SCOPE(TerrainStream_schedule);
    struct Group
    {
        NodeAddress address;
        uint8_t depth;
        double distance;
        std::vector<NodeAddress> pending;
    };
    std::vector<Group> groups;
    for (const auto& address : m_required)
    {
        auto& node = *find(address);
        std::vector<NodeAddress> pending;
        if (!address.depth && node.content.state == BuildState::Pending)
        {
            pending.push_back(address);
            groups.push_back({address, 0, distanceToNode(address, m_observer, m_settings.rootWidth), pending});
        }
        if (node.content.state != BuildState::Loaded || m_desired.contains(address) || !node.children[0])
        {
            continue;
        }
        bool failed = false;
        for (const auto& child : node.children)
        {
            failed = failed || child->content.state == BuildState::Failed;
            if (child->content.state == BuildState::Pending)
            {
                pending.push_back(child->address);
            }
        }
        if (!failed && !pending.empty())
        {
            groups.push_back(
                {address,
                 uint8_t(address.depth + 1),
                 distanceToNode(address, m_observer, m_settings.rootWidth),
                 std::move(pending)}
            );
        }
    }
    std::sort(
        groups.begin(),
        groups.end(),
        [](const auto& a, const auto& b)
        {
            if (a.depth != b.depth)
                return a.depth < b.depth;
            if (a.distance != b.distance)
                return a.distance < b.distance;
            return a.address < b.address;
        }
    );
    const auto counts = diagnostics();
    size_t cpu = counts.residentCpuBytes + counts.retiredCpuBytes + counts.reservedCpuBytes;
    size_t gpu = counts.residentGpuBytes + counts.retiredGpuBytes + counts.reservedGpuBytes;
    for (auto& group : groups)
    {
        const auto count = group.pending.size();
        if (cpu > m_settings.cpuBudget || gpu > m_settings.gpuBudget ||
            count > (m_settings.cpuBudget - cpu) / m_settings.maximumPayloadBytes ||
            count > (m_settings.gpuBudget - gpu) / m_settings.maximumPayloadBytes ||
            count > m_settings.maximumJobs - std::min(m_settings.maximumJobs, m_jobs.size()))
        {
            m_budgetBlocked = true;
            continue;
        }
        std::sort(
            group.pending.begin(),
            group.pending.end(),
            [&](const auto& a, const auto& b)
            {
                const auto da = distanceToNode(a, m_observer, m_settings.rootWidth);
                const auto db = distanceToNode(b, m_observer, m_settings.rootWidth);
                return da != db ? da < db : a < b;
            }
        );
        // All throwing allocations precede slot mutation: a sibling reservation commits together.
        std::map<RequestId, Job> jobs;
        std::vector<BuildRequest> dispatch;
        for (const auto& address : group.pending)
        {
            if (m_sequence == UINT64_MAX)
                throw EngineError("Terrain request sequence exhausted.");
            BuildRequest request{{m_epoch, ++m_sequence}, address, m_settings.maximumPayloadBytes};
            jobs.emplace(request.id, Job{request});
            dispatch.push_back(request);
        }
        m_dispatch.reserve(m_dispatch.size() + dispatch.size());
        m_jobs.merge(jobs);
        for (const auto& request : dispatch)
        {
            auto& content = slot(request.address);
            content.state = BuildState::Loading;
            content.request = request.id;
            content.error.clear();
            m_dispatch.push_back(request);
        }
        cpu += count * m_settings.maximumPayloadBytes;
        gpu += count * m_settings.maximumPayloadBytes;
    }
}

void TerrainStream::cancelUnused(const std::set<NodeAddress>& required)
{
    for (auto& [id, job] : m_jobs)
    {
        if (job.cancelled || required.contains(job.request.address))
        {
            continue;
        }
        if (auto* content = owner(id))
        {
            content->request.reset();
            content->state = BuildState::Pending;
        }
        job.cancelled = true;
        m_cancellations.push_back(id);
    }
}

void TerrainStream::retire(Slot& content)
{
    if (content.payload)
    {
        if (!content.payload->empty)
        {
            const auto id = content.payload->id;
            // Reserve the message first so failed allocation cannot lose the release obligation.
            m_retirements.reserve(m_retirements.size() + 1);
            m_retired.emplace(id, content.payload);
            m_retirements.emplace_back(id, m_publications);
        }
        content.payload.reset();
        content.state = BuildState::Pending;
    }
}

void TerrainStream::prune(const std::set<NodeAddress>& required)
{
    OFG_TERRAIN_SCOPE(TerrainStream_prune);
    // Visit children before parents so a discarded branch transfers each allocation exactly once.
    std::function<bool(Node&)> visit = [&](Node& node)
    {
        bool retain = required.contains(node.address);
        bool retainChildren = false;
        for (auto& child : node.children)
        {
            if (child)
            {
                retainChildren = visit(*child) || retainChildren;
            }
        }
        if (!retainChildren)
        {
            for (auto& child : node.children)
            {
                if (child)
                    unindex(*child);
                child.reset();
            }
        }
        if (!retain)
        {
            retire(node.content);
        }
        return retain || retainChildren;
    };
    for (auto root = m_roots.begin(); root != m_roots.end();)
    {
        const bool retained = visit(*root->second);
        if (!retained && !m_wantedRoots.contains(root->first))
        {
            m_index.erase(root->second->address);
            root = m_roots.erase(root);
        }
        else
        {
            ++root;
        }
    }
}

void TerrainStream::update()
{
    if (m_idle)
    {
        return;
    }
    try
    {
        reconcile();
    } catch (...)
    {
        // Retry complete demand after allocation failure; the last published cut remains valid.
        m_demandDirty = true;
        invalidateSelection();
        throw;
    }
}

void TerrainStream::unindex(Node& node)
{
    for (auto& child : node.children)
    {
        if (child)
            unindex(*child);
    }
    m_index.erase(node.address);
}

void TerrainStream::reconcile()
{
    OFG_TERRAIN_SCOPE(TerrainStream_reconcile);
    // At most one cleanup retry; discarded speculative groups cannot cause an idle retry loop.
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        if (m_demandDirty)
            discoverDemand();
        selectReady();
        auto required = m_required;
        for (const auto& entry : m_cut)
        {
            auto address = entry.address;
            for (;;)
            {
                required.insert(address);
                if (!address.depth)
                    break;
                address = parentAddress(address);
            }
        }
        m_cancellations.reserve(m_cancellations.size() + m_jobs.size());
        m_retirements.reserve(m_retirements.size() + m_index.size());
        cancelUnused(required);
        const auto before = m_index.size();
        prune(required);
        if (!pass && before != m_index.size() && m_budgetBlocked)
        {
            m_demandDirty = true;
            continue;
        }
        break;
    }
    schedule();
    m_idle = true;
}

std::vector<BuildRequest> TerrainStream::takeRequests()
{
    std::vector<BuildRequest> result;
    result.swap(m_dispatch);
    // A queued request may have been cancelled before the adapter first observes it.
    std::erase_if(
        result,
        [&](const auto& request)
        {
            return !owner(request.id);
        }
    );
    return result;
}

std::vector<RequestId> TerrainStream::takeCancellations()
{
    std::vector<RequestId> result;
    result.swap(m_cancellations);
    return result;
}

bool TerrainStream::acceptGenerated(RequestId id, size_t cpuBytes)
{
    auto* content = owner(id);
    if (!content)
    {
        ++m_staleResults;
        if (!awaitingUpload(id))
            acknowledgeCancellation(id);
        return false;
    }
    auto& job = m_jobs.at(id);
    if (job.generated)
    {
        ++m_staleResults;
        return false;
    }
    if (cpuBytes > job.request.byteLimit)
    {
        fail(id, "Terrain output exceeds its reserved byte limit.");
        return false;
    }
    job.generated = true;
    return true;
}

bool TerrainStream::complete(RequestId id, ReadyContent payload)
{
    auto* content = owner(id);
    if (!content)
    {
        ++m_staleResults;
        // An upload retains its reservation until the renderer confirms resource release.
        if (!awaitingUpload(id))
            acknowledgeCancellation(id);
        return false;
    }
    const auto& job = m_jobs.at(id);
    if (payload.cpuBytes > job.request.byteLimit || payload.gpuBytes > job.request.byteLimit ||
        (!payload.empty && !job.generated) || (payload.certifiedEmpty && !payload.empty))
    {
        fail(id, "Invalid prepared terrain payload or missing CPU completion.", FailureStage::Upload);
        return false;
    }
    payload.id = id;
    auto ready = std::make_shared<const ReadyContent>(payload);
    m_demandDirty = m_demandDirty || payload.certifiedEmpty;
    content->payload = std::move(ready);
    invalidateSelection();
    content->state = BuildState::Loaded;
    content->request.reset();
    m_jobs.erase(id);
    return true;
}

void TerrainStream::fail(RequestId id, std::string error, FailureStage stage)
{
    const auto job = m_jobs.find(id);
    if (job != m_jobs.end() && job->second.generated && stage == FailureStage::Generation)
    {
        ++m_staleResults;
        return;
    }
    if (auto* content = owner(id))
    {
        content->request.reset();
        content->state = BuildState::Failed;
        content->error = std::move(error);
    }
    else
    {
        ++m_staleResults;
    }
    if (job != m_jobs.end() && job->second.generated && stage == FailureStage::Upload)
    {
        if (!job->second.cancelled)
            m_cancellations.push_back(id);
        job->second.cancelled = true;
        invalidateSelection();
    }
    else if (m_jobs.erase(id))
    {
        invalidateSelection();
    }
}

void TerrainStream::acknowledgeCancellation(RequestId id)
{
    const auto job = m_jobs.find(id);
    if (job != m_jobs.end() && job->second.cancelled)
    {
        m_jobs.erase(job);
        invalidateSelection();
    }
}

void TerrainStream::retry(NodeAddress address)
{
    auto* node = find(address);
    if (node && node->content.state == BuildState::Failed)
    {
        node->content.state = BuildState::Pending;
        node->content.error.clear();
        invalidateSelection();
    }
}

void TerrainStream::retryFailures()
{
    for (const auto& [address, node] : m_index)
        retry(address);
}

void TerrainStream::abandonRenderer()
{
    if (!m_cut.empty() || !m_wantedRoots.empty())
    {
        throw EngineError("Renderer accounting can only detach after terrain shutdown.");
    }
    m_retired.clear();
    std::erase_if(
        m_jobs,
        [](const auto& item)
        {
            return item.second.cancelled && item.second.generated;
        }
    );
    invalidateSelection();
}

void TerrainStream::reset()
{
    invalidateSelection();
    if (m_epoch == UINT64_MAX)
    {
        throw EngineError("Terrain epoch exhausted.");
    }
    ++m_epoch;
    m_wantedRoots.clear();
    m_desired.clear();
    m_required.clear();
    m_demandDirty = true;
    m_overrides.clear();
    m_cut.clear();
    cancelUnused({});
    prune({});
    ++m_publications;
}

const std::vector<CutEntry>& TerrainStream::cut() const noexcept
{
    return m_cut;
}

std::vector<std::pair<RequestId, uint64_t>> TerrainStream::takeRetirements()
{
    std::vector<std::pair<RequestId, uint64_t>> result;
    result.swap(m_retirements);
    return result;
}

void TerrainStream::releasePayload(RequestId id)
{
    if (m_retired.erase(id))
    {
        m_demandDirty = true;
        invalidateSelection();
    }
    acknowledgeCancellation(id);
}

bool TerrainStream::awaitingUpload(RequestId id) const
{
    const auto job = m_jobs.find(id);
    return job != m_jobs.end() && job->second.generated;
}

StreamDiagnostics TerrainStream::diagnostics() const
{
    OFG_TERRAIN_SCOPE(TerrainStream_diagnostics);
    StreamDiagnostics result;
    std::function<void(const Node&)> visit = [&](const Node& node)
    {
        ++result.nodes;
        const auto& content = node.content;
        {
            switch (content.state)
            {
            case BuildState::Pending:
                ++result.pending;
                break;
            case BuildState::Loading:
                ++result.loading;
                break;
            case BuildState::Loaded:
                ++result.loaded;
                break;
            case BuildState::Failed:
                ++result.failed;
                break;
            }
            if (content.payload)
            {
                result.residentCpuBytes += content.payload->cpuBytes;
                result.residentGpuBytes += content.payload->gpuBytes;
            }
        }
        for (const auto& child : node.children)
        {
            if (child)
            {
                visit(*child);
            }
        }
    };
    for (const auto& [cell, root] : m_roots)
    {
        visit(*root);
        result.admittedRoots += root->admitted;
    }
    std::set<CellAddress> admitted;
    for (const auto& entry : m_cut)
    {
        admitted.insert(entry.address.cell);
        result.unresolvedRefinements += !m_desired.contains(entry.address);
        if (!entry.payload->empty)
        {
            result.deepestSurfaceDepth = std::max(result.deepestSurfaceDepth, entry.address.depth);
        }
    }
    for (const auto& cell : m_wantedRoots)
    {
        result.loadingRoots += !admitted.contains(cell);
    }
    for (const auto& [id, job] : m_jobs)
    {
        result.reservedCpuBytes += job.request.byteLimit;
        result.reservedGpuBytes += job.request.byteLimit;
    }
    for (const auto& [id, payload] : m_retired)
    {
        result.retiredCpuBytes += payload->cpuBytes;
        result.retiredGpuBytes += payload->gpuBytes;
    }
    result.selected = m_cut.size();
    result.jobs = m_jobs.size();
    result.publications = m_publications;
    result.staleResults = m_staleResults;
    result.budgetBlocked = m_budgetBlocked;
    result.planningIdle = m_idle && m_jobs.empty();
    return result;
}

std::optional<BuildState> TerrainStream::state(NodeAddress address) const
{
    const auto* node = find(address);
    return node ? std::optional<BuildState>{node->content.state} : std::nullopt;
}

std::optional<RequestId> TerrainStream::request(NodeAddress address) const
{
    const auto* node = find(address);
    return node ? node->content.request : std::nullopt;
}

void TerrainStream::validate() const
{
    std::map<CellAddress, double> coverage;
    std::vector<NodeAddress> addresses;
    for (const auto& entry : m_cut)
    {
        const auto* node = find(entry.address);
        if (!node || !entry.payload || state(entry.address) != BuildState::Loaded)
        {
            throw EngineError("Terrain cut contains unprepared content.");
        }
        coverage[entry.address.cell] += std::ldexp(1.0, -3 * entry.address.depth);
        addresses.push_back(entry.address);
        auto address = entry.address;
        for (;;)
        {
            if (state(address) != BuildState::Loaded)
            {
                throw EngineError("Terrain cut lost a retained ancestor base.");
            }
            if (!address.depth)
            {
                break;
            }
            address = parentAddress(address);
        }
    }
    for (const auto& [cell, volume] : coverage)
    {
        if (volume != 1)
        {
            throw EngineError("Terrain cut does not cover an admitted root.");
        }
    }
    for (const auto& [cell, root] : m_roots)
    {
        if (root->admitted != coverage.contains(cell))
        {
            throw EngineError("Terrain cut lost or invented admitted root coverage.");
        }
    }
    for (size_t i = 0; i < addresses.size(); ++i)
    {
        for (size_t j = i + 1; j < addresses.size(); ++j)
        {
            if (addresses[i] == addresses[j] || isAncestor(addresses[i], addresses[j]) ||
                isAncestor(addresses[j], addresses[i]))
            {
                throw EngineError("Terrain cut overlaps itself.");
            }
            if (sharedFace(addresses[i], addresses[j]) >= 0 &&
                std::abs(int(addresses[i].depth) - int(addresses[j].depth)) > 1)
            {
                throw EngineError("Terrain cut exceeds the 2:1 neighbor spacing limit.");
            }
        }
    }
    const LeafSet leaves(addresses.begin(), addresses.end());
    for (const auto& entry : m_cut)
    {
        if (transitionFaces(entry.address, leaves) != entry.transitionFaces)
        {
            throw EngineError("Terrain cut has stale boundary geometry.");
        }
    }
    const auto d = diagnostics();
    if (d.residentCpuBytes + d.retiredCpuBytes + d.reservedCpuBytes > m_settings.cpuBudget ||
        d.residentGpuBytes + d.retiredGpuBytes + d.reservedGpuBytes > m_settings.gpuBudget)
    {
        throw EngineError("Terrain content exceeds its reserved budget.");
    }
}
} // namespace ofg::terrain
