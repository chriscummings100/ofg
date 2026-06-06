import { deepEqual, equal, throws } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import {
  TERRAIN_CHUNK_CELLS_PER_AXIS,
  terrainChunkCoord,
  terrainChunkCoordContainingPosition,
  terrainChunkKey
} from "./terrainChunk.js";

describe("terrainChunk", () => {
  it("uses 32 cells per axis to mirror the Rust terrain chunk grid", () => {
    equal(TERRAIN_CHUNK_CELLS_PER_AXIS, 32);
  });

  it("creates stable chunk keys that support negative coordinates", () => {
    const coord = terrainChunkCoord(-2, 3, 4);

    equal(terrainChunkKey(coord), "-2,3,4");
  });

  it("rejects non-integer chunk coordinates", () => {
    throws(() => terrainChunkCoord(0.5, 0, 0), /integer/);
    throws(() => terrainChunkCoord(0, Number.NaN, 0), /integer/);
  });

  it("returns immutable chunk coordinate objects", () => {
    const coord = terrainChunkCoord(1, 2, 3);

    throws(() => {
      (coord as { x: number }).x = 99;
    }, /read only|readonly|not writable/);
  });

  it("finds the 3D chunk coordinate containing a world position", () => {
    deepEqual(terrainChunkCoordContainingPosition(vec3(0, 0, 0)), terrainChunkCoord(0, 0, 0));
    deepEqual(terrainChunkCoordContainingPosition(vec3(31.99, 31.99, 31.99)), terrainChunkCoord(0, 0, 0));
    deepEqual(terrainChunkCoordContainingPosition(vec3(32, 32, 32)), terrainChunkCoord(1, 1, 1));
    deepEqual(terrainChunkCoordContainingPosition(vec3(-0.01, -0.01, -0.01)), terrainChunkCoord(-1, -1, -1));
    deepEqual(terrainChunkCoordContainingPosition(vec3(16, -16, 48), 0.5), terrainChunkCoord(1, -1, 3));
  });

  it("rejects non-positive cell sizes", () => {
    throws(() => terrainChunkCoordContainingPosition(vec3(0, 0, 0), 0), /cellSize/);
    throws(() => terrainChunkCoordContainingPosition(vec3(0, 0, 0), -1), /cellSize/);
  });
});
