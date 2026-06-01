import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { TERRAIN_CORE_WASM_METADATA } from "../../generated/terrain/terrainCoreWasm.js";
import { generateTerrainDensityChunk } from "./terrainChunk.js";
import { generateTerrainDensityChunkWithWasm } from "./terrainCoreDensityChunk.js";
import {
  createSeedWorldDescriptor,
  createTerrainGenerator,
  type TerrainPresetId
} from "./terrainGenerator.js";
import {
  instantiateTerrainCoreWasm,
  readTerrainCoreDensityChunkBuffer,
  terrainPresetToWasmCode
} from "./terrainCoreWasm.js";

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
  });

  it("instantiates the generated WASM artifact", async () => {
    const wasm = await loadTerrainCore();

    equal(wasm.exports.ofg_terrain_core_version(), 1);
    equal(wasm.exports.ofg_terrain_core_preset_count(), PRESETS.length);
    equal(wasm.exports.ofg_density_chunk_sample_count(), 33 * 33 * 33);
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
