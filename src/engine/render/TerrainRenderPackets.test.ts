import { equal, throws } from "node:assert/strict";
import { identityMat4 } from "../math/mat4.js";
import { vec4 } from "../math/vec4.js";
import { ResourceStore } from "../scene/ResourceStore.js";
import { terrainChunkCoord } from "../world/terrainChunk.js";
import { Material } from "./Material.js";
import { Mesh } from "./Mesh.js";
import { TerrainRenderPacketStore } from "./TerrainRenderPackets.js";
import { Texture } from "./Texture.js";

describe("TerrainRenderPacketStore", () => {
  it("adds and replaces chunk packets by key", () => {
    const store = new TerrainRenderPacketStore();
    const first = { key: "0,0,0", mesh: createMesh("mesh:first") };
    const second = { key: "1,0,0", mesh: createMesh("mesh:second") };
    const replacement = { key: "0,0,0", mesh: createMesh("mesh:replacement") };

    store.addChunk(first);
    store.addChunk(second);
    store.addChunk(replacement);

    equal(store.chunks.length, 2);
    equal(store.getChunk("0,0,0"), replacement);
    equal(store.chunks[0], replacement);
    equal(store.chunks[1], second);
  });

  it("removes chunk packets by coordinate", () => {
    const store = new TerrainRenderPacketStore({
      chunks: [{ key: "2,-1,3", mesh: createMesh("mesh:terrain") }]
    });

    equal(store.removeChunk(terrainChunkCoord(2, -1, 3)), true);
    equal(store.removeChunk(terrainChunkCoord(2, -1, 3)), false);
    equal(store.chunks.length, 0);
  });

  it("retains only requested chunk packets", () => {
    const store = new TerrainRenderPacketStore({
      chunks: [
        { key: "0,0,0", mesh: createMesh("mesh:first") },
        { key: "1,0,0", mesh: createMesh("mesh:second") },
        { key: "2,0,0", mesh: createMesh("mesh:third") }
      ]
    });

    store.retainChunks(["1,0,0", terrainChunkCoord(2, 0, 0)]);

    equal(store.chunks.length, 2);
    equal(store.chunks[0].key, "1,0,0");
    equal(store.chunks[1].key, "2,0,0");
  });

  it("does not retain ownership of constructor chunk arrays", () => {
    const chunks = [{ key: "0,0,0", mesh: createMesh("mesh:first") }];
    const store = new TerrainRenderPacketStore({ chunks });
    chunks.push({ key: "1,0,0", mesh: createMesh("mesh:second") });

    equal(store.chunks.length, 1);
  });

  it("emits render items without a scene component", () => {
    const resources = new ResourceStore();
    const mesh = createMesh("mesh:terrain");
    const material = new Material("material:terrain", {
      albedoFactor: vec4(0.25, 0.5, 0.75, 1)
    });
    resources.addMaterial(material);
    const chunkMatrix = identityMat4();
    chunkMatrix[12] = 32;
    const worldMatrix = identityMat4();
    worldMatrix[12] = 5;
    const store = new TerrainRenderPacketStore({
      itemIdPrefix: "terrain:rust",
      chunks: [{ key: "1,0,0", mesh, material: material.id, worldMatrix: chunkMatrix }]
    });

    const items = store.getRenderItems(resources, worldMatrix);

    equal(items.length, 1);
    equal(items[0].id, "terrain:rust:1,0,0");
    equal(items[0].mesh, mesh);
    equal(items[0].material, material);
    equal(items[0].worldMatrix[12], 37);
  });

  it("resolves terrain material textures from an explicit resource store", () => {
    const resources = new ResourceStore();
    const albedo = new Texture("texture:terrain.albedo", 1, 1, "rgba8unorm", {
      data: new Uint8Array([0, 255, 0, 255])
    });
    const normal = new Texture("texture:terrain.normal", 1, 1, "rgba8unorm", {
      data: new Uint8Array([128, 128, 255, 255])
    });
    const packedMaterial = new Texture("texture:terrain.material", 1, 1, "rgba8unorm", {
      data: new Uint8Array([0, 255, 255, 128])
    });
    const material = new Material("material:terrain", {
      albedoTexture: albedo.id,
      normalTexture: normal.id,
      materialTexture: packedMaterial.id
    });
    resources.addTexture(albedo);
    resources.addTexture(normal);
    resources.addTexture(packedMaterial);
    resources.addMaterial(material);
    const store = new TerrainRenderPacketStore({
      chunks: [{ key: "0,0,0", mesh: createMesh("mesh:terrain"), material: material.id }]
    });

    const items = store.getRenderItems(resources);

    equal(items[0].albedoTexture, albedo);
    equal(items[0].normalTexture, normal);
    equal(items[0].materialTexture, packedMaterial);
  });

  it("throws useful errors for missing packet resources", () => {
    const resources = new ResourceStore();
    const store = new TerrainRenderPacketStore({
      chunks: [{ key: "0,0,0", mesh: createMesh("mesh:terrain"), material: "material:missing" }]
    });

    throws(() => store.getRenderItems(resources), /Material resource 'material:missing'/);
  });
});

function createMesh(id: string): Mesh {
  return new Mesh(id, new Float32Array([0, 0, 0]), new Uint32Array([0]), {
    floatsPerVertex: 3,
    attributes: [{ name: "position", offset: 0, size: 3 }]
  });
}
