import { equal } from "node:assert/strict";
import { createSeedTerrainField } from "./scalarField.js";
import { buildHeightfieldMesh, getFloatsPerVertex } from "./terrainMesh.js";

describe("terrainMesh", () => {
  it("builds a heightfield mesh with shared vertices", () => {
    const mesh = buildHeightfieldMesh(createSeedTerrainField(), {
      halfExtent: 4,
      cellsPerAxis: 8
    });

    equal(mesh.vertices.length, 9 * 9 * getFloatsPerVertex());
    equal(mesh.indices.length, 8 * 8 * 6);
  });

  it("places the first vertex at the negative terrain extent", () => {
    const mesh = buildHeightfieldMesh(createSeedTerrainField(), {
      halfExtent: 4,
      cellsPerAxis: 8
    });

    equal(mesh.vertices[0], -4);
    equal(mesh.vertices[2], -4);
  });
});
