// Private little-endian request layout shared by the two separately compiled WASM modules.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ofg::terrain {
struct WorkerWire
{
    uint64_t epoch, sequence;
    int64_t cellX, cellY, cellZ;
    uint64_t seed;
    uint32_t x, y, z, depth;
    double rootWidth, heightOffset, amplitude;
    uint32_t intervals, byteLimit, flags;
};
static_assert(sizeof(WorkerWire) == 104);
static_assert(offsetof(WorkerWire, intervals) == 88);
static_assert(offsetof(WorkerWire, byteLimit) == 92);
static_assert(offsetof(WorkerWire, flags) == 96);
} // namespace ofg::terrain
