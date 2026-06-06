import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";
import type {
  BrowserFrameInput,
  RustBrowserGameCommand,
  RustBrowserGameDebugSnapshot
} from "./browserGameTypes.js";

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

export type EngineWebBrowserGame = {
  resize(width: number, height: number): void;
  resetGame(terrainSeed: number, terrainPreset: number): void;
  tick(frame: BrowserFrameInput): void;
  command(command: RustBrowserGameCommand): void;
  debugSnapshot(): RustBrowserGameDebugSnapshot;
  upsertTerrainMesh(
    chunkKey: string,
    vertices: Float32Array,
    indices: Uint32Array
  ): void;
  destroyTerrainMesh(chunkKey: string): void;
  retainTerrainMeshes(chunkKeys: string[]): void;
  clearTerrainMeshes(): void;
  upsertTerrainTextures(
    width: number,
    height: number,
    layers: number,
    formatCode: number,
    albedoData: Uint8Array,
    normalData: Uint8Array,
    materialData: Uint8Array
  ): void;
  renderGameFrame(aspect: number): void;
  status(): EngineWebRendererStatus;
};

export type EngineWebWasmModule = {
  default(input?: unknown): Promise<unknown>;
  readonly RustBrowserGame: {
    create(canvas: HTMLCanvasElement): Promise<EngineWebBrowserGame>;
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

export async function createEngineWebBrowserGame(
  canvas: HTMLCanvasElement,
  loadModule: () => Promise<EngineWebWasmModule> = loadEngineWebWasmModule
): Promise<EngineWebBrowserGame> {
  patchLegacyWgpuRequiredLimits();
  const module = await loadModule();

  return module.RustBrowserGame.create(canvas);
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
