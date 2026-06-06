import type { TerrainChunkCoord } from "./terrainChunk.js";
import {
  readTerrainCoreMeshIndexBuffer,
  readTerrainCoreMeshVertexBuffer,
  terrainPresetToWasmCode,
  type TerrainCoreWasmInstance
} from "./terrainCoreWasm.js";
import type { WorldDescriptor } from "./terrainDescriptor.js";
import type { MeshData } from "./terrainMesh.js";

export function generateTerrainChunkMeshWithWasm(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor,
  coord: TerrainChunkCoord,
  cellSize = 1
): MeshData {
  terrainCore.exports.ofg_build_chunk_mesh(
    descriptor.seed,
    terrainPresetToWasmCode(descriptor.terrainPreset),
    coord.x,
    coord.y,
    coord.z,
    cellSize
  );

  return {
    vertices: new Float32Array(readTerrainCoreMeshVertexBuffer(terrainCore.exports)),
    indices: new Uint32Array(readTerrainCoreMeshIndexBuffer(terrainCore.exports))
  };
}
