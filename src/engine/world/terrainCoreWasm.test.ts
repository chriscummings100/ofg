import { equal, notEqual, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";
import { terrainChunkKey } from "./terrainChunk.js";
import { generateTerrainChunkMeshWithWasm } from "./terrainCoreChunkMesh.js";
import { createTerrainCoreDensityChunkStore } from "./terrainCoreDensityChunkStore.js";
import { generateTerrainDensityChunkWithWasm } from "./terrainCoreDensityChunk.js";
import {
  createSeedWorldDescriptor,
  TERRAIN_PRESET_IDS,
  type TerrainPresetId
} from "./terrainDescriptor.js";
import {
  instantiateTerrainCoreWasm,
  readTerrainCoreDensityChunkStoreStats,
  readTerrainCoreMeshIndexBuffer,
  readTerrainCoreMeshVertexBuffer,
  readTerrainCoreDensityChunkBuffer,
  terrainPresetToWasmCode
} from "./terrainCoreWasm.js";
import { createTerrainCoreStreamScheduler } from "./terrainCoreStreamScheduler.js";
import { getFloatsPerVertex } from "./terrainMesh.js";

const PRESETS: readonly TerrainPresetId[] = TERRAIN_PRESET_IDS;

describe("terrain core WASM", () => {
  it("exposes deterministic terrain core artifact metadata", () => {
    equal(TERRAIN_CORE_WASM_METADATA.id, "terrain_core");
    equal(TERRAIN_CORE_WASM_METADATA.sourceCrate, "crates/terrain_core");
    equal(TERRAIN_CORE_WASM_METADATA.assetPath, "assets/wasm/terrain_core.wasm");
    equal(TERRAIN_CORE_WASM_METADATA.target, "wasm32-unknown-unknown");
    ok(/^sha256-[0-9a-f]{64}$/.test(TERRAIN_CORE_WASM_METADATA.artifactHash));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_height_at"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_density_at"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_fill_density_chunk"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_store_density_chunk_buffer"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_retain_density_chunk_store_window"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_prepare_density_chunk_window"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_density_chunk_store_entry_count"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_build_chunk_mesh"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_stream_tick"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_stream_complete_lod0"));
  });

  it("instantiates the generated WASM artifact", async () => {
    const wasm = await loadTerrainCore();

    equal(wasm.exports.ofg_terrain_core_version(), 1);
    equal(wasm.exports.ofg_terrain_core_preset_count(), PRESETS.length);
    equal(wasm.exports.ofg_density_chunk_sample_count(), 33 * 33 * 33);
    ok(wasm.exports.ofg_density_chunk_store_max_entries() >= 8);
  });

  it("returns deterministic finite height and density samples for every preset", async () => {
    const wasm = await loadTerrainCore();
    const points = [
      { x: 0, z: 0 },
      { x: 12.5, z: -20.25 },
      { x: -47.75, z: 31.5 },
      { x: 96.125, z: -64.875 }
    ] as const;
    const presetHeights: number[] = [];

    for (const terrainPreset of PRESETS) {
      const descriptor = createSeedWorldDescriptor(0x0F6, { terrainPreset });
      const presetCode = terrainPresetToWasmCode(terrainPreset);

      for (const point of points) {
        const firstHeight = wasm.exports.ofg_height_at(
          descriptor.seed,
          presetCode,
          point.x,
          point.z
        );
        const secondHeight = wasm.exports.ofg_height_at(
          descriptor.seed,
          presetCode,
          point.x,
          point.z
        );
        const densityAtSurface = wasm.exports.ofg_density_at(
          descriptor.seed,
          presetCode,
          point.x,
          firstHeight,
          point.z
        );
        const densityBelow = wasm.exports.ofg_density_at(
          descriptor.seed,
          presetCode,
          point.x,
          firstHeight - 4,
          point.z
        );
        const densityAbove = wasm.exports.ofg_density_at(
          descriptor.seed,
          presetCode,
          point.x,
          firstHeight + 4,
          point.z
        );

        ok(Number.isFinite(firstHeight));
        equal(firstHeight, secondHeight);
        ok(Number.isFinite(densityAtSurface));
        ok(Math.abs(densityAtSurface) < 0.05);
        ok(densityBelow < densityAbove);
      }

      presetHeights.push(wasm.exports.ofg_height_at(descriptor.seed, presetCode, 64, -96));
    }

    notEqual(new Set(presetHeights.map((height) => height.toFixed(3))).size, 1);
  });

  it("fills deterministic finite density chunks in WASM", async () => {
    const wasm = await loadTerrainCore();
    const descriptor = createSeedWorldDescriptor(0x0F6, { terrainPreset: "rockyHighland" });
    const coord = { x: -1, y: 0, z: 2 };
    const first = generateTerrainDensityChunkWithWasm(wasm, descriptor, coord, { cellSize: 1 });
    const second = generateTerrainDensityChunkWithWasm(wasm, descriptor, coord, { cellSize: 1 });

    equal(first.densities.length, 33 * 33 * 33);
    equal(readTerrainCoreDensityChunkBuffer(wasm.exports).length, first.densities.length);
    equal(first.densities[0], second.densities[0]);
    equal(
      first.densities[first.densities.length - 1],
      second.densities[second.densities.length - 1]
    );

    let finiteSamples = 0;
    for (let index = 0; index < first.densities.length; index += 1024) {
      ok(Number.isFinite(first.densities[index]));
      finiteSamples += 1;
    }
    ok(finiteSamples > 20);
  });

  it("builds renderable terrain chunk meshes in WASM", async () => {
    const wasm = await loadTerrainCore();
    const descriptor = createSeedWorldDescriptor(0x0F6, { terrainPreset: "rollingHills" });
    const mesh = generateTerrainChunkMeshWithWasm(
      wasm,
      descriptor,
      { x: 0, y: 0, z: 0 },
      1
    );

    ok(mesh.vertices.length > 0);
    ok(mesh.indices.length > 0);
    equal(mesh.vertices.length % getFloatsPerVertex(), 0);
    equal(mesh.indices.length % 3, 0);
    equal(readTerrainCoreMeshVertexBuffer(wasm.exports).length, mesh.vertices.length);
    equal(readTerrainCoreMeshIndexBuffer(wasm.exports).length, mesh.indices.length);

    const vertexCount = mesh.vertices.length / getFloatsPerVertex();
    for (const index of mesh.indices) {
      ok(index < vertexCount, `Mesh index ${index} should reference ${vertexCount} vertices.`);
    }

    for (let offset = 0; offset < mesh.vertices.length; offset += getFloatsPerVertex()) {
      const materialWeightSum =
        mesh.vertices[offset + 15] +
        mesh.vertices[offset + 16] +
        mesh.vertices[offset + 17] +
        mesh.vertices[offset + 18];

      ok(Number.isFinite(mesh.vertices[offset]));
      ok(Number.isFinite(mesh.vertices[offset + 1]));
      ok(Number.isFinite(mesh.vertices[offset + 2]));
      assertClose(materialWeightSum, 1, 0.00001);
    }
  });

  it("prepares a retained density window for WASM mesh reuse", async () => {
    const wasm = await loadTerrainCore();
    const descriptor = createSeedWorldDescriptor(0x0F6, { terrainPreset: "rollingHills" });
    const preset = terrainPresetToWasmCode(descriptor.terrainPreset);

    wasm.exports.ofg_reset_density_chunk_store();
    const prepared = wasm.exports.ofg_prepare_density_chunk_window(
      descriptor.seed,
      preset,
      0,
      0,
      0,
      1,
      1,
      1,
      1
    );
    const afterPrepare = readTerrainCoreDensityChunkStoreStats(wasm.exports);

    equal(prepared, 8);
    equal(afterPrepare.entries, 8);
    equal(afterPrepare.generations, 8);

    generateTerrainChunkMeshWithWasm(wasm, descriptor, { x: 0, y: 0, z: 0 }, 1);
    const afterMesh = readTerrainCoreDensityChunkStoreStats(wasm.exports);

    equal(afterMesh.generations, afterPrepare.generations);
    equal(afterMesh.reuses, afterPrepare.reuses + 8);
  });

  it("stores density job results in the Rust density store", async () => {
    const wasm = await loadTerrainCore();
    const descriptor = createSeedWorldDescriptor(0x0F6, { terrainPreset: "rollingHills" });
    const store = createTerrainCoreDensityChunkStore(wasm, descriptor);
    const coord = { x: -1, y: 0, z: 2 };
    const generated = generateTerrainDensityChunkWithWasm(wasm, descriptor, coord, {
      cellSize: 1
    });

    store.clear();
    store.store({
      key: terrainChunkKey(coord),
      coord,
      densities: generated.densities
    }, 1);

    equal(store.size(), 1);

    store.retainOnly([{ x: -1, y: 0, z: 2 }], 1);
    equal(store.size(), 1);
    store.retainOnly([], 1);
    equal(store.size(), 0);
  });

  it("drives terrain stream scheduling through WASM buffers", async () => {
    const wasm = await loadTerrainCore();
    const scheduler = createTerrainCoreStreamScheduler(wasm, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0],
      maxInFlightJobs: 8
    });

    scheduler.syncCenter({ x: 0, y: 0, z: 0 });

    equal(scheduler.desiredLod0Coords().map(chunkKey).join(";"), "0,0,0");
    equal(scheduler.desiredDensityCoords().length, 8);
    equal(scheduler.status().missingDensityCount, 8);

    const densityJobs = scheduler.tick();
    equal(densityJobs.length, 8);
    equal(densityJobs.every((job) => job.kind === "density"), true);
    equal(scheduler.status().inFlightDensityCount, 8);

    for (const job of densityJobs) {
      if (job.kind === "density") {
        equal(scheduler.completeDensity(job.generation, job.coord), true);
      }
    }

    const lodJobs = scheduler.tick();
    equal(lodJobs.length, 1);
    equal(lodJobs[0].kind, "lod");
    if (lodJobs[0].kind === "lod") {
      equal(lodJobs[0].lod, 0);
      equal(scheduler.completeLod0(lodJobs[0].generation, lodJobs[0].coord, false), true);
    }
    equal(scheduler.status().lod0ReadyCount, 1);
  });
});

async function loadTerrainCore() {
  const bytes = readFileSync(TERRAIN_CORE_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;

  return instantiateTerrainCoreWasm(wasmBytes);
}

function assertClose(actual: number, expected: number, epsilon: number): void {
  ok(
    Math.abs(actual - expected) <= epsilon,
    `Expected ${actual} to be within ${epsilon} of ${expected}`
  );
}

function chunkKey(coord: { readonly x: number; readonly y: number; readonly z: number }): string {
  return `${coord.x},${coord.y},${coord.z}`;
}
