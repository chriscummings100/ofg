import { deepEqual, equal } from "node:assert/strict";
import {
  fakeRenderDebugOptions,
  fakeRendererStatus,
  fakeRustPerfStats
} from "../../../tests/fixtures/debugSnapshotFixtures.js";
import type { BrowserFrameInput, RustBrowserGameCommand } from "./browserGameTypes.js";
import {
  RustBrowserGameRuntime,
  terrainPresetToWasmCode,
  type RustBrowserGameRenderer
} from "./rustBrowserGameRuntime.js";

describe("RustBrowserGameRuntime", () => {
  it("maps browser terrain preset ids to Rust reset-game preset codes", () => {
    equal(terrainPresetToWasmCode("seed"), 0);
    equal(terrainPresetToWasmCode("rollingHills"), 1);
    equal(terrainPresetToWasmCode("mountainValley"), 2);
    equal(terrainPresetToWasmCode("rockyHighland"), 3);
  });

  it("delegates frame input, commands, and snapshots to Rust", () => {
    const renderer = fakeRenderer();
    const runtime = new RustBrowserGameRuntime({ renderer });
    const frame: BrowserFrameInput = {
      deltaSeconds: 0.125,
      movement: {
        forward: 1,
        right: -1,
        up: 0,
        fast: true
      },
      look: {
        deltaX: 2,
        deltaY: -3
      }
    };

    runtime.tick(frame);
    runtime.command({ type: "resetStreaming" });
    runtime.command({ type: "setPlayerPosition", x: 32, z: 16 });
    runtime.command({ type: "setShadowDebugView", view: "shadowDepthCascade0" });
    runtime.command({ type: "setPostProcessDebugView", view: "sceneColor" });
    runtime.command({ type: "setPostProcessToneMapping", enabled: false, exposure: 0.75 });
    runtime.command({
      type: "setPostProcessBloom",
      enabled: true,
      threshold: 0.85,
      intensity: 0.3
    });
    runtime.command({
      type: "setPostProcessDepthOfField",
      enabled: true,
      focusDistance: 18,
      focusRange: 4,
      maxBlurPixels: 10
    });
    runtime.command({
      type: "setPostProcessFog",
      enabled: true,
      startDistance: 6400,
      endDistance: 10800,
      density: 0.8,
      colorR: 0.5,
      colorG: 0.6,
      colorB: 0.7,
      curve: 1.6
    });
    runtime.command({ type: "setWaterDebugView", view: "pathLength" });
    runtime.command({
      type: "setWaterOptions",
      enabled: true,
      reflectionEnabled: false,
      seaLevelMeters: 0,
      shallowDepthMeters: 3,
      deepDepthMeters: 42,
      waveScale: 0.12,
      waveStrength: 0.3
    });
    runtime.command({
      type: "setRenderDebugOptions",
      skyEnabled: false,
      skyCloudNoiseEnabled: false,
      shadowSamplingEnabled: false,
      materialMode: "lambert"
    });
    runtime.command({ type: "resetRenderDebugOptions" });
    runtime.command({ type: "resetPerfStats" });
    const snapshot = runtime.debugSnapshot();

    deepEqual(renderer.tickCalls[0], frame);
    deepEqual(renderer.commandCalls[0], { type: "resetStreaming" });
    deepEqual(renderer.commandCalls[1], { type: "setPlayerPosition", x: 32, z: 16 });
    deepEqual(renderer.commandCalls[2], {
      type: "setShadowDebugView",
      view: "shadowDepthCascade0"
    });
    deepEqual(renderer.commandCalls[3], { type: "setPostProcessDebugView", view: "sceneColor" });
    deepEqual(renderer.commandCalls[4], {
      type: "setPostProcessToneMapping",
      enabled: false,
      exposure: 0.75
    });
    deepEqual(renderer.commandCalls[5], {
      type: "setPostProcessBloom",
      enabled: true,
      threshold: 0.85,
      intensity: 0.3
    });
    deepEqual(renderer.commandCalls[6], {
      type: "setPostProcessDepthOfField",
      enabled: true,
      focusDistance: 18,
      focusRange: 4,
      maxBlurPixels: 10
    });
    deepEqual(renderer.commandCalls[7], {
      type: "setPostProcessFog",
      enabled: true,
      startDistance: 6400,
      endDistance: 10800,
      density: 0.8,
      colorR: 0.5,
      colorG: 0.6,
      colorB: 0.7,
      curve: 1.6
    });
    deepEqual(renderer.commandCalls[8], { type: "setWaterDebugView", view: "pathLength" });
    deepEqual(renderer.commandCalls[9], {
      type: "setWaterOptions",
      enabled: true,
      reflectionEnabled: false,
      seaLevelMeters: 0,
      shallowDepthMeters: 3,
      deepDepthMeters: 42,
      waveScale: 0.12,
      waveStrength: 0.3
    });
    deepEqual(renderer.commandCalls[10], {
      type: "setRenderDebugOptions",
      skyEnabled: false,
      skyCloudNoiseEnabled: false,
      shadowSamplingEnabled: false,
      materialMode: "lambert"
    });
    deepEqual(renderer.commandCalls[11], { type: "resetRenderDebugOptions" });
    deepEqual(renderer.commandCalls[12], { type: "resetPerfStats" });
    equal(snapshot.terrainStreamStatus.workerPoolRuntime, "rust-sync");
    equal(snapshot.rendererStatus.runtime, "rust-wgpu");
    equal(snapshot.rendererStatus.gpuTimerAvailable, false);
    equal(snapshot.rendererStatus.postProcessRuntime, "rust-wgpu");
    equal(snapshot.rendererStatus.postProcessToneMappingEnabled, true);
    equal(snapshot.rendererStatus.postProcessBloomEnabled, true);
    equal(snapshot.rendererStatus.postProcessDofEnabled, false);
    equal(snapshot.rendererStatus.postProcessFogEnabled, true);
    equal(snapshot.rendererStatus.waterRuntime, "rust-wgpu");
    equal(snapshot.rendererStatus.waterBathymetryRuntime, "rust-heightfield");
    equal(snapshot.shadowDebugView, "cascadeIndex");
    equal(snapshot.rustPerfStats.sampleCount, 1);
    equal(snapshot.renderDebugOptions.skyEnabled, true);
    equal(snapshot.playerMode, "debugFly");
    deepEqual(snapshot.playerPosition, { x: 32, y: 8, z: 16 });
  });
});

const FAKE_TERRAIN_VARIANT = Object.freeze([
  1, 1, 3, 16, 4, 0.004, 2, 0.5, 3, 3, 0.009, 2.1, 0.48, 1, 1.8, 2,
  0.004, 2, 0.5, 14, 0.018, 1.3, 3, 0.03, 2.05, 0.44, 3.2, 1, 1, 1, 1, 1
]);

type FakeRenderer = RustBrowserGameRenderer & {
  readonly tickCalls: BrowserFrameInput[];
  readonly commandCalls: RustBrowserGameCommand[];
};

function fakeRenderer(): FakeRenderer {
  return {
    runtime: "rust-wgpu",
    tickCalls: [],
    commandCalls: [],
    tick(frame) {
      this.tickCalls.push(frame);
    },
    command(command) {
      this.commandCalls.push(command);
    },
    getDebugSnapshot() {
      return {
        playerMode: "debugFly",
        playerPosition: { x: 32, y: 8, z: 16 },
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
          generation: 1,
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
              minDesiredNodeY: 0,
              maxDesiredNodeY: 0,
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
        rendererStatus: fakeRendererStatus(),
        rustPerfStats: fakeRustPerfStats(),
        renderDebugOptions: fakeRenderDebugOptions(),
        shadowDebugView: "cascadeIndex",
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
