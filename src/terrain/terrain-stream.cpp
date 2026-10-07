// Terrain replacement plans keep current coverage alive until complete compatible payloads are prepared.
#include "terrain/terrain-profile.h"
#include "terrain/terrain-stream.h"
#include "core/engine-error.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_set>

namespace ofg::terrain {
namespace {
// Concrete spatial lookup for transient candidate cuts; iteration order never determines publication order.
struct AddressHash
{
    // Combines exact integer coordinates, preserving distant-cell identity without floating conversion.
    size_t operator()(const NodeAddress& address) const noexcept
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
};
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
    std::shared_ptr<PreparedPayload> payload;
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

struct TerrainStream::Plan
{
    std::vector<NodeAddress> candidate;
    std::set<NodeAddress> required;
    std::optional<CellAddress> admitting, removing;
};

TerrainStream::TerrainStream(StreamSettings settings)
    : m_settings(settings)
{
    if (!std::isfinite(settings.rootWidth) || settings.rootWidth <= 0 || settings.maximumDepth > 16 ||
        !settings.maximumPayloadBytes || !settings.maximumJobs || !settings.maximumNodes ||
        !settings.maximumPlansPerUpdate)
    {
        throw EngineError("Invalid terrain stream settings.");
    }
}

TerrainStream::~TerrainStream() = default;

void TerrainStream::invalidatePlans() noexcept
{
    m_idle = false;
    m_budgetBlocked = false;
    m_attemptedOperations.clear();
}

bool TerrainStream::requestRoot(CellAddress cell)
{
    if (m_wantedRoots.contains(cell))
    {
        return true;
    }
    invalidatePlans();
    if (!m_roots.contains(cell))
    {
        if (diagnostics().nodes >= m_settings.maximumNodes)
        {
            m_budgetBlocked = true;
            return false;
        }
        auto node = std::make_unique<Node>();
        node->address.cell = cell;
        m_roots.emplace(cell, std::move(node));
    }
    m_wantedRoots.insert(cell);
    return true;
}

void TerrainStream::withdrawRoot(CellAddress cell)
{
    if (m_wantedRoots.erase(cell))
    {
        invalidatePlans();
    }
}

void TerrainStream::setObserver(WorldPosition observer)
{
    observer = normalizePosition(observer, m_settings.rootWidth);
    if (observer.cell != m_observer.cell || observer.local != m_observer.local)
    {
        m_observer = observer;
        // Recheck distance demand, but sub-threshold motion must not restart blocked work ahead of every other plan.
        m_idle = false;
    }
}

void TerrainStream::setRefinement(NodeAddress node, std::optional<bool> refine)
{
    validateAddress(node);
    invalidatePlans();
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
    const auto root = m_roots.find(address.cell);
    if (root == m_roots.end())
    {
        return nullptr;
    }
    auto* node = root->second.get();
    for (int level = address.depth - 1; level >= 0; --level)
    {
        const auto index =
            ((address.x >> level) & 1) | (((address.y >> level) & 1) << 1) | (((address.z >> level) & 1) << 2);
        node = node->children[index].get();
        if (!node)
        {
            return nullptr;
        }
    }
    return node;
}

bool TerrainStream::createChildren(Node& node)
{
    if (node.children[0])
    {
        return true;
    }
    if (m_settings.maximumNodes - diagnostics().nodes < 8)
    {
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

std::optional<TerrainStream::Plan> TerrainStream::makePlan(NodeAddress seed, Operation operation)
{
    OFG_TERRAIN_SCOPE(TerrainStream_makePlan);
    auto* node = find(seed);
    if (!node || (operation == Operation::Admit && node->admitted) ||
        (operation == Operation::Split && !wantsChildren(*node)) ||
        (operation == Operation::Merge && (!node->children[0] || wantsChildren(*node))))
    {
        return {};
    }
    std::vector<NodeAddress> addresses;
    for (const auto& entry : m_cut)
    {
        addresses.push_back(entry.address);
    }

    if (operation == Operation::Admit) // Admit a root, possibly with a refined initial boundary.
    {
        if (std::any_of(
                addresses.begin(),
                addresses.end(),
                [&](auto a)
                {
                    return a.cell == seed.cell;
                }
            ))
        {
            return {};
        }
        addresses.push_back(seed);
    }
    else if (operation == Operation::Split) // Replace one leaf by all eight children.
    {
        const auto selected = std::find(addresses.begin(), addresses.end(), seed);
        if (selected == addresses.end() || !wantsChildren(*node))
        {
            return {};
        }
        if (!createChildren(*node))
        {
            return {};
        }
        addresses.erase(selected);
        for (const auto& child : node->children)
        {
            addresses.push_back(child->address);
        }
    }
    else if (operation == Operation::Merge) // Merge exactly one level, never skip displayed descendants.
    {
        if (!node->children[0] || wantsChildren(*node))
        {
            return {};
        }
        for (const auto& child : node->children)
        {
            if (std::find(addresses.begin(), addresses.end(), child->address) == addresses.end())
            {
                return {};
            }
        }
        std::erase_if(
            addresses,
            [&](auto a)
            {
                return isAncestor(seed, a);
            }
        );
        addresses.push_back(seed);
    }
    else // Explicitly withdraw root coverage; remaining boundaries still prepare atomically.
    {
        std::erase_if(
            addresses,
            [&](auto a)
            {
                return a.cell == seed.cell;
            }
        );
    }

    // Visit the existing cut once and append newly split children to the same work list. Only new finer
    // leaves can introduce another imbalance; rebuilding/scanning the whole set after each split is unnecessary.
    LeafSet leaves(addresses.begin(), addresses.end());
    for (size_t index = 0; index < addresses.size(); ++index)
    {
        const auto address = addresses[index];
        if (!leaves.contains(address))
        {
            continue;
        }
        for (unsigned face = 0; face < 6; ++face)
        {
            for (;;)
            {
                const auto neighbor = faceNeighbor(address, face, leaves);
                if (!neighbor || address.depth <= neighbor->depth + 1)
                {
                    break;
                }
                if (operation == Operation::Merge)
                {
                    return {}; // A merge must wait rather than undo itself.
                }
                auto* coarser = find(*neighbor);
                if (!coarser || neighbor->depth >= m_settings.maximumDepth)
                {
                    throw EngineError("Terrain balancing cannot split the coarse node.");
                }
                if (!createChildren(*coarser))
                {
                    return {};
                }
                leaves.erase(*neighbor);
                for (const auto& child : coarser->children)
                {
                    leaves.insert(child->address);
                    addresses.push_back(child->address);
                }
            }
        }
    }
    std::erase_if(
        addresses,
        [&](const auto& address)
        {
            return !leaves.contains(address);
        }
    );

    Plan plan;
    if (operation == Operation::Admit)
    {
        plan.admitting = seed.cell;
    }
    if (operation == Operation::Withdraw)
    {
        plan.removing = seed.cell;
    }
    std::sort(addresses.begin(), addresses.end());
    plan.candidate = std::move(addresses);
    size_t current = 0;
    for (const auto& key : plan.candidate)
    {
        while (current < m_cut.size() && m_cut[current].address < key)
        {
            ++current;
        }
        if (current < m_cut.size() && m_cut[current].address == key)
        {
            continue;
        }
        // Unchanged cut entries and their ancestors are already prepared and retained by the cut itself.
        // Only changed entries need asynchronous dependencies; retain unchanged entries through the existing cut.
        plan.required.insert(key);
        auto address = key;
        for (;;)
        {
            plan.required.insert(address);
            if (!address.depth)
            {
                break;
            }
            address = parentAddress(address);
        }
    }
    return plan;
}

bool TerrainStream::schedule(Plan& plan)
{
    OFG_TERRAIN_SCOPE(TerrainStream_schedule);
    std::vector<NodeAddress> missing;
    for (const auto& key : plan.required)
    {
        auto& content = slot(key);
        if (content.state == BuildState::Failed)
        {
            return false;
        }
        if (content.state == BuildState::Pending)
        {
            missing.push_back(key);
        }
    }
    if (missing.empty())
    {
        return true;
    }

    // A balanced replacement may span many sibling groups. Reserve each group together,
    // but build the closure in waves; completed small payloads release their worst-case reserve.
    // Reserving the entire closure at once can permanently block it even when the final meshes easily fit.
    std::map<NodeAddress, std::vector<NodeAddress>> groups;
    for (const auto& key : missing)
    {
        if (key.depth && slot(parentAddress(key)).state != BuildState::Loaded)
        {
            continue; // Parent completion precedes dispatch, including roots admitted beside detailed terrain.
        }
        groups[key.depth ? parentAddress(key) : key].push_back(key);
    }
    const auto counts = diagnostics();
    size_t cpu = counts.residentCpuBytes + counts.retiredCpuBytes + counts.reservedCpuBytes;
    size_t gpu = counts.residentGpuBytes + counts.retiredGpuBytes + counts.reservedGpuBytes;
    size_t reservedJobs = m_jobs.size();
    missing.clear();
    for (const auto& [identity, group] : groups)
    {
        if (cpu > m_settings.cpuBudget || gpu > m_settings.gpuBudget ||
            group.size() > (m_settings.cpuBudget - cpu) / m_settings.maximumPayloadBytes ||
            group.size() > (m_settings.gpuBudget - gpu) / m_settings.maximumPayloadBytes ||
            group.size() > m_settings.maximumJobs - std::min(m_settings.maximumJobs, reservedJobs))
        {
            m_budgetBlocked = true;
            continue;
        }
        missing.insert(missing.end(), group.begin(), group.end());
        cpu += group.size() * m_settings.maximumPayloadBytes;
        gpu += group.size() * m_settings.maximumPayloadBytes;
        reservedJobs += group.size();
    }

    // Prepare all potentially throwing storage before modifying build slots.
    std::map<RequestId, Job> jobs;
    std::vector<BuildRequest> dispatch;
    for (const auto& key : missing)
    {
        if (m_sequence == UINT64_MAX)
        {
            throw EngineError("Terrain request sequence exhausted.");
        }
        BuildRequest request{{m_epoch, ++m_sequence}, key, m_settings.maximumPayloadBytes};
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
    return true;
}

bool TerrainStream::publish(const Plan& plan)
{
    OFG_TERRAIN_SCOPE(TerrainStream_publish);
    for (const auto& key : plan.required)
    {
        if (slot(key).state != BuildState::Loaded)
        {
            return false;
        }
    }
    bool same = plan.candidate.size() == m_cut.size();
    for (size_t i = 0; same && i < m_cut.size(); ++i)
    {
        same = m_cut[i].address == plan.candidate[i];
    }
    if (same)
    {
        return false;
    }

    std::vector<CutEntry> next;
    next.reserve(plan.candidate.size());
    const LeafSet leaves(plan.candidate.begin(), plan.candidate.end());
    for (const auto& address : plan.candidate)
    {
        next.push_back({address, slot(address).payload, transitionFaces(address, leaves)});
    }
    if (plan.admitting)
    {
        m_roots.at(*plan.admitting)->admitted = true;
    }
    if (plan.removing)
    {
        m_roots.at(*plan.removing)->admitted = false;
    }
    m_cut.swap(next);
    invalidatePlans();
    ++m_publications;
    return true;
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
        m_retired.push_back(std::move(content.payload));
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
    completedSubmission(m_completedSubmission);
    if (m_idle)
    {
        return;
    }
    try
    {
        reconcile();
    } catch (...)
    {
        // An interrupted plan was not evaluated: permit the next update to retry from the retained cut.
        invalidatePlans();
        throw;
    }
}

void TerrainStream::reconcile()
{
    OFG_TERRAIN_SCOPE(TerrainStream_reconcile);
    bool demandChanged = false;
    std::function<void(Node&)> updateDemand = [&](Node& node)
    {
        const bool previous = node.distanceWantsChildren;
        const double distance = distanceToNode(node.address, m_observer, m_settings.rootWidth);
        const double width = std::ldexp(m_settings.rootWidth, -node.address.depth);
        if (distance < 2 * width)
        {
            node.distanceWantsChildren = true;
        }
        else if (distance > 2.5 * width)
        {
            node.distanceWantsChildren = false;
        }
        demandChanged = demandChanged || previous != node.distanceWantsChildren;
        for (auto& child : node.children)
        {
            if (child)
            {
                updateDemand(*child);
            }
        }
    };
    for (auto& [cell, root] : m_roots)
    {
        updateDemand(*root);
    }
    if (demandChanged)
    {
        invalidatePlans();
    }

    std::vector<std::pair<NodeAddress, Operation>> operations;
    for (const auto& [cell, root] : m_roots)
    {
        if (!m_wantedRoots.contains(cell))
        {
            operations.push_back({root->address, Operation::Withdraw});
        }
    }
    for (const auto& cell : m_wantedRoots)
    {
        if (!find(NodeAddress{cell})->admitted)
        {
            operations.push_back({NodeAddress{cell}, Operation::Admit});
        }
    }

    std::set<NodeAddress> parents;
    LeafSet selected;
    for (const auto& entry : m_cut)
    {
        selected.insert(entry.address);
    }
    for (const auto& entry : m_cut)
    {
        if (entry.address.depth)
        {
            parents.insert(parentAddress(entry.address));
        }
    }
    for (const auto& parent : parents)
    {
        if (m_wantedRoots.contains(parent.cell) && !wantsChildren(*find(parent)))
        {
            const auto children = childAddresses(parent);
            if (std::all_of(
                    children.begin(),
                    children.end(),
                    [&](const auto& child)
                    {
                        return selected.contains(child);
                    }
                ))
            {
                operations.push_back({parent, Operation::Merge});
            }
        }
    }
    for (const auto& entry : m_cut)
    {
        if (m_wantedRoots.contains(entry.address.cell) && wantsChildren(*find(entry.address)))
        {
            operations.push_back({entry.address, Operation::Split});
        }
    }

    // Cache the distance once per operation instead of recalculating global AABBs at each sort comparison.
    // Release capacity first, establish coarse coverage outward, then refine outward. Otherwise detailed air
    // around a high camera could monopolize workers before visible ground has any coarse coverage.
    struct OrderedOperation
    {
        std::pair<NodeAddress, Operation> identity;
        int priority;
        double distance;
    };
    std::vector<OrderedOperation> ordered;
    ordered.reserve(operations.size());
    for (const auto& operation : operations)
    {
        ordered.push_back(
            {operation,
             (operation.second == Operation::Merge || operation.second == Operation::Withdraw) ? 0
             : operation.second == Operation::Admit                                            ? 1
                                                                                               : 2,
             distanceToNode(operation.first, m_observer, m_settings.rootWidth)}
        );
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const auto& a, const auto& b)
        {
            if (a.priority != b.priority)
            {
                return a.priority < b.priority;
            }
            if (a.distance != b.distance)
            {
                return a.distance < b.distance;
            }
            return a.identity < b.identity;
        }
    );
    operations.clear();
    for (const auto& operation : ordered)
    {
        operations.push_back(operation.identity);
    }
    // Try each operation once per change to demand, topology, readiness or capacity. Held/failed/budget-blocked
    // operations yield to the next nearest region; unchanged blocked plans do not consume every future frame.
    std::set<NodeAddress> required;
    const std::set<std::pair<NodeAddress, Operation>> eligible(operations.begin(), operations.end());
    std::erase_if(
        m_pendingRequirements,
        [&](const auto& value)
        {
            return !eligible.contains(value.first);
        }
    );
    size_t attempted = 0;
    for (const auto& identity : operations)
    {
        if (m_attemptedOperations.contains(identity))
        {
            continue;
        }
        if (attempted++ == m_settings.maximumPlansPerUpdate)
        {
            break;
        }
        m_attemptedOperations.insert(identity);
        const auto [seed, operation] = identity;
        m_pendingRequirements.erase(identity);
        auto plan = makePlan(seed, operation);
        if (!plan)
        {
            continue;
        }
        if (schedule(*plan))
        {
            if (!publish(*plan))
            {
                m_pendingRequirements.emplace(identity, std::move(plan->required));
            }
        }
    }
    for (const auto& entry : m_cut)
    {
        required.insert(entry.address);
        auto address = entry.address;
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
    for (const auto& cell : m_wantedRoots)
    {
        required.insert(NodeAddress{cell});
    }
    for (auto& [identity, dependencies] : m_pendingRequirements)
    {
        // Current cut/ancestor ownership already protects these values; avoid repeated full-cut copies.
        for (const auto& key : required)
        {
            dependencies.erase(key);
        }
    }
    for (const auto& [identity, dependencies] : m_pendingRequirements)
    {
        required.insert(dependencies.begin(), dependencies.end());
    }
    m_idle = m_jobs.empty() && std::all_of(
                                   operations.begin(),
                                   operations.end(),
                                   [&](const auto& operation)
                                   {
                                       return m_attemptedOperations.contains(operation);
                                   }
                               );
    cancelUnused(required);
    prune(required);
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
    std::stable_sort(
        result.begin(),
        result.end(),
        [&](const auto& a, const auto& b)
        {
            return distanceToNode(a.address, m_observer, m_settings.rootWidth) <
                   distanceToNode(b.address, m_observer, m_settings.rootWidth);
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

bool TerrainStream::complete(RequestId id, std::shared_ptr<PreparedPayload> payload)
{
    auto* content = owner(id);
    if (!content)
    {
        ++m_staleResults;
        if (m_jobs.contains(id) && payload)
        {
            m_retired.push_back(std::move(payload));
        }
        acknowledgeCancellation(id);
        return false;
    }
    const auto& job = m_jobs.at(id);
    if (!payload || payload->cpuBytes > job.request.byteLimit || payload->gpuBytes > job.request.byteLimit ||
        (!payload->empty && !job.generated) || (payload->certifiedEmpty && !payload->empty))
    {
        fail(id, "Invalid prepared terrain payload or missing CPU completion.", FailureStage::Upload);
        return false;
    }
    content->payload = std::move(payload);
    invalidatePlans();
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
    if (m_jobs.erase(id))
    {
        invalidatePlans();
    }
}

void TerrainStream::failUpload(RequestId id, std::shared_ptr<PreparedPayload> payload, std::string error)
{
    if (!m_jobs.contains(id))
    {
        ++m_staleResults;
        return;
    }
    m_retired.push_back(std::move(payload));
    fail(id, std::move(error), FailureStage::Upload);
}

void TerrainStream::acknowledgeCancellation(RequestId id)
{
    const auto job = m_jobs.find(id);
    if (job != m_jobs.end() && job->second.cancelled)
    {
        m_jobs.erase(job);
        invalidatePlans();
    }
}

void TerrainStream::retry(NodeAddress address)
{
    auto* node = find(address);
    if (node && node->content.state == BuildState::Failed)
    {
        node->content.state = BuildState::Pending;
        node->content.error.clear();
        invalidatePlans();
    }
}

void TerrainStream::reset()
{
    invalidatePlans();
    if (m_epoch == UINT64_MAX)
    {
        throw EngineError("Terrain epoch exhausted.");
    }
    ++m_epoch;
    m_wantedRoots.clear();
    m_pendingRequirements.clear();
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

void TerrainStream::submitted(const std::vector<CutEntry>& snapshot, uint64_t serial)
{
    if (serial <= m_lastSubmission)
    {
        throw EngineError("Terrain submission serials must increase.");
    }
    m_lastSubmission = serial;
    for (const auto& entry : snapshot)
    {
        entry.payload->lastSubmission = std::max(entry.payload->lastSubmission, serial);
    }
}

void TerrainStream::completedSubmission(uint64_t serial)
{
    if (serial < m_completedSubmission || serial > m_lastSubmission)
    {
        throw EngineError("Invalid terrain GPU completion serial.");
    }
    m_completedSubmission = serial;
    const auto before = m_retired.size();
    std::erase_if(
        m_retired,
        [&](const auto& payload)
        {
            return payload->lastSubmission <= serial && payload.use_count() == 1;
        }
    );
    if (before != m_retired.size())
    {
        invalidatePlans();
    }
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
        result.unresolvedRefinements += wantsChildren(*find(entry.address));
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
    for (const auto& payload : m_retired)
    {
        result.retiredCpuBytes += payload->cpuBytes;
        result.retiredGpuBytes += payload->gpuBytes;
    }
    result.selected = m_cut.size();
    result.jobs = m_jobs.size();
    result.publications = m_publications;
    result.staleResults = m_staleResults;
    result.budgetBlocked = m_budgetBlocked;
    result.planningIdle = m_idle;
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
