import { equal } from "node:assert/strict";
import { Component } from "./Component.js";
import { resetScene } from "./activeScene.js";
import { createDirectionalLight } from "../render/Lighting.js";
import { vec3 } from "../math/vec3.js";

class TestComponent extends Component {
  updates = 0;

  override update(): void {
    this.updates += 1;
  }
}

describe("Scene", () => {
  it("findByName returns the first enabled matching entity", () => {
    const scene = resetScene();
    scene.createEntity("Target");

    equal(scene.findByName("Target")?.name, "Target");
  });

  it("findByName skips disabled entities", () => {
    const scene = resetScene();
    const disabled = scene.createEntity("Target");
    disabled.enabled = false;
    const enabled = scene.createEntity("Target");

    equal(scene.findByName("Target"), enabled);
  });

  it("queryComponents returns matching components", () => {
    const scene = resetScene();
    const component = scene.createEntity("Entity").addComponent(new TestComponent());

    equal(scene.queryComponents(TestComponent)[0], component);
  });

  it("queryComponents skips disabled entity subtrees", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);
    child.addComponent(new TestComponent());
    parent.enabled = false;

    equal(scene.queryComponents(TestComponent).length, 0);
  });

  it("update skips disabled entity subtrees", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);
    const component = child.addComponent(new TestComponent());
    parent.enabled = false;

    scene.update(1);

    equal(component.updates, 0);
  });

  it("stores the main directional light for render extraction", () => {
    const scene = resetScene();
    const light = createDirectionalLight({ direction: vec3(1, 1, 0), ambient: 0.4 });

    scene.mainLight = light;

    equal(scene.mainLight, light);
  });

  it("destroyEntity clears activeCamera when destroying an ancestor", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const camera = scene.createEntity("Camera");
    parent.addChild(camera);
    scene.activeCamera = camera;

    scene.destroyEntity(parent);

    equal(scene.activeCamera, undefined);
  });

  it("destroyEntity keeps activeCamera when destroying an unrelated entity", () => {
    const scene = resetScene();
    const camera = scene.createEntity("Camera");
    const unrelated = scene.createEntity("Unrelated");
    scene.activeCamera = camera;

    scene.destroyEntity(unrelated);

    equal(scene.activeCamera, camera);
  });
});
