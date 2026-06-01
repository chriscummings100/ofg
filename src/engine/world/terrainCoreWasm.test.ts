import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";
import { generateTerrainDensityChunk } from "./terrainChunk.js";
import { generateTerrainChunkMeshWithWasm } from "./terrainCoreChunkMesh.js";
import { generateTerrainDensityChunkWithWasm } from "./terrainCoreDensityChunk.js";
import {
  createSeedWorldDescriptor,
  createTerrainGenerator,
  type TerrainPresetId
} from "./terrainGenerator.js";
import {
  instantiateTerrainCoreWasm,
  readTerrainCoreDensityChunkStoreStats,
  readTerrainCoreMeshIndexBuffer,
  readTerrainCoreMeshVertexBuffer,
  readTerrainCoreDensityChunkBuffer,
  terrainPresetToWasmCode
} from "./terrainCoreWasm.js";
import { getFloatsPerVertex } from "./terrainMesh.js";

const PRESETS: readonly TerrainPresetId[] = [
  "seed",
  "rollingHills",
  "mountainValley",
  "rockyHighland"
];

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
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_prepare_density_chunk_window"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_density_chunk_store_entry_count"));
    ok(TERRAIN_CORE_WASM_METADATA.exports.includes("ofg_build_chunk_mesh"));
  });

  it("instantiates the generated WASM artifact", async () => {
    const wasm = await loadTerrainCore();

    equal(wasm.exports.ofg_terrain_core_version(), 1);
    equal(wasm.exports.ofg_terrain_core_preset_count(), PRESETS.length);
    equal(wasm.exports.ofg_density_chunk_sample_count(), 33 * 33 * 33);
    ok(wasm.exports.ofg_density_chunk_store_max_entries() >= 8);
  });

  it("matches TypeScript terrain height and density golden samples", async () => {
    const wasm = await loadTerrainCore();
    const points = [
      { x: 0, z: 0 },
      { x: 12.5, z: -20.25 },
      { x: -47.75, z: 31.5 },
      { x: 96.125, z: -64.875 }
    ] as const;

    for (const terrainPreset of PRESETS) {
      const descriptor = createSeedWorldDescriptor(0x0F6, { terrainPreset });
      const terrain = createTerrainGenerator(descriptor);
      const presetCode = terrainPresetToWasmCode(terrainPreset);

      for (const point of points) {
        const expectedHeight = terrain.heightAt(point.x, point.z);
        const actualHeight = wasm.exports.ofg_height_at(
          descriptor.seed,
          presetCode,
          point.x,
          point.z
        );
        const sampleY = Math.round(expectedHeight * 2) / 2;
        const expectedDensity = terrain.densityAt({ x: point.x, y: sampleY, z: point.z });
        const actualDensity = wasm.exports.ofg_density_at(
          descriptor.seed,
          presetCode,
          point.x,
          sampleY,
          point.z
        );

        assertClose(actualHeight, expectedHeight, 0.000001);
        assertClose(actualDensity, expectedDensity, 0.000001);
      }
    }
  });

  it("fills density chunks matching the TypeScript terrain generator", async () => {
    const wasm = await loadTerrainCore();
    const descriptor = createSeedWorldDescriptor(0x0F6, { terrainPreset: "rockyHighland" });
    const terrain = createTerrainGenerator(descriptor);
    const coord = { x: -1, y: 0, z: 2 };
    const expected = generateTerrainDensityChunk(terrain, coord, { cellSize: 1 });
    const actual = generateTerrainDensityChunkWithWasm(wasm, descriptor, coord, { cellSize: 1 });

    equal(actual.densities.length, expected.densities.length);
    equal(readTerrainCoreDensityChunkBuffer(wasm.exports).length, expected.densities.length);

    for (let index = 0; index < expected.densities.length; index += 1) {
      assertClose(actual.densities[index], expected.densities[index], 0.00002);
    }
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
