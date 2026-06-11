import { vec3, type Vec3 } from "../math/vec3.js";
import {
  createEngineWebBrowserGame,
  type EngineWebBrowserGame
} from "./engineWebWasm.js";
import type { BrowserTextureAssetLoader } from "../browser/textureAssetLoader.js";
import type {
  BrowserTerrainFrameDiagnostics,
  BrowserFrameInput,
  PlayerCharacterId,
  PlayerMode,
  RustBrowserGameCommand,
  RustBrowserGameDebugSnapshot,
  ShadowDebugView
} from "./browserGameTypes.js";
import { TerrainWorkerClient, type TerrainBuildCompletion } from "./terrainWorkerClient.js";

const TERRAIN_COMPLETION_BUDGET_PER_FRAME = 6;

export type TerrainWorkerBridge = {
  readonly workerCount: number;
  takeCompletions(maxCount?: number): TerrainBuildCompletion[];
  submitRequests(requests: ReturnType<EngineWebBrowserGame["takeTerrainBuildRequests"]>): void;
  status?(): {
    readonly pendingCompletionCount: number;
    readonly inFlightRequestCount: number;
  };
  reset(): void;
};

export class RustBrowserGameAdapter {
  readonly runtime = "rust-wgpu" as const;
  private width = 1;
  private height = 1;
  private lastTerrainFrameDiagnostics = defaultBrowserTerrainFrameDiagnostics();

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly game: EngineWebBrowserGame,
    private readonly terrainWorkers?: TerrainWorkerBridge
  ) {}

  static async create(
    canvas: HTMLCanvasElement,
    assetLoader?: BrowserTextureAssetLoader
  ): Promise<RustBrowserGameAdapter> {
    const game = await createEngineWebBrowserGame(canvas, assetLoader);
    const terrainWorkers = new TerrainWorkerClient();
    const adapter = new RustBrowserGameAdapter(canvas, game, terrainWorkers);
    try {
      game.configureTerrainWorkers({ workerCount: terrainWorkers.workerCount });
      adapter.resize();
    } catch (error) {
      terrainWorkers.dispose();
      throw error;
    }

    return adapter;
  }

  resize(): void {
    const { width, height } = this.computeDisplaySize();

    if (width === this.width && height === this.height) {
      return;
    }

    this.width = width;
    this.height = height;
    this.canvas.width = width;
    this.canvas.height = height;
    this.game.resize({ width, height });
  }

  tick(frame: BrowserFrameInput): void {
    this.resize();
    const pendingCompletionCountBefore =
      this.terrainWorkers?.status?.().pendingCompletionCount ?? 0;
    const takeCompletionsStartedAt = performance.now();
    const completions = this.terrainWorkers?.takeCompletions(
      TERRAIN_COMPLETION_BUDGET_PER_FRAME
    ) ?? [];
    const takeCompletionsMs = performance.now() - takeCompletionsStartedAt;
    const completeTerrainBuildsStartedAt = performance.now();
    this.game.completeTerrainBuilds(completions);
    const completeTerrainBuildsMs = performance.now() - completeTerrainBuildsStartedAt;
    const gameTickStartedAt = performance.now();
    this.game.tick(frame);
    const gameTickMs = performance.now() - gameTickStartedAt;
    const takeRequestsStartedAt = performance.now();
    const requests = this.game.takeTerrainBuildRequests();
    const takeRequestsMs = performance.now() - takeRequestsStartedAt;
    const submitRequestsStartedAt = performance.now();
    this.terrainWorkers?.submitRequests(requests);
    const submitRequestsMs = performance.now() - submitRequestsStartedAt;
    const workerStatus = this.terrainWorkers?.status?.();
    this.lastTerrainFrameDiagnostics = {
      completionBudget: TERRAIN_COMPLETION_BUDGET_PER_FRAME,
      pendingCompletionCountBefore,
      pendingCompletionCountAfter: workerStatus?.pendingCompletionCount ?? 0,
      drainedCompletionCount: completions.length,
      drainedCompletionVertexBytes: completionVertexBytes(completions),
      drainedCompletionIndexBytes: completionIndexBytes(completions),
      submittedRequestCount: requests.length,
      workerInFlightRequestCount: workerStatus?.inFlightRequestCount ?? 0,
      takeCompletionsMs,
      completeTerrainBuildsMs,
      gameTickMs,
      takeRequestsMs,
      submitRequestsMs
    };
  }

  command(command: RustBrowserGameCommand): void {
    if (
      command.type === "resetGame" ||
      command.type === "setTerrainVariant" ||
      command.type === "resetStreaming"
    ) {
      this.terrainWorkers?.reset();
    }
    this.game.command(command);
  }

  getDebugSnapshot(): RustBrowserGameDebugSnapshot {
    const snapshot = this.game.debugSnapshot();

    return {
      playerMode: validatePlayerMode(snapshot.playerMode),
      playerPosition: vec3(
        snapshot.playerPosition.x,
        snapshot.playerPosition.y,
        snapshot.playerPosition.z
      ),
      loadedTerrainChunkKeys: [...snapshot.loadedTerrainChunkKeys],
      loadedTerrainNodeKeys: [...snapshot.loadedTerrainNodeKeys],
      terrainChunkKeys: [...snapshot.terrainChunkKeys],
      terrainNodeKeys: [...snapshot.terrainNodeKeys],
      terrainPreset: snapshot.terrainPreset,
      terrainSeed: snapshot.terrainSeed,
      terrainVariantRevision: snapshot.terrainVariantRevision,
      terrainVariant: [...snapshot.terrainVariant],
      terrainPresetCatalog: snapshot.terrainPresetCatalog.map((entry) => ({
        code: entry.code,
        id: entry.id,
        name: entry.name,
        terrainVariant: [...entry.terrainVariant]
      })),
      terrainVariantProbe: {
        ...snapshot.terrainVariantProbe,
        materialIndices: [...snapshot.terrainVariantProbe.materialIndices],
        materialWeights: [...snapshot.terrainVariantProbe.materialWeights],
        biomeWeights: { ...snapshot.terrainVariantProbe.biomeWeights }
      },
      terrainStreamStatus: snapshot.terrainStreamStatus,
      terrainStreamerRuntime: snapshot.terrainStreamerRuntime,
      terrainStreamSchedulerRuntime: snapshot.terrainStreamSchedulerRuntime,
      terrainDensityStoreRuntime: snapshot.terrainDensityStoreRuntime,
      terrainWorkerPoolRuntime: snapshot.terrainWorkerPoolRuntime,
      renderPacketRuntime: snapshot.renderPacketRuntime,
      terrainRenderPacketRuntime: snapshot.terrainRenderPacketRuntime,
      rendererRuntime: snapshot.rendererRuntime,
      terrainWorkerCount: snapshot.terrainWorkerCount,
      browserTerrainFrame: this.lastTerrainFrameDiagnostics,
      playerControllerRuntime: snapshot.playerControllerRuntime,
      rendererStatus: snapshot.rendererStatus,
      rustPerfStats: snapshot.rustPerfStats,
      renderDebugOptions: snapshot.renderDebugOptions,
      shadowDebugView: validateShadowDebugView(snapshot.shadowDebugView),
      skyRuntime: snapshot.skyRuntime,
      skyDayPhase: snapshot.skyDayPhase,
      skySunElevation: snapshot.skySunElevation,
      skyCloudCoverage: snapshot.skyCloudCoverage,
      skyStarIntensity: snapshot.skyStarIntensity,
      playerCharacterId: snapshot.playerCharacterId === undefined
        ? undefined
        : validatePlayerCharacterId(snapshot.playerCharacterId),
      playerCharacterLabel: snapshot.playerCharacterLabel,
      playerCharacterRuntime: snapshot.playerCharacterRuntime,
      playerCharacterVisible: snapshot.playerCharacterVisible,
      playerCharacterFollowsPlayer: snapshot.playerCharacterFollowsPlayer,
      debugPlayerMarkerVisible: snapshot.debugPlayerMarkerVisible,
      modelPrimitiveCount: snapshot.modelPrimitiveCount,
      modelMaterialCount: snapshot.modelMaterialCount,
      modelTextureCount: snapshot.modelTextureCount,
      modelNonFallbackAlbedoPartCount: snapshot.modelNonFallbackAlbedoPartCount,
      modelAnimationRuntime: snapshot.modelAnimationRuntime,
      activeModelAnimationClip: snapshot.activeModelAnimationClip,
      nextModelAnimationClip: snapshot.nextModelAnimationClip,
      modelAnimationTimeSeconds: snapshot.modelAnimationTimeSeconds,
      modelAnimationDurationSeconds: snapshot.modelAnimationDurationSeconds,
      modelAnimationBlendWeight: snapshot.modelAnimationBlendWeight,
      modelAnimationWalkRunBlendWeight: snapshot.modelAnimationWalkRunBlendWeight,
      modelAnimationPlaybackScale: snapshot.modelAnimationPlaybackScale,
      modelAnimationLocomotionSpeedMetersPerSecond:
        snapshot.modelAnimationLocomotionSpeedMetersPerSecond,
      modelAnimationWalkSpeedMetersPerSecond: snapshot.modelAnimationWalkSpeedMetersPerSecond,
      modelAnimationRunSpeedMetersPerSecond: snapshot.modelAnimationRunSpeedMetersPerSecond,
      modelAnimationIdlePlaybackScale: snapshot.modelAnimationIdlePlaybackScale,
      modelAnimationWalkPlaybackScale: snapshot.modelAnimationWalkPlaybackScale,
      modelAnimationRunPlaybackScale: snapshot.modelAnimationRunPlaybackScale,
      modelSkinningRuntime: snapshot.modelSkinningRuntime,
      modelSkinningJointCount: snapshot.modelSkinningJointCount
    };
  }

  private computeDisplaySize(): { readonly width: number; readonly height: number } {
    const pixelRatio = Math.min(window.devicePixelRatio || 1, 2);

    return {
      width: Math.max(1, Math.floor(this.canvas.clientWidth * pixelRatio)),
      height: Math.max(1, Math.floor(this.canvas.clientHeight * pixelRatio))
    };
  }
}

function defaultBrowserTerrainFrameDiagnostics(): BrowserTerrainFrameDiagnostics {
  return {
    completionBudget: TERRAIN_COMPLETION_BUDGET_PER_FRAME,
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
  };
}

function completionVertexBytes(completions: readonly TerrainBuildCompletion[]): number {
  return completions.reduce(
    (total, completion) => total + completion.vertices.byteLength,
    0
  );
}

function completionIndexBytes(completions: readonly TerrainBuildCompletion[]): number {
  return completions.reduce(
    (total, completion) => total + completion.indices.byteLength,
    0
  );
}

function validatePlayerMode(mode: PlayerMode): PlayerMode {
  if (mode === "firstPerson" || mode === "thirdPerson" || mode === "debugFly") {
    return mode;
  }

  throw new Error(`Rust browser game returned unknown player mode '${mode}'.`);
}

function validatePlayerCharacterId(character: PlayerCharacterId): PlayerCharacterId {
  if (character === "male" || character === "female") {
    return character;
  }

  throw new Error(`Rust browser game returned unknown player character '${character}'.`);
}

function validateShadowDebugView(view: ShadowDebugView): ShadowDebugView {
  if (
    view === "off" ||
    view === "cascadeIndex" ||
    view === "shadowVisibility" ||
    view === "shadowDepthCascade0" ||
    view === "shadowDepthCascade1" ||
    view === "shadowDepthCascade2" ||
    view === "shadowDepthCascade3"
  ) {
    return view;
  }

  throw new Error(`Rust browser game returned unknown shadow debug view '${view}'.`);
}
