import { multiplyMat4, type Mat4 } from "../math/mat4.js";
import type { Vec3 } from "../math/vec3.js";
import { Component } from "../scene/Component.js";
import { getScene } from "../scene/activeScene.js";
import type { TerrainField } from "../world/scalarField.js";
import {
  sampleTerrainDensity,
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey,
  type TerrainDensitySample
} from "../world/terrainChunk.js";
import type { ResourceId } from "../scene/types.js";
import type { Mesh } from "./Mesh.js";
import type { RenderItem } from "./RenderWorld.js";
import {
  terrainRenderChunkInputToPacket,
  type TerrainRenderChunkInput
} from "./TerrainRenderPackets.js";

export type ChunkKey = TerrainChunkKey;

export type TerrainChunk = {
  readonly key: ChunkKey;
  readonly mesh: Mesh;
  readonly material?: ResourceId;
  readonly worldMatrix?: Mat4;
};

export class TerrainRenderer extends Component {
  field: TerrainField;
  chunks: TerrainChunk[];

  constructor(field: TerrainField, chunks: TerrainChunk[] = []) {
    super();
    this.field = field;
    this.chunks = [...chunks];
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

  sampleAt(position: Vec3): TerrainDensitySample {
    return sampleTerrainDensity(this.field, position);
  }

  addChunk(chunk: TerrainRenderChunkInput): void {
    const packet = terrainRenderChunkInputToPacket(chunk);
    const index = this.chunks.findIndex((existing) => existing.key === packet.key);
    if (index === -1) {
      this.chunks.push(packet);
      return;
    }

    this.chunks[index] = packet;
  }

  getChunk(chunk: ChunkKey | TerrainChunkCoord): TerrainChunk | undefined {
    const key = toChunkKey(chunk);
    return this.chunks.find((candidate) => candidate.key === key);
  }

  removeChunk(chunk: ChunkKey | TerrainChunkCoord): boolean {
    const key = toChunkKey(chunk);
    const index = this.chunks.findIndex((candidate) => candidate.key === key);
    if (index === -1) {
      return false;
    }

    this.chunks.splice(index, 1);
    return true;
  }

  setChunks(chunks: TerrainChunk[]): void {
    this.chunks = [...chunks];
  }

  rebuildChunk(chunk: ChunkKey | TerrainChunkCoord): TerrainChunk | undefined {
    // TerrainChunkStreamer owns generated mesh rebuilds for now.
    return this.getChunk(chunk);
  }

  getRenderItems(): RenderItem[] {
    const entity = this.entity;
    if (!this.enabled || entity === undefined) {
      return [];
    }

    const entityWorldMatrix = entity.transform.getWorldMatrix();
    const resources = getScene().resources;
    return this.chunks.map((chunk) => {
      const material = chunk.material === undefined ? undefined : resources.getMaterial(chunk.material);
      return {
        id: `terrain:${entity.id}:${chunk.key}`,
        mesh: chunk.mesh,
        material,
        albedoTexture: material?.albedoTexture === undefined
          ? undefined
          : resources.getTexture(material.albedoTexture),
        normalTexture: material?.normalTexture === undefined
          ? undefined
          : resources.getTexture(material.normalTexture),
        materialTexture: material?.materialTexture === undefined
          ? undefined
          : resources.getTexture(material.materialTexture),
        worldMatrix: chunk.worldMatrix === undefined
          ? entityWorldMatrix
          : multiplyMat4(entityWorldMatrix, chunk.worldMatrix)
      };
    });
  }
}

function toChunkKey(chunk: ChunkKey | TerrainChunkCoord): ChunkKey {
  return typeof chunk === "string" ? chunk : terrainChunkKey(chunk);
}
