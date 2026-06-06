import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";
import {
  createBrowserTextureAssetLoader,
  type BrowserTextureAssetLoader
} from "../browser/textureAssetLoader.js";
import type {
  BrowserFrameInput,
  BrowserViewport,
  RustBrowserGameCommand,
  RustBrowserGameDebugSnapshot
} from "./browserGameTypes.js";

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
  resize(viewport: BrowserViewport): void;
  tick(frame: BrowserFrameInput): void;
  command(command: RustBrowserGameCommand): void;
  debugSnapshot(): RustBrowserGameDebugSnapshot;
};

export type EngineWebWasmModule = {
  default(input?: unknown): Promise<unknown>;
  readonly RustBrowserGame: {
    create(
      canvas: HTMLCanvasElement,
      assetLoader: BrowserTextureAssetLoader
    ): Promise<EngineWebBrowserGame>;
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
  assetLoader: BrowserTextureAssetLoader = createBrowserTextureAssetLoader(),
  loadModule: () => Promise<EngineWebWasmModule> = loadEngineWebWasmModule
): Promise<EngineWebBrowserGame> {
  patchLegacyWgpuRequiredLimits();
  const module = await loadModule();

  return module.RustBrowserGame.create(canvas, assetLoader);
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
