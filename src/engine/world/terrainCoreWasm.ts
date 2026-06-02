import type { TerrainPresetId } from "./terrainGenerator.js";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";

export type TerrainCoreWasmExports = {
  readonly memory: WebAssembly.Memory;
  readonly ofg_terrain_core_version: () => number;
  readonly ofg_terrain_core_preset_count: () => number;
  readonly ofg_density_chunk_store_max_entries: () => number;
  readonly ofg_stream_vertical_offset_buffer_capacity: () => number;
  readonly ofg_stream_vertical_offset_buffer_ptr: () => number;
  readonly ofg_stream_job_buffer_capacity: () => number;
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
  readonly ofg_terrain_mesh_packet_coord_buffer_capacity: () => number;
  readonly ofg_terrain_mesh_packet_lod_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_x_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_y_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_z_buffer_ptr: () => number;
  readonly ofg_prepare_terrain_mesh_packet_input: (
    vertexLen: number,
    indexLen: number
  ) => number;
  readonly ofg_terrain_mesh_packet_input_vertex_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_input_vertex_buffer_len: () => number;
  readonly ofg_terrain_mesh_packet_input_index_buffer_ptr: () => number;
  readonly ofg_terrain_mesh_packet_input_index_buffer_len: () => number;
  readonly ofg_store_terrain_mesh_packet_buffer: (
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    lod: number
  ) => number;
  readonly ofg_reset_terrain_mesh_packet_store: () => void;
  readonly ofg_terrain_mesh_packet_store_entry_count: () => number;
  readonly ofg_terrain_mesh_packet_store_version: () => number;
  readonly ofg_terrain_mesh_packet_store_contains: (
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    lod: number
  ) => number;
  readonly ofg_remove_terrain_mesh_packet: (
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    lod: number
  ) => number;
  readonly ofg_retain_terrain_mesh_packets: (count: number) => number;
  readonly ofg_write_terrain_mesh_packet_coords: () => number;
  readonly ofg_load_terrain_mesh_packet_buffer: (
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    lod: number
  ) => number;
  readonly ofg_stream_configure: (
    horizontalRadius: number,
    verticalOffsetCount: number,
    maxInFlightJobs: number
  ) => number;
  readonly ofg_stream_generation: () => number;
  readonly ofg_stream_sync_center: (
    chunkX: number,
    chunkY: number,
    chunkZ: number
  ) => void;
  readonly ofg_stream_reset: (
    chunkX: number,
    chunkY: number,
    chunkZ: number
  ) => void;
  readonly ofg_stream_invalidate_all: () => void;
  readonly ofg_stream_tick: () => number;
  readonly ofg_stream_complete_density: (
    generation: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number
  ) => number;
  readonly ofg_stream_fail_density: (
    generation: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number
  ) => number;
  readonly ofg_stream_complete_lod0: (
    generation: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    empty: number
  ) => number;
  readonly ofg_stream_fail_lod0: (
    generation: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number
  ) => number;
  readonly ofg_stream_write_desired_density_coords: () => number;
  readonly ofg_stream_write_desired_lod0_coords: () => number;
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
  readonly ofg_store_density_chunk_buffer: (
    seed: number,
    preset: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number
  ) => number;
  readonly ofg_density_chunk_store_contains: (
    seed: number,
    preset: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number
  ) => number;
  readonly ofg_load_density_chunk_buffer: (
    seed: number,
    preset: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number
  ) => number;
  readonly ofg_retain_density_chunk_store_window: (
    seed: number,
    preset: number,
    minChunkX: number,
    minChunkY: number,
    minChunkZ: number,
    maxChunkX: number,
    maxChunkY: number,
    maxChunkZ: number,
    cellSize: number
  ) => number;
  readonly ofg_prepare_density_chunk_window: (
    seed: number,
    preset: number,
    minChunkX: number,
    minChunkY: number,
    minChunkZ: number,
    maxChunkX: number,
    maxChunkY: number,
    maxChunkZ: number,
    cellSize: number
  ) => number;
  readonly ofg_density_chunk_sample_count: () => number;
  readonly ofg_density_chunk_buffer_ptr: () => number;
  readonly ofg_fill_density_chunk: (
    seed: number,
    preset: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number
  ) => void;
  readonly ofg_build_chunk_mesh: (
    seed: number,
    preset: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number
  ) => number;
  readonly ofg_mesh_vertex_buffer_ptr: () => number;
  readonly ofg_mesh_vertex_buffer_len: () => number;
  readonly ofg_mesh_index_buffer_ptr: () => number;
  readonly ofg_mesh_index_buffer_len: () => number;
  readonly ofg_macro_base_elevation_at: (
    seed: number,
    preset: number,
    x: number,
    z: number
  ) => number;
  readonly ofg_density_at: (
    seed: number,
    preset: number,
    x: number,
    y: number,
    z: number
  ) => number;
  readonly ofg_height_at: (
    seed: number,
    preset: number,
    x: number,
    z: number
  ) => number;
};

export type TerrainCoreWasmInstance = {
  readonly exports: TerrainCoreWasmExports;
};

export type TerrainCoreDensityChunkStoreStats = {
  readonly entries: number;
  readonly maxEntries: number;
  readonly reuses: number;
  readonly generations: number;
  readonly evictions: number;
};

const TERRAIN_PRESET_CODES: Readonly<Record<TerrainPresetId, number>> = Object.freeze({
  seed: 0,
  rollingHills: 1,
  mountainValley: 2,
  rockyHighland: 3
});

export async function instantiateTerrainCoreWasm(
  bytes: ArrayBuffer
): Promise<TerrainCoreWasmInstance> {
  const wasm = await WebAssembly.instantiate(bytes, {});
  const exports = wasm.instance.exports as TerrainCoreWasmExports;
  assertTerrainCoreExports(exports);

  return Object.freeze({ exports });
}

export async function loadTerrainCoreWasm(
  assetPath = TERRAIN_CORE_WASM_METADATA.assetPath,
  fetchWasm: typeof fetch = fetch
): Promise<TerrainCoreWasmInstance> {
  const response = await fetchWasm(assetPath);
  if (!response.ok) {
    throw new Error(`Failed to load terrain WASM artifact '${assetPath}': ${response.status}`);
  }

  return instantiateTerrainCoreWasm(await response.arrayBuffer());
}

export function terrainPresetToWasmCode(preset: TerrainPresetId): number {
  return TERRAIN_PRESET_CODES[preset];
}

export function readTerrainCoreDensityChunkBuffer(
  exports: TerrainCoreWasmExports
): Float32Array {
  const sampleCount = exports.ofg_density_chunk_sample_count();
  const ptr = exports.ofg_density_chunk_buffer_ptr();

  return new Float32Array(exports.memory.buffer, ptr, sampleCount);
}

export function readTerrainCoreDensityChunkStoreStats(
  exports: TerrainCoreWasmExports
): TerrainCoreDensityChunkStoreStats {
  return {
    entries: exports.ofg_density_chunk_store_entry_count(),
    maxEntries: exports.ofg_density_chunk_store_max_entries(),
    reuses: exports.ofg_density_chunk_store_reuse_count(),
    generations: exports.ofg_density_chunk_store_generation_count(),
    evictions: exports.ofg_density_chunk_store_eviction_count()
  };
}

export function readTerrainCoreMeshVertexBuffer(
  exports: TerrainCoreWasmExports
): Float32Array {
  return new Float32Array(
    exports.memory.buffer,
    exports.ofg_mesh_vertex_buffer_ptr(),
    exports.ofg_mesh_vertex_buffer_len()
  );
}

export function readTerrainCoreMeshIndexBuffer(
  exports: TerrainCoreWasmExports
): Uint32Array {
  return new Uint32Array(
    exports.memory.buffer,
    exports.ofg_mesh_index_buffer_ptr(),
    exports.ofg_mesh_index_buffer_len()
  );
}

export function readTerrainCoreMeshPacketInputVertexBuffer(
  exports: TerrainCoreWasmExports
): Float32Array {
  return new Float32Array(
    exports.memory.buffer,
    exports.ofg_terrain_mesh_packet_input_vertex_buffer_ptr(),
    exports.ofg_terrain_mesh_packet_input_vertex_buffer_len()
  );
}

export function readTerrainCoreMeshPacketInputIndexBuffer(
  exports: TerrainCoreWasmExports
): Uint32Array {
  return new Uint32Array(
    exports.memory.buffer,
    exports.ofg_terrain_mesh_packet_input_index_buffer_ptr(),
    exports.ofg_terrain_mesh_packet_input_index_buffer_len()
  );
}

function assertTerrainCoreExports(exports: WebAssembly.Exports): asserts exports is TerrainCoreWasmExports {
  if (!(exports.memory instanceof WebAssembly.Memory)) {
    throw new Error("Terrain WASM export is missing: memory");
  }

  const expectedFunctionNames = [
    "ofg_terrain_core_version",
    "ofg_terrain_core_preset_count",
    "ofg_density_chunk_store_max_entries",
    "ofg_stream_vertical_offset_buffer_capacity",
    "ofg_stream_vertical_offset_buffer_ptr",
    "ofg_stream_job_buffer_capacity",
    "ofg_stream_coord_buffer_capacity",
    "ofg_stream_job_kind_buffer_ptr",
    "ofg_stream_job_lod_buffer_ptr",
    "ofg_stream_job_generation_buffer_ptr",
    "ofg_stream_job_x_buffer_ptr",
    "ofg_stream_job_y_buffer_ptr",
    "ofg_stream_job_z_buffer_ptr",
    "ofg_stream_coord_x_buffer_ptr",
    "ofg_stream_coord_y_buffer_ptr",
    "ofg_stream_coord_z_buffer_ptr",
    "ofg_terrain_mesh_packet_coord_buffer_capacity",
    "ofg_terrain_mesh_packet_lod_buffer_ptr",
    "ofg_terrain_mesh_packet_x_buffer_ptr",
    "ofg_terrain_mesh_packet_y_buffer_ptr",
    "ofg_terrain_mesh_packet_z_buffer_ptr",
    "ofg_prepare_terrain_mesh_packet_input",
    "ofg_terrain_mesh_packet_input_vertex_buffer_ptr",
    "ofg_terrain_mesh_packet_input_vertex_buffer_len",
    "ofg_terrain_mesh_packet_input_index_buffer_ptr",
    "ofg_terrain_mesh_packet_input_index_buffer_len",
    "ofg_store_terrain_mesh_packet_buffer",
    "ofg_reset_terrain_mesh_packet_store",
    "ofg_terrain_mesh_packet_store_entry_count",
    "ofg_terrain_mesh_packet_store_version",
    "ofg_terrain_mesh_packet_store_contains",
    "ofg_remove_terrain_mesh_packet",
    "ofg_retain_terrain_mesh_packets",
    "ofg_write_terrain_mesh_packet_coords",
    "ofg_load_terrain_mesh_packet_buffer",
    "ofg_stream_configure",
    "ofg_stream_generation",
    "ofg_stream_sync_center",
    "ofg_stream_reset",
    "ofg_stream_invalidate_all",
    "ofg_stream_tick",
    "ofg_stream_complete_density",
    "ofg_stream_fail_density",
    "ofg_stream_complete_lod0",
    "ofg_stream_fail_lod0",
    "ofg_stream_write_desired_density_coords",
    "ofg_stream_write_desired_lod0_coords",
    "ofg_stream_status_desired_density_count",
    "ofg_stream_status_desired_lod0_count",
    "ofg_stream_status_density_ready_count",
    "ofg_stream_status_lod0_ready_count",
    "ofg_stream_status_lod0_empty_count",
    "ofg_stream_status_in_flight_density_count",
    "ofg_stream_status_in_flight_lod_count",
    "ofg_stream_status_missing_density_count",
    "ofg_stream_status_missing_lod0_count",
    "ofg_stream_status_max_in_flight_jobs",
    "ofg_density_chunk_store_entry_count",
    "ofg_density_chunk_store_reuse_count",
    "ofg_density_chunk_store_generation_count",
    "ofg_density_chunk_store_eviction_count",
    "ofg_reset_density_chunk_store",
    "ofg_store_density_chunk_buffer",
    "ofg_density_chunk_store_contains",
    "ofg_load_density_chunk_buffer",
    "ofg_retain_density_chunk_store_window",
    "ofg_prepare_density_chunk_window",
    "ofg_density_chunk_sample_count",
    "ofg_density_chunk_buffer_ptr",
    "ofg_fill_density_chunk",
    "ofg_build_chunk_mesh",
    "ofg_mesh_vertex_buffer_ptr",
    "ofg_mesh_vertex_buffer_len",
    "ofg_mesh_index_buffer_ptr",
    "ofg_mesh_index_buffer_len",
    "ofg_macro_base_elevation_at",
    "ofg_density_at",
    "ofg_height_at"
  ] as const;

  for (const name of expectedFunctionNames) {
    if (typeof exports[name] !== "function") {
      throw new Error(`Terrain WASM export is missing: ${name}`);
    }
  }
}
