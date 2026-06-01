import type { TerrainChunkCoord } from "./terrainChunk.js";
import {
  readTerrainCoreMeshIndexBuffer,
  readTerrainCoreMeshVertexBuffer,
  terrainPresetToWasmCode,
  type TerrainCoreWasmInstance
} from "./terrainCoreWasm.js";
import type { WorldDescriptor } from "./terrainGenerator.js";
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

export function prepareTerrainCoreDensityChunkWindow(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor,
  coords: readonly TerrainChunkCoord[],
  cellSize = 1
): number {
  if (coords.length === 0) {
    return 0;
  }

  let minX = coords[0].x;
  let minY = coords[0].y;
  let minZ = coords[0].z;
  let maxX = coords[0].x;
  let maxY = coords[0].y;
  let maxZ = coords[0].z;
  for (const coord of coords) {
    minX = Math.min(minX, coord.x);
    minY = Math.min(minY, coord.y);
    minZ = Math.min(minZ, coord.z);
    maxX = Math.max(maxX, coord.x);
    maxY = Math.max(maxY, coord.y);
    maxZ = Math.max(maxZ, coord.z);
  }

  return terrainCore.exports.ofg_prepare_density_chunk_window(
    descriptor.seed,
    terrainPresetToWasmCode(descriptor.terrainPreset),
    minX,
    minY,
    minZ,
    maxX,
    maxY,
    maxZ,
    cellSize
  );
}

export function createTerrainCoreChunkMeshGenerator(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor
): (coord: TerrainChunkCoord, cellSize: number) => MeshData {
  return (coord, cellSize) =>
    generateTerrainChunkMeshWithWasm(terrainCore, descriptor, coord, cellSize);
}

export function createTerrainCoreDensityChunkWindowGenerator(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor
): (coords: readonly TerrainChunkCoord[], cellSize: number) => void {
  return (coords, cellSize) => {
    prepareTerrainCoreDensityChunkWindow(terrainCore, descriptor, coords, cellSize);
  };
}
