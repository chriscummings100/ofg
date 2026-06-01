import type { TerrainChunkCoord, TerrainChunkKey } from "./terrainChunk.js";
import type { WorldDescriptor } from "./terrainGenerator.js";

export type TerrainChunkJobRequest = {
  readonly generation: number;
  readonly coord: TerrainChunkCoord;
  readonly cellSize: number;
};

export type TerrainWorkerChunkJobRequest = TerrainChunkJobRequest & {
  readonly descriptor: WorldDescriptor;
};

export type TerrainChunkJobStats = {
  readonly totalMs: number;
  readonly vertexCount: number;
  readonly indexCount: number;
};

export type TerrainChunkJobResult = {
  readonly generation: number;
  readonly key: TerrainChunkKey;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly stats: TerrainChunkJobStats;
};

export type TerrainChunkJobGenerator = {
  readonly workerCount?: number;
  generateChunk(request: TerrainChunkJobRequest): Promise<TerrainChunkJobResult>;
  reset?(): void;
  dispose?(): void;
};

export type TerrainWorkerRequestMessage = {
  readonly type: "generateChunk";
  readonly requestId: number;
  readonly request: TerrainWorkerChunkJobRequest;
};

export type TerrainWorkerResultMessage = {
  readonly type: "chunkResult";
  readonly requestId: number;
  readonly result: TerrainChunkJobResult;
};

export type TerrainWorkerErrorMessage = {
  readonly type: "error";
  readonly requestId: number;
  readonly message: string;
};

export type TerrainWorkerMessage = TerrainWorkerResultMessage | TerrainWorkerErrorMessage;
