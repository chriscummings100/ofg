import { equal, throws } from "node:assert/strict";
import { vec4 } from "../math/vec4.js";
import { Material } from "../render/Material.js";
import { Mesh } from "../render/Mesh.js";
import { Texture } from "../render/Texture.js";
import { ResourceStore } from "./ResourceStore.js";

describe("ResourceStore", () => {
  it("adds and retrieves meshes by stable id", () => {
    const store = new ResourceStore();
    const mesh = createMesh("mesh:test");

    equal(store.addMesh(mesh), "mesh:test");
    equal(store.getMesh("mesh:test"), mesh);
  });

  it("throws when a mesh id is missing", () => {
    const store = new ResourceStore();

    throws(() => store.getMesh("mesh:missing"), /does not exist/);
  });

  it("removes meshes", () => {
    const store = new ResourceStore();
    store.addMesh(createMesh("mesh:test"));
    store.removeMesh("mesh:test");

    throws(() => store.getMesh("mesh:test"), /does not exist/);
  });

  it("replaces meshes with the same id", () => {
    const store = new ResourceStore();
    const small = createMesh("mesh:test");
    const large = new Mesh("mesh:test", new Float32Array(12), new Uint32Array(6), {
      floatsPerVertex: 6,
      attributes: [
        { name: "position", offset: 0, size: 3 },
        { name: "color", offset: 3, size: 3 }
      ]
    });

    store.addMesh(small);
    store.addMesh(large);

    equal(store.getMesh("mesh:test"), large);
  });

  it("stores materials and textures independently", () => {
    const store = new ResourceStore();
    const texture = new Texture("texture:test", 1, 1, "rgba8unorm");
    const material = new Material("material:test", vec4(1, 0, 0, 1));

    store.addTexture(texture);
    store.addMaterial(material);

    equal(store.getTexture("texture:test"), texture);
    equal(store.getMaterial("material:test"), material);
  });

  it("throws when material and texture ids are missing", () => {
    const store = new ResourceStore();

    throws(() => store.getTexture("texture:missing"), /Texture resource/);
    throws(() => store.getMaterial("material:missing"), /Material resource/);
  });

  it("removes materials and textures", () => {
    const store = new ResourceStore();
    store.addTexture(new Texture("texture:test", 1, 1, "rgba8unorm"));
    store.addMaterial(new Material("material:test", vec4(1, 0, 0, 1)));

    store.removeTexture("texture:test");
    store.removeMaterial("material:test");

    throws(() => store.getTexture("texture:test"), /Texture resource/);
    throws(() => store.getMaterial("material:test"), /Material resource/);
  });
});

function createMesh(id: string): Mesh {
  return new Mesh(id, new Float32Array([0, 0, 0]), new Uint32Array([0]), {
    floatsPerVertex: 3,
    attributes: [{ name: "position", offset: 0, size: 3 }]
  });
}
