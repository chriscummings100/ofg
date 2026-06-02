import { deepEqual, equal } from "node:assert/strict";
import { packTerrainMaterialWeights, terrainMaterialLayer } from "./terrainMaterials.js";
import {
  MATERIAL_INDICES_VERTEX_OFFSET,
  MATERIAL_WEIGHTS_VERTEX_OFFSET,
  POSITION_COLOR_NORMAL_UV_LAYOUT,
  expandTerrainMeshForTriangleMaterialPalettes,
  getFloatsPerVertex,
  writePackedTerrainMaterial
} from "./terrainMesh.js";

describe("terrainMesh", () => {
  it("defines the terrain vertex material layout", () => {
    equal(getFloatsPerVertex(), 19);
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[4].name, "materialIndices");
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[4].offset, MATERIAL_INDICES_VERTEX_OFFSET);
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[5].name, "materialWeights");
    equal(POSITION_COLOR_NORMAL_UV_LAYOUT.attributes[5].offset, MATERIAL_WEIGHTS_VERTEX_OFFSET);
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
