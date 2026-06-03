import {
  parseTerrainChunkKey,
  terrainChunkCoord,
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey
} from "../world/terrainChunk.js";
import {
  readTerrainCoreMeshIndexBuffer,
  readTerrainCoreMeshPacketInputIndexBuffer,
  readTerrainCoreMeshPacketInputVertexBuffer,
  readTerrainCoreMeshVertexBuffer,
  type TerrainCoreWasmInstance
} from "../world/terrainCoreWasm.js";

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
  getChunk(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainRenderChunkPacket | undefined;
  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean;
  clear(): void;
  retainChunks(chunks: readonly (TerrainChunkKey | TerrainChunkCoord)[]): void;
};

export class TerrainCoreRenderPacketStore implements TerrainRenderChunkSink {
  readonly runtime = "rust" as const;
  private cachedVersion = -1;
  private cachedChunks: TerrainRenderChunkPacket[] = [];

  constructor(
    private readonly terrainCore: TerrainCoreWasmInstance
  ) {}

  get chunks(): readonly TerrainRenderChunkPacket[] {
    this.syncMeshCache();
    return this.cachedChunks;
  }

  addChunk(chunk: TerrainRenderChunkInput): void {
    const vertices = "mesh" in chunk ? chunk.mesh.vertices : chunk.vertices;
    const indices = "mesh" in chunk ? chunk.mesh.indices : chunk.indices;
    const prepared = this.terrainCore.exports.ofg_prepare_terrain_mesh_packet_input(
      vertices.length,
      indices.length
    );
    if (prepared !== 1) {
      throw new Error(`Rust terrain mesh packet store rejected chunk '${chunk.key}' buffer shape.`);
    }

    readTerrainCoreMeshPacketInputVertexBuffer(this.terrainCore.exports).set(vertices);
    readTerrainCoreMeshPacketInputIndexBuffer(this.terrainCore.exports).set(indices);

    const coord = parseTerrainChunkKey(chunk.key);
    const stored = this.terrainCore.exports.ofg_store_terrain_mesh_packet_buffer(
      coord.x,
      coord.y,
      coord.z,
      0
    );
    if (stored !== 1) {
      throw new Error(`Rust terrain mesh packet store rejected chunk '${chunk.key}'.`);
    }

  }

  getChunk(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainRenderChunkPacket | undefined {
    const key = toChunkKey(chunk);
    this.syncMeshCache();
    return this.cachedChunks.find((candidate) => candidate.key === key);
  }

  removeChunk(chunk: TerrainChunkKey | TerrainChunkCoord): boolean {
    const coord = toCoord(chunk);
    return this.terrainCore.exports.ofg_remove_terrain_mesh_packet(
      coord.x,
      coord.y,
      coord.z,
      0
    ) === 1;
  }

  clear(): void {
    this.terrainCore.exports.ofg_reset_terrain_mesh_packet_store();
  }

  retainChunks(chunks: readonly (TerrainChunkKey | TerrainChunkCoord)[]): void {
    const exports = this.terrainCore.exports;
    const count = chunks.length;
    const capacity = exports.ofg_terrain_mesh_packet_coord_buffer_capacity();
    if (count > capacity) {
      throw new Error(
        `Terrain mesh packet retain count ${count} exceeds WASM capacity ${capacity}.`
      );
    }

    const lods = this.meshPacketLodBuffer(count);
    const xs = this.meshPacketXBuffer(count);
    const ys = this.meshPacketYBuffer(count);
    const zs = this.meshPacketZBuffer(count);
    for (let index = 0; index < count; index += 1) {
      const coord = toCoord(chunks[index]);
      lods[index] = 0;
      xs[index] = coord.x;
      ys[index] = coord.y;
      zs[index] = coord.z;
    }

    if (exports.ofg_retain_terrain_mesh_packets(count) !== 1) {
      throw new Error("Rust terrain mesh packet store rejected the retain set.");
    }
  }

  size(): number {
    return this.terrainCore.exports.ofg_terrain_mesh_packet_store_entry_count();
  }

  chunkKeys(): TerrainChunkKey[] {
    return this.meshPacketCoords()
      .map(({ coord }) => terrainChunkKey(coord))
      .sort();
  }

  private syncMeshCache(): void {
    const version = this.terrainCore.exports.ofg_terrain_mesh_packet_store_version();
    if (version === this.cachedVersion) {
      return;
    }

    const coords = this.meshPacketCoords();
    const chunks: TerrainRenderChunkPacket[] = [];

    for (const { coord, lod } of coords) {
      if (lod !== 0) {
        throw new Error(`Unsupported Rust terrain mesh packet LOD '${lod}'.`);
      }

      const loaded = this.terrainCore.exports.ofg_load_terrain_mesh_packet_buffer(
        coord.x,
        coord.y,
        coord.z,
        lod
      );
      if (loaded !== 1) {
        throw new Error(`Rust terrain mesh packet '${terrainChunkKey(coord)}' could not be loaded.`);
      }

      const key = terrainChunkKey(coord);
      chunks.push({
        key,
        mesh: {
          vertices: new Float32Array(readTerrainCoreMeshVertexBuffer(this.terrainCore.exports)),
          indices: new Uint32Array(readTerrainCoreMeshIndexBuffer(this.terrainCore.exports))
        }
      });
    }

    this.cachedChunks = chunks;
    this.cachedVersion = version;
  }

  private meshPacketCoords(): { readonly coord: TerrainChunkCoord; readonly lod: number }[] {
    const expectedCount = this.terrainCore.exports.ofg_terrain_mesh_packet_store_entry_count();
    const count = this.terrainCore.exports.ofg_write_terrain_mesh_packet_coords();
    if (count !== expectedCount) {
      throw new Error(
        `Rust terrain mesh packet coord buffer wrote ${count} packets, expected ${expectedCount}.`
      );
    }

    const lods = new Uint32Array(this.meshPacketLodBuffer(count));
    const xs = new Int32Array(this.meshPacketXBuffer(count));
    const ys = new Int32Array(this.meshPacketYBuffer(count));
    const zs = new Int32Array(this.meshPacketZBuffer(count));

    return Array.from({ length: count }, (_value, index) => ({
      coord: terrainChunkCoord(xs[index], ys[index], zs[index]),
      lod: lods[index]
    })).sort((a, b) =>
      terrainChunkKey(a.coord).localeCompare(terrainChunkKey(b.coord)) || a.lod - b.lod
    );
  }

  private meshPacketLodBuffer(length: number): Uint32Array {
    return new Uint32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_terrain_mesh_packet_lod_buffer_ptr(),
      length
    );
  }

  private meshPacketXBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_terrain_mesh_packet_x_buffer_ptr(),
      length
    );
  }

  private meshPacketYBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_terrain_mesh_packet_y_buffer_ptr(),
      length
    );
  }

  private meshPacketZBuffer(length: number): Int32Array {
    return new Int32Array(
      this.terrainCore.exports.memory.buffer,
      this.terrainCore.exports.ofg_terrain_mesh_packet_z_buffer_ptr(),
      length
    );
  }
}

export function createTerrainCoreRenderPacketStore(
  terrainCore: TerrainCoreWasmInstance
): TerrainCoreRenderPacketStore {
  return new TerrainCoreRenderPacketStore(terrainCore);
}

export function createMirroredTerrainRenderChunkSink(
  sinks: readonly TerrainRenderChunkSink[]
): TerrainRenderChunkSink {
  if (sinks.length === 0) {
    throw new Error("Mirrored terrain render sink requires at least one target sink.");
  }

  return {
    addChunk(chunk) {
      for (const sink of sinks) {
        sink.addChunk(chunk);
      }
    },
    getChunk(chunk) {
      for (const sink of sinks) {
        const result = sink.getChunk(chunk);
        if (result !== undefined) {
          return result;
        }
      }

      return undefined;
    },
    removeChunk(chunk) {
      let removed = false;
      for (const sink of sinks) {
        removed = sink.removeChunk(chunk) || removed;
      }

      return removed;
    },
    clear() {
      for (const sink of sinks) {
        sink.clear();
      }
    },
    retainChunks(chunks) {
      for (const sink of sinks) {
        sink.retainChunks(chunks);
      }
    }
  };
}

function toChunkKey(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainChunkKey {
  return typeof chunk === "string" ? chunk : terrainChunkKey(chunk);
}

function toCoord(chunk: TerrainChunkKey | TerrainChunkCoord): TerrainChunkCoord {
  return typeof chunk === "string" ? parseTerrainChunkKey(chunk) : chunk;
}
