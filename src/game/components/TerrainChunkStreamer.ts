import { Mesh } from "../../engine/render/Mesh.js";
import { TerrainRenderer } from "../../engine/render/TerrainRenderer.js";
import { Component } from "../../engine/scene/Component.js";
import type { Entity } from "../../engine/scene/Entity.js";
import type { ResourceId } from "../../engine/scene/types.js";
import type { Vec3 } from "../../engine/math/vec3.js";
import type {
  TerrainChunkJobGenerator,
  TerrainChunkJobStats,
  TerrainDensityJobStats
} from "../../engine/world/terrainChunkWorkerTypes.js";
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
  readonly chunkJobGenerator?: TerrainChunkJobGenerator;
  readonly maxConcurrentChunkJobs?: number;
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
  chunkJobGenerator?: TerrainChunkJobGenerator;
  maxConcurrentChunkJobs: number;

  private readonly loadedChunkKeys = new Set<TerrainChunkKey>();
  private readonly densityReadyChunkKeys = new Set<TerrainChunkKey>();
  private readonly renderChunkKeys = new Set<TerrainChunkKey>();
  private readonly desiredRenderChunkKeys = new Set<TerrainChunkKey>();
  private readonly emptyRenderChunkKeys = new Set<TerrainChunkKey>();
  private readonly inFlightDensityGenerations = new Map<TerrainChunkKey, number>();
  private readonly inFlightChunkGenerations = new Map<TerrainChunkKey, number>();
  private lastCenterCoord?: TerrainChunkCoord;
  private streamGeneration = 0;
  private lastDensityJobStats?: TerrainDensityJobStats;
  private lastChunkJobStats?: TerrainChunkJobStats;

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
    this.chunkJobGenerator = options.chunkJobGenerator;
    this.maxConcurrentChunkJobs = options.maxConcurrentChunkJobs ??
      options.chunkJobGenerator?.workerCount ??
      1;
    validateOptions(
      this.horizontalRadius,
      this.verticalChunkOffsets,
      this.cellSize,
      this.maxConcurrentChunkJobs
    );
  }

  override update(): void {
    const center = this.target?.transform.getWorldPosition() ?? this.entity?.transform.getWorldPosition();
    if (center === undefined) {
      return;
    }

    this.syncAround(center);
  }

  syncAround(center: Vec3): void {
    validateOptions(
      this.horizontalRadius,
      this.verticalChunkOffsets,
      this.cellSize,
      this.maxConcurrentChunkJobs
    );
    const centerCoord = terrainChunkCoordContainingPosition(center, this.cellSize);
    const desiredDensity = this.buildDesiredDensityChunkKeys(centerCoord);
    const desiredRender = this.buildDesiredRenderChunkKeys(centerCoord);
    if (
      setsMatch(this.loadedChunkKeys, desiredDensity) &&
      setsMatch(this.desiredRenderChunkKeys, desiredRender)
    ) {
      this.lastCenterCoord = centerCoord;
      this.pumpChunkJobs();
      return;
    }

    this.loadedChunkKeys.clear();
    this.desiredRenderChunkKeys.clear();

    for (const key of desiredDensity) {
      this.loadedChunkKeys.add(key);
    }
    for (const key of desiredRender) {
      this.desiredRenderChunkKeys.add(key);
    }
    this.removeDensityChunksOutsideDesiredWindow();
    this.removeRenderChunksOutsideDesiredWindow();
    this.removeEmptyChunksOutsideDesiredWindow();
    this.lastCenterCoord = centerCoord;
    this.loadRenderWindow(centerCoord);
  }

  rebuildChunk(chunk: TerrainChunkKey | TerrainChunkCoord): void {
    const coord = typeof chunk === "string" ? parseTerrainChunkKey(chunk) : chunk;
    const centerCoord = this.lastCenterCoord ?? coord;
    this.nextGeneration();
    this.clearRenderedChunks();
    this.loadedChunkKeys.clear();
    this.densityReadyChunkKeys.clear();
    this.desiredRenderChunkKeys.clear();
    this.emptyRenderChunkKeys.clear();
    this.inFlightDensityGenerations.clear();
    this.inFlightChunkGenerations.clear();
    for (const key of this.buildDesiredDensityChunkKeys(centerCoord)) {
      this.loadedChunkKeys.add(key);
    }
    for (const key of this.buildDesiredRenderChunkKeys(centerCoord)) {
      this.desiredRenderChunkKeys.add(key);
    }

    this.loadRenderWindow(centerCoord);
  }

  resetStreaming(center?: Vec3): void {
    this.cancelPendingWork(true);
    this.clearRenderedChunks();
    this.loadedChunkKeys.clear();
    this.densityReadyChunkKeys.clear();
    this.desiredRenderChunkKeys.clear();
    this.emptyRenderChunkKeys.clear();
    this.inFlightDensityGenerations.clear();
    this.inFlightChunkGenerations.clear();

    const nextCenter =
      center ??
      this.target?.transform.getWorldPosition() ??
      this.entity?.transform.getWorldPosition();
    if (nextCenter !== undefined) {
      this.syncAround(nextCenter);
    }
  }

  invalidateAll(): void {
    this.cancelPendingWork(false);
    this.clearRenderedChunks();
    this.loadedChunkKeys.clear();
    this.densityReadyChunkKeys.clear();
    this.desiredRenderChunkKeys.clear();
    this.emptyRenderChunkKeys.clear();
    this.inFlightDensityGenerations.clear();
    this.inFlightChunkGenerations.clear();
  }

  getLoadedChunkKeys(): string[] {
    return [...this.loadedChunkKeys].sort();
  }

  getStreamStatus(): {
    readonly generation: number;
    readonly pending: boolean;
    readonly loadedChunkCount: number;
    readonly densityReadyChunkCount: number;
    readonly inFlightDensityCount: number;
    readonly missingDensityCount: number;
    readonly desiredRenderChunkCount: number;
    readonly renderedChunkCount: number;
    readonly emptyChunkCount: number;
    readonly inFlightChunkCount: number;
    readonly missingChunkCount: number;
    readonly maxConcurrentChunkJobs: number;
    readonly lastDensityJobStats?: TerrainDensityJobStats;
    readonly lastChunkJobStats?: TerrainChunkJobStats;
  } {
    const missingDensityCount = this.countMissingDensityJobs();
    const missingChunkCount = this.countMissingChunkJobs();

    return {
      generation: this.streamGeneration,
      pending: this.chunkJobGenerator !== undefined && (
        this.inFlightDensityGenerations.size > 0 ||
        this.inFlightChunkGenerations.size > 0 ||
        missingDensityCount > 0 ||
        missingChunkCount > 0
      ),
      loadedChunkCount: this.loadedChunkKeys.size,
      densityReadyChunkCount: this.densityReadyChunkKeys.size,
      inFlightDensityCount: this.inFlightDensityGenerations.size,
      missingDensityCount,
      desiredRenderChunkCount: this.desiredRenderChunkKeys.size,
      renderedChunkCount: this.renderChunkKeys.size,
      emptyChunkCount: this.emptyRenderChunkKeys.size,
      inFlightChunkCount: this.inFlightChunkGenerations.size,
      missingChunkCount,
      maxConcurrentChunkJobs: this.maxConcurrentChunkJobs,
      lastDensityJobStats: this.lastDensityJobStats,
      lastChunkJobStats: this.lastChunkJobStats
    };
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

  private buildDesiredRenderChunkKeys(centerCoord: TerrainChunkCoord): Set<TerrainChunkKey> {
    const desired = new Set<TerrainChunkKey>();
    for (const coord of this.buildRenderChunkCoords(centerCoord)) {
      desired.add(terrainChunkKey(coord));
    }

    return desired;
  }

  private loadRenderWindow(centerCoord: TerrainChunkCoord): void {
    if (this.chunkMeshGenerator !== undefined) {
      this.prepareDensityChunks?.(this.loadedDensityChunkCoords(), this.cellSize);
      this.loadRenderWindowFromMeshGenerator(centerCoord, this.chunkMeshGenerator);
      return;
    }

    if (this.chunkJobGenerator !== undefined) {
      this.pumpChunkJobs();
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

  private pumpChunkJobs(): void {
    if (this.chunkJobGenerator === undefined || this.lastCenterCoord === undefined) {
      return;
    }

    while (this.activeWorkerJobCount() < this.maxConcurrentChunkJobs) {
      const densityCoord = this.nextDensityJobCoord(this.lastCenterCoord);
      if (densityCoord !== undefined) {
        this.submitDensityJob(densityCoord);
        continue;
      }

      const coord = this.nextChunkJobCoord(this.lastCenterCoord);
      if (coord === undefined) {
        return;
      }

      this.submitChunkJob(coord);
    }
  }

  private submitDensityJob(coord: TerrainChunkCoord): void {
    if (this.chunkJobGenerator === undefined) {
      return;
    }

    const key = terrainChunkKey(coord);
    const generation = this.streamGeneration;
    this.inFlightDensityGenerations.set(key, generation);
    void this.chunkJobGenerator.prepareDensityChunk({
      generation,
      coord,
      cellSize: this.cellSize
    }).then((result) => {
      this.inFlightDensityGenerations.delete(result.key);
      if (
        result.generation !== this.streamGeneration ||
        !this.loadedChunkKeys.has(result.key)
      ) {
        this.pumpChunkJobs();
        return;
      }

      this.densityReadyChunkKeys.add(result.key);
      this.lastDensityJobStats = result.stats;
      this.pumpChunkJobs();
    }).catch((error: unknown) => {
      this.inFlightDensityGenerations.delete(key);
      if (generation === this.streamGeneration) {
        console.warn("Terrain density job failed.", error);
        this.pumpChunkJobs();
      }
    });
  }

  private submitChunkJob(coord: TerrainChunkCoord): void {
    if (this.chunkJobGenerator === undefined) {
      return;
    }

    const key = terrainChunkKey(coord);
    const generation = this.streamGeneration;
    this.inFlightChunkGenerations.set(key, generation);
    void this.chunkJobGenerator.generateChunk({
      generation,
      coord,
      cellSize: this.cellSize
    }).then((result) => {
      this.inFlightChunkGenerations.delete(result.key);
      if (
        result.generation !== this.streamGeneration ||
        !this.desiredRenderChunkKeys.has(result.key)
      ) {
        this.pumpChunkJobs();
        return;
      }

      this.applyChunkJobResult(result.key, result.vertices, result.indices);
      this.lastChunkJobStats = result.stats;
      this.pumpChunkJobs();
    }).catch((error: unknown) => {
      this.inFlightChunkGenerations.delete(key);
      if (generation === this.streamGeneration) {
        console.warn("Terrain chunk job failed.", error);
        this.emptyRenderChunkKeys.add(key);
        this.pumpChunkJobs();
      }
    });
  }

  private nextDensityJobCoord(centerCoord: TerrainChunkCoord): TerrainChunkCoord | undefined {
    const candidates = [...this.loadedChunkKeys]
      .filter((key) => this.shouldSubmitDensityJob(key))
      .map(parseTerrainChunkKey);
    candidates.sort((a, b) =>
      chunkPriority(a, centerCoord) - chunkPriority(b, centerCoord) ||
      terrainChunkKey(a).localeCompare(terrainChunkKey(b))
    );

    return candidates[0];
  }

  private shouldSubmitDensityJob(key: TerrainChunkKey): boolean {
    return this.loadedChunkKeys.has(key) &&
      !this.densityReadyChunkKeys.has(key) &&
      !this.inFlightDensityGenerations.has(key);
  }

  private nextChunkJobCoord(centerCoord: TerrainChunkCoord): TerrainChunkCoord | undefined {
    const candidates = this.buildRenderChunkCoords(centerCoord)
      .filter((coord) => this.shouldSubmitChunkJob(terrainChunkKey(coord)));
    candidates.sort((a, b) =>
      chunkPriority(a, centerCoord) - chunkPriority(b, centerCoord) ||
      terrainChunkKey(a).localeCompare(terrainChunkKey(b))
    );

    return candidates[0];
  }

  private shouldSubmitChunkJob(key: TerrainChunkKey): boolean {
    return this.desiredRenderChunkKeys.has(key) &&
      !this.renderChunkKeys.has(key) &&
      !this.emptyRenderChunkKeys.has(key) &&
      !this.inFlightChunkGenerations.has(key) &&
      this.meshDensityDependenciesReady(parseTerrainChunkKey(key));
  }

  private meshDensityDependenciesReady(coord: TerrainChunkCoord): boolean {
    return this.buildNeighborChunkCoords(coord)
      .every((densityCoord) => this.densityReadyChunkKeys.has(terrainChunkKey(densityCoord)));
  }

  private applyChunkJobResult(
    key: TerrainChunkKey,
    vertices: Float32Array,
    indices: Uint32Array
  ): void {
    if (indices.length === 0) {
      this.emptyRenderChunkKeys.add(key);
      this.terrain.removeChunk(key);
      this.renderChunkKeys.delete(key);
      return;
    }

    this.emptyRenderChunkKeys.delete(key);
    this.terrain.addChunk({
      key,
      mesh: new Mesh(
        `${this.meshIdPrefix}:${key}`,
        vertices,
        indices,
        POSITION_COLOR_NORMAL_UV_LAYOUT
      ),
      material: this.material
    });
    this.renderChunkKeys.add(key);
  }

  private countMissingChunkJobs(): number {
    let missing = 0;
    for (const key of this.desiredRenderChunkKeys) {
      if (
        !this.renderChunkKeys.has(key) &&
        !this.emptyRenderChunkKeys.has(key) &&
        !this.inFlightChunkGenerations.has(key)
      ) {
        missing += 1;
      }
    }

    return missing;
  }

  private countMissingDensityJobs(): number {
    let missing = 0;
    for (const key of this.loadedChunkKeys) {
      if (
        !this.densityReadyChunkKeys.has(key) &&
        !this.inFlightDensityGenerations.has(key)
      ) {
        missing += 1;
      }
    }

    return missing;
  }

  private activeWorkerJobCount(): number {
    return this.inFlightDensityGenerations.size + this.inFlightChunkGenerations.size;
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

  private nextGeneration(): number {
    this.streamGeneration += 1;

    return this.streamGeneration;
  }

  private cancelPendingWork(resetGenerator: boolean): void {
    this.streamGeneration += 1;
    this.lastDensityJobStats = undefined;
    this.lastChunkJobStats = undefined;
    if (resetGenerator) {
      this.chunkJobGenerator?.reset?.();
    }
  }

  private clearRenderedChunks(): void {
    for (const key of this.renderChunkKeys) {
      this.terrain.removeChunk(key);
    }
    this.renderChunkKeys.clear();
  }

  private removeDensityChunksOutsideDesiredWindow(): void {
    for (const key of [...this.densityReadyChunkKeys]) {
      if (!this.loadedChunkKeys.has(key)) {
        this.densityReadyChunkKeys.delete(key);
      }
    }

    for (const key of [...this.inFlightDensityGenerations.keys()]) {
      if (!this.loadedChunkKeys.has(key)) {
        this.inFlightDensityGenerations.delete(key);
      }
    }
  }

  private removeRenderChunksOutsideDesiredWindow(): void {
    for (const key of [...this.renderChunkKeys]) {
      if (!this.desiredRenderChunkKeys.has(key)) {
        this.terrain.removeChunk(key);
        this.renderChunkKeys.delete(key);
      }
    }
  }

  private removeEmptyChunksOutsideDesiredWindow(): void {
    for (const key of [...this.emptyRenderChunkKeys]) {
      if (!this.desiredRenderChunkKeys.has(key)) {
        this.emptyRenderChunkKeys.delete(key);
      }
    }
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
  cellSize: number,
  maxConcurrentChunkJobs = 1
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

  if (!Number.isInteger(maxConcurrentChunkJobs) || maxConcurrentChunkJobs <= 0) {
    throw new Error("TerrainChunkStreamer maxConcurrentChunkJobs must be a positive integer.");
  }
}

function chunkPriority(coord: TerrainChunkCoord, centerCoord: TerrainChunkCoord): number {
  const dx = coord.x - centerCoord.x;
  const dy = coord.y - centerCoord.y;
  const dz = coord.z - centerCoord.z;

  return dx * dx + dz * dz + Math.abs(dy) * 0.5;
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
