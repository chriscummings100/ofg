import { equal, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { createSeedTerrainField } from "./scalarField.js";

describe("scalarField", () => {
  it("reports zero density on the terrain surface", () => {
    const field = createSeedTerrainField();
    const x = 8;
    const z = -3;
    const y = field.heightAt(x, z);

    equal(field.densityAt(vec3(x, y, z)), 0);
  });

  it("returns upward-facing normals for the seed terrain", () => {
    const field = createSeedTerrainField();
    const normal = field.normalAt(2, 5);

    ok(normal.y > 0.8);
    ok(Math.abs(Math.hypot(normal.x, normal.y, normal.z) - 1) < 1e-12);
  });
});
