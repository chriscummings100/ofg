import { BrowserWorkerGroup } from "../browser/browserWorkerGroup.js";
import type { WorldDescriptor } from "./terrainGenerator.js";
import type {
  TerrainChunkJobGenerator,
  TerrainChunkJobRequest,
  TerrainChunkJobResult,
  TerrainDensityJobRequest,
  TerrainDensityJobResult,
  TerrainWorkerMessage,
  TerrainWorkerRequestMessage
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
  private readonly workerGroup: BrowserWorkerGroup<TerrainWorkerRequestMessage, TerrainWorkerMessage>;
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
    this.workerGroup = new BrowserWorkerGroup({
      workerCount: this.workerCount,
      workerFactory: this.workerFactory,
      onMessage: (workerIndex, message) => {
        this.handleMessage(this.slots[workerIndex], message);
      },
      onError: (workerIndex, message) => {
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

    const message: TerrainWorkerRequestMessage = {
      type: "prepareDensityChunk",
      requestId: task.requestId,
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
      this.workerGroup.post(task.workerIndex, message);
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

    const message: TerrainWorkerRequestMessage = {
      type: "generateChunk",
      requestId: task.requestId,
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
      this.workerGroup.post(task.workerIndex, message, transfer);
    });
  }

  reset(): void {
    this.rejectPending("Terrain worker request was reset.");
    this.workerPool.reset();
    this.slots = this.createSlots();
    this.workerGroup.reset();
  }

  dispose(): void {
    this.rejectPending("Terrain worker was disposed.");
    this.workerGroup.dispose();
  }

  private createSlots(): TerrainWorkerSlot[] {
    return Array.from({ length: this.workerCount }, () => ({
      pending: new Map()
    }));
  }

  private handleMessage(slot: TerrainWorkerSlot, message: TerrainWorkerMessage): void {
    const pending = slot.pending.get(message.requestId);
    if (pending === undefined) {
      return;
    }

    slot.pending.delete(message.requestId);
    if (message.type === "error") {
      this.workerPool.failTask(message.requestId);
      pending.reject(new Error(message.message));
      return;
    }

    const resultGeneration = message.result.generation;
    const completion = this.workerPool.finishTask(
      message.requestId,
      pending.kind,
      pending.lod,
      resultGeneration,
      message.type === "densityResult" ? message.result.coord : pending.coord
    );
    if (completion !== "matched") {
      pending.reject(new Error(`Rust terrain worker pool reported ${completion} task completion.`));
      return;
    }

    pending.resolve(message.result);
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
