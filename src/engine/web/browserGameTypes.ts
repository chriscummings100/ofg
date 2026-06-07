import type { Vec3 } from "../math/vec3.js";
import type { TerrainChunkKey } from "../world/terrainChunk.js";
import type { TerrainPresetId } from "../world/terrainDescriptor.js";
import type { EngineWebRendererStatus } from "./engineWebWasm.js";

export type PlayerMode = "firstPerson" | "thirdPerson" | "debugFly";

export type PlayerCharacterId = "male" | "female";

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
  | { readonly type: "resetStreaming" };

export type RustBrowserGameResetCommand = {
  readonly type: "resetGame";
  readonly terrainSeed: number;
  readonly terrainPreset: number;
};

export type RustBrowserGameCommand = RustBrowserGameResetCommand | GameCommand;

export type TerrainStreamJobStats = {
  readonly totalMs: number;
  readonly vertexCount?: number;
  readonly indexCount?: number;
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
  readonly maxConcurrentChunkJobs: number;
  readonly workerPoolRuntime: "rust";
  readonly lastDensityJobStats?: TerrainStreamJobStats;
  readonly lastChunkJobStats?: TerrainStreamJobStats;
};

export type RustBrowserGameDebugSnapshot = {
  readonly playerMode: PlayerMode;
  readonly playerPosition: Vec3;
  readonly loadedTerrainChunkKeys: TerrainChunkKey[];
  readonly terrainChunkKeys: TerrainChunkKey[];
  readonly terrainPreset: TerrainPresetId;
  readonly terrainSeed: number;
  readonly terrainStreamStatus: TerrainStreamStatus;
  readonly terrainStreamerRuntime: "rust";
  readonly terrainStreamSchedulerRuntime: "rust";
  readonly terrainDensityStoreRuntime: "rust";
  readonly terrainWorkerPoolRuntime: "rust";
  readonly renderPacketRuntime: "rust";
  readonly terrainRenderPacketRuntime: "rust";
  readonly rendererRuntime: "rust-wgpu";
  readonly rendererStatus: EngineWebRendererStatus;
  readonly terrainWorkerCount: number;
  readonly playerControllerRuntime: "rust";
  readonly playerCharacterId?: PlayerCharacterId;
  readonly playerCharacterLabel?: string;
  readonly playerCharacterRuntime?: "rust";
  readonly playerCharacterVisible?: boolean;
  readonly playerCharacterFollowsPlayer?: boolean;
  readonly debugPlayerMarkerVisible?: boolean;
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
