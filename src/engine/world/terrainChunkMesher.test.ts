import { equal, ok, throws } from "node:assert/strict";
import {
  generateTerrainDensityChunk,
  terrainChunkCoord,
  TERRAIN_CHUNK_CELLS_PER_AXIS,
  TERRAIN_CHUNK_SAMPLES_PER_AXIS,
  type TerrainDensitySource
} from "./terrainChunk.js";
import {
  findHighestSurfaceInColumn,
  meshChunkHighestSurface,
  meshChunkHighestSurfaceStack
} from "./terrainChunkMesher.js";
import { getFloatsPerVertex } from "./terrainMesh.js";

describe("terrainChunkMesher", () => {
  it("finds an interpolated highest surface in a density column", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(2.5), terrainChunkCoord(0, 0, 0));

    equal(findHighestSurfaceInColumn(chunk, 4, 7), 2.5);
  });

  it("finds surfaces inside negative-y chunks", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(-4.5), terrainChunkCoord(0, -1, 0));

    equal(findHighestSurfaceInColumn(chunk, 4, 7), -4.5);
  });

  it("returns undefined when a column has no surface crossing", () => {
    const alwaysAir: TerrainDensitySource = { densityAt: () => 1 };
    const alwaysSolid: TerrainDensitySource = { densityAt: () => -1 };

    equal(findHighestSurfaceInColumn(
      generateTerrainDensityChunk(alwaysAir, terrainChunkCoord(0, 0, 0)),
      0,
      0
    ), undefined);
    equal(findHighestSurfaceInColumn(
      generateTerrainDensityChunk(alwaysSolid, terrainChunkCoord(0, 0, 0)),
      0,
      0
    ), undefined);
  });

  it("rejects invalid surface columns", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(1), terrainChunkCoord(0, 0, 0));

    throws(() => findHighestSurfaceInColumn(chunk, -1, 0), /0 to 32/);
    throws(() => findHighestSurfaceInColumn(chunk, 0, 33), /0 to 32/);
  });

  it("meshes a flat chunk surface with shared vertices and full cell coverage", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(4), terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkHighestSurface(chunk);

    equal(mesh.vertices.length, 33 * 33 * getFloatsPerVertex());
    equal(mesh.indices.length, 32 * 32 * 6);
    equal(mesh.vertices[1], 4);
    ok(Math.abs(mesh.vertices[6]) < 1e-6);
    equal(mesh.vertices[7], 1);
    ok(Math.abs(mesh.vertices[8]) < 1e-6);
  });

  it("writes chunk world x and z positions into mesh vertices", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(1, 0, -1));

    const mesh = meshChunkHighestSurface(chunk);
    const finalOffset = (TERRAIN_CHUNK_SAMPLES_PER_AXIS * TERRAIN_CHUNK_SAMPLES_PER_AXIS - 1) *
      getFloatsPerVertex();

    equal(mesh.vertices[0], 32);
    equal(mesh.vertices[2], -32);
    equal(mesh.vertices[finalOffset + 0], 64);
    equal(mesh.vertices[finalOffset + 2], 0);
  });

  it("uses chunk cell size for mesh spacing", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(1, 0, 0), {
      cellSize: 0.5
    });

    const mesh = meshChunkHighestSurface(chunk);
    const nextVertexOffset = getFloatsPerVertex();

    equal(mesh.vertices[0], 16);
    equal(mesh.vertices[nextVertexOffset + 0], 16.5);
  });

  it("writes normalized uv coordinates across the chunk", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkHighestSurface(chunk);
    const finalOffset = (TERRAIN_CHUNK_SAMPLES_PER_AXIS * TERRAIN_CHUNK_SAMPLES_PER_AXIS - 1) *
      getFloatsPerVertex();

    equal(mesh.vertices[9], 0);
    equal(mesh.vertices[10], 0);
    equal(mesh.vertices[finalOffset + 9], 1);
    equal(mesh.vertices[finalOffset + 10], 1);
  });

  it("skips cells whose corners have no surface", () => {
    const source: TerrainDensitySource = {
      densityAt(position) {
        if (position.x > 16) {
          return 1;
        }

        return position.y - 2;
      }
    };
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkHighestSurface(chunk);

    ok(mesh.indices.length > 0);
    ok(mesh.indices.length < TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS * 6);
  });

  it("returns no indices for chunks without a height surface", () => {
    const chunk = generateTerrainDensityChunk({ densityAt: () => 1 }, terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkHighestSurface(chunk);

    equal(mesh.indices.length, 0);
    equal(mesh.vertices.length, 33 * 33 * getFloatsPerVertex());
  });

  it("meshes a complete surface across a vertical stack of density chunks", () => {
    const source: TerrainDensitySource = {
      densityAt(position) {
        return position.y - (position.x < 16 ? -1 : 1);
      }
    };
    const lower = generateTerrainDensityChunk(source, terrainChunkCoord(0, -1, 0));
    const upper = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const lowerMesh = meshChunkHighestSurface(lower);
    const upperMesh = meshChunkHighestSurface(upper);
    const stackMesh = meshChunkHighestSurfaceStack([upper, lower]);

    ok(lowerMesh.indices.length < TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS * 6);
    ok(upperMesh.indices.length < TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS * 6);
    equal(stackMesh.indices.length, TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS * 6);
  });

  it("uses a neutral y chunk origin for stacked mesh xz positions", () => {
    const source = createPlaneSource(-4);
    const lower = generateTerrainDensityChunk(source, terrainChunkCoord(1, -1, 2));

    const mesh = meshChunkHighestSurfaceStack([lower]);

    equal(mesh.vertices[0], 32);
    equal(mesh.vertices[1], -4);
    equal(mesh.vertices[2], 64);
  });

  it("rejects invalid terrain chunk stacks", () => {
    const first = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(0, 0, 0));
    const second = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(1, 0, 0));
    const gapped = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(0, 2, 0));
    const duplicate = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(0, 0, 0));
    const scaled = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(0, 0, 0), {
      cellSize: 2
    });

    throws(() => meshChunkHighestSurfaceStack([]), /at least one/);
    throws(() => meshChunkHighestSurfaceStack([first, second]), /share x, z, and cellSize/);
    throws(() => meshChunkHighestSurfaceStack([first, scaled]), /share x, z, and cellSize/);
    throws(() => meshChunkHighestSurfaceStack([first, gapped]), /vertically contiguous/);
    throws(() => meshChunkHighestSurfaceStack([first, duplicate]), /vertically contiguous/);
  });

  it("writes sloped normals from neighboring surface heights", () => {
    const source: TerrainDensitySource = {
      densityAt(position) {
        return position.y - position.x * 0.25;
      }
    };
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkHighestSurface(chunk);
    const offset = (1 + TERRAIN_CHUNK_SAMPLES_PER_AXIS) * getFloatsPerVertex();

    ok(mesh.vertices[offset + 6] < 0);
    ok(mesh.vertices[offset + 7] > 0.9);
  });

  it("uses supplied world-space normals for surface vertices", () => {
    const chunk = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkHighestSurface(chunk, {
      surfaceNormalAt: () => ({ x: 0, y: 0, z: 2 })
    });

    equal(mesh.vertices[6], 0);
    equal(mesh.vertices[7], 0);
    equal(mesh.vertices[8], 1);
  });

  it("writes matching supplied normals on adjacent chunk seams", () => {
    const first = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(0, 0, 0));
    const second = generateTerrainDensityChunk(createPlaneSource(0), terrainChunkCoord(1, 0, 0));
    const normalAt = (position: { readonly x: number }) => ({
      x: position.x * 0.01,
      y: 1,
      z: 0
    });

    const firstMesh = meshChunkHighestSurface(first, { surfaceNormalAt: normalAt });
    const secondMesh = meshChunkHighestSurface(second, { surfaceNormalAt: normalAt });
    const firstSeamOffset = TERRAIN_CHUNK_CELLS_PER_AXIS * getFloatsPerVertex();

    equal(firstMesh.vertices[firstSeamOffset + 6], secondMesh.vertices[6]);
    equal(firstMesh.vertices[firstSeamOffset + 7], secondMesh.vertices[7]);
    equal(firstMesh.vertices[firstSeamOffset + 8], secondMesh.vertices[8]);
  });
});

function createPlaneSource(height: number): TerrainDensitySource {
  return {
    densityAt: (position) => position.y - height
  };
}
