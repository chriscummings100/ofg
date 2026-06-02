import { getFloatsPerVertex } from "../world/terrainMesh.js";
import {
  createEngineWebBrowserGame,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebBrowserGame,
  type EngineWebRendererStatus
} from "../web/engineWebWasm.js";
import {
  DEFAULT_ALBEDO_FACTOR,
  DEFAULT_SPECULAR,
  DEFAULT_SPECULAR_FACTOR,
  DEFAULT_TEXTURE_SCALE,
  type Material
} from "./Material.js";
import type { RenderItemPacket, RenderMeshPacket } from "./RenderPackets.js";
import type { Texture } from "./Texture.js";

const WORLD_MATRIX_FLOATS = 16;
const MATERIAL_PACKET_FLOATS = 10;
const IDENTITY_WORLD_MATRIX = new Float32Array([
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1
]);

export class RustWgpuRendererAdapter {
  readonly runtime = "rust-wgpu" as const;
  private readonly uploadedMeshes = new Map<string, RenderMeshPacket>();
  private readonly uploadedTextures = new Map<string, Texture>();
  private width = 1;
  private height = 1;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly game: EngineWebBrowserGame
  ) {}

  static async create(canvas: HTMLCanvasElement): Promise<RustWgpuRendererAdapter> {
    const game = await createEngineWebBrowserGame(canvas);
    const adapter = new RustWgpuRendererAdapter(canvas, game);
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

  renderEngineFrame(engineSnapshot: Float32Array, items: readonly RenderItemPacket[]): void {
    this.resize();
    const itemCount = items.length;
    const itemIds: string[] = [];
    const meshIds: string[] = [];
    const albedoTextureIds: string[] = [];
    const normalTextureIds: string[] = [];
    const materialTextureIds: string[] = [];
    const worldMatrices = new Float32Array(itemCount * WORLD_MATRIX_FLOATS);
    const materialPackets = new Float32Array(itemCount * MATERIAL_PACKET_FLOATS);
    const seenMeshes = new Set<RenderMeshPacket>();

    for (let index = 0; index < itemCount; index += 1) {
      const item = items[index];

      this.upsertMeshIfNeeded(item.mesh);
      this.upsertTextureIfNeeded(item.albedoTexture);
      this.upsertTextureIfNeeded(item.normalTexture);
      this.upsertTextureIfNeeded(item.materialTexture);

      seenMeshes.add(item.mesh);
      itemIds.push(item.id);
      meshIds.push(item.mesh.id);
      albedoTextureIds.push(item.albedoTexture?.id ?? "");
      normalTextureIds.push(item.normalTexture?.id ?? "");
      materialTextureIds.push(item.materialTexture?.id ?? "");
      worldMatrices.set(item.worldMatrix ?? IDENTITY_WORLD_MATRIX, index * WORLD_MATRIX_FLOATS);
      writeMaterialPacket(item.material, materialPackets, index * MATERIAL_PACKET_FLOATS);
    }

    this.game.renderEngineFrame(
      engineSnapshot,
      this.getAspectRatio(),
      itemIds,
      meshIds,
      albedoTextureIds,
      normalTextureIds,
      materialTextureIds,
      worldMatrices,
      materialPackets
    );
    this.pruneUploadedMeshes(seenMeshes);
  }

  private upsertMeshIfNeeded(mesh: RenderMeshPacket): void {
    if (mesh.floatsPerVertex !== getFloatsPerVertex()) {
      throw new Error(
        `RustWgpuRenderer only supports ${getFloatsPerVertex()} floats per vertex; ` +
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
      throw new Error(`RustWgpuRenderer does not support texture format '${texture.format}'.`);
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

function writeMaterialPacket(
  material: Material | undefined,
  target: Float32Array,
  offset: number
): void {
  const albedo = material?.albedoFactor ?? DEFAULT_ALBEDO_FACTOR;
  const specular = material?.specular ?? DEFAULT_SPECULAR;

  target[offset] = albedo.x;
  target[offset + 1] = albedo.y;
  target[offset + 2] = albedo.z;
  target[offset + 3] = albedo.w;
  target[offset + 4] = specular.x;
  target[offset + 5] = specular.y;
  target[offset + 6] = specular.z;
  target[offset + 7] = material?.specularFactor ?? DEFAULT_SPECULAR_FACTOR;
  target[offset + 8] = material?.flags ?? 0;
  target[offset + 9] = material?.textureScale ?? DEFAULT_TEXTURE_SCALE;
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
