import { getFloatsPerVertex } from "../world/terrainMesh.js";
import {
  createEngineWebBrowserGame,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebBrowserGame,
  type EngineWebRendererStatus
} from "./engineWebWasm.js";
import type { RenderMeshPacket } from "../render/RenderPackets.js";
import type { TerrainRenderSource } from "../render/TerrainCoreRenderPackets.js";
import type { Texture } from "../render/Texture.js";

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
  private readonly uploadedTextures = new Map<string, Texture>();
  private uploadedTerrainMaterial?: UploadedTerrainMaterial;
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

    this.upsertTextureIfNeeded(terrain.albedoTexture);
    this.upsertTextureIfNeeded(terrain.normalTexture);
    this.upsertTextureIfNeeded(terrain.materialTexture);
    this.upsertTerrainMaterialIfNeeded(
      terrain.albedoTexture,
      terrain.normalTexture,
      terrain.materialTexture
    );

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

  private upsertTextureIfNeeded(texture: Texture | undefined): void {
    if (texture === undefined || this.uploadedTextures.get(texture.id) === texture) {
      return;
    }

    if (texture.format !== "rgba8unorm") {
      throw new Error(`RustBrowserGame renderer does not support texture format '${texture.format}'.`);
    }

    const data = texture.data === undefined
      ? createOpaqueWhiteTextureData(texture.width, texture.height, texture.layers)
      : new Uint8Array(texture.data.buffer, texture.data.byteOffset, texture.data.byteLength);
    this.game.upsertTexture(
      texture.id,
      texture.width,
      texture.height,
      texture.layers,
      ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
      data
    );
    this.uploadedTextures.set(texture.id, texture);
  }

  private upsertTerrainMaterialIfNeeded(
    albedoTexture: Texture | undefined,
    normalTexture: Texture | undefined,
    materialTexture: Texture | undefined
  ): void {
    const uploaded = {
      albedoTextureId: albedoTexture?.id ?? "",
      normalTextureId: normalTexture?.id ?? "",
      materialTextureId: materialTexture?.id ?? ""
    };
    const cached = this.uploadedTerrainMaterial;
    if (
      cached?.albedoTextureId === uploaded.albedoTextureId &&
      cached.normalTextureId === uploaded.normalTextureId &&
      cached.materialTextureId === uploaded.materialTextureId
    ) {
      return;
    }

    this.game.upsertTerrainMaterial(
      uploaded.albedoTextureId,
      uploaded.normalTextureId,
      uploaded.materialTextureId
    );
    this.uploadedTerrainMaterial = uploaded;
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

function createOpaqueWhiteTextureData(width: number, height: number, layers: number): Uint8Array {
  const data = new Uint8Array(width * height * layers * 4);
  for (let offset = 0; offset < data.length; offset += 4) {
    data[offset] = 255;
    data[offset + 1] = 255;
    data[offset + 2] = 255;
    data[offset + 3] = 255;
  }

  return data;
}

type UploadedTerrainMaterial = {
  readonly albedoTextureId: string;
  readonly normalTextureId: string;
  readonly materialTextureId: string;
};
