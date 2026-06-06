import type { Vec3 } from "../math/vec3.js";
import type { TerrainChunkKey } from "../world/terrainChunk.js";
import type { TerrainPresetId } from "../world/terrainDescriptor.js";
import type { EngineWebRendererStatus } from "./engineWebWasm.js";
import type { TerrainCoreWorkerStreamStatus } from "./terrainCoreWorkerStreamer.js";

export type PlayerMode = "firstPerson" | "debugFly";

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

export type RustBrowserGameCommand = RustBrowserGameResetCommand | Exclude<
  GameCommand,
  { readonly type: "resetStreaming" }
>;

export type RustBrowserGameDebugSnapshot = {
  readonly playerMode: PlayerMode;
  readonly playerPosition: Vec3;
};

export type GameDebugSnapshot = {
  readonly playerMode: PlayerMode;
  readonly playerPosition: Vec3;
  readonly loadedTerrainChunkKeys: TerrainChunkKey[];
  readonly terrainChunkKeys: TerrainChunkKey[];
  readonly terrainPreset: TerrainPresetId;
  readonly terrainSeed: number;
  readonly terrainStreamStatus: TerrainCoreWorkerStreamStatus;
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
};

export type TransformSnapshot = {
  readonly position: Vec3;
  readonly yaw: number;
  readonly pitch: number;
};
