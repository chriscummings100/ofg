import { equal, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { createSeedTerrainField } from "./scalarField.js";

describe("scalarField", () => {
  it("reports zero density on the terrain surface", () => {
    const field = createSeedTerrainField();
    const x = 8;
    const z = -3;
    const y = field.heightAt(x, z);

    ok(Math.abs(field.densityAt(vec3(x, y, z))) < 0.001);
  });

  it("returns upward-facing normals for the seed terrain", () => {
    const field = createSeedTerrainField();
    const normal = field.normalAt(2, 5);

    ok(normal.y > 0.25);
    ok(Math.abs(Math.hypot(normal.x, normal.y, normal.z) - 1) < 1e-12);
  });

  it("samples density and gradients at arbitrary 3D positions", () => {
    const field = createSeedTerrainField();
    const position = vec3(9, 4, -12);
    const sample = field.sampleAt?.(position);

    if (sample === undefined) {
      throw new Error("Seed terrain field should expose density samples.");
    }

    equal(sample.density, field.densityAt(position));
    ok(Number.isFinite(sample.gradient.x));
    ok(Number.isFinite(sample.gradient.y));
    ok(Number.isFinite(sample.gradient.z));
    ok(Math.hypot(sample.gradient.x, sample.gradient.y, sample.gradient.z) > 0);
  });

  it("uses deterministic noise terrain with useful height variation", () => {
    const first = createSeedTerrainField();
    const second = createSeedTerrainField();
    const heights = [
      first.heightAt(-48, -48),
      first.heightAt(-16, 24),
      first.heightAt(0, 0),
      first.heightAt(32, -8),
      first.heightAt(64, 40)
    ];

    equal(first.heightAt(32, -8), second.heightAt(32, -8));
    ok(heights.every(Number.isFinite));
    ok(Math.max(...heights) - Math.min(...heights) > 5);
  });

  it("uses 3D detail noise inside the density field", () => {
    const field = createSeedTerrainField();
    const samplePoints = [
      vec3(3, 5, -7),
      vec3(17, -2, 23),
      vec3(41, 11, -19)
    ];
    const verticalDeltas = samplePoints.map((position) =>
      field.densityAt(vec3(position.x, position.y + 6, position.z)) -
      field.densityAt(position)
    );

    ok(verticalDeltas.some((delta) => Math.abs(delta - 6) > 0.05));
  });

  it("returns normals that point from solid toward air", () => {
    const field = createSeedTerrainField();
    const x = 17;
    const z = 23;
    const y = field.heightAt(x, z);
    const normal = field.normalAt(x, z);
    const step = 0.05;
    const airSide = field.densityAt(vec3(
      x + normal.x * step,
      y + normal.y * step,
      z + normal.z * step
    ));
    const solidSide = field.densityAt(vec3(
      x - normal.x * step,
      y - normal.y * step,
      z - normal.z * step
    ));

    ok(airSide > solidSide);
  });
});
