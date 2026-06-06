import type { TerrainChunkCoord } from "./terrainChunk.js";
import type { WorldDescriptor } from "./terrainDescriptor.js";

export type TerrainChunkJobRequest = {
  readonly generation: number;
  readonly coord: TerrainChunkCoord;
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
  readonly coord: TerrainChunkCoord;
  readonly densities: Float32Array;
  readonly stats: TerrainDensityJobStats;
};

export type TerrainChunkJobResult = {
  readonly generation: number;
  readonly coord: TerrainChunkCoord;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly stats: TerrainChunkJobStats;
};

export type TerrainChunkJobGenerator = {
  readonly workerCount?: number;
  readonly workerPoolRuntime?: "rust";
  prepareDensityChunk(request: TerrainDensityJobRequest): Promise<TerrainDensityJobResult>;
  generateChunk(request: TerrainChunkJobRequest): Promise<TerrainChunkJobResult>;
  reset?(): void;
  dispose?(): void;
};

export type TerrainWorkerDensityRequestPayload = {
  readonly type: "prepareDensityChunk";
  readonly request: TerrainWorkerDensityJobRequest;
};

export type TerrainWorkerChunkRequestPayload = {
  readonly type: "generateChunk";
  readonly request: TerrainWorkerChunkJobRequest;
};

export type TerrainWorkerRequestPayload =
  | TerrainWorkerDensityRequestPayload
  | TerrainWorkerChunkRequestPayload;

export type TerrainWorkerDensityResultPayload = {
  readonly type: "densityResult";
  readonly result: TerrainDensityJobResult;
};

export type TerrainWorkerChunkResultPayload = {
  readonly type: "chunkResult";
  readonly result: TerrainChunkJobResult;
};

export type TerrainWorkerResultPayload =
  | TerrainWorkerDensityResultPayload
  | TerrainWorkerChunkResultPayload;
