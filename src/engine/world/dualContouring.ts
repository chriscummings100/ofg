import { dot, normalize, vec3, type Vec3 } from "../math/vec3.js";
import {
  sampleTerrainDensity,
  TERRAIN_CHUNK_CELLS_PER_AXIS,
  TerrainDensityChunk,
  type TerrainChunkBounds,
  type TerrainChunkSampleCoord,
  type TerrainDensitySource
} from "./terrainChunk.js";
import { colorForHeight, getFloatsPerVertex, type MeshData } from "./terrainMesh.js";

export type TerrainCellCoord = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
};

export type HermiteIntersection = {
  readonly edgeIndex: number;
  readonly startSample: TerrainChunkSampleCoord;
  readonly endSample: TerrainChunkSampleCoord;
  readonly t: number;
  readonly position: Vec3;
  readonly normal: Vec3;
};

export type DualContouringVertexPlacement = "qef" | "centroid";

export type DualContouringCellVertexOptions = {
  readonly placement?: DualContouringVertexPlacement;
};

export type DualContouringMeshOptions = DualContouringCellVertexOptions;

type CellCorner = TerrainChunkSampleCoord;

const CELL_CORNERS: readonly CellCorner[] = [
  { x: 0, y: 0, z: 0 },
  { x: 1, y: 0, z: 0 },
  { x: 0, y: 1, z: 0 },
  { x: 1, y: 1, z: 0 },
  { x: 0, y: 0, z: 1 },
  { x: 1, y: 0, z: 1 },
  { x: 0, y: 1, z: 1 },
  { x: 1, y: 1, z: 1 }
] as const;

const CELL_EDGES: readonly (readonly [number, number])[] = [
  [0, 1],
  [2, 3],
  [4, 5],
  [6, 7],
  [0, 2],
  [1, 3],
  [4, 6],
  [5, 7],
  [0, 4],
  [1, 5],
  [2, 6],
  [3, 7]
] as const;

export function extractHermiteIntersections(
  chunk: TerrainDensityChunk,
  cell: TerrainCellCoord,
  source: TerrainDensitySource
): HermiteIntersection[] {
  assertCellCoord(cell);
  const cornerDensities = CELL_CORNERS.map((corner) => {
    return chunk.densityAtSample(sampleForCellCorner(cell, corner));
  });
  const intersections: HermiteIntersection[] = [];

  for (let edgeIndex = 0; edgeIndex < CELL_EDGES.length; edgeIndex += 1) {
    const [startCornerIndex, endCornerIndex] = CELL_EDGES[edgeIndex];
    const startDensity = cornerDensities[startCornerIndex];
    const endDensity = cornerDensities[endCornerIndex];
    if (!hasSignChange(startDensity, endDensity)) {
      continue;
    }

    const startSample = sampleForCellCorner(cell, CELL_CORNERS[startCornerIndex]);
    const endSample = sampleForCellCorner(cell, CELL_CORNERS[endCornerIndex]);
    const startPosition = chunk.samplePosition(startSample);
    const endPosition = chunk.samplePosition(endSample);
    const t = clamp01(startDensity / (startDensity - endDensity));
    const position = lerpVec3(startPosition, endPosition, t);
    const normal = normalize(sampleTerrainDensity(source, position).gradient);

    intersections.push({
      edgeIndex,
      startSample,
      endSample,
      t,
      position,
      normal
    });
  }

  return intersections;
}

export function dualContouringCellBounds(
  chunk: TerrainDensityChunk,
  cell: TerrainCellCoord
): TerrainChunkBounds {
  assertCellCoord(cell);
  const min = chunk.samplePosition(cell);

  return {
    min,
    max: vec3(
      min.x + chunk.cellSize,
      min.y + chunk.cellSize,
      min.z + chunk.cellSize
    )
  };
}

export function placeDualContouringCellVertex(
  intersections: readonly HermiteIntersection[],
  bounds: TerrainChunkBounds,
  options: DualContouringCellVertexOptions = {}
): Vec3 | undefined {
  if (intersections.length === 0) {
    return undefined;
  }

  const centroid = centroidOfIntersections(intersections);
  if (options.placement === "centroid") {
    return clampToBounds(centroid, bounds);
  }

  return clampToBounds(solveQef(intersections) ?? centroid, bounds);
}

export function meshChunkDualContouring(
  chunk: TerrainDensityChunk,
  source: TerrainDensitySource,
  options: DualContouringMeshOptions = {}
): MeshData {
  const cellVertexIndices = new Int32Array(
    TERRAIN_CHUNK_CELLS_PER_AXIS *
    TERRAIN_CHUNK_CELLS_PER_AXIS *
    TERRAIN_CHUNK_CELLS_PER_AXIS
  );
  cellVertexIndices.fill(-1);
  const vertices: number[] = [];

  for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y < TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const cell = { x, y, z };
        const intersections = extractHermiteIntersections(chunk, cell, source);
        const position = placeDualContouringCellVertex(
          intersections,
          dualContouringCellBounds(chunk, cell),
          options
        );
        if (position === undefined) {
          continue;
        }

        const vertexIndex = vertices.length / getFloatsPerVertex();
        cellVertexIndices[cellIndex(x, y, z)] = vertexIndex;
        writeDualContouringVertex(vertices, chunk.bounds(), position, averageNormal(intersections));
      }
    }
  }

  const indices: number[] = [];
  emitXEdgeQuads(chunk, cellVertexIndices, indices);
  emitYEdgeQuads(chunk, cellVertexIndices, indices);
  emitZEdgeQuads(chunk, cellVertexIndices, indices);

  return {
    vertices: Float32Array.from(vertices),
    indices: Uint32Array.from(indices)
  };
}

function emitXEdgeQuads(
  chunk: TerrainDensityChunk,
  cellVertexIndices: Int32Array,
  indices: number[]
): void {
  for (let z = 0; z <= TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y <= TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const startDensity = chunk.densityAtSample({ x, y, z });
        const endDensity = chunk.densityAtSample({ x: x + 1, y, z });
        if (!hasSignChange(startDensity, endDensity)) {
          continue;
        }

        emitQuad(indices, [
          cellVertexIndex(cellVertexIndices, x, y - 1, z - 1),
          cellVertexIndex(cellVertexIndices, x, y, z - 1),
          cellVertexIndex(cellVertexIndices, x, y - 1, z),
          cellVertexIndex(cellVertexIndices, x, y, z)
        ], startDensity <= 0 && endDensity > 0);
      }
    }
  }
}

function emitYEdgeQuads(
  chunk: TerrainDensityChunk,
  cellVertexIndices: Int32Array,
  indices: number[]
): void {
  for (let z = 0; z <= TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y < TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x <= TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const startDensity = chunk.densityAtSample({ x, y, z });
        const endDensity = chunk.densityAtSample({ x, y: y + 1, z });
        if (!hasSignChange(startDensity, endDensity)) {
          continue;
        }

        emitQuad(indices, [
          cellVertexIndex(cellVertexIndices, x - 1, y, z - 1),
          cellVertexIndex(cellVertexIndices, x - 1, y, z),
          cellVertexIndex(cellVertexIndices, x, y, z - 1),
          cellVertexIndex(cellVertexIndices, x, y, z)
        ], startDensity <= 0 && endDensity > 0);
      }
    }
  }
}

function emitZEdgeQuads(
  chunk: TerrainDensityChunk,
  cellVertexIndices: Int32Array,
  indices: number[]
): void {
  for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y <= TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x <= TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const startDensity = chunk.densityAtSample({ x, y, z });
        const endDensity = chunk.densityAtSample({ x, y, z: z + 1 });
        if (!hasSignChange(startDensity, endDensity)) {
          continue;
        }

        emitQuad(indices, [
          cellVertexIndex(cellVertexIndices, x - 1, y - 1, z),
          cellVertexIndex(cellVertexIndices, x, y - 1, z),
          cellVertexIndex(cellVertexIndices, x - 1, y, z),
          cellVertexIndex(cellVertexIndices, x, y, z)
        ], startDensity <= 0 && endDensity > 0);
      }
    }
  }
}

function emitQuad(indices: number[], vertices: readonly number[], forward: boolean): void {
  if (vertices.some((vertex) => vertex < 0)) {
    return;
  }

  const [a, b, c, d] = vertices;
  if (forward) {
    indices.push(a, b, c, c, b, d);
    return;
  }

  indices.push(a, c, b, c, d, b);
}

function writeDualContouringVertex(
  vertices: number[],
  chunkBounds: TerrainChunkBounds,
  position: Vec3,
  normal: Vec3
): void {
  const color = colorForHeight(position.y);
  const width = chunkBounds.max.x - chunkBounds.min.x;
  const depth = chunkBounds.max.z - chunkBounds.min.z;

  vertices.push(
    position.x,
    position.y,
    position.z,
    color[0],
    color[1],
    color[2],
    normal.x,
    normal.y,
    normal.z,
    width === 0 ? 0 : (position.x - chunkBounds.min.x) / width,
    depth === 0 ? 0 : (position.z - chunkBounds.min.z) / depth
  );
}

function averageNormal(intersections: readonly HermiteIntersection[]): Vec3 {
  let x = 0;
  let y = 0;
  let z = 0;
  for (const intersection of intersections) {
    x += intersection.normal.x;
    y += intersection.normal.y;
    z += intersection.normal.z;
  }

  return normalize(vec3(x, y, z));
}

function solveQef(intersections: readonly HermiteIntersection[]): Vec3 | undefined {
  let m00 = 0;
  let m01 = 0;
  let m02 = 0;
  let m11 = 0;
  let m12 = 0;
  let m22 = 0;
  let r0 = 0;
  let r1 = 0;
  let r2 = 0;

  for (const intersection of intersections) {
    const normal = intersection.normal;
    if (normal.x === 0 && normal.y === 0 && normal.z === 0) {
      continue;
    }

    const b = dot(normal, intersection.position);
    m00 += normal.x * normal.x;
    m01 += normal.x * normal.y;
    m02 += normal.x * normal.z;
    m11 += normal.y * normal.y;
    m12 += normal.y * normal.z;
    m22 += normal.z * normal.z;
    r0 += normal.x * b;
    r1 += normal.y * b;
    r2 += normal.z * b;
  }

  return solve3x3([
    [m00, m01, m02, r0],
    [m01, m11, m12, r1],
    [m02, m12, m22, r2]
  ]);
}

function solve3x3(matrix: number[][]): Vec3 | undefined {
  for (let column = 0; column < 3; column += 1) {
    let pivotRow = column;
    for (let row = column + 1; row < 3; row += 1) {
      if (Math.abs(matrix[row][column]) > Math.abs(matrix[pivotRow][column])) {
        pivotRow = row;
      }
    }

    if (Math.abs(matrix[pivotRow][column]) < 1e-8) {
      return undefined;
    }

    if (pivotRow !== column) {
      const temp = matrix[column];
      matrix[column] = matrix[pivotRow];
      matrix[pivotRow] = temp;
    }

    const pivot = matrix[column][column];
    for (let value = column; value < 4; value += 1) {
      matrix[column][value] /= pivot;
    }

    for (let row = 0; row < 3; row += 1) {
      if (row === column) {
        continue;
      }

      const factor = matrix[row][column];
      for (let value = column; value < 4; value += 1) {
        matrix[row][value] -= factor * matrix[column][value];
      }
    }
  }

  return vec3(matrix[0][3], matrix[1][3], matrix[2][3]);
}

function centroidOfIntersections(intersections: readonly HermiteIntersection[]): Vec3 {
  let x = 0;
  let y = 0;
  let z = 0;
  for (const intersection of intersections) {
    x += intersection.position.x;
    y += intersection.position.y;
    z += intersection.position.z;
  }

  return vec3(
    x / intersections.length,
    y / intersections.length,
    z / intersections.length
  );
}

function sampleForCellCorner(
  cell: TerrainCellCoord,
  corner: CellCorner
): TerrainChunkSampleCoord {
  return {
    x: cell.x + corner.x,
    y: cell.y + corner.y,
    z: cell.z + corner.z
  };
}

function cellVertexIndex(
  cellVertexIndices: Int32Array,
  x: number,
  y: number,
  z: number
): number {
  if (
    x < 0 ||
    y < 0 ||
    z < 0 ||
    x >= TERRAIN_CHUNK_CELLS_PER_AXIS ||
    y >= TERRAIN_CHUNK_CELLS_PER_AXIS ||
    z >= TERRAIN_CHUNK_CELLS_PER_AXIS
  ) {
    return -1;
  }

  return cellVertexIndices[cellIndex(x, y, z)];
}

function cellIndex(x: number, y: number, z: number): number {
  return x +
    y * TERRAIN_CHUNK_CELLS_PER_AXIS +
    z * TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS;
}

function lerpVec3(a: Vec3, b: Vec3, t: number): Vec3 {
  return vec3(
    a.x + (b.x - a.x) * t,
    a.y + (b.y - a.y) * t,
    a.z + (b.z - a.z) * t
  );
}

function clampToBounds(position: Vec3, bounds: TerrainChunkBounds): Vec3 {
  return vec3(
    clamp(position.x, bounds.min.x, bounds.max.x),
    clamp(position.y, bounds.min.y, bounds.max.y),
    clamp(position.z, bounds.min.z, bounds.max.z)
  );
}

function hasSignChange(a: number, b: number): boolean {
  return (a <= 0 && b > 0) || (a > 0 && b <= 0);
}

function clamp01(value: number): number {
  return clamp(value, 0, 1);
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function assertCellCoord(cell: TerrainCellCoord): void {
  assertCellCoordAxis("x", cell.x);
  assertCellCoordAxis("y", cell.y);
  assertCellCoordAxis("z", cell.z);
}

function assertCellCoordAxis(name: string, value: number): void {
  if (!Number.isInteger(value) || value < 0 || value >= TERRAIN_CHUNK_CELLS_PER_AXIS) {
    throw new Error(`Dual Contouring cell ${name} must be an integer from 0 to 31.`);
  }
}
