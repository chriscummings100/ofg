import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
  instantiateTerrainCoreWasm,
  type TerrainCoreWasmInstance
} from "./terrainCoreWasm.js";
import { TerrainCoreWorkerPool } from "./terrainCoreWorkerPool.js";
import {
  terrainChunkCoord,
  terrainChunkKey,
  type TerrainChunkCoord
} from "./terrainChunk.js";
import {
  TerrainChunkWorkerClient
} from "./terrainChunkWorkerClient.js";
import type {
  TerrainWorkerMessage,
  TerrainWorkerRequestMessage
} from "./terrainChunkWorkerTypes.js";
import { createSeedWorldDescriptor } from "./terrainGenerator.js";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";

describe("TerrainChunkWorkerClient", () => {
  it("uses the Rust worker pool for request ids, worker slots, and reset state", async () => {
    const terrainCore = await loadTerrainCore();
    const workerPool = new TerrainCoreWorkerPool(terrainCore, 2);
    const fakeWorkers: FakeTerrainWorker[] = [];
    const client = new TerrainChunkWorkerClient(createSeedWorldDescriptor(0x0F6), {
      workerPool,
      workerFactory: () => {
        const worker = new FakeTerrainWorker();
        fakeWorkers.push(worker);
        return worker as unknown as Worker;
      }
    });
    const firstCoord = terrainChunkCoord(0, 0, 0);
    const secondCoord = terrainChunkCoord(1, 0, 0);

    const first = client.prepareDensityChunk({
      generation: 11,
      coord: firstCoord,
      cellSize: 1
    });
    const second = client.prepareDensityChunk({
      generation: 11,
      coord: secondCoord,
      cellSize: 1
    });

    equal(client.workerPoolRuntime, "rust");
    equal(workerPool.inFlightCount, 2);
    equal(fakeWorkers.length, 2);
    equal(fakeWorkers[0].messages[0].requestId, 1);
    equal(fakeWorkers[1].messages[0].requestId, 2);
    equal(fakeWorkers[0].messages[0].request.coord, firstCoord);
    equal(fakeWorkers[1].messages[0].request.coord, secondCoord);

    fakeWorkers[0].emitMessage(densityResultMessage(1, 11, firstCoord));
    const firstResult = await first;

    equal(firstResult.key, terrainChunkKey(firstCoord));
    equal(workerPool.inFlightCount, 1);

    const secondRejected = expectRejected(second, /reset/);
    const firstGenerationWorkers = fakeWorkers.slice();
    client.reset();
    await secondRejected;

    equal(workerPool.inFlightCount, 0);
    equal(firstGenerationWorkers.every((worker) => worker.terminated), true);
    equal(fakeWorkers.length, 4);
    equal(fakeWorkers.slice(2).every((worker) => !worker.terminated), true);
  });
});

class FakeTerrainWorker {
  readonly messages: TerrainWorkerRequestMessage[] = [];
  terminated = false;
  private readonly messageListeners: Array<(event: MessageEvent<TerrainWorkerMessage>) => void> = [];
  private readonly errorListeners: Array<(event: ErrorEvent) => void> = [];

  addEventListener(
    type: "message" | "error",
    listener: ((event: MessageEvent<TerrainWorkerMessage>) => void) |
      ((event: ErrorEvent) => void)
  ): void {
    if (type === "message") {
      this.messageListeners.push(listener as (event: MessageEvent<TerrainWorkerMessage>) => void);
    } else {
      this.errorListeners.push(listener as (event: ErrorEvent) => void);
    }
  }

  postMessage(message: TerrainWorkerRequestMessage): void {
    this.messages.push(message);
  }

  terminate(): void {
    this.terminated = true;
  }

  emitMessage(message: TerrainWorkerMessage): void {
    for (const listener of this.messageListeners) {
      listener({ data: message } as MessageEvent<TerrainWorkerMessage>);
    }
  }
}

function densityResultMessage(
  requestId: number,
  generation: number,
  coord: TerrainChunkCoord
): TerrainWorkerMessage {
  return {
    type: "densityResult",
    requestId,
    result: {
      generation,
      key: terrainChunkKey(coord),
      coord,
      densities: new Float32Array([1, 2, 3]),
      stats: { totalMs: 1 }
    }
  };
}

async function loadTerrainCore(): Promise<TerrainCoreWasmInstance> {
  const bytes = readFileSync(TERRAIN_CORE_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;

  return instantiateTerrainCoreWasm(wasmBytes);
}

async function expectRejected(promise: Promise<unknown>, pattern: RegExp): Promise<void> {
  try {
    await promise;
  } catch (error) {
    ok(pattern.test(error instanceof Error ? error.message : String(error)));
    return;
  }

  throw new Error("Expected promise to reject.");
}
