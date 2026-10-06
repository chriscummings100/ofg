// Exact octree identities and integer-cell/double-local positions; no graphics or world-sized floats.
#pragma once

#include <array>
#include <compare>
#include <cstdint>

namespace ofg::terrain {
struct CellAddress
{
    int64_t x = 0, y = 0, z = 0;
    // Orders cells deterministically for scheduling and reproducible snapshots.
    auto operator<=>(const CellAddress&) const = default;
};

struct NodeAddress
{
    CellAddress cell;
    uint32_t x = 0, y = 0, z = 0;
    uint8_t depth = 0;
    // Orders node identities independently of allocation addresses.
    auto operator<=>(const NodeAddress&) const = default;
};

struct WorldPosition
{
    CellAddress cell;
    std::array<double, 3> local{};
};

// Rejects unsupported depth or coordinates outside the root's dyadic subdivision.
void validateAddress(const NodeAddress& address);
// Returns all eight children in X-bit/Y-bit/Z-bit order; rejects depth above 16.
std::array<NodeAddress, 8> childAddresses(const NodeAddress& parent);
// Returns the immediate parent; a root has no parent and throws.
NodeAddress parentAddress(const NodeAddress& child);
// Reports strict spatial ancestry using integer coordinates.
bool isAncestor(const NodeAddress& ancestor, const NodeAddress& descendant);
// Returns the face of a touching b relative to a, or -1; edge/corner contact is excluded.
// Faces are -X,+X,-Y,+Y,-Z,+Z, including across root boundaries.
int sharedFace(const NodeAddress& a, const NodeAddress& b);
// Normalizes finite local metres into [0, rootWidth); rejects address overflow.
WorldPosition normalizePosition(WorldPosition position, double rootWidth);
// Returns node minimum relative to an observer, subtracting cells before converting to metres.
std::array<double, 3> relativeMinimum(const NodeAddress& node, const WorldPosition& observer, double rootWidth);
// Returns Euclidean distance in metres from an observer to the node's closed AABB.
double distanceToNode(const NodeAddress& node, const WorldPosition& observer, double rootWidth);
} // namespace ofg::terrain
