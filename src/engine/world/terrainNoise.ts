import { clamp, vec3, type Vec3 } from "../math/vec3.js";
import {
  sampleFractalSimplex3D,
  SimplexNoise3D,
  type FractalNoiseOptions,
  type SimplexNoiseSample
} from "./simplexNoise3D.js";

export type RidgedFractalNoiseOptions = FractalNoiseOptions & {
  readonly ridgeOffset?: number;
  readonly ridgeSharpness?: number;
};

export type DomainWarpOptions = FractalNoiseOptions & {
  readonly amplitude: number;
};

export type DomainWarpSample = {
  readonly position: Vec3;
  readonly offset: Vec3;
};

export type CellularNoiseOptions = {
  readonly frequency: number;
  readonly seed?: number;
};

export type CellularNoiseSample = {
  readonly nearestDistance: number;
  readonly secondNearestDistance: number;
  readonly edgeDistance: number;
  readonly cellId: number;
};

const UINT32_SCALE = 1 / 4294967296;

export function sampleRidgedFractalSimplex3D(
  noise: SimplexNoise3D,
  position: Vec3,
  options: RidgedFractalNoiseOptions
): SimplexNoiseSample {
  validateRidgedOptions(options);

  const lacunarity = options.lacunarity ?? 2;
  const persistence = options.persistence ?? 0.5;
  const ridgeOffset = options.ridgeOffset ?? 1;
  const ridgeSharpness = options.ridgeSharpness ?? 1;
  let amplitude = 1;
  let frequency = options.frequency;
  let amplitudeSum = 0;
  let value = 0;
  let gradientX = 0;
  let gradientY = 0;
  let gradientZ = 0;

  for (let octave = 0; octave < options.octaves; octave += 1) {
    const sample = noise.sampleWithGradient(
      position.x * frequency,
      position.y * frequency,
      position.z * frequency
    );
    const rawRidge = ridgeOffset - Math.abs(sample.value);
    const ridgeBase = clamp(rawRidge / ridgeOffset, 0, 1);
    const ridgeValue = ridgeBase ** ridgeSharpness;
    const derivativeByValue =
      rawRidge <= 0 || Math.abs(sample.value) <= Number.EPSILON
        ? 0
        : -Math.sign(sample.value) * ridgeSharpness * (ridgeBase ** (ridgeSharpness - 1)) /
          ridgeOffset;

    value += ridgeValue * amplitude;
    gradientX += sample.gradient.x * derivativeByValue * amplitude * frequency;
    gradientY += sample.gradient.y * derivativeByValue * amplitude * frequency;
    gradientZ += sample.gradient.z * derivativeByValue * amplitude * frequency;
    amplitudeSum += amplitude;
    amplitude *= persistence;
    frequency *= lacunarity;
  }

  return {
    value: value / amplitudeSum,
    gradient: vec3(
      gradientX / amplitudeSum,
      gradientY / amplitudeSum,
      gradientZ / amplitudeSum
    )
  };
}

export function sampleDomainWarp2D(
  noise: SimplexNoise3D,
  position: Vec3,
  options: DomainWarpOptions
): DomainWarpSample {
  validateDomainWarpOptions(options);

  const xWarp = sampleFractalSimplex3D(
    noise,
    vec3(position.x + 31.17, 93.5, position.z - 47.23),
    options
  );
  const zWarp = sampleFractalSimplex3D(
    noise,
    vec3(position.x - 73.81, -18.25, position.z + 11.47),
    options
  );
  const offset = vec3(xWarp.value * options.amplitude, 0, zWarp.value * options.amplitude);

  return {
    offset,
    position: vec3(position.x + offset.x, position.y, position.z + offset.z)
  };
}

export function sampleCellular2D(
  position: Vec3,
  options: CellularNoiseOptions
): CellularNoiseSample {
  validateCellularOptions(options);

  const seed = options.seed ?? 0;
  const sampleX = position.x * options.frequency;
  const sampleZ = position.z * options.frequency;
  const cellX = Math.floor(sampleX);
  const cellZ = Math.floor(sampleZ);
  let nearestDistance = Number.POSITIVE_INFINITY;
  let secondNearestDistance = Number.POSITIVE_INFINITY;
  let nearestCellId = 0;

  for (let dz = -2; dz <= 2; dz += 1) {
    for (let dx = -2; dx <= 2; dx += 1) {
      const candidateX = cellX + dx;
      const candidateZ = cellZ + dz;
      const featureX = candidateX + hash01(candidateX, candidateZ, seed, 0xA53C9E27);
      const featureZ = candidateZ + hash01(candidateX, candidateZ, seed, 0xC2B2AE35);
      const distance = Math.hypot(featureX - sampleX, featureZ - sampleZ);

      if (distance < nearestDistance) {
        secondNearestDistance = nearestDistance;
        nearestDistance = distance;
        nearestCellId = hashUint32(candidateX, candidateZ, seed, 0x9E3779B9);
      } else if (distance < secondNearestDistance) {
        secondNearestDistance = distance;
      }
    }
  }

  return {
    nearestDistance,
    secondNearestDistance,
    edgeDistance: secondNearestDistance - nearestDistance,
    cellId: nearestCellId
  };
}

function validateRidgedOptions(options: RidgedFractalNoiseOptions): void {
  if (!Number.isInteger(options.octaves) || options.octaves <= 0) {
    throw new Error("Ridged fractal noise octaves must be a positive integer.");
  }

  if (options.frequency <= 0) {
    throw new Error("Ridged fractal noise frequency must be positive.");
  }

  if (options.lacunarity !== undefined && options.lacunarity <= 0) {
    throw new Error("Ridged fractal noise lacunarity must be positive.");
  }

  if (options.persistence !== undefined && options.persistence <= 0) {
    throw new Error("Ridged fractal noise persistence must be positive.");
  }

  if (options.ridgeOffset !== undefined && options.ridgeOffset <= 0) {
    throw new Error("Ridged fractal noise ridgeOffset must be positive.");
  }

  if (options.ridgeSharpness !== undefined && options.ridgeSharpness <= 0) {
    throw new Error("Ridged fractal noise ridgeSharpness must be positive.");
  }
}

function validateDomainWarpOptions(options: DomainWarpOptions): void {
  if (options.amplitude < 0) {
    throw new Error("Domain warp amplitude cannot be negative.");
  }
}

function validateCellularOptions(options: CellularNoiseOptions): void {
  if (options.frequency <= 0) {
    throw new Error("Cellular noise frequency must be positive.");
  }
}

function hash01(x: number, z: number, seed: number, salt: number): number {
  return hashUint32(x, z, seed, salt) * UINT32_SCALE;
}

function hashUint32(x: number, z: number, seed: number, salt: number): number {
  let value = (seed ^ salt) >>> 0;
  value ^= Math.imul(x | 0, 0x85EBCA6B);
  value = Math.imul(value ^ (value >>> 13), 0xC2B2AE35);
  value ^= Math.imul(z | 0, 0x27D4EB2F);
  value = Math.imul(value ^ (value >>> 16), 0x165667B1);
  return (value ^ (value >>> 15)) >>> 0;
}
