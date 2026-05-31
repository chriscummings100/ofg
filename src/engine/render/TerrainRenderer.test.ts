import { equal, throws } from "node:assert/strict";
import { identityMat4 } from "../math/mat4.js";
import { vec3 } from "../math/vec3.js";
import { vec4 } from "../math/vec4.js";
import { resetScene } from "../scene/activeScene.js";
import { createSeedTerrainField } from "../world/scalarField.js";
import { Material } from "./Material.js";
import { Mesh } from "./Mesh.js";
import { TerrainRenderer } from "./TerrainRenderer.js";
import { Texture } from "./Texture.js";

describe("TerrainRenderer", () => {
  it("delegates height queries to the terrain field", () => {
    const field = createSeedTerrainField();
    const terrain = new TerrainRenderer(field);

    equal(terrain.heightAt(2, 3), field.heightAt(2, 3));
  });

  it("delegates density queries to the terrain field", () => {
    const field = createSeedTerrainField();
    const terrain = new TerrainRenderer(field);
    const position = vec3(2, 4, 3);

    equal(terrain.densityAt(position), field.densityAt(position));
  });

  it("registers itself as scene terrain when attached", () => {
    const scene = resetScene();
    const terrain = scene
      .createEntity("Terrain")
      .addComponent(new TerrainRenderer(createSeedTerrainField()));

    equal(scene.terrain, terrain);
  });

  it("clears scene terrain when detached", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Terrain");
    const terrain = entity.addComponent(new TerrainRenderer(createSeedTerrainField()));

    entity.removeComponent(terrain);

    equal(scene.terrain, undefined);
  });

  it("does not clear scene terrain when detaching an older terrain", () => {
    const scene = resetScene();
    const firstEntity = scene.createEntity("First terrain");
    const first = firstEntity.addComponent(new TerrainRenderer(createSeedTerrainField()));
    const second = scene
      .createEntity("Second terrain")
      .addComponent(new TerrainRenderer(createSeedTerrainField()));

    firstEntity.removeComponent(first);

    equal(scene.terrain, second);
  });

  it("emits terrain render items", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:terrain");
    const terrain = scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh }]
    ));

    const items = terrain.getRenderItems();

    equal(items.length, 1);
    equal(items[0].id, `terrain:${terrain.entity?.id}:0,0,0`);
    equal(items[0].mesh, mesh);
  });

  it("emits multiple chunks with material and world matrix", () => {
    const scene = resetScene();
    const firstMesh = createMesh("mesh:first");
    const secondMesh = createMesh("mesh:second");
    const material = new Material("material:terrain", { albedoFactor: vec4(0, 1, 0, 1) });
    scene.resources.addMaterial(material);
    const secondMatrix = identityMat4();
    secondMatrix[12] = 32;
    const terrain = scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [
        { key: "0,0,0", mesh: firstMesh },
        { key: "1,0,0", mesh: secondMesh, material: material.id, worldMatrix: secondMatrix }
      ]
    ));

    const items = terrain.getRenderItems();

    equal(items.length, 2);
    equal(items[0].worldMatrix[12], 0);
    equal(items[1].mesh, secondMesh);
    equal(items[1].material, material);
    equal(items[1].worldMatrix[12], 32);
  });

  it("resolves terrain albedo textures from scene resources", () => {
    const scene = resetScene();
    const texture = new Texture("texture:terrain", 1, 1, "rgba8unorm", {
      data: new Uint8Array([0, 255, 0, 255])
    });
    const material = new Material("material:terrain", {
      albedoTexture: texture.id
    });
    scene.resources.addTexture(texture);
    scene.resources.addMaterial(material);
    const terrain = scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh: createMesh("mesh:terrain"), material: material.id }]
    ));

    const items = terrain.getRenderItems();

    equal(items[0].material, material);
    equal(items[0].albedoTexture, texture);
  });

  it("throws a useful error for missing terrain material resources", () => {
    const scene = resetScene();
    const terrain = scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh: createMesh("mesh:terrain"), material: "material:missing" }]
    ));

    throws(() => terrain.getRenderItems(), /Material resource 'material:missing'/);
  });

  it("throws a useful error for missing terrain albedo texture resources", () => {
    const scene = resetScene();
    const material = new Material("material:terrain", { albedoTexture: "texture:missing" });
    scene.resources.addMaterial(material);
    const terrain = scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh: createMesh("mesh:terrain"), material: material.id }]
    ));

    throws(() => terrain.getRenderItems(), /Texture resource 'texture:missing'/);
  });

  it("applies the terrain entity world transform to chunk matrices", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Terrain");
    entity.transform.setPosition(vec3(5, 0, 0));
    const chunkMatrix = identityMat4();
    chunkMatrix[12] = 32;
    const terrain = entity.addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "1,0,0", mesh: createMesh("mesh:terrain"), worldMatrix: chunkMatrix }]
    ));

    const items = terrain.getRenderItems();

    equal(items[0].worldMatrix[12], 37);
  });

  it("emits no render items when disabled", () => {
    const scene = resetScene();
    const terrain = scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh: createMesh("mesh:terrain") }]
    ));
    terrain.enabled = false;

    equal(terrain.getRenderItems().length, 0);
  });

  it("emits no render items after being detached", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Terrain");
    const terrain = entity.addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh: createMesh("mesh:terrain") }]
    ));

    entity.removeComponent(terrain);

    equal(terrain.getRenderItems().length, 0);
  });
});

function createMesh(id: string): Mesh {
  return new Mesh(id, new Float32Array([0, 0, 0]), new Uint32Array([0]), {
    floatsPerVertex: 3,
    attributes: [{ name: "position", offset: 0, size: 3 }]
  });
}
