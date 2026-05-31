import { vec3, type Vec3 } from "../math/vec3.js";

export type SimplexNoiseSample = {
  readonly value: number;
  readonly gradient: Vec3;
};

export type FractalNoiseOptions = {
  readonly octaves: number;
  readonly frequency: number;
  readonly lacunarity?: number;
  readonly persistence?: number;
};

const F3 = 1 / 3;
const G3 = 1 / 6;
const NOISE_SCALE = 32;

const GRADIENTS: readonly Vec3[] = Object.freeze([
  vec3(1, 1, 0),
  vec3(-1, 1, 0),
  vec3(1, -1, 0),
  vec3(-1, -1, 0),
  vec3(1, 0, 1),
  vec3(-1, 0, 1),
  vec3(1, 0, -1),
  vec3(-1, 0, -1),
  vec3(0, 1, 1),
  vec3(0, -1, 1),
  vec3(0, 1, -1),
  vec3(0, -1, -1)
]);

export class SimplexNoise3D {
  private readonly perm: Uint8Array;

  constructor(seed = 0) {
    this.perm = buildPermutation(seed);
  }

  sample(x: number, y: number, z: number): number {
    return this.sampleWithGradient(x, y, z).value;
  }

  sampleWithGradient(x: number, y: number, z: number): SimplexNoiseSample {
    const skew = (x + y + z) * F3;
    const i = fastFloor(x + skew);
    const j = fastFloor(y + skew);
    const k = fastFloor(z + skew);
    const unskew = (i + j + k) * G3;
    const cellOriginX = i - unskew;
    const cellOriginY = j - unskew;
    const cellOriginZ = k - unskew;
    const x0 = x - cellOriginX;
    const y0 = y - cellOriginY;
    const z0 = z - cellOriginZ;
    const offsets = simplexCornerOffsets(x0, y0, z0);

    let value = 0;
    let gradientX = 0;
    let gradientY = 0;
    let gradientZ = 0;

    for (const offset of offsets) {
      const xCorner = x0 - offset.x + offset.unskew;
      const yCorner = y0 - offset.y + offset.unskew;
      const zCorner = z0 - offset.z + offset.unskew;
      const corner = cornerContribution(
        this.gradientAt(i + offset.x, j + offset.y, k + offset.z),
        xCorner,
        yCorner,
        zCorner
      );
      value += corner.value;
      gradientX += corner.gradient.x;
      gradientY += corner.gradient.y;
      gradientZ += corner.gradient.z;
    }

    return {
      value: value * NOISE_SCALE,
      gradient: vec3(
        gradientX * NOISE_SCALE,
        gradientY * NOISE_SCALE,
        gradientZ * NOISE_SCALE
      )
    };
  }

  private gradientAt(i: number, j: number, k: number): Vec3 {
    const hash = this.perm[
      (i + this.perm[(j + this.perm[k & 255]) & 255]) & 255
    ];

    return GRADIENTS[hash % GRADIENTS.length];
  }
}

export function sampleFractalSimplex3D(
  noise: SimplexNoise3D,
  position: Vec3,
  options: FractalNoiseOptions
): SimplexNoiseSample {
  validateFractalOptions(options);

  const lacunarity = options.lacunarity ?? 2;
  const persistence = options.persistence ?? 0.5;
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
    value += sample.value * amplitude;
    gradientX += sample.gradient.x * amplitude * frequency;
    gradientY += sample.gradient.y * amplitude * frequency;
    gradientZ += sample.gradient.z * amplitude * frequency;
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

type SimplexCornerOffset = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
  readonly unskew: number;
};

function simplexCornerOffsets(x0: number, y0: number, z0: number): readonly SimplexCornerOffset[] {
  let i1 = 0;
  let j1 = 0;
  let k1 = 0;
  let i2 = 0;
  let j2 = 0;
  let k2 = 0;

  if (x0 >= y0) {
    if (y0 >= z0) {
      i1 = 1;
      i2 = 1;
      j2 = 1;
    } else if (x0 >= z0) {
      i1 = 1;
      i2 = 1;
      k2 = 1;
    } else {
      k1 = 1;
      i2 = 1;
      k2 = 1;
    }
  } else if (y0 < z0) {
    k1 = 1;
    j2 = 1;
    k2 = 1;
  } else if (x0 < z0) {
    j1 = 1;
    j2 = 1;
    k2 = 1;
  } else {
    j1 = 1;
    i2 = 1;
    j2 = 1;
  }

  return [
    { x: 0, y: 0, z: 0, unskew: 0 },
    { x: i1, y: j1, z: k1, unskew: G3 },
    { x: i2, y: j2, z: k2, unskew: 2 * G3 },
    { x: 1, y: 1, z: 1, unskew: 3 * G3 }
  ];
}

function cornerContribution(gradient: Vec3, x: number, y: number, z: number): SimplexNoiseSample {
  const attenuation = 0.6 - x * x - y * y - z * z;
  if (attenuation <= 0) {
    return {
      value: 0,
      gradient: vec3(0, 0, 0)
    };
  }

  const dot = gradient.x * x + gradient.y * y + gradient.z * z;
  const attenuation2 = attenuation * attenuation;
  const attenuation3 = attenuation2 * attenuation;
  const attenuation4 = attenuation2 * attenuation2;
  const derivativeScale = -8 * attenuation3 * dot;

  return {
    value: attenuation4 * dot,
    gradient: vec3(
      attenuation4 * gradient.x + derivativeScale * x,
      attenuation4 * gradient.y + derivativeScale * y,
      attenuation4 * gradient.z + derivativeScale * z
    )
  };
}

function validateFractalOptions(options: FractalNoiseOptions): void {
  if (!Number.isInteger(options.octaves) || options.octaves <= 0) {
    throw new Error("Fractal simplex noise octaves must be a positive integer.");
  }

  if (options.frequency <= 0) {
    throw new Error("Fractal simplex noise frequency must be positive.");
  }

  if (options.lacunarity !== undefined && options.lacunarity <= 0) {
    throw new Error("Fractal simplex noise lacunarity must be positive.");
  }

  if (options.persistence !== undefined && options.persistence <= 0) {
    throw new Error("Fractal simplex noise persistence must be positive.");
  }
}

function buildPermutation(seed: number): Uint8Array {
  const values = new Uint8Array(256);
  for (let index = 0; index < values.length; index += 1) {
    values[index] = index;
  }

  const random = mulberry32(seed >>> 0);
  for (let index = values.length - 1; index > 0; index -= 1) {
    const swapIndex = Math.floor(random() * (index + 1));
    const value = values[index];
    values[index] = values[swapIndex];
    values[swapIndex] = value;
  }

  const perm = new Uint8Array(512);
  for (let index = 0; index < perm.length; index += 1) {
    perm[index] = values[index & 255];
  }

  return perm;
}

function mulberry32(seed: number): () => number {
  let state = seed;
  return () => {
    state = (state + 0x6D2B79F5) >>> 0;
    let value = state;
    value = Math.imul(value ^ (value >>> 15), value | 1);
    value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
    return ((value ^ (value >>> 14)) >>> 0) / 4294967296;
  };
}

function fastFloor(value: number): number {
  return Math.floor(value);
}
