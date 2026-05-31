import { deepEqual, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { sampleFractalSimplex3D, SimplexNoise3D } from "./simplexNoise3D.js";
import {
  sampleCellular2D,
  sampleDomainWarp2D,
  sampleRidgedFractalSimplex3D
} from "./terrainNoise.js";

describe("terrainNoise", () => {
  it("samples deterministic ridged fractal noise", () => {
    const first = new SimplexNoise3D(123);
    const second = new SimplexNoise3D(123);
    const position = vec3(12.5, 0, -8.25);
    const options = {
      octaves: 4,
      frequency: 0.012,
      lacunarity: 2.1,
      persistence: 0.5,
      ridgeSharpness: 2
    };

    deepEqual(
      sampleRidgedFractalSimplex3D(first, position, options),
      sampleRidgedFractalSimplex3D(second, position, options)
    );
  });

  it("keeps ridged fractal values bounded with sharp local peaks", () => {
    const noise = new SimplexNoise3D(77);
    const ridgedValues: number[] = [];
    const fbmValues: number[] = [];

    for (let z = -6; z <= 6; z += 1) {
      for (let x = -6; x <= 6; x += 1) {
        const position = vec3(x * 17.5, 11.25, z * 17.5);
        ridgedValues.push(sampleRidgedFractalSimplex3D(noise, position, {
          octaves: 4,
          frequency: 0.011,
          lacunarity: 2,
          persistence: 0.5,
          ridgeSharpness: 2.2
        }).value);
        fbmValues.push(clamp01(sampleFractalSimplex3D(noise, position, {
          octaves: 4,
          frequency: 0.011,
          lacunarity: 2,
          persistence: 0.5
        }).value * 0.5 + 0.5));
      }
    }

    const ridgedUpperSpread = percentile(ridgedValues, 0.95) - percentile(ridgedValues, 0.5);
    const fbmUpperSpread = percentile(fbmValues, 0.95) - percentile(fbmValues, 0.5);

    ok(ridgedValues.every((value) => value >= 0 && value <= 1));
    ok(Math.max(...ridgedValues) > 0.75);
    ok(ridgedUpperSpread > fbmUpperSpread * 0.5);
  });

  it("samples finite deterministic domain warps with local continuity", () => {
    const noise = new SimplexNoise3D(90210);
    const options = {
      octaves: 3,
      frequency: 0.006,
      lacunarity: 2,
      persistence: 0.5,
      amplitude: 18
    };
    const first = sampleDomainWarp2D(noise, vec3(32, 0, -16), options);
    const second = sampleDomainWarp2D(new SimplexNoise3D(90210), vec3(32, 0, -16), options);
    const nearby = sampleDomainWarp2D(noise, vec3(32.1, 0, -15.9), options);

    deepEqual(first, second);
    ok(Number.isFinite(first.offset.x));
    ok(Number.isFinite(first.offset.z));
    ok(Math.hypot(first.offset.x - nearby.offset.x, first.offset.z - nearby.offset.z) < 1);
  });

  it("samples deterministic cellular distances and ids", () => {
    const options = { frequency: 0.025, seed: 42 };
    const position = vec3(17.5, 0, -3.25);
    const first = sampleCellular2D(position, options);
    const second = sampleCellular2D(position, options);

    deepEqual(first, second);
    ok(first.nearestDistance >= 0);
    ok(first.secondNearestDistance >= first.nearestDistance);
    ok(first.edgeDistance >= 0);
    ok(Number.isInteger(first.cellId));
  });
});

function percentile(values: readonly number[], amount: number): number {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.floor((sorted.length - 1) * amount)];
}

function clamp01(value: number): number {
  return Math.min(1, Math.max(0, value));
}
