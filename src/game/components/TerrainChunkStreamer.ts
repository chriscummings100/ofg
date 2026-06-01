import { Mesh } from "../../engine/render/Mesh.js";
import { TerrainRenderer } from "../../engine/render/TerrainRenderer.js";
import { Component } from "../../engine/scene/Component.js";
import type { Entity } from "../../engine/scene/Entity.js";
import type { ResourceId } from "../../engine/scene/types.js";
import type { Vec3 } from "../../engine/math/vec3.js";
import {
  generateTerrainDensityChunk,
  parseTerrainChunkKey,
  terrainChunkCoord,
  terrainChunkCoordContainingPosition,
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey,
  type TerrainDensityChunkGenerator,
  type TerrainDensitySource
} from "../../engine/world/terrainChunk.js";
import { meshChunkDualContouringWithNeighbors } from "../../engine/world/dualContouring.js";
import {
  POSITION_COLOR_NORMAL_UV_LAYOUT,
  expandTerrainMeshForTriangleMaterialPalettes
} from "../../engine/world/terrainMesh.js";
import type { MeshData } from "../../engine/world/terrainMesh.js";

export type TerrainChunkMeshGenerator = (
  coord: TerrainChunkCoord,
  cellSize: number
) => MeshData;

export type TerrainDensityWindowGenerator = (
  coords: readonly TerrainChunkCoord[],
  cellSize: number
) => void;

export type TerrainChunkStreamerOptions = {
  readonly target?: Entity;
  readonly material?: ResourceId;
  readonly horizontalRadius?: number;
  readonly verticalChunkOffsets?: readonly number[];
  readonly cellSize?: number;
  readonly meshIdPrefix?: string;
  readonly densityChunkGenerator?: TerrainDensityChunkGenerator;
  readonly prepareDensityChunks?: TerrainDensityWindowGenerator;
  readonly chunkMeshGenerator?: TerrainChunkMeshGenerator;
};

export class TerrainChunkStreamer extends Component {
  readonly terrain: TerrainRenderer;
  readonly source: TerrainDensitySource;
  target?: Entity;
  material?: ResourceId;
  horizontalRadius: number;
  verticalChunkOffsets: readonly number[];
  cellSize: number;
  meshIdPrefix: string;
  densityChunkGenerator?: TerrainDensityChunkGenerator;
  prepareDensityChunks?: TerrainDensityWindowGenerator;
  chunkMeshGenerator?: TerrainChunkMeshGenerator;

  private readonly loadedChunkKeys = new Set<TerrainChunkKey>();
  private readonly renderChunkKeys = new Set<TerrainChunkKey>();
  private lastCenterCoord?: TerrainChunkCoord;

  constructor(
    terrain: TerrainRenderer,
    source: TerrainDensitySource,
    options: TerrainChunkStreamerOptions = {}
  ) {
    super();
    this.terrain = terrain;
    this.source = source;
    this.target = options.target;
    this.material = options.material;
    this.horizontalRadius = options.horizontalRadius ?? 1;
    this.verticalChunkOffsets = options.verticalChunkOffsets ?? [-1, 0, 1];
    this.cellSize = options.cellSize ?? 1;
    this.meshIdPrefix = options.meshIdPrefix ?? "mesh:terrain.chunk";
    this.densityChunkGenerator = options.densityChunkGenerator;
    this.prepareDensityChunks = options.prepareDensityChunks;
    this.chunkMeshGenerator = options.chunkMeshGenerator;
    validateOptions(this.horizontalRadius, this.verticalChunkOffsets, this.cellSize);
  }

  override update(): void {
    const center = this.target?.transform.getWorldPosition() ?? this.entity?.transform.getWorldPosition();
    if (center === undefined) {
      return;
    }

    this.syncAround(center);
  }

  syncAround(center: Vec3): void {
    validateOptions(this.horizontalRadius, this.verticalChunkOffsets, this.cellSize);
    const centerCoord = terrainChunkCoordContainingPosition(center, this.cellSize);
    const desired = this.buildDesiredDensityChunkKeys(centerCoord);
    if (setsMatch(this.loadedChunkKeys, desired)) {
      this.lastCenterCoord = centerCoord;
      return;
    }

    for (const key of this.renderChunkKeys) {
      this.terrain.removeChunk(key);
    }
    this.renderChunkKeys.clear();
    this.loadedChunkKeys.clear();

    for (const key of desired) {
      this.loadedChunkKeys.add(key);
    }
    this.lastCenterCoord = centerCoord;
    this.loadRenderWindow(centerCoord);
  }

  rebuildChunk(chunk: TerrainChunkKey | TerrainChunkCoord): void {
    const coord = typeof chunk === "string" ? parseTerrainChunkKey(chunk) : chunk;
    const centerCoord = this.lastCenterCoord ?? coord;
    for (const key of this.renderChunkKeys) {
      this.terrain.removeChunk(key);
    }
    this.renderChunkKeys.clear();
    this.loadedChunkKeys.clear();
    for (const key of this.buildDesiredDensityChunkKeys(centerCoord)) {
      this.loadedChunkKeys.add(key);
    }

    this.loadRenderWindow(centerCoord);
  }

  invalidateAll(): void {
    for (const key of this.renderChunkKeys) {
      this.terrain.removeChunk(key);
    }

    this.loadedChunkKeys.clear();
    this.renderChunkKeys.clear();
  }

  getLoadedChunkKeys(): string[] {
    return [...this.loadedChunkKeys].sort();
  }

  private buildDesiredDensityChunkKeys(centerCoord: TerrainChunkCoord): Set<TerrainChunkKey> {
    const desired = new Set<TerrainChunkKey>();
    for (const coord of this.buildRenderChunkCoords(centerCoord)) {
      for (const densityCoord of this.buildNeighborChunkCoords(coord)) {
        desired.add(terrainChunkKey(densityCoord));
      }
    }

    return desired;
  }

  private loadRenderWindow(centerCoord: TerrainChunkCoord): void {
    if (this.chunkMeshGenerator !== undefined) {
      this.prepareDensityChunks?.(this.loadedDensityChunkCoords(), this.cellSize);
      this.loadRenderWindowFromMeshGenerator(centerCoord, this.chunkMeshGenerator);
      return;
    }

    const densityChunks = new Map<TerrainChunkKey, ReturnType<typeof generateTerrainDensityChunk>>();
    for (const coord of this.loadedDensityChunkCoords()) {
      this.getOrGenerateDensityChunk(densityChunks, coord);
    }

    for (const coord of this.buildRenderChunkCoords(centerCoord)) {
      const centerChunk = this.getOrGenerateDensityChunk(densityChunks, coord);
      if (!chunkMayContainSurface(centerChunk)) {
        continue;
      }

      const neighborChunks = this.buildNeighborChunkCoords(coord)
        .map((neighborCoord) => this.getOrGenerateDensityChunk(densityChunks, neighborCoord));
      const rawMeshData = meshChunkDualContouringWithNeighbors(neighborChunks, coord, this.source, {
        // Raw QEF placement is too unstable on noisy terrain until it is regularized.
        placement: "centroid"
      });
      const meshData = expandTerrainMeshForTriangleMaterialPalettes(rawMeshData);
      if (meshData.indices.length === 0) {
        continue;
      }

      const key = terrainChunkKey(coord);
      this.terrain.addChunk({
        key,
        mesh: new Mesh(
          `${this.meshIdPrefix}:${key}`,
          meshData.vertices,
          meshData.indices,
          POSITION_COLOR_NORMAL_UV_LAYOUT
        ),
        material: this.material
      });
      this.renderChunkKeys.add(key);
    }
  }

  private loadRenderWindowFromMeshGenerator(
    centerCoord: TerrainChunkCoord,
    chunkMeshGenerator: TerrainChunkMeshGenerator
  ): void {
    for (const coord of this.buildRenderChunkCoords(centerCoord)) {
      const meshData = chunkMeshGenerator(coord, this.cellSize);
      if (meshData.indices.length === 0) {
        continue;
      }

      const key = terrainChunkKey(coord);
      this.terrain.addChunk({
        key,
        mesh: new Mesh(
          `${this.meshIdPrefix}:${key}`,
          meshData.vertices,
          meshData.indices,
          POSITION_COLOR_NORMAL_UV_LAYOUT
        ),
        material: this.material
      });
      this.renderChunkKeys.add(key);
    }
  }

  private getOrGenerateDensityChunk(
    chunks: Map<TerrainChunkKey, ReturnType<typeof generateTerrainDensityChunk>>,
    coord: TerrainChunkCoord
  ): ReturnType<typeof generateTerrainDensityChunk> {
    const key = terrainChunkKey(coord);
    let chunk = chunks.get(key);
    if (chunk === undefined) {
      chunk = (this.densityChunkGenerator ?? generateTerrainDensityChunk)(
        this.source,
        coord,
        { cellSize: this.cellSize }
      );
      chunks.set(key, chunk);
    }

    return chunk;
  }

  private loadedDensityChunkCoords(): TerrainChunkCoord[] {
    return [...this.loadedChunkKeys].sort().map(parseTerrainChunkKey);
  }

  private buildRenderChunkCoords(centerCoord: TerrainChunkCoord): TerrainChunkCoord[] {
    const coords: TerrainChunkCoord[] = [];
    for (let z = centerCoord.z - this.horizontalRadius; z <= centerCoord.z + this.horizontalRadius; z += 1) {
      for (let x = centerCoord.x - this.horizontalRadius; x <= centerCoord.x + this.horizontalRadius; x += 1) {
        coords.push(...this.buildDensityChunkCoordsForColumn(x, centerCoord.y, z));
      }
    }

    return coords;
  }

  private buildNeighborChunkCoords(centerCoord: TerrainChunkCoord): TerrainChunkCoord[] {
    const coords: TerrainChunkCoord[] = [];
    for (let z = centerCoord.z; z <= centerCoord.z + 1; z += 1) {
      for (let y = centerCoord.y; y <= centerCoord.y + 1; y += 1) {
        for (let x = centerCoord.x; x <= centerCoord.x + 1; x += 1) {
          coords.push(terrainChunkCoord(x, y, z));
        }
      }
    }

    return coords;
  }

  private buildDensityChunkCoordsForColumn(x: number, centerY: number, z: number): TerrainChunkCoord[] {
    return this.verticalChunkOffsets.map((offset) => terrainChunkCoord(x, centerY + offset, z));
  }
}

function setsMatch(a: ReadonlySet<string>, b: ReadonlySet<string>): boolean {
  if (a.size !== b.size) {
    return false;
  }

  for (const value of a) {
    if (!b.has(value)) {
      return false;
    }
  }

  return true;
}

function validateOptions(
  horizontalRadius: number,
  verticalChunkOffsets: readonly number[],
  cellSize: number
): void {
  if (!Number.isInteger(horizontalRadius) || horizontalRadius < 0) {
    throw new Error("TerrainChunkStreamer horizontalRadius must be a non-negative integer.");
  }

  if (
    verticalChunkOffsets.length === 0 ||
    verticalChunkOffsets.some((offset) => !Number.isInteger(offset))
  ) {
    throw new Error("TerrainChunkStreamer verticalChunkOffsets must contain integer chunk offsets.");
  }

  if (new Set(verticalChunkOffsets).size !== verticalChunkOffsets.length) {
    throw new Error("TerrainChunkStreamer verticalChunkOffsets must not contain duplicates.");
  }

  if (cellSize <= 0) {
    throw new Error("TerrainChunkStreamer cellSize must be positive.");
  }
}

function chunkMayContainSurface(chunk: ReturnType<typeof generateTerrainDensityChunk>): boolean {
  let hasSolid = false;
  let hasAir = false;

  for (const density of chunk.densities) {
    hasSolid ||= density <= 0;
    hasAir ||= density > 0;
    if (hasSolid && hasAir) {
      return true;
    }
  }

  return false;
}
