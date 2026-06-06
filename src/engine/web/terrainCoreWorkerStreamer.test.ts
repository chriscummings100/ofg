import { equal } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { vec3 } from "../math/vec3.js";
import type {
  TerrainRenderChunkInput,
  TerrainRenderChunkPacket,
  TerrainRenderChunkSink
} from "../render/terrainRenderChunkSink.js";
import {
  TerrainCoreDensityChunkStore
} from "../world/terrainCoreDensityChunkStore.js";
import {
  createTerrainCoreStreamScheduler
} from "../world/terrainCoreStreamScheduler.js";
import {
  instantiateTerrainCoreWasm,
  type TerrainCoreWasmInstance
} from "../world/terrainCoreWasm.js";
import {
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "../world/terrainChunk.js";
import type {
  TerrainChunkJobGenerator,
  TerrainDensityJobResult
} from "../world/terrainChunkWorkerTypes.js";
import { createSeedWorldDescriptor } from "../world/terrainDescriptor.js";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";
import {
  TerrainCoreWorkerStreamer,
  type TerrainCoreWorkerStreamerOptions
} from "./terrainCoreWorkerStreamer.js";

describe("TerrainCoreWorkerStreamer", () => {
  it("executes worker jobs selected by the Rust stream scheduler", async () => {
    const terrainCore = await loadTerrainCore();
    const worker = createImmediateWorker();
    const { streamer, renderPackets, densityStore } = createStreamer(terrainCore, worker);

    streamer.syncAround(vec3(0, 0, 0));

    equal(worker.densityRequests.length, 8);
    equal(worker.chunkRequests.length, 0);
    equal(streamer.getStreamStatus().inFlightDensityCount, 8);

    await flushMicrotasks(8);

    equal(worker.chunkRequests.length, 1);
    equal(terrainChunkKey(worker.chunkRequests[0]), "0,0,0");
    equal(renderPackets.size(), 1);
    equal(renderPackets.chunks[0].key, "0,0,0");
    equal(densityStore.size(), 8);
    equal(streamer.getLoadedChunkKeys().length, 8);
    equal(streamer.getStreamStatus().pending, false);
    equal(streamer.getStreamStatus().densityReadyChunkCount, 8);
    equal(streamer.getStreamStatus().renderedChunkCount, 1);
    equal(streamer.getStreamStatus().lastDensityJobStats?.totalMs, 1);
    equal(streamer.getStreamStatus().lastChunkJobStats?.indexCount, 3);
  });

  it("rejects stale worker results through Rust generations after reset", async () => {
    const terrainCore = await loadTerrainCore();
    const worker = createDeferredDensityWorker();
    const { streamer, densityStore } = createStreamer(terrainCore, worker);

    streamer.syncAround(vec3(0, 0, 0));
    equal(worker.densityRequests.length, 8);
    streamer.resetStreaming(vec3(32, 0, 0));
    equal(worker.resetCount, 1);
    equal(worker.densityRequests.length, 16);

    worker.densityRequests[0].resolve();
    await Promise.resolve();

    equal(densityStore.size(), 0);
    equal(streamer.getStreamStatus().generation, 1);
    equal(streamer.getStreamStatus().inFlightDensityCount, 8);
  });

  it("prunes Rust density and mesh packet stores when the desired center changes", async () => {
    const terrainCore = await loadTerrainCore();
    const worker = createImmediateWorker();
    const { streamer, renderPackets, densityStore } = createStreamer(terrainCore, worker);

    streamer.syncAround(vec3(0, 0, 0));
    await flushMicrotasks(8);
    equal(renderPackets.size(), 1);
    equal(densityStore.size(), 8);

    streamer.syncAround(vec3(32, 0, 0));

    equal(renderPackets.size(), 0);
    equal(densityStore.size(), 4);
    equal(streamer.getLoadedChunkKeys().includes("1,0,0"), true);
    equal(streamer.getLoadedChunkKeys().includes("0,0,0"), false);
  });
});

function createStreamer(
  terrainCore: TerrainCoreWasmInstance,
  worker: TerrainChunkJobGenerator,
  options: TerrainCoreWorkerStreamerOptions = {}
): {
  readonly streamer: TerrainCoreWorkerStreamer;
  readonly renderPackets: RecordingTerrainRenderChunkSink;
  readonly densityStore: TerrainCoreDensityChunkStore;
} {
  const descriptor = createSeedWorldDescriptor(0x0F6);
  const scheduler = createTerrainCoreStreamScheduler(terrainCore, {
    horizontalRadius: 0,
    verticalChunkOffsets: [0],
    maxInFlightJobs: 8
  });
  const densityStore = new TerrainCoreDensityChunkStore(terrainCore, descriptor);
  const renderPackets = new RecordingTerrainRenderChunkSink();
  const streamer = new TerrainCoreWorkerStreamer(
    renderPackets,
    scheduler,
    densityStore,
    worker,
    {
      ...options
    }
  );

  return { streamer, renderPackets, densityStore };
}

class RecordingTerrainRenderChunkSink implements TerrainRenderChunkSink {
  private readonly packets = new Map<TerrainChunkKey, TerrainRenderChunkPacket>();

  get chunks(): readonly TerrainRenderChunkPacket[] {
    return [...this.packets.values()].sort((a, b) => a.key.localeCompare(b.key));
  }

  addChunk(chunk: TerrainRenderChunkInput): void {
    this.packets.set(chunk.key, {
      key: chunk.key,
      mesh: "mesh" in chunk ? chunk.mesh : {
        vertices: chunk.vertices,
        indices: chunk.indices
      }
    });
  }

  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean {
    return this.packets.delete(toChunkKey(chunk));
  }

  clear(): void {
    this.packets.clear();
  }

  retainChunks(chunks: readonly (TerrainChunkKey | TerrainChunkCoord)[]): void {
    const retained = new Set(chunks.map(toChunkKey));
    for (const key of this.packets.keys()) {
      if (!retained.has(key)) {
        this.packets.delete(key);
      }
    }
  }

  size(): number {
    return this.packets.size;
  }
}

function toChunkKey(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainChunkKey {
  return typeof chunk === "string" ? chunk : terrainChunkKey(chunk);
}

function createImmediateWorker(): TerrainChunkJobGenerator & {
  readonly densityRequests: TerrainDensityJobResult[];
  readonly chunkRequests: TerrainChunkCoord[];
} {
  const densityRequests: TerrainDensityJobResult[] = [];
  const chunkRequests: TerrainChunkCoord[] = [];

  return {
    workerCount: 8,
    densityRequests,
    chunkRequests,
    async prepareDensityChunk(request) {
      const result = {
        generation: request.generation,
        coord: request.coord,
        densities: createDensitySamples(densitySampleMarker(request.coord)),
        stats: { totalMs: 1 }
      };
      densityRequests.push(result);
      return result;
    },
    async generateChunk(request) {
      chunkRequests.push(request.coord);
      return {
        generation: request.generation,
        coord: request.coord,
        ...createTriangleMeshData(),
        stats: {
          totalMs: 3,
          vertexCount: 3,
          indexCount: 3
        }
      };
    }
  };
}

function createDeferredDensityWorker(): TerrainChunkJobGenerator & {
  resetCount: number;
  readonly densityRequests: Array<{ readonly resolve: () => void }>;
} {
  const densityRequests: Array<{ readonly resolve: () => void }> = [];
  return {
    workerCount: 8,
    resetCount: 0,
    reset() {
      this.resetCount += 1;
    },
    prepareDensityChunk(request) {
      return new Promise((resolve) => {
        densityRequests.push({
          resolve() {
            resolve({
              generation: request.generation,
              coord: request.coord,
              densities: createDensitySamples(),
              stats: { totalMs: 1 }
            });
          }
        });
      });
    },
    async generateChunk(request) {
      return {
        generation: request.generation,
        coord: request.coord,
        ...createTriangleMeshData(),
        stats: {
          totalMs: 3,
          vertexCount: 3,
          indexCount: 3
        }
      };
    },
    densityRequests
  };
}

function createTriangleMeshData() {
  return {
    vertices: new Float32Array([
      0, 0, 0, 0.3, 0.5, 0.4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
      1, 0, 0, 0.3, 0.5, 0.4, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
      0, 0, 1, 0.3, 0.5, 0.4, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0
    ]),
    indices: new Uint32Array([0, 1, 2])
  };
}

function createDensitySamples(firstSample = 0): Float32Array {
  const samples = new Float32Array(33 * 33 * 33);
  samples[0] = firstSample;

  return samples;
}

function densitySampleMarker(coord: TerrainChunkCoord): number {
  return coord.x + coord.y * 10 + coord.z * 100;
}

async function flushMicrotasks(count: number): Promise<void> {
  for (let index = 0; index < count; index += 1) {
    await Promise.resolve();
  }
}

async function loadTerrainCore(): Promise<TerrainCoreWasmInstance> {
  const bytes = readFileSync(TERRAIN_CORE_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;

  return instantiateTerrainCoreWasm(wasmBytes);
}
