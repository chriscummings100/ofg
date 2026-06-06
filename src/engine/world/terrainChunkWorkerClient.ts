import {
  BrowserWorkerHost,
  type BrowserWorkerCompletionEnvelope
} from "../browser/browserWorkerHost.js";
import type { WorldDescriptor } from "./terrainDescriptor.js";
import type {
  TerrainChunkJobGenerator,
  TerrainChunkJobRequest,
  TerrainChunkJobResult,
  TerrainDensityJobRequest,
  TerrainDensityJobResult,
  TerrainWorkerRequestPayload,
  TerrainWorkerResultPayload
} from "./terrainChunkWorkerTypes.js";
import {
  createTerrainCoreWorkerPool,
  type TerrainWorkerTaskKind,
  type TerrainWorkerTaskPool
} from "./terrainCoreWorkerPool.js";
import type { TerrainCoreWasmInstance } from "./terrainCoreWasm.js";
import { terrainDensityChunkTransferList } from "./terrainDensityTransfer.js";

type PendingRequest = {
  readonly kind: TerrainWorkerTaskKind;
  readonly lod: number;
  readonly generation: number;
  readonly coord: TerrainChunkJobRequest["coord"];
  readonly resolve: (result: unknown) => void;
  readonly reject: (error: Error) => void;
};

type TerrainWorkerSlot = {
  readonly pending: Map<number, PendingRequest>;
};

export class TerrainChunkWorkerClient implements TerrainChunkJobGenerator {
  readonly workerCount: number;
  readonly workerPoolRuntime: "rust" | "typescript";
  private readonly workerHost: BrowserWorkerHost<
    TerrainWorkerRequestPayload,
    TerrainWorkerResultPayload
  >;
  private slots: TerrainWorkerSlot[];

  constructor(
    private readonly descriptor: WorldDescriptor,
    options: {
      readonly workerCount?: number;
      readonly workerPool?: TerrainWorkerTaskPool;
      readonly workerFactory?: () => Worker;
    } = {}
  ) {
    this.workerPool = options.workerPool ??
      new TypeScriptTerrainWorkerPool(options.workerCount ?? defaultTerrainWorkerCount());
    this.workerCount = this.workerPool.workerCount;
    this.workerPoolRuntime = this.workerPool.runtime;
    this.workerFactory = options.workerFactory ?? createTerrainChunkWorker;
    this.slots = this.createSlots();
    this.workerHost = new BrowserWorkerHost({
      workerCount: this.workerCount,
      workerFactory: this.workerFactory,
      onCompletion: (workerIndex, completion) => {
        this.handleCompletion(this.slots[workerIndex], completion);
      },
      onWorkerError: (workerIndex, message) => {
        this.rejectSlot(this.slots[workerIndex], message);
      }
    });
  }

  private readonly workerPool: TerrainWorkerTaskPool;
  private readonly workerFactory: () => Worker;

  prepareDensityChunk(request: TerrainDensityJobRequest): Promise<TerrainDensityJobResult> {
    const task = this.workerPool.beginTask(
      "density",
      0,
      request.generation,
      request.coord
    );
    if (task === undefined) {
      return Promise.reject(new Error("Rust terrain worker pool has no idle worker."));
    }
    const slot = this.slots[task.workerIndex];

    const payload: TerrainWorkerRequestPayload = {
      type: "prepareDensityChunk",
      request: {
        ...request,
        descriptor: this.descriptor
      }
    };

    return new Promise<TerrainDensityJobResult>((resolve, reject) => {
      slot.pending.set(task.requestId, {
        kind: "density",
        lod: 0,
        generation: request.generation,
        coord: request.coord,
        resolve: (result) => resolve(result as TerrainDensityJobResult),
        reject
      });
      this.workerHost.post(task.workerIndex, task.requestId, payload);
    });
  }

  generateChunk(request: TerrainChunkJobRequest): Promise<TerrainChunkJobResult> {
    const task = this.workerPool.beginTask(
      "lod",
      0,
      request.generation,
      request.coord
    );
    if (task === undefined) {
      return Promise.reject(new Error("Rust terrain worker pool has no idle worker."));
    }
    const slot = this.slots[task.workerIndex];

    const payload: TerrainWorkerRequestPayload = {
      type: "generateChunk",
      request: {
        ...request,
        descriptor: this.descriptor
      }
    };

    return new Promise<TerrainChunkJobResult>((resolve, reject) => {
      slot.pending.set(task.requestId, {
        kind: "lod",
        lod: 0,
        generation: request.generation,
        coord: request.coord,
        resolve: (result) => resolve(result as TerrainChunkJobResult),
        reject
      });
      const transfer = request.densityBufferTransfer === "move"
        ? terrainDensityChunkTransferList(request.densityChunks)
        : [];
      this.workerHost.post(task.workerIndex, task.requestId, payload, transfer);
    });
  }

  reset(): void {
    this.rejectPending("Terrain worker request was reset.");
    this.workerPool.reset();
    this.slots = this.createSlots();
    this.workerHost.reset();
  }

  dispose(): void {
    this.rejectPending("Terrain worker was disposed.");
    this.workerHost.dispose();
  }

  private createSlots(): TerrainWorkerSlot[] {
    return Array.from({ length: this.workerCount }, () => ({
      pending: new Map()
    }));
  }

  private handleCompletion(
    slot: TerrainWorkerSlot,
    completion: BrowserWorkerCompletionEnvelope<TerrainWorkerResultPayload>
  ): void {
    const pending = slot.pending.get(completion.requestId);
    if (pending === undefined) {
      return;
    }

    slot.pending.delete(completion.requestId);
    if (completion.type === "error") {
      this.workerPool.failTask(completion.requestId);
      pending.reject(new Error(completion.message));
      return;
    }

    const payload = completion.payload;
    const result = payload.result;
    const resultGeneration = result.generation;
    const resultCoord = payload.type === "densityResult"
      ? payload.result.coord
      : pending.coord;
    const taskCompletion = this.workerPool.finishTask(
      completion.requestId,
      pending.kind,
      pending.lod,
      resultGeneration,
      resultCoord
    );
    if (taskCompletion !== "matched") {
      pending.reject(new Error(`Rust terrain worker pool reported ${taskCompletion} task completion.`));
      return;
    }

    pending.resolve(result);
  }

  private rejectSlot(slot: TerrainWorkerSlot, message: string): void {
    for (const [requestId, pending] of slot.pending.entries()) {
      this.workerPool.failTask(requestId);
      pending.reject(new Error(message));
    }
    slot.pending.clear();
  }

  private rejectPending(message: string): void {
    for (const slot of this.slots) {
      this.rejectSlot(slot, message);
    }
  }
}

export function canUseTerrainChunkWorker(): boolean {
  return typeof Worker !== "undefined";
}

export function createTerrainChunkWorkerClient(
  descriptor: WorldDescriptor,
  terrainCore?: TerrainCoreWasmInstance
): TerrainChunkWorkerClient | undefined {
  if (!canUseTerrainChunkWorker()) {
    return undefined;
  }

  const workerCount = defaultTerrainWorkerCount();
  const workerPool = terrainCore === undefined
    ? undefined
    : createTerrainCoreWorkerPool(terrainCore, workerCount);

  return new TerrainChunkWorkerClient(descriptor, {
    workerCount,
    workerPool
  });
}

function createTerrainChunkWorker(): Worker {
  return new Worker(new URL("./terrainChunkWorker.js", import.meta.url), {
    type: "module",
    name: "ofg-terrain"
  });
}

function defaultTerrainWorkerCount(): number {
  const hardwareConcurrency = globalThis.navigator?.hardwareConcurrency ?? 2;

  return Math.max(1, Math.min(6, hardwareConcurrency - 1));
}

class TypeScriptTerrainWorkerPool implements TerrainWorkerTaskPool {
  readonly runtime = "typescript" as const;
  private nextRequestId = 1;
  private nextWorkerIndex = 0;

  constructor(readonly workerCount: number) {}

  get inFlightCount(): number {
    return 0;
  }

  reset(): void {
    this.nextRequestId = 1;
    this.nextWorkerIndex = 0;
  }

  beginTask(): { readonly requestId: number; readonly workerIndex: number; readonly runtimeGeneration: number } {
    const requestId = this.nextRequestId;
    this.nextRequestId += 1;
    const workerIndex = this.nextWorkerIndex % this.workerCount;
    this.nextWorkerIndex = (this.nextWorkerIndex + 1) % this.workerCount;

    return {
      requestId,
      workerIndex,
      runtimeGeneration: 0
    };
  }

  finishTask(): "matched" {
    return "matched";
  }

  failTask(): boolean {
    return true;
  }
}
