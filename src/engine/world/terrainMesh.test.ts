import { deepEqual, equal, ok } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import type { TerrainField } from "./scalarField.js";
import { createSeedTerrainField } from "./scalarField.js";
import { packTerrainMaterialWeights, terrainMaterialLayer } from "./terrainMaterials.js";
import {
  MATERIAL_INDICES_VERTEX_OFFSET,
  MATERIAL_WEIGHTS_VERTEX_OFFSET,
  POSITION_COLOR_NORMAL_UV_LAYOUT,
  buildHeightfieldMesh,
  expandTerrainMeshForTriangleMaterialPalettes,
  getFloatsPerVertex,
  writePackedTerrainMaterial
} from "./terrainMesh.js";

describe("terrainMesh", () => {
  it("builds a heightfield mesh with shared vertices", () => {
    const mesh = buildHeightfieldMesh(createSeedTerrainField(), {
      halfExtent: 4,
      cellsPerAxis: 8
    });

    equal(mesh.vertices.length, 9 * 9 * getFloatsPerVertex());
    equal(mesh.indices.length, 8 * 8 * 6);
  });

  it("defines the terrain vertex material layout", () => {
    equal(getFloatsPerVertex(), 19);
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[4].name, "materialIndices");
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[4].offset, MATERIAL_INDICES_VERTEX_OFFSET);
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[5].name, "materialWeights");
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[5].offset, MATERIAL_WEIGHTS_VERTEX_OFFSET);
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
    equal(mesh.vertices[getFloatsPerVertex() + 1], 2);
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

  it("writes smooth field normals for every vertex", () => {
    const mesh = buildHeightfieldMesh(createFieldWithKnownNormal(), {
      halfExtent: 1,
      cellsPerAxis: 2
    });

    for (let offset = 0; offset < mesh.vertices.length; offset += getFloatsPerVertex()) {
      equal(mesh.vertices[offset + 6], 0.25);
      equal(mesh.vertices[offset + 7], 0.5);
      equal(mesh.vertices[offset + 8], 0.75);
    }
  });

  it("writes normalized uv coordinates over the terrain extent", () => {
    const mesh = buildHeightfieldMesh(createFlatField(0), {
      halfExtent: 1,
      cellsPerAxis: 2
    });
    const finalVertexOffset = mesh.vertices.length - getFloatsPerVertex();

    equal(mesh.vertices[9], 0);
    equal(mesh.vertices[10], 0);
    equal(mesh.vertices[finalVertexOffset + 9], 1);
    equal(mesh.vertices[finalVertexOffset + 10], 1);
  });

  it("writes default material weights for heightfield vertices", () => {
    const mesh = buildHeightfieldMesh(createFlatField(0), {
      halfExtent: 1,
      cellsPerAxis: 1
    });

    equal(mesh.vertices[MATERIAL_INDICES_VERTEX_OFFSET], 0);
    equal(mesh.vertices[MATERIAL_WEIGHTS_VERTEX_OFFSET], 1);
    equal(mesh.vertices[MATERIAL_WEIGHTS_VERTEX_OFFSET + 1], 0);
  });

  it("expands indexed terrain triangles to triangle-local material palettes", () => {
    const floatsPerVertex = getFloatsPerVertex();
    const vertices = new Float32Array(3 * floatsPerVertex);
    writePackedTerrainMaterial(
      vertices,
      0,
      packTerrainMaterialWeights([{ material: "meadowGrass", weight: 1 }])
    );
    writePackedTerrainMaterial(
      vertices,
      floatsPerVertex,
      packTerrainMaterialWeights([{ material: "snow", weight: 1 }])
    );
    writePackedTerrainMaterial(
      vertices,
      floatsPerVertex * 2,
      packTerrainMaterialWeights([{ material: "cliffRock", weight: 1 }])
    );

    const expanded = expandTerrainMeshForTriangleMaterialPalettes({
      vertices,
      indices: new Uint32Array([0, 1, 2])
    });

    deepEqual(Array.from(expanded.indices), [0, 1, 2]);
    equal(expanded.vertices.length, 3 * floatsPerVertex);
    for (let offset = 0; offset < expanded.vertices.length; offset += floatsPerVertex) {
      deepEqual(
        Array.from(expanded.vertices.slice(offset + MATERIAL_INDICES_VERTEX_OFFSET, offset + MATERIAL_INDICES_VERTEX_OFFSET + 4)),
        [
          terrainMaterialLayer("meadowGrass"),
          terrainMaterialLayer("cliffRock"),
          terrainMaterialLayer("snow"),
          0
        ]
      );
    }
    deepEqual(
      Array.from(expanded.vertices.slice(MATERIAL_WEIGHTS_VERTEX_OFFSET, MATERIAL_WEIGHTS_VERTEX_OFFSET + 4)),
      [1, 0, 0, 0]
    );
    deepEqual(
      Array.from(expanded.vertices.slice(floatsPerVertex + MATERIAL_WEIGHTS_VERTEX_OFFSET, floatsPerVertex + MATERIAL_WEIGHTS_VERTEX_OFFSET + 4)),
      [0, 0, 1, 0]
    );
    deepEqual(
      Array.from(expanded.vertices.slice(floatsPerVertex * 2 + MATERIAL_WEIGHTS_VERTEX_OFFSET, floatsPerVertex * 2 + MATERIAL_WEIGHTS_VERTEX_OFFSET + 4)),
      [0, 1, 0, 0]
    );
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

function createFieldWithKnownNormal(): TerrainField {
  return {
    heightAt: () => 0,
    densityAt: (position) => position.y,
    normalAt: () => vec3(0.25, 0.5, 0.75)
  };
}
