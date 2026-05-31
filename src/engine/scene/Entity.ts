import { Transform } from "./Transform.js";
import { Component } from "./Component.js";
import type { ComponentType, EntityId } from "./types.js";

export class Entity {
  readonly id: EntityId;
  name: string;
  enabled = true;
  parent?: Entity;
  readonly transform = new Transform();
  readonly children: Entity[] = [];
  readonly components: Component[] = [];

  private readonly rootEntity: boolean;
  private destroyed = false;

  constructor(id: EntityId, name: string, rootEntity = false) {
    this.id = id;
    this.name = name;
    this.rootEntity = rootEntity;
  }

  addChild(child: Entity): void {
    if (child.rootEntity) {
      throw new Error("The scene root entity cannot be parented under another entity.");
    }

    if (child === this || this.isDescendantOf(child)) {
      throw new Error("Entity hierarchy cannot contain cycles.");
    }

    child.parent?.removeChild(child);
    child.parent = this;
    child.transform.setParent(this.transform);
    this.children.push(child);
  }

  removeChild(child: Entity): void {
    const index = this.children.indexOf(child);
    if (index === -1) {
      return;
    }

    this.children.splice(index, 1);
    child.parent = undefined;
    child.transform.setParent(undefined);
  }

  destroy(): void {
    if (this.rootEntity) {
      throw new Error("The scene root entity cannot be destroyed.");
    }

    if (this.destroyed) {
      return;
    }

    this.destroyed = true;
    for (const child of [...this.children]) {
      child.destroy();
    }

    for (const component of [...this.components]) {
      this.removeComponent(component);
    }

    this.parent?.removeChild(this);
  }

  addComponent<T extends Component>(component: T): T {
    if (component.entity !== undefined) {
      throw new Error("Component is already attached to an entity.");
    }

    component.entity = this;
    this.components.push(component);
    component.onAttach();
    return component;
  }

  getComponent<T extends Component>(type: ComponentType<T>): T | undefined {
    return this.components.find((component): component is T => component instanceof type);
  }

  removeComponent(component: Component): void {
    const index = this.components.indexOf(component);
    if (index === -1) {
      return;
    }

    this.components.splice(index, 1);
    component.onDetach();
    component.entity = undefined;
  }

  updateWorldTransform(): void {
    this.transform.getWorldMatrix();
    for (const child of this.children) {
      child.updateWorldTransform();
    }
  }

  isDestroyed(): boolean {
    return this.destroyed;
  }

  private isDescendantOf(candidateAncestor: Entity): boolean {
    let current: Entity | undefined = this.parent;
    while (current !== undefined) {
      if (current === candidateAncestor) {
        return true;
      }

      current = current.parent;
    }

    return false;
  }
}
