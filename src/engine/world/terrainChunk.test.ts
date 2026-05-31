import { deepEqual, equal, ok, throws } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import {
  EditableTerrainDensitySource,
  TERRAIN_CHUNK_CELLS_PER_AXIS,
  TERRAIN_CHUNK_SAMPLE_COUNT,
  TERRAIN_CHUNK_SAMPLES_PER_AXIS,
  TerrainDensityChunk,
  applyTerrainEdits,
  createSubtractSphereEdit,
  generateTerrainDensityChunk,
  parseTerrainChunkKey,
  terrainChunkBounds,
  terrainChunkCoordContainingPosition,
  terrainChunkCoord,
  terrainChunkKey,
  terrainChunkOrigin,
  terrainChunkSampleIndex,
  terrainChunkSamplePosition,
  type TerrainEdit,
  type TerrainDensitySource
} from "./terrainChunk.js";

describe("terrainChunk", () => {
  it("uses 32 cells and 33 samples per axis", () => {
    equal(TERRAIN_CHUNK_CELLS_PER_AXIS, 32);
    equal(TERRAIN_CHUNK_SAMPLES_PER_AXIS, 33);
    equal(TERRAIN_CHUNK_SAMPLE_COUNT, 33 * 33 * 33);
  });

  it("creates stable chunk keys that support negative coordinates", () => {
    const coord = terrainChunkCoord(-2, 3, 4);

    equal(terrainChunkKey(coord), "-2,3,4");
    deepEqual(parseTerrainChunkKey("-2,3,4"), coord);
  });

  it("rejects invalid chunk coordinates and keys", () => {
    throws(() => terrainChunkCoord(0.5, 0, 0), /integer/);
    throws(() => parseTerrainChunkKey("0:0:0"), /Invalid terrain chunk key/);
    throws(() => parseTerrainChunkKey("0,0,0,0"), /Invalid terrain chunk key/);
    throws(() => parseTerrainChunkKey("0.5,0,0"), /Invalid terrain chunk key/);
  });

  it("computes chunk origins and bounds in all three axes", () => {
    const coord = terrainChunkCoord(2, -1, 3);

    deepEqual(terrainChunkOrigin(coord, 0.5), vec3(32, -16, 48));
    deepEqual(terrainChunkBounds(coord, 0.5), {
      min: vec3(32, -16, 48),
      max: vec3(48, 0, 64)
    });
  });

  it("finds the 3D chunk coordinate containing a world position", () => {
    deepEqual(terrainChunkCoordContainingPosition(vec3(0, 0, 0)), terrainChunkCoord(0, 0, 0));
    deepEqual(terrainChunkCoordContainingPosition(vec3(31.99, 31.99, 31.99)), terrainChunkCoord(0, 0, 0));
    deepEqual(terrainChunkCoordContainingPosition(vec3(32, 32, 32)), terrainChunkCoord(1, 1, 1));
    deepEqual(terrainChunkCoordContainingPosition(vec3(-0.01, -0.01, -0.01)), terrainChunkCoord(-1, -1, -1));
    deepEqual(terrainChunkCoordContainingPosition(vec3(16, -16, 48), 0.5), terrainChunkCoord(1, -1, 3));
  });

  it("rejects non-positive cell sizes", () => {
    const coord = terrainChunkCoord(0, 0, 0);

    throws(() => terrainChunkOrigin(coord, 0), /cellSize/);
    throws(() => terrainChunkBounds(coord, -1), /cellSize/);
    throws(() => terrainChunkSamplePosition(coord, { x: 0, y: 0, z: 0 }, 0), /cellSize/);
    throws(() => new TerrainDensityChunk(coord, { cellSize: -0.25 }), /cellSize/);
    throws(
      () => generateTerrainDensityChunk({ densityAt: () => 0 }, coord, { cellSize: 0 }),
      /cellSize/
    );
  });

  it("indexes samples with x as the fastest axis", () => {
    equal(terrainChunkSampleIndex({ x: 0, y: 0, z: 0 }), 0);
    equal(terrainChunkSampleIndex({ x: 1, y: 0, z: 0 }), 1);
    equal(terrainChunkSampleIndex({ x: 0, y: 1, z: 0 }), 33);
    equal(terrainChunkSampleIndex({ x: 0, y: 0, z: 1 }), 33 * 33);
    equal(terrainChunkSampleIndex({ x: 32, y: 32, z: 32 }), TERRAIN_CHUNK_SAMPLE_COUNT - 1);
  });

  it("rejects sample coordinates outside the 33 cubed lattice", () => {
    throws(() => terrainChunkSampleIndex({ x: 33, y: 0, z: 0 }), /0 to 32/);
    throws(() => terrainChunkSampleIndex({ x: 0, y: 0.5, z: 0 }), /0 to 32/);
    throws(() => terrainChunkSamplePosition(terrainChunkCoord(0, 0, 0), { x: 0, y: -1, z: 0 }), /0 to 32/);
  });

  it("computes sample positions from 3D chunk coordinates", () => {
    const position = terrainChunkSamplePosition(
      terrainChunkCoord(1, 2, -1),
      { x: 4, y: 5, z: 6 },
      2
    );

    deepEqual(position, vec3(72, 138, -52));
  });

  it("stores and updates density samples", () => {
    const chunk = new TerrainDensityChunk(terrainChunkCoord(0, 0, 0));

    chunk.setDensityAtSample({ x: 1, y: 2, z: 3 }, -4.5);

    equal(chunk.key, "0,0,0");
    equal(chunk.densityAtSample({ x: 1, y: 2, z: 3 }), -4.5);
    equal(chunk.densities.length, TERRAIN_CHUNK_SAMPLE_COUNT);
  });

  it("initializes default density samples to zero", () => {
    const chunk = new TerrainDensityChunk(terrainChunkCoord(0, 0, 0));

    equal(chunk.densityAtSample({ x: 0, y: 0, z: 0 }), 0);
    equal(chunk.densityAtSample({ x: 32, y: 32, z: 32 }), 0);
  });

  it("keeps provided density buffers by reference", () => {
    const densities = new Float32Array(TERRAIN_CHUNK_SAMPLE_COUNT);
    densities[terrainChunkSampleIndex({ x: 2, y: 3, z: 4 })] = 7;

    const chunk = new TerrainDensityChunk(terrainChunkCoord(0, 0, 0), { densities });

    equal(chunk.densities, densities);
    equal(chunk.densityAtSample({ x: 2, y: 3, z: 4 }), 7);
  });

  it("copies constructor coordinates into an immutable chunk coord", () => {
    const coord = { x: 1, y: 2, z: 3 };
    const chunk = new TerrainDensityChunk(coord);
    coord.x = 99;

    deepEqual(chunk.coord, terrainChunkCoord(1, 2, 3));
    throws(() => {
      (chunk.coord as { x: number }).x = 99;
    }, /read only|readonly|not writable/);
  });

  it("uses the chunk cell size for sample positions and bounds", () => {
    const chunk = new TerrainDensityChunk(terrainChunkCoord(-1, 1, 2), { cellSize: 0.25 });

    deepEqual(chunk.samplePosition({ x: 4, y: 8, z: 12 }), vec3(-7, 10, 19));
    deepEqual(chunk.bounds(), {
      min: vec3(-8, 8, 16),
      max: vec3(0, 16, 24)
    });
  });

  it("rejects density sample access outside the chunk", () => {
    const chunk = new TerrainDensityChunk(terrainChunkCoord(0, 0, 0));

    throws(() => chunk.densityAtSample({ x: 0, y: 33, z: 0 }), /0 to 32/);
    throws(() => chunk.setDensityAtSample({ x: 0, y: 0, z: -1 }, 1), /0 to 32/);
  });

  it("rejects density arrays with the wrong sample count", () => {
    throws(
      () => new TerrainDensityChunk(terrainChunkCoord(0, 0, 0), {
        densities: new Float32Array(TERRAIN_CHUNK_SAMPLE_COUNT - 1)
      }),
      /require 35937 samples/
    );
  });

  it("samples a baseline density source into a complete chunk", () => {
    const source: TerrainDensitySource = {
      densityAt: (position) => position.x + position.y * 10 + position.z * 100
    };

    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    equal(chunk.densityAtSample({ x: 0, y: 0, z: 0 }), 0);
    equal(chunk.densityAtSample({ x: 1, y: 2, z: 3 }), 321);
    equal(chunk.densityAtSample({ x: 32, y: 32, z: 32 }), 3552);
  });

  it("passes scaled world positions to baseline density sampling", () => {
    const source: TerrainDensitySource = {
      densityAt: (position) => position.x + position.y * 10 + position.z * 100
    };

    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(1, -1, 0), {
      cellSize: 0.5
    });

    equal(chunk.densityAtSample({ x: 0, y: 0, z: 0 }), -144);
    equal(chunk.densityAtSample({ x: 2, y: 4, z: 6 }), 177);
  });

  it("samples the baseline exactly once for each chunk lattice point", () => {
    let sampleCount = 0;
    const source: TerrainDensitySource = {
      densityAt() {
        sampleCount += 1;
        return 0;
      }
    };

    generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    equal(sampleCount, TERRAIN_CHUNK_SAMPLE_COUNT);
  });

  it("samples adjacent chunks with matching seam densities", () => {
    const source: TerrainDensitySource = {
      densityAt: (position) => position.x + position.y + position.z
    };

    const left = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));
    const right = generateTerrainDensityChunk(source, terrainChunkCoord(1, 0, 0));

    equal(
      left.densityAtSample({ x: 32, y: 9, z: 12 }),
      right.densityAtSample({ x: 0, y: 9, z: 12 })
    );
  });

  it("samples adjacent y and z chunk seams at matching world positions", () => {
    const source: TerrainDensitySource = {
      densityAt: (position) => position.x + position.y * 10 + position.z * 100
    };

    const center = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));
    const above = generateTerrainDensityChunk(source, terrainChunkCoord(0, 1, 0));
    const front = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 1));

    equal(
      center.densityAtSample({ x: 7, y: 32, z: 11 }),
      above.densityAtSample({ x: 7, y: 0, z: 11 })
    );
    equal(
      center.densityAtSample({ x: 7, y: 11, z: 32 }),
      front.densityAtSample({ x: 7, y: 11, z: 0 })
    );
  });

  it("applies subtract sphere edits after baseline density", () => {
    const edit = createSubtractSphereEdit({
      id: "cut:sphere",
      center: vec3(2, 2, 2),
      radius: 3
    });

    equal(applyTerrainEdits(-10, vec3(2, 2, 2), [edit]), 3);
    equal(applyTerrainEdits(-10, vec3(3, 2, 2), [edit]), 2);
    equal(applyTerrainEdits(10, vec3(2, 2, 2), [edit]), 10);
    equal(applyTerrainEdits(-10, vec3(10, 2, 2), [edit]), -10);
    equal(applyTerrainEdits(-10, vec3(5, 2, 2), [edit]), -10);
  });

  it("applies terrain edits in array order", () => {
    const first: TerrainEdit = {
      id: "first",
      bounds: { min: vec3(0, 0, 0), max: vec3(0, 0, 0) },
      apply: (density) => density + 3
    };
    const second: TerrainEdit = {
      id: "second",
      bounds: { min: vec3(0, 0, 0), max: vec3(0, 0, 0) },
      apply: (density) => density * 2
    };

    equal(applyTerrainEdits(4, vec3(0, 0, 0), [first, second]), 14);
    equal(applyTerrainEdits(4, vec3(0, 0, 0), [second, first]), 11);
  });

  it("records subtract sphere edit bounds", () => {
    const edit = createSubtractSphereEdit({
      id: "cut:sphere",
      center: vec3(4, 5, 6),
      radius: 2
    });

    deepEqual(edit.bounds, {
      min: vec3(2, 3, 4),
      max: vec3(6, 7, 8)
    });
  });

  it("rejects invalid sphere edit radii", () => {
    throws(
      () => createSubtractSphereEdit({ id: "bad", center: vec3(0, 0, 0), radius: 0 }),
      /positive/
    );
  });

  it("generates chunks with edits applied on top of the baseline", () => {
    const source: TerrainDensitySource = {
      densityAt: () => -5
    };
    const edit = createSubtractSphereEdit({
      id: "cut:sphere",
      center: vec3(1, 1, 1),
      radius: 2
    });

    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0), {
      edits: [edit]
    });

    equal(chunk.densityAtSample({ x: 1, y: 1, z: 1 }), 2);
    equal(chunk.densityAtSample({ x: 10, y: 10, z: 10 }), -5);
  });

  it("applies generated chunk edits to scaled world positions", () => {
    const source: TerrainDensitySource = {
      densityAt: () => -4
    };
    const edit = createSubtractSphereEdit({
      id: "cut:sphere",
      center: vec3(16, 16, 16),
      radius: 8
    });

    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(1, 1, 1), {
      cellSize: 0.5,
      edits: [edit]
    });

    equal(chunk.densityAtSample({ x: 0, y: 0, z: 0 }), 8);
    equal(chunk.densityAtSample({ x: 32, y: 32, z: 32 }), -4);
  });

  it("wraps a baseline source with replaceable edits", () => {
    const source = new EditableTerrainDensitySource({ densityAt: () => -5 });
    source.addEdit(createSubtractSphereEdit({
      id: "cut:sphere",
      center: vec3(0, 0, 0),
      radius: 2
    }));
    source.addEdit(createSubtractSphereEdit({
      id: "cut:sphere",
      center: vec3(10, 0, 0),
      radius: 2
    }));

    equal(source.edits.length, 1);
    equal(source.densityAt(vec3(0, 0, 0)), -5);
    equal(source.densityAt(vec3(10, 0, 0)), 2);
    equal(source.removeEdit("cut:sphere"), true);
    equal(source.removeEdit("cut:sphere"), false);
    equal(source.densityAt(vec3(10, 0, 0)), -5);
  });

  it("deduplicates editable source constructor edits by id", () => {
    const source = new EditableTerrainDensitySource({ densityAt: () => -5 }, [
      createSubtractSphereEdit({ id: "cut:sphere", center: vec3(0, 0, 0), radius: 2 }),
      createSubtractSphereEdit({ id: "cut:sphere", center: vec3(10, 0, 0), radius: 2 })
    ]);

    equal(source.edits.length, 1);
    equal(source.densityAt(vec3(0, 0, 0)), -5);
    equal(source.densityAt(vec3(10, 0, 0)), 2);
  });

  it("clears all editable terrain edits", () => {
    const source = new EditableTerrainDensitySource({ densityAt: () => -1 }, [
      createSubtractSphereEdit({ id: "cut:first", center: vec3(0, 0, 0), radius: 1 }),
      createSubtractSphereEdit({ id: "cut:second", center: vec3(4, 0, 0), radius: 1 })
    ]);

    source.clearEdits();

    equal(source.edits.length, 0);
    ok(source.densityAt(vec3(0, 0, 0)) < 0);
  });
});
