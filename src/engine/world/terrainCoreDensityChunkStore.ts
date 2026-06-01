import {
  terrainChunkKey,
  type TerrainChunkCoord
} from "./terrainChunk.js";
import type { TerrainDensityChunkPayload } from "./terrainChunkWorkerTypes.js";
import {
  readTerrainCoreDensityChunkBuffer,
  terrainPresetToWasmCode,
  type TerrainCoreWasmInstance
} from "./terrainCoreWasm.js";
import type { WorldDescriptor } from "./terrainGenerator.js";

export type TerrainDensityChunkStore = {
  readonly runtime: "rust" | "typescript";
  clear(): void;
  size(): number;
  retainOnly(coords: readonly TerrainChunkCoord[], cellSize: number): void;
  has(coord: TerrainChunkCoord, cellSize: number): boolean;
  store(chunk: TerrainDensityChunkPayload, cellSize: number): void;
  get(coord: TerrainChunkCoord, cellSize: number): TerrainDensityChunkPayload | undefined;
};

export class TerrainCoreDensityChunkStore implements TerrainDensityChunkStore {
  readonly runtime = "rust" as const;

  constructor(
    private readonly terrainCore: TerrainCoreWasmInstance,
    private readonly descriptor: WorldDescriptor
  ) {}

  clear(): void {
    this.terrainCore.exports.ofg_reset_density_chunk_store();
  }

  size(): number {
    return this.terrainCore.exports.ofg_density_chunk_store_entry_count();
  }

  retainOnly(coords: readonly TerrainChunkCoord[], cellSize: number): void {
    if (coords.length === 0) {
      this.clear();
      return;
    }

    const bounds = densityCoordBounds(coords);
    this.terrainCore.exports.ofg_retain_density_chunk_store_window(
      this.descriptor.seed,
      terrainPresetToWasmCode(this.descriptor.terrainPreset),
      bounds.minX,
      bounds.minY,
      bounds.minZ,
      bounds.maxX,
      bounds.maxY,
      bounds.maxZ,
      cellSize
    );
  }

  has(coord: TerrainChunkCoord, cellSize: number): boolean {
    return this.terrainCore.exports.ofg_density_chunk_store_contains(
      this.descriptor.seed,
      terrainPresetToWasmCode(this.descriptor.terrainPreset),
      coord.x,
      coord.y,
      coord.z,
      cellSize
    ) === 1;
  }

  store(chunk: TerrainDensityChunkPayload, cellSize: number): void {
    const buffer = readTerrainCoreDensityChunkBuffer(this.terrainCore.exports);
    if (chunk.densities.length !== buffer.length) {
      throw new Error(
        `Terrain density chunk '${chunk.key}' has ${chunk.densities.length} samples; ` +
        `expected ${buffer.length}.`
      );
    }

    buffer.set(chunk.densities);
    const stored = this.terrainCore.exports.ofg_store_density_chunk_buffer(
      this.descriptor.seed,
      terrainPresetToWasmCode(this.descriptor.terrainPreset),
      chunk.coord.x,
      chunk.coord.y,
      chunk.coord.z,
      cellSize
    );
    if (stored !== 1) {
      throw new Error(`Rust terrain density store rejected chunk '${chunk.key}'.`);
    }
  }

  get(coord: TerrainChunkCoord, cellSize: number): TerrainDensityChunkPayload | undefined {
    const loaded = this.terrainCore.exports.ofg_load_density_chunk_buffer(
      this.descriptor.seed,
      terrainPresetToWasmCode(this.descriptor.terrainPreset),
      coord.x,
      coord.y,
      coord.z,
      cellSize
    );
    if (loaded !== 1) {
      return undefined;
    }

    return {
      key: terrainChunkKey(coord),
      coord,
      densities: new Float32Array(readTerrainCoreDensityChunkBuffer(this.terrainCore.exports))
    };
  }
}

export function createTerrainCoreDensityChunkStore(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor
): TerrainCoreDensityChunkStore {
  return new TerrainCoreDensityChunkStore(terrainCore, descriptor);
}

function densityCoordBounds(coords: readonly TerrainChunkCoord[]): {
  readonly minX: number;
  readonly minY: number;
  readonly minZ: number;
  readonly maxX: number;
  readonly maxY: number;
  readonly maxZ: number;
} {
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

  return { minX, minY, minZ, maxX, maxY, maxZ };
}
