// Shared browser-side terrain chunk identifiers.
// Rust owns terrain density sampling, storage, meshing, and stream scheduling.
// TypeScript keeps only coordinate math for descriptors, debug snapshots, test
// adapters, and browser-side chunk-key display.
import type { Vec3 } from "../math/vec3.js";

export const TERRAIN_CHUNK_CELLS_PER_AXIS = 32;

export type TerrainChunkCoord = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
};

export type TerrainChunkKey = string;

export function terrainChunkCoord(x: number, y: number, z: number): TerrainChunkCoord {
  assertInteger("x", x);
  assertInteger("y", y);
  assertInteger("z", z);
  return Object.freeze({ x, y, z });
}

export function terrainChunkKey(coord: TerrainChunkCoord): TerrainChunkKey {
  return `${coord.x},${coord.y},${coord.z}`;
}

export function terrainChunkCoordContainingPosition(
  position: Vec3,
  cellSize = 1
): TerrainChunkCoord {
  assertPositiveCellSize(cellSize);
  const chunkSize = TERRAIN_CHUNK_CELLS_PER_AXIS * cellSize;
  return terrainChunkCoord(
    Math.floor(position.x / chunkSize),
    Math.floor(position.y / chunkSize),
    Math.floor(position.z / chunkSize)
  );
}

function assertInteger(name: string, value: number): void {
  if (!Number.isInteger(value)) {
    throw new Error(`Terrain chunk ${name} coordinate must be an integer.`);
  }
}

function assertPositiveCellSize(cellSize: number): void {
  if (cellSize <= 0) {
    throw new Error("Terrain chunk cellSize must be positive.");
  }
}
