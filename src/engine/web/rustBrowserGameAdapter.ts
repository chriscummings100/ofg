import { vec3, type Vec3 } from "../math/vec3.js";
import {
  createEngineWebBrowserGame,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebBrowserGame
} from "./engineWebWasm.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type {
  BrowserFrameInput,
  PlayerMode,
  RustBrowserGameCommand,
  RustBrowserGameDebugSnapshot,
} from "./browserGameTypes.js";

export class RustBrowserGameAdapter {
  readonly runtime = "rust-wgpu" as const;
  private uploadedTerrainTextures?: TerrainMaterialTextures;
  private width = 1;
  private height = 1;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly game: EngineWebBrowserGame
  ) {}

  static async create(canvas: HTMLCanvasElement): Promise<RustBrowserGameAdapter> {
    const game = await createEngineWebBrowserGame(canvas);
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

  setTerrainTextures(textures: TerrainMaterialTextures): void {
    this.upsertTerrainTexturesIfNeeded(textures);
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
      rendererStatus: snapshot.rendererStatus
    };
  }

  terrainHeightAt(x: number, z: number): number {
    return this.game.terrainHeightAt(x, z);
  }

  private upsertTerrainTexturesIfNeeded(textures: TerrainMaterialTextures | undefined): void {
    if (textures === undefined || this.uploadedTerrainTextures === textures) {
      return;
    }

    validateTerrainTextureArrays(textures);
    this.game.upsertTerrainTextures(
      textures.albedo.width,
      textures.albedo.height,
      textures.albedo.layers,
      ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
      textures.albedo.data,
      textures.normal.data,
      textures.material.data
    );
    this.uploadedTerrainTextures = textures;
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
  if (mode === "firstPerson" || mode === "debugFly") {
    return mode;
  }

  throw new Error(`Rust browser game returned unknown player mode '${mode}'.`);
}

function validateTerrainTextureArrays(textures: TerrainMaterialTextures): void {
  const { width, height, layers } = textures.albedo;
  for (const [label, texture] of [
    ["normal", textures.normal],
    ["material", textures.material]
  ] as const) {
    if (texture.width !== width || texture.height !== height || texture.layers !== layers) {
      throw new Error(
        `RustBrowserGame renderer received terrain ${label} texture dimensions ` +
        `${texture.width}x${texture.height}x${texture.layers}; expected ` +
        `${width}x${height}x${layers}.`
      );
    }
  }
}
