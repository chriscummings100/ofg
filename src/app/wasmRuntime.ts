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
  readonly modelLoadingState: string;
  readonly playerModelLoaded: boolean;
  readonly demoScene: RuntimeDemoSceneStatus;
  readonly renderCulling: RuntimeRenderCullingStatus;
  readonly shadow: RuntimeShadowStatus;
  readonly pipelineCreateCount: number;
  readonly bufferCreateCount: number;
  readonly surfaceConfigureCount: number;
  readonly bloomActiveLevelCount: number;
  readonly bloomEncodedPassCount: number;
  readonly bloomDrawCount: number;
  readonly bloomEstimatedReadBytes: number;
  readonly bloomEstimatedWriteBytes: number;
  readonly bloomSkipped: boolean;
  readonly tempBufferActiveBytes: number;
  readonly tempBufferReusableBytes: number;
  readonly tempBufferPeakBytes: number;
  readonly tempBufferCreatedCount: number;
  readonly tempBufferReusedCount: number;
  readonly tempBufferDiscardedCount: number;
  readonly tempBufferActiveCount: number;
  readonly tempBufferReusableCount: number;
  readonly tempBufferEarlyReleaseCount: number;
  readonly tempBufferEndFrameReturnCount: number;
  readonly lastError: string | null;
}

export interface RuntimeDemoSceneStatus {
  readonly name: string;
  readonly boxCount: number;
  readonly nearBoxCount: number;
  readonly midBoxCount: number;
  readonly farBoxCount: number;
  readonly partlyBelowGroundCount: number;
  readonly overlapClusterBoxCount: number;
  readonly offCameraCandidateCount: number;
}

export interface RuntimeRenderCullingStatus {
  readonly extractedObjectCount: number;
  readonly cameraVisibleObjectCount: number;
  readonly cameraCulledObjectCount: number;
}

export interface RuntimeShadowCascadeStatus {
  readonly index: number;
  readonly testedCasterCount: number;
  readonly acceptedCasterCount: number;
  readonly rejectedCasterCount: number;
  readonly drawCount: number;
  readonly submeshCount: number;
  readonly indexCount: number;
}

export interface RuntimeShadowStatus {
  readonly enabled: boolean;
  readonly cascadeCount: number;
  readonly encodedPassCount: number;
  readonly mapSize: number;
  readonly estimatedDepthBytes: number;
  readonly pcfMode: string;
  readonly pcfSampleCount: number;
  readonly sunElevationRadians: number;
  readonly effectiveIntensity: number;
  readonly lowSunClamped: boolean;
  readonly cascades: readonly RuntimeShadowCascadeStatus[];
  readonly totalTestedCasterCount: number;
  readonly totalAcceptedCasterCount: number;
  readonly totalRejectedCasterCount: number;
  readonly totalDrawCount: number;
  readonly totalSubmeshCount: number;
  readonly totalIndexCount: number;
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
  readonly toggleShadowDebugOverlay: boolean;
  readonly toggleOverheadSun: boolean;
}

export interface BlobLoadRequest {
  readonly id: number;
  readonly uri: string;
}

export type BlobFetch = (uri: string) => Promise<Uint8Array>;

export interface BrowserGameRuntime {
  // Receives physical canvas size and device-pixel ratio from the host.
  resize(width: number, height: number, devicePixelRatio: number): void;
  // Advances the runtime by one requestAnimationFrame timestamp.
  frame(timeMs: number): void;
  // Forwards one raw control input snapshot.
  setControlInput(input: ControlInput): void;
  // Returns queued generic blob-load requests from the C++ runtime.
  blobLoads(): readonly BlobLoadRequest[];
  // Marks a generic blob request as actively loading in the browser host.
  markBlobLoading(id: number): void;
  // Completes a generic blob request with fetched bytes.
  completeBlobLoad(id: number, bytes: Uint8Array): void;
  // Fails a generic blob request with a browser-side diagnostic.
  failBlobLoad(id: number, message: string): void;
  // Starts browser fetches for queued generic blob requests.
  pumpBlobLoads(fetchBlob?: BlobFetch): void;
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
    cycleCameraMode: boolean,
    toggleShadowDebugOverlay: boolean,
    toggleOverheadSun: boolean
  ): void;
  // Returns generic blob-load requests as a JSON array.
  blob_loads_json(): string;
  // Marks a generic blob-load request as in-flight.
  mark_blob_loading(id: number): void;
  // Completes a generic blob-load request with fetched bytes.
  complete_blob_load(id: number, bytes: Uint8Array): void;
  // Fails a generic blob-load request with a browser-side diagnostic.
  fail_blob_load(id: number, message: string): void;
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

type RuntimeDemoSceneStatusRecord = Record<keyof RuntimeDemoSceneStatus, unknown>;

type RuntimeRenderCullingStatusRecord = Record<keyof RuntimeRenderCullingStatus, unknown>;

type RuntimeShadowCascadeStatusRecord = Record<keyof RuntimeShadowCascadeStatus, unknown>;

type RuntimeShadowStatusRecord = Record<keyof RuntimeShadowStatus, unknown>;

type BlobLoadRequestRecord = Record<keyof BlobLoadRequest, unknown>;

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
  readonly #inFlightBlobLoads = new Set<number>();
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
      input.cycleCameraMode,
      input.toggleShadowDebugOverlay,
      input.toggleOverheadSun
    );
  }

  // Parses queued generic blob requests from the C++ runtime.
  blobLoads(): readonly BlobLoadRequest[] {
    this.#assertLive();
    return parseBlobLoadRequests(this.#game.blob_loads_json());
  }

  // Marks a generic blob request as actively loading.
  markBlobLoading(id: number): void {
    this.#assertLive();
    validateBlobLoadId(id);
    this.#game.mark_blob_loading(id);
  }

  // Completes a generic blob request with fetched bytes.
  completeBlobLoad(id: number, bytes: Uint8Array): void {
    this.#assertLive();
    validateBlobLoadId(id);
    this.#game.complete_blob_load(id, bytes);
  }

  // Fails a generic blob request with a diagnostic message.
  failBlobLoad(id: number, message: string): void {
    this.#assertLive();
    validateBlobLoadId(id);
    this.#game.fail_blob_load(id, message);
  }

  // Starts one browser fetch per queued generic blob id.
  pumpBlobLoads(fetchBlob: BlobFetch = fetchBlobLoadBytes): void {
    this.#assertLive();
    for (const request of this.blobLoads()) {
      if (this.#inFlightBlobLoads.has(request.id)) {
        continue;
      }
      this.#inFlightBlobLoads.add(request.id);
      try {
        this.markBlobLoading(request.id);
      } catch (error) {
        this.#inFlightBlobLoads.delete(request.id);
        throw error;
      }

      void fetchBlob(request.uri)
        .then((bytes) => {
          if (!this.#disposed) {
            this.completeBlobLoad(request.id, bytes);
          }
        })
        .catch((error: unknown) => {
          if (!this.#disposed) {
            this.failBlobLoad(
              request.id,
              error instanceof Error ? error.message : String(error)
            );
          }
        })
        .finally(() => {
          this.#inFlightBlobLoads.delete(request.id);
        });
    }
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
    this.#inFlightBlobLoads.clear();
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
  requireBooleanControlField(input.lookActive, "lookActive");
  requireBooleanControlField(input.fast, "fast");
  requireBooleanControlField(input.slow, "slow");
  requireBooleanControlField(input.cycleCameraMode, "cycleCameraMode");
  requireBooleanControlField(input.toggleShadowDebugOverlay, "toggleShadowDebugOverlay");
  requireBooleanControlField(input.toggleOverheadSun, "toggleOverheadSun");
}

// Requires a control input number to be finite.
function requireFiniteControlNumber(value: number, key: keyof ControlInput): void {
  if (!Number.isFinite(value)) {
    throw new Error(`Control input field ${key} must be a finite number.`);
  }
}

// Requires a control input boolean before crossing the Embind boundary.
function requireBooleanControlField(value: boolean, key: keyof ControlInput): void {
  if (typeof value !== "boolean") {
    throw new Error(`Control input field ${key} must be a boolean.`);
  }
}

// Validates blob ids before crossing the Embind boundary.
function validateBlobLoadId(id: number): void {
  if (!Number.isSafeInteger(id) || id <= 0) {
    throw new Error("Blob load id must be a positive safe integer.");
  }
}

// Fetches one C++-requested blob URI from packaged site assets.
async function fetchBlobLoadBytes(uri: string): Promise<Uint8Array> {
  const url = uri.startsWith("/") ? uri : `/${uri}`;
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to fetch ${url}: ${response.status} ${response.statusText}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

// Parses and validates the generic blob-load request JSON payload.
export function parseBlobLoadRequests(json: string): readonly BlobLoadRequest[] {
  const value = JSON.parse(json) as unknown;
  if (!Array.isArray(value)) {
    throw new Error("Blob load requests must be an array.");
  }
  return value.map((entry, index) => parseBlobLoadRequest(entry, index));
}

// Parses one blob-load request record with precise field errors.
function parseBlobLoadRequest(value: unknown, index: number): BlobLoadRequest {
  if (!isRecord(value)) {
    throw new Error(`Blob load request ${index} must be an object.`);
  }

  const record = value as Partial<BlobLoadRequestRecord>;
  const id = record.id;
  if (typeof id !== "number" || !Number.isSafeInteger(id) || id <= 0) {
    throw new Error(`Blob load request ${index} field id must be a positive safe integer.`);
  }
  const uri = record.uri;
  if (typeof uri !== "string" || uri.length === 0) {
    throw new Error(`Blob load request ${index} field uri must be a non-empty string.`);
  }
  return { id, uri };
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
    modelLoadingState: requireString(record, "modelLoadingState"),
    playerModelLoaded: requireBoolean(record, "playerModelLoaded"),
    demoScene: requireDemoSceneStatus(record, "demoScene"),
    renderCulling: requireRenderCullingStatus(record, "renderCulling"),
    shadow: requireShadowStatus(record, "shadow"),
    pipelineCreateCount: requireNonNegativeInteger(record, "pipelineCreateCount"),
    bufferCreateCount: requireNonNegativeInteger(record, "bufferCreateCount"),
    surfaceConfigureCount: requireNonNegativeInteger(record, "surfaceConfigureCount"),
    bloomActiveLevelCount: requireNonNegativeInteger(record, "bloomActiveLevelCount"),
    bloomEncodedPassCount: requireNonNegativeInteger(record, "bloomEncodedPassCount"),
    bloomDrawCount: requireNonNegativeInteger(record, "bloomDrawCount"),
    bloomEstimatedReadBytes: requireNonNegativeInteger(record, "bloomEstimatedReadBytes"),
    bloomEstimatedWriteBytes: requireNonNegativeInteger(record, "bloomEstimatedWriteBytes"),
    bloomSkipped: requireBoolean(record, "bloomSkipped"),
    tempBufferActiveBytes: requireNonNegativeInteger(record, "tempBufferActiveBytes"),
    tempBufferReusableBytes: requireNonNegativeInteger(record, "tempBufferReusableBytes"),
    tempBufferPeakBytes: requireNonNegativeInteger(record, "tempBufferPeakBytes"),
    tempBufferCreatedCount: requireNonNegativeInteger(record, "tempBufferCreatedCount"),
    tempBufferReusedCount: requireNonNegativeInteger(record, "tempBufferReusedCount"),
    tempBufferDiscardedCount: requireNonNegativeInteger(record, "tempBufferDiscardedCount"),
    tempBufferActiveCount: requireNonNegativeInteger(record, "tempBufferActiveCount"),
    tempBufferReusableCount: requireNonNegativeInteger(record, "tempBufferReusableCount"),
    tempBufferEarlyReleaseCount: requireNonNegativeInteger(record, "tempBufferEarlyReleaseCount"),
    tempBufferEndFrameReturnCount: requireNonNegativeInteger(record, "tempBufferEndFrameReturnCount"),
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

// Requires the nested demo-scene diagnostics object.
function requireDemoSceneStatus(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): RuntimeDemoSceneStatus {
  const value = record[key];
  if (!isRecord(value)) {
    throw new Error(`Runtime debug status field ${key} must be an object.`);
  }
  const demoRecord = value as Partial<RuntimeDemoSceneStatusRecord>;
  return {
    name: requireDemoSceneString(demoRecord, "name"),
    boxCount: requireDemoSceneNonNegativeInteger(demoRecord, "boxCount"),
    nearBoxCount: requireDemoSceneNonNegativeInteger(demoRecord, "nearBoxCount"),
    midBoxCount: requireDemoSceneNonNegativeInteger(demoRecord, "midBoxCount"),
    farBoxCount: requireDemoSceneNonNegativeInteger(demoRecord, "farBoxCount"),
    partlyBelowGroundCount: requireDemoSceneNonNegativeInteger(
      demoRecord,
      "partlyBelowGroundCount"
    ),
    overlapClusterBoxCount: requireDemoSceneNonNegativeInteger(
      demoRecord,
      "overlapClusterBoxCount"
    ),
    offCameraCandidateCount: requireDemoSceneNonNegativeInteger(
      demoRecord,
      "offCameraCandidateCount"
    )
  };
}

// Requires one demo-scene diagnostic string.
function requireDemoSceneString(
  record: Partial<RuntimeDemoSceneStatusRecord>,
  key: keyof RuntimeDemoSceneStatus
): string {
  const value = record[key];
  if (typeof value !== "string") {
    throw new Error(`Runtime debug status field demoScene.${key} must be a string.`);
  }
  return value;
}

// Requires one demo-scene diagnostic count.
function requireDemoSceneNonNegativeInteger(
  record: Partial<RuntimeDemoSceneStatusRecord>,
  key: keyof RuntimeDemoSceneStatus
): number {
  const value = record[key];
  if (typeof value !== "number" || !Number.isFinite(value) || !Number.isInteger(value) || value < 0) {
    throw new Error(
      `Runtime debug status field demoScene.${key} must be a non-negative integer.`
    );
  }
  return value;
}

// Requires the nested render-culling diagnostics object.
function requireRenderCullingStatus(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): RuntimeRenderCullingStatus {
  const value = record[key];
  if (!isRecord(value)) {
    throw new Error(`Runtime debug status field ${key} must be an object.`);
  }
  const cullingRecord = value as Partial<RuntimeRenderCullingStatusRecord>;
  return {
    extractedObjectCount: requireRenderCullingNonNegativeInteger(
      cullingRecord,
      "extractedObjectCount"
    ),
    cameraVisibleObjectCount: requireRenderCullingNonNegativeInteger(
      cullingRecord,
      "cameraVisibleObjectCount"
    ),
    cameraCulledObjectCount: requireRenderCullingNonNegativeInteger(
      cullingRecord,
      "cameraCulledObjectCount"
    )
  };
}

// Requires one render-culling diagnostic count.
function requireRenderCullingNonNegativeInteger(
  record: Partial<RuntimeRenderCullingStatusRecord>,
  key: keyof RuntimeRenderCullingStatus
): number {
  const value = record[key];
  if (typeof value !== "number" || !Number.isFinite(value) || !Number.isInteger(value) || value < 0) {
    throw new Error(
      `Runtime debug status field renderCulling.${key} must be a non-negative integer.`
    );
  }
  return value;
}

// Requires the nested shadow diagnostics object.
function requireShadowStatus(
  record: Partial<RuntimeDebugStatusRecord>,
  key: keyof RuntimeDebugStatus
): RuntimeShadowStatus {
  const value = record[key];
  if (!isRecord(value)) {
    throw new Error(`Runtime debug status field ${key} must be an object.`);
  }
  const shadowRecord = value as Partial<RuntimeShadowStatusRecord>;
  const cascades = shadowRecord.cascades;
  if (!Array.isArray(cascades)) {
    throw new Error("Runtime debug status field shadow.cascades must be an array.");
  }
  if (cascades.length !== 3) {
    throw new Error(
      `Runtime debug status field shadow.cascades must contain 3 cascades, got ${cascades.length}.`
    );
  }
  return {
    enabled: requireShadowBoolean(shadowRecord, "enabled"),
    cascadeCount: requireShadowNonNegativeInteger(shadowRecord, "cascadeCount"),
    encodedPassCount: requireShadowNonNegativeInteger(shadowRecord, "encodedPassCount"),
    mapSize: requireShadowNonNegativeInteger(shadowRecord, "mapSize"),
    estimatedDepthBytes: requireShadowNonNegativeInteger(shadowRecord, "estimatedDepthBytes"),
    pcfMode: requireShadowString(shadowRecord, "pcfMode"),
    pcfSampleCount: requireShadowNonNegativeInteger(shadowRecord, "pcfSampleCount"),
    sunElevationRadians: requireShadowFiniteNumber(shadowRecord, "sunElevationRadians"),
    effectiveIntensity: requireShadowFiniteNumber(shadowRecord, "effectiveIntensity"),
    lowSunClamped: requireShadowBoolean(shadowRecord, "lowSunClamped"),
    cascades: cascades.map((cascade, index) => parseShadowCascadeStatus(cascade, index)),
    totalTestedCasterCount: requireShadowNonNegativeInteger(
      shadowRecord,
      "totalTestedCasterCount"
    ),
    totalAcceptedCasterCount: requireShadowNonNegativeInteger(
      shadowRecord,
      "totalAcceptedCasterCount"
    ),
    totalRejectedCasterCount: requireShadowNonNegativeInteger(
      shadowRecord,
      "totalRejectedCasterCount"
    ),
    totalDrawCount: requireShadowNonNegativeInteger(shadowRecord, "totalDrawCount"),
    totalSubmeshCount: requireShadowNonNegativeInteger(shadowRecord, "totalSubmeshCount"),
    totalIndexCount: requireShadowNonNegativeInteger(shadowRecord, "totalIndexCount")
  };
}

// Parses one shadow-cascade diagnostics record.
function parseShadowCascadeStatus(
  value: unknown,
  index: number
): RuntimeShadowCascadeStatus {
  if (!isRecord(value)) {
    throw new Error(`Runtime debug status field shadow.cascades[${index}] must be an object.`);
  }
  const cascadeRecord = value as Partial<RuntimeShadowCascadeStatusRecord>;
  return {
    index: requireShadowCascadeNonNegativeInteger(cascadeRecord, "index", index),
    testedCasterCount: requireShadowCascadeNonNegativeInteger(
      cascadeRecord,
      "testedCasterCount",
      index
    ),
    acceptedCasterCount: requireShadowCascadeNonNegativeInteger(
      cascadeRecord,
      "acceptedCasterCount",
      index
    ),
    rejectedCasterCount: requireShadowCascadeNonNegativeInteger(
      cascadeRecord,
      "rejectedCasterCount",
      index
    ),
    drawCount: requireShadowCascadeNonNegativeInteger(cascadeRecord, "drawCount", index),
    submeshCount: requireShadowCascadeNonNegativeInteger(cascadeRecord, "submeshCount", index),
    indexCount: requireShadowCascadeNonNegativeInteger(cascadeRecord, "indexCount", index)
  };
}

// Requires one shadow diagnostic boolean.
function requireShadowBoolean(
  record: Partial<RuntimeShadowStatusRecord>,
  key: keyof RuntimeShadowStatus
): boolean {
  const value = record[key];
  if (typeof value !== "boolean") {
    throw new Error(`Runtime debug status field shadow.${key} must be a boolean.`);
  }
  return value;
}

// Requires one shadow diagnostic string.
function requireShadowString(
  record: Partial<RuntimeShadowStatusRecord>,
  key: keyof RuntimeShadowStatus
): string {
  const value = record[key];
  if (typeof value !== "string") {
    throw new Error(`Runtime debug status field shadow.${key} must be a string.`);
  }
  return value;
}

// Requires one finite shadow diagnostic scalar.
function requireShadowFiniteNumber(
  record: Partial<RuntimeShadowStatusRecord>,
  key: keyof RuntimeShadowStatus
): number {
  const value = record[key];
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(`Runtime debug status field shadow.${key} must be a finite number.`);
  }
  return value;
}

// Requires one shadow diagnostic count.
function requireShadowNonNegativeInteger(
  record: Partial<RuntimeShadowStatusRecord>,
  key: keyof RuntimeShadowStatus
): number {
  const value = requireShadowFiniteNumber(record, key);
  if (!Number.isInteger(value) || value < 0) {
    throw new Error(
      `Runtime debug status field shadow.${key} must be a non-negative integer.`
    );
  }
  return value;
}

// Requires one shadow cascade diagnostic count.
function requireShadowCascadeNonNegativeInteger(
  record: Partial<RuntimeShadowCascadeStatusRecord>,
  key: keyof RuntimeShadowCascadeStatus,
  index: number
): number {
  const value = record[key];
  if (typeof value !== "number" || !Number.isFinite(value) || !Number.isInteger(value) || value < 0) {
    throw new Error(
      `Runtime debug status field shadow.cascades[${index}].${key} must be a non-negative integer.`
    );
  }
  return value;
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
