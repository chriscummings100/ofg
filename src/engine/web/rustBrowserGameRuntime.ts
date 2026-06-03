import { vec3, type Vec3 } from "../math/vec3.js";
import type { TerrainRenderChunkSink } from "../render/TerrainCoreRenderPackets.js";
import { loadTerrainMaterialTextures, type TerrainMaterialTextures } from "../render/terrainTextures.js";
import { createTerrainCoreDensityChunkStore } from "../world/terrainCoreDensityChunkStore.js";
import { createTerrainCoreStreamScheduler } from "../world/terrainCoreStreamScheduler.js";
import {
  loadTerrainCoreWasm,
  terrainPresetToWasmCode,
  type TerrainCoreWasmInstance
} from "../world/terrainCoreWasm.js";
import {
  createTerrainChunkWorkerClient,
  type TerrainChunkWorkerClient
} from "../world/terrainChunkWorkerClient.js";
import type { TerrainChunkKey } from "../world/terrainChunk.js";
import type { TerrainPresetId, WorldDescriptor } from "../world/terrainDescriptor.js";
import type { PlayerMode, PlayerMovementIntent } from "./browserGameTypes.js";
import {
  TerrainCoreWorkerStreamer,
  type TerrainCoreWorkerStreamStatus
} from "./terrainCoreWorkerStreamer.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";
import type { EngineWebRendererStatus } from "./engineWebWasm.js";

const DEFAULT_TERRAIN_STREAM_CONFIG = {
  horizontalRadius: 1,
  verticalChunkOffsets: [-2, -1, 0, 1],
  cellSize: 1
} as const;

type TerrainHeightSampler = (x: number, z: number) => number;

export type RustBrowserGameRenderer = TerrainRenderChunkSink & {
  readonly runtime: "rust-wgpu";
  setTerrainTextures(textures: TerrainMaterialTextures): void;
  resetGame(terrainSeed: number, terrainPreset: number): void;
  tick(deltaSeconds: number, intent: PlayerMovementIntent): void;
  renderGameFrame(): void;
  toggleCameraMode(): PlayerMode;
  getPlayerMode(): PlayerMode;
  setPlayerMode(mode: PlayerMode): void;
  getPlayerPosition(): Vec3;
  setPlayerPosition(x: number, z: number): void;
  setDebugCamera(position: Vec3, yaw: number, pitch: number): void;
  getStatus(): EngineWebRendererStatus;
  chunkKeys(): TerrainChunkKey[];
};

export type TerrainWorkerStreamer = {
  readonly runtime: "rust";
  syncAround(center: Vec3): void;
  update(): void;
  resetStreaming(center?: Vec3): void;
  getLoadedChunkKeys(): TerrainChunkKey[];
  getStreamStatus(): TerrainCoreWorkerStreamStatus;
};

export type RustBrowserGameRuntimeDependencies = {
  readonly descriptor: WorldDescriptor;
  readonly renderer: RustBrowserGameRenderer;
  readonly terrainStreamer: TerrainWorkerStreamer;
  readonly terrainWorker: {
    readonly workerCount: number;
    readonly workerPoolRuntime: "rust" | "typescript";
  };
  readonly terrainDensityChunkStore: {
    readonly runtime: "rust";
  };
  readonly terrainHeightAt: TerrainHeightSampler;
};

export class RustBrowserGameRuntime {
  readonly rendererRuntime = "rust-wgpu" as const;
  readonly playerControllerRuntime = "rust" as const;
  readonly renderPacketRuntime = "rust" as const;
  readonly terrainRenderPacketRuntime = "rust" as const;
  readonly terrainStreamSchedulerRuntime = "rust" as const;

  constructor(
    private readonly dependencies: RustBrowserGameRuntimeDependencies
  ) {}

  tick(deltaSeconds: number, intent: PlayerMovementIntent): void {
    this.dependencies.renderer.tick(deltaSeconds, intent);
    this.dependencies.terrainStreamer.update();
  }

  renderFrame(): void {
    this.dependencies.renderer.renderGameFrame();
  }

  toggleCameraMode(): PlayerMode {
    return this.dependencies.renderer.toggleCameraMode();
  }

  getPlayerMode(): PlayerMode {
    return this.dependencies.renderer.getPlayerMode();
  }

  setPlayerMode(mode: PlayerMode): void {
    this.dependencies.renderer.setPlayerMode(mode);
  }

  setDebugCamera(x: number, y: number, z: number, yaw: number, pitch: number): void {
    this.dependencies.renderer.setDebugCamera(vec3(x, y, z), yaw, pitch);
  }

  setPlayerPosition(x: number, z: number): void {
    this.dependencies.renderer.setPlayerPosition(x, z);
    this.dependencies.terrainStreamer.syncAround(this.dependencies.renderer.getPlayerPosition());
  }

  resetTerrainStreaming(): void {
    this.dependencies.terrainStreamer.resetStreaming(
      this.dependencies.renderer.getPlayerPosition()
    );
  }

  getLoadedTerrainChunkKeys(): TerrainChunkKey[] {
    return this.dependencies.terrainStreamer.getLoadedChunkKeys();
  }

  getTerrainChunkKeys(): TerrainChunkKey[] {
    return this.dependencies.renderer.chunkKeys();
  }

  getTerrainPreset(): TerrainPresetId {
    return this.dependencies.descriptor.terrainPreset;
  }

  getTerrainSeed(): number {
    return this.dependencies.descriptor.seed;
  }

  getTerrainStreamStatus(): TerrainCoreWorkerStreamStatus {
    return this.dependencies.terrainStreamer.getStreamStatus();
  }

  getTerrainStreamerRuntime(): "rust" {
    return this.dependencies.terrainStreamer.runtime;
  }

  getTerrainDensityStoreRuntime(): "rust" {
    return this.dependencies.terrainDensityChunkStore.runtime;
  }

  getTerrainWorkerPoolRuntime(): "rust" | "typescript" {
    return this.dependencies.terrainWorker.workerPoolRuntime;
  }

  getRendererStatus(): EngineWebRendererStatus {
    return this.dependencies.renderer.getStatus();
  }

  getTerrainWorkerCount(): number {
    return this.dependencies.terrainWorker.workerCount;
  }

  getTerrainHeight(x: number, z: number): number {
    return this.dependencies.terrainHeightAt(x, z);
  }
}

export async function createRustBrowserGameRuntime(
  canvas: HTMLCanvasElement,
  descriptor: WorldDescriptor
): Promise<RustBrowserGameRuntime> {
  const renderer = await RustBrowserGameAdapter.create(canvas);
  const terrainCore = await loadTerrainCoreWasm();
  const terrainPresetCode = terrainPresetToWasmCode(descriptor.terrainPreset);
  renderer.resetGame(descriptor.seed, terrainPresetCode);

  const terrainWorker = createRequiredTerrainWorker(descriptor, terrainCore);
  const terrainStreamScheduler = createTerrainCoreStreamScheduler(terrainCore, {
    horizontalRadius: DEFAULT_TERRAIN_STREAM_CONFIG.horizontalRadius,
    verticalChunkOffsets: DEFAULT_TERRAIN_STREAM_CONFIG.verticalChunkOffsets,
    maxInFlightJobs: terrainWorker.workerCount
  });
  const terrainDensityChunkStore = createTerrainCoreDensityChunkStore(terrainCore, descriptor);

  renderer.setTerrainTextures(await loadTerrainMaterialTextures());

  const terrainStreamer = new TerrainCoreWorkerStreamer(
    renderer,
    terrainStreamScheduler,
    terrainDensityChunkStore,
    terrainWorker,
    {
      getTargetPosition: () => renderer.getPlayerPosition(),
      cellSize: DEFAULT_TERRAIN_STREAM_CONFIG.cellSize
    }
  );

  terrainStreamer.syncAround(renderer.getPlayerPosition());

  return new RustBrowserGameRuntime({
    descriptor,
    renderer,
    terrainStreamer,
    terrainWorker,
    terrainDensityChunkStore,
    terrainHeightAt: createTerrainHeightSampler(terrainCore, descriptor)
  });
}

function createRequiredTerrainWorker(
  descriptor: WorldDescriptor,
  terrainCore: TerrainCoreWasmInstance
): TerrainChunkWorkerClient {
  const worker = createTerrainChunkWorkerClient(descriptor, terrainCore);
  if (worker === undefined) {
    throw new Error("Terrain workers are required for the playable Rust terrain runtime.");
  }

  return worker;
}

function createTerrainHeightSampler(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor
): TerrainHeightSampler {
  const preset = terrainPresetToWasmCode(descriptor.terrainPreset);

  return (x, z) => terrainCore.exports.ofg_height_at(descriptor.seed, preset, x, z);
}
