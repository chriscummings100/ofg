import type { TerrainRenderChunkSink } from "../render/terrainRenderChunkSink.js";
import type { Vec3 } from "../math/vec3.js";
import {
  terrainChunkCoordContainingPosition,
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "../world/terrainChunk.js";
import type { TerrainDensityChunkStore } from "../world/terrainCoreDensityChunkStore.js";
import type {
  TerrainStreamJob,
  TerrainStreamScheduler
} from "../world/terrainCoreStreamScheduler.js";
import {
  type TerrainChunkJobGenerator,
  type TerrainChunkJobStats,
  type TerrainDensityJobStats
} from "../world/terrainChunkWorkerTypes.js";

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
  readonly workerPoolRuntime: "rust" | "unknown";
  readonly lastDensityJobStats?: TerrainDensityJobStats;
  readonly lastChunkJobStats?: TerrainChunkJobStats;
};

export type TerrainCoreWorkerStreamerOptions = {
  readonly getTargetPosition?: () => Vec3 | undefined;
  readonly cellSize?: number;
};

export class TerrainCoreWorkerStreamer {
  readonly runtime = "rust" as const;
  enabled = true;
  getTargetPosition?: () => Vec3 | undefined;
  cellSize: number;

  private lastCenterCoord?: TerrainChunkCoord;
  private lastDensityJobStats?: TerrainDensityJobStats;
  private lastChunkJobStats?: TerrainChunkJobStats;

  constructor(
    private readonly terrain: TerrainRenderChunkSink,
    private readonly streamScheduler: TerrainStreamScheduler,
    private readonly densityChunkStore: TerrainDensityChunkStore,
    private readonly worker: TerrainChunkJobGenerator,
    options: TerrainCoreWorkerStreamerOptions = {}
  ) {
    this.getTargetPosition = options.getTargetPosition;
    this.cellSize = options.cellSize ?? 1;
    validateCellSize(this.cellSize);
  }

  update(): void {
    if (!this.enabled) {
      return;
    }

    const center = this.getTargetPosition?.();
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
      this.getTargetPosition?.();
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
      workerPoolRuntime: this.worker.workerPoolRuntime ?? "unknown",
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
      if (
        result.generation !== generation ||
        !terrainChunkCoordsEqual(result.coord, coord)
      ) {
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
    void this.worker.generateChunk({
      generation,
      coord,
      cellSize: this.cellSize
    }).then((result) => {
      const key = terrainChunkKey(coord);
      if (
        result.generation !== generation ||
        !terrainChunkCoordsEqual(result.coord, coord)
      ) {
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
          vertices: result.vertices,
          indices: result.indices
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
}

function validateCellSize(cellSize: number): void {
  if (cellSize <= 0) {
    throw new Error("TerrainCoreWorkerStreamer cellSize must be positive.");
  }
}

function terrainChunkCoordsEqual(
  left: TerrainChunkCoord,
  right: TerrainChunkCoord
): boolean {
  return left.x === right.x && left.y === right.y && left.z === right.z;
}
