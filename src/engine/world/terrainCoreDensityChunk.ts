import {
  terrainChunkCoord,
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "./terrainChunk.js";
import {
  readTerrainCoreDensityChunkBuffer,
  terrainPresetToWasmCode,
  type TerrainCoreWasmInstance
} from "./terrainCoreWasm.js";
import type { WorldDescriptor } from "./terrainDescriptor.js";

export type GenerateTerrainCoreDensityChunkOptions = {
  readonly cellSize?: number;
};

export type TerrainCoreDensityChunk = {
  readonly key: TerrainChunkKey;
  readonly coord: TerrainChunkCoord;
  readonly cellSize: number;
  readonly densities: Float32Array;
};

export function generateTerrainDensityChunkWithWasm(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor,
  coord: TerrainChunkCoord,
  options: GenerateTerrainCoreDensityChunkOptions = {}
): TerrainCoreDensityChunk {
  const cellSize = options.cellSize ?? 1;
  const preset = terrainPresetToWasmCode(descriptor.terrainPreset);
  const chunkCoord = terrainChunkCoord(coord.x, coord.y, coord.z);

  terrainCore.exports.ofg_fill_density_chunk(
    descriptor.seed,
    preset,
    chunkCoord.x,
    chunkCoord.y,
    chunkCoord.z,
    cellSize
  );

  return {
    key: terrainChunkKey(chunkCoord),
    coord: chunkCoord,
    cellSize,
    densities: new Float32Array(readTerrainCoreDensityChunkBuffer(terrainCore.exports))
  };
}
