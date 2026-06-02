import {
  TerrainDensityChunk,
  type GenerateTerrainDensityChunkOptions,
  type TerrainChunkCoord,
  type TerrainDensityChunkGenerator
} from "./terrainChunk.js";
import {
  readTerrainCoreDensityChunkBuffer,
  terrainPresetToWasmCode,
  type TerrainCoreWasmInstance
} from "./terrainCoreWasm.js";
import type { WorldDescriptor } from "./terrainDescriptor.js";

export function generateTerrainDensityChunkWithWasm(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor,
  coord: TerrainChunkCoord,
  options: GenerateTerrainDensityChunkOptions = {}
): TerrainDensityChunk {
  const cellSize = options.cellSize ?? 1;
  const preset = terrainPresetToWasmCode(descriptor.terrainPreset);

  terrainCore.exports.ofg_fill_density_chunk(
    descriptor.seed,
    preset,
    coord.x,
    coord.y,
    coord.z,
    cellSize
  );

  return new TerrainDensityChunk(coord, {
    cellSize,
    densities: new Float32Array(readTerrainCoreDensityChunkBuffer(terrainCore.exports))
  });
}

export function createTerrainCoreDensityChunkGenerator(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor
): TerrainDensityChunkGenerator {
  return (_source, coord, options) =>
    generateTerrainDensityChunkWithWasm(terrainCore, descriptor, coord, options);
}
