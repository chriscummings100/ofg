/* tslint:disable */
/* eslint-disable */
export class RustBrowserGame {
  private constructor();
  free(): void;
  static create(canvas: HTMLCanvasElement, asset_loader: any): Promise<RustBrowserGame>;
  resize(viewport: any): void;
  tick(frame: any): void;
  configureTerrainWorkers(options: any): void;
  takeTerrainBuildRequests(): any;
  completeTerrainBuilds(completions: any): number;
  command(command: any): void;
  debugSnapshot(): any;
}

export type InitInput = RequestInfo | URL | Response | BufferSource | WebAssembly.Module;

export interface InitOutput {
  readonly memory: WebAssembly.Memory;
  readonly __wbg_rustbrowsergame_free: (a: number, b: number) => void;
  readonly rustbrowsergame_create: (a: number, b: number) => number;
  readonly rustbrowsergame_resize: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_tick: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_configureTerrainWorkers: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_takeTerrainBuildRequests: (a: number, b: number) => void;
  readonly rustbrowsergame_completeTerrainBuilds: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_command: (a: number, b: number, c: number) => void;
  readonly rustbrowsergame_debugSnapshot: (a: number, b: number) => void;
  readonly ofg_terrain_core_version: () => number;
  readonly ofg_terrain_core_preset_count: () => number;
  readonly ofg_terrain_variant_flat_value_count: () => number;
  readonly ofg_terrain_variant_buffer_ptr: () => number;
  readonly ofg_write_terrain_variant_preset: (a: number) => number;
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
  readonly ofg_stream_complete_lod0: (a: number, b: number, c: number, d: number, e: number) => number;
  readonly ofg_stream_fail_lod0: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_stream_write_desired_density_coords: () => number;
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
  readonly ofg_retain_density_chunk_store_window: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number) => number;
  readonly ofg_prepare_density_chunk_window: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number) => number;
  readonly ofg_density_chunk_sample_count: () => number;
  readonly ofg_density_chunk_buffer_ptr: () => number;
  readonly ofg_fill_density_chunk: (a: number, b: number, c: number, d: number, e: number, f: number) => void;
  readonly ofg_build_chunk_mesh: (a: number, b: number, c: number, d: number, e: number, f: number) => number;
  readonly ofg_build_chunk_mesh_for_variant: (a: number, b: number, c: number, d: number, e: number) => number;
  readonly ofg_mesh_vertex_buffer_ptr: () => number;
  readonly ofg_mesh_vertex_buffer_len: () => number;
  readonly ofg_mesh_index_buffer_ptr: () => number;
  readonly ofg_mesh_index_buffer_len: () => number;
  readonly ofg_build_water_node_packet_for_variant: (a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number) => number;
  readonly ofg_water_node_bathymetry_buffer_ptr: () => number;
  readonly ofg_water_node_bathymetry_buffer_len: () => number;
  readonly ofg_water_node_bathymetry_texel_count: () => number;
  readonly ofg_water_node_bathymetry_origin_x: () => number;
  readonly ofg_water_node_bathymetry_origin_z: () => number;
  readonly ofg_water_node_bathymetry_world_span_x: () => number;
  readonly ofg_water_node_bathymetry_world_span_z: () => number;
  readonly ofg_water_node_bathymetry_sea_level: () => number;
  readonly ofg_water_node_bathymetry_max_depth: () => number;
  readonly ofg_macro_base_elevation_at: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_density_at: (a: number, b: number, c: number, d: number, e: number) => number;
  readonly ofg_stream_fail_density: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_height_at: (a: number, b: number, c: number, d: number) => number;
  readonly ofg_stream_job_buffer_capacity: () => number;
  readonly ofg_terrain_mesh_packet_coord_buffer_capacity: () => number;
  readonly ofg_worker_pool_max_workers: () => number;
  readonly ofg_stream_write_desired_lod0_coords: () => number;
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
