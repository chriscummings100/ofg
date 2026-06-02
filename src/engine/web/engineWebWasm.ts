import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";

export const ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM = 1;
export const ENGINE_WEB_INVALID_HANDLE = -1n;

export type EngineWebWasmExports = {
  readonly memory: WebAssembly.Memory;
  readonly ofg_engine_web_version: () => number;
  readonly ofg_engine_web_required_texture_array_layers: () => number;
  readonly ofg_engine_web_reset: () => void;
  readonly ofg_engine_web_configure: (
    canvasWidth: number,
    canvasHeight: number,
    maxTextureArrayLayers: number
  ) => number;
  readonly ofg_engine_web_configured: () => number;
  readonly ofg_engine_web_resize: (canvasWidth: number, canvasHeight: number) => number;
  readonly ofg_engine_web_canvas_width: () => number;
  readonly ofg_engine_web_canvas_height: () => number;
  readonly ofg_engine_web_max_texture_array_layers: () => number;
  readonly ofg_engine_web_register_mesh: (
    vertexFloatCount: number,
    indexCount: number,
    floatsPerVertex: number
  ) => bigint;
  readonly ofg_engine_web_destroy_mesh: (handle: bigint) => number;
  readonly ofg_engine_web_register_texture: (
    width: number,
    height: number,
    layers: number,
    formatCode: number
  ) => bigint;
  readonly ofg_engine_web_destroy_texture: (handle: bigint) => number;
  readonly ofg_engine_web_register_object: () => bigint;
  readonly ofg_engine_web_destroy_object: (handle: bigint) => number;
  readonly ofg_engine_web_begin_frame: (canvasWidth: number, canvasHeight: number) => number;
  readonly ofg_engine_web_note_draw: (meshHandle: bigint, objectHandle: bigint) => number;
  readonly ofg_engine_web_mesh_count: () => number;
  readonly ofg_engine_web_texture_count: () => number;
  readonly ofg_engine_web_object_count: () => number;
  readonly ofg_engine_web_frame_index: () => bigint;
  readonly ofg_engine_web_frame_draw_count: () => number;
  readonly ofg_engine_web_last_error_code: () => number;
};

export type EngineWebWasmInstance = {
  readonly exports: EngineWebWasmExports;
};

export type EngineWebRendererStatus = {
  readonly version: number;
  readonly runtime: "rust";
  readonly configured: boolean;
  readonly canvasWidth: number;
  readonly canvasHeight: number;
  readonly maxTextureArrayLayers: number;
  readonly requiredTextureArrayLayers: number;
  readonly meshCount: number;
  readonly textureCount: number;
  readonly objectCount: number;
  readonly frameIndex: bigint;
  readonly frameDrawCount: number;
  readonly lastErrorCode: number;
};

export type EngineWebMeshRegistration = {
  readonly vertexFloatCount: number;
  readonly indexCount: number;
  readonly floatsPerVertex: number;
};

export type EngineWebTextureRegistration = {
  readonly width: number;
  readonly height: number;
  readonly layers: number;
  readonly formatCode: number;
};

export class EngineWebGpuBridge {
  readonly runtime = "rust" as const;
  readonly #exports: EngineWebWasmExports;

  constructor(instance: EngineWebWasmInstance) {
    this.#exports = instance.exports;
  }

  reset(): void {
    this.#exports.ofg_engine_web_reset();
  }

  configure(
    canvasWidth: number,
    canvasHeight: number,
    maxTextureArrayLayers: number
  ): boolean {
    return this.#exports.ofg_engine_web_configure(
      canvasWidth,
      canvasHeight,
      maxTextureArrayLayers
    ) === 1;
  }

  resize(canvasWidth: number, canvasHeight: number): boolean {
    return this.#exports.ofg_engine_web_resize(canvasWidth, canvasHeight) === 1;
  }

  registerMesh(mesh: EngineWebMeshRegistration): bigint | undefined {
    return this.handleOrUndefined(this.#exports.ofg_engine_web_register_mesh(
      mesh.vertexFloatCount,
      mesh.indexCount,
      mesh.floatsPerVertex
    ));
  }

  destroyMesh(handle: bigint): boolean {
    return this.#exports.ofg_engine_web_destroy_mesh(handle) === 1;
  }

  registerTexture(texture: EngineWebTextureRegistration): bigint | undefined {
    return this.handleOrUndefined(this.#exports.ofg_engine_web_register_texture(
      texture.width,
      texture.height,
      texture.layers,
      texture.formatCode
    ));
  }

  destroyTexture(handle: bigint): boolean {
    return this.#exports.ofg_engine_web_destroy_texture(handle) === 1;
  }

  registerObject(): bigint | undefined {
    return this.handleOrUndefined(this.#exports.ofg_engine_web_register_object());
  }

  destroyObject(handle: bigint): boolean {
    return this.#exports.ofg_engine_web_destroy_object(handle) === 1;
  }

  beginFrame(canvasWidth: number, canvasHeight: number): boolean {
    return this.#exports.ofg_engine_web_begin_frame(canvasWidth, canvasHeight) === 1;
  }

  noteDraw(meshHandle: bigint, objectHandle: bigint): boolean {
    return this.#exports.ofg_engine_web_note_draw(meshHandle, objectHandle) === 1;
  }

  status(): EngineWebRendererStatus {
    return Object.freeze({
      version: this.#exports.ofg_engine_web_version(),
      runtime: "rust" as const,
      configured: this.#exports.ofg_engine_web_configured() === 1,
      canvasWidth: this.#exports.ofg_engine_web_canvas_width(),
      canvasHeight: this.#exports.ofg_engine_web_canvas_height(),
      maxTextureArrayLayers: this.#exports.ofg_engine_web_max_texture_array_layers(),
      requiredTextureArrayLayers: this.#exports.ofg_engine_web_required_texture_array_layers(),
      meshCount: this.#exports.ofg_engine_web_mesh_count(),
      textureCount: this.#exports.ofg_engine_web_texture_count(),
      objectCount: this.#exports.ofg_engine_web_object_count(),
      frameIndex: this.#exports.ofg_engine_web_frame_index(),
      frameDrawCount: this.#exports.ofg_engine_web_frame_draw_count(),
      lastErrorCode: this.#exports.ofg_engine_web_last_error_code()
    });
  }

  private handleOrUndefined(handle: bigint): bigint | undefined {
    return handle === ENGINE_WEB_INVALID_HANDLE ? undefined : handle;
  }
}

export async function instantiateEngineWebWasm(
  bytes: ArrayBuffer
): Promise<EngineWebWasmInstance> {
  const wasm = await WebAssembly.instantiate(bytes, {});
  const exports = wasm.instance.exports as EngineWebWasmExports;
  assertEngineWebExports(exports);

  return Object.freeze({ exports });
}

export async function loadEngineWebWasm(
  assetPath = ENGINE_WEB_WASM_METADATA.assetPath,
  fetchWasm: typeof fetch = fetch
): Promise<EngineWebWasmInstance> {
  const response = await fetchWasm(assetPath);
  if (!response.ok) {
    throw new Error(`Failed to load engine web WASM artifact '${assetPath}': ${response.status}`);
  }

  return instantiateEngineWebWasm(await response.arrayBuffer());
}

function assertEngineWebExports(exports: WebAssembly.Exports): asserts exports is EngineWebWasmExports {
  if (!(exports.memory instanceof WebAssembly.Memory)) {
    throw new Error("Engine Web WASM export is missing: memory");
  }

  const expectedFunctionNames = [
    "ofg_engine_web_version",
    "ofg_engine_web_required_texture_array_layers",
    "ofg_engine_web_reset",
    "ofg_engine_web_configure",
    "ofg_engine_web_configured",
    "ofg_engine_web_resize",
    "ofg_engine_web_canvas_width",
    "ofg_engine_web_canvas_height",
    "ofg_engine_web_max_texture_array_layers",
    "ofg_engine_web_register_mesh",
    "ofg_engine_web_destroy_mesh",
    "ofg_engine_web_register_texture",
    "ofg_engine_web_destroy_texture",
    "ofg_engine_web_register_object",
    "ofg_engine_web_destroy_object",
    "ofg_engine_web_begin_frame",
    "ofg_engine_web_note_draw",
    "ofg_engine_web_mesh_count",
    "ofg_engine_web_texture_count",
    "ofg_engine_web_object_count",
    "ofg_engine_web_frame_index",
    "ofg_engine_web_frame_draw_count",
    "ofg_engine_web_last_error_code"
  ] as const;

  for (const name of expectedFunctionNames) {
    if (typeof exports[name] !== "function") {
      throw new Error(`Engine Web WASM export is missing: ${name}`);
    }
  }
}
