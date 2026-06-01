import { equal, ok, throws } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { vec3 } from "../../engine/math/vec3.js";
import { TerrainRenderer } from "../../engine/render/TerrainRenderer.js";
import { resetScene } from "../../engine/scene/activeScene.js";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";
import type { TerrainField } from "../../engine/world/scalarField.js";
import {
  generateTerrainDensityChunk,
  terrainChunkKey,
  type TerrainChunkCoord
} from "../../engine/world/terrainChunk.js";
import { createTerrainCoreStreamScheduler } from "../../engine/world/terrainCoreStreamScheduler.js";
import type { TerrainDensityChunkStore } from "../../engine/world/terrainCoreDensityChunkStore.js";
import { instantiateTerrainCoreWasm } from "../../engine/world/terrainCoreWasm.js";
import { TerrainChunkStreamer } from "./TerrainChunkStreamer.js";

describe("TerrainChunkStreamer", () => {
  it("generates a render chunk around the target entity", () => {
    const scene = resetScene();
    const target = scene.createEntity("Player");
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      target,
      material: "material:terrain",
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(target.transform.getWorldPosition());

    equal(streamer.getLoadedChunkKeys().length, 8);
    equal(streamer.getLoadedChunkKeys().includes("0,0,0"), true);
    equal(streamer.getLoadedChunkKeys().includes("1,1,1"), true);
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,0,0");
    equal(terrain.chunks[0].material, "material:terrain");
    equal(terrain.chunks[0].mesh.id, "mesh:terrain.chunk:0,0,0");
    ok(terrain.chunks[0].mesh.indices.length > 0);
  });

  it("generates square xz neighborhoods for every requested vertical chunk offset", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 1,
      verticalChunkOffsets: [-1, 0]
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(streamer.getLoadedChunkKeys().length, 48);
    equal(streamer.getLoadedChunkKeys().includes("-1,0,-1"), true);
    equal(streamer.getLoadedChunkKeys().includes("1,-1,1"), true);
    equal(streamer.getLoadedChunkKeys().includes("2,1,2"), true);
    equal(terrain.chunks.length, 9);
    equal(terrain.chunks.every((chunk) => streamer.getLoadedChunkKeys().includes(chunk.key)), true);
    equal(terrain.chunks.some((chunk) => chunk.key === "0,0,0"), true);
  });

  it("moves the loaded chunk window as the target crosses chunk boundaries", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));
    const firstMesh = terrain.chunks[0].mesh;
    streamer.syncAround(vec3(1, 0, 1));

    equal(terrain.chunks[0].mesh, firstMesh);

    streamer.syncAround(vec3(32, 0, 0));

    equal(streamer.getLoadedChunkKeys().length, 8);
    equal(streamer.getLoadedChunkKeys().includes("1,0,0"), true);
    equal(streamer.getLoadedChunkKeys().includes("2,1,1"), true);
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "1,0,0");
    equal(terrain.getChunk("0,0,0"), undefined);
  });

  it("skips render chunks with no surface while remembering they were loaded", () => {
    let sampleCount = 0;
    const source: TerrainField = {
      heightAt: () => 0,
      densityAt: () => {
        sampleCount += 1;
        return 1;
      },
      normalAt: () => vec3(0, 1, 0)
    };
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));
    streamer.syncAround(vec3(1, 0, 1));

    equal(streamer.getLoadedChunkKeys().length, 8);
    equal(streamer.getLoadedChunkKeys().includes("0,0,0"), true);
    equal(terrain.chunks.length, 0);
    equal(sampleCount, 8 * 33 * 33 * 33);
  });

  it("uses a custom density chunk generator when provided", () => {
    const generatedKeys: string[] = [];
    const source: TerrainField = {
      heightAt: () => 0,
      densityAt: () => 1,
      normalAt: () => vec3(0, 1, 0)
    };
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      densityChunkGenerator(generatorSource, coord, options) {
        generatedKeys.push(terrainChunkKey(coord));
        return generateTerrainDensityChunk(generatorSource, coord, options);
      }
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(generatedKeys.length, 8);
    equal(generatedKeys.includes("0,0,0"), true);
    equal(generatedKeys.includes("1,1,1"), true);
    equal(terrain.chunks.length, 0);
  });

  it("uses a custom chunk mesh generator when provided", () => {
    const generatedKeys: string[] = [];
    const preparedKeys: string[] = [];
    const source = createFlatField(0);
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      prepareDensityChunks(coords) {
        preparedKeys.push(...coords.map(terrainChunkKey));
      },
      chunkMeshGenerator(coord) {
        generatedKeys.push(terrainChunkKey(coord));
        return {
          vertices: new Float32Array([
            0, 0, 0, 0.3, 0.5, 0.4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
            1, 0, 0, 0.3, 0.5, 0.4, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
            0, 0, 1, 0.3, 0.5, 0.4, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0
          ]),
          indices: new Uint32Array([0, 1, 2])
        };
      }
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(generatedKeys.join(","), "0,0,0");
    equal(preparedKeys.length, 8);
    equal(preparedKeys.includes("0,0,0"), true);
    equal(preparedKeys.includes("1,1,1"), true);
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].mesh.indices.length, 3);
  });

  it("applies async chunk jobs from a worker-style generator", async () => {
    const source = createFlatField(0);
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxConcurrentChunkJobs: 8,
      chunkJobGenerator: {
        async prepareDensityChunk(request) {
          return {
            generation: request.generation,
            key: terrainChunkKey(request.coord),
            coord: request.coord,
            densities: createDensitySamples(),
            stats: { totalMs: 1 }
          };
        },
        async generateChunk(request) {
          equal(request.densityChunks.length, 8);
          return {
            generation: request.generation,
            key: terrainChunkKey(request.coord),
            ...createTriangleMeshData(),
            stats: {
              totalMs: 3,
              vertexCount: 3,
              indexCount: 3
            }
          };
        }
      }
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(terrain.chunks.length, 0);
    equal(streamer.getStreamStatus().pending, true);
    await flushMicrotasks(4);

    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,0,0");
    equal(streamer.getStreamStatus().pending, false);
    equal(streamer.getStreamStatus().densityReadyChunkCount, 8);
    equal(streamer.getStreamStatus().sharedDensityChunkCount, 8);
    equal(streamer.getStreamStatus().lastDensityJobStats?.totalMs, 1);
    equal(streamer.getStreamStatus().lastChunkJobStats?.indexCount, 3);
  });

  it("can delegate async job selection to the Rust stream scheduler", async () => {
    const terrainCore = await loadTerrainCore();
    const source = createFlatField(0);
    const terrain = new TerrainRenderer(source);
    const streamScheduler = createTerrainCoreStreamScheduler(terrainCore, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxInFlightJobs: 8
    });
    const generatedDensityKeys: string[] = [];
    const generatedChunkKeys: string[] = [];
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxConcurrentChunkJobs: 8,
      streamScheduler,
      chunkJobGenerator: {
        async prepareDensityChunk(request) {
          generatedDensityKeys.push(terrainChunkKey(request.coord));
          return {
            generation: request.generation,
            key: terrainChunkKey(request.coord),
            coord: request.coord,
            densities: createDensitySamples(),
            stats: { totalMs: 1 }
          };
        },
        async generateChunk(request) {
          generatedChunkKeys.push(terrainChunkKey(request.coord));
          equal(request.densityChunks.length, 8);
          return {
            generation: request.generation,
            key: terrainChunkKey(request.coord),
            ...createTriangleMeshData(),
            stats: {
              totalMs: 3,
              vertexCount: 3,
              indexCount: 3
            }
          };
        }
      }
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(streamer.getLoadedChunkKeys().length, 8);
    equal(generatedDensityKeys.length, 8);
    equal(streamer.getStreamStatus().inFlightDensityCount, 8);
    await flushMicrotasks(4);

    equal(generatedChunkKeys.join(","), "0,0,0");
    equal(terrain.chunks.length, 1);
    equal(streamer.getStreamStatus().generation, 0);
    equal(streamer.getStreamStatus().densityReadyChunkCount, 8);
    equal(streamer.getStreamStatus().renderedChunkCount, 1);
    equal(streamer.getStreamStatus().pending, false);
  });

  it("uses an injected density store for scheduler-backed mesh dependencies", async () => {
    const terrainCore = await loadTerrainCore();
    const source = createFlatField(0);
    const terrain = new TerrainRenderer(source);
    const densityStore = new RecordingDensityChunkStore();
    const streamScheduler = createTerrainCoreStreamScheduler(terrainCore, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxInFlightJobs: 8
    });
    const generatedChunkKeys: string[] = [];
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxConcurrentChunkJobs: 8,
      streamScheduler,
      densityChunkStore: densityStore,
      chunkJobGenerator: {
        async prepareDensityChunk(request) {
          return {
            generation: request.generation,
            key: terrainChunkKey(request.coord),
            coord: request.coord,
            densities: createDensitySamples(densitySampleMarker(request.coord)),
            stats: { totalMs: 1 }
          };
        },
        async generateChunk(request) {
          generatedChunkKeys.push(terrainChunkKey(request.coord));
          equal(request.densityChunks.length, 8);
          equal(densityStore.storedKeys.length, 8);
          equal(densityStore.loadedKeys.length, 8);
          equal(request.densityChunks[0].densities[0], densitySampleMarker(request.densityChunks[0].coord));

          return {
            generation: request.generation,
            key: terrainChunkKey(request.coord),
            ...createTriangleMeshData(),
            stats: {
              totalMs: 3,
              vertexCount: 3,
              indexCount: 3
            }
          };
        }
      }
    });

    streamer.syncAround(vec3(0, 0, 0));
    await flushMicrotasks(4);

    equal(generatedChunkKeys.join(","), "0,0,0");
    equal(streamer.getStreamStatus().sharedDensityChunkCount, 8);
    equal(terrain.chunks.length, 1);

    streamer.invalidateAll();

    equal(densityStore.size(), 0);
    equal(streamer.getStreamStatus().sharedDensityChunkCount, 0);
  });

  it("ignores stale async chunk results after a streaming reset", async () => {
    const source = createFlatField(0);
    const terrain = new TerrainRenderer(source);
    const requests: Array<{
      readonly generation: number;
      readonly resolve: (key: string) => void;
    }> = [];
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxConcurrentChunkJobs: 8,
      chunkJobGenerator: {
        async prepareDensityChunk(request) {
          return {
            generation: request.generation,
            key: terrainChunkKey(request.coord),
            coord: request.coord,
            densities: createDensitySamples(),
            stats: { totalMs: 1 }
          };
        },
        generateChunk(request) {
          return new Promise((resolve) => {
            requests.push({
              generation: request.generation,
              resolve(key: string) {
                resolve({
                  generation: request.generation,
                  key,
                  ...createTriangleMeshData(),
                  stats: {
                    totalMs: 3,
                    vertexCount: 3,
                    indexCount: 3
                  }
                });
              }
            });
          });
        }
      }
    });

    streamer.syncAround(vec3(0, 0, 0));
    await flushMicrotasks(4);
    streamer.resetStreaming(vec3(32, 0, 0));
    await flushMicrotasks(4);

    equal(requests.length, 2);
    requests[0].resolve("0,0,0");
    await Promise.resolve();
    equal(terrain.chunks.length, 0);

    requests[1].resolve("1,0,0");
    await Promise.resolve();
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "1,0,0");
  });

  it("keeps newer same-key density work in flight when stale results arrive", async () => {
    const source = createFlatField(0);
    const terrain = new TerrainRenderer(source);
    const densityRequests: Array<{
      readonly resolve: () => void;
    }> = [];
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxConcurrentChunkJobs: 8,
      chunkJobGenerator: {
        prepareDensityChunk(request) {
          const key = terrainChunkKey(request.coord);
          return new Promise((resolve) => {
            densityRequests.push({
              resolve() {
                resolve({
                  generation: request.generation,
                  key,
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
            key: terrainChunkKey(request.coord),
            ...createTriangleMeshData(),
            stats: {
              totalMs: 3,
              vertexCount: 3,
              indexCount: 3
            }
          };
        }
      }
    });

    streamer.syncAround(vec3(0, 0, 0));
    equal(densityRequests.length, 8);

    streamer.resetStreaming(vec3(0, 0, 0));
    equal(densityRequests.length, 16);

    densityRequests[0].resolve();
    await Promise.resolve();

    equal(densityRequests.length, 16);
    equal(streamer.getStreamStatus().inFlightDensityCount, 8);
    equal(streamer.getStreamStatus().sharedDensityChunkCount, 0);
  });

  it("submits nearest async chunk jobs up to the concurrency limit", async () => {
    const source = createFlatField(0);
    const terrain = new TerrainRenderer(source);
    const densityRequests: Array<{
      readonly key: string;
      readonly resolve: () => void;
    }> = [];
    const chunkRequests: Array<{
      readonly key: string;
      readonly resolve: () => void;
    }> = [];
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 1,
      verticalChunkOffsets: [0],
      maxConcurrentChunkJobs: 2,
      chunkJobGenerator: {
        workerCount: 2,
        prepareDensityChunk(request) {
          const key = terrainChunkKey(request.coord);
          return new Promise((resolve) => {
            densityRequests.push({
              key,
              resolve() {
                resolve({
                  generation: request.generation,
                  key,
                  coord: request.coord,
                  densities: createDensitySamples(),
                  stats: { totalMs: 1 }
                });
              }
            });
          });
        },
        generateChunk(request) {
          const key = terrainChunkKey(request.coord);
          return new Promise((resolve) => {
            chunkRequests.push({
              key,
              resolve() {
                resolve({
                  generation: request.generation,
                  key,
                  ...createTriangleMeshData(),
                  stats: {
                    totalMs: 3,
                    vertexCount: 3,
                    indexCount: 3
                  }
                });
              }
            });
          });
        }
      }
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(densityRequests.length, 2);
    equal(densityRequests[0].key, "0,0,0");
    equal(streamer.getStreamStatus().inFlightDensityCount, 2);
    equal(streamer.getStreamStatus().inFlightChunkCount, 0);

    for (let index = 0; index < densityRequests.length; index += 1) {
      densityRequests[index].resolve();
      await Promise.resolve();
    }
    await flushMicrotasks(40);

    equal(streamer.getStreamStatus().densityReadyChunkCount, streamer.getStreamStatus().loadedChunkCount);
    equal(chunkRequests.length, 2);
    equal(chunkRequests[0].key, "0,0,0");
    equal(streamer.getStreamStatus().inFlightChunkCount, 2);

    chunkRequests[0].resolve();
    await Promise.resolve();

    equal(terrain.chunks.length, 1);
    equal(chunkRequests.length, 3);
    equal(streamer.getStreamStatus().inFlightChunkCount, 2);
  });

  it("uses terrain density sample gradients when building chunk meshes", () => {
    const source: TerrainField = {
      heightAt: () => 0,
      densityAt: (position) => position.y,
      sampleAt: (position) => ({
        density: position.y,
        gradient: vec3(0, 0, 2)
      }),
      normalAt: () => vec3(0, 1, 0)
    };
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].mesh.vertices[6], 0);
    equal(terrain.chunks[0].mesh.vertices[7], 0);
    equal(terrain.chunks[0].mesh.vertices[8], 1);
  });

  it("uses stable centroid placement for runtime Dual Contouring vertices", () => {
    const source: TerrainField = {
      heightAt: () => 0.5,
      densityAt: (position) => position.y - 0.5,
      sampleAt: (position) => ({
        density: position.y - 0.5,
        gradient: stressNormalForPlacement(position)
      }),
      normalAt: () => vec3(0, 1, 0)
    };
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].mesh.vertices[0], 0.5);
    equal(terrain.chunks[0].mesh.vertices[1], 0.5);
    equal(terrain.chunks[0].mesh.vertices[2], 0.5);
  });

  it("can rebuild an already loaded chunk", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });
    streamer.syncAround(vec3(0, 0, 0));
    const firstMesh = terrain.chunks[0].mesh;

    streamer.rebuildChunk("0,0,0");

    equal(streamer.getLoadedChunkKeys().length, 8);
    equal(streamer.getLoadedChunkKeys().includes("0,0,0"), true);
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,0,0");
    ok(terrain.chunks[0].mesh !== firstMesh);
  });

  it("can invalidate all streamed chunks", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 1,
      verticalChunkOffsets: [0]
    });
    streamer.syncAround(vec3(0, 0, 0));

    streamer.invalidateAll();

    equal(streamer.getLoadedChunkKeys().length, 0);
    equal(terrain.chunks.length, 0);
  });

  it("updates from scene traversal when attached as a component", () => {
    const scene = resetScene();
    const target = scene.createEntity("Player");
    const terrainEntity = scene.createEntity("Terrain");
    const terrain = terrainEntity.addComponent(new TerrainRenderer(createFlatField(0)));
    terrainEntity.addComponent(new TerrainChunkStreamer(terrain, createFlatField(0), {
      target,
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    }));

    scene.update(0);

    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,0,0");
  });

  it("validates streaming options", () => {
    const terrain = new TerrainRenderer(createFlatField(0));

    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: -1
    }), /horizontalRadius/);
    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      verticalChunkOffsets: []
    }), /verticalChunkOffsets/);
    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      verticalChunkOffsets: [0, 0]
    }), /verticalChunkOffsets/);
    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      cellSize: 0
    }), /cellSize/);
  });

  it("centers vertical chunk offsets on the target y coordinate", () => {
    const terrain = new TerrainRenderer(createFlatField(-70));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(-70), {
      horizontalRadius: 0,
      verticalChunkOffsets: [-1, 0, 1]
    });

    streamer.syncAround(vec3(0, -70, 0));

    equal(streamer.getLoadedChunkKeys().length, 16);
    equal(streamer.getLoadedChunkKeys().includes("0,-3,0"), true);
    equal(streamer.getLoadedChunkKeys().includes("1,-1,1"), true);
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,-3,0");
    ok(terrain.chunks[0].mesh.indices.length > 0);
  });
});

function createFlatField(height: number): TerrainField {
  return {
    heightAt: () => height,
    densityAt: (position) => position.y - height,
    normalAt: () => vec3(0, 1, 0)
  };
}

function stressNormalForPlacement(position: { readonly x: number; readonly z: number }) {
  if (position.x < 0.5 && position.z < 0.5) {
    return vec3(1, 0, 0);
  }

  if (position.z > 0.5) {
    return vec3(0, 0, 1);
  }

  return vec3(0, 1, 0);
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

class RecordingDensityChunkStore implements TerrainDensityChunkStore {
  readonly runtime = "rust" as const;
  readonly storedKeys: string[] = [];
  readonly loadedKeys: string[] = [];
  private readonly chunks = new Map<string, Float32Array>();

  clear(): void {
    this.chunks.clear();
  }

  size(): number {
    return this.chunks.size;
  }

  retainOnly(coords: readonly TerrainChunkCoord[], _cellSize: number): void {
    const keep = new Set(coords.map(terrainChunkKey));
    for (const key of [...this.chunks.keys()]) {
      if (!keep.has(key)) {
        this.chunks.delete(key);
      }
    }
  }

  has(coord: TerrainChunkCoord, _cellSize: number): boolean {
    return this.chunks.has(terrainChunkKey(coord));
  }

  store(chunk: { readonly key: string; readonly densities: Float32Array }, _cellSize: number): void {
    this.storedKeys.push(chunk.key);
    this.chunks.set(chunk.key, new Float32Array(chunk.densities));
  }

  get(
    coord: TerrainChunkCoord,
    _cellSize: number
  ): { readonly key: string; readonly coord: TerrainChunkCoord; readonly densities: Float32Array } | undefined {
    const key = terrainChunkKey(coord);
    const densities = this.chunks.get(key);
    if (densities === undefined) {
      return undefined;
    }

    this.loadedKeys.push(key);
    return {
      key,
      coord,
      densities: new Float32Array(densities)
    };
  }
}

async function flushMicrotasks(count: number): Promise<void> {
  for (let index = 0; index < count; index += 1) {
    await Promise.resolve();
  }
}

async function loadTerrainCore() {
  const bytes = readFileSync(TERRAIN_CORE_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;

  return instantiateTerrainCoreWasm(wasmBytes);
}
