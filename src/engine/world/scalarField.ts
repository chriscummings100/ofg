import { normalize, vec3, type Vec3 } from "../math/vec3.js";
import { sampleFractalSimplex3D, SimplexNoise3D } from "./simplexNoise3D.js";
import type { TerrainDensitySample, TerrainDensitySource } from "./terrainChunk.js";

export type TerrainField = TerrainDensitySource & {
  readonly heightAt: (x: number, z: number) => number;
  readonly normalAt: (x: number, z: number) => Vec3;
};

const TERRAIN_NOISE = new SimplexNoise3D(0x0F6);
const LARGE_FEATURE_NOISE = {
  octaves: 3,
  frequency: 0.0065,
  lacunarity: 2,
  persistence: 0.52
} as const;
const DENSITY_DETAIL_NOISE = {
  octaves: 3,
  frequency: 0.035,
  lacunarity: 2.15,
  persistence: 0.46
} as const;
const BASE_TERRAIN_HEIGHT = 2;
const LARGE_FEATURE_HEIGHT_SCALE = 22;
const DENSITY_DETAIL_AMPLITUDE = 5;
const SURFACE_SEARCH_MIN_Y = -96;
const SURFACE_SEARCH_MAX_Y = 96;
const SURFACE_SEARCH_STEP = 1;
const SURFACE_REFINE_STEPS = 12;

export function createSeedTerrainField(): TerrainField {
  return {
    heightAt,
    densityAt(position) {
      return terrainDensitySampleAt(position).density;
    },
    sampleAt(position) {
      return terrainDensitySampleAt(position);
    },
    normalAt(x, z) {
      const y = heightAt(x, z);
      const gradient = terrainDensitySampleAt(vec3(x, y, z)).gradient;

      return normalize(gradient);
    }
  };
}

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

  return largeFeatureHeightAt(x, z).height;
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
  const largeFeature = largeFeatureHeightAt(position.x, position.z);
  const detail = sampleFractalSimplex3D(
    TERRAIN_NOISE,
    vec3(position.x + 83.5, position.y - 41.75, position.z - 19.25),
    DENSITY_DETAIL_NOISE
  );

  return {
    density: position.y - largeFeature.height - detail.value * DENSITY_DETAIL_AMPLITUDE,
    gradient: vec3(
      -largeFeature.gradientX - detail.gradient.x * DENSITY_DETAIL_AMPLITUDE,
      1 - detail.gradient.y * DENSITY_DETAIL_AMPLITUDE,
      -largeFeature.gradientZ - detail.gradient.z * DENSITY_DETAIL_AMPLITUDE
    )
  };
}

function largeFeatureHeightAt(x: number, z: number): {
  readonly height: number;
  readonly gradientX: number;
  readonly gradientZ: number;
} {
  const large = sampleFractalSimplex3D(TERRAIN_NOISE, vec3(x, 17.25, z), LARGE_FEATURE_NOISE);

  return {
    height: BASE_TERRAIN_HEIGHT + large.value * LARGE_FEATURE_HEIGHT_SCALE,
    gradientX: large.gradient.x * LARGE_FEATURE_HEIGHT_SCALE,
    gradientZ: large.gradient.z * LARGE_FEATURE_HEIGHT_SCALE
  };
}
