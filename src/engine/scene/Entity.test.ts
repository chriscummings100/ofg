import { equal, ok, throws } from "node:assert/strict";
import { Component } from "./Component.js";
import { vec3 } from "../math/vec3.js";
import { resetScene } from "./activeScene.js";

class LifecycleComponent extends Component {
  detached = 0;

  override onDetach(): void {
    this.detached += 1;
  }
}

describe("Entity", () => {
  it("creates entities with stable unique ids", () => {
    const scene = resetScene();
    const first = scene.createEntity("First");
    const second = scene.createEntity("Second");

    ok(first.id > 0);
    ok(second.id > first.id);
  });

  it("parents new scene entities under the root", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Child");

    equal(entity.parent, scene.root);
    equal(scene.root.children.includes(entity), true);
  });

  it("reparenting removes the child from its previous parent", () => {
    const scene = resetScene();
    const firstParent = scene.createEntity("First parent");
    const secondParent = scene.createEntity("Second parent");
    const child = scene.createEntity("Child");

    firstParent.addChild(child);
    secondParent.addChild(child);

    equal(firstParent.children.includes(child), false);
    equal(secondParent.children.includes(child), true);
    equal(child.parent, secondParent);
  });

  it("removeChild detaches the child and clears transform parent", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);
    parent.transform.setPosition(vec3(10, 0, 0));
    child.transform.setPosition(vec3(1, 0, 0));
    equal(child.transform.getWorldPosition().x, 11);

    parent.removeChild(child);

    equal(child.parent, undefined);
    equal(parent.children.includes(child), false);
    equal(child.transform.getWorldPosition().x, 1);
  });

  it("removeChild ignores entities that are not children", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");

    parent.removeChild(child);

    equal(child.parent, scene.root);
    equal(scene.root.children.includes(child), true);
  });

  it("destroy removes descendants from traversal", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);

    parent.destroy();

    const names: string[] = [];
    scene.traverse((entity) => names.push(entity.name));
    equal(names.includes("Parent"), false);
    equal(names.includes("Child"), false);
  });

  it("destroy detaches components once even if called repeatedly", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Entity");
    const component = entity.addComponent(new LifecycleComponent());

    entity.destroy();
    entity.destroy();

    equal(component.entity, undefined);
    equal(component.detached, 1);
  });

  it("destroy clears scene activeCamera references", () => {
    const scene = resetScene();
    const camera = scene.createEntity("Camera");
    scene.activeCamera = camera;

    camera.destroy();

    equal(scene.activeCamera, undefined);
  });

  it("destroy clears scene activeCamera references for descendants", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const camera = scene.createEntity("Camera");
    parent.addChild(camera);
    scene.activeCamera = camera;

    parent.destroy();

    equal(scene.activeCamera, undefined);
  });

  it("root cannot be parented under another entity", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Entity");

    throws(() => entity.addChild(scene.root), /root entity cannot be parented/);
  });

  it("rejects hierarchy cycles", () => {
    const scene = resetScene();
    const parent = scene.createEntity("Parent");
    const child = scene.createEntity("Child");
    parent.addChild(child);

    throws(() => child.addChild(parent), /cycles/);
  });

  it("destroying the root throws", () => {
    const scene = resetScene();

    throws(() => scene.root.destroy(), /root entity cannot be destroyed/);
  });
});
