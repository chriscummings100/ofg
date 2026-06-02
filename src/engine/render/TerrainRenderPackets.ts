import { identityMat4, multiplyMat4, type Mat4 } from "../math/mat4.js";
import type { ResourceStore } from "../scene/ResourceStore.js";
import type { ResourceId } from "../scene/types.js";
import {
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "../world/terrainChunk.js";
import { Mesh, type VertexLayout } from "./Mesh.js";
import type { RenderItem } from "./RenderWorld.js";

export type TerrainRenderChunkPacket = {
  readonly key: TerrainChunkKey;
  readonly mesh: Mesh;
  readonly material?: ResourceId;
  readonly worldMatrix?: Mat4;
};

export type TerrainRenderChunkMeshPacket = {
  readonly key: TerrainChunkKey;
  readonly meshId: ResourceId;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly layout: VertexLayout;
  readonly material?: ResourceId;
  readonly worldMatrix?: Mat4;
};

export type TerrainRenderChunkInput =
  | TerrainRenderChunkPacket
  | TerrainRenderChunkMeshPacket;

export type TerrainRenderChunkSink = {
  addChunk(chunk: TerrainRenderChunkInput): void;
  getChunk(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainRenderChunkPacket | undefined;
  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean;
  clear(): void;
  retainChunks(chunks: readonly (TerrainChunkKey | TerrainChunkCoord)[]): void;
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

  addChunk(chunk: TerrainRenderChunkInput): void {
    const packet = terrainRenderChunkInputToPacket(chunk);
    const index = this.chunkList.findIndex((existing) => existing.key === packet.key);
    if (index === -1) {
      this.chunkList.push(packet);
      return;
    }

    this.chunkList[index] = packet;
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

  retainChunks(chunks: readonly (TerrainChunkKey | TerrainChunkCoord)[]): void {
    const keep = new Set(chunks.map(toChunkKey));
    this.chunkList = this.chunkList.filter((chunk) => keep.has(chunk.key));
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

export function terrainRenderChunkInputToPacket(
  chunk: TerrainRenderChunkInput
): TerrainRenderChunkPacket {
  if ("mesh" in chunk) {
    return chunk;
  }

  return {
    key: chunk.key,
    mesh: new Mesh(chunk.meshId, chunk.vertices, chunk.indices, chunk.layout),
    material: chunk.material,
    worldMatrix: chunk.worldMatrix
  };
}

function toChunkKey(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainChunkKey {
  return typeof chunk === "string" ? chunk : terrainChunkKey(chunk);
}
