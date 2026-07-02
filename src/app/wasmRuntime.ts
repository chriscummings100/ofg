// Default TypeScript wrapper around the C++/WASM browser runtime.
//
// The TypeScript host owns module loading, canvas lifecycle calls, debug-status
// parsing, and Embind object deletion. C++ owns runtime state, WebGPU setup,
// renderer resources, and draw submission.

export interface RuntimeDebugStatus {
  readonly initialized: boolean;
  readonly lifecycleState: string;
  readonly frameCount: number;
  readonly canvasWidth: number;
  readonly canvasHeight: number;
  readonly devicePixelRatio: number;
  readonly surfaceFormat: string;
  readonly adapterName: string;
  readonly backend: string;
  readonly cameraMode: string;
  readonly pipelineCreateCount: number;
  readonly bufferCreateCount: number;
  readonly surfaceConfigureCount: number;
  readonly lastError: string | null;
}

export interface ControlInput {
  readonly moveX: number;
  readonly moveY: number;
  readonly moveZ: number;
  readonly lookDeltaX: number;
  readonly lookDeltaY: number;
  readonly lookActive: boolean;
  readonly fast: boolean;
  readonly slow: boolean;
  readonly cycleCameraMode: boolean;
}

export interface BrowserGameRuntime {
  // Receives physical canvas size and device-pixel ratio from the host.
  resize(width: number, height: number, devicePixelRatio: number): void;
  // Advances the runtime by one requestAnimationFrame timestamp.
  frame(timeMs: number): void;
  // Forwards one raw control input snapshot.
  setControlInput(input: ControlInput): void;
  // Returns validated debug status for UI, smoke tests, and diagnostics.
  debugStatus(): RuntimeDebugStatus;
  // Releases runtime resources and makes later calls fail clearly.
  dispose(): void;
}

export interface RawBrowserGame {
  // Forwards the physical canvas size to the C++ runtime.
  resize(width: number, height: number, devicePixelRatio: number): void;
  // Advances the C++ runtime by one frame timestamp.
  frame(timeMs: number): void;
  // Forwards raw control input to the C++ runtime.
  set_control_input(
    moveX: number,
    moveY: number,
    moveZ: number,
    lookDeltaX: number,
    lookDeltaY: number,
    lookActive: boolean,
    fast: boolean,
    slow: boolean,
    cycleCameraMode: boolean
  ): void;
  // Returns the raw debug-status JSON string from C++.
  debug_status_json(): string;
  // Releases WebGPU resources owned by the C++ runtime.
  dispose(): void;
  // Releases the Embind wrapper object.
  delete(): void;
}

export interface GeneratedWasmModule {
  BrowserGame: {
    // Creates the Embind BrowserGame facade for a host canvas.
    create(canvas: HTMLCanvasElement): RawBrowserGame | Promise<RawBrowserGame>;
  };
}

interface GeneratedWasmFactory {
  // Instantiates the generated Emscripten module.
  default(options: {
    // Resolves sidecar WASM assets relative to the generated module.
    locateFile(path: string): string;
  }): Promise<GeneratedWasmModule>;
}

type RuntimeDebugStatusRecord = Record<keyof RuntimeDebugStatus, unknown>;

const WASM_MODULE_URL = "/assets/wasm/ofg_cpp/ofg_cpp.js";

// Loads the generated C++/WASM module and creates an app-facing runtime.
export async function createBrowserGameRuntime(
  canvas: HTMLCanvasElement
): Promise<BrowserGameRuntime> {
  const wasmFactory = (await import(WASM_MODULE_URL)) as GeneratedWasmFactory;
  const module = await wasmFactory.default({
    locateFile(path: string) {
      return `/assets/wasm/ofg_cpp/${path}`;
    }
  });
  return createBrowserGameRuntimeFromModule(module, canvas);
}

// Creates an app-facing runtime from an already-instantiated C++ module.
export async function createBrowserGameRuntimeFromModule(
  module: GeneratedWasmModule,
  canvas: HTMLCanvasElement
): Promise<BrowserGameRuntime> {
  const raw = await Promise.resolve(module.BrowserGame.create(canvas));
  return createBrowserGameRuntimeFromRaw(raw);
}

// Wraps a raw Embind object behind the BrowserGameRuntime interface.
export function createBrowserGameRuntimeFromRaw(game: RawBrowserGame): BrowserGameRuntime {
  return new CppBrowserGameRuntime(game);
}

// Owns the raw Embind BrowserGame object and enforces dispose-before-use errors.
class CppBrowserGameRuntime implements BrowserGameRuntime {
  readonly #game: RawBrowserGame;
  #disposed = false;

  // Stores the raw Embind object for lifecycle delegation.
  constructor(game: RawBrowserGame) {
    this.#game = game;
  }

  // Forwards resize only while the wrapper is live.
  resize(width: number, height: number, devicePixelRatio: number): void {
    this.#assertLive();
    this.#game.resize(width, height, devicePixelRatio);
  }

  // Forwards frame only while the wrapper is live.
  frame(timeMs: number): void {
    this.#assertLive();
    this.#game.frame(timeMs);
  }

  // Validates and forwards raw control input only while the wrapper is live.
  setControlInput(input: ControlInput): void {
    this.#assertLive();
    validateControlInput(input);
    this.#game.set_control_input(
      input.moveX,
      input.moveY,
      input.moveZ,
      input.lookDeltaX,
      input.lookDeltaY,
      input.lookActive,
      input.fast,
      input.slow,
      input.cycleCameraMode
    );
  }

  // Parses the C++ debug-status JSON through the shared validator.
  debugStatus(): RuntimeDebugStatus {
    this.#assertLive();
    return parseRuntimeDebugStatus(this.#game.debug_status_json());
  }

  // Disposes the C++ runtime once and releases the Embind wrapper.
  dispose(): void {
    if (this.#disposed) {
      return;
    }
    this.#game.dispose();
    this.#game.delete();
    this.#disposed = true;
  }

  // Throws the stable disposed-runtime error used by tests and callers.
  #assertLive(): void {
    if (this.#disposed) {
      throw new Error("Browser game runtime has been disposed.");
    }
  }
}

// Validates finite scalar input before crossing the Embind boundary.
function validateControlInput(input: ControlInput): void {
  requireFiniteControlNumber(input.moveX, "moveX");
  requireFiniteControlNumber(input.moveY, "moveY");
  requireFiniteControlNumber(input.moveZ, "moveZ");
  requireFiniteControlNumber(input.lookDeltaX, "lookDeltaX");
  requireFiniteControlNumber(input.lookDeltaY, "lookDeltaY");
}

// Requires a control input number to be finite.
function requireFiniteControlNumber(value: number, key: keyof ControlInput): void {
  if (!Number.isFinite(value)) {
    throw new Error(`Control input field ${key} must be a finite number.`);
  }
}

// Parses and validates the runtime debug-status JSON payload.
export function parseRuntimeDebugStatus(json: string): RuntimeDebugStatus {
  const value = JSON.parse(json) as unknown;
  if (!isRecord(value)) {
    throw new Error("Runtime debug status must be an object.");
  }

  const record = value as Partial<RuntimeDebugStatusRecord>;
  return {
    initialized: requireBoolean(record, "initialized"),
    lifecycleState: requireString(record, "lifecycleState"),
    frameCount: requireNonNegativeInteger(record, "frameCount"),
    canvasWidth: requireNonNegativeInteger(record, "canvasWidth"),
    canvasHeight: requireNonNegativeInteger(record, "canvasHeight"),
    devicePixelRatio: requireFiniteNumber(record, "devicePixelRatio"),
    surfaceFormat: requireString(record, "surfaceFormat"),
    adapterName: requireString(record, "adapterName"),
    backend: requireString(record, "backend"),
    cameraMode: requireString(record, "cameraMode"),
    pipelineCreateCount: requireNonNegativeInteger(record, "pipelineCreateCount"),
    bufferCreateCount: requireNonNegativeInteger(record, "bufferCreateCount"),
    surfaceConfigureCount: requireNonNegativeInteger(record, "surfaceConfigureCount"),
    lastError: requireNullableString(record, "lastError")
  };
}

// Reports whether a parsed JSON value is an object record.
function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

// Requires a debug-status field to be boolean.
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

// Requires a debug-status field to be a string.
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

// Requires a debug-status field to be a string or null.
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

// Requires a debug-status field to be a finite number.
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

// Requires a debug-status field to be a non-negative integer.
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
