// Deterministic CPU noise and bounded-step Transvoxel meshing, shared by native and browser C++ workers.
#pragma once

#include "terrain/terrain-address.h"
#include "resources/mesh.h"

#include <memory>

namespace ofg::terrain {
struct GeneratorSettings
{
    double rootWidth = 1024;
    double heightOffset = 24;
    double amplitude = 180;
    uint64_t seed = 1;
    uint32_t intervals = 32;
};

struct TerrainGeometry
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    // Eight boundaries delimit regular triangles, then -X,+X,-Y,+Y,-Z,+Z; empty parts repeat an offset.
    std::array<uint32_t, 8> partOffsets{};
    bool certifiedEmpty = false;
    // Counts allocated output storage, including unused vector capacity.
    size_t allocatedBytes() const noexcept;
};

// Evaluates a globally consistent height using integer lattice hashing before floating interpolation.
double terrainHeight(CellAddress cell, double localX, double localZ, const GeneratorSettings& settings);
// Validates received worker geometry before any GPU allocation; positions are node-local metres.
void validateGeometry(const TerrainGeometry& geometry, double nodeWidth, size_t byteLimit);

class TerrainMesher
{
public:
    // Builds the regular mesh and all six faces once; scratch/output remain owned by this task.
    TerrainMesher(NodeAddress address, GeneratorSettings settings, size_t byteLimit);
    // Releases scratch and unclaimed output; cancellation is destruction between step calls.
    ~TerrainMesher();
    TerrainMesher(const TerrainMesher&) = delete;
    TerrainMesher& operator=(const TerrainMesher&) = delete;
    // Processes at most cellBudget regular or transition cells; true means output is ready.
    bool step(uint32_t cellBudget = 256);
    // Transfers completed output exactly once; throws if called before completion.
    TerrainGeometry takeGeometry();

private:
    struct Work;
    std::unique_ptr<Work> m_work;
};
} // namespace ofg::terrain
