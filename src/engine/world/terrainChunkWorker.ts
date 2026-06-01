import { terrainChunkKey } from "./terrainChunk.js";
import { generateTerrainChunkMeshWithWasm } from "./terrainCoreChunkMesh.js";
import { loadTerrainCoreWasm, type TerrainCoreWasmInstance } from "./terrainCoreWasm.js";
import type {
  TerrainChunkJobResult,
  TerrainWorkerChunkJobRequest,
  TerrainWorkerMessage,
  TerrainWorkerRequestMessage
} from "./terrainChunkWorkerTypes.js";

let terrainCorePromise: Promise<TerrainCoreWasmInstance> | undefined;
const workerSelf = self as unknown as {
  addEventListener(
    type: "message",
    listener: (event: MessageEvent<TerrainWorkerRequestMessage>) => void
  ): void;
  postMessage(message: TerrainWorkerMessage, options?: { readonly transfer: Transferable[] }): void;
};

workerSelf.addEventListener("message", (event: MessageEvent<TerrainWorkerRequestMessage>) => {
  const message = event.data;
  if (message.type !== "generateChunk") {
    return;
  }

  void generateChunk(message.request)
    .then((result) => {
      workerSelf.postMessage({
        type: "chunkResult",
        requestId: message.requestId,
        result
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

async function generateChunk(
  request: TerrainWorkerChunkJobRequest
): Promise<TerrainChunkJobResult> {
  const terrainCore = await loadWorkerTerrainCore();
  const startedAt = performance.now();
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
