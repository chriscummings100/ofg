import {
  terrainChunkCoord,
  type TerrainChunkCoord
} from "./terrainChunk.js";
import type { TerrainCoreWasmInstance } from "./terrainCoreWasm.js";

export type TerrainStreamConfig = {
  readonly horizontalRadius: number;
  readonly verticalChunkOffsets: readonly number[];
  readonly maxInFlightJobs: number;
};

export type TerrainStreamJob =
  | {
      readonly kind: "density";
      readonly generation: number;
      readonly coord: TerrainChunkCoord;
    }
  | {
      readonly kind: "lod";
      readonly generation: number;
      readonly lod: number;
      readonly coord: TerrainChunkCoord;
    };

export type TerrainStreamStatus = {
  readonly generation: number;
  readonly desiredDensityCount: number;
  readonly desiredLod0Count: number;
  readonly densityReadyCount: number;
  readonly lod0ReadyCount: number;
  readonly lod0EmptyCount: number;
  readonly inFlightDensityCount: number;
  readonly inFlightLodCount: number;
  readonly missingDensityCount: number;
  readonly missingLod0Count: number;
  readonly maxInFlightJobs: number;
};

export type TerrainStreamScheduler = {
  syncCenter(coord: TerrainChunkCoord): void;
  reset(coord: TerrainChunkCoord): void;
  invalidateAll(): void;
  tick(): TerrainStreamJob[];
  completeDensity(generation: number, coord: TerrainChunkCoord): boolean;
  failDensity(generation: number, coord: TerrainChunkCoord): boolean;
  completeLod0(generation: number, coord: TerrainChunkCoord, empty: boolean): boolean;
  failLod0(generation: number, coord: TerrainChunkCoord): boolean;
  desiredDensityCoords(): TerrainChunkCoord[];
  desiredLod0Coords(): TerrainChunkCoord[];
  lod0DependencyCoords(coord: TerrainChunkCoord): TerrainChunkCoord[];
  status(): TerrainStreamStatus;
};

export class TerrainCoreStreamScheduler implements TerrainStreamScheduler {
  constructor(
    private readonly terrainCore: TerrainCoreWasmInstance,
    config: TerrainStreamConfig
  ) {
    validateTerrainStreamConfig(config);
    const exports = this.terrainCore.exports;
    const offsetCapacity = exports.ofg_stream_vertical_offset_buffer_capacity();
    if (config.verticalChunkOffsets.length > offsetCapacity) {
      throw new Error(
        `Terrain stream vertical offset count ${config.verticalChunkOffsets.length} exceeds ` +
        `WASM capacity ${offsetCapacity}.`
      );
    }

    const maxInFlightCapacity = exports.ofg_stream_job_buffer_capacity();
    if (config.maxInFlightJobs > maxInFlightCapacity) {
      throw new Error(
        `Terrain stream maxInFlightJobs ${config.maxInFlightJobs} exceeds ` +
        `WASM capacity ${maxInFlightCapacity}.`
      );
    }

    this.verticalOffsetBuffer(config.verticalChunkOffsets.length)
      .set(config.verticalChunkOffsets);
    const configured = exports.ofg_stream_configure(
      config.horizontalRadius,
      config.verticalChunkOffsets.length,
      config.maxInFlightJobs
    );
    if (configured !== 1) {
      throw new Error("Terrain stream scheduler rejected its configuration.");
    }
  }

  syncCenter(coord: TerrainChunkCoord): void {
    this.terrainCore.exports.ofg_stream_sync_center(coord.x, coord.y, coord.z);
  }

  reset(coord: TerrainChunkCoord): void {
    this.terrainCore.exports.ofg_stream_reset(coord.x, coord.y, coord.z);
  }

  invalidateAll(): void {
    this.terrainCore.exports.ofg_stream_invalidate_all();
  }

  tick(): TerrainStreamJob[] {
    const count = this.terrainCore.exports.ofg_stream_tick();
    const kinds = this.jobKindBuffer(count);
    const lods = this.jobLodBuffer(count);
    const generations = this.jobGenerationBuffer(count);
    const xs = this.jobXBuffer(count);
    const ys = this.jobYBuffer(count);
    const zs = this.jobZBuffer(count);
    const jobs: TerrainStreamJob[] = [];

    for (let index = 0; index < count; index += 1) {
      const generation = generations[index];
      const coord = terrainChunkCoord(xs[index], ys[index], zs[index]);

      if (kinds[index] === 0) {
        jobs.push({ kind: "density", generation, coord });
        continue;
      }

      jobs.push({ kind: "lod", generation, lod: lods[index], coord });
    }

    return jobs;
  }

  completeDensity(generation: number, coord: TerrainChunkCoord): boolean {
    return this.terrainCore.exports.ofg_stream_complete_density(
      generation,
      coord.x,
      coord.y,
      coord.z
    ) === 1;
  }

  failDensity(generation: number, coord: TerrainChunkCoord): boolean {
    return this.terrainCore.exports.ofg_stream_fail_density(
      generation,
      coord.x,
      coord.y,
      coord.z
    ) === 1;
  }

  completeLod0(generation: number, coord: TerrainChunkCoord, empty: boolean): boolean {
    return this.terrainCore.exports.ofg_stream_complete_lod0(
      generation,
      coord.x,
      coord.y,
      coord.z,
      Number(empty)
    ) === 1;
  }

  failLod0(generation: number, coord: TerrainChunkCoord): boolean {
    return this.terrainCore.exports.ofg_stream_fail_lod0(
      generation,
      coord.x,
      coord.y,
      coord.z
    ) === 1;
  }

  desiredDensityCoords(): TerrainChunkCoord[] {
    const count = this.terrainCore.exports.ofg_stream_write_desired_density_coords();
    this.assertCoordBufferCanRepresent(count, this.status().desiredDensityCount);

    return this.readCoordBuffer(count);
  }

  desiredLod0Coords(): TerrainChunkCoord[] {
    const count = this.terrainCore.exports.ofg_stream_write_desired_lod0_coords();
    this.assertCoordBufferCanRepresent(count, this.status().desiredLod0Count);

    return this.readCoordBuffer(count);
  }

  lod0DependencyCoords(coord: TerrainChunkCoord): TerrainChunkCoord[] {
    const count = this.terrainCore.exports.ofg_stream_write_lod0_dependency_coords(
      coord.x,
      coord.y,
      coord.z
    );

    return this.readCoordBuffer(count);
  }

  status(): TerrainStreamStatus {
    const exports = this.terrainCore.exports;

    return {
      generation: exports.ofg_stream_generation(),
      desiredDensityCount: exports.ofg_stream_status_desired_density_count(),
      desiredLod0Count: exports.ofg_stream_status_desired_lod0_count(),
      densityReadyCount: exports.ofg_stream_status_density_ready_count(),
      lod0ReadyCount: exports.ofg_stream_status_lod0_ready_count(),
      lod0EmptyCount: exports.ofg_stream_status_lod0_empty_count(),
      inFlightDensityCount: exports.ofg_stream_status_in_flight_density_count(),
      inFlightLodCount: exports.ofg_stream_status_in_flight_lod_count(),
      missingDensityCount: exports.ofg_stream_status_missing_density_count(),
      missingLod0Count: exports.ofg_stream_status_missing_lod0_count(),
      maxInFlightJobs: exports.ofg_stream_status_max_in_flight_jobs()
    };
  }

  private readCoordBuffer(count: number): TerrainChunkCoord[] {
    const xs = this.coordXBuffer(count);
    const ys = this.coordYBuffer(count);
    const zs = this.coordZBuffer(count);
    const coords: TerrainChunkCoord[] = [];

    for (let index = 0; index < count; index += 1) {
      coords.push(terrainChunkCoord(xs[index], ys[index], zs[index]));
    }

    return coords;
  }

  private assertCoordBufferCanRepresent(written: number, expected: number): void {
    if (written !== expected) {
      throw new Error(
        `Terrain stream coord buffer wrote ${written} coords, expected ${expected}.`
      );
    }
  }

  private verticalOffsetBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_vertical_offset_buffer_ptr(),
      length
    );
  }

  private jobKindBuffer(length: number): Uint32Array {
    return new Uint32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_job_kind_buffer_ptr(),
      length
    );
  }

  private jobLodBuffer(length: number): Uint32Array {
    return new Uint32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_job_lod_buffer_ptr(),
      length
    );
  }

  private jobGenerationBuffer(length: number): Float64Array {
    return new Float64Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_job_generation_buffer_ptr(),
      length
    );
  }

  private jobXBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_job_x_buffer_ptr(),
      length
    );
  }

  private jobYBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_job_y_buffer_ptr(),
      length
    );
  }

  private jobZBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_job_z_buffer_ptr(),
      length
    );
  }

  private coordXBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_coord_x_buffer_ptr(),
      length
    );
  }

  private coordYBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_coord_y_buffer_ptr(),
      length
    );
  }

  private coordZBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_stream_coord_z_buffer_ptr(),
      length
    );
  }
}

export function createTerrainCoreStreamScheduler(
  terrainCore: TerrainCoreWasmInstance,
  config: TerrainStreamConfig
): TerrainCoreStreamScheduler {
  return new TerrainCoreStreamScheduler(terrainCore, config);
}

function validateTerrainStreamConfig(config: TerrainStreamConfig): void {
  if (!Number.isInteger(config.horizontalRadius) || config.horizontalRadius < 0) {
    throw new Error("Terrain stream horizontalRadius must be a non-negative integer.");
  }

  if (
    config.verticalChunkOffsets.length === 0 ||
    config.verticalChunkOffsets.some((offset) => !Number.isInteger(offset))
  ) {
    throw new Error("Terrain stream verticalChunkOffsets must contain integer chunk offsets.");
  }

  if (new Set(config.verticalChunkOffsets).size !== config.verticalChunkOffsets.length) {
    throw new Error("Terrain stream verticalChunkOffsets must not contain duplicates.");
  }

  if (!Number.isInteger(config.maxInFlightJobs) || config.maxInFlightJobs <= 0) {
    throw new Error("Terrain stream maxInFlightJobs must be a positive integer.");
  }
}
