import { equal } from "node:assert/strict";
import { deepEqual } from "node:assert/strict";
import {
  fakeRenderDebugOptions,
  fakeRendererStatus,
  fakeRustPerfStats
} from "../../../tests/fixtures/debugSnapshotFixtures.js";
import type { BrowserFrameInput } from "./browserGameTypes.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter, type TerrainWorkerBridge } from "./rustBrowserGameAdapter.js";
import type { TerrainBuildCompletion, TerrainBuildRequest } from "./terrainWorkerClient.js";

describe("RustBrowserGameAdapter", () => {
  it("resizes and ticks through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const frame = fakeFrameInput();

    withFakeWindow(() => adapter.tick(frame));

    equal(fake.tickCalls[0], frame);
    equal(fake.resizeCalls[0]?.width, 640);
    equal(fake.resizeCalls[0]?.height, 480);
  });

  it("routes terrain worker completions and requests around the Rust tick", () => {
    const fake = fakeBrowserGame();
    const request = fakeTerrainBuildRequest(8);
    const completion = fakeTerrainBuildCompletion(7);
    fake.pendingTerrainBuildRequests.push(request);
    const workers = fakeTerrainWorkers([completion]);
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake, workers);

    withFakeWindow(() => adapter.tick(fakeFrameInput()));

    deepEqual(fake.completedTerrainBuilds[0], [completion]);
    deepEqual(workers.submittedRequests[0], [request]);
    equal(workers.lastTakeCompletionsMaxCount, 6);
    const snapshot = adapter.getDebugSnapshot();
    const terrainFrame = snapshot.browserTerrainFrame;
    equal(terrainFrame?.drainedCompletionCount, 1);
    equal(terrainFrame?.submittedRequestCount, 1);

    withFakeWindow(() => adapter.tick(fakeFrameInput()));

    deepEqual(fake.completedTerrainBuilds[1], []);
  });

  it("resets terrain workers before terrain-changing commands", () => {
    const fake = fakeBrowserGame();
    const workers = fakeTerrainWorkers([]);
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake, workers);

    adapter.command({ type: "resetGame", terrainSeed: 0x0F6, terrainPreset: 1 });
    adapter.command({
      type: "setTerrainVariant",
      terrainSeed: 0x0F6,
      terrainPreset: 1,
      terrainVariant: FAKE_TERRAIN_VARIANT
    });
    adapter.command({ type: "resetStreaming" });
    adapter.command({ type: "togglePlayerMode" });

    equal(workers.resetCount, 3);
    equal(fake.commandCalls[1]?.type, "setTerrainVariant");
  });

  it("forwards browser frame input and player controls to the Rust game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);

    adapter.command({ type: "resetGame", terrainSeed: 0x0F6, terrainPreset: 1 });
    withFakeWindow(() => adapter.tick({
      deltaSeconds: 0.25,
      movement: {
        forward: 1,
        right: -1,
        up: 0,
        fast: true
      },
      look: {
        deltaX: 3,
        deltaY: -2
      }
    }));
    adapter.command({ type: "togglePlayerMode" });
    adapter.command({ type: "setPlayerMode", mode: "firstPerson" });
    adapter.command({ type: "togglePlayerCharacter" });
    adapter.command({ type: "setPlayerCharacter", character: "female" });
    adapter.command({
      type: "setPlayerAnimationTuning",
      walkSpeedMetersPerSecond: 5.5,
      runSpeedMetersPerSecond: 16.5,
      idlePlaybackScale: 1,
      walkPlaybackScale: 0.95,
      runPlaybackScale: 1.1
    });
    adapter.command({ type: "setPlayerPosition", x: 96, z: 12 });
    adapter.command({ type: "setDebugCamera", x: 1, y: 2, z: 3, yaw: 0.25, pitch: -0.5 });
    adapter.command({ type: "setShadowDebugView", view: "cascadeIndex" });
    adapter.command({ type: "setPostProcessDebugView", view: "linearDepth" });
    adapter.command({ type: "setPostProcessToneMapping", enabled: true, exposure: 1.25 });
    adapter.command({
      type: "setPostProcessBloom",
      enabled: true,
      threshold: 0.9,
      intensity: 0.2
    });
    adapter.command({
      type: "setPostProcessDepthOfField",
      enabled: true,
      focusDistance: 18,
      focusRange: 4,
      maxBlurPixels: 10
    });
    adapter.command({
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
    adapter.command({ type: "setWaterDebugView", view: "bottomDepth" });
    adapter.command({
      type: "setWaterOptions",
      enabled: true,
      reflectionEnabled: false,
      seaLevelMeters: 0.5,
      shallowDepthMeters: 2.5,
      deepDepthMeters: 36,
      waveScale: 0.08,
      waveStrength: 0.25
    });
    adapter.command({
      type: "setRenderDebugOptions",
      skyEnabled: false,
      skyCloudNoiseEnabled: false,
      materialMode: "lambert"
    });
    adapter.command({ type: "resetRenderDebugOptions" });
    adapter.command({ type: "resetPerfStats" });
    const snapshot = adapter.getDebugSnapshot();

    equal(fake.commandCalls[0]?.type, "resetGame");
    equal(fake.tickCalls[0]?.deltaSeconds, 0.25);
    equal(fake.tickCalls[0]?.movement.forward, 1);
    equal(fake.tickCalls[0]?.movement.right, -1);
    equal(fake.tickCalls[0]?.movement.fast, true);
    equal(fake.commandCalls[1]?.type, "togglePlayerMode");
    equal(fake.commandCalls[2]?.type, "setPlayerMode");
    equal(fake.commandCalls[3]?.type, "togglePlayerCharacter");
    equal(fake.commandCalls[4]?.type, "setPlayerCharacter");
    equal(fake.commandCalls[5]?.type, "setPlayerAnimationTuning");
    equal(fake.commandCalls[6]?.type, "setPlayerPosition");
    equal(fake.commandCalls[7]?.type, "setDebugCamera");
    equal(fake.commandCalls[8]?.type, "setShadowDebugView");
    equal(fake.commandCalls[9]?.type, "setPostProcessDebugView");
    equal(fake.commandCalls[10]?.type, "setPostProcessToneMapping");
    equal(fake.commandCalls[11]?.type, "setPostProcessBloom");
    equal(fake.commandCalls[12]?.type, "setPostProcessDepthOfField");
    equal(fake.commandCalls[13]?.type, "setPostProcessFog");
    equal(fake.commandCalls[14]?.type, "setWaterDebugView");
    equal(fake.commandCalls[15]?.type, "setWaterOptions");
    equal(fake.commandCalls[16]?.type, "setRenderDebugOptions");
    equal(fake.commandCalls[17]?.type, "resetRenderDebugOptions");
    equal(fake.commandCalls[18]?.type, "resetPerfStats");
    equal(snapshot.playerMode, "firstPerson");
    equal(snapshot.playerPosition.x, 96);
    equal(snapshot.shadowDebugView, "shadowVisibility");
    equal(snapshot.loadedTerrainChunkKeys[0], "0,0,0");
    equal(snapshot.loadedTerrainNodeKeys[0], "lod0:0,0,0");
    equal(snapshot.terrainNodeKeys[0], "lod0:0,0,0");
    equal(snapshot.rendererStatus.postProcessDebugView, "final");
    equal(snapshot.rendererStatus.postProcessExposure, 1);
    equal(snapshot.rendererStatus.postProcessBloomThreshold, 1);
    equal(snapshot.rendererStatus.postProcessDofEnabled, false);
    equal(snapshot.rendererStatus.postProcessDofFocusDistance, 30);
    equal(snapshot.rendererStatus.postProcessFogEnabled, true);
    equal(snapshot.rendererStatus.waterRuntime, "rust-wgpu");
    equal(snapshot.rendererStatus.waterDebugView, "final");
    equal(snapshot.rendererStatus.gpuTimerAvailable, false);
    equal(snapshot.rustPerfStats.sampleCount, 1);
    equal(snapshot.renderDebugOptions.skyEnabled, true);
    equal(snapshot.browserTerrainFrame?.completionBudget, 6);
    equal(snapshot.playerCharacterId, "female");
    equal(snapshot.playerCharacterLabel, "Female");
    equal(snapshot.modelAnimationRuntime, "rust");
    equal(snapshot.activeModelAnimationClip, "test-move");
    equal(snapshot.nextModelAnimationClip, "test-walk");
    equal(snapshot.modelAnimationTimeSeconds, 0.25);
    equal(snapshot.modelAnimationDurationSeconds, 2);
    equal(snapshot.modelAnimationBlendWeight, 0.5);
    equal(snapshot.modelAnimationWalkRunBlendWeight, 1);
    equal(snapshot.modelAnimationPlaybackScale, 1.1);
    equal(snapshot.modelAnimationLocomotionSpeedMetersPerSecond, 16.5);
    equal(snapshot.modelAnimationWalkSpeedMetersPerSecond, 5.5);
    equal(snapshot.modelAnimationRunSpeedMetersPerSecond, 16.5);
    equal(snapshot.modelAnimationIdlePlaybackScale, 1);
    equal(snapshot.modelAnimationWalkPlaybackScale, 0.95);
    equal(snapshot.modelAnimationRunPlaybackScale, 1.1);
    equal(snapshot.modelSkinningRuntime, "rust-cpu");
    equal(snapshot.modelSkinningJointCount, 2);
    equal(snapshot.playerCharacterRuntime, "rust");
    equal(snapshot.playerCharacterVisible, true);
    equal(snapshot.playerCharacterFollowsPlayer, true);
    equal(snapshot.debugPlayerMarkerVisible, false);
    equal(snapshot.skyRuntime, "rust");
    equal(snapshot.skyDayPhase, 0.25);
    equal(snapshot.skySunElevation, 0.64);
    equal(snapshot.skyCloudCoverage, 0.34);
    equal(snapshot.skyStarIntensity, 0.08);
  });
});

const FAKE_TERRAIN_VARIANT = Object.freeze([
  1, 1, 3, 16, 4, 0.004, 2, 0.5, 3, 3, 0.009, 2.1, 0.48, 1, 1.8, 2,
  0.004, 2, 0.5, 14, 0.018, 1.3, 3, 0.03, 2.05, 0.44, 3.2, 1, 1, 1, 1, 1
]);

type FakeBrowserGame = EngineWebBrowserGame & {
  resizeCalls: Array<Parameters<EngineWebBrowserGame["resize"]>[0]>;
  tickCalls: BrowserFrameInput[];
  commandCalls: Array<Parameters<EngineWebBrowserGame["command"]>[0]>;
  completedTerrainBuilds: TerrainBuildCompletion[][];
  pendingTerrainBuildRequests: TerrainBuildRequest[];
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
    resizeCalls: [],
    tickCalls: [],
    commandCalls: [],
    completedTerrainBuilds: [],
    pendingTerrainBuildRequests: [],
    resize(viewport) {
      this.resizeCalls.push(viewport);
    },
    tick(frame) {
      this.tickCalls.push(frame);
    },
    configureTerrainWorkers() {},
    takeTerrainBuildRequests() {
      return this.pendingTerrainBuildRequests.splice(0);
    },
    completeTerrainBuilds(completions) {
      this.completedTerrainBuilds.push([...completions]);
      return completions.length;
    },
    command(command) {
      this.commandCalls.push(command);
    },
    debugSnapshot() {
      return {
        playerMode: "firstPerson",
        playerPosition: {
          x: 96,
          y: 7,
          z: 12
        },
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
        shadowDebugView: "shadowVisibility",
        terrainWorkerCount: 0,
        browserTerrainFrame: {
          completionBudget: 6,
          pendingCompletionCountBefore: 0,
          pendingCompletionCountAfter: 0,
          drainedCompletionCount: 0,
          drainedCompletionVertexBytes: 0,
          drainedCompletionIndexBytes: 0,
          submittedRequestCount: 0,
          workerInFlightRequestCount: 0,
          takeCompletionsMs: 0,
          completeTerrainBuildsMs: 0,
          gameTickMs: 0,
          takeRequestsMs: 0,
          submitRequestsMs: 0
        },
        playerControllerRuntime: "rust",
        skyRuntime: "rust",
        skyDayPhase: 0.25,
        skySunElevation: 0.64,
        skyCloudCoverage: 0.34,
        skyStarIntensity: 0.08,
        playerCharacterId: "female",
        playerCharacterLabel: "Female",
        playerCharacterRuntime: "rust",
        playerCharacterVisible: true,
        playerCharacterFollowsPlayer: true,
        debugPlayerMarkerVisible: false,
        modelAnimationRuntime: "rust",
        activeModelAnimationClip: "test-move",
        nextModelAnimationClip: "test-walk",
        modelAnimationTimeSeconds: 0.25,
        modelAnimationDurationSeconds: 2,
        modelAnimationBlendWeight: 0.5,
        modelAnimationWalkRunBlendWeight: 1,
        modelAnimationPlaybackScale: 1.1,
        modelAnimationLocomotionSpeedMetersPerSecond: 16.5,
        modelAnimationWalkSpeedMetersPerSecond: 5.5,
        modelAnimationRunSpeedMetersPerSecond: 16.5,
        modelAnimationIdlePlaybackScale: 1,
        modelAnimationWalkPlaybackScale: 0.95,
        modelAnimationRunPlaybackScale: 1.1,
        modelSkinningRuntime: "rust-cpu",
        modelSkinningJointCount: 2
      };
    }
  };
}

function fakeTerrainWorkers(completions: TerrainBuildCompletion[]): TerrainWorkerBridge & {
  submittedRequests: TerrainBuildRequest[][];
  lastTakeCompletionsMaxCount?: number;
  resetCount: number;
} {
  return {
    workerCount: 2,
    submittedRequests: [],
    resetCount: 0,
    takeCompletions(maxCount) {
      this.lastTakeCompletionsMaxCount = maxCount;
      return completions.splice(0, maxCount ?? completions.length);
    },
    submitRequests(requests) {
      this.submittedRequests.push([...requests]);
    },
    status() {
      return {
        pendingCompletionCount: completions.length,
        inFlightRequestCount: 0
      };
    },
    reset() {
      this.resetCount += 1;
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

function fakeTerrainBuildRequest(requestId: number): TerrainBuildRequest {
  return {
    requestId,
    generation: 1,
    lod: 0,
    x: 0,
    y: 0,
    z: 0,
    seed: 0x0F6,
    preset: 1,
    variantRevision: 2,
    terrainVariant: FAKE_TERRAIN_VARIANT,
    cellSize: 1
  };
}

function fakeTerrainBuildCompletion(requestId: number): TerrainBuildCompletion {
  return {
    requestId,
    generation: 1,
    lod: 0,
    x: 0,
    y: 0,
    z: 0,
    variantRevision: 2,
    failed: false,
    vertices: new Float32Array([1, 2, 3]),
    indices: new Uint32Array([0]),
    waterTexelCount: 0,
    waterOriginX: 0,
    waterOriginZ: 0,
    waterWorldSpanX: 0,
    waterWorldSpanZ: 0,
    waterSeaLevelMeters: 0,
    waterMaxDepthMeters: 0
  };
}

function fakeFrameInput(): BrowserFrameInput {
  return {
    deltaSeconds: 0.25,
    movement: {
      forward: 0,
      right: 0,
      up: 0,
      fast: false
    },
    look: {
      deltaX: 0,
      deltaY: 0
    }
  };
}

function fakeCanvas(): HTMLCanvasElement {
  return {
    clientWidth: 640,
    clientHeight: 480,
    width: 0,
    height: 0
  } as HTMLCanvasElement;
}

function withFakeWindow(action: () => void): void {
  const globalWithWindow = globalThis as unknown as {
    window?: { devicePixelRatio: number };
  };
  const previousWindow = globalWithWindow.window;
  globalWithWindow.window = { devicePixelRatio: 1 };
  try {
    action();
  } finally {
    globalWithWindow.window = previousWindow;
  }
}
