import type { TerrainCoreRenderPacketStore } from "../../engine/render/TerrainCoreRenderPackets.js";
import type { ResourceId } from "../../engine/scene/types.js";
import { Component } from "../../engine/scene/Component.js";
import type { Entity } from "../../engine/scene/Entity.js";
import type { Vec3 } from "../../engine/math/vec3.js";
import {
  terrainChunkCoordContainingPosition,
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "../../engine/world/terrainChunk.js";
import type { TerrainDensityChunkStore } from "../../engine/world/terrainCoreDensityChunkStore.js";
import type {
  TerrainStreamJob,
  TerrainStreamScheduler
} from "../../engine/world/terrainCoreStreamScheduler.js";
import {
  type TerrainChunkJobGenerator,
  type TerrainChunkJobStats,
  type TerrainDensityChunkPayload,
  type TerrainDensityJobStats
} from "../../engine/world/terrainChunkWorkerTypes.js";
import {
  prepareTerrainDensityChunkForWorkerTransfer,
  resolveTerrainDensityTransferMode,
  type TerrainDensityTransferMode,
  type TerrainDensityTransferModeRequest
} from "../../engine/world/terrainDensityTransfer.js";
import { POSITION_COLOR_NORMAL_UV_LAYOUT } from "../../engine/world/terrainMesh.js";

export type TerrainCoreWorkerStreamStatus = {
  readonly generation: number;
  readonly pending: boolean;
  readonly loadedChunkCount: number;
  readonly densityReadyChunkCount: number;
  readonly sharedDensityChunkCount: number;
  readonly inFlightDensityCount: number;
  readonly missingDensityCount: number;
  readonly desiredRenderChunkCount: number;
  readonly renderedChunkCount: number;
  readonly emptyChunkCount: number;
  readonly inFlightChunkCount: number;
  readonly missingChunkCount: number;
  readonly maxConcurrentChunkJobs: number;
  readonly densityTransferMode: TerrainDensityTransferMode;
  readonly lastDensityJobStats?: TerrainDensityJobStats;
  readonly lastChunkJobStats?: TerrainChunkJobStats;
};

export type TerrainCoreWorkerStreamerOptions = {
  readonly target?: Entity;
  readonly material?: ResourceId;
  readonly cellSize?: number;
  readonly meshIdPrefix?: string;
  readonly densityTransferMode?: TerrainDensityTransferModeRequest;
};

export class TerrainCoreWorkerStreamer extends Component {
  readonly runtime = "rust" as const;
  target?: Entity;
  material?: ResourceId;
  cellSize: number;
  meshIdPrefix: string;
  densityTransferMode: TerrainDensityTransferMode;

  private lastCenterCoord?: TerrainChunkCoord;
  private lastDensityJobStats?: TerrainDensityJobStats;
  private lastChunkJobStats?: TerrainChunkJobStats;

  constructor(
    private readonly terrain: TerrainCoreRenderPacketStore,
    private readonly streamScheduler: TerrainStreamScheduler,
    private readonly densityChunkStore: TerrainDensityChunkStore,
    private readonly worker: TerrainChunkJobGenerator,
    options: TerrainCoreWorkerStreamerOptions = {}
  ) {
    super();
    this.target = options.target;
    this.material = options.material;
    this.cellSize = options.cellSize ?? 1;
    this.meshIdPrefix = options.meshIdPrefix ?? "mesh:terrain.chunk";
    this.densityTransferMode = resolveTerrainDensityTransferMode(options.densityTransferMode);
    validateCellSize(this.cellSize);
  }

  override update(): void {
    const center = this.target?.transform.getWorldPosition() ?? this.entity?.transform.getWorldPosition();
    if (center !== undefined) {
      this.syncAround(center);
    }
  }

  syncAround(center: Vec3): void {
    validateCellSize(this.cellSize);
    const centerCoord = terrainChunkCoordContainingPosition(center, this.cellSize);
    this.streamScheduler.syncCenter(centerCoord);
    this.lastCenterCoord = centerCoord;
    this.retainDesiredStores();
    this.pumpJobs();
  }

  resetStreaming(center?: Vec3): void {
    this.worker.reset?.();
    this.clearRuntimeState();

    const nextCenter =
      center ??
      this.target?.transform.getWorldPosition() ??
      this.entity?.transform.getWorldPosition();
    if (nextCenter === undefined) {
      return;
    }

    const centerCoord = terrainChunkCoordContainingPosition(nextCenter, this.cellSize);
    this.streamScheduler.reset(centerCoord);
    this.lastCenterCoord = centerCoord;
    this.pumpJobs();
  }

  invalidateAll(): void {
    this.worker.reset?.();
    this.streamScheduler.invalidateAll();
    this.clearRuntimeState();
  }

  getLoadedChunkKeys(): TerrainChunkKey[] {
    return this.streamScheduler
      .desiredDensityCoords()
      .map(terrainChunkKey)
      .sort();
  }

  getStreamStatus(): TerrainCoreWorkerStreamStatus {
    const status = this.streamScheduler.status();
    return {
      generation: status.generation,
      pending: status.inFlightDensityCount > 0 ||
        status.inFlightLodCount > 0 ||
        status.missingDensityCount > 0 ||
        status.missingLod0Count > 0,
      loadedChunkCount: status.desiredDensityCount,
      densityReadyChunkCount: status.densityReadyCount,
      sharedDensityChunkCount: this.densityChunkStore.size(),
      inFlightDensityCount: status.inFlightDensityCount,
      missingDensityCount: status.missingDensityCount,
      desiredRenderChunkCount: status.desiredLod0Count,
      renderedChunkCount: status.lod0ReadyCount,
      emptyChunkCount: status.lod0EmptyCount,
      inFlightChunkCount: status.inFlightLodCount,
      missingChunkCount: status.missingLod0Count,
      maxConcurrentChunkJobs: status.maxInFlightJobs,
      densityTransferMode: this.densityTransferMode,
      lastDensityJobStats: this.lastDensityJobStats,
      lastChunkJobStats: this.lastChunkJobStats
    };
  }

  private clearRuntimeState(): void {
    this.lastDensityJobStats = undefined;
    this.lastChunkJobStats = undefined;
    this.lastCenterCoord = undefined;
    this.densityChunkStore.clear();
    this.terrain.clear();
  }

  private retainDesiredStores(): void {
    this.densityChunkStore.retainOnly(
      this.streamScheduler.desiredDensityCoords(),
      this.cellSize
    );
    this.terrain.retainChunks(this.streamScheduler.desiredLod0Coords());
  }

  private pumpJobs(): void {
    if (this.lastCenterCoord === undefined) {
      return;
    }

    for (const job of this.streamScheduler.tick()) {
      this.submitJob(job);
    }
  }

  private submitJob(job: TerrainStreamJob): void {
    if (job.kind === "density") {
      this.submitDensityJob(job.generation, job.coord);
      return;
    }

    if (job.lod === 0) {
      this.submitLod0Job(job.generation, job.coord);
    }
  }

  private submitDensityJob(generation: number, coord: TerrainChunkCoord): void {
    void this.worker.prepareDensityChunk({
      generation,
      coord,
      cellSize: this.cellSize
    }).then((result) => {
      const key = terrainChunkKey(coord);
      if (result.generation !== generation || result.key !== key) {
        this.streamScheduler.failDensity(generation, coord);
        this.pumpJobs();
        return;
      }

      if (!this.streamScheduler.completeDensity(result.generation, coord)) {
        this.pumpJobs();
        return;
      }

      this.densityChunkStore.store({
        key,
        coord: result.coord,
        densities: result.densities
      }, this.cellSize);
      this.lastDensityJobStats = result.stats;
      this.pumpJobs();
    }).catch((error: unknown) => {
      if (this.streamScheduler.failDensity(generation, coord)) {
        console.warn("Terrain density job failed.", error);
      }
      this.pumpJobs();
    });
  }

  private submitLod0Job(generation: number, coord: TerrainChunkCoord): void {
    const densityChunks = this.densityDependenciesForLod0(coord);
    if (densityChunks === undefined) {
      this.streamScheduler.failLod0(generation, coord);
      this.pumpJobs();
      return;
    }

    void this.worker.generateChunk({
      generation,
      coord,
      densityChunks,
      densityBufferTransfer: this.densityTransferMode === "transfer" ? "move" : "clone",
      cellSize: this.cellSize
    }).then((result) => {
      const key = terrainChunkKey(coord);
      if (result.generation !== generation || result.key !== key) {
        this.streamScheduler.failLod0(generation, coord);
        this.pumpJobs();
        return;
      }

      if (!this.streamScheduler.completeLod0(
        result.generation,
        coord,
        result.indices.length === 0
      )) {
        this.pumpJobs();
        return;
      }

      if (result.indices.length === 0) {
        this.terrain.removeChunk(key);
      } else {
        this.terrain.addChunk({
          key,
          meshId: `${this.meshIdPrefix}:${key}`,
          vertices: result.vertices,
          indices: result.indices,
          layout: POSITION_COLOR_NORMAL_UV_LAYOUT,
          material: this.material
        });
      }
      this.lastChunkJobStats = result.stats;
      this.pumpJobs();
    }).catch((error: unknown) => {
      if (this.streamScheduler.failLod0(generation, coord)) {
        console.warn("Terrain chunk job failed.", error);
      }
      this.pumpJobs();
    });
  }

  private densityDependenciesForLod0(
    coord: TerrainChunkCoord
  ): readonly TerrainDensityChunkPayload[] | undefined {
    const chunks: TerrainDensityChunkPayload[] = [];
    for (const dependency of this.streamScheduler.lod0DependencyCoords(coord)) {
      const chunk = this.densityChunkStore.get(dependency, this.cellSize);
      if (chunk === undefined) {
        return undefined;
      }

      chunks.push(prepareTerrainDensityChunkForWorkerTransfer(
        chunk,
        this.densityTransferMode
      ));
    }

    return chunks;
  }
}

function validateCellSize(cellSize: number): void {
  if (cellSize <= 0) {
    throw new Error("TerrainCoreWorkerStreamer cellSize must be positive.");
  }
}
