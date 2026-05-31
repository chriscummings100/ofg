import { equal, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import type { TerrainField } from "./scalarField.js";
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

  it("places the final vertex at the positive terrain extent", () => {
    const mesh = buildHeightfieldMesh(createSeedTerrainField(), {
      halfExtent: 4,
      cellsPerAxis: 8
    });
    const finalVertexOffset = mesh.vertices.length - getFloatsPerVertex();

    equal(mesh.vertices[finalVertexOffset], 4);
    equal(mesh.vertices[finalVertexOffset + 2], 4);
  });

  it("supports small meshes", () => {
    const mesh = buildHeightfieldMesh(createFlatField(2), {
      halfExtent: 1,
      cellsPerAxis: 1
    });

    equal(mesh.vertices.length, 4 * getFloatsPerVertex());
    equal(mesh.indices.length, 6);
    equal(mesh.vertices[1], 2);
    equal(mesh.vertices[7], 2);
  });

  it("samples field height for every vertex", () => {
    const mesh = buildHeightfieldMesh(createSlopedField(), {
      halfExtent: 1,
      cellsPerAxis: 2
    });

    for (let offset = 0; offset < mesh.vertices.length; offset += getFloatsPerVertex()) {
      const x = mesh.vertices[offset];
      const y = mesh.vertices[offset + 1];
      const z = mesh.vertices[offset + 2];
      ok(Math.abs(y - (x + z)) < 1e-6);
    }
  });

  it("builds indices within the vertex range", () => {
    const mesh = buildHeightfieldMesh(createSeedTerrainField(), {
      halfExtent: 4,
      cellsPerAxis: 8
    });
    const vertexCount = mesh.vertices.length / getFloatsPerVertex();

    for (const index of mesh.indices) {
      ok(index >= 0);
      ok(index < vertexCount);
    }
  });
});

function createFlatField(height: number): TerrainField {
  return {
    heightAt: () => height,
    densityAt: (position) => position.y - height,
    normalAt: () => vec3(0, 1, 0)
  };
}

function createSlopedField(): TerrainField {
  return {
    heightAt: (x, z) => x + z,
    densityAt: (position) => position.y - position.x - position.z,
    normalAt: () => vec3(0, 1, 0)
  };
}
