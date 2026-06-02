import { getFloatsPerVertex } from "../world/terrainMesh.js";
import {
  createEngineWebRenderer,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebRendererStatus,
  type EngineWebWgpuRenderer
} from "../web/engineWebWasm.js";
import { FRAME_UNIFORM_FLOATS, buildFrameUniformValues } from "./FrameUniforms.js";
import type { Mesh } from "./Mesh.js";
import { OBJECT_UNIFORM_FLOATS, buildObjectUniformValues } from "./ObjectUniforms.js";
import type { RenderItem, RenderWorld } from "./RenderWorld.js";
import type { Texture } from "./Texture.js";

type GpuMeshHandle = {
  readonly handle: number;
};

type GpuObjectHandle = {
  readonly handle: number;
  readonly uniformValues: Float32Array;
};

export class RustWgpuRendererAdapter {
  readonly runtime = "rust-wgpu" as const;
  private readonly meshCache = new Map<Mesh, GpuMeshHandle>();
  private readonly textureCache = new WeakMap<Texture, number>();
  private readonly objectHandles = new Map<string, GpuObjectHandle>();
  private readonly frameUniformValues = new Float32Array(FRAME_UNIFORM_FLOATS);
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
    const objectUniforms = new Float32Array(itemCount * OBJECT_UNIFORM_FLOATS);
    const seenItemIds = new Set<string>();
    const seenMeshes = new Set<Mesh>();

    buildFrameUniformValues(renderWorld.camera, renderWorld.mainLight, this.frameUniformValues);
    for (let index = 0; index < itemCount; index += 1) {
      const item = renderWorld.items[index];
      const object = this.getGpuObject(item);
      const objectUniformTarget = objectUniforms.subarray(
        index * OBJECT_UNIFORM_FLOATS,
        (index + 1) * OBJECT_UNIFORM_FLOATS
      );

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
      buildObjectUniformValues(item.worldMatrix, item.material, object.uniformValues);
      objectUniformTarget.set(object.uniformValues);
    }

    this.renderer.render(
      this.frameUniformValues,
      meshHandles,
      objectHandles,
      albedoTextureHandles,
      normalTextureHandles,
      materialTextureHandles,
      objectUniforms
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
      handle: this.renderer.registerObject(),
      uniformValues: new Float32Array(OBJECT_UNIFORM_FLOATS)
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
