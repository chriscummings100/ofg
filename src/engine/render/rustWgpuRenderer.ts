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
import type { Mesh } from "./Mesh.js";
import type { RenderItem, RenderWorld } from "./RenderWorld.js";
import type { Texture } from "./Texture.js";

const FRAME_PACKET_FLOATS = 43;
const WORLD_MATRIX_FLOATS = 16;
const MATERIAL_PACKET_FLOATS = 10;

type GpuMeshHandle = {
  readonly handle: number;
};

type GpuObjectHandle = {
  readonly handle: number;
};

export class RustWgpuRendererAdapter {
  readonly runtime = "rust-wgpu" as const;
  private readonly meshCache = new Map<Mesh, GpuMeshHandle>();
  private readonly textureCache = new WeakMap<Texture, number>();
  private readonly objectHandles = new Map<string, GpuObjectHandle>();
  private readonly framePacket = new Float32Array(FRAME_PACKET_FLOATS);
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

  render(renderWorld: RenderWorld): void {
    this.resize();
    const itemCount = renderWorld.items.length;
    const meshHandles = new Float64Array(itemCount);
    const objectHandles = new Float64Array(itemCount);
    const albedoTextureHandles = new Float64Array(itemCount);
    const normalTextureHandles = new Float64Array(itemCount);
    const materialTextureHandles = new Float64Array(itemCount);
    const worldMatrices = new Float32Array(itemCount * WORLD_MATRIX_FLOATS);
    const materialPackets = new Float32Array(itemCount * MATERIAL_PACKET_FLOATS);
    const seenItemIds = new Set<string>();
    const seenMeshes = new Set<Mesh>();

    writeFramePacket(renderWorld, this.framePacket);
    for (let index = 0; index < itemCount; index += 1) {
      const item = renderWorld.items[index];
      const object = this.getGpuObject(item);

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
      worldMatrices.set(item.worldMatrix, index * WORLD_MATRIX_FLOATS);
      writeMaterialPacket(item.material, materialPackets, index * MATERIAL_PACKET_FLOATS);
    }

    this.renderer.render(
      this.framePacket,
      meshHandles,
      objectHandles,
      albedoTextureHandles,
      normalTextureHandles,
      materialTextureHandles,
      worldMatrices,
      materialPackets
    );
    this.pruneObjectHandles(seenItemIds);
    this.pruneGpuMeshes(seenMeshes);
  }

  private getGpuMesh(mesh: Mesh): GpuMeshHandle {
    const cached = this.meshCache.get(mesh);
    if (cached !== undefined) {
      return cached;
    }

    if (mesh.layout.floatsPerVertex !== getFloatsPerVertex()) {
      throw new Error(
        `RustWgpuRenderer only supports ${getFloatsPerVertex()} floats per vertex; ` +
        `mesh '${mesh.id}' uses ${mesh.layout.floatsPerVertex}.`
      );
    }

    const gpuMesh = {
      handle: this.renderer.registerMesh(mesh.vertices, mesh.indices, mesh.layout.floatsPerVertex)
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

  private getGpuObject(item: RenderItem): GpuObjectHandle {
    const cached = this.objectHandles.get(item.id);
    if (cached !== undefined) {
      return cached;
    }

    const object = {
      handle: this.renderer.registerObject()
    };
    this.objectHandles.set(item.id, object);

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

  private pruneGpuMeshes(seenMeshes: Set<Mesh>): void {
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

function writeFramePacket(renderWorld: RenderWorld, target: Float32Array): void {
  target.set(renderWorld.camera.viewProjection, 0);
  target.set(renderWorld.camera.inverseViewProjection, 16);
  target[32] = renderWorld.camera.eye.x;
  target[33] = renderWorld.camera.eye.y;
  target[34] = renderWorld.camera.eye.z;
  target[35] = renderWorld.mainLight.direction.x;
  target[36] = renderWorld.mainLight.direction.y;
  target[37] = renderWorld.mainLight.direction.z;
  target[38] = renderWorld.mainLight.color.x;
  target[39] = renderWorld.mainLight.color.y;
  target[40] = renderWorld.mainLight.color.z;
  target[41] = renderWorld.mainLight.intensity;
  target[42] = renderWorld.mainLight.ambient;
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
