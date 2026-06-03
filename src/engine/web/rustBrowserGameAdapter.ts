import {
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "../world/terrainChunk.js";
import { vec3, type Vec3 } from "../math/vec3.js";
import {
  createEngineWebBrowserGame,
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  type EngineWebBrowserGame,
  type EngineWebRendererStatus
} from "./engineWebWasm.js";
import type {
  TerrainRenderChunkInput,
  TerrainRenderChunkPacket,
  TerrainRenderChunkSink
} from "../render/TerrainCoreRenderPackets.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type {
  PlayerMode,
  PlayerMovementIntent
} from "../../game/components/playerTypes.js";

const FIRST_PERSON_MODE = 0;
const DEBUG_FLY_MODE = 1;

export class RustBrowserGameAdapter implements TerrainRenderChunkSink {
  readonly runtime = "rust-wgpu" as const;
  private uploadedTerrainTextures?: TerrainMaterialTextures;
  private readonly terrainChunkKeys = new Set<TerrainChunkKey>();
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
    this.game.resize(width, height);
  }

  getAspectRatio(): number {
    return this.width / this.height;
  }

  getStatus(): EngineWebRendererStatus {
    return this.game.status();
  }

  setTerrainTextures(textures: TerrainMaterialTextures): void {
    this.upsertTerrainTexturesIfNeeded(textures);
  }

  resetGame(terrainSeed: number, terrainPreset: number): void {
    this.game.resetGame(terrainSeed, terrainPreset);
  }

  tick(deltaSeconds: number, intent: PlayerMovementIntent): void {
    this.game.tick(
      deltaSeconds,
      intent.forward,
      intent.right,
      intent.up,
      intent.fast,
      intent.lookDeltaX,
      intent.lookDeltaY
    );
  }

  toggleCameraMode(): PlayerMode {
    return playerModeFromCode(this.game.togglePlayerMode());
  }

  getPlayerMode(): PlayerMode {
    return playerModeFromCode(this.game.playerMode());
  }

  setPlayerMode(mode: PlayerMode): void {
    this.game.setPlayerMode(playerModeToCode(mode));
  }

  getPlayerPosition(): Vec3 {
    return vec3(
      this.game.playerX(),
      this.game.playerY(),
      this.game.playerZ()
    );
  }

  setPlayerPosition(x: number, z: number): void {
    this.game.setPlayerPosition(x, z);
  }

  setDebugCamera(position: Vec3, yaw: number, pitch: number): void {
    this.game.setDebugCamera(position.x, position.y, position.z, yaw, pitch);
  }

  addChunk(chunk: TerrainRenderChunkInput): void {
    const mesh = "mesh" in chunk ? chunk.mesh : chunk;
    this.terrainChunkKeys.add(chunk.key);
    this.game.upsertTerrainMesh(chunk.key, mesh.vertices, mesh.indices);
  }

  getChunk(_chunk: TerrainChunkKey | TerrainChunkCoord): TerrainRenderChunkPacket | undefined {
    return undefined;
  }

  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean {
    const key = toChunkKey(chunk);
    const existed = this.terrainChunkKeys.delete(key);
    this.game.destroyTerrainMesh(key);
    return existed;
  }

  clear(): void {
    this.terrainChunkKeys.clear();
    this.game.clearTerrainMeshes();
  }

  retainChunks(chunks: readonly (TerrainChunkKey | TerrainChunkCoord)[]): void {
    const keys = chunks.map(toChunkKey);
    const retainSet = new Set(keys);
    for (const key of this.terrainChunkKeys) {
      if (!retainSet.has(key)) {
        this.terrainChunkKeys.delete(key);
      }
    }
    this.game.retainTerrainMeshes(keys);
  }

  chunkKeys(): TerrainChunkKey[] {
    return [...this.terrainChunkKeys].sort();
  }

  renderGameFrame(): void {
    this.resize();
    this.game.renderGameFrame(this.getAspectRatio());
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

function toChunkKey(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainChunkKey {
  return typeof chunk === "string" ? chunk : terrainChunkKey(chunk);
}

function playerModeToCode(mode: PlayerMode): number {
  return mode === "firstPerson" ? FIRST_PERSON_MODE : DEBUG_FLY_MODE;
}

function playerModeFromCode(code: number): PlayerMode {
  if (code === FIRST_PERSON_MODE) {
    return "firstPerson";
  }

  if (code === DEBUG_FLY_MODE) {
    return "debugFly";
  }

  throw new Error(`Rust browser game returned unknown player mode '${code}'.`);
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
