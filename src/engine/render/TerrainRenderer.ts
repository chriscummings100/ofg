import { multiplyMat4, type Mat4 } from "../math/mat4.js";
import type { Vec3 } from "../math/vec3.js";
import { Component } from "../scene/Component.js";
import { getScene } from "../scene/activeScene.js";
import type { TerrainField } from "../world/scalarField.js";
import type { Material } from "./Material.js";
import type { Mesh } from "./Mesh.js";
import type { RenderItem } from "./RenderWorld.js";

export type ChunkKey = string;

export type TerrainChunk = {
  readonly key: ChunkKey;
  readonly mesh: Mesh;
  readonly material?: Material;
  readonly worldMatrix?: Mat4;
};

export class TerrainRenderer extends Component {
  field: TerrainField;
  chunks: TerrainChunk[];

  constructor(field: TerrainField, chunks: TerrainChunk[] = []) {
    super();
    this.field = field;
    this.chunks = chunks;
  }

  override onAttach(): void {
    getScene().terrain = this;
  }

  override onDetach(): void {
    const scene = getScene();
    if (scene.terrain === this) {
      scene.terrain = undefined;
    }
  }

  heightAt(x: number, z: number): number {
    return this.field.heightAt(x, z);
  }

  densityAt(position: Vec3): number {
    return this.field.densityAt(position);
  }

  rebuildChunk(_chunkKey: ChunkKey): void {
    // The chunk rebuild boundary is reserved for the Dual Contouring phase.
  }

  getRenderItems(): RenderItem[] {
    const entity = this.entity;
    if (!this.enabled || entity === undefined) {
      return [];
    }

    const entityWorldMatrix = entity.transform.getWorldMatrix();
    return this.chunks.map((chunk) => ({
      id: `terrain:${entity.id}:${chunk.key}`,
      mesh: chunk.mesh,
      material: chunk.material,
      worldMatrix: chunk.worldMatrix === undefined
        ? entityWorldMatrix
        : multiplyMat4(entityWorldMatrix, chunk.worldMatrix)
    }));
  }
}
