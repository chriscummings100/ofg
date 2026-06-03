/* tslint:disable */
/* eslint-disable */
export class RustBrowserGame {
  private constructor();
  free(): void;
  static create(canvas: HTMLCanvasElement): Promise<RustBrowserGame>;
  resize(width: number, height: number): void;
  resetGame(terrain_seed: number, terrain_preset: number): void;
  tick(delta_seconds: number, forward: number, right: number, up: number, fast: boolean, look_delta_x: number, look_delta_y: number): void;
  togglePlayerMode(): number;
  playerMode(): number;
  setPlayerMode(mode: number): void;
  playerX(): number;
  playerY(): number;
  playerZ(): number;
  setPlayerPosition(x: number, z: number): void;
  setDebugCamera(x: number, y: number, z: number, yaw: number, pitch: number): void;
  upsertTerrainMesh(chunk_key: string, vertices: Float32Array, indices: Uint32Array): void;
  destroyTerrainMesh(chunk_key: string): void;
  retainTerrainMeshes(chunk_keys: Array<any>): void;
  clearTerrainMeshes(): void;
  upsertTerrainTextures(width: number, height: number, layers: number, format_code: number, albedo_data: Uint8Array, normal_data: Uint8Array, material_data: Uint8Array): void;
  renderGameFrame(aspect: number): void;
  status(): RustBrowserGameStatus;
}
export class RustBrowserGameStatus {
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
  readonly __wbg_rustbrowsergame_free: (a: number, b: number) => void;
  readonly __wbg_rustbrowsergamestatus_free: (a: number, b: number) => void;
  readonly rustbrowsergame_create: (a: number) => number;
  readonly rustbrowsergame_resize: (a: number, b: number, c: number, d: number) => void;
  readonly rustbrowsergame_resetGame: (a: number, b: number, c: number, d: number) => void;
  readonly rustbrowsergame_tick: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number) => void;
  readonly rustbrowsergame_togglePlayerMode: (a: number, b: number) => void;
  readonly rustbrowsergame_playerMode: (a: number, b: number) => void;
  readonly rustbrowsergame_setPlayerMode: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_playerX: (a: number, b: number) => void;
  readonly rustbrowsergame_playerY: (a: number, b: number) => void;
  readonly rustbrowsergame_playerZ: (a: number, b: number) => void;
  readonly rustbrowsergame_setPlayerPosition: (a: number, b: number, c: number, d: number) => void;
  readonly rustbrowsergame_setDebugCamera: (a: number, b: number, c: number, d: number, e: number, f: number, g: number) => void;
  readonly rustbrowsergame_upsertTerrainMesh: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number) => void;
  readonly rustbrowsergame_destroyTerrainMesh: (a: number, b: number, c: number, d: number) => void;
  readonly rustbrowsergame_retainTerrainMeshes: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_clearTerrainMeshes: (a: number, b: number) => void;
  readonly rustbrowsergame_upsertTerrainTextures: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number, j: number, k: number, l: number) => void;
  readonly rustbrowsergame_renderGameFrame: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_status: (a: number) => number;
  readonly rustbrowsergamestatus_version: (a: number) => number;
  readonly rustbrowsergamestatus_runtime: (a: number, b: number) => void;
  readonly rustbrowsergamestatus_configured: (a: number) => number;
  readonly rustbrowsergamestatus_canvasWidth: (a: number) => number;
  readonly rustbrowsergamestatus_canvasHeight: (a: number) => number;
  readonly rustbrowsergamestatus_requiredTextureArrayLayers: (a: number) => number;
  readonly rustbrowsergamestatus_maxTextureArrayLayers: (a: number) => number;
  readonly rustbrowsergamestatus_meshCount: (a: number) => number;
  readonly rustbrowsergamestatus_textureCount: (a: number) => number;
  readonly rustbrowsergamestatus_objectCount: (a: number) => number;
  readonly rustbrowsergamestatus_frameIndex: (a: number) => number;
  readonly rustbrowsergamestatus_frameDrawCount: (a: number) => number;
  readonly ofg_terrain_core_version: () => number;
  readonly ofg_terrain_core_preset_count: () => number;
  readonly ofg_density_chunk_store_max_entries: () => number;
  readonly ofg_stream_vertical_offset_buffer_capacity: () => number;
  readonly ofg_stream_vertical_offset_buffer_ptr: () => number;
  readonly ofg_stream_coord_buffer_capacity: () => number;
  readonly ofg_stream_job_kind_buffer_ptr: () => number;
  readonly ofg_stream_job_lod_buffer_ptr: () => number;
  readonly ofg_stream_job_generation_buffer_ptr: () => number;
  readonly ofg_stream_job_x_buffer_ptr: () => number;
  readonly ofg_stream_job_y_buffer_ptr: () => number;
  readonly ofg_stream_job_z_buffer_ptr: () => number;
  readonly ofg_stream_coord_x_buffer_ptr: () => number;
  readonly ofg_stream_coord_y_buffer_ptr: () => number;
  readonly ofg_stream_coord_z_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_lod_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_x_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_y_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_z_buffer_ptr: () => number;
  readonly ofg_worker_pool_configure: (a: number) => number;
  readonly ofg_worker_pool_reset: () => void;
  readonly ofg_worker_pool_worker_count: () => number;
  readonly ofg_worker_pool_in_flight_count: () => number;
  readonly ofg_worker_pool_runtime_generation: () => number;
  readonly ofg_worker_pool_task_request_id: () => number;
  readonly ofg_worker_pool_task_worker_index: () => number;
  readonly ofg_worker_pool_task_runtime_generation: () => number;
  readonly ofg_worker_pool_begin_task: (a: number, b: number, c: number, d: number, e: number, f: number) => number;
  readonly ofg_worker_pool_finish_task: (a: number, b: number, c: number, d: number, e: number, f: number, g: number) => number;
  readonly ofg_worker_pool_fail_task: (a: number) => number;
  readonly ofg_prepare_terrain_mesh_packet_input: (a: number, b: number) => number;
  readonly ofg_terrain_mesh_packet_input_vertex_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_input_vertex_buffer_len: () => number;
  readonly ofg_terrain_mesh_packet_input_index_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_input_index_buffer_len: () => number;
  readonly ofg_store_terrain_mesh_packet_buffer: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_reset_terrain_mesh_packet_store: () => void;
  readonly ofg_terrain_mesh_packet_store_entry_count: () => number;
  readonly ofg_terrain_mesh_packet_store_version: () => number;
  readonly ofg_terrain_mesh_packet_store_contains: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_remove_terrain_mesh_packet: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_retain_terrain_mesh_packets: (a: number) => number;
  readonly ofg_write_terrain_mesh_packet_coords: () => number;
  readonly ofg_load_terrain_mesh_packet_buffer: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_stream_configure: (a: number, b: number, c: number) => number;
  readonly ofg_stream_generation: () => number;
  readonly ofg_stream_sync_center: (a: number, b: number, c: number) => void;
  readonly ofg_stream_reset: (a: number, b: number, c: number) => void;
  readonly ofg_stream_invalidate_all: () => void;
  readonly ofg_stream_tick: () => number;
  readonly ofg_stream_complete_density: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_stream_fail_density: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_stream_complete_lod0: (a: number, b: number, c: number, d: number, e: number) => number;
  readonly ofg_stream_fail_lod0: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_stream_write_desired_density_coords: () => number;
  readonly ofg_stream_write_desired_lod0_coords: () => number;
  readonly ofg_stream_write_lod0_dependency_coords: (a: number, b: number, c: number) => number;
  readonly ofg_stream_status_desired_density_count: () => number;
  readonly ofg_stream_status_desired_lod0_count: () => number;
  readonly ofg_stream_status_density_ready_count: () => number;
  readonly ofg_stream_status_lod0_ready_count: () => number;
  readonly ofg_stream_status_lod0_empty_count: () => number;
  readonly ofg_stream_status_in_flight_density_count: () => number;
  readonly ofg_stream_status_in_flight_lod_count: () => number;
  readonly ofg_stream_status_missing_density_count: () => number;
  readonly ofg_stream_status_missing_lod0_count: () => number;
  readonly ofg_stream_status_max_in_flight_jobs: () => number;
  readonly ofg_density_chunk_store_entry_count: () => number;
  readonly ofg_density_chunk_store_reuse_count: () => number;
  readonly ofg_density_chunk_store_generation_count: () => number;
  readonly ofg_density_chunk_store_eviction_count: () => number;
  readonly ofg_reset_density_chunk_store: () => void;
  readonly ofg_store_density_chunk_buffer: (a: number, b: number, c: number, d: number, e: number, f: number) => number;
  readonly ofg_density_chunk_store_contains: (a: number, b: number, c: number, d: number, e: number, f: number) => number;
  readonly ofg_load_density_chunk_buffer: (a: number, b: number, c: number, d: number, e: number, f: number) => number;
  readonly ofg_retain_density_chunk_store_window: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number) => number;
  readonly ofg_prepare_density_chunk_window: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number) => number;
  readonly ofg_density_chunk_sample_count: () => number;
  readonly ofg_density_chunk_buffer_ptr: () => number;
  readonly ofg_fill_density_chunk: (a: number, b: number, c: number, d: number, e: number, f: number) => void;
  readonly ofg_build_chunk_mesh: (a: number, b: number, c: number, d: number, e: number, f: number) => number;
  readonly ofg_mesh_vertex_buffer_ptr: () => number;
  readonly ofg_mesh_vertex_buffer_len: () => number;
  readonly ofg_mesh_index_buffer_ptr: () => number;
  readonly ofg_mesh_index_buffer_len: () => number;
  readonly ofg_macro_base_elevation_at: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_density_at: (a: number, b: number, c: number, d: number, e: number) => number;
  readonly ofg_height_at: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_stream_job_buffer_capacity: () => number;
  readonly ofg_terrain_mesh_packet_coord_buffer_capacity: () => number;
  readonly ofg_worker_pool_max_workers: () => number;
  readonly ofg_engine_core_version: () => number;
  readonly ofg_engine_create: () => void;
  readonly ofg_engine_create_entity: () => bigint;
  readonly ofg_engine_create_player: (a: number, b: number, c: number) => bigint;
  readonly ofg_engine_has_player: () => number;
  readonly ofg_engine_player_camera_entity: () => bigint;
  readonly ofg_engine_player_mode: () => number;
  readonly ofg_engine_set_player_mode: (a: number) => number;
  readonly ofg_engine_toggle_player_mode: () => number;
  readonly ofg_engine_set_player_intent: (a: number, b: number, c: number, d: number, e: number, f: number) => number;
  readonly ofg_engine_set_player_position: (a: number, b: number, c: number) => number;
  readonly ofg_engine_set_player_view: (a: number, b: number) => number;
  readonly ofg_engine_set_debug_camera: (a: number, b: number, c: number, d: number, e: number) => number;
  readonly ofg_engine_update_player: (a: number, b: number, c: number) => number;
  readonly ofg_engine_preview_player_x: (a: number) => number;
  readonly ofg_engine_preview_player_y: (a: number) => number;
  readonly ofg_engine_preview_player_z: (a: number) => number;
  readonly ofg_engine_update: (a: number) => number;
  readonly ofg_engine_tick: () => bigint;
  readonly ofg_engine_elapsed_seconds: () => number;
  readonly ofg_engine_entity_count: () => number;
  readonly ofg_engine_player_eye_x: () => number;
  readonly ofg_engine_player_eye_y: () => number;
  readonly ofg_engine_player_eye_z: () => number;
  readonly ofg_engine_player_eye_yaw: () => number;
  readonly ofg_engine_player_eye_pitch: () => number;
  readonly ofg_engine_player_x: () => number;
  readonly ofg_engine_player_y: () => number;
  readonly ofg_engine_player_z: () => number;
  readonly ofg_engine_render_snapshot_f32_count: () => number;
  readonly ofg_engine_render_snapshot_f32_ptr: () => number;
  readonly ofg_engine_write_render_snapshot: () => number;
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
