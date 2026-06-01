import type { WorldDescriptor } from "./terrainGenerator.js";
import type {
  TerrainChunkJobGenerator,
  TerrainChunkJobRequest,
  TerrainChunkJobResult,
  TerrainWorkerMessage,
  TerrainWorkerRequestMessage
} from "./terrainChunkWorkerTypes.js";

type PendingRequest = {
  readonly resolve: (result: TerrainChunkJobResult) => void;
  readonly reject: (error: Error) => void;
};

type TerrainWorkerSlot = {
  readonly worker: Worker;
  readonly pending: Map<number, PendingRequest>;
};

export class TerrainChunkWorkerClient implements TerrainChunkJobGenerator {
  readonly workerCount: number;
  private nextRequestId = 1;
  private nextWorkerIndex = 0;
  private slots: TerrainWorkerSlot[];

  constructor(
    private readonly descriptor: WorldDescriptor,
    options: {
      readonly workerCount?: number;
      readonly workerFactory?: () => Worker;
    } = {}
  ) {
    this.workerCount = options.workerCount ?? defaultTerrainWorkerCount();
    this.workerFactory = options.workerFactory ?? createTerrainChunkWorker;
    this.slots = this.createSlots();
  }

  private readonly workerFactory: () => Worker;

  generateChunk(request: TerrainChunkJobRequest): Promise<TerrainChunkJobResult> {
    const slot = this.nextSlot();
    const requestId = this.nextRequestId;
    this.nextRequestId += 1;

    const message: TerrainWorkerRequestMessage = {
      type: "generateChunk",
      requestId,
      request: {
        ...request,
        descriptor: this.descriptor
      }
    };

    return new Promise<TerrainChunkJobResult>((resolve, reject) => {
      slot.pending.set(requestId, { resolve, reject });
      slot.worker.postMessage(message);
    });
  }

  reset(): void {
    this.rejectPending("Terrain worker request was reset.");
    this.terminateSlots();
    this.slots = this.createSlots();
    this.nextWorkerIndex = 0;
  }

  dispose(): void {
    this.rejectPending("Terrain worker was disposed.");
    this.terminateSlots();
  }

  private createSlots(): TerrainWorkerSlot[] {
    return Array.from({ length: this.workerCount }, () => {
      const worker = this.workerFactory();
      const slot: TerrainWorkerSlot = {
        worker,
        pending: new Map()
      };

      worker.addEventListener("message", (event: MessageEvent<TerrainWorkerMessage>) => {
        this.handleMessage(slot, event.data);
      });
      worker.addEventListener("error", (event) => {
        this.rejectSlot(slot, event.message || "Terrain worker failed.");
      });

      return slot;
    });
  }

  private nextSlot(): TerrainWorkerSlot {
    const slot = this.slots[this.nextWorkerIndex % this.slots.length];
    this.nextWorkerIndex = (this.nextWorkerIndex + 1) % this.slots.length;

    return slot;
  }

  private handleMessage(slot: TerrainWorkerSlot, message: TerrainWorkerMessage): void {
    const pending = slot.pending.get(message.requestId);
    if (pending === undefined) {
      return;
    }

    slot.pending.delete(message.requestId);
    if (message.type === "error") {
      pending.reject(new Error(message.message));
      return;
    }

    pending.resolve(message.result);
  }

  private rejectSlot(slot: TerrainWorkerSlot, message: string): void {
    for (const pending of slot.pending.values()) {
      pending.reject(new Error(message));
    }
    slot.pending.clear();
  }

  private rejectPending(message: string): void {
    for (const slot of this.slots) {
      this.rejectSlot(slot, message);
    }
  }

  private terminateSlots(): void {
    for (const slot of this.slots) {
      slot.worker.terminate();
    }
  }
}

export function canUseTerrainChunkWorker(): boolean {
  return typeof Worker !== "undefined";
}

export function createTerrainChunkWorkerClient(
  descriptor: WorldDescriptor
): TerrainChunkWorkerClient | undefined {
  if (!canUseTerrainChunkWorker()) {
    return undefined;
  }

  return new TerrainChunkWorkerClient(descriptor);
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
