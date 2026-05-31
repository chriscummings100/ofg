import { equal, throws } from "node:assert/strict";
import { Component } from "./Component.js";
import { resetScene } from "./activeScene.js";

class TestComponent extends Component {
  attached = 0;
  detached = 0;
  updates = 0;

  override onAttach(): void {
    this.attached += 1;
  }

  override onDetach(): void {
    this.detached += 1;
  }

  override update(): void {
    this.updates += 1;
  }
}

describe("Component", () => {
  it("addComponent attaches the component to the entity", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Entity");
    const component = entity.addComponent(new TestComponent());

    equal(component.entity, entity);
    equal(component.attached, 1);
  });

  it("removeComponent detaches the component", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Entity");
    const component = entity.addComponent(new TestComponent());

    entity.removeComponent(component);

    equal(component.entity, undefined);
    equal(component.detached, 1);
  });

  it("scene update calls enabled components", () => {
    const scene = resetScene();
    const component = scene.createEntity("Entity").addComponent(new TestComponent());

    scene.update(1);

    equal(component.updates, 1);
  });

  it("scene update skips disabled components", () => {
    const scene = resetScene();
    const component = scene.createEntity("Entity").addComponent(new TestComponent());
    component.enabled = false;

    scene.update(1);

    equal(component.updates, 0);
  });

  it("scene update resumes components when re-enabled", () => {
    const scene = resetScene();
    const component = scene.createEntity("Entity").addComponent(new TestComponent());
    component.enabled = false;
    scene.update(1);
    component.enabled = true;

    scene.update(1);

    equal(component.updates, 1);
  });

  it("a component cannot be attached to two entities", () => {
    const scene = resetScene();
    const component = scene.createEntity("First").addComponent(new TestComponent());
    const second = scene.createEntity("Second");

    throws(() => second.addComponent(component), /already attached/);
  });

  it("removeComponent ignores components that are not attached", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Entity");
    const component = new TestComponent();

    entity.removeComponent(component);

    equal(component.detached, 0);
    equal(component.entity, undefined);
  });

  it("destroying an entity detaches its components", () => {
    const scene = resetScene();
    const entity = scene.createEntity("Entity");
    const component = entity.addComponent(new TestComponent());

    entity.destroy();

    equal(component.entity, undefined);
    equal(component.detached, 1);
  });
});
