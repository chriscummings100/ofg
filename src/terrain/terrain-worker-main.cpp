// Standalone worker WASM exports only bounded CPU meshing; no graphics, scene state or shared heap.
#include "terrain/terrain-generator.h"
#include "terrain/terrain-worker-wire.h"
#include "core/engine-error.h"

#include <emscripten.h>

namespace {
std::unique_ptr<ofg::terrain::TerrainMesher> mesher;
ofg::terrain::TerrainGeometry geometry;
std::string error;
} // namespace

extern "C"
{
    // Starts one request copied from the message buffer, replacing only already-finished/cancelled work.
    EMSCRIPTEN_KEEPALIVE int terrainBegin(const ofg::terrain::WorkerWire* wire)
    {
        geometry = {};
        mesher.reset();
        error.clear();
        try
        {
            if (wire->depth > 16 || wire->faces > 63 || wire->byteLimit > (16u << 20))
            {
                throw ofg::EngineError("Invalid terrain worker packet.");
            }
            ofg::terrain::GeneratorSettings settings;
            settings.seed = wire->seed;
            settings.intervals = wire->intervals;
            settings.rootWidth = wire->rootWidth;
            settings.heightOffset = wire->heightOffset;
            settings.amplitude = wire->amplitude;
            mesher = std::make_unique<ofg::terrain::TerrainMesher>(
                ofg::terrain::NodeAddress{
                    {wire->cellX, wire->cellY, wire->cellZ},
                    wire->x,
                    wire->y,
                    wire->z,
                    uint8_t(wire->depth)
                },
                uint8_t(wire->faces),
                settings,
                wire->byteLimit
            );
            return 0;
        } catch (const std::exception& failure)
        {
            error = failure.what();
            return -1;
        }
    }

    // Returns zero for more work, one for complete, minus one for failure; the JS caller yields between batches.
    EMSCRIPTEN_KEEPALIVE int terrainStep()
    {
        try
        {
            if (!mesher)
            {
                throw ofg::EngineError("Terrain worker has no active job.");
            }
            if (!mesher->step(256))
            {
                return 0;
            }
            geometry = mesher->takeGeometry();
            mesher.reset();
            return 1;
        } catch (const std::exception& failure)
        {
            error = failure.what();
            mesher.reset();
            return -1;
        }
    }

    // Releases active scratch/output after cancellation or successful transfer.
    EMSCRIPTEN_KEEPALIVE void terrainDiscard()
    {
        mesher.reset();
        geometry = {};
    }
    // Exposes completed vertex bytes only while the result remains worker-owned.
    EMSCRIPTEN_KEEPALIVE const void* terrainVertices()
    {
        return geometry.vertices.data();
    }
    // Reports used vertex bytes, excluding allocator capacity.
    EMSCRIPTEN_KEEPALIVE uint32_t terrainVertexBytes()
    {
        return uint32_t(geometry.vertices.size() * sizeof(ofg::Vertex));
    }
    // Exposes completed index bytes only while the result remains worker-owned.
    EMSCRIPTEN_KEEPALIVE const void* terrainIndices()
    {
        return geometry.indices.data();
    }
    // Reports used index bytes.
    EMSCRIPTEN_KEEPALIVE uint32_t terrainIndexBytes()
    {
        return uint32_t(geometry.indices.size() * sizeof(uint32_t));
    }
    // Reports the conservative empty-space certificate separately from a sampled-empty result.
    EMSCRIPTEN_KEEPALIVE int terrainCertifiedEmpty()
    {
        return geometry.certifiedEmpty;
    }
    // Exposes the current terminal diagnostic as UTF-8.
    EMSCRIPTEN_KEEPALIVE const char* terrainError()
    {
        return error.c_str();
    }
}
