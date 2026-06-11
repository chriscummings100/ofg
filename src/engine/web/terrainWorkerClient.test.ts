import { deepEqual, equal } from "node:assert/strict";
import {
  defaultTerrainWorkerCount,
  TerrainWorkerClient,
  type TerrainBuildCompletion,
  type TerrainBuildRequest
} from "./terrainWorkerClient.js";
import type {
  BrowserWorkerCompletionEnvelope,
  BrowserWorkerRequestEnvelope
} from "../browser/browserWorkerHost.js";

describe("TerrainWorkerClient", () => {
  it("clamps hardware concurrency and leaves room for the browser thread", () => {
    equal(defaultTerrainWorkerCount(1), 2);
    equal(defaultTerrainWorkerCount(4), 3);
    equal(defaultTerrainWorkerCount(64), 12);
  });

  it("posts Rust-issued requests and returns typed-array completions", () => {
    const workers: FakeWorker[] = [];
    const client = new TerrainWorkerClient({
      workerCount: 2,
      workerFactory: () => {
        const worker = new FakeWorker();
        workers.push(worker);
        return worker as unknown as Worker;
      }
    });
    const request = fakeRequest(42);

    client.submitRequests([request]);
    deepEqual(workers[0].messages[0], {
      type: "request",
      requestId: 42,
      payload: request
    });

    const completion = fakeCompletion(request);
    workers[0].emitMessage({
      type: "complete",
      requestId: 42,
      payload: completion
    });

    deepEqual(client.takeCompletions(), [completion]);
    deepEqual(client.takeCompletions(), []);
  });

  it("drains completed terrain builds by caller budget", () => {
    const workers: FakeWorker[] = [];
    const client = new TerrainWorkerClient({
      workerCount: 1,
      workerFactory: () => {
        const worker = new FakeWorker();
        workers.push(worker);
        return worker as unknown as Worker;
      }
    });
    const firstRequest = fakeRequest(1);
    const secondRequest = fakeRequest(2);
    const thirdRequest = fakeRequest(3);
    client.submitRequests([firstRequest, secondRequest, thirdRequest]);
    workers[0].emitMessage({
      type: "complete",
      requestId: 1,
      payload: fakeCompletion(firstRequest)
    });
    workers[0].emitMessage({
      type: "complete",
      requestId: 2,
      payload: fakeCompletion(secondRequest)
    });
    workers[0].emitMessage({
      type: "complete",
      requestId: 3,
      payload: fakeCompletion(thirdRequest)
    });

    equal(client.status().pendingCompletionCount, 3);
    deepEqual(client.takeCompletions(2).map((completion) => completion.requestId), [1, 2]);
    equal(client.status().pendingCompletionCount, 1);
    deepEqual(client.takeCompletions(2).map((completion) => completion.requestId), [3]);
    deepEqual(client.takeCompletions(0), []);
  });

  it("turns worker errors into failed completions so Rust can retry", () => {
    const workers: FakeWorker[] = [];
    const client = new TerrainWorkerClient({
      workerCount: 2,
      workerFactory: () => {
        const worker = new FakeWorker();
        workers.push(worker);
        return worker as unknown as Worker;
      }
    });
    const firstRequest = fakeRequest(7);
    const secondRequest = fakeRequest(8);

    client.submitRequests([firstRequest, secondRequest]);
    workers[0].emitError("boom");
    const completions = client.takeCompletions();

    equal(completions.length, 2);
    deepEqual(completions.map((completion) => completion.requestId), [7, 8]);
    for (const completion of completions) {
      equal(completion.failed, true);
      equal(completion.message, "boom");
      equal(completion.vertices.length, 0);
      equal(completion.indices.length, 0);
      equal(completion.waterTexelCount, 0);
      equal(completion.waterDepths, undefined);
    }
    equal(workers[0].terminated, true);
    equal(workers[1].terminated, true);
    equal(workers[2].terminated, false);
    equal(workers[3].terminated, false);
  });
});

const FAKE_TERRAIN_VARIANT = Object.freeze([
  1, 1, 3, 16, 4, 0.004, 2, 0.5, 3, 3, 0.009, 2.1, 0.48, 1, 1.8, 2,
  0.004, 2, 0.5, 14, 0.018, 1.3, 3, 0.03, 2.05, 0.44, 3.2, 1, 1, 1, 1, 1
]);

function fakeRequest(requestId: number): TerrainBuildRequest {
  return {
    requestId,
    generation: 2,
    lod: 1,
    x: 3,
    y: -1,
    z: 4,
    seed: 0x0F6,
    preset: 1,
    variantRevision: 3,
    terrainVariant: FAKE_TERRAIN_VARIANT,
    cellSize: 2
  };
}

function fakeCompletion(request: TerrainBuildRequest): TerrainBuildCompletion {
  return {
    requestId: request.requestId,
    generation: request.generation,
    lod: request.lod,
    x: request.x,
    y: request.y,
    z: request.z,
    variantRevision: request.variantRevision,
    failed: false,
    vertices: new Float32Array([1, 2, 3]),
    indices: new Uint32Array([0]),
    waterTexelCount: 2,
    waterOriginX: 0,
    waterOriginZ: 32,
    waterWorldSpanX: 32,
    waterWorldSpanZ: 32,
    waterSeaLevelMeters: 0,
    waterMaxDepthMeters: 4,
    waterDepths: new Float32Array([0, 1, 2, 4])
  };
}

class FakeWorker {
  readonly messages: BrowserWorkerRequestEnvelope<unknown>[] = [];
  terminated = false;
  private readonly messageListeners: Array<(
    event: MessageEvent<BrowserWorkerCompletionEnvelope<unknown>>
  ) => void> = [];
  private readonly errorListeners: Array<(event: ErrorEvent) => void> = [];

  addEventListener(
    type: "message" | "error",
    listener: ((event: MessageEvent<BrowserWorkerCompletionEnvelope<unknown>>) => void) |
      ((event: ErrorEvent) => void)
  ): void {
    if (type === "message") {
      this.messageListeners.push(
        listener as (event: MessageEvent<BrowserWorkerCompletionEnvelope<unknown>>) => void
      );
    } else {
      this.errorListeners.push(listener as (event: ErrorEvent) => void);
    }
  }

  postMessage(message: BrowserWorkerRequestEnvelope<unknown>): void {
    this.messages.push(message);
  }

  terminate(): void {
    this.terminated = true;
  }

  emitMessage(message: BrowserWorkerCompletionEnvelope<unknown>): void {
    for (const listener of this.messageListeners) {
      listener({ data: message } as MessageEvent<BrowserWorkerCompletionEnvelope<unknown>>);
    }
  }

  emitError(message: string): void {
    for (const listener of this.errorListeners) {
      listener({ message } as ErrorEvent);
    }
  }
}
