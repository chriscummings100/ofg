import { equal, throws } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { vec4 } from "../math/vec4.js";
import { resetScene } from "../scene/activeScene.js";
import { Material } from "./Material.js";
import { Mesh } from "./Mesh.js";
import { MeshRenderer } from "./MeshRenderer.js";
import { Texture } from "./Texture.js";

describe("MeshRenderer", () => {
  it("emits a render item with the entity world matrix", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const entity = scene.createEntity("Rendered");
    entity.transform.setPosition(vec3(3, 4, 5));
    const renderer = entity.addComponent(new MeshRenderer(mesh.id));

    const item = renderer.getRenderItem();

    equal(item?.id, `mesh-renderer:${entity.id}`);
    equal(item?.mesh, mesh);
    equal(item?.worldMatrix[12], 3);
    equal(item?.worldMatrix[13], 4);
    equal(item?.worldMatrix[14], 5);
  });

  it("resolves mesh and material from the global scene resources", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    const material = new Material("material:test", { albedoFactor: vec4(1, 1, 1, 1) });
    scene.resources.addMesh(mesh);
    scene.resources.addMaterial(material);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(mesh.id, material.id));

    const item = renderer.getRenderItem();

    equal(item?.mesh, mesh);
    equal(item?.material, material);
  });

  it("reflects mesh and material property changes", () => {
    const scene = resetScene();
    const firstMesh = createMesh("mesh:first");
    const secondMesh = createMesh("mesh:second");
    const firstMaterial = new Material("material:first", { albedoFactor: vec4(1, 0, 0, 1) });
    const secondMaterial = new Material("material:second", { albedoFactor: vec4(0, 1, 0, 1) });
    scene.resources.addMesh(firstMesh);
    scene.resources.addMesh(secondMesh);
    scene.resources.addMaterial(firstMaterial);
    scene.resources.addMaterial(secondMaterial);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(firstMesh.id, firstMaterial.id));

    renderer.mesh = secondMesh.id;
    renderer.material = secondMaterial.id;

    const item = renderer.getRenderItem();
    equal(item?.mesh, secondMesh);
    equal(item?.material, secondMaterial);
  });

  it("resolves material albedo textures from scene resources", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    const texture = new Texture("texture:albedo", 1, 1, "rgba8unorm", {
      data: new Uint8Array([255, 255, 255, 255])
    });
    const material = new Material("material:test", {
      albedoFactor: vec4(1, 1, 1, 1),
      albedoTexture: texture.id
    });
    scene.resources.addMesh(mesh);
    scene.resources.addTexture(texture);
    scene.resources.addMaterial(material);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(mesh.id, material.id));

    const item = renderer.getRenderItem();

    equal(item?.material, material);
    equal(item?.albedoTexture, texture);
  });

  it("resolves material texture arrays from scene resources", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    const albedo = new Texture("texture:albedo", 1, 1, "rgba8unorm", {
      data: new Uint8Array([255, 255, 255, 255])
    });
    const normal = new Texture("texture:normal", 1, 1, "rgba8unorm", {
      data: new Uint8Array([128, 128, 255, 255])
    });
    const packedMaterial = new Texture("texture:material", 1, 1, "rgba8unorm", {
      data: new Uint8Array([0, 255, 255, 128])
    });
    const material = new Material("material:test", {
      albedoTexture: albedo.id,
      normalTexture: normal.id,
      materialTexture: packedMaterial.id
    });
    scene.resources.addMesh(mesh);
    scene.resources.addTexture(albedo);
    scene.resources.addTexture(normal);
    scene.resources.addTexture(packedMaterial);
    scene.resources.addMaterial(material);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(mesh.id, material.id));

    const item = renderer.getRenderItem();

    equal(item?.albedoTexture, albedo);
    equal(item?.normalTexture, normal);
    equal(item?.materialTexture, packedMaterial);
  });

  it("allows material to be cleared", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    const material = new Material("material:test", { albedoFactor: vec4(1, 1, 1, 1) });
    scene.resources.addMesh(mesh);
    scene.resources.addMaterial(material);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(mesh.id, material.id));

    renderer.material = undefined;

    equal(renderer.getRenderItem()?.material, undefined);
  });

  it("uses parent world transforms", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);
    parent.transform.setPosition(vec3(10, 0, 0));
    child.transform.setPosition(vec3(2, 3, 4));
    const renderer = child.addComponent(new MeshRenderer(mesh.id));

    const item = renderer.getRenderItem();

    equal(item?.worldMatrix[12], 12);
    equal(item?.worldMatrix[13], 3);
    equal(item?.worldMatrix[14], 4);
  });

  it("emits no render item when hidden", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const renderer = scene.createEntity("Rendered").addComponent(new MeshRenderer(mesh.id));
    renderer.visible = false;

    equal(renderer.getRenderItem(), undefined);
  });

  it("emits again when visibility is restored", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const renderer = scene.createEntity("Rendered").addComponent(new MeshRenderer(mesh.id));
    renderer.visible = false;
    equal(renderer.getRenderItem(), undefined);

    renderer.visible = true;

    equal(renderer.getRenderItem()?.mesh, mesh);
  });

  it("emits no render item when disabled", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const renderer = scene.createEntity("Rendered").addComponent(new MeshRenderer(mesh.id));
    renderer.enabled = false;

    equal(renderer.getRenderItem(), undefined);
  });

  it("throws a useful error for missing mesh resources", () => {
    const scene = resetScene();
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer("mesh:missing"));

    throws(() => renderer.getRenderItem(), /Mesh resource 'mesh:missing'/);
  });

  it("throws a useful error for missing material resources", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(mesh.id, "material:missing"));

    throws(() => renderer.getRenderItem(), /Material resource 'material:missing'/);
  });

  it("throws a useful error for missing albedo texture resources", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    const material = new Material("material:test", { albedoTexture: "texture:missing" });
    scene.resources.addMesh(mesh);
    scene.resources.addMaterial(material);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(mesh.id, material.id));

    throws(() => renderer.getRenderItem(), /Texture resource 'texture:missing'/);
  });

  it("throws a useful error for missing material map resources", () => {
    const scene = resetScene();
    const mesh = createMesh("mesh:test");
    const material = new Material("material:test", { materialTexture: "texture:missing" });
    scene.resources.addMesh(mesh);
    scene.resources.addMaterial(material);
    const renderer = scene
      .createEntity("Rendered")
      .addComponent(new MeshRenderer(mesh.id, material.id));

    throws(() => renderer.getRenderItem(), /Texture resource 'texture:missing'/);
  });
});

function createMesh(id: string): Mesh {
  return new Mesh(id, new Float32Array([0, 0, 0]), new Uint32Array([0]), {
    floatsPerVertex: 3,
    attributes: [{ name: "position", offset: 0, size: 3 }]
  });
}
