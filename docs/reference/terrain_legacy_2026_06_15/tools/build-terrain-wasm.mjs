import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const checkOnly = process.argv.includes("--check");
const crateName = "terrain_core";
const packageName = "terrain_core";
const target = "wasm32-unknown-unknown";
const cargoArtifactPath = `target/${target}/release/${crateName}.wasm`;
const assetPath = "assets/wasm/terrain_core.wasm";
const expectedExports = [
  "memory",
  "ofg_terrain_core_version",
  "ofg_terrain_core_preset_count",
  "ofg_terrain_variant_flat_value_count",
  "ofg_terrain_variant_buffer_ptr",
  "ofg_write_terrain_variant_preset",
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
  "ofg_worker_pool_max_workers",
  "ofg_worker_pool_configure",
  "ofg_worker_pool_reset",
  "ofg_worker_pool_worker_count",
  "ofg_worker_pool_in_flight_count",
  "ofg_worker_pool_runtime_generation",
  "ofg_worker_pool_task_request_id",
  "ofg_worker_pool_task_worker_index",
  "ofg_worker_pool_task_runtime_generation",
  "ofg_worker_pool_begin_task",
  "ofg_worker_pool_finish_task",
  "ofg_worker_pool_fail_task",
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
  "ofg_stream_write_lod0_dependency_coords",
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
  "ofg_retain_density_chunk_store_window",
  "ofg_prepare_density_chunk_window",
  "ofg_density_chunk_sample_count",
  "ofg_density_chunk_buffer_ptr",
  "ofg_fill_density_chunk",
  "ofg_build_chunk_mesh",
  "ofg_build_chunk_mesh_for_variant",
  "ofg_mesh_vertex_buffer_ptr",
  "ofg_mesh_vertex_buffer_len",
  "ofg_mesh_index_buffer_ptr",
  "ofg_mesh_index_buffer_len",
  "ofg_build_water_node_packet_for_variant",
  "ofg_water_node_bathymetry_buffer_ptr",
  "ofg_water_node_bathymetry_buffer_len",
  "ofg_water_node_bathymetry_texel_count",
  "ofg_water_node_bathymetry_origin_x",
  "ofg_water_node_bathymetry_origin_z",
  "ofg_water_node_bathymetry_world_span_x",
  "ofg_water_node_bathymetry_world_span_z",
  "ofg_water_node_bathymetry_sea_level",
  "ofg_water_node_bathymetry_max_depth",
  "ofg_macro_base_elevation_at",
  "ofg_density_at",
  "ofg_height_at"
];

const build = spawnSync(
  "cargo",
  ["build", "-p", packageName, "--target", target, "--release"],
  {
    cwd: root,
    stdio: "inherit"
  }
);

if (build.status !== 0) {
  process.exitCode = build.status ?? 1;
  process.exit();
}

const wasmBytes = readFileSync(resolve(root, cargoArtifactPath));
const assetAbsolutePath = resolve(root, assetPath);
const currentAsset = existsSync(assetAbsolutePath)
  ? readFileSync(assetAbsolutePath)
  : undefined;

let hasMismatch = false;
const wasmExports = new Set(
  WebAssembly.Module.exports(new WebAssembly.Module(wasmBytes)).map((entry) => entry.name)
);
const missingExports = expectedExports.filter((name) => !wasmExports.has(name));
if (missingExports.length > 0) {
  console.error(`Terrain WASM artifact is missing exports: ${missingExports.join(", ")}`);
  hasMismatch = true;
}

if (!currentAsset || !Buffer.from(currentAsset).equals(Buffer.from(wasmBytes))) {
  if (checkOnly) {
    console.error(`Terrain WASM asset is stale: ${assetPath}`);
    hasMismatch = true;
  } else {
    mkdirSync(dirname(assetAbsolutePath), { recursive: true });
    writeFileSync(assetAbsolutePath, wasmBytes);
    console.log(`Generated ${assetPath}`);
  }
}

if (hasMismatch) {
  process.exitCode = 1;
}
