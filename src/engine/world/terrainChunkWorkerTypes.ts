import type { TerrainChunkCoord, TerrainChunkKey } from "./terrainChunk.js";
import type { WorldDescriptor } from "./terrainGenerator.js";

export type TerrainChunkJobRequest = {
  readonly generation: number;
  readonly coord: TerrainChunkCoord;
  readonly densityChunks: readonly TerrainDensityChunkPayload[];
  readonly densityBufferTransfer?: "clone" | "move";
  readonly cellSize: number;
};

export type TerrainDensityJobRequest = {
  readonly generation: number;
  readonly coord: TerrainChunkCoord;
  readonly cellSize: number;
};

export type TerrainWorkerChunkJobRequest = TerrainChunkJobRequest & {
  readonly descriptor: WorldDescriptor;
};

export type TerrainWorkerDensityJobRequest = TerrainDensityJobRequest & {
  readonly descriptor: WorldDescriptor;
};

export type TerrainChunkJobStats = {
  readonly totalMs: number;
  readonly vertexCount: number;
  readonly indexCount: number;
};

export type TerrainDensityJobStats = {
  readonly totalMs: number;
};

export type TerrainDensityJobResult = {
  readonly generation: number;
  readonly key: TerrainChunkKey;
  readonly coord: TerrainChunkCoord;
  readonly densities: Float32Array;
  readonly stats: TerrainDensityJobStats;
};

export type TerrainDensityChunkPayload = {
  readonly key: TerrainChunkKey;
  readonly coord: TerrainChunkCoord;
  readonly densities: Float32Array;
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
  readonly workerPoolRuntime?: "rust" | "typescript";
  prepareDensityChunk(request: TerrainDensityJobRequest): Promise<TerrainDensityJobResult>;
  generateChunk(request: TerrainChunkJobRequest): Promise<TerrainChunkJobResult>;
  reset?(): void;
  dispose?(): void;
};

export type TerrainWorkerDensityRequestMessage = {
  readonly type: "prepareDensityChunk";
  readonly requestId: number;
  readonly request: TerrainWorkerDensityJobRequest;
};

export type TerrainWorkerChunkRequestMessage = {
  readonly type: "generateChunk";
  readonly requestId: number;
  readonly request: TerrainWorkerChunkJobRequest;
};

export type TerrainWorkerRequestMessage =
  | TerrainWorkerDensityRequestMessage
  | TerrainWorkerChunkRequestMessage;

export type TerrainWorkerDensityResultMessage = {
  readonly type: "densityResult";
  readonly requestId: number;
  readonly result: TerrainDensityJobResult;
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

export type TerrainWorkerMessage =
  | TerrainWorkerDensityResultMessage
  | TerrainWorkerResultMessage
  | TerrainWorkerErrorMessage;
