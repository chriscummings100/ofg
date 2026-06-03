import { getFloatsPerVertex } from "../world/terrainMesh.js";
import {
  createEngineWebBrowserGame,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebBrowserGame,
  type EngineWebRendererStatus
} from "./engineWebWasm.js";
import type { RenderMeshPacket } from "../render/RenderPackets.js";
import type { TerrainRenderSource } from "../render/TerrainCoreRenderPackets.js";
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
  private readonly uploadedMeshes = new Map<string, RenderMeshPacket>();
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
    const itemCount = chunks.length;
    const itemIds: string[] = [];
    const meshIds: string[] = [];
    const worldMatrices = new Float32Array(itemCount * WORLD_MATRIX_FLOATS);
    const seenMeshes = new Set<RenderMeshPacket>();

    this.upsertTerrainTexturesIfNeeded(terrain.terrainTextures);

    for (let index = 0; index < itemCount; index += 1) {
      const chunk = chunks[index];

      this.upsertMeshIfNeeded(chunk.mesh);
      seenMeshes.add(chunk.mesh);
      itemIds.push(`${terrain.itemIdPrefix}:${chunk.key}`);
      meshIds.push(chunk.mesh.id);
      worldMatrices.set(chunk.worldMatrix ?? IDENTITY_WORLD_MATRIX, index * WORLD_MATRIX_FLOATS);
    }

    this.game.renderEngineFrame(
      engineSnapshot,
      this.getAspectRatio(),
      itemIds,
      meshIds,
      worldMatrices
    );
    this.pruneUploadedMeshes(seenMeshes);
  }

  private upsertMeshIfNeeded(mesh: RenderMeshPacket): void {
    if (mesh.floatsPerVertex !== getFloatsPerVertex()) {
      throw new Error(
        `RustBrowserGame renderer only supports ${getFloatsPerVertex()} floats per vertex; ` +
        `mesh '${mesh.id}' uses ${mesh.floatsPerVertex}.`
      );
    }

    if (this.uploadedMeshes.get(mesh.id) === mesh) {
      return;
    }

    this.game.upsertMesh(mesh.id, mesh.vertices, mesh.indices, mesh.floatsPerVertex);
    this.uploadedMeshes.set(mesh.id, mesh);
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

  private pruneUploadedMeshes(seenMeshes: Set<RenderMeshPacket>): void {
    for (const [id, mesh] of this.uploadedMeshes) {
      if (seenMeshes.has(mesh)) {
        continue;
      }

      this.game.destroyMesh(id);
      this.uploadedMeshes.delete(id);
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
