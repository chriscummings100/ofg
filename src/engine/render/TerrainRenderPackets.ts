import { identityMat4, multiplyMat4, type Mat4 } from "../math/mat4.js";
import type { ResourceStore } from "../scene/ResourceStore.js";
import type { ResourceId } from "../scene/types.js";
import {
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "../world/terrainChunk.js";
import type { Mesh } from "./Mesh.js";
import type { RenderItem } from "./RenderWorld.js";

export type TerrainRenderChunkPacket = {
  readonly key: TerrainChunkKey;
  readonly mesh: Mesh;
  readonly material?: ResourceId;
  readonly worldMatrix?: Mat4;
};

export type TerrainRenderChunkSink = {
  addChunk(chunk: TerrainRenderChunkPacket): void;
  getChunk(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainRenderChunkPacket | undefined;
  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean;
};

export class TerrainRenderPacketStore implements TerrainRenderChunkSink {
  readonly itemIdPrefix: string;
  private chunkList: TerrainRenderChunkPacket[];

  constructor(
    options: {
      readonly itemIdPrefix?: string;
      readonly chunks?: readonly TerrainRenderChunkPacket[];
    } = {}
  ) {
    this.itemIdPrefix = options.itemIdPrefix ?? "terrain:packet";
    this.chunkList = [...(options.chunks ?? [])];
  }

  get chunks(): readonly TerrainRenderChunkPacket[] {
    return this.chunkList;
  }

  addChunk(chunk: TerrainRenderChunkPacket): void {
    const index = this.chunkList.findIndex((existing) => existing.key === chunk.key);
    if (index === -1) {
      this.chunkList.push(chunk);
      return;
    }

    this.chunkList[index] = chunk;
  }

  getChunk(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainRenderChunkPacket | undefined {
    const key = toChunkKey(chunk);
    return this.chunkList.find((candidate) => candidate.key === key);
  }

  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean {
    const key = toChunkKey(chunk);
    const index = this.chunkList.findIndex((candidate) => candidate.key === key);
    if (index === -1) {
      return false;
    }

    this.chunkList.splice(index, 1);
    return true;
  }

  clear(): void {
    this.chunkList = [];
  }

  setChunks(chunks: readonly TerrainRenderChunkPacket[]): void {
    this.chunkList = [...chunks];
  }

  getRenderItems(
    resources: ResourceStore,
    worldMatrix: Mat4 = identityMat4()
  ): RenderItem[] {
    return this.chunkList.map((chunk) => {
      const material = chunk.material === undefined ? undefined : resources.getMaterial(chunk.material);
      return {
        id: `${this.itemIdPrefix}:${chunk.key}`,
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
          ? worldMatrix
          : multiplyMat4(worldMatrix, chunk.worldMatrix)
      };
    });
  }
}

function toChunkKey(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainChunkKey {
  return typeof chunk === "string" ? chunk : terrainChunkKey(chunk);
}
