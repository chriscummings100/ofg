// Web Worker entry point for terrain node builds. It loads the Rust
// terrain_core WASM artifact and calls the raw mesh export for one Rust-issued
// build request at a time.

import type {
  BrowserWorkerCompletionEnvelope,
  BrowserWorkerRequestEnvelope
} from "../browser/browserWorkerHost.js";
import type {
  TerrainBuildCompletion,
  TerrainBuildRequest
} from "./terrainWorkerClient.js";

type TerrainCoreExports = {
  readonly memory: WebAssembly.Memory;
  ofg_terrain_variant_flat_value_count(): number;
  ofg_terrain_variant_buffer_ptr(): number;
  ofg_build_chunk_mesh(
    seed: number,
    preset: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number
  ): number;
  ofg_build_chunk_mesh_for_variant(
    seed: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number
  ): number;
  ofg_mesh_vertex_buffer_ptr(): number;
  ofg_mesh_vertex_buffer_len(): number;
  ofg_mesh_index_buffer_ptr(): number;
  ofg_mesh_index_buffer_len(): number;
  ofg_build_water_node_packet_for_variant(
    seed: number,
    lod: number,
    chunkX: number,
    chunkY: number,
    chunkZ: number,
    cellSize: number,
    seaLevel: number,
    maxDepthMeters: number
  ): number;
  ofg_water_node_bathymetry_buffer_ptr(): number;
  ofg_water_node_bathymetry_buffer_len(): number;
  ofg_water_node_bathymetry_texel_count(): number;
  ofg_water_node_bathymetry_origin_x(): number;
  ofg_water_node_bathymetry_origin_z(): number;
  ofg_water_node_bathymetry_world_span_x(): number;
  ofg_water_node_bathymetry_world_span_z(): number;
  ofg_water_node_bathymetry_sea_level(): number;
  ofg_water_node_bathymetry_max_depth(): number;
};

type TerrainBuildWorkerScope = {
  addEventListener(
    type: "message",
    listener: (event: MessageEvent<BrowserWorkerRequestEnvelope<TerrainBuildRequest>>) => void
  ): void;
  postMessage(
    message: BrowserWorkerCompletionEnvelope<TerrainBuildCompletion>,
    transfer?: Transferable[]
  ): void;
};

const worker = self as unknown as TerrainBuildWorkerScope;
const wasmUrl = new URL("../../../assets/wasm/terrain_core.wasm", import.meta.url);
const WATER_NODE_MAX_RELEVANT_DEPTH_METERS = 64;
let terrainCorePromise: Promise<TerrainCoreExports> | undefined;

worker.addEventListener(
  "message",
  (event: MessageEvent<BrowserWorkerRequestEnvelope<TerrainBuildRequest>>) => {
    void handleRequest(event.data);
  }
);

async function handleRequest(envelope: BrowserWorkerRequestEnvelope<TerrainBuildRequest>): Promise<void> {
  if (envelope.type !== "request") {
    return;
  }

  try {
    const terrainCore = await loadTerrainCore();
    const startedAt = performance.now();
    const request = envelope.payload;
    writeTerrainVariantBuffer(terrainCore, request.terrainVariant);
    terrainCore.ofg_build_chunk_mesh_for_variant(
      request.seed,
      request.x,
      request.y,
      request.z,
      request.cellSize
    );
    const vertices = copyFloat32Buffer(
      terrainCore.memory,
      terrainCore.ofg_mesh_vertex_buffer_ptr(),
      terrainCore.ofg_mesh_vertex_buffer_len()
    );
    const indices = copyUint32Buffer(
      terrainCore.memory,
      terrainCore.ofg_mesh_index_buffer_ptr(),
      terrainCore.ofg_mesh_index_buffer_len()
    );
    terrainCore.ofg_build_water_node_packet_for_variant(
      request.seed,
      request.lod,
      request.x,
      request.y,
      request.z,
      request.cellSize,
      0,
      WATER_NODE_MAX_RELEVANT_DEPTH_METERS
    );
    const waterDepths = copyOptionalWaterDepths(terrainCore);
    const payload: TerrainBuildCompletion = {
      requestId: request.requestId,
      generation: request.generation,
      lod: request.lod,
      x: request.x,
      y: request.y,
      z: request.z,
      variantRevision: request.variantRevision,
      failed: false,
      vertices,
      indices,
      waterTexelCount: terrainCore.ofg_water_node_bathymetry_texel_count(),
      waterOriginX: terrainCore.ofg_water_node_bathymetry_origin_x(),
      waterOriginZ: terrainCore.ofg_water_node_bathymetry_origin_z(),
      waterWorldSpanX: terrainCore.ofg_water_node_bathymetry_world_span_x(),
      waterWorldSpanZ: terrainCore.ofg_water_node_bathymetry_world_span_z(),
      waterSeaLevelMeters: terrainCore.ofg_water_node_bathymetry_sea_level(),
      waterMaxDepthMeters: terrainCore.ofg_water_node_bathymetry_max_depth(),
      waterDepths,
      durationMs: performance.now() - startedAt
    };
    const transfer = [vertices.buffer, indices.buffer];
    if (waterDepths !== undefined) {
      transfer.push(waterDepths.buffer);
    }
    const completion: BrowserWorkerCompletionEnvelope<TerrainBuildCompletion> = {
      type: "complete",
      requestId: envelope.requestId,
      payload
    };
    worker.postMessage(completion, transfer);
  } catch (error) {
    const completion: BrowserWorkerCompletionEnvelope<TerrainBuildCompletion> = {
      type: "error",
      requestId: envelope.requestId,
      message: error instanceof Error ? error.message : String(error)
    };
    worker.postMessage(completion);
  }
}

async function loadTerrainCore(): Promise<TerrainCoreExports> {
  terrainCorePromise ??= fetch(wasmUrl).then(async (response) => {
    if (!response.ok) {
      throw new Error(`Could not load terrain_core.wasm: ${response.status}`);
    }
    const bytes = await response.arrayBuffer();
    const result = await WebAssembly.instantiate(bytes);
    return result.instance.exports as TerrainCoreExports;
  });

  return terrainCorePromise;
}

function copyFloat32Buffer(memory: WebAssembly.Memory, pointer: number, length: number): Float32Array {
  return new Float32Array(memory.buffer, pointer, length).slice();
}

function copyUint32Buffer(memory: WebAssembly.Memory, pointer: number, length: number): Uint32Array {
  return new Uint32Array(memory.buffer, pointer, length).slice();
}

function copyOptionalWaterDepths(terrainCore: TerrainCoreExports): Float32Array | undefined {
  const texelCount = terrainCore.ofg_water_node_bathymetry_texel_count();
  const length = terrainCore.ofg_water_node_bathymetry_buffer_len();
  if (texelCount === 0 || length === 0) {
    return undefined;
  }
  if (length !== texelCount * texelCount) {
    throw new Error(
      `Water bathymetry length ${length} did not match ${texelCount}x${texelCount}.`
    );
  }

  return copyFloat32Buffer(
    terrainCore.memory,
    terrainCore.ofg_water_node_bathymetry_buffer_ptr(),
    length
  );
}

function writeTerrainVariantBuffer(
  terrainCore: TerrainCoreExports,
  terrainVariant: readonly number[]
): void {
  const expectedLength = terrainCore.ofg_terrain_variant_flat_value_count();
  if (terrainVariant.length !== expectedLength) {
    throw new Error(
      `Terrain build request variant length ${terrainVariant.length} did not match terrain_core ${expectedLength}.`
    );
  }

  new Float64Array(
    terrainCore.memory.buffer,
    terrainCore.ofg_terrain_variant_buffer_ptr(),
    expectedLength
  ).set(terrainVariant);
}
