import { equal } from "node:assert/strict";
import { Mesh } from "./Mesh.js";

describe("Mesh", () => {
  it("stores small mesh buffers and layout", () => {
    const vertices = new Float32Array([0, 1, 2]);
    const indices = new Uint32Array([0]);
    const mesh = new Mesh("mesh:small", vertices, indices, {
      floatsPerVertex: 3,
      attributes: [{ name: "position", offset: 0, size: 3 }]
    });

    equal(mesh.id, "mesh:small");
    equal(mesh.vertices, vertices);
    equal(mesh.indices, indices);
    equal(mesh.layout.floatsPerVertex, 3);
  });

  it("stores larger interleaved mesh layouts", () => {
    const mesh = new Mesh("mesh:large", new Float32Array(6 * 128), new Uint32Array(256), {
      floatsPerVertex: 6,
      attributes: [
        { name: "position", offset: 0, size: 3 },
        { name: "color", offset: 3, size: 3 }
      ]
    });

    equal(mesh.vertices.length, 768);
    equal(mesh.indices.length, 256);
    equal(mesh.layout.attributes[1].name, "color");
  });
});
