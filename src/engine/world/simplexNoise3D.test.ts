import { equal, notEqual, ok, throws } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { sampleFractalSimplex3D, SimplexNoise3D } from "./simplexNoise3D.js";

describe("SimplexNoise3D", () => {
  it("returns deterministic values for a seed", () => {
    const first = new SimplexNoise3D(1234);
    const second = new SimplexNoise3D(1234);

    equal(first.sample(0.25, -1.5, 3.75), second.sample(0.25, -1.5, 3.75));
  });

  it("uses the seed to choose a different gradient lattice", () => {
    const first = new SimplexNoise3D(1);
    const second = new SimplexNoise3D(2);

    notEqual(first.sample(2.25, 1.5, -0.75), second.sample(2.25, 1.5, -0.75));
  });

  it("returns finite values and gradients", () => {
    const sample = new SimplexNoise3D(7).sampleWithGradient(12.5, -3.25, 8.75);

    ok(Number.isFinite(sample.value));
    ok(Math.abs(sample.value) <= 1.25);
    ok(Number.isFinite(sample.gradient.x));
    ok(Number.isFinite(sample.gradient.y));
    ok(Number.isFinite(sample.gradient.z));
  });

  it("reports analytic gradients that match finite differences", () => {
    const noise = new SimplexNoise3D(19);
    const x = 0.123;
    const y = -0.456;
    const z = 0.789;
    const epsilon = 1e-4;
    const sample = noise.sampleWithGradient(x, y, z);

    const dx = (noise.sample(x + epsilon, y, z) - noise.sample(x - epsilon, y, z)) / (epsilon * 2);
    const dy = (noise.sample(x, y + epsilon, z) - noise.sample(x, y - epsilon, z)) / (epsilon * 2);
    const dz = (noise.sample(x, y, z + epsilon) - noise.sample(x, y, z - epsilon)) / (epsilon * 2);

    ok(Math.abs(sample.gradient.x - dx) < 1e-5);
    ok(Math.abs(sample.gradient.y - dy) < 1e-5);
    ok(Math.abs(sample.gradient.z - dz) < 1e-5);
  });

  it("combines octaves while keeping gradients in input coordinate space", () => {
    const noise = new SimplexNoise3D(29);
    const position = vec3(3.5, -2.25, 7.75);
    const options = {
      octaves: 4,
      frequency: 0.07,
      lacunarity: 2,
      persistence: 0.5
    };
    const epsilon = 1e-3;
    const sample = sampleFractalSimplex3D(noise, position, options);
    const right = sampleFractalSimplex3D(noise, vec3(position.x + epsilon, position.y, position.z), options);
    const left = sampleFractalSimplex3D(noise, vec3(position.x - epsilon, position.y, position.z), options);
    const dx = (right.value - left.value) / (epsilon * 2);

    ok(Number.isFinite(sample.value));
    ok(Math.abs(sample.gradient.x - dx) < 1e-5);
  });

  it("validates fractal noise options", () => {
    const noise = new SimplexNoise3D();

    throws(() => sampleFractalSimplex3D(noise, vec3(0, 0, 0), {
      octaves: 0,
      frequency: 1
    }), /octaves/);
    throws(() => sampleFractalSimplex3D(noise, vec3(0, 0, 0), {
      octaves: 1,
      frequency: 0
    }), /frequency/);
    throws(() => sampleFractalSimplex3D(noise, vec3(0, 0, 0), {
      octaves: 1,
      frequency: 1,
      lacunarity: 0
    }), /lacunarity/);
    throws(() => sampleFractalSimplex3D(noise, vec3(0, 0, 0), {
      octaves: 1,
      frequency: 1,
      persistence: 0
    }), /persistence/);
  });
});
