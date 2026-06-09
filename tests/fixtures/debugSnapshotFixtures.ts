// Shared debug snapshot fragments for tests that exercise the browser/Rust facade contract.
// These values intentionally look like a real rendered frame so tests can assert the
// full perf/debug status shape without copying dozens of fields per test.
import type {
  GpuPassTimingSample,
  NumericPerfSummary,
  RenderCounterSample,
  RenderCounterSummary,
  RenderDebugOptions,
  RustCpuPerfSample,
  RustCpuPerfSummary,
  RustPerfStats
} from "../../src/engine/web/browserGameTypes.js";
import type { EngineWebRendererStatus } from "../../src/engine/web/engineWebWasm.js";

export function fakeRenderDebugOptions(
  overrides: Partial<RenderDebugOptions> = {}
): RenderDebugOptions {
  return {
    terrainLodMask: 0xFFFFFFFF,
    skyEnabled: true,
    skyCloudNoiseEnabled: true,
    shadowPassEnabled: true,
    shadowCascadeMask: 0b1111,
    shadowSamplingEnabled: true,
    shadowSunMode: "production",
    whiteTexturesEnabled: false,
    materialMode: "full",
    ...overrides
  };
}

export function fakeNumericPerfSummary(latest = 0): NumericPerfSummary {
  return {
    latest,
    min: latest,
    max: latest,
    average: latest,
    p95: latest
  };
}

export function fakeRenderCounterSample(
  overrides: Partial<RenderCounterSample> = {}
): RenderCounterSample {
  return {
    frameCandidateCount: 1,
    frameVisibleDrawCount: 1,
    frameCulledCount: 0,
    frameShadowDrawCount: 1,
    terrainDrawCount: 1,
    modelDrawCount: 0,
    skyDrawCount: 1,
    postProcessDrawCount: 2,
    submittedVertexCount: 3,
    submittedIndexCount: 3,
    submittedTriangleCount: 1,
    terrainLodCounters: [
      {
        lod: 0,
        drawCount: 1,
        vertexCount: 3,
        indexCount: 3,
        triangleCount: 1
      }
    ],
    shadowCascadeCounters: [0, 1, 2, 3].map((cascadeIndex) => ({
      cascadeIndex,
      enabled: true,
      candidateCount: 1,
      visibleCount: cascadeIndex === 0 ? 1 : 0,
      culledCount: cascadeIndex === 0 ? 0 : 1,
      drawCount: cascadeIndex === 0 ? 1 : 0,
      vertexCount: cascadeIndex === 0 ? 3 : 0,
      indexCount: cascadeIndex === 0 ? 3 : 0,
      triangleCount: cascadeIndex === 0 ? 1 : 0
    })),
    ...overrides
  };
}

export function fakeGpuPassTimings(): GpuPassTimingSample {
  return {
    shadowCascadeMs: [null, null, null, null],
    sceneMs: null,
    bloomMs: null,
    postProcessMs: null,
    totalMeasuredMs: null
  };
}

export function fakeRendererStatus(
  overrides: Partial<EngineWebRendererStatus> = {}
): EngineWebRendererStatus {
  const counters = fakeRenderCounterSample();
  return {
    version: 1,
    runtime: "rust-wgpu",
    configured: true,
    canvasWidth: 640,
    canvasHeight: 480,
    maxTextureArrayLayers: 16,
    requiredTextureArrayLayers: 16,
    meshCount: 1,
    textureCount: 3,
    objectCount: 1,
    frameIndex: 1,
    frameDrawCount: counters.frameVisibleDrawCount,
    frameVisibleDrawCount: counters.frameVisibleDrawCount,
    frameShadowDrawCount: counters.frameShadowDrawCount,
    frameCulledDrawCount: counters.frameCulledCount,
    frameSubmittedVertexCount: counters.submittedVertexCount,
    frameSubmittedIndexCount: counters.submittedIndexCount,
    frameSubmittedTriangleCount: counters.submittedTriangleCount,
    terrainUpdateTotalMs: 0,
    terrainUpdateUpsertedMeshCount: 0,
    terrainUpdateRemovedMeshCount: 0,
    terrainUpdateUploadedVertexFloatCount: 0,
    terrainUpdateUploadedIndexCount: 0,
    shadowCascadeCount: 4,
    shadowMapSize: 1024,
    shadowMaxDistanceMeters: 100,
    shadowStrength: 1,
    shadowEffectiveSunElevation: 1,
    shadowEffectiveSunDirection: { x: 0, y: 1, z: 0 },
    gpuTimerAvailable: false,
    gpuTimerUnavailableReason: "adapter does not expose TIMESTAMP_QUERY",
    gpuTimestampPeriodNs: 0,
    gpuTimerPendingReadbackCount: 0,
    renderDebugOptions: fakeRenderDebugOptions(),
    lastRenderCounters: counters,
    lastGpuPassTimings: fakeGpuPassTimings(),
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
    postProcessDofMaxBlurPixels: 6,
    ...overrides
  };
}

export function fakeRustPerfStats(overrides: Partial<RustPerfStats> = {}): RustPerfStats {
  const zero = fakeNumericPerfSummary(0);
  const counters = fakeRenderCounterSample();
  const cpuSample = fakeRustCpuPerfSample();
  return {
    sampleCount: 1,
    capacity: 600,
    gpuTimerStatus: {
      available: false,
      unavailableReason: "adapter does not expose TIMESTAMP_QUERY",
      timestampPeriodNs: 0,
      pendingReadbackCount: 0
    },
    rustCpu: fakeRustCpuPerfSummary(zero),
    rendererCounters: fakeRenderCounterSummary(zero),
    gpu: {
      shadowCascadeMs: [zero, zero, zero, zero],
      sceneMs: zero,
      bloomMs: zero,
      postProcessMs: zero,
      totalMeasuredMs: zero
    },
    latest: {
      frameIndex: 1,
      rustCpu: cpuSample,
      rendererCounters: counters,
      gpuPassTimings: fakeGpuPassTimings()
    },
    terrainLodCounters: counters.terrainLodCounters,
    shadowCascadeCounters: counters.shadowCascadeCounters,
    ...overrides
  };
}

function fakeRustCpuPerfSample(): RustCpuPerfSample {
  return {
    totalFrameMs: 0,
    inputParseMs: 0,
    gameStateTickMs: 0,
    playerCharacterUpdateMs: 0,
    terrainStreamUpdateMs: 0,
    renderFrameMs: 0,
    renderPacketBuildMs: 0,
    rendererPrepareMs: 0,
    rendererShadowCpuMs: 0,
    rendererSceneCpuMs: 0,
    rendererPostCpuMs: 0,
    rendererSubmitMs: 0
  };
}

function fakeRustCpuPerfSummary(summary: NumericPerfSummary): RustCpuPerfSummary {
  return {
    totalFrameMs: summary,
    inputParseMs: summary,
    gameStateTickMs: summary,
    playerCharacterUpdateMs: summary,
    terrainStreamUpdateMs: summary,
    renderFrameMs: summary,
    renderPacketBuildMs: summary,
    rendererPrepareMs: summary,
    rendererShadowCpuMs: summary,
    rendererSceneCpuMs: summary,
    rendererPostCpuMs: summary,
    rendererSubmitMs: summary
  };
}

function fakeRenderCounterSummary(summary: NumericPerfSummary): RenderCounterSummary {
  return {
    frameCandidateCount: summary,
    frameVisibleDrawCount: summary,
    frameCulledCount: summary,
    frameShadowDrawCount: summary,
    terrainDrawCount: summary,
    modelDrawCount: summary,
    skyDrawCount: summary,
    postProcessDrawCount: summary,
    submittedVertexCount: summary,
    submittedIndexCount: summary,
    submittedTriangleCount: summary
  };
}
