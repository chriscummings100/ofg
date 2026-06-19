// Narrow TypeScript wrapper around the Rust/WASM browser runtime.

export interface RuntimeDebugStatus {
  readonly initialized: boolean;
  readonly frameCount: number;
  readonly canvasWidth: number;
  readonly canvasHeight: number;
  readonly devicePixelRatio: number;
  readonly surfaceFormat: string;
  readonly adapterName: string;
  readonly backend: string;
  readonly pipelineCreateCount: number;
  readonly bufferCreateCount: number;
  readonly surfaceConfigureCount: number;
  readonly lastError: string | null;
}

export interface BrowserGameRuntime {
  resize(width: number, height: number, devicePixelRatio: number): void;
  frame(timeMs: number): void;
  debugStatus(): RuntimeDebugStatus;
  dispose(): void;
}

export interface RawBrowserGame {
  resize(width: number, height: number, devicePixelRatio: number): void;
  frame(timeMs: number): void;
  debug_status_json(): string;
  dispose(): void;
  free(): void;
}

interface GeneratedWasmModule {
  default(): Promise<unknown>;
  BrowserGame: {
    create(canvas: HTMLCanvasElement): Promise<RawBrowserGame>;
  };
}

type RuntimeDebugStatusRecord = Record<keyof RuntimeDebugStatus, unknown>;

const WASM_MODULE_URL = "/assets/wasm/ofg_web/ofg_web.js";

export async function createBrowserGameRuntime(
  canvas: HTMLCanvasElement
): Promise<BrowserGameRuntime> {
  const wasmModule = (await import(WASM_MODULE_URL)) as GeneratedWasmModule;
  await wasmModule.default();
  const game = await wasmModule.BrowserGame.create(canvas);
  return createBrowserGameRuntimeFromRaw(game);
}

export function createBrowserGameRuntimeFromRaw(
  game: RawBrowserGame
): BrowserGameRuntime {
  return new RustBrowserGameRuntime(game);
}

class RustBrowserGameRuntime implements BrowserGameRuntime {
  readonly #game: RawBrowserGame;
  #disposed = false;

  constructor(game: RawBrowserGame) {
    this.#game = game;
  }

  resize(width: number, height: number, devicePixelRatio: number): void {
    this.#assertLive();
    this.#game.resize(width, height, devicePixelRatio);
  }

  frame(timeMs: number): void {
    this.#assertLive();
    this.#game.frame(timeMs);
  }

  debugStatus(): RuntimeDebugStatus {
    this.#assertLive();
    return parseRuntimeDebugStatus(this.#game.debug_status_json());
  }

  dispose(): void {
    if (this.#disposed) {
      return;
    }
    this.#game.dispose();
    this.#game.free();
    this.#disposed = true;
  }

  #assertLive(): void {
    if (this.#disposed) {
      throw new Error("Browser game runtime has been disposed.");
    }
  }
}

export function parseRuntimeDebugStatus(json: string): RuntimeDebugStatus {
  const value = JSON.parse(json) as unknown;
  if (!isRecord(value)) {
    throw new Error("Runtime debug status must be an object.");
  }

  const record = value as Partial<RuntimeDebugStatusRecord>;
  return {
    initialized: requireBoolean(record, "initialized"),
    frameCount: requireNonNegativeInteger(record, "frameCount"),
    canvasWidth: requireNonNegativeInteger(record, "canvasWidth"),
    canvasHeight: requireNonNegativeInteger(record, "canvasHeight"),
    devicePixelRatio: requireFiniteNumber(record, "devicePixelRatio"),
    surfaceFormat: requireString(record, "surfaceFormat"),
    adapterName: requireString(record, "adapterName"),
    backend: requireString(record, "backend"),
    pipelineCreateCount: requireNonNegativeInteger(record, "pipelineCreateCount"),
    bufferCreateCount: requireNonNegativeInteger(record, "bufferCreateCount"),
    surfaceConfigureCount: requireNonNegativeInteger(record, "surfaceConfigureCount"),
    lastError: requireNullableString(record, "lastError")
  };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function requireBoolean(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): boolean {
  const value = record[key];
  if (typeof value !== "boolean") {
    throw new Error(`Runtime debug status field ${key} must be a boolean.`);
  }
  return value;
}

function requireString(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): string {
  const value = record[key];
  if (typeof value !== "string") {
    throw new Error(`Runtime debug status field ${key} must be a string.`);
  }
  return value;
}

function requireNullableString(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): string | null {
  const value = record[key];
  if (value === null || typeof value === "string") {
    return value;
  }
  throw new Error(`Runtime debug status field ${key} must be a string or null.`);
}

function requireFiniteNumber(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): number {
  const value = record[key];
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(`Runtime debug status field ${key} must be a finite number.`);
  }
  return value;
}

function requireNonNegativeInteger(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): number {
  const value = requireFiniteNumber(record, key);
  if (!Number.isInteger(value) || value < 0) {
    throw new Error(
      `Runtime debug status field ${key} must be a non-negative integer.`
    );
  }
  return value;
}
