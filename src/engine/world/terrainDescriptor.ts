import {
  TERRAIN_PRESET_IDS,
  type TerrainPresetId
} from "../../generated/world/terrainPresets.js";

export { TERRAIN_PRESET_IDS, type TerrainPresetId };
export type ClimatePresetId = "temperate";
export type TerrainMaterialPaletteId = "seed";

export type WorldDescriptor = {
  readonly seed: number;
  readonly seaLevel: number;
  readonly terrainPreset: TerrainPresetId;
  readonly climatePreset: ClimatePresetId;
  readonly materialPalette: TerrainMaterialPaletteId;
};

export const DEFAULT_TERRAIN_SEED = 0x0F6;
export const DEFAULT_SEA_LEVEL = 0;
export const DEFAULT_TERRAIN_PRESET: TerrainPresetId = "sineGrass";

export const DEFAULT_WORLD_DESCRIPTOR = createSeedWorldDescriptor();

export function createSeedWorldDescriptor(
  seed = DEFAULT_TERRAIN_SEED,
  overrides: Partial<Omit<WorldDescriptor, "seed">> = {}
): WorldDescriptor {
  const descriptor = Object.freeze({
    seed: seed >>> 0,
    seaLevel: overrides.seaLevel ?? DEFAULT_SEA_LEVEL,
    terrainPreset: overrides.terrainPreset ?? DEFAULT_TERRAIN_PRESET,
    climatePreset: overrides.climatePreset ?? "temperate",
    materialPalette: overrides.materialPalette ?? "seed"
  });
  validateWorldDescriptor(descriptor);

  return descriptor;
}

export function isTerrainPresetId(value: string): value is TerrainPresetId {
  return TERRAIN_PRESET_IDS.some((terrainPreset) => terrainPreset === value);
}

export function validateWorldDescriptor(descriptor: WorldDescriptor): void {
  if (!Number.isInteger(descriptor.seed) || descriptor.seed < 0) {
    throw new Error("World descriptor seed must be a non-negative integer.");
  }

  if (!Number.isFinite(descriptor.seaLevel)) {
    throw new Error("World descriptor seaLevel must be finite.");
  }

  if (!isTerrainPresetId(descriptor.terrainPreset)) {
    throw new Error(`Unknown terrain preset '${String(descriptor.terrainPreset)}'.`);
  }

  if (descriptor.climatePreset !== "temperate") {
    throw new Error(`Unknown climate preset '${String(descriptor.climatePreset)}'.`);
  }

  if (descriptor.materialPalette !== "seed") {
    throw new Error(`Unknown terrain material palette '${String(descriptor.materialPalette)}'.`);
  }
}
