import { distance, vec3, type Vec3 } from "../math/vec3.js";

export const TERRAIN_CHUNK_CELLS_PER_AXIS = 32;
export const TERRAIN_CHUNK_SAMPLES_PER_AXIS = TERRAIN_CHUNK_CELLS_PER_AXIS + 1;
export const TERRAIN_CHUNK_SAMPLE_COUNT =
  TERRAIN_CHUNK_SAMPLES_PER_AXIS *
  TERRAIN_CHUNK_SAMPLES_PER_AXIS *
  TERRAIN_CHUNK_SAMPLES_PER_AXIS;

export type TerrainChunkCoord = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
};

export type TerrainChunkSampleCoord = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
};

export type TerrainChunkKey = string;

export type TerrainChunkBounds = {
  readonly min: Vec3;
  readonly max: Vec3;
};

export type TerrainDensitySample = {
  readonly density: number;
  readonly gradient: Vec3;
};

export type TerrainDensitySource = {
  readonly densityAt: (position: Vec3) => number;
  readonly sampleAt?: (position: Vec3) => TerrainDensitySample;
};

export type TerrainEdit = {
  readonly id: string;
  readonly bounds: TerrainChunkBounds;
  readonly apply: (density: number, position: Vec3) => number;
};

export type TerrainDensityChunkOptions = {
  readonly cellSize?: number;
  readonly densities?: Float32Array;
};

export type GenerateTerrainDensityChunkOptions = {
  readonly cellSize?: number;
  readonly edits?: readonly TerrainEdit[];
};

export type TerrainDensityChunkGenerator = (
  source: TerrainDensitySource,
  coord: TerrainChunkCoord,
  options?: GenerateTerrainDensityChunkOptions
) => TerrainDensityChunk;

export type SphereTerrainEditOptions = {
  readonly id: string;
  readonly center: Vec3;
  readonly radius: number;
};

export class TerrainDensityChunk {
  readonly coord: TerrainChunkCoord;
  readonly key: TerrainChunkKey;
  readonly cellSize: number;
  readonly densities: Float32Array;

  constructor(coord: TerrainChunkCoord, options: TerrainDensityChunkOptions = {}) {
    const cellSize = options.cellSize ?? 1;
    assertPositiveCellSize(cellSize);

    const densities = options.densities ?? new Float32Array(TERRAIN_CHUNK_SAMPLE_COUNT);
    if (densities.length !== TERRAIN_CHUNK_SAMPLE_COUNT) {
      throw new Error(
        `Terrain density chunks require ${TERRAIN_CHUNK_SAMPLE_COUNT} samples.`
      );
    }

    this.coord = terrainChunkCoord(coord.x, coord.y, coord.z);
    this.key = terrainChunkKey(this.coord);
    this.cellSize = cellSize;
    this.densities = densities;
  }

  densityAtSample(sample: TerrainChunkSampleCoord): number {
    return this.densities[terrainChunkSampleIndex(sample)];
  }

  setDensityAtSample(sample: TerrainChunkSampleCoord, density: number): void {
    this.densities[terrainChunkSampleIndex(sample)] = density;
  }

  samplePosition(sample: TerrainChunkSampleCoord): Vec3 {
    return terrainChunkSamplePosition(this.coord, sample, this.cellSize);
  }

  bounds(): TerrainChunkBounds {
    return terrainChunkBounds(this.coord, this.cellSize);
  }
}

export class EditableTerrainDensitySource implements TerrainDensitySource {
  readonly base: TerrainDensitySource;
  readonly edits: TerrainEdit[] = [];

  constructor(base: TerrainDensitySource, edits: readonly TerrainEdit[] = []) {
    this.base = base;
    for (const edit of edits) {
      this.addEdit(edit);
    }
  }

  densityAt(position: Vec3): number {
    return applyTerrainEdits(this.base.densityAt(position), position, this.edits);
  }

  sampleAt(position: Vec3): TerrainDensitySample {
    if (this.edits.length === 0) {
      const baseSample = this.base.sampleAt?.(position);
      if (baseSample !== undefined) {
        return baseSample;
      }
    }

    return estimateTerrainDensitySample((samplePosition) => this.densityAt(samplePosition), position, 0.01);
  }

  addEdit(edit: TerrainEdit): void {
    this.removeEdit(edit.id);
    this.edits.push(edit);
  }

  removeEdit(id: string): boolean {
    const index = this.edits.findIndex((edit) => edit.id === id);
    if (index === -1) {
      return false;
    }

    this.edits.splice(index, 1);
    return true;
  }

  clearEdits(): void {
    this.edits.length = 0;
  }
}

export function sampleTerrainDensity(
  source: TerrainDensitySource,
  position: Vec3,
  gradientStep = 0.01
): TerrainDensitySample {
  const sample = source.sampleAt?.(position);
  if (sample !== undefined) {
    return sample;
  }

  if (gradientStep <= 0) {
    throw new Error("Terrain density gradient step must be positive.");
  }

  return estimateTerrainDensitySample(source.densityAt, position, gradientStep);
}

function estimateTerrainDensitySample(
  densityAt: (position: Vec3) => number,
  position: Vec3,
  gradientStep: number
): TerrainDensitySample {
  const density = densityAt(position);
  const x0 = densityAt(vec3(position.x - gradientStep, position.y, position.z));
  const x1 = densityAt(vec3(position.x + gradientStep, position.y, position.z));
  const y0 = densityAt(vec3(position.x, position.y - gradientStep, position.z));
  const y1 = densityAt(vec3(position.x, position.y + gradientStep, position.z));
  const z0 = densityAt(vec3(position.x, position.y, position.z - gradientStep));
  const z1 = densityAt(vec3(position.x, position.y, position.z + gradientStep));
  const invSpan = 1 / (gradientStep * 2);

  return {
    density,
    gradient: vec3(
      (x1 - x0) * invSpan,
      (y1 - y0) * invSpan,
      (z1 - z0) * invSpan
    )
  };
}

export function terrainChunkCoord(x: number, y: number, z: number): TerrainChunkCoord {
  assertInteger("x", x);
  assertInteger("y", y);
  assertInteger("z", z);
  return Object.freeze({ x, y, z });
}

export function terrainChunkKey(coord: TerrainChunkCoord): TerrainChunkKey {
  return `${coord.x},${coord.y},${coord.z}`;
}

export function parseTerrainChunkKey(key: TerrainChunkKey): TerrainChunkCoord {
  const match = /^(-?\d+),(-?\d+),(-?\d+)$/.exec(key);
  if (match === null) {
    throw new Error(`Invalid terrain chunk key '${key}'.`);
  }

  return terrainChunkCoord(
    Number.parseInt(match[1], 10),
    Number.parseInt(match[2], 10),
    Number.parseInt(match[3], 10)
  );
}

export function terrainChunkOrigin(coord: TerrainChunkCoord, cellSize = 1): Vec3 {
  assertPositiveCellSize(cellSize);
  const chunkSize = TERRAIN_CHUNK_CELLS_PER_AXIS * cellSize;
  return vec3(coord.x * chunkSize, coord.y * chunkSize, coord.z * chunkSize);
}

export function terrainChunkBounds(coord: TerrainChunkCoord, cellSize = 1): TerrainChunkBounds {
  const min = terrainChunkOrigin(coord, cellSize);
  const chunkSize = TERRAIN_CHUNK_CELLS_PER_AXIS * cellSize;
  return {
    min,
    max: vec3(min.x + chunkSize, min.y + chunkSize, min.z + chunkSize)
  };
}

export function terrainChunkCoordContainingPosition(
  position: Vec3,
  cellSize = 1
): TerrainChunkCoord {
  assertPositiveCellSize(cellSize);
  const chunkSize = TERRAIN_CHUNK_CELLS_PER_AXIS * cellSize;
  return terrainChunkCoord(
    Math.floor(position.x / chunkSize),
    Math.floor(position.y / chunkSize),
    Math.floor(position.z / chunkSize)
  );
}

export function terrainChunkSampleIndex(sample: TerrainChunkSampleCoord): number {
  assertSampleCoord("x", sample.x);
  assertSampleCoord("y", sample.y);
  assertSampleCoord("z", sample.z);

  return sample.x +
    sample.y * TERRAIN_CHUNK_SAMPLES_PER_AXIS +
    sample.z * TERRAIN_CHUNK_SAMPLES_PER_AXIS * TERRAIN_CHUNK_SAMPLES_PER_AXIS;
}

export function terrainChunkSamplePosition(
  coord: TerrainChunkCoord,
  sample: TerrainChunkSampleCoord,
  cellSize = 1
): Vec3 {
  const origin = terrainChunkOrigin(coord, cellSize);
  assertSampleCoord("x", sample.x);
  assertSampleCoord("y", sample.y);
  assertSampleCoord("z", sample.z);

  return vec3(
    origin.x + sample.x * cellSize,
    origin.y + sample.y * cellSize,
    origin.z + sample.z * cellSize
  );
}

export function generateTerrainDensityChunk(
  source: TerrainDensitySource,
  coord: TerrainChunkCoord,
  options: GenerateTerrainDensityChunkOptions = {}
): TerrainDensityChunk {
  const chunk = new TerrainDensityChunk(coord, { cellSize: options.cellSize });
  const edits = options.edits ?? [];

  for (let z = 0; z < TERRAIN_CHUNK_SAMPLES_PER_AXIS; z += 1) {
    for (let y = 0; y < TERRAIN_CHUNK_SAMPLES_PER_AXIS; y += 1) {
      for (let x = 0; x < TERRAIN_CHUNK_SAMPLES_PER_AXIS; x += 1) {
        const sample = { x, y, z };
        const position = chunk.samplePosition(sample);
        const density = applyTerrainEdits(source.densityAt(position), position, edits);
        chunk.setDensityAtSample(sample, density);
      }
    }
  }

  return chunk;
}

export function applyTerrainEdits(
  density: number,
  position: Vec3,
  edits: readonly TerrainEdit[]
): number {
  let editedDensity = density;
  for (const edit of edits) {
    editedDensity = edit.apply(editedDensity, position);
  }

  return editedDensity;
}

export function createSubtractSphereEdit(options: SphereTerrainEditOptions): TerrainEdit {
  const { center, radius } = options;
  if (radius <= 0) {
    throw new Error("Sphere terrain edit radius must be positive.");
  }

  return {
    id: options.id,
    bounds: {
      min: vec3(center.x - radius, center.y - radius, center.z - radius),
      max: vec3(center.x + radius, center.y + radius, center.z + radius)
    },
    apply(density, position) {
      const distanceFromCenter = distance(position, center);
      if (distanceFromCenter >= radius) {
        return density;
      }

      return Math.max(density, radius - distanceFromCenter);
    }
  };
}

function assertInteger(name: string, value: number): void {
  if (!Number.isInteger(value)) {
    throw new Error(`Terrain chunk ${name} coordinate must be an integer.`);
  }
}

function assertSampleCoord(name: string, value: number): void {
  if (!Number.isInteger(value) || value < 0 || value >= TERRAIN_CHUNK_SAMPLES_PER_AXIS) {
    throw new Error(
      `Terrain chunk sample ${name} must be an integer from 0 to ` +
      `${TERRAIN_CHUNK_SAMPLES_PER_AXIS - 1}.`
    );
  }
}

function assertPositiveCellSize(cellSize: number): void {
  if (cellSize <= 0) {
    throw new Error("Terrain chunk cellSize must be positive.");
  }
}
