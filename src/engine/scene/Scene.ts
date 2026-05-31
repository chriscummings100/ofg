import { Component } from "./Component.js";
import { Entity } from "./Entity.js";
import { ResourceStore } from "./ResourceStore.js";
import type { ComponentType } from "./types.js";
import { createDirectionalLight, type DirectionalLight } from "../render/Lighting.js";
import type { TerrainRenderer } from "../render/TerrainRenderer.js";

export class Scene {
  readonly root: Entity;
  readonly resources = new ResourceStore();
  mainLight: DirectionalLight = createDirectionalLight();
  terrain?: TerrainRenderer;
  activeCamera?: Entity;

  private nextEntityId = 1;

  constructor() {
    this.root = new Entity(0, "Root", true, this);
  }

  createEntity(name = "Entity"): Entity {
    const entity = new Entity(this.nextEntityId, name, false, this);
    this.nextEntityId += 1;
    this.root.addChild(entity);
    return entity;
  }

  destroyEntity(entity: Entity): void {
    entity.destroy();
  }

  notifyEntityDestroying(entity: Entity): void {
    const activeCamera = this.activeCamera;
    if (activeCamera !== undefined && containsEntity(entity, activeCamera)) {
      this.activeCamera = undefined;
    }
  }

  update(deltaSeconds: number): void {
    this.traverse((entity) => {
      for (const component of entity.components) {
        if (component.enabled) {
          component.update(deltaSeconds);
        }
      }
    });
  }

  traverse(callback: (entity: Entity) => void): void {
    visitEnabled(this.root, callback);
  }

  findByName(name: string): Entity | undefined {
    let match: Entity | undefined;
    this.traverse((entity) => {
      if (match === undefined && entity.name === name) {
        match = entity;
      }
    });

    return match;
  }

  queryComponents<T extends Component>(type: ComponentType<T>): T[] {
    const components: T[] = [];
    this.traverse((entity) => {
      for (const component of entity.components) {
        if (component instanceof type) {
          components.push(component);
        }
      }
    });

    return components;
  }

  getTerrainHeight(x: number, z: number): number | undefined {
    return this.terrain?.heightAt(x, z);
  }
}

function containsEntity(root: Entity, target: Entity): boolean {
  if (root === target) {
    return true;
  }

  return root.children.some((child) => containsEntity(child, target));
}

function visitEnabled(entity: Entity, callback: (entity: Entity) => void): void {
  if (!entity.enabled || entity.isDestroyed()) {
    return;
  }

  callback(entity);
  for (const child of entity.children) {
    visitEnabled(child, callback);
  }
}
