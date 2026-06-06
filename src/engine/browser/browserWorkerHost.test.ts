import { deepEqual, equal, throws } from "node:assert/strict";
import {
  BrowserWorkerHost,
  type BrowserWorkerCompletionEnvelope,
  type BrowserWorkerRequestEnvelope
} from "./browserWorkerHost.js";

describe("BrowserWorkerHost", () => {
  it("posts opaque request envelopes and reports worker completions", () => {
    const workers: FakeWorker[] = [];
    const completions: Array<{
      readonly workerIndex: number;
      readonly completion: BrowserWorkerCompletionEnvelope<{ readonly ok: true }>;
    }> = [];
    const host = new BrowserWorkerHost<{ readonly job: string }, { readonly ok: true }>({
      workerCount: 2,
      workerFactory: () => {
        const worker = new FakeWorker();
        workers.push(worker);
        return worker as unknown as Worker;
      },
      onCompletion(workerIndex, completion) {
        completions.push({ workerIndex, completion });
      },
      onWorkerError() {}
    });

    host.post(1, 42, { job: "opaque" });
    deepEqual(workers[1].messages[0], {
      type: "request",
      requestId: 42,
      payload: { job: "opaque" }
    });

    workers[1].emitMessage({
      type: "complete",
      requestId: 42,
      payload: { ok: true }
    });

    equal(completions.length, 1);
    equal(completions[0].workerIndex, 1);
    deepEqual(completions[0].completion, {
      type: "complete",
      requestId: 42,
      payload: { ok: true }
    });
  });

  it("recreates workers on reset and rejects invalid worker slots", () => {
    const workers: FakeWorker[] = [];
    const host = new BrowserWorkerHost<{ readonly job: string }, { readonly ok: true }>({
      workerCount: 1,
      workerFactory: () => {
        const worker = new FakeWorker();
        workers.push(worker);
        return worker as unknown as Worker;
      },
      onCompletion() {},
      onWorkerError() {}
    });

    throws(() => host.post(1, 7, { job: "outside" }), /outside the worker host/);
    host.reset();

    equal(workers.length, 2);
    equal(workers[0].terminated, true);
    equal(workers[1].terminated, false);
  });
});

class FakeWorker {
  readonly messages: BrowserWorkerRequestEnvelope<unknown>[] = [];
  terminated = false;
  private readonly messageListeners: Array<(
    event: MessageEvent<BrowserWorkerCompletionEnvelope<unknown>>
  ) => void> = [];

  addEventListener(
    type: "message" | "error",
    listener: ((event: MessageEvent<BrowserWorkerCompletionEnvelope<unknown>>) => void) |
      ((event: ErrorEvent) => void)
  ): void {
    if (type === "message") {
      this.messageListeners.push(
        listener as (event: MessageEvent<BrowserWorkerCompletionEnvelope<unknown>>) => void
      );
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
}
