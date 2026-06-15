// Browser terrain worker transport. Rust owns terrain scheduling, request ids,
// hierarchy, and completion validation; this client only routes opaque build
// requests to Web Workers and returns their typed-array completions.

import {
  BrowserWorkerHost,
  type BrowserWorkerCompletionEnvelope,
  type BrowserWorkerFactory
} from "../browser/browserWorkerHost.js";

export type TerrainVariantFlatValues = readonly number[];

export type TerrainBuildRequest = {
  readonly requestId: number;
  readonly generation: number;
  readonly lod: number;
  readonly x: number;
  readonly y: number;
  readonly z: number;
  readonly seed: number;
  readonly preset: number;
  readonly variantRevision: number;
  readonly terrainVariant: TerrainVariantFlatValues;
  readonly cellSize: number;
};

export type TerrainBuildCompletion = {
  readonly requestId: number;
  readonly generation: number;
  readonly lod: number;
  readonly x: number;
  readonly y: number;
  readonly z: number;
  readonly variantRevision: number;
  readonly failed: boolean;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly durationMs?: number;
  readonly message?: string;
};

export type TerrainWorkerClientOptions = {
  readonly workerCount?: number;
  readonly workerFactory?: BrowserWorkerFactory;
  readonly hardwareConcurrency?: number;
};

export type TerrainWorkerClientStatus = {
  readonly pendingCompletionCount: number;
  readonly inFlightRequestCount: number;
};

export class TerrainWorkerClient {
  readonly workerCount: number;
  private readonly host: BrowserWorkerHost<TerrainBuildRequest, TerrainBuildCompletion>;
  private readonly requestsById = new Map<number, TerrainBuildRequest>();
  private readonly requestIdsByWorker = new Map<number, Set<number>>();
  private completions: TerrainBuildCompletion[] = [];
  private nextWorkerIndex = 0;

  constructor(options: TerrainWorkerClientOptions = {}) {
    this.workerCount = options.workerCount ?? defaultTerrainWorkerCount(options.hardwareConcurrency);
    const workerFactory = options.workerFactory ?? createTerrainBuildWorker;
    this.host = new BrowserWorkerHost<TerrainBuildRequest, TerrainBuildCompletion>({
      workerCount: this.workerCount,
      workerFactory,
      onCompletion: (workerIndex, completion) => {
        this.recordCompletion(workerIndex, completion);
      },
      onWorkerError: (_workerIndex, message) => {
        this.recordWorkerPoolFailure(message);
      }
    });
  }

  submitRequests(requests: readonly TerrainBuildRequest[]): void {
    for (const request of requests) {
      const workerIndex = this.nextWorkerIndex;
      this.nextWorkerIndex = (this.nextWorkerIndex + 1) % this.workerCount;
      this.requestsById.set(request.requestId, request);
      let workerRequestIds = this.requestIdsByWorker.get(workerIndex);
      if (workerRequestIds === undefined) {
        workerRequestIds = new Set<number>();
        this.requestIdsByWorker.set(workerIndex, workerRequestIds);
      }
      workerRequestIds.add(request.requestId);
      this.host.post(workerIndex, request.requestId, request);
    }
  }

  takeCompletions(maxCount = Number.POSITIVE_INFINITY): TerrainBuildCompletion[] {
    const count = Number.isFinite(maxCount)
      ? Math.max(0, Math.floor(maxCount))
      : this.completions.length;
    if (count >= this.completions.length) {
      const completions = this.completions;
      this.completions = [];
      return completions;
    }

    return this.completions.splice(0, count);
  }

  status(): TerrainWorkerClientStatus {
    return {
      pendingCompletionCount: this.completions.length,
      inFlightRequestCount: this.requestsById.size
    };
  }

  reset(): void {
    this.requestsById.clear();
    this.requestIdsByWorker.clear();
    this.completions = [];
    this.nextWorkerIndex = 0;
    this.host.reset();
  }

  dispose(): void {
    this.host.dispose();
    this.requestsById.clear();
    this.requestIdsByWorker.clear();
    this.completions = [];
  }

  private recordCompletion(
    workerIndex: number,
    completion: BrowserWorkerCompletionEnvelope<TerrainBuildCompletion>
  ): void {
    this.requestIdsByWorker.get(workerIndex)?.delete(completion.requestId);
    const request = this.requestsById.get(completion.requestId);
    this.requestsById.delete(completion.requestId);

    if (completion.type === "complete") {
      this.completions.push(completion.payload);
      return;
    }

    if (request !== undefined) {
      this.completions.push(failedCompletion(request, completion.message));
    }
  }

  private recordWorkerPoolFailure(message: string): void {
    for (const request of this.requestsById.values()) {
      this.completions.push(failedCompletion(request, message));
    }
    this.requestsById.clear();
    this.requestIdsByWorker.clear();
    this.host.reset();
  }
}

export function defaultTerrainWorkerCount(hardwareConcurrency = globalThis.navigator?.hardwareConcurrency): number {
  const reported = Number.isFinite(hardwareConcurrency) ? Math.floor(hardwareConcurrency) : 2;
  return Math.min(12, Math.max(2, reported - 1));
}

function createTerrainBuildWorker(): Worker {
  return new Worker(new URL("./terrainBuildWorker.js", import.meta.url), { type: "module" });
}

function failedCompletion(request: TerrainBuildRequest, message: string): TerrainBuildCompletion {
  return {
    requestId: request.requestId,
    generation: request.generation,
    lod: request.lod,
    x: request.x,
    y: request.y,
    z: request.z,
    variantRevision: request.variantRevision,
    failed: true,
    vertices: new Float32Array(0),
    indices: new Uint32Array(0),
    message
  };
}
