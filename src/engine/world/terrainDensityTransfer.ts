import type { TerrainDensityChunkPayload } from "./terrainChunkWorkerTypes.js";

export type TerrainDensityTransferMode = "shared" | "transfer";

export type TerrainDensityTransferModeRequest =
  | TerrainDensityTransferMode
  | "auto";

export function canUseSharedTerrainDensityBuffers(
  environment = globalThis
): boolean {
  const sharedEnvironment = environment as typeof globalThis & {
    readonly SharedArrayBuffer?: typeof SharedArrayBuffer;
    readonly crossOriginIsolated?: boolean;
  };

  return sharedEnvironment.SharedArrayBuffer !== undefined &&
    sharedEnvironment.crossOriginIsolated === true;
}

export function resolveTerrainDensityTransferMode(
  request: TerrainDensityTransferModeRequest = "auto",
  sharedBuffersAvailable = canUseSharedTerrainDensityBuffers()
): TerrainDensityTransferMode {
  if (request !== "auto") {
    return request;
  }

  return sharedBuffersAvailable ? "shared" : "transfer";
}

export function prepareTerrainDensityChunkForWorkerTransfer(
  chunk: TerrainDensityChunkPayload,
  mode: TerrainDensityTransferMode
): TerrainDensityChunkPayload {
  if (mode !== "shared" || isSharedDensityBuffer(chunk.densities.buffer)) {
    return chunk;
  }

  if (typeof SharedArrayBuffer === "undefined") {
    return chunk;
  }

  const sharedBuffer = new SharedArrayBuffer(chunk.densities.byteLength);
  const densities = new Float32Array(sharedBuffer);
  densities.set(chunk.densities);

  return {
    key: chunk.key,
    coord: chunk.coord,
    densities
  };
}

export function terrainDensityChunkTransferList(
  chunks: readonly TerrainDensityChunkPayload[]
): Transferable[] {
  const transfer = new Set<ArrayBuffer>();

  for (const chunk of chunks) {
    const buffer = chunk.densities.buffer;
    if (!isSharedDensityBuffer(buffer)) {
      transfer.add(buffer);
    }
  }

  return [...transfer];
}

export function isSharedDensityBuffer(
  buffer: ArrayBufferLike
): buffer is SharedArrayBuffer {
  return typeof SharedArrayBuffer !== "undefined" &&
    buffer instanceof SharedArrayBuffer;
}
