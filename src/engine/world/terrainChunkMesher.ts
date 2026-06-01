import { normalize, vec3, type Vec3 } from "../math/vec3.js";
import {
  TERRAIN_CHUNK_CELLS_PER_AXIS,
  TERRAIN_CHUNK_SAMPLES_PER_AXIS,
  TerrainDensityChunk,
  terrainChunkCoord
} from "./terrainChunk.js";
import {
  colorForHeight,
  getFloatsPerVertex,
  writePackedTerrainMaterial,
  type MeshData
} from "./terrainMesh.js";

const NO_SURFACE = Number.NaN;

export type TerrainSurfaceMesherOptions = {
  readonly surfaceNormalAt?: (position: Vec3) => Vec3;
};

export function findHighestSurfaceInColumn(
  chunk: TerrainDensityChunk,
  x: number,
  z: number
): number | undefined {
  assertColumnSampleCoord("x", x);
  assertColumnSampleCoord("z", z);

  for (let y = TERRAIN_CHUNK_CELLS_PER_AXIS - 1; y >= 0; y -= 1) {
    const lower = chunk.densityAtSample({ x, y, z });
    const upper = chunk.densityAtSample({ x, y: y + 1, z });
    if (lower <= 0 && upper > 0) {
      const t = lower === upper ? 0 : lower / (lower - upper);
      return chunk.samplePosition({ x, y, z }).y + t * chunk.cellSize;
    }
  }

  return undefined;
}

export function meshChunkHighestSurface(
  chunk: TerrainDensityChunk,
  options: TerrainSurfaceMesherOptions = {}
): MeshData {
  const heights = sampleHighestSurfaceHeights([chunk]);

  return meshSurfaceHeights(chunk, heights, options);
}

export function meshChunkHighestSurfaceStack(
  chunks: readonly TerrainDensityChunk[],
  options: TerrainSurfaceMesherOptions = {}
): MeshData {
  if (chunks.length === 0) {
    throw new Error("meshChunkHighestSurfaceStack requires at least one terrain density chunk.");
  }

  const sortedChunks = [...chunks].sort((a, b) => b.coord.y - a.coord.y);
  const reference = sortedChunks[0];
  for (const chunk of sortedChunks) {
    if (
      chunk.coord.x !== reference.coord.x ||
      chunk.coord.z !== reference.coord.z ||
      chunk.cellSize !== reference.cellSize
    ) {
      throw new Error("Stacked terrain chunks must share x, z, and cellSize.");
    }
  }
  for (let index = 1; index < sortedChunks.length; index += 1) {
    const previousY = sortedChunks[index - 1].coord.y;
    const currentY = sortedChunks[index].coord.y;
    if (previousY - currentY !== 1) {
      throw new Error("Stacked terrain chunks must be vertically contiguous.");
    }
  }

  const referenceCoord = terrainChunkCoord(reference.coord.x, 0, reference.coord.z);
  const referenceChunk = new TerrainDensityChunk(referenceCoord, { cellSize: reference.cellSize });
  const heights = sampleHighestSurfaceHeights(sortedChunks);

  return meshSurfaceHeights(referenceChunk, heights, options);
}

function sampleHighestSurfaceHeights(
  chunks: readonly TerrainDensityChunk[]
): Float32Array {
  const heights = new Float32Array(TERRAIN_CHUNK_SAMPLES_PER_AXIS * TERRAIN_CHUNK_SAMPLES_PER_AXIS);
  heights.fill(NO_SURFACE);

  for (let z = 0; z < TERRAIN_CHUNK_SAMPLES_PER_AXIS; z += 1) {
    for (let x = 0; x < TERRAIN_CHUNK_SAMPLES_PER_AXIS; x += 1) {
      for (const chunk of chunks) {
        const height = findHighestSurfaceInColumn(chunk, x, z);
        if (height !== undefined) {
          heights[surfaceIndex(x, z)] = height;
          break;
        }
      }
    }
  }

  return heights;
}

function meshSurfaceHeights(
  referenceChunk: TerrainDensityChunk,
  heights: Float32Array,
  options: TerrainSurfaceMesherOptions
): MeshData {
  const vertices = new Float32Array(
    TERRAIN_CHUNK_SAMPLES_PER_AXIS *
    TERRAIN_CHUNK_SAMPLES_PER_AXIS *
    getFloatsPerVertex()
  );
  for (let z = 0; z < TERRAIN_CHUNK_SAMPLES_PER_AXIS; z += 1) {
    for (let x = 0; x < TERRAIN_CHUNK_SAMPLES_PER_AXIS; x += 1) {
      writeSurfaceVertex(referenceChunk, heights, vertices, x, z, options);
    }
  }

  const indices: number[] = [];
  for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
      if (
        !hasSurface(heights, x, z) ||
        !hasSurface(heights, x + 1, z) ||
        !hasSurface(heights, x, z + 1) ||
        !hasSurface(heights, x + 1, z + 1)
      ) {
        continue;
      }

      const topLeft = surfaceIndex(x, z);
      const topRight = surfaceIndex(x + 1, z);
      const bottomLeft = surfaceIndex(x, z + 1);
      const bottomRight = surfaceIndex(x + 1, z + 1);
      indices.push(
        topLeft, bottomLeft, topRight,
        topRight, bottomLeft, bottomRight
      );
    }
  }

  return {
    vertices,
    indices: Uint32Array.from(indices)
  };
}

function writeSurfaceVertex(
  chunk: TerrainDensityChunk,
  heights: Float32Array,
  vertices: Float32Array,
  x: number,
  z: number,
  options: TerrainSurfaceMesherOptions
): void {
  const vertexOffset = surfaceIndex(x, z) * getFloatsPerVertex();
  const samplePosition = chunk.samplePosition({ x, y: 0, z });
  const height = heightOrFallback(heights, x, z, samplePosition.y);
  const color = colorForHeight(height);
  const position = vec3(samplePosition.x, height, samplePosition.z);
  const normal = sampledNormalAt(options, position) ?? normalForSurface(heights, x, z, chunk.cellSize);

  vertices[vertexOffset + 0] = samplePosition.x;
  vertices[vertexOffset + 1] = height;
  vertices[vertexOffset + 2] = samplePosition.z;
  vertices[vertexOffset + 3] = color[0];
  vertices[vertexOffset + 4] = color[1];
  vertices[vertexOffset + 5] = color[2];
  vertices[vertexOffset + 6] = normal.x;
  vertices[vertexOffset + 7] = normal.y;
  vertices[vertexOffset + 8] = normal.z;
  vertices[vertexOffset + 9] = x / TERRAIN_CHUNK_CELLS_PER_AXIS;
  vertices[vertexOffset + 10] = z / TERRAIN_CHUNK_CELLS_PER_AXIS;
  writePackedTerrainMaterial(vertices, vertexOffset);
}

function sampledNormalAt(options: TerrainSurfaceMesherOptions, position: Vec3): Vec3 | undefined {
  const normal = options.surfaceNormalAt?.(position);
  if (normal === undefined) {
    return undefined;
  }

  if (!Number.isFinite(normal.x) || !Number.isFinite(normal.y) || !Number.isFinite(normal.z)) {
    return undefined;
  }

  const length = Math.hypot(normal.x, normal.y, normal.z);
  if (length <= Number.EPSILON) {
    return undefined;
  }

  return normalize(normal);
}

function normalForSurface(
  heights: Float32Array,
  x: number,
  z: number,
  cellSize: number
) {
  const center = heightOrFallback(heights, x, z, 0);
  const left = heightOrFallback(heights, x - 1, z, center);
  const right = heightOrFallback(heights, x + 1, z, center);
  const back = heightOrFallback(heights, x, z - 1, center);
  const front = heightOrFallback(heights, x, z + 1, center);
  const dx = (right - left) / ((x > 0 && x < TERRAIN_CHUNK_CELLS_PER_AXIS ? 2 : 1) * cellSize);
  const dz = (front - back) / ((z > 0 && z < TERRAIN_CHUNK_CELLS_PER_AXIS ? 2 : 1) * cellSize);

  return normalize(vec3(-dx, 1, -dz));
}

function heightOrFallback(
  heights: Float32Array,
  x: number,
  z: number,
  fallback: number
): number {
  if (x < 0 || z < 0 || x >= TERRAIN_CHUNK_SAMPLES_PER_AXIS || z >= TERRAIN_CHUNK_SAMPLES_PER_AXIS) {
    return fallback;
  }

  const height = heights[surfaceIndex(x, z)];
  return Number.isFinite(height) ? height : fallback;
}

function hasSurface(heights: Float32Array, x: number, z: number): boolean {
  return Number.isFinite(heights[surfaceIndex(x, z)]);
}

function surfaceIndex(x: number, z: number): number {
  return x + z * TERRAIN_CHUNK_SAMPLES_PER_AXIS;
}

function assertColumnSampleCoord(name: string, value: number): void {
  if (!Number.isInteger(value) || value < 0 || value >= TERRAIN_CHUNK_SAMPLES_PER_AXIS) {
    throw new Error(`Terrain surface column ${name} must be an integer from 0 to 32.`);
  }
}
