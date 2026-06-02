import type { TerrainChunkCoord } from "./terrainChunk.js";
import type { TerrainCoreWasmInstance } from "./terrainCoreWasm.js";

export type TerrainWorkerTaskKind = "density" | "lod";
export type TerrainWorkerTaskCompletion = "stale" | "matched" | "mismatched";

export type TerrainWorkerTaskLease = {
  readonly requestId: number;
  readonly workerIndex: number;
  readonly runtimeGeneration: number;
};

export type TerrainWorkerTaskPool = {
  readonly runtime: "rust" | "typescript";
  readonly workerCount: number;
  readonly inFlightCount: number;
  reset(): void;
  beginTask(
    kind: TerrainWorkerTaskKind,
    lod: number,
    generation: number,
    coord: TerrainChunkCoord
  ): TerrainWorkerTaskLease | undefined;
  finishTask(
    requestId: number,
    kind: TerrainWorkerTaskKind,
    lod: number,
    generation: number,
    coord: TerrainChunkCoord
  ): TerrainWorkerTaskCompletion;
  failTask(requestId: number): boolean;
};

export class TerrainCoreWorkerPool implements TerrainWorkerTaskPool {
  readonly runtime = "rust" as const;

  constructor(
    private readonly terrainCore: TerrainCoreWasmInstance,
    workerCount: number
  ) {
    validateWorkerCount(workerCount);
    const maxWorkers = this.terrainCore.exports.ofg_worker_pool_max_workers();
    if (workerCount > maxWorkers) {
      throw new Error(
        `Terrain worker count ${workerCount} exceeds WASM capacity ${maxWorkers}.`
      );
    }

    const configured = this.terrainCore.exports.ofg_worker_pool_configure(workerCount);
    if (configured !== 1) {
      throw new Error("Rust terrain worker pool rejected its configuration.");
    }
  }

  get workerCount(): number {
    return this.terrainCore.exports.ofg_worker_pool_worker_count();
  }

  get inFlightCount(): number {
    return this.terrainCore.exports.ofg_worker_pool_in_flight_count();
  }

  reset(): void {
    this.terrainCore.exports.ofg_worker_pool_reset();
  }

  beginTask(
    kind: TerrainWorkerTaskKind,
    lod: number,
    generation: number,
    coord: TerrainChunkCoord
  ): TerrainWorkerTaskLease | undefined {
    const accepted = this.terrainCore.exports.ofg_worker_pool_begin_task(
      terrainWorkerTaskKindCode(kind),
      lod,
      generation,
      coord.x,
      coord.y,
      coord.z
    );
    if (accepted !== 1) {
      return undefined;
    }

    return {
      requestId: this.terrainCore.exports.ofg_worker_pool_task_request_id(),
      workerIndex: this.terrainCore.exports.ofg_worker_pool_task_worker_index(),
      runtimeGeneration: this.terrainCore.exports.ofg_worker_pool_task_runtime_generation()
    };
  }

  finishTask(
    requestId: number,
    kind: TerrainWorkerTaskKind,
    lod: number,
    generation: number,
    coord: TerrainChunkCoord
  ): TerrainWorkerTaskCompletion {
    return terrainWorkerTaskCompletionFromCode(
      this.terrainCore.exports.ofg_worker_pool_finish_task(
        requestId,
        terrainWorkerTaskKindCode(kind),
        lod,
        generation,
        coord.x,
        coord.y,
        coord.z
      )
    );
  }

  failTask(requestId: number): boolean {
    return this.terrainCore.exports.ofg_worker_pool_fail_task(requestId) === 1;
  }
}

export function createTerrainCoreWorkerPool(
  terrainCore: TerrainCoreWasmInstance,
  workerCount: number
): TerrainCoreWorkerPool {
  return new TerrainCoreWorkerPool(terrainCore, workerCount);
}

export function terrainWorkerTaskKindCode(kind: TerrainWorkerTaskKind): number {
  switch (kind) {
    case "density":
      return 0;
    case "lod":
      return 1;
  }
}

function terrainWorkerTaskCompletionFromCode(code: number): TerrainWorkerTaskCompletion {
  switch (code) {
    case 0:
      return "stale";
    case 1:
      return "matched";
    case 2:
      return "mismatched";
    default:
      throw new Error(`Unknown Rust terrain worker completion code '${code}'.`);
  }
}

function validateWorkerCount(workerCount: number): void {
  if (!Number.isInteger(workerCount) || workerCount <= 0) {
    throw new Error("Terrain worker count must be a positive integer.");
  }
}
