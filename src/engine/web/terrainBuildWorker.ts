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
      durationMs: performance.now() - startedAt
    };
    const transfer = [vertices.buffer, indices.buffer];
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
