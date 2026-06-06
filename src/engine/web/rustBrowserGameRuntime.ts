import type { Vec3 } from "../math/vec3.js";
import type { TerrainRenderChunkSink } from "../render/terrainRenderChunkSink.js";
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
import type {
  BrowserFrameInput,
  GameCommand,
  GameDebugSnapshot,
  RustBrowserGameCommand,
  RustBrowserGameDebugSnapshot
} from "./browserGameTypes.js";
import {
  TerrainCoreWorkerStreamer,
  type TerrainCoreWorkerStreamStatus
} from "./terrainCoreWorkerStreamer.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

const DEFAULT_TERRAIN_STREAM_CONFIG = {
  horizontalRadius: 1,
  verticalChunkOffsets: [-2, -1, 0, 1],
  cellSize: 1
} as const;

type TerrainHeightSampler = (x: number, z: number) => number;

export type RustBrowserGameRenderer = TerrainRenderChunkSink & {
  readonly runtime: "rust-wgpu";
  setTerrainTextures(textures: TerrainMaterialTextures): void;
  tick(frame: BrowserFrameInput): void;
  renderFrame(): void;
  command(command: RustBrowserGameCommand): void;
  getDebugSnapshot(): RustBrowserGameDebugSnapshot;
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
    readonly workerPoolRuntime: "rust";
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

  tick(frame: BrowserFrameInput): void {
    this.dependencies.renderer.tick(frame);
    this.dependencies.terrainStreamer.update();
    this.dependencies.renderer.renderFrame();
  }

  command(command: GameCommand): void {
    switch (command.type) {
      case "togglePlayerMode":
      case "setPlayerMode":
      case "setDebugCamera":
        this.dependencies.renderer.command(command);
        return;
      case "setPlayerPosition":
        this.dependencies.renderer.command(command);
        this.dependencies.terrainStreamer.syncAround(
          this.dependencies.renderer.getDebugSnapshot().playerPosition
        );
        return;
      case "resetStreaming":
        this.dependencies.terrainStreamer.resetStreaming(
          this.dependencies.renderer.getDebugSnapshot().playerPosition
        );
        return;
    }
  }

  debugSnapshot(): GameDebugSnapshot {
    const rendererSnapshot = this.dependencies.renderer.getDebugSnapshot();

    return {
      playerMode: rendererSnapshot.playerMode,
      playerPosition: rendererSnapshot.playerPosition,
      loadedTerrainChunkKeys: this.dependencies.terrainStreamer.getLoadedChunkKeys(),
      terrainChunkKeys: this.dependencies.renderer.chunkKeys(),
      terrainPreset: this.dependencies.descriptor.terrainPreset,
      terrainSeed: this.dependencies.descriptor.seed,
      terrainStreamStatus: this.dependencies.terrainStreamer.getStreamStatus(),
      terrainStreamerRuntime: this.dependencies.terrainStreamer.runtime,
      terrainStreamSchedulerRuntime: this.terrainStreamSchedulerRuntime,
      terrainDensityStoreRuntime: this.dependencies.terrainDensityChunkStore.runtime,
      terrainWorkerPoolRuntime: this.dependencies.terrainWorker.workerPoolRuntime,
      renderPacketRuntime: this.renderPacketRuntime,
      terrainRenderPacketRuntime: this.terrainRenderPacketRuntime,
      rendererRuntime: this.rendererRuntime,
      rendererStatus: rendererSnapshot.rendererStatus,
      terrainWorkerCount: this.dependencies.terrainWorker.workerCount,
      playerControllerRuntime: this.playerControllerRuntime
    };
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
  renderer.command({
    type: "resetGame",
    terrainSeed: descriptor.seed,
    terrainPreset: terrainPresetCode
  });

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
      getTargetPosition: () => renderer.getDebugSnapshot().playerPosition,
      cellSize: DEFAULT_TERRAIN_STREAM_CONFIG.cellSize
    }
  );

  terrainStreamer.syncAround(renderer.getDebugSnapshot().playerPosition);

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
