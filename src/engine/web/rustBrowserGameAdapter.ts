import { getFloatsPerVertex } from "../world/terrainMesh.js";
import type { TerrainChunkKey } from "../world/terrainChunk.js";
import {
  createEngineWebBrowserGame,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebBrowserGame,
  type EngineWebRendererStatus
} from "./engineWebWasm.js";
import type {
  TerrainRenderMeshPacket,
  TerrainRenderSource
} from "../render/TerrainCoreRenderPackets.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";

const WORLD_MATRIX_FLOATS = 16;
const IDENTITY_WORLD_MATRIX = new Float32Array([
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1
]);

export class RustBrowserGameAdapter {
  readonly runtime = "rust-wgpu" as const;
  private readonly uploadedTerrainMeshes = new Map<TerrainChunkKey, TerrainRenderMeshPacket>();
  private uploadedTerrainTextures?: TerrainMaterialTextures;
  private width = 1;
  private height = 1;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly game: EngineWebBrowserGame
  ) {}

  static async create(canvas: HTMLCanvasElement): Promise<RustBrowserGameAdapter> {
    const game = await createEngineWebBrowserGame(canvas);
    const adapter = new RustBrowserGameAdapter(canvas, game);
    adapter.resize();

    return adapter;
  }

  resize(): void {
    const { width, height } = this.computeDisplaySize();

    if (width === this.width && height === this.height) {
      return;
    }

    this.width = width;
    this.height = height;
    this.canvas.width = width;
    this.canvas.height = height;
    this.game.resize(width, height);
  }

  getAspectRatio(): number {
    return this.width / this.height;
  }

  getStatus(): EngineWebRendererStatus {
    return this.game.status();
  }

  renderEngineFrame(engineSnapshot: Float32Array, terrain: TerrainRenderSource): void {
    this.resize();
    const chunks = terrain.chunks;
    const chunkCount = chunks.length;
    const chunkKeys: string[] = [];
    const worldMatrices = new Float32Array(chunkCount * WORLD_MATRIX_FLOATS);
    const seenChunkKeys = new Set<TerrainChunkKey>();

    this.upsertTerrainTexturesIfNeeded(terrain.terrainTextures);

    for (let index = 0; index < chunkCount; index += 1) {
      const chunk = chunks[index];

      this.upsertTerrainMeshIfNeeded(chunk.key, chunk.mesh);
      seenChunkKeys.add(chunk.key);
      chunkKeys.push(chunk.key);
      worldMatrices.set(chunk.worldMatrix ?? IDENTITY_WORLD_MATRIX, index * WORLD_MATRIX_FLOATS);
    }

    this.game.renderEngineFrame(
      engineSnapshot,
      this.getAspectRatio(),
      chunkKeys,
      worldMatrices
    );
    this.pruneUploadedTerrainMeshes(seenChunkKeys);
  }

  private upsertTerrainMeshIfNeeded(
    chunkKey: TerrainChunkKey,
    mesh: TerrainRenderMeshPacket
  ): void {
    if (mesh.floatsPerVertex !== getFloatsPerVertex()) {
      throw new Error(
        `RustBrowserGame renderer only supports ${getFloatsPerVertex()} floats per vertex; ` +
        `terrain chunk '${chunkKey}' uses ${mesh.floatsPerVertex}.`
      );
    }

    if (this.uploadedTerrainMeshes.get(chunkKey) === mesh) {
      return;
    }

    this.game.upsertTerrainMesh(chunkKey, mesh.vertices, mesh.indices, mesh.floatsPerVertex);
    this.uploadedTerrainMeshes.set(chunkKey, mesh);
  }

  private upsertTerrainTexturesIfNeeded(textures: TerrainMaterialTextures | undefined): void {
    if (textures === undefined || this.uploadedTerrainTextures === textures) {
      return;
    }

    validateTerrainTextureArrays(textures);
    this.game.upsertTerrainTextures(
      textures.albedo.width,
      textures.albedo.height,
      textures.albedo.layers,
      ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
      textures.albedo.data,
      textures.normal.data,
      textures.material.data
    );
    this.uploadedTerrainTextures = textures;
  }

  private pruneUploadedTerrainMeshes(seenChunkKeys: Set<TerrainChunkKey>): void {
    for (const key of this.uploadedTerrainMeshes.keys()) {
      if (seenChunkKeys.has(key)) {
        continue;
      }

      this.game.destroyTerrainMesh(key);
      this.uploadedTerrainMeshes.delete(key);
    }
  }

  private computeDisplaySize(): { readonly width: number; readonly height: number } {
    const pixelRatio = Math.min(window.devicePixelRatio || 1, 2);

    return {
      width: Math.max(1, Math.floor(this.canvas.clientWidth * pixelRatio)),
      height: Math.max(1, Math.floor(this.canvas.clientHeight * pixelRatio))
    };
  }
}

function validateTerrainTextureArrays(textures: TerrainMaterialTextures): void {
  const { width, height, layers } = textures.albedo;
  for (const [label, texture] of [
    ["normal", textures.normal],
    ["material", textures.material]
  ] as const) {
    if (texture.width !== width || texture.height !== height || texture.layers !== layers) {
      throw new Error(
        `RustBrowserGame renderer received terrain ${label} texture dimensions ` +
        `${texture.width}x${texture.height}x${texture.layers}; expected ` +
        `${width}x${height}x${layers}.`
      );
    }
  }
}
