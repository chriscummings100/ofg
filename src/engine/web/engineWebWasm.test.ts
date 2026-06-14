import { equal, ok } from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";
import {
  fakeRenderDebugOptions,
  fakeRendererStatus,
  fakeRustPerfStats
} from "../../../tests/fixtures/debugSnapshotFixtures.js";
import {
  createEngineWebBrowserGame,
  loadEngineWebWasmModule,
  patchLegacyWgpuRequiredLimits,
  type EngineWebBrowserGame,
  type EngineWebWasmModule
} from "./engineWebWasm.js";

describe("engine web WASM", () => {
  it("exposes deterministic engine web wasm-bindgen artifact metadata", () => {
    equal(ENGINE_WEB_WASM_METADATA.id, "engine_web");
    equal(ENGINE_WEB_WASM_METADATA.sourceCrate, "crates/engine_web");
    equal(ENGINE_WEB_WASM_METADATA.modulePath, "assets/wasm/engine_web/engine_web.js");
    equal(ENGINE_WEB_WASM_METADATA.wasmPath, "assets/wasm/engine_web/engine_web_bg.wasm");
    equal(ENGINE_WEB_WASM_METADATA.dtsPath, "assets/wasm/engine_web/engine_web.d.ts");
    equal(ENGINE_WEB_WASM_METADATA.target, "wasm32-unknown-unknown");
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_WEB_WASM_METADATA.wasmHash));
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_WEB_WASM_METADATA.moduleHash));
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_WEB_WASM_METADATA.dtsHash));
    const exports = ENGINE_WEB_WASM_METADATA.exports as readonly string[];
    ok(exports.includes("RustBrowserGame"));
    equal(exports.includes("RustBrowserGameStatus"), false);
  });

  it("recomputes generated wasm-bindgen artifact hashes from current files", () => {
    const wasmHash = hashFile(ENGINE_WEB_WASM_METADATA.wasmPath);
    const moduleHash = hashFile(ENGINE_WEB_WASM_METADATA.modulePath);
    const dtsHash = hashFile(ENGINE_WEB_WASM_METADATA.dtsPath);

    equal(ENGINE_WEB_WASM_METADATA.wasmHash, wasmHash);
    equal(ENGINE_WEB_WASM_METADATA.moduleHash, moduleHash);
    equal(ENGINE_WEB_WASM_METADATA.dtsHash, dtsHash);
  });

  it("emits wasm-bindgen glue for the Rust/wgpu renderer facade", () => {
    const moduleText = readFileSync(ENGINE_WEB_WASM_METADATA.modulePath, "utf8");
    const dtsText = readFileSync(ENGINE_WEB_WASM_METADATA.dtsPath, "utf8");

    ok(moduleText.includes("export class RustBrowserGame"));
    equal(moduleText.includes("export class RustBrowserGameStatus"), false);
    ok(dtsText.includes("static create(canvas: HTMLCanvasElement, asset_loader: any): Promise<RustBrowserGame>"));
    ok(dtsText.includes("resize(viewport: any): void"));
    equal(dtsText.includes("resize(width"), false);
    equal(dtsText.includes("resetGame"), false);
    ok(dtsText.includes("tick(frame: any): void"));
    equal(dtsText.includes("tick(delta_seconds"), false);
    ok(dtsText.includes("command(command: any): void"));
    ok(dtsText.includes("debugSnapshot(): any"));
    equal(dtsText.includes("terrainHeightAt"), false);
    equal(dtsText.includes("togglePlayerMode"), false);
    equal(dtsText.includes("playerMode()"), false);
    equal(dtsText.includes("setPlayerMode"), false);
    equal(dtsText.includes("playerX"), false);
    equal(dtsText.includes("playerY"), false);
    equal(dtsText.includes("playerZ"), false);
    equal(dtsText.includes("setPlayerPosition"), false);
    equal(dtsText.includes("setDebugCamera"), false);
    equal(dtsText.includes("upsertTerrainMesh"), false);
    equal(dtsText.includes("destroyTerrainMesh"), false);
    equal(dtsText.includes("retainTerrainMeshes"), false);
    equal(dtsText.includes("clearTerrainMeshes"), false);
    equal(dtsText.includes("upsertTerrainTextures"), false);
    equal(dtsText.includes("renderFrame(): void"), false);
    equal(dtsText.includes("renderGameFrame"), false);
    equal(dtsText.includes("status()"), false);
    equal(dtsText.includes("RustBrowserGameStatus"), false);
    equal(dtsText.includes("renderEngineFrame"), false);
    equal(dtsText.includes("upsertMesh"), false);
    equal(dtsText.includes("destroyMesh"), false);
    equal(dtsText.includes("floats_per_vertex"), false);
    equal(dtsText.includes("world_matrices"), false);
    equal(dtsText.includes("upsertTexture"), false);
    equal(dtsText.includes("upsertTerrainMaterial"), false);
    equal(dtsText.includes("upsertMaterial"), false);
  });

  it("loads and initializes the wasm-bindgen module through a dynamic import hook", async () => {
    const calls: string[] = [];
    const module = fakeModule();
    const loaded = await loadEngineWebWasmModule(async (specifier) => {
      calls.push(specifier);
      return module;
    });

    equal(loaded, module);
    equal(module.initialized, true);
    equal(calls.length, 1);
    ok(calls[0].endsWith("/assets/wasm/engine_web/engine_web.js"));
  });

  it("creates the Rust browser game facade from the loaded module", async () => {
    const game = fakeBrowserGame();
    const assetLoader = {
      loadTextureArrays: async () => [],
      loadBytes: async () => []
    };
    const created = await createEngineWebBrowserGame(
      {} as HTMLCanvasElement,
      assetLoader,
      async () => fakeModule(game)
    );

    equal(created, game);
    equal(fakeCreateAssetLoaders[0], assetLoader);
  });

  it("patches the legacy wgpu limit name before browser device requests", async () => {
    let requestedLimits: Record<string, number | undefined> | undefined;
    const globalObject = {
      GPUAdapter: {
        prototype: {
          async requestDevice(descriptor?: { requiredLimits?: Record<string, number> }) {
            requestedLimits = descriptor?.requiredLimits;
            return {};
          }
        }
      }
    };

    equal(patchLegacyWgpuRequiredLimits(globalObject as unknown as typeof globalThis), true);
    await globalObject.GPUAdapter.prototype.requestDevice({
      requiredLimits: {
        maxTextureArrayLayers: 16,
        maxInterStageShaderComponents: 60
      }
    });

    equal(requestedLimits?.maxTextureArrayLayers, 16);
    equal(requestedLimits?.maxInterStageShaderVariables, undefined);
    equal(requestedLimits?.maxInterStageShaderComponents, undefined);
    equal(patchLegacyWgpuRequiredLimits(globalObject as unknown as typeof globalThis), false);
  });
});

function fakeModule(
  game: EngineWebBrowserGame = fakeBrowserGame()
): EngineWebWasmModule & { initialized: boolean } {
  return {
    initialized: false,
    async default() {
      this.initialized = true;
    },
    RustBrowserGame: {
      async create(_canvas, assetLoader) {
        fakeCreateAssetLoaders.push(assetLoader);
        return game;
      }
    }
  };
}

const fakeCreateAssetLoaders: unknown[] = [];
const FAKE_TERRAIN_VARIANT = Object.freeze([
  1, 1, 3, 16, 4, 0.004, 2, 0.5, 3, 3, 0.009, 2.1, 0.48, 1, 1.8, 2,
  0.004, 2, 0.5, 14, 0.018, 1.3, 3, 0.03, 2.05, 0.44, 3.2, 1, 1, 1, 1, 1
]);

function hashFile(path: string): string {
  return `sha256-${createHash("sha256").update(readFileSync(path)).digest("hex")}`;
}

function fakeBrowserGame(): EngineWebBrowserGame {
  return {
    resize() {},
    tick() {},
    configureTerrainWorkers() {},
    takeTerrainBuildRequests() {
      return [];
    },
    completeTerrainBuilds(completions) {
      return completions.length;
    },
    command() {},
    debugSnapshot() {
      return {
        playerMode: "firstPerson",
        playerPosition: { x: 0, y: 0, z: 0 },
        loadedTerrainChunkKeys: ["0,0,0"],
        loadedTerrainNodeKeys: ["lod0:0,0,0"],
        terrainChunkKeys: ["0,0,0"],
        terrainNodeKeys: ["lod0:0,0,0"],
        terrainPreset: "rollingHills",
        terrainSeed: 0x0F6,
        terrainVariantRevision: 2,
        terrainVariant: FAKE_TERRAIN_VARIANT,
        terrainPresetCatalog: fakeTerrainPresetCatalog(),
        terrainVariantProbe: fakeTerrainVariantProbe(),
        terrainStreamStatus: {
          generation: 0,
          pending: false,
          loadedChunkCount: 1,
          densityReadyChunkCount: 1,
          sharedDensityChunkCount: 1,
          inFlightDensityCount: 0,
          missingDensityCount: 0,
          desiredRenderChunkCount: 1,
          renderedChunkCount: 1,
          emptyChunkCount: 0,
          inFlightChunkCount: 0,
          missingChunkCount: 0,
          loadedNodeCount: 1,
          desiredRenderNodeCount: 1,
          renderedNodeCount: 1,
          emptyNodeCount: 0,
          missingNodeCount: 0,
          maxRenderedLod: 0,
          visibleWorldSpanXMeters: 32,
          visibleWorldSpanZMeters: 32,
          terrainLodSummary: [
            {
              lod: 0,
              desiredNodeCount: 1,
              densityReadyNodeCount: 1,
              renderedNodeCount: 1,
              emptyNodeCount: 0,
              missingNodeCount: 0
            }
          ],
          placementCandidateCount: 0,
          placementSampleCount: 0,
          placementMissedSurfaceCount: 0,
          placementRejectedBelowWaterCount: 0,
          placementRejectedSlopeCount: 0,
          transitionFaceCount: 0,
          transitionMeshCount: 0,
          transitionVertexFloatCount: 0,
          transitionIndexCount: 0,
          maxConcurrentChunkJobs: 6,
          workerPoolRuntime: "rust-sync",
          terrainWorkerCount: 0,
          terrainWorkerInFlightCount: 0,
          terrainWorkerQueuedRequestCount: 0,
          terrainWorkerCompletedCount: 0,
          terrainWorkerStaleCompletionCount: 0,
          terrainWorkerFailedCount: 0,
          synchronousBuildCount: 0
        },
        terrainStreamerRuntime: "rust",
        terrainStreamSchedulerRuntime: "rust",
        terrainDensityStoreRuntime: "rust",
        terrainWorkerPoolRuntime: "rust-sync",
        renderPacketRuntime: "rust",
        terrainRenderPacketRuntime: "rust",
        rendererRuntime: "rust-wgpu",
        rendererStatus: fakeRendererStatus({ canvasWidth: 1, canvasHeight: 1, meshCount: 0 }),
        rustPerfStats: fakeRustPerfStats(),
        renderDebugOptions: fakeRenderDebugOptions(),
        shadowDebugView: "off",
        terrainWorkerCount: 0,
        playerControllerRuntime: "rust"
      };
    }
  };
}

function fakeTerrainPresetCatalog() {
  return [
    {
      code: 1,
      id: "rollingHills" as const,
      name: "Rolling Hills",
      terrainVariant: FAKE_TERRAIN_VARIANT
    }
  ];
}

function fakeTerrainVariantProbe() {
  return {
    sampleCount: 5,
    heightMin: 1,
    heightMax: 8,
    slopeMin: 0.1,
    slopeMax: 0.6,
    macroBaseElevation: 4,
    mountainness: 0.35,
    ridge: 0.42,
    cellularEdge: 0.22,
    materialIndices: [0, 11, 13, 15],
    materialWeights: [0.5, 0.25, 0.15, 0.1],
    biomeWeights: {
      grassland: 0.4,
      temperateForest: 0.2,
      wetland: 0.1,
      coastBeach: 0,
      dryBadland: 0.1,
      alpineMeadow: 0.1,
      highMountainRock: 0.1,
      snowTundra: 0
    }
  };
}
