// Dyadic spatial arithmetic preserves root identity at negative and distant coordinates.
#include "terrain/terrain-address.h"
#include "core/engine-error.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ofg::terrain {
void validateAddress(const NodeAddress& a)
{
    if (a.depth > 16 || a.x >= (1u << a.depth) || a.y >= (1u << a.depth) || a.z >= (1u << a.depth))
    {
        throw EngineError("Invalid terrain node address.");
    }
}

std::array<NodeAddress, 8> childAddresses(const NodeAddress& p)
{
    validateAddress(p);
    if (p.depth == 16)
    {
        throw EngineError("Terrain depth exceeds 16.");
    }
    std::array<NodeAddress, 8> result;
    for (uint32_t i = 0; i < 8; ++i)
    {
        result[i] =
            {p.cell, p.x * 2 + (i & 1), p.y * 2 + ((i >> 1) & 1), p.z * 2 + ((i >> 2) & 1), uint8_t(p.depth + 1)};
    }
    return result;
}

NodeAddress parentAddress(const NodeAddress& a)
{
    validateAddress(a);
    if (!a.depth)
    {
        throw EngineError("A terrain root has no parent.");
    }
    return {a.cell, a.x / 2, a.y / 2, a.z / 2, uint8_t(a.depth - 1)};
}

bool isAncestor(const NodeAddress& a, const NodeAddress& b)
{
    if (a.cell != b.cell || a.depth >= b.depth)
    {
        return false;
    }
    const auto shift = b.depth - a.depth;
    return (b.x >> shift) == a.x && (b.y >> shift) == a.y && (b.z >> shift) == a.z;
}

int sharedFace(const NodeAddress& a, const NodeAddress& b)
{
    const int depth = std::max(a.depth, b.depth);
    const int64_t scale = int64_t(1) << depth;
    const std::array<int64_t, 3> ac{a.cell.x, a.cell.y, a.cell.z}, bc{b.cell.x, b.cell.y, b.cell.z};
    const std::array<uint32_t, 3> ap{a.x, a.y, a.z}, bp{b.x, b.y, b.z};
    std::array<int64_t, 3> amin{}, bmin{};
    const int64_t aw = int64_t(1) << (depth - a.depth), bw = int64_t(1) << (depth - b.depth);
    for (int axis = 0; axis < 3; ++axis)
    {
        // Only neighboring roots can touch. Avoid signed subtraction at INT64 limits.
        int64_t delta = 0;
        if (bc[axis] != ac[axis])
        {
            if (ac[axis] != std::numeric_limits<int64_t>::max() && bc[axis] == ac[axis] + 1)
            {
                delta = 1;
            }
            else if (ac[axis] != std::numeric_limits<int64_t>::min() && bc[axis] == ac[axis] - 1)
            {
                delta = -1;
            }
            else
            {
                return -1;
            }
        }
        amin[axis] = int64_t(ap[axis]) * aw;
        bmin[axis] = delta * scale + int64_t(bp[axis]) * bw;
    }
    int face = -1;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (amin[axis] + aw == bmin[axis] || bmin[axis] + bw == amin[axis])
        {
            if (face >= 0)
            {
                return -1;
            }
            face = axis * 2 + int(amin[axis] + aw == bmin[axis]);
        }
        else if (std::max(amin[axis], bmin[axis]) >= std::min(amin[axis] + aw, bmin[axis] + bw))
        {
            return -1;
        }
    }
    return face;
}

WorldPosition normalizePosition(WorldPosition p, double width)
{
    if (!std::isfinite(width) || width <= 0)
    {
        throw EngineError("Terrain root width must be finite and positive.");
    }
    std::array<int64_t*, 3> cells{&p.cell.x, &p.cell.y, &p.cell.z};
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = std::floor(p.local[axis] / width);
        if (!std::isfinite(delta) || delta < -0x1p63 || delta >= 0x1p63)
        {
            throw EngineError("Terrain position exceeds the address range.");
        }
        const auto shift = int64_t(delta);
        const auto cell = *cells[axis];
        if ((shift > 0 && cell > INT64_MAX - shift) || (shift < 0 && cell < INT64_MIN - shift))
        {
            throw EngineError("Terrain position normalization overflows its cell.");
        }
        *cells[axis] += shift;
        p.local[axis] -= delta * width;
        // Rounding just below zero can produce width exactly; normalize that final boundary too.
        if (p.local[axis] >= width)
        {
            if (*cells[axis] == INT64_MAX)
            {
                throw EngineError("Terrain position normalization overflows its cell.");
            }
            ++*cells[axis];
            p.local[axis] = 0;
        }
    }
    return p;
}

std::array<double, 3> relativeMinimum(const NodeAddress& a, const WorldPosition& p, double width)
{
    const std::array<int64_t, 3> ac{a.cell.x, a.cell.y, a.cell.z}, pc{p.cell.x, p.cell.y, p.cell.z};
    const std::array<uint32_t, 3> local{a.x, a.y, a.z};
    std::array<double, 3> result;
    for (int axis = 0; axis < 3; ++axis)
    {
        // Unsigned magnitude subtraction is exact even across zero and avoids signed overflow.
        const bool positive = ac[axis] >= pc[axis];
        const uint64_t difference =
            positive ? uint64_t(ac[axis]) - uint64_t(pc[axis]) : uint64_t(pc[axis]) - uint64_t(ac[axis]);
        const double cells = positive ? double(difference) : -double(difference);
        result[axis] = cells * width + double(local[axis]) * std::ldexp(width, -a.depth) - p.local[axis];
    }
    return result;
}

double distanceToNode(const NodeAddress& a, const WorldPosition& p, double width)
{
    const auto minimum = relativeMinimum(a, p, width);
    const double size = std::ldexp(width, -a.depth);
    double square = 0;
    for (const double value : minimum)
    {
        const double distance = std::max({value, -value - size, 0.0});
        square += distance * distance;
    }
    return std::sqrt(square);
}
} // namespace ofg::terrain
