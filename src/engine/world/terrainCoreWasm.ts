import type { TerrainPresetId } from "./terrainGenerator.js";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";

export type TerrainCoreWasmExports = {
  readonly memory: WebAssembly.Memory;
  readonly ofg_terrain_core_version: () => number;
  readonly ofg_terrain_core_preset_count: () => number;
  readonly ofg_density_chunk_store_max_entries: () => number;
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

function assertTerrainCoreExports(exports: WebAssembly.Exports): asserts exports is TerrainCoreWasmExports {
  if (!(exports.memory instanceof WebAssembly.Memory)) {
    throw new Error("Terrain WASM export is missing: memory");
  }

  const expectedFunctionNames = [
    "ofg_terrain_core_version",
    "ofg_terrain_core_preset_count",
    "ofg_density_chunk_store_max_entries",
    "ofg_density_chunk_store_entry_count",
    "ofg_density_chunk_store_reuse_count",
    "ofg_density_chunk_store_generation_count",
    "ofg_density_chunk_store_eviction_count",
    "ofg_reset_density_chunk_store",
    "ofg_store_density_chunk_buffer",
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
