export type BrowserWorkerFactory = () => Worker;

export type BrowserWorkerGroupOptions<TMessage> = {
  readonly workerCount: number;
  readonly workerFactory: BrowserWorkerFactory;
  readonly onMessage: (workerIndex: number, message: TMessage) => void;
  readonly onError: (workerIndex: number, message: string) => void;
};

export class BrowserWorkerGroup<TRequest, TMessage> {
  readonly workerCount: number;
  private readonly workerFactory: BrowserWorkerFactory;
  private readonly onMessage: (workerIndex: number, message: TMessage) => void;
  private readonly onError: (workerIndex: number, message: string) => void;
  private workers: Worker[];

  constructor(options: BrowserWorkerGroupOptions<TMessage>) {
    validateWorkerCount(options.workerCount);
    this.workerCount = options.workerCount;
    this.workerFactory = options.workerFactory;
    this.onMessage = options.onMessage;
    this.onError = options.onError;
    this.workers = this.createWorkers();
  }

  post(workerIndex: number, message: TRequest, transfer: Transferable[] = []): void {
    const worker = this.workers[workerIndex];
    if (worker === undefined) {
      throw new Error(`Browser worker index ${workerIndex} is outside the worker group.`);
    }

    if (transfer.length > 0) {
      worker.postMessage(message, transfer);
    } else {
      worker.postMessage(message);
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
      worker.addEventListener("message", (event: MessageEvent<TMessage>) => {
        this.onMessage(workerIndex, event.data);
      });
      worker.addEventListener("error", (event) => {
        this.onError(workerIndex, event.message || "Browser worker failed.");
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
    throw new Error("BrowserWorkerGroup workerCount must be a positive integer.");
  }
}
