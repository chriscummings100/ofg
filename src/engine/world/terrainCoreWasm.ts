import type { TerrainPresetId } from "./terrainGenerator.js";

export type TerrainCoreWasmExports = {
  readonly ofg_terrain_core_version: () => number;
  readonly ofg_terrain_core_preset_count: () => number;
  readonly ofg_macro_base_elevation_at: (
    seed: number,
    preset: number,
    x: number,
    z: number
  ) => number;
  readonly ofg_density_at: (
    seed: number,
    preset: number,
    x: number,
    y: number,
    z: number
  ) => number;
  readonly ofg_height_at: (
    seed: number,
    preset: number,
    x: number,
    z: number
  ) => number;
};

export type TerrainCoreWasmInstance = {
  readonly exports: TerrainCoreWasmExports;
};

const TERRAIN_PRESET_CODES: Readonly<Record<TerrainPresetId, number>> = Object.freeze({
  seed: 0,
  rollingHills: 1,
  mountainValley: 2,
  rockyHighland: 3
});

export async function instantiateTerrainCoreWasm(
  bytes: ArrayBuffer
): Promise<TerrainCoreWasmInstance> {
  const wasm = await WebAssembly.instantiate(bytes, {});
  const exports = wasm.instance.exports as TerrainCoreWasmExports;
  assertTerrainCoreExports(exports);

  return Object.freeze({ exports });
}

export function terrainPresetToWasmCode(preset: TerrainPresetId): number {
  return TERRAIN_PRESET_CODES[preset];
}

function assertTerrainCoreExports(exports: WebAssembly.Exports): asserts exports is TerrainCoreWasmExports {
  const expectedFunctionNames = [
    "ofg_terrain_core_version",
    "ofg_terrain_core_preset_count",
    "ofg_macro_base_elevation_at",
    "ofg_density_at",
    "ofg_height_at"
  ] as const;

  for (const name of expectedFunctionNames) {
    if (typeof exports[name] !== "function") {
      throw new Error(`Terrain WASM export is missing: ${name}`);
    }
  }
}
