import { equal, notEqual, ok } from "node:assert/strict";
import { terrainChunkCoord } from "./terrainChunk.js";
import {
  canUseSharedTerrainDensityBuffers,
  isSharedDensityBuffer,
  prepareTerrainDensityChunkForWorkerTransfer,
  resolveTerrainDensityTransferMode,
  terrainDensityChunkTransferList
} from "./terrainDensityTransfer.js";

describe("terrainDensityTransfer", () => {
  it("resolves auto mode from shared-memory availability", () => {
    equal(resolveTerrainDensityTransferMode("auto", true), "shared");
    equal(resolveTerrainDensityTransferMode("auto", false), "transfer");
    equal(resolveTerrainDensityTransferMode("shared", false), "shared");
    equal(resolveTerrainDensityTransferMode("transfer", true), "transfer");
  });

  it("requires cross-origin isolation before selecting browser shared buffers", () => {
    if (typeof SharedArrayBuffer === "undefined") {
      equal(canUseSharedTerrainDensityBuffers({
        crossOriginIsolated: true
      } as typeof globalThis), false);
      return;
    }

    equal(canUseSharedTerrainDensityBuffers({
      SharedArrayBuffer,
      crossOriginIsolated: true
    } as typeof globalThis), true);
    equal(canUseSharedTerrainDensityBuffers({
      SharedArrayBuffer,
      crossOriginIsolated: false
    } as typeof globalThis), false);
  });

  it("copies density payloads into shared buffers when requested", () => {
    if (typeof SharedArrayBuffer === "undefined") {
      return;
    }

    const chunk = createDensityPayload();
    const sharedChunk = prepareTerrainDensityChunkForWorkerTransfer(chunk, "shared");

    notEqual(sharedChunk.densities.buffer, chunk.densities.buffer);
    ok(isSharedDensityBuffer(sharedChunk.densities.buffer));
    equal(sharedChunk.densities[0], 1.5);
    chunk.densities[0] = 9;
    equal(sharedChunk.densities[0], 1.5);
  });

  it("does not transfer shared density buffers through postMessage transfer lists", () => {
    if (typeof SharedArrayBuffer === "undefined") {
      return;
    }

    const sharedChunk = prepareTerrainDensityChunkForWorkerTransfer(
      createDensityPayload(),
      "shared"
    );

    equal(terrainDensityChunkTransferList([sharedChunk]).length, 0);
  });

  it("uses transferable ArrayBuffers for fallback density payloads", () => {
    const chunk = createDensityPayload();
    const transfer = terrainDensityChunkTransferList([chunk]);

    equal(transfer.length, 1);
    equal(transfer[0], chunk.densities.buffer);
  });
});

function createDensityPayload() {
  return {
    key: "0,0,0",
    coord: terrainChunkCoord(0, 0, 0),
    densities: new Float32Array([1.5, -2, 3])
  };
}
