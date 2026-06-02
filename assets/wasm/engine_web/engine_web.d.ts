/* tslint:disable */
/* eslint-disable */
export class RustWgpuRenderer {
  private constructor();
  free(): void;
  static create(canvas: HTMLCanvasElement): Promise<RustWgpuRenderer>;
  resize(width: number, height: number): void;
  registerMesh(vertices: Float32Array, indices: Uint32Array, floats_per_vertex: number): number;
  destroyMesh(handle: number): void;
  registerTexture(width: number, height: number, layers: number, format_code: number, data: Uint8Array): number;
  destroyTexture(handle: number): void;
  registerObject(): number;
  destroyObject(handle: number): void;
  render(frame_packet: Float32Array, mesh_handles: Float64Array, object_handles: Float64Array, albedo_texture_handles: Float64Array, normal_texture_handles: Float64Array, material_texture_handles: Float64Array, world_matrices: Float32Array, material_packets: Float32Array): void;
  status(): RustWgpuRendererStatus;
  fallbackAlbedoTextureHandle(): number;
  fallbackNormalTextureHandle(): number;
  fallbackMaterialTextureHandle(): number;
}
export class RustWgpuRendererStatus {
  private constructor();
  free(): void;
  readonly version: number;
  readonly runtime: string;
  readonly configured: boolean;
  readonly canvasWidth: number;
  readonly canvasHeight: number;
  readonly requiredTextureArrayLayers: number;
  readonly maxTextureArrayLayers: number;
  readonly meshCount: number;
  readonly textureCount: number;
  readonly objectCount: number;
  readonly frameIndex: number;
  readonly frameDrawCount: number;
}

export type InitInput = RequestInfo | URL | Response | BufferSource | WebAssembly.Module;

export interface InitOutput {
  readonly memory: WebAssembly.Memory;
  readonly ofg_engine_web_version: () => number;
  readonly ofg_engine_web_required_texture_array_layers: () => number;
  readonly ofg_engine_web_reset: () => void;
  readonly ofg_engine_web_configure: (a: number, b: number, c: number) => number;
  readonly ofg_engine_web_configured: () => number;
  readonly ofg_engine_web_resize: (a: number, b: number) => number;
  readonly ofg_engine_web_canvas_width: () => number;
  readonly ofg_engine_web_canvas_height: () => number;
  readonly ofg_engine_web_max_texture_array_layers: () => number;
  readonly ofg_engine_web_register_mesh: (a: number, b: number, c: number) => bigint;
  readonly ofg_engine_web_destroy_mesh: (a: bigint) => number;
  readonly ofg_engine_web_register_texture: (a: number, b: number, c: number, d: number) => bigint;
  readonly ofg_engine_web_destroy_texture: (a: bigint) => number;
  readonly ofg_engine_web_register_object: () => bigint;
  readonly ofg_engine_web_destroy_object: (a: bigint) => number;
  readonly ofg_engine_web_begin_frame: (a: number, b: number) => number;
  readonly ofg_engine_web_note_draw: (a: bigint, b: bigint) => number;
  readonly ofg_engine_web_mesh_count: () => number;
  readonly ofg_engine_web_texture_count: () => number;
  readonly ofg_engine_web_object_count: () => number;
  readonly ofg_engine_web_frame_index: () => bigint;
  readonly ofg_engine_web_frame_draw_count: () => number;
  readonly ofg_engine_web_last_error_code: () => number;
  readonly __wbg_rustwgpurenderer_free: (a: number, b: number) => void;
  readonly __wbg_rustwgpurendererstatus_free: (a: number, b: number) => void;
  readonly rustwgpurenderer_create: (a: number) => number;
  readonly rustwgpurenderer_resize: (a: number, b: number, c: number, d: number) => void;
  readonly rustwgpurenderer_registerMesh: (a: number, b: number, c: number, d: number, e: number, f: number, g: number) => void;
  readonly rustwgpurenderer_destroyMesh: (a: number, b: number, c: number) => void;
  readonly rustwgpurenderer_registerTexture: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number) => void;
  readonly rustwgpurenderer_destroyTexture: (a: number, b: number, c: number) => void;
  readonly rustwgpurenderer_registerObject: (a: number, b: number) => void;
  readonly rustwgpurenderer_destroyObject: (a: number, b: number, c: number) => void;
  readonly rustwgpurenderer_render: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number, j: number, k: number, l: number, m: number, n: number, o: number, p: number, q: number, r: number) => void;
  readonly rustwgpurenderer_status: (a: number) => number;
  readonly rustwgpurenderer_fallbackAlbedoTextureHandle: (a: number) => number;
  readonly rustwgpurenderer_fallbackNormalTextureHandle: (a: number) => number;
  readonly rustwgpurenderer_fallbackMaterialTextureHandle: (a: number) => number;
  readonly rustwgpurendererstatus_version: (a: number) => number;
  readonly rustwgpurendererstatus_runtime: (a: number, b: number) => void;
  readonly rustwgpurendererstatus_configured: (a: number) => number;
  readonly rustwgpurendererstatus_canvasWidth: (a: number) => number;
  readonly rustwgpurendererstatus_canvasHeight: (a: number) => number;
  readonly rustwgpurendererstatus_requiredTextureArrayLayers: (a: number) => number;
  readonly rustwgpurendererstatus_maxTextureArrayLayers: (a: number) => number;
  readonly rustwgpurendererstatus_meshCount: (a: number) => number;
  readonly rustwgpurendererstatus_textureCount: (a: number) => number;
  readonly rustwgpurendererstatus_objectCount: (a: number) => number;
  readonly rustwgpurendererstatus_frameIndex: (a: number) => number;
  readonly rustwgpurendererstatus_frameDrawCount: (a: number) => number;
  readonly __wbindgen_exn_store: (a: number) => void;
  readonly __wbindgen_free: (a: number, b: number, c: number) => void;
  readonly __wbindgen_malloc: (a: number, b: number) => number;
  readonly __wbindgen_realloc: (a: number, b: number, c: number, d: number) => number;
  readonly __wbindgen_export_4: WebAssembly.Table;
  readonly __wbindgen_add_to_stack_pointer: (a: number) => number;
  readonly _dyn_core__ops__function__FnMut__A____Output___R_as_wasm_bindgen__closure__WasmClosure___describe__invoke__h2909d47b96d2b968: (a: number, b: number, c: number) => void;
  readonly _dyn_core__ops__function__FnMut__A____Output___R_as_wasm_bindgen__closure__WasmClosure___describe__invoke__h5dc9a98bc8edeb10: (a: number, b: number, c: number) => void;
  readonly wasm_bindgen__convert__closures__invoke2_mut__h3ff0595175ed627e: (a: number, b: number, c: number, d: number) => void;
}

export type SyncInitInput = BufferSource | WebAssembly.Module;
/**
* Instantiates the given `module`, which can either be bytes or
* a precompiled `WebAssembly.Module`.
*
* @param {{ module: SyncInitInput }} module - Passing `SyncInitInput` directly is deprecated.
*
* @returns {InitOutput}
*/
export function initSync(module: { module: SyncInitInput } | SyncInitInput): InitOutput;

/**
* If `module_or_path` is {RequestInfo} or {URL}, makes a request and
* for everything else, calls `WebAssembly.instantiate` directly.
*
* @param {{ module_or_path: InitInput | Promise<InitInput> }} module_or_path - Passing `InitInput` directly is deprecated.
*
* @returns {Promise<InitOutput>}
*/
export default function __wbg_init (module_or_path?: { module_or_path: InitInput | Promise<InitInput> } | InitInput | Promise<InitInput>): Promise<InitOutput>;
