import type { Vec3 } from "../math/vec3.js";
import type { TerrainChunkKey } from "../world/terrainChunk.js";
import type { TerrainPresetId } from "../world/terrainDescriptor.js";
import type { EngineWebRendererStatus } from "./engineWebWasm.js";
import type { TerrainVariantFlatValues } from "./terrainWorkerClient.js";

export type { TerrainVariantFlatValues } from "./terrainWorkerClient.js";

export type PlayerMode = "firstPerson" | "thirdPerson" | "debugFly";

export type PlayerCharacterId = "male" | "female";

export type ShadowDebugView =
  | "off"
  | "cascadeIndex"
  | "shadowVisibility"
  | "shadowDepthCascade0"
  | "shadowDepthCascade1"
  | "shadowDepthCascade2"
  | "shadowDepthCascade3";

export type PostProcessDebugView =
  | "final"
  | "sceneColor"
  | "linearDepth"
  | "postToneMap"
  | "bloom"
  | "dofCoc"
  | "dofBlurred"
  | "fogFactor";

export type PostProcessFogSettings = {
  readonly enabled: boolean;
  readonly startDistance: number;
  readonly endDistance: number;
  readonly density: number;
  readonly colorR: number;
  readonly colorG: number;
  readonly colorB: number;
  readonly curve: number;
};

export type WaterDebugView = "final" | "bottomDepth" | "pathLength" | "fresnel" | "reflection";

export type RenderMaterialDebugMode = "full" | "lambert" | "lodColor";

export type ShadowSunMode = "production" | "overhead" | "angled" | "low";

export type RenderDebugOptions = {
  readonly terrainLodMask: number;
  readonly skyEnabled: boolean;
  readonly skyCloudNoiseEnabled: boolean;
  readonly shadowPassEnabled: boolean;
  readonly shadowCascadeMask: number;
  readonly shadowSamplingEnabled: boolean;
  readonly shadowSunMode: ShadowSunMode;
  readonly whiteTexturesEnabled: boolean;
  readonly materialMode: RenderMaterialDebugMode;
};

export type RenderDebugOptionsUpdate = Partial<RenderDebugOptions>;

export type WaterOptionsUpdate = {
  readonly enabled?: boolean;
  readonly reflectionEnabled?: boolean;
  readonly seaLevelMeters?: number;
  readonly shallowDepthMeters?: number;
  readonly deepDepthMeters?: number;
  readonly waveScale?: number;
  readonly waveStrength?: number;
};

export type PlayerAnimationTuning = {
  readonly walkSpeedMetersPerSecond: number;
  readonly runSpeedMetersPerSecond: number;
  readonly idlePlaybackScale: number;
  readonly walkPlaybackScale: number;
  readonly runPlaybackScale: number;
};

export type BrowserViewport = {
  readonly width: number;
  readonly height: number;
};

export type BrowserFrameInput = {
  readonly deltaSeconds: number;
  readonly movement: {
    readonly forward: number;
    readonly right: number;
    readonly up: number;
    readonly fast: boolean;
  };
  readonly look: {
    readonly deltaX: number;
    readonly deltaY: number;
  };
};

export type GameCommand =
  | { readonly type: "togglePlayerMode" }
  | { readonly type: "setPlayerMode"; readonly mode: PlayerMode }
  | { readonly type: "togglePlayerCharacter" }
  | { readonly type: "setPlayerCharacter"; readonly character: PlayerCharacterId }
  | ({ readonly type: "setPlayerAnimationTuning" } & PlayerAnimationTuning)
  | {
      readonly type: "setPlayerPosition";
      readonly x: number;
      readonly y?: number;
      readonly z: number;
    }
  | {
      readonly type: "setDebugCamera";
      readonly x: number;
      readonly y: number;
      readonly z: number;
      readonly yaw: number;
      readonly pitch: number;
    }
  | { readonly type: "setShadowDebugView"; readonly view: ShadowDebugView }
  | { readonly type: "setPostProcessDebugView"; readonly view: PostProcessDebugView }
  | {
      readonly type: "setPostProcessToneMapping";
      readonly enabled: boolean;
      readonly exposure: number;
    }
  | {
      readonly type: "setPostProcessBloom";
      readonly enabled: boolean;
      readonly threshold: number;
      readonly intensity: number;
    }
  | {
      readonly type: "setPostProcessDepthOfField";
      readonly enabled: boolean;
      readonly focusDistance: number;
      readonly focusRange: number;
      readonly maxBlurPixels: number;
    }
  | ({ readonly type: "setPostProcessFog" } & PostProcessFogSettings)
  | { readonly type: "setWaterDebugView"; readonly view: WaterDebugView }
  | ({ readonly type: "setWaterOptions" } & WaterOptionsUpdate)
  | ({ readonly type: "setRenderDebugOptions" } & RenderDebugOptionsUpdate)
  | { readonly type: "resetRenderDebugOptions" }
  | { readonly type: "resetPerfStats" }
  | {
      readonly type: "setTerrainVariant";
      readonly terrainSeed: number;
      readonly terrainPreset: number;
      readonly terrainVariant: TerrainVariantFlatValues;
    }
  | { readonly type: "resetStreaming" };

export type RustBrowserGameResetCommand = {
  readonly type: "resetGame";
  readonly terrainSeed: number;
  readonly terrainPreset: number;
  readonly terrainVariant?: TerrainVariantFlatValues;
};

export type RustBrowserGameCommand = RustBrowserGameResetCommand | GameCommand;

export type TerrainStreamJobStats = {
  readonly totalMs: number;
  readonly vertexCount?: number;
  readonly indexCount?: number;
};

export type TerrainWorkerPoolRuntime = "rust-sync" | "browser-worker";

export type TerrainNodeKey = string;

export type TerrainPresetCatalogEntry = {
  readonly code: number;
  readonly id: TerrainPresetId;
  readonly name: string;
  readonly terrainVariant: TerrainVariantFlatValues;
};

export type TerrainBiomeWeightsProbe = {
  readonly grassland: number;
  readonly temperateForest: number;
  readonly wetland: number;
  readonly coastBeach: number;
  readonly dryBadland: number;
  readonly alpineMeadow: number;
  readonly highMountainRock: number;
  readonly snowTundra: number;
};

export type TerrainVariantProbeSummary = {
  readonly sampleCount: number;
  readonly heightMin: number;
  readonly heightMax: number;
  readonly slopeMin: number;
  readonly slopeMax: number;
  readonly macroBaseElevation: number;
  readonly mountainness: number;
  readonly ridge: number;
  readonly cellularEdge: number;
  readonly materialIndices: readonly number[];
  readonly materialWeights: readonly number[];
  readonly biomeWeights: TerrainBiomeWeightsProbe;
};

export type TerrainLodSummary = {
  readonly lod: number;
  readonly desiredNodeCount: number;
  readonly minDesiredNodeY: number | null;
  readonly maxDesiredNodeY: number | null;
  readonly densityReadyNodeCount: number;
  readonly renderedNodeCount: number;
  readonly emptyNodeCount: number;
  readonly missingNodeCount: number;
};

export type TerrainStreamStatus = {
  readonly generation: number;
  readonly pending: boolean;
  readonly loadedChunkCount: number;
  readonly densityReadyChunkCount: number;
  readonly sharedDensityChunkCount: number;
  readonly inFlightDensityCount: number;
  readonly missingDensityCount: number;
  readonly desiredRenderChunkCount: number;
  readonly renderedChunkCount: number;
  readonly emptyChunkCount: number;
  readonly inFlightChunkCount: number;
  readonly missingChunkCount: number;
  readonly loadedNodeCount: number;
  readonly desiredRenderNodeCount: number;
  readonly renderedNodeCount: number;
  readonly emptyNodeCount: number;
  readonly missingNodeCount: number;
  readonly maxRenderedLod: number;
  readonly visibleWorldSpanXMeters: number;
  readonly visibleWorldSpanZMeters: number;
  readonly terrainLodSummary: TerrainLodSummary[];
  readonly placementCandidateCount: number;
  readonly placementSampleCount: number;
  readonly placementMissedSurfaceCount: number;
  readonly placementRejectedBelowWaterCount: number;
  readonly placementRejectedSlopeCount: number;
  readonly transitionFaceCount: number;
  readonly transitionMeshCount: number;
  readonly transitionVertexFloatCount: number;
  readonly transitionIndexCount: number;
  readonly maxConcurrentChunkJobs: number;
  readonly workerPoolRuntime: TerrainWorkerPoolRuntime;
  readonly terrainWorkerCount: number;
  readonly terrainWorkerInFlightCount: number;
  readonly terrainWorkerQueuedRequestCount: number;
  readonly terrainWorkerCompletedCount: number;
  readonly terrainWorkerStaleCompletionCount: number;
  readonly terrainWorkerFailedCount: number;
  readonly synchronousBuildCount: number;
  readonly lastDensityJobStats?: TerrainStreamJobStats;
  readonly lastChunkJobStats?: TerrainStreamJobStats;
};

export type BrowserTerrainFrameDiagnostics = {
  readonly completionBudget: number;
  readonly pendingCompletionCountBefore: number;
  readonly pendingCompletionCountAfter: number;
  readonly drainedCompletionCount: number;
  readonly drainedCompletionVertexBytes: number;
  readonly drainedCompletionIndexBytes: number;
  readonly submittedRequestCount: number;
  readonly workerInFlightRequestCount: number;
  readonly takeCompletionsMs: number;
  readonly completeTerrainBuildsMs: number;
  readonly gameTickMs: number;
  readonly takeRequestsMs: number;
  readonly submitRequestsMs: number;
};

export type NumericPerfSummary = {
  readonly latest: number;
  readonly min: number;
  readonly max: number;
  readonly average: number;
  readonly p95: number;
};

export type RustCpuPerfSummary = {
  readonly totalFrameMs: NumericPerfSummary;
  readonly inputParseMs: NumericPerfSummary;
  readonly gameStateTickMs: NumericPerfSummary;
  readonly playerCharacterUpdateMs: NumericPerfSummary;
  readonly terrainCompletionIngestMs: NumericPerfSummary;
  readonly terrainStreamUpdateMs: NumericPerfSummary;
  readonly terrainStreamTickMs: NumericPerfSummary;
  readonly terrainStreamSyncMs: NumericPerfSummary;
  readonly terrainStreamSchedulerMs: NumericPerfSummary;
  readonly terrainStreamWorkerQueueMs: NumericPerfSummary;
  readonly terrainStreamVisibilityMs: NumericPerfSummary;
  readonly terrainStreamVisibilitySelectMs: NumericPerfSummary;
  readonly terrainStreamVisibilityStatusMs: NumericPerfSummary;
  readonly terrainStreamVisibilityApplyMs: NumericPerfSummary;
  readonly terrainMeshDestroyMs: NumericPerfSummary;
  readonly terrainMeshUploadMs: NumericPerfSummary;
  readonly renderFrameMs: NumericPerfSummary;
  readonly renderPacketBuildMs: NumericPerfSummary;
  readonly rendererPrepareMs: NumericPerfSummary;
  readonly rendererShadowCpuMs: NumericPerfSummary;
  readonly rendererSceneCpuMs: NumericPerfSummary;
  readonly rendererPostCpuMs: NumericPerfSummary;
  readonly rendererSubmitMs: NumericPerfSummary;
};

export type RustCpuPerfSample = {
  readonly totalFrameMs: number;
  readonly inputParseMs: number;
  readonly gameStateTickMs: number;
  readonly playerCharacterUpdateMs: number;
  readonly terrainCompletionIngestMs: number;
  readonly terrainStreamUpdateMs: number;
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
  readonly renderFrameMs: number;
  readonly renderPacketBuildMs: number;
  readonly rendererPrepareMs: number;
  readonly rendererShadowCpuMs: number;
  readonly rendererSceneCpuMs: number;
  readonly rendererPostCpuMs: number;
  readonly rendererSubmitMs: number;
};

export type TerrainLodCounter = {
  readonly lod: number;
  readonly drawCount: number;
  readonly vertexCount: number;
  readonly indexCount: number;
  readonly triangleCount: number;
};

export type ShadowCascadeCounter = {
  readonly cascadeIndex: number;
  readonly enabled: boolean;
  readonly candidateCount: number;
  readonly visibleCount: number;
  readonly culledCount: number;
  readonly drawCount: number;
  readonly vertexCount: number;
  readonly indexCount: number;
  readonly triangleCount: number;
};

export type RenderCounterSummary = {
  readonly frameCandidateCount: NumericPerfSummary;
  readonly frameVisibleDrawCount: NumericPerfSummary;
  readonly frameCulledCount: NumericPerfSummary;
  readonly frameShadowDrawCount: NumericPerfSummary;
  readonly terrainDrawCount: NumericPerfSummary;
  readonly modelDrawCount: NumericPerfSummary;
  readonly skyDrawCount: NumericPerfSummary;
  readonly postProcessDrawCount: NumericPerfSummary;
  readonly submittedVertexCount: NumericPerfSummary;
  readonly submittedIndexCount: NumericPerfSummary;
  readonly submittedTriangleCount: NumericPerfSummary;
};

export type RenderCounterSample = {
  readonly frameCandidateCount: number;
  readonly frameVisibleDrawCount: number;
  readonly frameCulledCount: number;
  readonly frameShadowDrawCount: number;
  readonly terrainDrawCount: number;
  readonly modelDrawCount: number;
  readonly skyDrawCount: number;
  readonly postProcessDrawCount: number;
  readonly submittedVertexCount: number;
  readonly submittedIndexCount: number;
  readonly submittedTriangleCount: number;
  readonly terrainLodCounters: TerrainLodCounter[];
  readonly shadowCascadeCounters: ShadowCascadeCounter[];
};

export type GpuTimerStatus = {
  readonly available: boolean;
  readonly unavailableReason: string;
  readonly timestampPeriodNs: number;
  readonly pendingReadbackCount: number;
};

export type GpuPassTimingSummary = {
  readonly shadowCascadeMs: NumericPerfSummary[];
  readonly sceneMs: NumericPerfSummary;
  readonly bloomMs: NumericPerfSummary;
  readonly postProcessMs: NumericPerfSummary;
  readonly totalMeasuredMs: NumericPerfSummary;
};

export type GpuPassTimingSample = {
  readonly shadowCascadeMs: Array<number | null>;
  readonly sceneMs: number | null;
  readonly bloomMs: number | null;
  readonly postProcessMs: number | null;
  readonly totalMeasuredMs: number | null;
};

export type RustPerfStats = {
  readonly sampleCount: number;
  readonly capacity: number;
  readonly gpuTimerStatus: GpuTimerStatus;
  readonly rustCpu: RustCpuPerfSummary;
  readonly rendererCounters: RenderCounterSummary;
  readonly gpu: GpuPassTimingSummary;
  readonly latest?: {
    readonly frameIndex: number;
    readonly rustCpu: RustCpuPerfSample;
    readonly rendererCounters: RenderCounterSample;
    readonly gpuPassTimings: GpuPassTimingSample;
  };
  readonly terrainLodCounters: TerrainLodCounter[];
  readonly shadowCascadeCounters: ShadowCascadeCounter[];
};

export type RustBrowserGameDebugSnapshot = {
  readonly playerMode: PlayerMode;
  readonly playerPosition: Vec3;
  readonly loadedTerrainChunkKeys: TerrainChunkKey[];
  readonly loadedTerrainNodeKeys: TerrainNodeKey[];
  readonly terrainChunkKeys: TerrainChunkKey[];
  readonly terrainNodeKeys: TerrainNodeKey[];
  readonly terrainPreset: TerrainPresetId;
  readonly terrainSeed: number;
  readonly terrainVariantRevision: number;
  readonly terrainVariant: TerrainVariantFlatValues;
  readonly terrainPresetCatalog: TerrainPresetCatalogEntry[];
  readonly terrainVariantProbe: TerrainVariantProbeSummary;
  readonly terrainStreamStatus: TerrainStreamStatus;
  readonly terrainStreamerRuntime: "rust";
  readonly terrainStreamSchedulerRuntime: "rust";
  readonly terrainDensityStoreRuntime: "rust";
  readonly terrainWorkerPoolRuntime: TerrainWorkerPoolRuntime;
  readonly renderPacketRuntime: "rust";
  readonly terrainRenderPacketRuntime: "rust";
  readonly rendererRuntime: "rust-wgpu";
  readonly rendererStatus: EngineWebRendererStatus;
  readonly rustPerfStats: RustPerfStats;
  readonly renderDebugOptions: RenderDebugOptions;
  readonly shadowDebugView: ShadowDebugView;
  readonly skyRuntime?: "rust";
  readonly skyDayPhase?: number;
  readonly skySunElevation?: number;
  readonly skyCloudCoverage?: number;
  readonly skyStarIntensity?: number;
  readonly terrainWorkerCount: number;
  readonly browserTerrainFrame?: BrowserTerrainFrameDiagnostics;
  readonly playerControllerRuntime: "rust";
  readonly playerCharacterId?: PlayerCharacterId;
  readonly playerCharacterLabel?: string;
  readonly playerCharacterRuntime?: "rust";
  readonly playerCharacterVisible?: boolean;
  readonly playerCharacterFollowsPlayer?: boolean;
  readonly debugPlayerMarkerVisible?: boolean;
  readonly modelPrimitiveCount?: number;
  readonly modelMaterialCount?: number;
  readonly modelTextureCount?: number;
  readonly modelNonFallbackAlbedoPartCount?: number;
  readonly modelAnimationRuntime?: "rust";
  readonly activeModelAnimationClip?: string;
  readonly nextModelAnimationClip?: string;
  readonly modelAnimationTimeSeconds?: number;
  readonly modelAnimationDurationSeconds?: number;
  readonly modelAnimationBlendWeight?: number;
  readonly modelAnimationWalkRunBlendWeight?: number;
  readonly modelAnimationPlaybackScale?: number;
  readonly modelAnimationLocomotionSpeedMetersPerSecond?: number;
  readonly modelAnimationWalkSpeedMetersPerSecond?: number;
  readonly modelAnimationRunSpeedMetersPerSecond?: number;
  readonly modelAnimationIdlePlaybackScale?: number;
  readonly modelAnimationWalkPlaybackScale?: number;
  readonly modelAnimationRunPlaybackScale?: number;
  readonly modelSkinningRuntime?: "rust-cpu";
  readonly modelSkinningJointCount?: number;
};

export type GameDebugSnapshot = RustBrowserGameDebugSnapshot;

export type TransformSnapshot = {
  readonly position: Vec3;
  readonly yaw: number;
  readonly pitch: number;
};
