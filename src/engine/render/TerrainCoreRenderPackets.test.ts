import { equal, notEqual, throws } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { terrainChunkCoord } from "../world/terrainChunk.js";
import {
  instantiateTerrainCoreWasm,
  type TerrainCoreWasmInstance
} from "../world/terrainCoreWasm.js";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";
import { TerrainCoreRenderPacketStore } from "./TerrainCoreRenderPackets.js";

describe("TerrainCoreRenderPacketStore", () => {
  it("stores terrain chunk packets in Rust and exposes terrain render source data", async () => {
    const terrainCore = await loadTerrainCore();
    const store = new TerrainCoreRenderPacketStore(terrainCore, {
      itemIdPrefix: "terrain:rust",
      meshIdPrefix: "mesh:terrain.chunk"
    });

    store.addChunk({
      key: "0,0,0",
      meshId: "mesh:ignored",
      ...createTriangleMeshData(),
      floatsPerVertex: 19
    });

    equal(store.runtime, "rust");
    equal(store.size(), 1);
    equal(store.chunks.length, 1);
    equal(store.chunks[0].key, "0,0,0");
    equal(store.chunks[0].mesh.id, "mesh:terrain.chunk:0,0,0");

    equal(store.itemIdPrefix, "terrain:rust");
  });

  it("removes Rust-owned terrain chunk packets by key or coord", async () => {
    const terrainCore = await loadTerrainCore();
    const store = new TerrainCoreRenderPacketStore(terrainCore);
    store.addChunk({
      key: "2,-1,3",
      meshId: "mesh:terrain.chunk:2,-1,3",
      ...createTriangleMeshData(),
      floatsPerVertex: 19
    });

    equal(store.removeChunk(terrainChunkCoord(2, -1, 3)), true);
    equal(store.removeChunk("2,-1,3"), false);
    equal(store.size(), 0);
    equal(store.chunks.length, 0);
  });

  it("retains only requested Rust-owned terrain chunk packets", async () => {
    const terrainCore = await loadTerrainCore();
    const store = new TerrainCoreRenderPacketStore(terrainCore);
    store.addChunk({
      key: "0,0,0",
      meshId: "mesh:terrain.chunk:0,0,0",
      ...createTriangleMeshData(0),
      floatsPerVertex: 19
    });
    store.addChunk({
      key: "1,0,0",
      meshId: "mesh:terrain.chunk:1,0,0",
      ...createTriangleMeshData(1),
      floatsPerVertex: 19
    });
    store.addChunk({
      key: "2,0,0",
      meshId: "mesh:terrain.chunk:2,0,0",
      ...createTriangleMeshData(2),
      floatsPerVertex: 19
    });

    store.retainChunks(["1,0,0", terrainChunkCoord(2, 0, 0)]);

    equal(store.size(), 2);
    equal(store.chunks.length, 2);
    equal(store.chunks[0].key, "1,0,0");
    equal(store.chunks[1].key, "2,0,0");
  });

  it("keeps cached render mesh packets stable until the Rust store version changes", async () => {
    const terrainCore = await loadTerrainCore();
    const store = new TerrainCoreRenderPacketStore(terrainCore);
    store.addChunk({
      key: "0,0,0",
      meshId: "mesh:terrain.chunk:0,0,0",
      ...createTriangleMeshData(1),
      floatsPerVertex: 19
    });
    const firstMesh = store.chunks[0].mesh;

    equal(store.chunks[0].mesh, firstMesh);

    store.addChunk({
      key: "0,0,0",
      meshId: "mesh:terrain.chunk:0,0,0",
      ...createTriangleMeshData(2),
      floatsPerVertex: 19
    });

    notEqual(store.chunks[0].mesh, firstMesh);
    equal(store.chunks[0].mesh.vertices[0], 2);
  });

  it("syncs multiple Rust packets after WASM mesh loads can reallocate memory", async () => {
    const terrainCore = await loadTerrainCore();
    const store = new TerrainCoreRenderPacketStore(terrainCore);
    store.addChunk({
      key: "0,0,0",
      meshId: "mesh:terrain.chunk:0,0,0",
      ...createTriangleMeshData(0),
      floatsPerVertex: 19
    });
    store.addChunk({
      key: "1,0,0",
      meshId: "mesh:terrain.chunk:1,0,0",
      ...createTriangleMeshData(1),
      floatsPerVertex: 19
    });

    equal(store.chunks.length, 2);
    equal(store.chunks[0].key, "0,0,0");
    equal(store.chunks[1].key, "1,0,0");
  });

  it("rejects invalid terrain mesh packet indices through Rust validation", async () => {
    const terrainCore = await loadTerrainCore();
    const store = new TerrainCoreRenderPacketStore(terrainCore);
    const mesh = createTriangleMeshData();

    throws(() => store.addChunk({
      key: "0,0,0",
      meshId: "mesh:invalid",
      vertices: mesh.vertices.slice(0, 19),
      indices: new Uint32Array([0, 1, 0]),
      floatsPerVertex: 19
    }), /rejected chunk/);
    equal(store.size(), 0);
  });
});

function createTriangleMeshData(firstX = 0) {
  return {
    vertices: new Float32Array([
      firstX, 0, 0, 0.3, 0.5, 0.4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
      1, 0, 0, 0.3, 0.5, 0.4, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
      0, 0, 1, 0.3, 0.5, 0.4, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0
    ]),
    indices: new Uint32Array([0, 1, 2])
  };
}

async function loadTerrainCore(): Promise<TerrainCoreWasmInstance> {
  const bytes = readFileSync(TERRAIN_CORE_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;

  return instantiateTerrainCoreWasm(wasmBytes);
}
