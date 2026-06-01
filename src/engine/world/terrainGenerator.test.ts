import { deepEqual, equal, ok, throws } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import {
  createSeedWorldDescriptor,
  createTerrainGenerator,
  TERRAIN_PRESET_IDS,
  type BiomeWeight,
  type TerrainMaterialId,
  type TerrainPresetId,
  type TerrainMaterialWeight
} from "./terrainGenerator.js";

describe("terrainGenerator", () => {
  it("returns identical samples for the same world descriptor", () => {
    const descriptor = createSeedWorldDescriptor(1234, { seaLevel: -3 });
    const first = createTerrainGenerator(descriptor);
    const second = createTerrainGenerator(descriptor);
    const position = vec3(12.5, 4.25, -19.75);

    deepEqual(first.macroAt(position), second.macroAt(position));
    deepEqual(first.biomeAt(position), second.biomeAt(position));
    deepEqual(first.surfaceAt(position), second.surfaceAt(position));
    equal(first.heightAt(12.5, -19.75), second.heightAt(12.5, -19.75));
  });

  it("produces different macro samples for different seeds", () => {
    const first = createTerrainGenerator(createSeedWorldDescriptor(1001));
    const second = createTerrainGenerator(createSeedWorldDescriptor(2002));
    const positions = [
      vec3(-48, 0, -48),
      vec3(-12.5, 0, 22.75),
      vec3(31.25, 0, -7.5),
      vec3(64, 0, 40)
    ];
    const totalElevationDelta = positions.reduce((total, position) =>
      total + Math.abs(first.macroAt(position).baseElevation - second.macroAt(position).baseElevation),
    0);

    ok(totalElevationDelta > 0.001);
  });

  it("keeps densityAt and sampleAt density in agreement", () => {
    const generator = createTerrainGenerator();
    const position = vec3(9, 4, -12);

    equal(generator.densityAt(position), generator.sampleAt(position).density);
    equal(generator.densityAt(position), generator.surfaceAt(position).density);
  });

  it("finds a height near the zero-density surface", () => {
    const generator = createTerrainGenerator();
    const x = 3.25;
    const z = -2.5;
    const y = generator.heightAt(x, z);

    ok(Math.abs(generator.densityAt(vec3(x, y, z))) < 0.001);
  });

  it("includes normalized biome and material weights", () => {
    const generator = createTerrainGenerator();
    const sample = generator.surfaceAt(vec3(7, 3, -11));

    equal(sumBiomeWeights(sample.biomeWeights), 1);
    ok(Math.abs(sumMaterialWeights(sample.materialWeights) - 1) < 1e-12);
    ok(sample.materialWeights.length >= 1);
    ok(sample.biomeWeights.length >= 1);
  });

  it("prefers snow at high cold altitude", () => {
    const generator = createTerrainGenerator();
    const sample = generator.surfaceAt(vec3(0, 60, 0));

    equal(dominantMaterial(sample.materialWeights), "snow");
    ok(materialWeight(sample.materialWeights, "snow") > 0.75);
  });

  it("prefers cliff rock on steep rocky highland surfaces", () => {
    const generator = createTerrainGenerator(
      createSeedWorldDescriptor(246, { terrainPreset: "rockyHighland" })
    );
    const y = generator.heightAt(-152, -192);
    const sample = generator.surfaceAt(vec3(-152, y, -192));

    equal(dominantMaterial(sample.materialWeights), "cliffRock");
    ok(materialWeight(sample.materialWeights, "cliffRock") > 0.75);
  });

  it("adds wet and sandy materials around lowland sea-level terrain", () => {
    const generator = createTerrainGenerator();
    const sample = generator.surfaceAt(vec3(0, 0, 0));

    ok(materialWeight(sample.materialWeights, "wetMud") > 0.15);
    ok(materialWeight(sample.materialWeights, "sand") > 0.1);
  });

  it("keeps material weights continuous across chunk boundary positions", () => {
    const generator = createTerrainGenerator();
    const leftOfBoundary = generator.surfaceAt(vec3(31.999, 2, -7.5)).materialWeights;
    const rightOfBoundary = generator.surfaceAt(vec3(32.001, 2, -7.5)).materialWeights;

    for (const material of ["meadowGrass", "dryGround", "mossRock", "redSoil"] as const) {
      ok(Math.abs(materialWeight(leftOfBoundary, material) - materialWeight(rightOfBoundary, material)) < 0.01);
    }
  });

  it("preserves descriptor values on the generator", () => {
    const descriptor = createSeedWorldDescriptor(42, { seaLevel: 5 });
    const generator = createTerrainGenerator(descriptor);

    equal(generator.descriptor, descriptor);
    equal(generator.descriptor.seed, 42);
    equal(generator.descriptor.seaLevel, 5);
  });

  it("uses the rolling hills preset by default", () => {
    equal(createSeedWorldDescriptor().terrainPreset, "rollingHills");
  });

  for (const terrainPreset of TERRAIN_PRESET_IDS) {
    it(`samples finite macro landforms for the ${terrainPreset} preset`, () => {
      const generator = createTerrainGenerator(
        createSeedWorldDescriptor(932, { terrainPreset })
      );
      const elevations = sampleMacroGrid(generator.macroAt).map((sample) => sample.baseElevation);

      ok(elevations.every(Number.isFinite));
      ok(Math.max(...elevations) - Math.min(...elevations) > 2);
    });
  }

  it("keeps macro values continuous across chunk boundary positions", () => {
    const generator = createTerrainGenerator(
      createSeedWorldDescriptor(932, { terrainPreset: "rockyHighland" })
    );
    const leftOfBoundary = generator.macroAt(vec3(31.999, 0, -7.5));
    const rightOfBoundary = generator.macroAt(vec3(32.001, 0, -7.5));

    ok(Math.abs(leftOfBoundary.baseElevation - rightOfBoundary.baseElevation) < 0.1);
    ok(Math.abs(leftOfBoundary.mountainness - rightOfBoundary.mountainness) < 0.01);
  });

  it("rejects unknown terrain presets at runtime", () => {
    throws(() => createTerrainGenerator({
      ...createSeedWorldDescriptor(),
      terrainPreset: "unknown" as TerrainPresetId
    }), /Unknown terrain preset/);
  });
});

function sumBiomeWeights(weights: readonly BiomeWeight[]): number {
  return weights.reduce((total, weight) => total + weight.weight, 0);
}

function sumMaterialWeights(weights: readonly TerrainMaterialWeight[]): number {
  return weights.reduce((total, weight) => total + weight.weight, 0);
}

function dominantMaterial(weights: readonly TerrainMaterialWeight[]): TerrainMaterialId {
  return weights.reduce((best, weight) => weight.weight > best.weight ? weight : best).material;
}

function materialWeight(
  weights: readonly TerrainMaterialWeight[],
  material: TerrainMaterialId
): number {
  return weights.find((weight) => weight.material === material)?.weight ?? 0;
}

function sampleMacroGrid(
  macroAt: (position: ReturnType<typeof vec3>) => { readonly baseElevation: number }
): readonly { readonly baseElevation: number }[] {
  const samples: { readonly baseElevation: number }[] = [];
  for (let z = -2; z <= 2; z += 1) {
    for (let x = -2; x <= 2; x += 1) {
      samples.push(macroAt(vec3(x * 48, 0, z * 48)));
    }
  }

  return samples;
}
