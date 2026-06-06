// Defines the temporary browser-side terrain render chunk sink contract.
// TypeScript uses this only to bridge worker mesh completions into the Rust
// browser game facade until Rust owns terrain mesh upload end to end.

import type {
  TerrainChunkCoord,
  TerrainChunkKey
} from "../world/terrainChunk.js";

export type TerrainRenderMeshPacket = {
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
};

export type TerrainRenderChunkPacket = {
  readonly key: TerrainChunkKey;
  readonly mesh: TerrainRenderMeshPacket;
};

export type TerrainRenderChunkMeshPacket = {
  readonly key: TerrainChunkKey;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
};

export type TerrainRenderChunkInput =
  | TerrainRenderChunkPacket
  | TerrainRenderChunkMeshPacket;

export type TerrainRenderChunkSink = {
  addChunk(chunk: TerrainRenderChunkInput): void;
  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean;
  clear(): void;
  retainChunks(chunks: readonly (TerrainChunkKey | TerrainChunkCoord)[]): void;
};
