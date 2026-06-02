import { getFloatsPerVertex } from "../world/terrainMesh.js";
import {
  createEngineWebRenderer,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebRendererStatus,
  type EngineWebWgpuRenderer
} from "../web/engineWebWasm.js";
import {
  DEFAULT_ALBEDO_FACTOR,
  DEFAULT_SPECULAR,
  DEFAULT_SPECULAR_FACTOR,
  DEFAULT_TEXTURE_SCALE,
  type Material
} from "./Material.js";
import type {
  PlayerMarkerRenderPacket,
  RenderItemPacket,
  RenderMeshPacket
} from "./RenderPackets.js";
import type { Texture } from "./Texture.js";

const WORLD_MATRIX_FLOATS = 16;
const MATERIAL_PACKET_FLOATS = 10;
const IDENTITY_WORLD_MATRIX = new Float32Array([
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1
]);

type GpuMeshHandle = {
  readonly handle: number;
};

type GpuObjectHandle = {
  readonly handle: number;
};

export class RustWgpuRendererAdapter {
  readonly runtime = "rust-wgpu" as const;
  private readonly meshCache = new Map<RenderMeshPacket, GpuMeshHandle>();
  private readonly textureCache = new WeakMap<Texture, number>();
  private readonly objectHandles = new Map<string, GpuObjectHandle>();
  private readonly playerMarkerMaterialPacket = new Float32Array(MATERIAL_PACKET_FLOATS);
  private readonly fallbackAlbedoTexture: number;
  private readonly fallbackNormalTexture: number;
  private readonly fallbackMaterialTexture: number;
  private width = 1;
  private height = 1;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly renderer: EngineWebWgpuRenderer
  ) {
    this.fallbackAlbedoTexture = renderer.fallbackAlbedoTextureHandle();
    this.fallbackNormalTexture = renderer.fallbackNormalTextureHandle();
    this.fallbackMaterialTexture = renderer.fallbackMaterialTextureHandle();
  }

  static async create(canvas: HTMLCanvasElement): Promise<RustWgpuRendererAdapter> {
    const renderer = await createEngineWebRenderer(canvas);
    const adapter = new RustWgpuRendererAdapter(canvas, renderer);
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
    this.renderer.resize(width, height);
  }

  getAspectRatio(): number {
    return this.width / this.height;
  }

  getStatus(): EngineWebRendererStatus {
    return this.renderer.status();
  }

  renderEngineFrame(
    engineSnapshot: Float32Array,
    items: readonly RenderItemPacket[],
    playerMarker: PlayerMarkerRenderPacket
  ): void {
    this.resize();
    const itemCount = items.length;
    const meshHandles = new Float64Array(itemCount);
    const objectHandles = new Float64Array(itemCount);
    const albedoTextureHandles = new Float64Array(itemCount);
    const normalTextureHandles = new Float64Array(itemCount);
    const materialTextureHandles = new Float64Array(itemCount);
    const worldMatrices = new Float32Array(itemCount * WORLD_MATRIX_FLOATS);
    const materialPackets = new Float32Array(itemCount * MATERIAL_PACKET_FLOATS);
    const seenItemIds = new Set<string>();
    const seenMeshes = new Set<RenderMeshPacket>();

    for (let index = 0; index < itemCount; index += 1) {
      const item = items[index];
      const object = this.getGpuObject(item.id);

      seenItemIds.add(item.id);
      seenMeshes.add(item.mesh);
      meshHandles[index] = this.getGpuMesh(item.mesh).handle;
      objectHandles[index] = object.handle;
      albedoTextureHandles[index] = this.getTextureHandle(
        item.albedoTexture,
        this.fallbackAlbedoTexture
      );
      normalTextureHandles[index] = this.getTextureHandle(
        item.normalTexture,
        this.fallbackNormalTexture
      );
      materialTextureHandles[index] = this.getTextureHandle(
        item.materialTexture,
        this.fallbackMaterialTexture
      );
      worldMatrices.set(item.worldMatrix ?? IDENTITY_WORLD_MATRIX, index * WORLD_MATRIX_FLOATS);
      writeMaterialPacket(item.material, materialPackets, index * MATERIAL_PACKET_FLOATS);
    }

    const playerMarkerObject = this.getGpuObject(playerMarker.id);
    seenItemIds.add(playerMarker.id);
    seenMeshes.add(playerMarker.mesh);
    writeMaterialPacket(playerMarker.material, this.playerMarkerMaterialPacket, 0);

    this.renderer.renderEngineFrame(
      engineSnapshot,
      this.getAspectRatio(),
      meshHandles,
      objectHandles,
      albedoTextureHandles,
      normalTextureHandles,
      materialTextureHandles,
      worldMatrices,
      materialPackets,
      this.getGpuMesh(playerMarker.mesh).handle,
      playerMarkerObject.handle,
      this.getTextureHandle(playerMarker.albedoTexture, this.fallbackAlbedoTexture),
      this.getTextureHandle(playerMarker.normalTexture, this.fallbackNormalTexture),
      this.getTextureHandle(playerMarker.materialTexture, this.fallbackMaterialTexture),
      this.playerMarkerMaterialPacket
    );
    this.pruneObjectHandles(seenItemIds);
    this.pruneGpuMeshes(seenMeshes);
  }

  private getGpuMesh(mesh: RenderMeshPacket): GpuMeshHandle {
    const cached = this.meshCache.get(mesh);
    if (cached !== undefined) {
      return cached;
    }

    if (mesh.floatsPerVertex !== getFloatsPerVertex()) {
      throw new Error(
        `RustWgpuRenderer only supports ${getFloatsPerVertex()} floats per vertex; ` +
        `mesh '${mesh.id}' uses ${mesh.floatsPerVertex}.`
      );
    }

    const gpuMesh = {
      handle: this.renderer.registerMesh(mesh.vertices, mesh.indices, mesh.floatsPerVertex)
    };
    this.meshCache.set(mesh, gpuMesh);

    return gpuMesh;
  }

  private getTextureHandle(texture: Texture | undefined, fallbackHandle: number): number {
    if (texture === undefined) {
      return fallbackHandle;
    }

    const cached = this.textureCache.get(texture);
    if (cached !== undefined) {
      return cached;
    }

    if (texture.format !== "rgba8unorm") {
      throw new Error(`RustWgpuRenderer does not support texture format '${texture.format}'.`);
    }

    const data = texture.data === undefined
      ? createOpaqueWhiteTextureData(texture.width, texture.height, texture.layers)
      : new Uint8Array(texture.data.buffer, texture.data.byteOffset, texture.data.byteLength);
    const handle = this.renderer.registerTexture(
      texture.width,
      texture.height,
      texture.layers,
      ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
      data
    );
    this.textureCache.set(texture, handle);

    return handle;
  }

  private getGpuObject(id: string): GpuObjectHandle {
    const cached = this.objectHandles.get(id);
    if (cached !== undefined) {
      return cached;
    }

    const object = {
      handle: this.renderer.registerObject()
    };
    this.objectHandles.set(id, object);

    return object;
  }

  private pruneObjectHandles(seenItemIds: Set<string>): void {
    for (const [id, object] of this.objectHandles) {
      if (seenItemIds.has(id)) {
        continue;
      }

      this.renderer.destroyObject(object.handle);
      this.objectHandles.delete(id);
    }
  }

  private pruneGpuMeshes(seenMeshes: Set<RenderMeshPacket>): void {
    for (const [mesh, gpuMesh] of this.meshCache) {
      if (seenMeshes.has(mesh)) {
        continue;
      }

      this.renderer.destroyMesh(gpuMesh.handle);
      this.meshCache.delete(mesh);
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
