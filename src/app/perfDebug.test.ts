import { equal, deepEqual, ok } from "node:assert/strict";
import {
  BrowserPerfTracker,
  buildPerfOverlayLines,
  buildPerfStats,
  dumpPerfStats,
  type BrowserCpuFrameSample
} from "./perfDebug.js";
import type { RustBrowserGameDebugSnapshot } from "../engine/web/browserGameTypes.js";

describe("perf debug helpers", () => {
  it("summarizes browser frame history in chronological order", () => {
    const tracker = new BrowserPerfTracker(2);
    tracker.record(sample(4));
    tracker.record(sample(8));
    tracker.record(sample(16));

    const summary = tracker.summary();

    equal(summary.sampleCount, 2);
    equal(summary.latest?.totalFrameMs, 16);
    equal(summary.browserCpu.totalFrameMs.min, 8);
    equal(summary.browserCpu.totalFrameMs.max, 16);
    equal(summary.browserCpu.totalFrameMs.average, 12);
  });

  it("combines browser and Rust-owned perf stats without changing their shape", () => {
    const snapshot = fakeSnapshot();
    const browser = new BrowserPerfTracker(4);
    browser.record(sample(10));

    const stats = buildPerfStats(browser.summary(), snapshot);

    equal(stats.browserCpu.sampleCount, 1);
    equal(stats.rustCpu.totalFrameMs.latest, 12);
    equal(stats.rendererCounters.frameVisibleDrawCount.latest, 3);
    equal(stats.gpu.timerStatus.available, false);
    deepEqual(stats.renderDebugOptions, snapshot.renderDebugOptions);
  });

  it("dumps stable console tables and returns the same stats object", () => {
    const stats = buildPerfStats(new BrowserPerfTracker(1).summary(), fakeSnapshot());
    const logs: unknown[] = [];
    const tables: unknown[] = [];

    const returned = dumpPerfStats(stats, {
      log: (...values: unknown[]) => logs.push(values),
      table: (value: unknown) => tables.push(value)
    });

    equal(returned, stats);
    ok(logs.length >= 1);
    equal(tables.length, 6);
  });

  it("formats live overlay lines with timing, LOD, cascade, and debug state", () => {
    const stats = buildPerfStats(new BrowserPerfTracker(1).summary(), fakeSnapshot());

    const lines = buildPerfOverlayLines(stats);

    ok(lines.some((line) => line.includes("Frame br")));
    ok(lines.some((line) => line.includes("LOD 0:d2/v100/t60 1:d1/v40/t20")));
    ok(lines.some((line) => line.includes("Casc 0:on/d3/c1/v120")));
    ok(lines.some((line) => line.includes("Post view=final tone=on exp=1")));
    ok(lines.some((line) => line.includes("Debug lod=11111111 sky=on cloud=on")));
  });
});

function sample(totalFrameMs: number): BrowserCpuFrameSample {
  return {
    totalFrameMs,
    inputAndFrameBuildMs: totalFrameMs * 0.1,
    gameTickMs: totalFrameMs * 0.2,
    debugSnapshotMs: totalFrameMs * 0.3,
    hudUpdateMs: totalFrameMs * 0.4
  };
}

function fakeSnapshot(): RustBrowserGameDebugSnapshot {
  const numeric = {
    latest: 12,
    min: 4,
    max: 16,
    average: 10,
    p95: 16
  };
  const rendererCounters = {
    frameCandidateCount: numeric,
    frameVisibleDrawCount: { ...numeric, latest: 3 },
    frameCulledCount: numeric,
    frameShadowDrawCount: numeric,
    terrainDrawCount: numeric,
    modelDrawCount: numeric,
    skyDrawCount: numeric,
    postProcessDrawCount: numeric,
    submittedVertexCount: numeric,
    submittedIndexCount: numeric,
    submittedTriangleCount: numeric
  };
  const terrainLodCounters = [
    { lod: 0, drawCount: 2, vertexCount: 100, indexCount: 180, triangleCount: 60 },
    { lod: 1, drawCount: 1, vertexCount: 40, indexCount: 60, triangleCount: 20 }
  ];
  const shadowCascadeCounters = [
    {
      cascadeIndex: 0,
      enabled: true,
      candidateCount: 4,
      visibleCount: 3,
      culledCount: 1,
      drawCount: 3,
      vertexCount: 120,
      indexCount: 180,
      triangleCount: 60
    },
    {
      cascadeIndex: 1,
      enabled: false,
      candidateCount: 4,
      visibleCount: 0,
      culledCount: 4,
      drawCount: 0,
      vertexCount: 0,
      indexCount: 0,
      triangleCount: 0
    }
  ];
  const renderCounterSample = {
    frameCandidateCount: 4,
    frameVisibleDrawCount: 3,
    frameCulledCount: 1,
    frameShadowDrawCount: 12,
    terrainDrawCount: 2,
    modelDrawCount: 1,
    skyDrawCount: 1,
    postProcessDrawCount: 2,
    submittedVertexCount: 100,
    submittedIndexCount: 180,
    submittedTriangleCount: 60,
    terrainLodCounters,
    shadowCascadeCounters
  };

  return {
    playerMode: "firstPerson",
    playerPosition: { x: 0, y: 0, z: 0 },
    loadedTerrainChunkKeys: [],
    loadedTerrainNodeKeys: [],
    terrainChunkKeys: [],
    terrainNodeKeys: [],
    terrainPreset: "rollingHills",
    terrainSeed: 1,
    terrainStreamStatus: {
      generation: 1,
      pending: false,
      loadedChunkCount: 0,
      densityReadyChunkCount: 0,
      sharedDensityChunkCount: 0,
      inFlightDensityCount: 0,
      missingDensityCount: 0,
      desiredRenderChunkCount: 0,
      renderedChunkCount: 0,
      emptyChunkCount: 0,
      inFlightChunkCount: 0,
      missingChunkCount: 0,
      loadedNodeCount: 0,
      desiredRenderNodeCount: 0,
      renderedNodeCount: 0,
      emptyNodeCount: 0,
      missingNodeCount: 0,
      maxRenderedLod: 0,
      visibleWorldSpanXMeters: 0,
      visibleWorldSpanZMeters: 0,
      terrainLodSummary: [],
      maxConcurrentChunkJobs: 0,
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
    rendererStatus: {
      version: 1,
      runtime: "rust-wgpu",
      configured: true,
      canvasWidth: 1,
      canvasHeight: 1,
      maxTextureArrayLayers: 16,
      requiredTextureArrayLayers: 16,
      meshCount: 1,
      textureCount: 1,
      objectCount: 1,
      frameIndex: 1,
      frameDrawCount: 1,
      frameVisibleDrawCount: 1,
      frameShadowDrawCount: 1,
      frameCulledDrawCount: 0,
      frameSubmittedVertexCount: 3,
      frameSubmittedIndexCount: 3,
      frameSubmittedTriangleCount: 1,
      terrainUpdateTotalMs: 0,
      terrainCompletionIngestMs: 0,
      terrainWorkerRequestDrainMs: 0,
      terrainStreamTickMs: 0,
      terrainStreamSyncMs: 0,
      terrainStreamSchedulerMs: 0,
      terrainStreamWorkerQueueMs: 0,
      terrainStreamVisibilityMs: 0,
      terrainStreamVisibilitySelectMs: 0,
      terrainStreamVisibilityStatusMs: 0,
      terrainStreamVisibilityApplyMs: 0,
      terrainMeshDestroyMs: 0,
      terrainMeshUploadMs: 0,
      terrainCompletionCount: 0,
      terrainCompletionAcceptedCount: 0,
      terrainCompletionVertexFloatCount: 0,
      terrainCompletionIndexCount: 0,
      terrainWorkerRequestCount: 0,
      terrainUpdateUpsertedMeshCount: 0,
      terrainUpdateRemovedMeshCount: 0,
      terrainUpdateUploadedVertexFloatCount: 0,
      terrainUpdateUploadedIndexCount: 0,
      terrainUpdateDeferredUploadCount: 0,
      terrainUpdateDeferredRemovalCount: 0,
      terrainUpdateUploadBudgetHit: false,
      terrainUpdateRemovalBudgetHit: false,
      shadowCascadeCount: 4,
      shadowMapSize: 1024,
      shadowMaxDistanceMeters: 100,
      shadowStrength: 1,
      shadowEffectiveSunElevation: 1,
      shadowEffectiveSunDirection: { x: 0, y: 1, z: 0 },
      gpuTimerAvailable: false,
      gpuTimerUnavailableReason: "test",
      gpuTimestampPeriodNs: 0,
      gpuTimerPendingReadbackCount: 0,
      renderDebugOptions: renderDebugOptions(),
      lastRenderCounters: renderCounterSample,
      lastGpuPassTimings: {
        shadowCascadeMs: [null, null, null, null],
        sceneMs: null,
        bloomMs: null,
        postProcessMs: null,
        totalMeasuredMs: null
      },
      postProcessRuntime: "rust-wgpu",
      postProcessDebugView: "final",
      postProcessExposure: 1,
      postProcessToneMappingEnabled: true,
      postProcessBloomEnabled: true,
      postProcessBloomThreshold: 1,
      postProcessBloomIntensity: 0.08,
      postProcessDofEnabled: false,
      postProcessDofFocusDistance: 30,
      postProcessDofFocusRange: 8,
      postProcessDofMaxBlurPixels: 6
    },
    rustPerfStats: {
      sampleCount: 1,
      capacity: 600,
      gpuTimerStatus: {
        available: false,
        unavailableReason: "test",
        timestampPeriodNs: 0,
        pendingReadbackCount: 0
      },
      rustCpu: {
        totalFrameMs: numeric,
        inputParseMs: numeric,
        gameStateTickMs: numeric,
        playerCharacterUpdateMs: numeric,
        terrainCompletionIngestMs: numeric,
        terrainStreamUpdateMs: numeric,
        terrainStreamTickMs: numeric,
        terrainStreamSyncMs: numeric,
        terrainStreamSchedulerMs: numeric,
        terrainStreamWorkerQueueMs: numeric,
        terrainStreamVisibilityMs: numeric,
        terrainStreamVisibilitySelectMs: numeric,
        terrainStreamVisibilityStatusMs: numeric,
        terrainStreamVisibilityApplyMs: numeric,
        terrainMeshDestroyMs: numeric,
        terrainMeshUploadMs: numeric,
        renderFrameMs: numeric,
        renderPacketBuildMs: numeric,
        rendererPrepareMs: numeric,
        rendererShadowCpuMs: numeric,
        rendererSceneCpuMs: numeric,
        rendererPostCpuMs: numeric,
        rendererSubmitMs: numeric
      },
      rendererCounters,
      gpu: {
        shadowCascadeMs: [numeric, numeric, numeric, numeric],
        sceneMs: numeric,
        bloomMs: numeric,
        postProcessMs: numeric,
        totalMeasuredMs: numeric
      },
      latest: {
        frameIndex: 1,
        rustCpu: {
          totalFrameMs: 12,
          inputParseMs: 1,
          gameStateTickMs: 2,
          playerCharacterUpdateMs: 3,
          terrainCompletionIngestMs: 0.5,
          terrainStreamUpdateMs: 4,
          terrainStreamTickMs: 1.5,
          terrainStreamSyncMs: 0.1,
          terrainStreamSchedulerMs: 0.2,
          terrainStreamWorkerQueueMs: 0.3,
          terrainStreamVisibilityMs: 0.4,
          terrainStreamVisibilitySelectMs: 0.15,
          terrainStreamVisibilityStatusMs: 0.05,
          terrainStreamVisibilityApplyMs: 0.2,
          terrainMeshDestroyMs: 0.25,
          terrainMeshUploadMs: 2.25,
          renderFrameMs: 5,
          renderPacketBuildMs: 1,
          rendererPrepareMs: 1,
          rendererShadowCpuMs: 1,
          rendererSceneCpuMs: 1,
          rendererPostCpuMs: 1,
          rendererSubmitMs: 1
        },
        rendererCounters: renderCounterSample,
        gpuPassTimings: {
          shadowCascadeMs: [null, null, null, null],
          sceneMs: null,
          bloomMs: null,
          postProcessMs: null,
          totalMeasuredMs: null
        }
      },
      terrainLodCounters,
      shadowCascadeCounters
    },
    renderDebugOptions: renderDebugOptions(),
    shadowDebugView: "off",
    terrainWorkerCount: 0,
    playerControllerRuntime: "rust"
  };
}

function renderDebugOptions() {
  return {
    terrainLodMask: 0xFFFFFFFF,
    skyEnabled: true,
    skyCloudNoiseEnabled: true,
    shadowPassEnabled: true,
    shadowCascadeMask: 0b1111,
    shadowSamplingEnabled: true,
    shadowSunMode: "production" as const,
    whiteTexturesEnabled: false,
    materialMode: "full" as const
  };
}
