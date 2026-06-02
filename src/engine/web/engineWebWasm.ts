import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";

export const ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM = 1;

export type EngineWebRendererStatus = {
  readonly version: number;
  readonly runtime: "rust-wgpu";
  readonly configured: boolean;
  readonly canvasWidth: number;
  readonly canvasHeight: number;
  readonly maxTextureArrayLayers: number;
  readonly requiredTextureArrayLayers: number;
  readonly meshCount: number;
  readonly textureCount: number;
  readonly objectCount: number;
  readonly frameIndex: number;
  readonly frameDrawCount: number;
};

export type EngineWebWgpuRenderer = {
  resize(width: number, height: number): void;
  registerMesh(
    vertices: Float32Array,
    indices: Uint32Array,
    floatsPerVertex: number
  ): number;
  destroyMesh(handle: number): void;
  registerTexture(
    width: number,
    height: number,
    layers: number,
    formatCode: number,
    data: Uint8Array
  ): number;
  destroyTexture(handle: number): void;
  registerObject(): number;
  destroyObject(handle: number): void;
  render(
    framePacket: Float32Array,
    meshHandles: Float64Array,
    objectHandles: Float64Array,
    albedoTextureHandles: Float64Array,
    normalTextureHandles: Float64Array,
    materialTextureHandles: Float64Array,
    worldMatrices: Float32Array,
    materialPackets: Float32Array
  ): void;
  renderEngineFrame(
    engineSnapshot: Float32Array,
    aspect: number,
    meshHandles: Float64Array,
    objectHandles: Float64Array,
    albedoTextureHandles: Float64Array,
    normalTextureHandles: Float64Array,
    materialTextureHandles: Float64Array,
    worldMatrices: Float32Array,
    materialPackets: Float32Array,
    playerMarkerMeshHandle: number,
    playerMarkerObjectHandle: number,
    playerMarkerAlbedoTextureHandle: number,
    playerMarkerNormalTextureHandle: number,
    playerMarkerMaterialTextureHandle: number,
    playerMarkerMaterialPacket: Float32Array
  ): void;
  fallbackAlbedoTextureHandle(): number;
  fallbackNormalTextureHandle(): number;
  fallbackMaterialTextureHandle(): number;
  status(): EngineWebRendererStatus;
};

export type EngineWebWasmModule = {
  default(input?: unknown): Promise<unknown>;
  readonly RustWgpuRenderer: {
    create(canvas: HTMLCanvasElement): Promise<EngineWebWgpuRenderer>;
  };
};

type DynamicImport = (specifier: string) => Promise<unknown>;

export async function loadEngineWebWasmModule(
  importModule: DynamicImport = (specifier) => import(specifier)
): Promise<EngineWebWasmModule> {
  const moduleUrl = new URL(`../../../${ENGINE_WEB_WASM_METADATA.modulePath}`, import.meta.url);
  const module = await importModule(moduleUrl.href) as EngineWebWasmModule;
  await module.default();

  return module;
}

export async function createEngineWebRenderer(
  canvas: HTMLCanvasElement,
  loadModule: () => Promise<EngineWebWasmModule> = loadEngineWebWasmModule
): Promise<EngineWebWgpuRenderer> {
  patchLegacyWgpuRequiredLimits();
  const module = await loadModule();

  return module.RustWgpuRenderer.create(canvas);
}

export function patchLegacyWgpuRequiredLimits(globalObject: typeof globalThis = globalThis): boolean {
  const gpuAdapter = (globalObject as unknown as {
    GPUAdapter?: {
      prototype?: {
        requestDevice?: (descriptor?: GpuDeviceDescriptorCompat) => Promise<unknown>;
        __ofgLegacyLimitPatch?: true;
      };
    };
  }).GPUAdapter;
  const prototype = gpuAdapter?.prototype;
  const original = prototype?.requestDevice;
  if (prototype === undefined || original === undefined || prototype.__ofgLegacyLimitPatch) {
    return false;
  }

  prototype.requestDevice = function patchedRequestDevice(
    this: unknown,
    descriptor?: GpuDeviceDescriptorCompat
  ): Promise<unknown> {
    const requiredLimits = descriptor?.requiredLimits;
    if (
      requiredLimits !== undefined &&
      "maxInterStageShaderComponents" in requiredLimits
    ) {
      const patchedLimits = { ...requiredLimits };
      delete patchedLimits.maxInterStageShaderComponents;

      return original.call(this, { ...descriptor, requiredLimits: patchedLimits });
    }

    return original.call(this, descriptor);
  };
  prototype.__ofgLegacyLimitPatch = true;

  return true;
}

type GpuDeviceDescriptorCompat = {
  readonly requiredFeatures?: readonly string[];
  readonly requiredLimits?: {
    readonly [key: string]: number | undefined;
    maxInterStageShaderComponents?: number;
    maxInterStageShaderVariables?: number;
  };
};
