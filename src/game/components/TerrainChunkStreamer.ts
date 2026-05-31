import { Mesh } from "../../engine/render/Mesh.js";
import { TerrainRenderer } from "../../engine/render/TerrainRenderer.js";
import { Component } from "../../engine/scene/Component.js";
import type { Entity } from "../../engine/scene/Entity.js";
import type { ResourceId } from "../../engine/scene/types.js";
import type { Vec3 } from "../../engine/math/vec3.js";
import {
  generateTerrainDensityChunk,
  parseTerrainChunkKey,
  terrainChunkCoord,
  terrainChunkCoordContainingPosition,
  terrainChunkKey,
  type TerrainChunkCoord,
  type TerrainChunkKey,
  type TerrainDensitySource
} from "../../engine/world/terrainChunk.js";
import { meshChunksDualContouring } from "../../engine/world/dualContouring.js";
import { POSITION_COLOR_NORMAL_UV_LAYOUT } from "../../engine/world/terrainMesh.js";

export type TerrainChunkStreamerOptions = {
  readonly target?: Entity;
  readonly material?: ResourceId;
  readonly horizontalRadius?: number;
  readonly verticalChunkOffsets?: readonly number[];
  readonly cellSize?: number;
  readonly meshIdPrefix?: string;
};

export class TerrainChunkStreamer extends Component {
  readonly terrain: TerrainRenderer;
  readonly source: TerrainDensitySource;
  target?: Entity;
  material?: ResourceId;
  horizontalRadius: number;
  verticalChunkOffsets: readonly number[];
  cellSize: number;
  meshIdPrefix: string;

  private readonly loadedChunkKeys = new Set<TerrainChunkKey>();
  private readonly renderChunkKeys = new Set<TerrainChunkKey>();
  private lastCenterCoord?: TerrainChunkCoord;

  constructor(
    terrain: TerrainRenderer,
    source: TerrainDensitySource,
    options: TerrainChunkStreamerOptions = {}
  ) {
    super();
    this.terrain = terrain;
    this.source = source;
    this.target = options.target;
    this.material = options.material;
    this.horizontalRadius = options.horizontalRadius ?? 1;
    this.verticalChunkOffsets = options.verticalChunkOffsets ?? [-1, 0, 1];
    this.cellSize = options.cellSize ?? 1;
    this.meshIdPrefix = options.meshIdPrefix ?? "mesh:terrain.chunk";
    validateOptions(this.horizontalRadius, this.verticalChunkOffsets, this.cellSize);
  }

  override update(): void {
    const center = this.target?.transform.getWorldPosition() ?? this.entity?.transform.getWorldPosition();
    if (center === undefined) {
      return;
    }

    this.syncAround(center);
  }

  syncAround(center: Vec3): void {
    validateOptions(this.horizontalRadius, this.verticalChunkOffsets, this.cellSize);
    const centerCoord = terrainChunkCoordContainingPosition(center, this.cellSize);
    const desired = this.buildDesiredDensityChunkKeys(centerCoord);
    if (setsMatch(this.loadedChunkKeys, desired)) {
      this.lastCenterCoord = centerCoord;
      return;
    }

    for (const key of this.renderChunkKeys) {
      this.terrain.removeChunk(key);
    }
    this.renderChunkKeys.clear();
    this.loadedChunkKeys.clear();

    for (const key of desired) {
      this.loadedChunkKeys.add(key);
    }
    this.lastCenterCoord = centerCoord;
    this.loadRenderWindow(centerCoord);
  }

  rebuildChunk(chunk: TerrainChunkKey | TerrainChunkCoord): void {
    const coord = typeof chunk === "string" ? parseTerrainChunkKey(chunk) : chunk;
    const centerCoord = this.lastCenterCoord ?? coord;
    for (const key of this.renderChunkKeys) {
      this.terrain.removeChunk(key);
    }
    this.renderChunkKeys.clear();
    this.loadedChunkKeys.delete(terrainChunkKey(coord));

    this.loadRenderWindow(centerCoord);
  }

  invalidateAll(): void {
    for (const key of this.renderChunkKeys) {
      this.terrain.removeChunk(key);
    }

    this.loadedChunkKeys.clear();
    this.renderChunkKeys.clear();
  }

  getLoadedChunkKeys(): string[] {
    return [...this.loadedChunkKeys].sort();
  }

  private buildDesiredDensityChunkKeys(centerCoord: TerrainChunkCoord): Set<TerrainChunkKey> {
    const desired = new Set<TerrainChunkKey>();
    for (let z = centerCoord.z - this.horizontalRadius; z <= centerCoord.z + this.horizontalRadius; z += 1) {
      for (let x = centerCoord.x - this.horizontalRadius; x <= centerCoord.x + this.horizontalRadius; x += 1) {
        for (const coord of this.buildDensityChunkCoordsForColumn(x, centerCoord.y, z)) {
          desired.add(terrainChunkKey(coord));
        }
      }
    }

    return desired;
  }

  private loadRenderWindow(centerCoord: TerrainChunkCoord): void {
    const densityChunks = this.buildDensityChunkCoords(centerCoord).map((coord) => {
      this.loadedChunkKeys.add(terrainChunkKey(coord));
      return generateTerrainDensityChunk(this.source, coord, { cellSize: this.cellSize });
    });
    const meshData = meshChunksDualContouring(densityChunks, this.source, {
      // Raw QEF placement is too unstable on noisy terrain until it is regularized.
      placement: "centroid"
    });
    if (meshData.indices.length === 0) {
      return;
    }

    const key = terrainChunkKey(centerCoord);
    this.terrain.addChunk({
      key,
      mesh: new Mesh(
        `${this.meshIdPrefix}:${key}`,
        meshData.vertices,
        meshData.indices,
        POSITION_COLOR_NORMAL_UV_LAYOUT
      ),
      material: this.material
    });
    this.renderChunkKeys.add(key);
  }

  private buildDensityChunkCoords(centerCoord: TerrainChunkCoord): TerrainChunkCoord[] {
    const coords: TerrainChunkCoord[] = [];
    for (let z = centerCoord.z - this.horizontalRadius; z <= centerCoord.z + this.horizontalRadius; z += 1) {
      for (let x = centerCoord.x - this.horizontalRadius; x <= centerCoord.x + this.horizontalRadius; x += 1) {
        coords.push(...this.buildDensityChunkCoordsForColumn(x, centerCoord.y, z));
      }
    }

    return coords;
  }

  private buildDensityChunkCoordsForColumn(x: number, centerY: number, z: number): TerrainChunkCoord[] {
    return this.verticalChunkOffsets.map((offset) => terrainChunkCoord(x, centerY + offset, z));
  }
}

function setsMatch(a: ReadonlySet<string>, b: ReadonlySet<string>): boolean {
  if (a.size !== b.size) {
    return false;
  }

  for (const value of a) {
    if (!b.has(value)) {
      return false;
    }
  }

  return true;
}

function validateOptions(
  horizontalRadius: number,
  verticalChunkOffsets: readonly number[],
  cellSize: number
): void {
  if (!Number.isInteger(horizontalRadius) || horizontalRadius < 0) {
    throw new Error("TerrainChunkStreamer horizontalRadius must be a non-negative integer.");
  }

  if (
    verticalChunkOffsets.length === 0 ||
    verticalChunkOffsets.some((offset) => !Number.isInteger(offset))
  ) {
    throw new Error("TerrainChunkStreamer verticalChunkOffsets must contain integer chunk offsets.");
  }

  if (new Set(verticalChunkOffsets).size !== verticalChunkOffsets.length) {
    throw new Error("TerrainChunkStreamer verticalChunkOffsets must not contain duplicates.");
  }

  if (cellSize <= 0) {
    throw new Error("TerrainChunkStreamer cellSize must be positive.");
  }
}
