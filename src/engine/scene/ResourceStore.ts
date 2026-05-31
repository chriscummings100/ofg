import type { Mesh } from "../render/Mesh.js";
import type { Texture } from "../render/Texture.js";
import type { Material } from "../render/Material.js";
import type { ResourceId } from "./types.js";

export class ResourceStore {
  private readonly meshes = new Map<ResourceId, Mesh>();
  private readonly textures = new Map<ResourceId, Texture>();
  private readonly materials = new Map<ResourceId, Material>();

  addMesh(mesh: Mesh): ResourceId {
    this.meshes.set(mesh.id, mesh);
    return mesh.id;
  }

  getMesh(id: ResourceId): Mesh {
    const mesh = this.meshes.get(id);
    if (mesh === undefined) {
      throw new Error(`Mesh resource '${id}' does not exist.`);
    }

    return mesh;
  }

  removeMesh(id: ResourceId): void {
    this.meshes.delete(id);
  }

  addTexture(texture: Texture): ResourceId {
    this.textures.set(texture.id, texture);
    return texture.id;
  }

  getTexture(id: ResourceId): Texture {
    const texture = this.textures.get(id);
    if (texture === undefined) {
      throw new Error(`Texture resource '${id}' does not exist.`);
    }

    return texture;
  }

  removeTexture(id: ResourceId): void {
    this.textures.delete(id);
  }

  addMaterial(material: Material): ResourceId {
    this.materials.set(material.id, material);
    return material.id;
  }

  getMaterial(id: ResourceId): Material {
    const material = this.materials.get(id);
    if (material === undefined) {
      throw new Error(`Material resource '${id}' does not exist.`);
    }

    return material;
  }

  removeMaterial(id: ResourceId): void {
    this.materials.delete(id);
  }
}
