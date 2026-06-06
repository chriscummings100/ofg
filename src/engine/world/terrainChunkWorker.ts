import { terrainChunkKey } from "./terrainChunk.js";
import {
  generateTerrainChunkMeshWithWasm,
  installTerrainCoreDensityChunk
} from "./terrainCoreChunkMesh.js";
import { generateTerrainDensityChunkWithWasm } from "./terrainCoreDensityChunk.js";
import { loadTerrainCoreWasm, type TerrainCoreWasmInstance } from "./terrainCoreWasm.js";
import type {
  BrowserWorkerCompletionEnvelope,
  BrowserWorkerRequestEnvelope
} from "../browser/browserWorkerHost.js";
import type {
  TerrainChunkJobResult,
  TerrainDensityJobResult,
  TerrainWorkerChunkJobRequest,
  TerrainWorkerDensityJobRequest,
  TerrainWorkerRequestPayload,
  TerrainWorkerResultPayload
} from "./terrainChunkWorkerTypes.js";

let terrainCorePromise: Promise<TerrainCoreWasmInstance> | undefined;
const workerSelf = self as unknown as {
  addEventListener(
    type: "message",
    listener: (
      event: MessageEvent<BrowserWorkerRequestEnvelope<TerrainWorkerRequestPayload>>
    ) => void
  ): void;
  postMessage(
    message: BrowserWorkerCompletionEnvelope<TerrainWorkerResultPayload>,
    options?: { readonly transfer: Transferable[] }
  ): void;
};

workerSelf.addEventListener("message", (
  event: MessageEvent<BrowserWorkerRequestEnvelope<TerrainWorkerRequestPayload>>
) => {
  const message = event.data;
  const payload = message.payload;

  if (payload.type === "prepareDensityChunk") {
    void prepareDensityChunk(payload.request)
      .then((result) => {
        workerSelf.postMessage({
          type: "complete",
          requestId: message.requestId,
          payload: {
            type: "densityResult",
            result
          }
        }, {
          transfer: [
            result.densities.buffer
          ]
        });
      })
      .catch((error: unknown) => {
        workerSelf.postMessage({
          type: "error",
          requestId: message.requestId,
          message: error instanceof Error ? error.message : String(error)
        });
      });
    return;
  }

  void generateChunk(payload.request)
    .then((result) => {
      workerSelf.postMessage({
        type: "complete",
        requestId: message.requestId,
        payload: {
          type: "chunkResult",
          result
        }
      }, {
        transfer: [
          result.vertices.buffer,
          result.indices.buffer
        ]
      });
    })
    .catch((error: unknown) => {
      workerSelf.postMessage({
        type: "error",
        requestId: message.requestId,
        message: error instanceof Error ? error.message : String(error)
      });
    });
});

async function prepareDensityChunk(
  request: TerrainWorkerDensityJobRequest
): Promise<TerrainDensityJobResult> {
  const terrainCore = await loadWorkerTerrainCore();
  const startedAt = performance.now();
  const chunk = generateTerrainDensityChunkWithWasm(
    terrainCore,
    request.descriptor,
    request.coord,
    { cellSize: request.cellSize }
  );
  const finishedAt = performance.now();

  return {
    generation: request.generation,
    key: terrainChunkKey(request.coord),
    coord: request.coord,
    densities: chunk.densities,
    stats: {
      totalMs: finishedAt - startedAt
    }
  };
}

async function generateChunk(
  request: TerrainWorkerChunkJobRequest
): Promise<TerrainChunkJobResult> {
  const terrainCore = await loadWorkerTerrainCore();
  const startedAt = performance.now();
  terrainCore.exports.ofg_reset_density_chunk_store();
  for (const densityChunk of request.densityChunks) {
    installTerrainCoreDensityChunk(
      terrainCore,
      request.descriptor,
      densityChunk,
      request.cellSize
    );
  }

  const mesh = generateTerrainChunkMeshWithWasm(
    terrainCore,
    request.descriptor,
    request.coord,
    request.cellSize
  );
  const finishedAt = performance.now();

  return {
    generation: request.generation,
    key: terrainChunkKey(request.coord),
    vertices: mesh.vertices,
    indices: mesh.indices,
    stats: {
      totalMs: finishedAt - startedAt,
      vertexCount: mesh.vertices.length,
      indexCount: mesh.indices.length
    }
  };
}

async function loadWorkerTerrainCore(): Promise<TerrainCoreWasmInstance> {
  terrainCorePromise ??= loadTerrainCoreWasm("/assets/wasm/terrain_core.wasm");

  return terrainCorePromise;
}
