import { vec3, type Vec3 } from "../math/vec3.js";
import {
  createEngineWebBrowserGame,
  type EngineWebBrowserGame
} from "./engineWebWasm.js";
import type { BrowserTextureAssetLoader } from "../browser/textureAssetLoader.js";
import type {
  BrowserFrameInput,
  PlayerCharacterId,
  PlayerMode,
  RustBrowserGameCommand,
  RustBrowserGameDebugSnapshot,
  ShadowDebugView,
} from "./browserGameTypes.js";

export class RustBrowserGameAdapter {
  readonly runtime = "rust-wgpu" as const;
  private width = 1;
  private height = 1;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly game: EngineWebBrowserGame
  ) {}

  static async create(
    canvas: HTMLCanvasElement,
    assetLoader?: BrowserTextureAssetLoader
  ): Promise<RustBrowserGameAdapter> {
    const game = await createEngineWebBrowserGame(canvas, assetLoader);
    const adapter = new RustBrowserGameAdapter(canvas, game);
    adapter.resize();

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
    this.game.tick(frame);
  }

  command(command: RustBrowserGameCommand): void {
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
      terrainChunkKeys: [...snapshot.terrainChunkKeys],
      terrainPreset: snapshot.terrainPreset,
      terrainSeed: snapshot.terrainSeed,
      terrainStreamStatus: snapshot.terrainStreamStatus,
      terrainStreamerRuntime: snapshot.terrainStreamerRuntime,
      terrainStreamSchedulerRuntime: snapshot.terrainStreamSchedulerRuntime,
      terrainDensityStoreRuntime: snapshot.terrainDensityStoreRuntime,
      terrainWorkerPoolRuntime: snapshot.terrainWorkerPoolRuntime,
      renderPacketRuntime: snapshot.renderPacketRuntime,
      terrainRenderPacketRuntime: snapshot.terrainRenderPacketRuntime,
      rendererRuntime: snapshot.rendererRuntime,
      terrainWorkerCount: snapshot.terrainWorkerCount,
      playerControllerRuntime: snapshot.playerControllerRuntime,
      rendererStatus: snapshot.rendererStatus,
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
