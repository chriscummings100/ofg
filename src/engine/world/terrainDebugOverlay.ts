import { clamp, normalize, vec3, type Vec3 } from "../math/vec3.js";
import {
  analyzeDualContouringCellVertex,
  extractHermiteIntersectionsForBounds
} from "./dualContouring.js";
import { TERRAIN_CHUNK_CELLS_PER_AXIS } from "./terrainChunk.js";
import type { TerrainGenerator, TerrainMaterialWeight } from "./terrainGenerator.js";

export const TERRAIN_DEBUG_OVERLAY_MODES = [
  "macroElevation",
  "mountainness",
  "slope",
  "normal",
  "densitySlice",
  "materialWeights",
  "qefError",
  "chunkBorders"
] as const;

export type TerrainDebugOverlayMode = typeof TERRAIN_DEBUG_OVERLAY_MODES[number];
export type TerrainDebugOverlayState = TerrainDebugOverlayMode | "off";

export type TerrainDebugOverlayOptions = {
  readonly center: Vec3;
  readonly mode: TerrainDebugOverlayMode;
  readonly resolution?: number;
  readonly worldSize?: number;
  readonly sliceY?: number;
  readonly chunkSize?: number;
};

export type TerrainDebugOverlay = {
  readonly mode: TerrainDebugOverlayMode;
  readonly width: number;
  readonly height: number;
  readonly pixels: Uint8ClampedArray;
};

const DEFAULT_RESOLUTION = 72;
const DEFAULT_WORLD_SIZE = 96;
const DEFAULT_CHUNK_SIZE = TERRAIN_CHUNK_CELLS_PER_AXIS;

export function buildTerrainDebugOverlay(
  terrain: TerrainGenerator,
  options: TerrainDebugOverlayOptions
): TerrainDebugOverlay {
  const resolution = options.resolution ?? DEFAULT_RESOLUTION;
  const worldSize = options.worldSize ?? DEFAULT_WORLD_SIZE;
  const chunkSize = options.chunkSize ?? DEFAULT_CHUNK_SIZE;
  validateOverlayOptions(resolution, worldSize, chunkSize);

  const pixels = new Uint8ClampedArray(resolution * resolution * 4);
  const halfWorldSize = worldSize * 0.5;
  const centerHeight = terrain.heightAt(options.center.x, options.center.z);
  const sliceY = options.sliceY ?? centerHeight;

  for (let py = 0; py < resolution; py += 1) {
    for (let px = 0; px < resolution; px += 1) {
      const u = resolution === 1 ? 0.5 : px / (resolution - 1);
      const v = resolution === 1 ? 0.5 : py / (resolution - 1);
      const x = options.center.x + u * worldSize - halfWorldSize;
      const z = options.center.z + v * worldSize - halfWorldSize;
      const color = sampleDebugColor(
        terrain,
        options.mode,
        vec3(x, sliceY, z),
        centerHeight,
        chunkSize
      );
      writePixel(pixels, resolution, px, py, color);
    }
  }

  return Object.freeze({
    mode: options.mode,
    width: resolution,
    height: resolution,
    pixels
  });
}

export function isTerrainDebugOverlayMode(value: string): value is TerrainDebugOverlayMode {
  return TERRAIN_DEBUG_OVERLAY_MODES.some((mode) => mode === value);
}

export function nextTerrainDebugOverlayState(
  current: TerrainDebugOverlayState
): TerrainDebugOverlayState {
  if (current === "off") {
    return TERRAIN_DEBUG_OVERLAY_MODES[0];
  }

  const index = TERRAIN_DEBUG_OVERLAY_MODES.indexOf(current);
  if (index === -1 || index === TERRAIN_DEBUG_OVERLAY_MODES.length - 1) {
    return "off";
  }

  return TERRAIN_DEBUG_OVERLAY_MODES[index + 1];
}

function sampleDebugColor(
  terrain: TerrainGenerator,
  mode: TerrainDebugOverlayMode,
  position: Vec3,
  centerHeight: number,
  chunkSize: number
): Vec3 {
  if (mode === "macroElevation") {
    return elevationRamp((terrain.macroAt(position).baseElevation - centerHeight) / 42);
  }

  if (mode === "mountainness") {
    const mountainness = terrain.macroAt(position).mountainness;
    return vec3(40 + mountainness * 215, 42 + mountainness * 76, 84 + mountainness * 142);
  }

  if (mode === "densitySlice") {
    const density = terrain.densityAt(position);
    const air = clamp(density / 18, 0, 1);
    const solid = clamp(-density / 18, 0, 1);
    const surface = 1 - clamp(Math.abs(density) / 2, 0, 1);
    return vec3(
      44 + solid * 170 + surface * 40,
      58 + surface * 170,
      80 + air * 170 + surface * 40
    );
  }

  const surfaceY = terrain.heightAt(position.x, position.z);
  const surface = terrain.surfaceAt(vec3(position.x, surfaceY, position.z));
  const normal = normalize(surface.gradient);

  if (mode === "slope") {
    const slope = clamp(1 - normal.y, 0, 1);
    return vec3(26 + slope * 229, 78 + slope * 156, 50 + slope * 48);
  }

  if (mode === "normal") {
    return vec3(
      (normal.x * 0.5 + 0.5) * 255,
      (normal.y * 0.5 + 0.5) * 255,
      (normal.z * 0.5 + 0.5) * 255
    );
  }

  if (mode === "materialWeights") {
    return materialWeightColor(surface.materialWeights);
  }

  if (mode === "qefError") {
    return qefErrorColor(terrain, position);
  }

  const base = elevationRamp((terrain.macroAt(position).baseElevation - centerHeight) / 42);
  if (isNearChunkBoundary(position.x, chunkSize) || isNearChunkBoundary(position.z, chunkSize)) {
    return vec3(255, 232, 112);
  }

  return vec3(base.x * 0.32, base.y * 0.4, base.z * 0.46);
}

function qefErrorColor(terrain: TerrainGenerator, position: Vec3): Vec3 {
  const surfaceY = terrain.heightAt(position.x, position.z);
  const debug = analyzeSurfaceCellQef(terrain, position.x, surfaceY, position.z);
  if (debug === undefined) {
    return vec3(16, 20, 38);
  }

  const error = clamp(Math.log2(debug.finalError * 256 + 1) / 8, 0, 1);
  const fallback = debug.fallbackReason === "none" ? 0 : 1;
  const intersectionSignal = clamp(debug.intersectionCount / 6, 0, 1);

  return vec3(
    24 + error * 180 + fallback * 50,
    62 + (1 - error) * 150 - fallback * 40,
    90 + intersectionSignal * 118 - fallback * 50
  );
}

function analyzeSurfaceCellQef(
  terrain: TerrainGenerator,
  x: number,
  y: number,
  z: number
) {
  const firstCellY = Math.floor(y);
  for (let yOffset = -1; yOffset <= 1; yOffset += 1) {
    const min = vec3(Math.floor(x), firstCellY + yOffset, Math.floor(z));
    const bounds = {
      min,
      max: vec3(min.x + 1, min.y + 1, min.z + 1)
    };
    const intersections = extractHermiteIntersectionsForBounds(bounds, terrain);
    if (intersections.length === 0) {
      continue;
    }

    return analyzeDualContouringCellVertex(intersections, bounds);
  }

  return undefined;
}

function materialWeightColor(weights: readonly TerrainMaterialWeight[]): Vec3 {
  let vegetation = 0;
  let soil = 0;
  let rock = 0;
  let snow = 0;

  for (const weight of weights) {
    if (
      weight.material === "meadowGrass" ||
      weight.material === "dryGround" ||
      weight.material === "forestGround" ||
      weight.material === "leafLitter" ||
      weight.material === "mossRock"
    ) {
      vegetation += weight.weight;
    } else if (
      weight.material === "bareSoil" ||
      weight.material === "dryMud" ||
      weight.material === "wetMud" ||
      weight.material === "sand" ||
      weight.material === "gravelSand" ||
      weight.material === "redSoil"
    ) {
      soil += weight.weight;
    } else if (
      weight.material === "scree" ||
      weight.material === "rockyGround" ||
      weight.material === "cliffRock" ||
      weight.material === "riverPebbles"
    ) {
      rock += weight.weight;
    } else if (weight.material === "snow") {
      snow += weight.weight;
    }
  }

  return vec3(
    42 + soil * 185 + rock * 104 + snow * 168,
    56 + vegetation * 168 + soil * 76 + rock * 92 + snow * 176,
    48 + rock * 150 + snow * 188
  );
}

function elevationRamp(value: number): Vec3 {
  const normalized = clamp(value * 0.5 + 0.5, 0, 1);
  if (normalized < 0.5) {
    const t = normalized / 0.5;
    return vec3(36 + t * 44, 65 + t * 112, 126 - t * 62);
  }

  const t = (normalized - 0.5) / 0.5;
  return vec3(80 + t * 154, 177 + t * 48, 64 + t * 118);
}

function isNearChunkBoundary(value: number, chunkSize: number): boolean {
  const wrapped = positiveModulo(value, chunkSize);
  return wrapped < 0.75 || wrapped > chunkSize - 0.75;
}

function positiveModulo(value: number, divisor: number): number {
  return ((value % divisor) + divisor) % divisor;
}

function writePixel(
  pixels: Uint8ClampedArray,
  width: number,
  x: number,
  y: number,
  color: Vec3
): void {
  const offset = (y * width + x) * 4;
  pixels[offset] = color.x;
  pixels[offset + 1] = color.y;
  pixels[offset + 2] = color.z;
  pixels[offset + 3] = 255;
}

function validateOverlayOptions(resolution: number, worldSize: number, chunkSize: number): void {
  if (!Number.isInteger(resolution) || resolution <= 0) {
    throw new Error("Terrain debug overlay resolution must be a positive integer.");
  }

  if (worldSize <= 0) {
    throw new Error("Terrain debug overlay worldSize must be positive.");
  }

  if (chunkSize <= 0) {
    throw new Error("Terrain debug overlay chunkSize must be positive.");
  }
}
