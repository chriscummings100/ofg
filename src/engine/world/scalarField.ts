import {
  createTerrainGenerator,
  type TerrainField
} from "./terrainGenerator.js";

export function createSeedTerrainField(): TerrainField {
  return createTerrainGenerator();
}

export {
  DEFAULT_SEA_LEVEL,
  DEFAULT_TERRAIN_PRESET,
  DEFAULT_TERRAIN_SEED,
  DEFAULT_WORLD_DESCRIPTOR,
  TERRAIN_PRESET_IDS,
  createSeedWorldDescriptor,
  createTerrainGenerator,
  type BiomeSample,
  type BiomeWeight,
  type ClimatePresetId,
  type MacroSample,
  type TerrainBiomeId,
  type TerrainDebugChannels,
  type TerrainField,
  type TerrainGenerator,
  type TerrainMaterialId,
  type TerrainMaterialPaletteId,
  type TerrainMaterialWeight,
  type TerrainPresetId,
  type TerrainSurfaceSample,
  type WorldDescriptor
} from "./terrainGenerator.js";
