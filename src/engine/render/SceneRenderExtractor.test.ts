import { equal, throws } from "node:assert/strict";
import { quatFromYaw } from "../math/quat.js";
import { vec3 } from "../math/vec3.js";
import { resetScene } from "../scene/activeScene.js";
import { createSeedTerrainField } from "../world/scalarField.js";
import { Mesh } from "./Mesh.js";
import { MeshRenderer } from "./MeshRenderer.js";
import { SceneRenderExtractor } from "./SceneRenderExtractor.js";
import { TerrainRenderer } from "./TerrainRenderer.js";

describe("SceneRenderExtractor", () => {
  it("builds a render world from the active scene", () => {
    const scene = resetScene();
    scene.activeCamera = scene.createEntity("Camera");

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.items.length, 0);
    equal(renderWorld.camera.eye.x, 0);
    equal(renderWorld.camera.inverseViewProjection.length, 16);
    equal(renderWorld.mainLight, scene.mainLight);
  });

  it("uses the scene main light", () => {
    const scene = resetScene();
    scene.activeCamera = scene.createEntity("Camera");
    scene.mainLight = {
      direction: vec3(1, 0, 0),
      color: vec3(1, 0.8, 0.6),
      intensity: 1.5,
      ambient: 0.2
    };

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.mainLight, scene.mainLight);
  });

  it("includes mesh renderer items", () => {
    const scene = resetScene();
    scene.activeCamera = scene.createEntity("Camera");
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    scene.createEntity("Rendered").addComponent(new MeshRenderer(mesh.id));

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.items.length, 1);
    equal(renderWorld.items[0].mesh, mesh);
  });

  it("includes terrain renderer items", () => {
    const scene = resetScene();
    scene.activeCamera = scene.createEntity("Camera");
    const mesh = createMesh("mesh:terrain");
    scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh }]
    ));

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.items.length, 1);
    equal(renderWorld.items[0].mesh, mesh);
  });

  it("keeps mesh items before terrain items", () => {
    const scene = resetScene();
    scene.activeCamera = scene.createEntity("Camera");
    const mesh = createMesh("mesh:actor");
    const terrainMesh = createMesh("mesh:terrain");
    scene.resources.addMesh(mesh);
    scene.createEntity("Rendered").addComponent(new MeshRenderer(mesh.id));
    scene.createEntity("Terrain").addComponent(new TerrainRenderer(
      createSeedTerrainField(),
      [{ key: "0,0,0", mesh: terrainMesh }]
    ));

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.items[0].mesh, mesh);
    equal(renderWorld.items[1].mesh, terrainMesh);
  });

  it("excludes disabled entities", () => {
    const scene = resetScene();
    scene.activeCamera = scene.createEntity("Camera");
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const entity = scene.createEntity("Rendered");
    entity.addComponent(new MeshRenderer(mesh.id));
    entity.enabled = false;

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.items.length, 0);
  });

  it("excludes hidden mesh renderers", () => {
    const scene = resetScene();
    scene.activeCamera = scene.createEntity("Camera");
    const mesh = createMesh("mesh:test");
    scene.resources.addMesh(mesh);
    const renderer = scene.createEntity("Rendered").addComponent(new MeshRenderer(mesh.id));
    renderer.visible = false;

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.items.length, 0);
  });

  it("uses the scene active camera", () => {
    const scene = resetScene();
    const camera = scene.createEntity("Camera");
    camera.transform.setPosition(vec3(3, 4, 5));
    scene.activeCamera = camera;

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(renderWorld.camera.eye.x, 3);
    equal(renderWorld.camera.eye.y, 4);
    equal(renderWorld.camera.eye.z, 5);
  });

  it("uses active camera rotation for target direction", () => {
    const scene = resetScene();
    const camera = scene.createEntity("Camera");
    camera.transform.setRotation(quatFromYaw(Math.PI / 2));
    scene.activeCamera = camera;

    const renderWorld = SceneRenderExtractor.buildRenderWorld();

    equal(Math.round(renderWorld.camera.target.x), 1);
    equal(Math.round(renderWorld.camera.target.z), 0);
  });

  it("throws when no active camera is set", () => {
    resetScene();

    throws(() => SceneRenderExtractor.buildRenderWorld(), /activeCamera/);
  });
});

function createMesh(id: string): Mesh {
  return new Mesh(id, new Float32Array([0, 0, 0]), new Uint32Array([0]), {
    floatsPerVertex: 3,
    attributes: [{ name: "position", offset: 0, size: 3 }]
  });
}
