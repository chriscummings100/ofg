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
    uint32_t x, y, z, depth, faces, intervals;
    double rootWidth, heightOffset, amplitude;
    uint32_t byteLimit, flags;
};
static_assert(sizeof(WorkerWire) == 104);
static_assert(offsetof(WorkerWire, byteLimit) == 96);
} // namespace ofg::terrain
