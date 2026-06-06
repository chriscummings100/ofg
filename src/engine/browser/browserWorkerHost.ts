// Hosts browser Worker pools while keeping job payload semantics opaque.
// Rust-owned systems can choose request ids and worker slots while TypeScript
// only posts payload envelopes and reports completions.

export type BrowserWorkerFactory = () => Worker;

export type BrowserWorkerRequestEnvelope<TPayload = unknown> = {
  readonly type: "request";
  readonly requestId: number;
  readonly payload: TPayload;
};

export type BrowserWorkerCompletionEnvelope<TPayload = unknown> =
  | {
      readonly type: "complete";
      readonly requestId: number;
      readonly payload: TPayload;
    }
  | {
      readonly type: "error";
      readonly requestId: number;
      readonly message: string;
    };

export type BrowserWorkerHostOptions<TCompletionPayload> = {
  readonly workerCount: number;
  readonly workerFactory: BrowserWorkerFactory;
  readonly onCompletion: (
    workerIndex: number,
    completion: BrowserWorkerCompletionEnvelope<TCompletionPayload>
  ) => void;
  readonly onWorkerError: (workerIndex: number, message: string) => void;
};

export class BrowserWorkerHost<TRequestPayload, TCompletionPayload> {
  readonly workerCount: number;
  private readonly workerFactory: BrowserWorkerFactory;
  private readonly onCompletion: (
    workerIndex: number,
    completion: BrowserWorkerCompletionEnvelope<TCompletionPayload>
  ) => void;
  private readonly onWorkerError: (workerIndex: number, message: string) => void;
  private workers: Worker[];

  constructor(options: BrowserWorkerHostOptions<TCompletionPayload>) {
    validateWorkerCount(options.workerCount);
    this.workerCount = options.workerCount;
    this.workerFactory = options.workerFactory;
    this.onCompletion = options.onCompletion;
    this.onWorkerError = options.onWorkerError;
    this.workers = this.createWorkers();
  }

  post(
    workerIndex: number,
    requestId: number,
    payload: TRequestPayload,
    transfer: Transferable[] = []
  ): void {
    const worker = this.workers[workerIndex];
    if (worker === undefined) {
      throw new Error(`Browser worker index ${workerIndex} is outside the worker host.`);
    }

    const envelope: BrowserWorkerRequestEnvelope<TRequestPayload> = {
      type: "request",
      requestId,
      payload
    };

    if (transfer.length > 0) {
      worker.postMessage(envelope, transfer);
    } else {
      worker.postMessage(envelope);
    }
  }

  reset(): void {
    this.terminateWorkers();
    this.workers = this.createWorkers();
  }

  dispose(): void {
    this.terminateWorkers();
    this.workers = [];
  }

  private createWorkers(): Worker[] {
    return Array.from({ length: this.workerCount }, (_unused, workerIndex) => {
      const worker = this.workerFactory();
      worker.addEventListener(
        "message",
        (event: MessageEvent<BrowserWorkerCompletionEnvelope<TCompletionPayload>>) => {
          this.onCompletion(workerIndex, event.data);
        }
      );
      worker.addEventListener("error", (event) => {
        this.onWorkerError(workerIndex, event.message || "Browser worker failed.");
      });

      return worker;
    });
  }

  private terminateWorkers(): void {
    for (const worker of this.workers) {
      worker.terminate();
    }
  }
}

function validateWorkerCount(workerCount: number): void {
  if (!Number.isInteger(workerCount) || workerCount <= 0) {
    throw new Error("BrowserWorkerHost workerCount must be a positive integer.");
  }
}
