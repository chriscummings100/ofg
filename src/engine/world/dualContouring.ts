import { dot, normalize, vec3, type Vec3 } from "../math/vec3.js";
import {
  sampleTerrainDensity,
  TERRAIN_CHUNK_CELLS_PER_AXIS,
  TerrainDensityChunk,
  type TerrainChunkCoord,
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

type DualContouringEdgeAxis = "x" | "y" | "z";

export type DualContouringVertexFallbackReason =
  | "none"
  | "empty"
  | "forcedCentroid"
  | "underconstrained"
  | "nonFinite"
  | "outOfBounds";

export type DualContouringCellVertexDebug = {
  readonly placement: DualContouringVertexPlacement | "none";
  readonly fallbackReason: DualContouringVertexFallbackReason;
  readonly position?: Vec3;
  readonly centroid?: Vec3;
  readonly qefPosition?: Vec3;
  readonly intersectionCount: number;
  readonly finalError: number;
  readonly centroidError: number;
  readonly qefError?: number;
};

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

export function extractHermiteIntersectionsForBounds(
  bounds: TerrainChunkBounds,
  source: TerrainDensitySource
): HermiteIntersection[] {
  const cornerDensities = CELL_CORNERS.map((corner) => {
    return source.densityAt(positionForBoundsCorner(bounds, corner));
  });
  const intersections: HermiteIntersection[] = [];

  for (let edgeIndex = 0; edgeIndex < CELL_EDGES.length; edgeIndex += 1) {
    const [startCornerIndex, endCornerIndex] = CELL_EDGES[edgeIndex];
    const startDensity = cornerDensities[startCornerIndex];
    const endDensity = cornerDensities[endCornerIndex];
    if (!hasSignChange(startDensity, endDensity)) {
      continue;
    }

    const startSample = CELL_CORNERS[startCornerIndex];
    const endSample = CELL_CORNERS[endCornerIndex];
    const startPosition = positionForBoundsCorner(bounds, startSample);
    const endPosition = positionForBoundsCorner(bounds, endSample);
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
  return analyzeDualContouringCellVertex(intersections, bounds, options).position;
}

export function analyzeDualContouringCellVertex(
  intersections: readonly HermiteIntersection[],
  bounds: TerrainChunkBounds,
  options: DualContouringCellVertexOptions = {}
): DualContouringCellVertexDebug {
  if (intersections.length === 0) {
    return {
      placement: "none",
      fallbackReason: "empty",
      intersectionCount: 0,
      finalError: 0,
      centroidError: 0
    };
  }

  const centroid = centroidOfIntersections(intersections);
  const centroidPosition = clampToBounds(centroid, bounds);
  const centroidError = qefErrorAt(intersections, centroidPosition);
  if (options.placement === "centroid") {
    return {
      placement: "centroid",
      fallbackReason: "forcedCentroid",
      position: centroidPosition,
      centroid,
      intersectionCount: intersections.length,
      finalError: centroidError,
      centroidError
    };
  }

  const qefPosition = solveQef(intersections);
  if (qefPosition === undefined) {
    return {
      placement: "centroid",
      fallbackReason: "underconstrained",
      position: centroidPosition,
      centroid,
      intersectionCount: intersections.length,
      finalError: centroidError,
      centroidError
    };
  }

  const qefError = qefErrorAt(intersections, qefPosition);
  if (!isFiniteVec3(qefPosition)) {
    return {
      placement: "centroid",
      fallbackReason: "nonFinite",
      position: centroidPosition,
      centroid,
      qefPosition,
      intersectionCount: intersections.length,
      finalError: centroidError,
      centroidError,
      qefError
    };
  }

  if (!isInsideBounds(qefPosition, bounds)) {
    return {
      placement: "centroid",
      fallbackReason: "outOfBounds",
      position: centroidPosition,
      centroid,
      qefPosition,
      intersectionCount: intersections.length,
      finalError: centroidError,
      centroidError,
      qefError
    };
  }

  return {
    placement: "qef",
    fallbackReason: "none",
    position: qefPosition,
    centroid,
    qefPosition,
    intersectionCount: intersections.length,
    finalError: qefError,
    centroidError,
    qefError
  };
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

export function meshChunksDualContouring(
  chunks: readonly TerrainDensityChunk[],
  source: TerrainDensitySource,
  options: DualContouringMeshOptions = {}
): MeshData {
  if (chunks.length === 0) {
    throw new Error("meshChunksDualContouring requires at least one terrain density chunk.");
  }

  const sortedChunks = [...chunks].sort(compareChunks);
  const firstChunk = sortedChunks[0];
  for (const chunk of sortedChunks) {
    if (chunk.cellSize !== firstChunk.cellSize) {
      throw new Error("Dual Contouring terrain chunks must share cellSize.");
    }
  }

  const vertexIndices = new Map<string, number>();
  const vertices: number[] = [];
  const meshBounds = boundsForChunks(sortedChunks);

  for (const chunk of sortedChunks) {
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
          vertexIndices.set(globalCellKey(chunk, x, y, z), vertexIndex);
          writeDualContouringVertex(vertices, meshBounds, position, averageNormal(intersections));
        }
      }
    }
  }

  const indices: number[] = [];
  const emittedEdges = new Set<string>();
  emitMultiChunkXEdgeQuads(sortedChunks, vertexIndices, emittedEdges, indices);
  emitMultiChunkYEdgeQuads(sortedChunks, vertexIndices, emittedEdges, indices);
  emitMultiChunkZEdgeQuads(sortedChunks, vertexIndices, emittedEdges, indices);

  return {
    vertices: Float32Array.from(vertices),
    indices: Uint32Array.from(indices)
  };
}

export function meshChunkDualContouringWithNeighbors(
  chunks: readonly TerrainDensityChunk[],
  centerCoord: TerrainChunkCoord,
  source: TerrainDensitySource,
  options: DualContouringMeshOptions = {}
): MeshData {
  if (chunks.length === 0) {
    throw new Error("meshChunkDualContouringWithNeighbors requires at least one terrain density chunk.");
  }

  const sortedChunks = [...chunks].sort(compareChunks);
  const chunkMap = new Map(sortedChunks.map((chunk) => [terrainChunkKeyFromCoord(chunk.coord), chunk]));
  const centerChunk = sortedChunks.find((chunk) =>
    chunk.coord.x === centerCoord.x &&
    chunk.coord.y === centerCoord.y &&
    chunk.coord.z === centerCoord.z
  );
  if (centerChunk === undefined) {
    throw new Error("Neighbor-aware Dual Contouring requires the center chunk.");
  }

  for (const chunk of sortedChunks) {
    if (chunk.cellSize !== centerChunk.cellSize) {
      throw new Error("Neighbor-aware Dual Contouring chunks must share cellSize.");
    }
  }

  const vertexIndices = new Map<string, number>();
  const vertices: number[] = [];
  const meshBounds = centerChunk.bounds();

  for (let z = 0; z <= TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y <= TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x <= TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const cellRef = localApronCellRef(centerCoord, x, y, z);
        const chunk = chunkMap.get(terrainChunkKeyFromCoord(cellRef.chunkCoord));
        if (chunk === undefined) {
          continue;
        }

        const intersections = extractHermiteIntersections(chunk, cellRef.cell, source);
        const position = placeDualContouringCellVertex(
          intersections,
          dualContouringCellBounds(chunk, cellRef.cell),
          options
        );
        if (position === undefined) {
          continue;
        }

        const vertexIndex = vertices.length / getFloatsPerVertex();
        vertexIndices.set(localCellKey(x, y, z), vertexIndex);
        writeDualContouringVertex(vertices, meshBounds, position, averageNormal(intersections));
      }
    }
  }

  const indices: number[] = [];
  emitOwnedXEdgeQuads(centerChunk, vertexIndices, indices);
  emitOwnedYEdgeQuads(centerChunk, vertexIndices, indices);
  emitOwnedZEdgeQuads(centerChunk, vertexIndices, indices);

  return compactMeshData(vertices, indices);
}

function emitOwnedXEdgeQuads(
  chunk: TerrainDensityChunk,
  cellVertexIndices: ReadonlyMap<string, number>,
  indices: number[]
): void {
  for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y < TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const startDensity = chunk.densityAtSample({ x, y: y + 1, z: z + 1 });
        const endDensity = chunk.densityAtSample({ x: x + 1, y: y + 1, z: z + 1 });
        if (!hasSignChange(startDensity, endDensity)) {
          continue;
        }

        emitQuad(indices, [
          localCellVertexIndex(cellVertexIndices, x, y, z),
          localCellVertexIndex(cellVertexIndices, x, y + 1, z),
          localCellVertexIndex(cellVertexIndices, x, y, z + 1),
          localCellVertexIndex(cellVertexIndices, x, y + 1, z + 1)
        ], startDensity <= 0 && endDensity > 0);
      }
    }
  }
}

function emitOwnedYEdgeQuads(
  chunk: TerrainDensityChunk,
  cellVertexIndices: ReadonlyMap<string, number>,
  indices: number[]
): void {
  for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y < TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const startDensity = chunk.densityAtSample({ x: x + 1, y, z: z + 1 });
        const endDensity = chunk.densityAtSample({ x: x + 1, y: y + 1, z: z + 1 });
        if (!hasSignChange(startDensity, endDensity)) {
          continue;
        }

        emitQuad(indices, [
          localCellVertexIndex(cellVertexIndices, x, y, z),
          localCellVertexIndex(cellVertexIndices, x, y, z + 1),
          localCellVertexIndex(cellVertexIndices, x + 1, y, z),
          localCellVertexIndex(cellVertexIndices, x + 1, y, z + 1)
        ], startDensity <= 0 && endDensity > 0);
      }
    }
  }
}

function emitOwnedZEdgeQuads(
  chunk: TerrainDensityChunk,
  cellVertexIndices: ReadonlyMap<string, number>,
  indices: number[]
): void {
  for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
    for (let y = 0; y < TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
      for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
        const startDensity = chunk.densityAtSample({ x: x + 1, y: y + 1, z });
        const endDensity = chunk.densityAtSample({ x: x + 1, y: y + 1, z: z + 1 });
        if (!hasSignChange(startDensity, endDensity)) {
          continue;
        }

        emitQuad(indices, [
          localCellVertexIndex(cellVertexIndices, x, y, z),
          localCellVertexIndex(cellVertexIndices, x + 1, y, z),
          localCellVertexIndex(cellVertexIndices, x, y + 1, z),
          localCellVertexIndex(cellVertexIndices, x + 1, y + 1, z)
        ], startDensity <= 0 && endDensity > 0);
      }
    }
  }
}

function emitMultiChunkXEdgeQuads(
  chunks: readonly TerrainDensityChunk[],
  vertexIndices: ReadonlyMap<string, number>,
  emittedEdges: Set<string>,
  indices: number[],
  ownsEdge: (axis: DualContouringEdgeAxis, global: TerrainCellCoord) => boolean = () => true
): void {
  for (const chunk of chunks) {
    for (let z = 0; z <= TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
      for (let y = 0; y <= TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
        for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
          const global = globalSampleCoord(chunk, x, y, z);
          const edgeKey = `x:${global.x},${global.y},${global.z}`;
          if (emittedEdges.has(edgeKey)) {
            continue;
          }
          emittedEdges.add(edgeKey);
          if (!ownsEdge("x", global)) {
            continue;
          }

          const startDensity = chunk.densityAtSample({ x, y, z });
          const endDensity = chunk.densityAtSample({ x: x + 1, y, z });
          if (!hasSignChange(startDensity, endDensity)) {
            continue;
          }

          emitQuad(indices, [
            cellVertexIndexFromMap(vertexIndices, global.x, global.y - 1, global.z - 1),
            cellVertexIndexFromMap(vertexIndices, global.x, global.y, global.z - 1),
            cellVertexIndexFromMap(vertexIndices, global.x, global.y - 1, global.z),
            cellVertexIndexFromMap(vertexIndices, global.x, global.y, global.z)
          ], startDensity <= 0 && endDensity > 0);
        }
      }
    }
  }
}

function emitMultiChunkYEdgeQuads(
  chunks: readonly TerrainDensityChunk[],
  vertexIndices: ReadonlyMap<string, number>,
  emittedEdges: Set<string>,
  indices: number[],
  ownsEdge: (axis: DualContouringEdgeAxis, global: TerrainCellCoord) => boolean = () => true
): void {
  for (const chunk of chunks) {
    for (let z = 0; z <= TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
      for (let y = 0; y < TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
        for (let x = 0; x <= TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
          const global = globalSampleCoord(chunk, x, y, z);
          const edgeKey = `y:${global.x},${global.y},${global.z}`;
          if (emittedEdges.has(edgeKey)) {
            continue;
          }
          emittedEdges.add(edgeKey);
          if (!ownsEdge("y", global)) {
            continue;
          }

          const startDensity = chunk.densityAtSample({ x, y, z });
          const endDensity = chunk.densityAtSample({ x, y: y + 1, z });
          if (!hasSignChange(startDensity, endDensity)) {
            continue;
          }

          emitQuad(indices, [
            cellVertexIndexFromMap(vertexIndices, global.x - 1, global.y, global.z - 1),
            cellVertexIndexFromMap(vertexIndices, global.x - 1, global.y, global.z),
            cellVertexIndexFromMap(vertexIndices, global.x, global.y, global.z - 1),
            cellVertexIndexFromMap(vertexIndices, global.x, global.y, global.z)
          ], startDensity <= 0 && endDensity > 0);
        }
      }
    }
  }
}

function emitMultiChunkZEdgeQuads(
  chunks: readonly TerrainDensityChunk[],
  vertexIndices: ReadonlyMap<string, number>,
  emittedEdges: Set<string>,
  indices: number[],
  ownsEdge: (axis: DualContouringEdgeAxis, global: TerrainCellCoord) => boolean = () => true
): void {
  for (const chunk of chunks) {
    for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
      for (let y = 0; y <= TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
        for (let x = 0; x <= TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
          const global = globalSampleCoord(chunk, x, y, z);
          const edgeKey = `z:${global.x},${global.y},${global.z}`;
          if (emittedEdges.has(edgeKey)) {
            continue;
          }
          emittedEdges.add(edgeKey);
          if (!ownsEdge("z", global)) {
            continue;
          }

          const startDensity = chunk.densityAtSample({ x, y, z });
          const endDensity = chunk.densityAtSample({ x, y, z: z + 1 });
          if (!hasSignChange(startDensity, endDensity)) {
            continue;
          }

          emitQuad(indices, [
            cellVertexIndexFromMap(vertexIndices, global.x - 1, global.y - 1, global.z),
            cellVertexIndexFromMap(vertexIndices, global.x, global.y - 1, global.z),
            cellVertexIndexFromMap(vertexIndices, global.x - 1, global.y, global.z),
            cellVertexIndexFromMap(vertexIndices, global.x, global.y, global.z)
          ], startDensity <= 0 && endDensity > 0);
        }
      }
    }
  }
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

function qefErrorAt(intersections: readonly HermiteIntersection[], position: Vec3): number {
  if (intersections.length === 0) {
    return 0;
  }

  let error = 0;
  for (const intersection of intersections) {
    const planeDistance = dot(intersection.normal, vec3(
      position.x - intersection.position.x,
      position.y - intersection.position.y,
      position.z - intersection.position.z
    ));
    error += planeDistance * planeDistance;
  }

  return error / intersections.length;
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

function positionForBoundsCorner(bounds: TerrainChunkBounds, corner: CellCorner): Vec3 {
  return vec3(
    corner.x === 0 ? bounds.min.x : bounds.max.x,
    corner.y === 0 ? bounds.min.y : bounds.max.y,
    corner.z === 0 ? bounds.min.z : bounds.max.z
  );
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

function cellVertexIndexFromMap(
  cellVertexIndices: ReadonlyMap<string, number>,
  x: number,
  y: number,
  z: number
): number {
  return cellVertexIndices.get(cellKey(x, y, z)) ?? -1;
}

function globalCellKey(
  chunk: TerrainDensityChunk,
  x: number,
  y: number,
  z: number
): string {
  const global = globalSampleCoord(chunk, x, y, z);
  return cellKey(global.x, global.y, global.z);
}

function cellKey(x: number, y: number, z: number): string {
  return `${x},${y},${z}`;
}

function localCellKey(x: number, y: number, z: number): string {
  return `${x},${y},${z}`;
}

function localCellVertexIndex(
  cellVertexIndices: ReadonlyMap<string, number>,
  x: number,
  y: number,
  z: number
): number {
  return cellVertexIndices.get(localCellKey(x, y, z)) ?? -1;
}

function localApronCellRef(
  centerCoord: TerrainChunkCoord,
  x: number,
  y: number,
  z: number
): {
  readonly chunkCoord: TerrainChunkCoord;
  readonly cell: TerrainCellCoord;
} {
  return {
    chunkCoord: {
      x: centerCoord.x + (x === TERRAIN_CHUNK_CELLS_PER_AXIS ? 1 : 0),
      y: centerCoord.y + (y === TERRAIN_CHUNK_CELLS_PER_AXIS ? 1 : 0),
      z: centerCoord.z + (z === TERRAIN_CHUNK_CELLS_PER_AXIS ? 1 : 0)
    },
    cell: {
      x: x === TERRAIN_CHUNK_CELLS_PER_AXIS ? 0 : x,
      y: y === TERRAIN_CHUNK_CELLS_PER_AXIS ? 0 : y,
      z: z === TERRAIN_CHUNK_CELLS_PER_AXIS ? 0 : z
    }
  };
}

function terrainChunkKeyFromCoord(coord: TerrainChunkCoord): string {
  return `${coord.x},${coord.y},${coord.z}`;
}

function compactMeshData(vertices: readonly number[], indices: readonly number[]): MeshData {
  const floatsPerVertex = getFloatsPerVertex();
  const remap = new Map<number, number>();
  const compactVertices: number[] = [];
  const compactIndices: number[] = [];

  for (const index of indices) {
    let compactIndex = remap.get(index);
    if (compactIndex === undefined) {
      compactIndex = remap.size;
      remap.set(index, compactIndex);
      const vertexOffset = index * floatsPerVertex;
      for (let value = 0; value < floatsPerVertex; value += 1) {
        compactVertices.push(vertices[vertexOffset + value]);
      }
    }

    compactIndices.push(compactIndex);
  }

  return {
    vertices: Float32Array.from(compactVertices),
    indices: Uint32Array.from(compactIndices)
  };
}

function globalSampleCoord(
  chunk: TerrainDensityChunk,
  x: number,
  y: number,
  z: number
): TerrainCellCoord {
  return {
    x: chunk.coord.x * TERRAIN_CHUNK_CELLS_PER_AXIS + x,
    y: chunk.coord.y * TERRAIN_CHUNK_CELLS_PER_AXIS + y,
    z: chunk.coord.z * TERRAIN_CHUNK_CELLS_PER_AXIS + z
  };
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

function isInsideBounds(position: Vec3, bounds: TerrainChunkBounds): boolean {
  return position.x >= bounds.min.x &&
    position.x <= bounds.max.x &&
    position.y >= bounds.min.y &&
    position.y <= bounds.max.y &&
    position.z >= bounds.min.z &&
    position.z <= bounds.max.z;
}

function isFiniteVec3(position: Vec3): boolean {
  return Number.isFinite(position.x) &&
    Number.isFinite(position.y) &&
    Number.isFinite(position.z);
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

function compareChunks(a: TerrainDensityChunk, b: TerrainDensityChunk): number {
  return a.coord.z - b.coord.z ||
    a.coord.y - b.coord.y ||
    a.coord.x - b.coord.x;
}

function boundsForChunks(chunks: readonly TerrainDensityChunk[]): TerrainChunkBounds {
  const first = chunks[0].bounds();
  let minX = first.min.x;
  let minY = first.min.y;
  let minZ = first.min.z;
  let maxX = first.max.x;
  let maxY = first.max.y;
  let maxZ = first.max.z;

  for (let index = 1; index < chunks.length; index += 1) {
    const bounds = chunks[index].bounds();
    minX = Math.min(minX, bounds.min.x);
    minY = Math.min(minY, bounds.min.y);
    minZ = Math.min(minZ, bounds.min.z);
    maxX = Math.max(maxX, bounds.max.x);
    maxY = Math.max(maxY, bounds.max.y);
    maxZ = Math.max(maxZ, bounds.max.z);
  }

  return {
    min: vec3(minX, minY, minZ),
    max: vec3(maxX, maxY, maxZ)
  };
}
