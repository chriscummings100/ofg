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
      type: "setRenderDebugOptions",
      skyEnabled: false,
      skyCloudNoiseEnabled: false,
      shadowSamplingEnabled: false,
      materialMode: "lambert"
    });
    deepEqual(renderer.commandCalls[8], { type: "resetRenderDebugOptions" });
    deepEqual(renderer.commandCalls[9], { type: "resetPerfStats" });
    equal(snapshot.terrainStreamStatus.workerPoolRuntime, "rust-sync");
    equal(snapshot.rendererStatus.runtime, "rust-wgpu");
    equal(snapshot.rendererStatus.gpuTimerAvailable, false);
    equal(snapshot.rendererStatus.postProcessRuntime, "rust-wgpu");
    equal(snapshot.rendererStatus.postProcessToneMappingEnabled, true);
    equal(snapshot.rendererStatus.postProcessBloomEnabled, true);
    equal(snapshot.rendererStatus.postProcessDofEnabled, false);
    equal(snapshot.shadowDebugView, "cascadeIndex");
    equal(snapshot.rustPerfStats.sampleCount, 1);
    equal(snapshot.renderDebugOptions.skyEnabled, true);
    equal(snapshot.playerMode, "debugFly");
    deepEqual(snapshot.playerPosition, { x: 32, y: 8, z: 16 });
  });
});

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
              densityReadyNodeCount: 1,
              renderedNodeCount: 1,
              emptyNodeCount: 0,
              missingNodeCount: 0
            }
          ],
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
