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
    }
    equal(workers[0].terminated, true);
    equal(workers[1].terminated, true);
    equal(workers[2].terminated, false);
    equal(workers[3].terminated, false);
  });
});

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
    failed: false,
    vertices: new Float32Array([1, 2, 3]),
    indices: new Uint32Array([0])
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
