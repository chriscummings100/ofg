import { equal } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { createBoxMesh } from "./primitiveMesh.js";
import { getFloatsPerVertex } from "./terrainMesh.js";

describe("primitiveMesh", () => {
  it("builds a box with eight shared vertices and twelve triangles", () => {
    const mesh = createBoxMesh(vec3(0, 0, 0), vec3(1, 1, 1), vec3(1, 0, 0));

    equal(mesh.vertices.length, 8 * getFloatsPerVertex());
    equal(mesh.indices.length, 12 * 3);
  });

  it("applies center and half size to vertex positions", () => {
    const mesh = createBoxMesh(vec3(10, 20, 30), vec3(1, 2, 3), vec3(1, 0, 0));

    equal(mesh.vertices[0], 9);
    equal(mesh.vertices[1], 18);
    equal(mesh.vertices[2], 27);
    equal(mesh.vertices[6], 11);
    equal(mesh.vertices[7], 18);
    equal(mesh.vertices[8], 27);
  });

  it("writes vertex colors for every box corner", () => {
    const mesh = createBoxMesh(vec3(0, 0, 0), vec3(1, 1, 1), vec3(0.25, 0.5, 0.75));

    for (let offset = 0; offset < mesh.vertices.length; offset += getFloatsPerVertex()) {
      equal(mesh.vertices[offset + 3], 0.25);
      equal(mesh.vertices[offset + 4], 0.5);
      equal(mesh.vertices[offset + 5], 0.75);
    }
  });
});
