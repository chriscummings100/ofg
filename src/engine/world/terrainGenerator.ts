import { clamp, normalize, vec3, type Vec3 } from "../math/vec3.js";
import {
  sampleFractalSimplex3D,
  SimplexNoise3D,
  type FractalNoiseOptions
} from "./simplexNoise3D.js";
import type { TerrainDensitySample, TerrainDensitySource } from "./terrainChunk.js";
import {
  normalizeTerrainMaterialWeights,
  type TerrainMaterialWeight
} from "./terrainMaterials.js";
import {
  sampleCellular2D,
  sampleDomainWarp2D,
  sampleRidgedFractalSimplex3D,
  type CellularNoiseOptions,
  type DomainWarpOptions,
  type RidgedFractalNoiseOptions
} from "./terrainNoise.js";

export const TERRAIN_PRESET_IDS = [
  "seed",
  "rollingHills",
  "mountainValley",
  "rockyHighland"
] as const;

export type TerrainPresetId = typeof TERRAIN_PRESET_IDS[number];
export type { TerrainMaterialId, TerrainMaterialWeight } from "./terrainMaterials.js";
export type ClimatePresetId = "temperate";
export type TerrainMaterialPaletteId = "seed";
export type TerrainBiomeId =
  | "grassland"
  | "temperateForest"
  | "wetland"
  | "coastBeach"
  | "dryBadland"
  | "alpineMeadow"
  | "highMountainRock"
  | "snowTundra";

export type WorldDescriptor = {
  readonly seed: number;
  readonly seaLevel: number;
  readonly terrainPreset: TerrainPresetId;
  readonly climatePreset: ClimatePresetId;
  readonly materialPalette: TerrainMaterialPaletteId;
};

export type MacroSample = {
  readonly baseElevation: number;
  readonly largeFeature: number;
  readonly mountainness: number;
  readonly continentality: number;
  readonly erosionSusceptibility: number;
  readonly ridge: number;
  readonly warp: Vec3;
};

export type BiomeWeight = {
  readonly biome: TerrainBiomeId;
  readonly weight: number;
};

export type BiomeSample = {
  readonly temperature: number;
  readonly moisture: number;
  readonly province: number;
  readonly weights: readonly BiomeWeight[];
};

export type TerrainDebugChannels = {
  readonly largeFeature: number;
  readonly detail: number;
  readonly ridge: number;
  readonly cellular: number;
  readonly mountainness: number;
  readonly continentality: number;
};

export type TerrainSurfaceSample = TerrainDensitySample & {
  readonly materialWeights: readonly TerrainMaterialWeight[];
  readonly biomeWeights: readonly BiomeWeight[];
  readonly debug: TerrainDebugChannels;
};

export type TerrainField = TerrainDensitySource & {
  readonly heightAt: (x: number, z: number) => number;
  readonly normalAt: (x: number, z: number) => Vec3;
};

export type TerrainGenerator = TerrainField & {
  readonly descriptor: WorldDescriptor;
  readonly macroAt: (position: Vec3) => MacroSample;
  readonly biomeAt: (position: Vec3) => BiomeSample;
  readonly surfaceAt: (position: Vec3) => TerrainSurfaceSample;
  readonly sampleAt: (position: Vec3) => TerrainSurfaceSample;
};

export const DEFAULT_TERRAIN_SEED = 0x0F6;
export const DEFAULT_SEA_LEVEL = 0;
export const DEFAULT_TERRAIN_PRESET: TerrainPresetId = "rollingHills";

type TerrainPresetDefinition = {
  readonly baseHeight: number;
  readonly heightScale: number;
  readonly largeFeatureNoise: FractalNoiseOptions;
  readonly ridgeHeightScale: number;
  readonly ridgeNoise: RidgedFractalNoiseOptions;
  readonly warp: DomainWarpOptions;
  readonly cellular: CellularNoiseOptions;
  readonly cellularHeightScale: number;
  readonly detailNoise: FractalNoiseOptions;
  readonly detailAmplitude: number;
};

type MacroTerrainSample = MacroSample & {
  readonly gradientX: number;
  readonly gradientZ: number;
  readonly cellularEdge: number;
};

const SEED_LARGE_FEATURE_NOISE = {
  octaves: 3,
  frequency: 0.0065,
  lacunarity: 2,
  persistence: 0.52
} as const;
const SEED_DENSITY_DETAIL_NOISE = {
  octaves: 3,
  frequency: 0.035,
  lacunarity: 2.15,
  persistence: 0.46
} as const;
const TERRAIN_PRESETS: Readonly<Record<TerrainPresetId, TerrainPresetDefinition>> = Object.freeze({
  seed: Object.freeze({
    baseHeight: 2,
    heightScale: 22,
    largeFeatureNoise: SEED_LARGE_FEATURE_NOISE,
    ridgeHeightScale: 0,
    ridgeNoise: {
      octaves: 1,
      frequency: 0.008
    },
    warp: {
      octaves: 1,
      frequency: 0.005,
      amplitude: 0
    },
    cellular: {
      frequency: 0.015
    },
    cellularHeightScale: 0,
    detailNoise: SEED_DENSITY_DETAIL_NOISE,
    detailAmplitude: 5
  }),
  rollingHills: Object.freeze({
    baseHeight: 3,
    heightScale: 16,
    largeFeatureNoise: {
      octaves: 4,
      frequency: 0.004,
      lacunarity: 2,
      persistence: 0.5
    },
    ridgeHeightScale: 3,
    ridgeNoise: {
      octaves: 3,
      frequency: 0.009,
      lacunarity: 2.1,
      persistence: 0.48,
      ridgeSharpness: 1.8
    },
    warp: {
      octaves: 2,
      frequency: 0.004,
      lacunarity: 2,
      persistence: 0.5,
      amplitude: 14
    },
    cellular: {
      frequency: 0.018
    },
    cellularHeightScale: 1.3,
    detailNoise: {
      octaves: 3,
      frequency: 0.03,
      lacunarity: 2.05,
      persistence: 0.44
    },
    detailAmplitude: 3.2
  }),
  mountainValley: Object.freeze({
    baseHeight: 2,
    heightScale: 20,
    largeFeatureNoise: {
      octaves: 4,
      frequency: 0.0028,
      lacunarity: 2,
      persistence: 0.53
    },
    ridgeHeightScale: 24,
    ridgeNoise: {
      octaves: 4,
      frequency: 0.0065,
      lacunarity: 2.05,
      persistence: 0.52,
      ridgeSharpness: 2.25
    },
    warp: {
      octaves: 3,
      frequency: 0.0032,
      lacunarity: 2,
      persistence: 0.5,
      amplitude: 28
    },
    cellular: {
      frequency: 0.012
    },
    cellularHeightScale: 2,
    detailNoise: {
      octaves: 3,
      frequency: 0.026,
      lacunarity: 2.1,
      persistence: 0.45
    },
    detailAmplitude: 4.5
  }),
  rockyHighland: Object.freeze({
    baseHeight: 7,
    heightScale: 18,
    largeFeatureNoise: {
      octaves: 4,
      frequency: 0.0036,
      lacunarity: 2.2,
      persistence: 0.5
    },
    ridgeHeightScale: 11,
    ridgeNoise: {
      octaves: 4,
      frequency: 0.011,
      lacunarity: 2.2,
      persistence: 0.5,
      ridgeSharpness: 1.45
    },
    warp: {
      octaves: 2,
      frequency: 0.0055,
      lacunarity: 2.1,
      persistence: 0.52,
      amplitude: 18
    },
    cellular: {
      frequency: 0.02
    },
    cellularHeightScale: 6,
    detailNoise: {
      octaves: 4,
      frequency: 0.038,
      lacunarity: 2.2,
      persistence: 0.48
    },
    detailAmplitude: 6.5
  })
});
const SURFACE_SEARCH_MIN_Y = -96;
const SURFACE_SEARCH_MAX_Y = 96;
const SURFACE_SEARCH_STEP = 1;
const SURFACE_REFINE_STEPS = 12;

export const DEFAULT_WORLD_DESCRIPTOR = createSeedWorldDescriptor();

export function createSeedWorldDescriptor(
  seed = DEFAULT_TERRAIN_SEED,
  overrides: Partial<Omit<WorldDescriptor, "seed">> = {}
): WorldDescriptor {
  return Object.freeze({
    seed: seed >>> 0,
    seaLevel: overrides.seaLevel ?? DEFAULT_SEA_LEVEL,
    terrainPreset: overrides.terrainPreset ?? DEFAULT_TERRAIN_PRESET,
    climatePreset: overrides.climatePreset ?? "temperate",
    materialPalette: overrides.materialPalette ?? "seed"
  });
}

export function createTerrainGenerator(
  descriptor: WorldDescriptor = DEFAULT_WORLD_DESCRIPTOR
): TerrainGenerator {
  validateWorldDescriptor(descriptor);

  const noise = new SimplexNoise3D(descriptor.seed);
  const terrainPreset = TERRAIN_PRESETS[descriptor.terrainPreset];

  function heightAt(x: number, z: number): number {
    let upperY = SURFACE_SEARCH_MAX_Y;
    let upperDensity = densityAtPosition(x, upperY, z);

    for (
      let lowerY = upperY - SURFACE_SEARCH_STEP;
      lowerY >= SURFACE_SEARCH_MIN_Y;
      lowerY -= SURFACE_SEARCH_STEP
    ) {
      const lowerDensity = densityAtPosition(x, lowerY, z);
      if (lowerDensity <= 0 && upperDensity > 0) {
        return refineSurfaceHeight(x, z, lowerY, upperY);
      }

      upperY = lowerY;
      upperDensity = lowerDensity;
    }

    return sampleMacroTerrain(noise, descriptor, terrainPreset, vec3(x, 0, z)).baseElevation;
  }

  function refineSurfaceHeight(
    x: number,
    z: number,
    solidY: number,
    airY: number
  ): number {
    let lowerY = solidY;
    let upperY = airY;
    for (let step = 0; step < SURFACE_REFINE_STEPS; step += 1) {
      const midY = (lowerY + upperY) * 0.5;
      if (densityAtPosition(x, midY, z) <= 0) {
        lowerY = midY;
      } else {
        upperY = midY;
      }
    }

    return (lowerY + upperY) * 0.5;
  }

  function densityAtPosition(x: number, y: number, z: number): number {
    return terrainDensitySampleAt(vec3(x, y, z)).density;
  }

  function terrainDensitySampleAt(position: Vec3): TerrainDensitySample {
    const macro = sampleMacroTerrain(noise, descriptor, terrainPreset, position);
    const detail = sampleFractalSimplex3D(
      noise,
      vec3(
        position.x + 83.5 + macro.warp.x * 0.15,
        position.y - 41.75,
        position.z - 19.25 + macro.warp.z * 0.15
      ),
      terrainPreset.detailNoise
    );

    return {
      density: position.y - macro.baseElevation - detail.value * terrainPreset.detailAmplitude,
      gradient: vec3(
        -macro.gradientX - detail.gradient.x * terrainPreset.detailAmplitude,
        1 - detail.gradient.y * terrainPreset.detailAmplitude,
        -macro.gradientZ - detail.gradient.z * terrainPreset.detailAmplitude
      )
    };
  }

  function macroAt(position: Vec3): MacroSample {
    const macro = sampleMacroTerrain(noise, descriptor, terrainPreset, position);

    return {
      baseElevation: macro.baseElevation,
      largeFeature: macro.largeFeature,
      mountainness: macro.mountainness,
      continentality: macro.continentality,
      erosionSusceptibility: macro.erosionSusceptibility,
      ridge: macro.ridge,
      warp: macro.warp
    };
  }

  function biomeAt(position: Vec3): BiomeSample {
    const macro = sampleMacroTerrain(noise, descriptor, terrainPreset, position);
    const climateNoise = sampleFractalSimplex3D(
      noise,
      vec3(position.x + 971.2, 43.5, position.z - 211.7),
      {
        octaves: 3,
        frequency: 0.0025,
        lacunarity: 2,
        persistence: 0.52
      }
    );
    const moistureNoise = sampleFractalSimplex3D(
      noise,
      vec3(position.x - 317.6, -29.25, position.z + 513.4),
      {
        octaves: 3,
        frequency: 0.0032,
        lacunarity: 2,
        persistence: 0.5
      }
    );
    const province = sampleCellular2D(position, {
      frequency: 0.0035,
      seed: descriptor.seed ^ 0x8A31D5E3
    });
    const altitude = position.y - descriptor.seaLevel;
    const high = smoothstep(14, 34, altitude);
    const veryHigh = smoothstep(30, 52, altitude);
    const nearSeaLevel = clamp(1 - Math.abs(altitude) / 8, 0, 1);
    const temperature = clamp(
      0.72 - high * 0.34 - veryHigh * 0.22 - macro.continentality * 0.05 +
      climateNoise.value * 0.12,
      0,
      1
    );
    const moisture = clamp(
      0.42 + (1 - macro.continentality) * 0.22 + macro.erosionSusceptibility * 0.12 -
      high * 0.09 + moistureNoise.value * 0.18,
      0,
      1
    );
    const wetness = smoothstep(0.5, 0.78, moisture) * (1 - high * 0.75);
    const dryness = smoothstep(0.48, 0.76, macro.continentality) *
      (1 - smoothstep(0.42, 0.68, moisture)) *
      (1 - high * 0.35);
    const coast = nearSeaLevel * smoothstep(0.4, 0.82, moisture) * (1 - high);
    const mountainRock = smoothstep(0.46, 0.76, macro.mountainness) * smoothstep(10, 26, altitude);
    const snow = smoothstep(34, 54, altitude) * (1 - smoothstep(0.28, 0.58, temperature));
    const alpine = smoothstep(16, 34, altitude) * (1 - snow) * (1 - mountainRock * 0.5);
    const forest = smoothstep(0.52, 0.78, moisture) *
      smoothstep(0.34, 0.72, temperature) *
      (1 - high * 0.7) *
      (1 - coast * 0.5) *
      (1 - dryness * 0.55);
    const grassland = (1 - high * 0.55) *
      (1 - wetness * 0.6) *
      (1 - dryness * 0.45) *
      (1 - forest * 0.45);

    return {
      temperature,
      moisture,
      province: province.cellId,
      weights: normalizeBiomeWeights([
        { biome: "grassland", weight: grassland },
        { biome: "temperateForest", weight: forest },
        { biome: "wetland", weight: wetness * (1 - coast * 0.35) },
        { biome: "coastBeach", weight: coast },
        { biome: "dryBadland", weight: dryness },
        { biome: "alpineMeadow", weight: alpine },
        { biome: "highMountainRock", weight: mountainRock * (1 - snow * 0.5) },
        { biome: "snowTundra", weight: snow }
      ])
    };
  }

  function surfaceAt(position: Vec3): TerrainSurfaceSample {
    const densitySample = terrainDensitySampleAt(position);
    const biome = biomeAt(position);
    const macro = sampleMacroTerrain(noise, descriptor, terrainPreset, position);

    return {
      ...densitySample,
      materialWeights: materialWeightsAt(
        position,
        densitySample.gradient,
        descriptor.seaLevel,
        macro,
        biome
      ),
      biomeWeights: biome.weights,
      debug: {
        largeFeature: macro.largeFeature,
        detail: (position.y - macro.baseElevation - densitySample.density) /
          terrainPreset.detailAmplitude,
        ridge: macro.ridge,
        cellular: macro.cellularEdge,
        mountainness: macro.mountainness,
        continentality: macro.continentality
      }
    };
  }

  function normalAt(x: number, z: number): Vec3 {
    const y = heightAt(x, z);
    const gradient = terrainDensitySampleAt(vec3(x, y, z)).gradient;

    return normalize(gradient);
  }

  return Object.freeze({
    descriptor,
    heightAt,
    densityAt(position) {
      return terrainDensitySampleAt(position).density;
    },
    sampleAt(position) {
      return surfaceAt(position);
    },
    normalAt,
    macroAt,
    biomeAt,
    surfaceAt
  });
}

function sampleMacroTerrain(
  noise: SimplexNoise3D,
  descriptor: WorldDescriptor,
  preset: TerrainPresetDefinition,
  position: Vec3
): MacroTerrainSample {
  const warp = sampleDomainWarp2D(noise, position, preset.warp);
  const large = sampleFractalSimplex3D(
    noise,
    vec3(warp.position.x, 17.25, warp.position.z),
    preset.largeFeatureNoise
  );
  const ridge = sampleRidgedFractalSimplex3D(
    noise,
    vec3(warp.position.x - 137.2, 61.4, warp.position.z + 88.1),
    preset.ridgeNoise
  );
  const cellular = sampleCellular2D(warp.position, {
    ...preset.cellular,
    seed: descriptor.seed ^ 0xB5297A4D
  });
  const normalizedLargeFeature = clamp(large.value * 0.5 + 0.5, 0, 1);
  const cellularEdge = 1 - clamp(cellular.edgeDistance * 2.5, 0, 1);
  const mountainness = clamp(normalizedLargeFeature * 0.55 + ridge.value * 0.45, 0, 1);
  const cellularContribution = (cellularEdge - 0.35) * preset.cellularHeightScale * mountainness;
  const ridgeContribution = ridge.value * preset.ridgeHeightScale * mountainness;
  const baseElevation =
    preset.baseHeight +
    large.value * preset.heightScale +
    ridgeContribution +
    cellularContribution;

  return {
    baseElevation,
    largeFeature: large.value,
    mountainness,
    continentality: normalizedLargeFeature,
    erosionSusceptibility: clamp(1 - ridge.value * 0.5 - cellularEdge * 0.2, 0, 1),
    ridge: ridge.value,
    warp: warp.offset,
    gradientX:
      large.gradient.x * preset.heightScale +
      ridge.gradient.x * preset.ridgeHeightScale * mountainness,
    gradientZ:
      large.gradient.z * preset.heightScale +
      ridge.gradient.z * preset.ridgeHeightScale * mountainness,
    cellularEdge
  };
}

function materialWeightsAt(
  position: Vec3,
  gradient: Vec3,
  seaLevel: number,
  macro: MacroTerrainSample,
  biome: BiomeSample
): readonly TerrainMaterialWeight[] {
  const normal = normalize(gradient);
  const slope = clamp(1 - normal.y, 0, 1);
  const lowland = clamp((seaLevel + 4 - position.y) / 8, 0, 1);
  const highland = clamp((position.y - 28) / 28, 0, 1);
  const cliff = smoothstep(0.62, 0.86, slope);
  const rocky = smoothstep(0.34, 0.68, slope) * (1 - cliff);
  const snow = smoothstep(38, 56, position.y) * smoothstep(0.1, 0.65, normal.y);
  const wet = lowland * smoothstep(0.12, 0.72, normal.y) * (1 - rocky) * (1 - cliff);
  const sand = clamp((seaLevel + 2.5 - Math.abs(position.y - seaLevel)) / 5, 0, 1) *
    smoothstep(0.18, 0.82, normal.y) *
    (0.45 + macro.continentality * 0.25);
  const dry = clamp(0.35 + macro.continentality * 0.45 - macro.mountainness * 0.25, 0, 1);
  const moss = clamp((macro.mountainness + macro.ridge) * 0.35, 0, 0.8) * (1 - cliff) * (1 - snow);
  const redSoil = clamp((macro.cellularEdge - 0.42) / 0.45, 0, 0.75) * dry * (1 - rocky) * (1 - snow);
  const meadow = (1 - dry * 0.55) * smoothstep(0.2, 0.85, normal.y) * (1 - wet) * (1 - snow);
  const dryGround = dry * smoothstep(0.28, 0.88, normal.y) * (1 - wet) * (1 - snow);
  const scree = rocky * highland * 0.65;
  const grasslandBiome = biomeWeight(biome, "grassland");
  const forestBiome = biomeWeight(biome, "temperateForest");
  const wetlandBiome = biomeWeight(biome, "wetland");
  const coastBiome = biomeWeight(biome, "coastBeach");
  const badlandBiome = biomeWeight(biome, "dryBadland");
  const alpineBiome = biomeWeight(biome, "alpineMeadow");
  const mountainRockBiome = biomeWeight(biome, "highMountainRock");
  const snowBiome = biomeWeight(biome, "snowTundra");

  return normalizeTerrainMaterialWeights([
    { material: "meadowGrass", weight: meadow * (0.72 + grasslandBiome * 0.42 + alpineBiome * 0.18) },
    { material: "dryGround", weight: dryGround * (0.72 + badlandBiome * 0.65) },
    { material: "forestGround", weight: (1 - dry) * 0.2 * (1 - rocky) * (1 - wet) + forestBiome * 0.45 },
    { material: "bareSoil", weight: lowland * 0.28 * (1 - wet) * (1 - sand) + wetlandBiome * 0.1 },
    { material: "wetMud", weight: wet + wetlandBiome * 0.65 },
    { material: "sand", weight: sand + coastBiome * 0.55 },
    { material: "gravelSand", weight: sand * rocky * 0.8 + coastBiome * rocky * 0.22 },
    { material: "scree", weight: scree + mountainRockBiome * rocky * 0.28 },
    { material: "rockyGround", weight: rocky * (1 - highland * 0.35) + mountainRockBiome * 0.3 },
    { material: "cliffRock", weight: cliff + mountainRockBiome * cliff * 0.35 },
    { material: "mossRock", weight: moss + forestBiome * 0.16 + alpineBiome * 0.14 },
    { material: "redSoil", weight: redSoil + badlandBiome * 0.4 },
    { material: "snow", weight: snow + snowBiome * 0.85 }
  ]);
}

function normalizeBiomeWeights(weights: readonly BiomeWeight[]): readonly BiomeWeight[] {
  const positiveWeights = weights.filter((weight) => weight.weight > 0);
  const total = positiveWeights.reduce((sum, weight) => sum + weight.weight, 0);
  if (total <= Number.EPSILON) {
    return Object.freeze([
      Object.freeze({ biome: "grassland", weight: 1 })
    ]);
  }

  return Object.freeze(positiveWeights.map((weight) =>
    Object.freeze({
      biome: weight.biome,
      weight: weight.weight / total
    })
  ));
}

function biomeWeight(biome: BiomeSample, id: TerrainBiomeId): number {
  return biome.weights.find((weight) => weight.biome === id)?.weight ?? 0;
}

function smoothstep(edge0: number, edge1: number, value: number): number {
  const t = clamp((value - edge0) / (edge1 - edge0), 0, 1);
  return t * t * (3 - 2 * t);
}

function validateWorldDescriptor(descriptor: WorldDescriptor): void {
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

function isTerrainPresetId(value: string): value is TerrainPresetId {
  return TERRAIN_PRESET_IDS.some((terrainPreset) => terrainPreset === value);
}
