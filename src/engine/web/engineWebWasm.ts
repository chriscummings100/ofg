import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";
import {
  createBrowserTextureAssetLoader,
  type BrowserTextureAssetLoader
} from "../browser/textureAssetLoader.js";
import type {
  BrowserFrameInput,
  BrowserViewport,
  GpuPassTimingSample,
  PostProcessDebugView,
  RenderCounterSample,
  RenderDebugOptions,
  RustBrowserGameCommand,
  WaterDebugView,
  RustBrowserGameDebugSnapshot
} from "./browserGameTypes.js";
import type { TerrainBuildCompletion, TerrainBuildRequest } from "./terrainWorkerClient.js";

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
  readonly frameVisibleDrawCount: number;
  readonly frameShadowDrawCount: number;
  readonly frameCulledDrawCount: number;
  readonly frameSubmittedVertexCount: number;
  readonly frameSubmittedIndexCount: number;
  readonly frameSubmittedTriangleCount: number;
  readonly terrainUpdateTotalMs: number;
  readonly terrainCompletionIngestMs: number;
  readonly terrainWorkerRequestDrainMs: number;
  readonly terrainStreamTickMs: number;
  readonly terrainStreamSyncMs: number;
  readonly terrainStreamSchedulerMs: number;
  readonly terrainStreamWorkerQueueMs: number;
  readonly terrainStreamVisibilityMs: number;
  readonly terrainStreamVisibilitySelectMs: number;
  readonly terrainStreamVisibilityStatusMs: number;
  readonly terrainStreamVisibilityApplyMs: number;
  readonly terrainMeshDestroyMs: number;
  readonly terrainMeshUploadMs: number;
  readonly terrainCompletionCount: number;
  readonly terrainCompletionAcceptedCount: number;
  readonly terrainCompletionVertexFloatCount: number;
  readonly terrainCompletionIndexCount: number;
  readonly terrainWorkerRequestCount: number;
  readonly terrainUpdateUpsertedMeshCount: number;
  readonly terrainUpdateRemovedMeshCount: number;
  readonly terrainUpdateUploadedVertexFloatCount: number;
  readonly terrainUpdateUploadedIndexCount: number;
  readonly terrainUpdateDeferredUploadCount: number;
  readonly terrainUpdateDeferredRemovalCount: number;
  readonly terrainUpdateUploadBudgetHit: boolean;
  readonly terrainUpdateRemovalBudgetHit: boolean;
  readonly shadowCascadeCount: number;
  readonly shadowMapSize: number;
  readonly shadowMaxDistanceMeters: number;
  readonly shadowStrength: number;
  readonly shadowEffectiveSunElevation: number;
  readonly shadowEffectiveSunDirection: {
    readonly x: number;
    readonly y: number;
    readonly z: number;
  };
  readonly gpuTimerAvailable: boolean;
  readonly gpuTimerUnavailableReason: string;
  readonly gpuTimestampPeriodNs: number;
  readonly gpuTimerPendingReadbackCount: number;
  readonly renderDebugOptions: RenderDebugOptions;
  readonly lastRenderCounters: RenderCounterSample;
  readonly lastGpuPassTimings: GpuPassTimingSample;
  readonly postProcessRuntime: "rust-wgpu";
  readonly postProcessDebugView: PostProcessDebugView;
  readonly postProcessExposure: number;
  readonly postProcessToneMappingEnabled: boolean;
  readonly postProcessBloomEnabled: boolean;
  readonly postProcessBloomThreshold: number;
  readonly postProcessBloomIntensity: number;
  readonly postProcessDofEnabled: boolean;
  readonly postProcessDofFocusDistance: number;
  readonly postProcessDofFocusRange: number;
  readonly postProcessDofMaxBlurPixels: number;
  readonly waterRuntime: "rust-wgpu";
  readonly waterEnabled: boolean;
  readonly waterReflectionEnabled: boolean;
  readonly waterSeaLevelMeters: number;
  readonly waterBathymetryRuntime: "rust-heightfield";
  readonly waterBathymetryGridSize: number;
  readonly waterBathymetryWorldSpanMeters: number;
  readonly waterBathymetryCenterX: number;
  readonly waterBathymetryCenterZ: number;
  readonly waterReflectionWidth: number;
  readonly waterReflectionHeight: number;
  readonly waterDebugView: WaterDebugView;
};

export type EngineWebBrowserGame = {
  resize(viewport: BrowserViewport): void;
  tick(frame: BrowserFrameInput): void;
  configureTerrainWorkers(options: { readonly workerCount: number }): void;
  takeTerrainBuildRequests(): TerrainBuildRequest[];
  completeTerrainBuilds(completions: readonly TerrainBuildCompletion[]): number;
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
