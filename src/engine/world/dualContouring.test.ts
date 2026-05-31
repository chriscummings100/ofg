import { deepEqual, equal, ok, throws } from "node:assert/strict";
import { dot, length, normalize, vec3, type Vec3 } from "../math/vec3.js";
import {
  dualContouringCellBounds,
  extractHermiteIntersections,
  meshChunkDualContouring,
  meshChunksDualContouring,
  placeDualContouringCellVertex,
  type HermiteIntersection
} from "./dualContouring.js";
import {
  generateTerrainDensityChunk,
  terrainChunkCoord,
  TERRAIN_CHUNK_CELLS_PER_AXIS,
  type TerrainDensitySource
} from "./terrainChunk.js";
import { getFloatsPerVertex } from "./terrainMesh.js";
import { createSeedTerrainField } from "./scalarField.js";

describe("dualContouring", () => {
  it("extracts Hermite intersections from a flat plane cell", () => {
    const source = createPlaneSource(vec3(0, 1, 0), 1.5);
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const intersections = extractHermiteIntersections(chunk, { x: 3, y: 1, z: 5 }, source);

    equal(intersections.length, 4);
    for (const intersection of intersections) {
      ok(Math.abs(intersection.position.y - 1.5) < 1e-6);
      ok(Math.abs(intersection.t - 0.5) < 1e-6);
      deepEqual(intersection.normal, vec3(0, 1, 0));
    }
  });

  it("extracts no Hermite intersections when the cell has no sign change", () => {
    const source: TerrainDensitySource = {
      densityAt: () => 1,
      sampleAt: () => ({ density: 1, gradient: vec3(0, 1, 0) })
    };
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const intersections = extractHermiteIntersections(chunk, { x: 3, y: 1, z: 5 }, source);

    equal(intersections.length, 0);
  });

  it("uses finite-difference gradients when a source has no sample API", () => {
    const source: TerrainDensitySource = {
      densityAt: (position) => position.x + position.y + position.z - 4.5
    };
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));
    const expectedNormal = normalize(vec3(1, 1, 1));

    const intersections = extractHermiteIntersections(chunk, { x: 1, y: 1, z: 1 }, source);

    equal(intersections.length, 6);
    for (const intersection of intersections) {
      assertVecApprox(intersection.normal, expectedNormal, 1e-10);
    }
  });

  it("extracts Hermite positions using chunk origin and cell size", () => {
    const source = createPlaneSource(vec3(0, 1, 0), -15.25);
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(1, -1, 2), {
      cellSize: 0.5
    });
    const cell = { x: 4, y: 1, z: 6 };

    const bounds = dualContouringCellBounds(chunk, cell);
    const intersections = extractHermiteIntersections(chunk, cell, source);

    deepEqual(bounds, {
      min: vec3(18, -15.5, 35),
      max: vec3(18.5, -15, 35.5)
    });
    equal(intersections.length, 4);
    for (const intersection of intersections) {
      ok(Math.abs(intersection.position.y + 15.25) < 1e-6);
      ok(Math.abs(intersection.t - 0.5) < 1e-6);
      ok(intersection.position.x >= bounds.min.x && intersection.position.x <= bounds.max.x);
      ok(intersection.position.z >= bounds.min.z && intersection.position.z <= bounds.max.z);
    }
  });

  it("extracts Hermite intersections from a diagonal plane cell", () => {
    const normal = normalize(vec3(1, 1, 1));
    const source = createPlaneSource(normal, dot(normal, vec3(1.5, 1.5, 1.5)));
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const intersections = extractHermiteIntersections(chunk, { x: 1, y: 1, z: 1 }, source);

    equal(intersections.length, 6);
    for (const intersection of intersections) {
      ok(Math.abs(dot(normal, intersection.position) - dot(normal, vec3(1.5, 1.5, 1.5))) < 1e-6);
      ok(Math.abs(intersection.normal.x - normal.x) < 1e-12);
      ok(Math.abs(intersection.normal.y - normal.y) < 1e-12);
      ok(Math.abs(intersection.normal.z - normal.z) < 1e-12);
    }
  });

  it("extracts Hermite intersections from a sphere cell", () => {
    const center = vec3(2, 2, 2);
    const radius = 0.75;
    const source = createSphereSource(center, radius);
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const intersections = extractHermiteIntersections(chunk, { x: 2, y: 2, z: 2 }, source);

    equal(intersections.length, 3);
    for (const intersection of intersections) {
      ok(Math.abs(length({
        x: intersection.position.x - center.x,
        y: intersection.position.y - center.y,
        z: intersection.position.z - center.z
      }) - radius) < 1e-6);
      ok(Math.abs(length(intersection.normal) - 1) < 1e-12);
    }
  });

  it("places a cell vertex at the centroid of Hermite crossings", () => {
    const source = createPlaneSource(vec3(0, 1, 0), 1.5);
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));
    const cell = { x: 3, y: 1, z: 5 };
    const intersections = extractHermiteIntersections(chunk, cell, source);

    const vertex = placeDualContouringCellVertex(
      intersections,
      dualContouringCellBounds(chunk, cell),
      { placement: "centroid" }
    );

    deepEqual(vertex, vec3(3.5, 1.5, 5.5));
  });

  it("uses QEF placement when Hermite planes have a unique solution", () => {
    const intersections = [
      createIntersection(vec3(0.25, 0.1, 0.1), vec3(1, 0, 0)),
      createIntersection(vec3(0.1, 0.5, 0.1), vec3(0, 1, 0)),
      createIntersection(vec3(0.1, 0.1, 0.75), vec3(0, 0, 1))
    ];

    const vertex = placeDualContouringCellVertex(intersections, {
      min: vec3(0, 0, 0),
      max: vec3(1, 1, 1)
    });

    deepEqual(vertex, vec3(0.25, 0.5, 0.75));
  });

  it("falls back to the centroid when QEF placement is underconstrained", () => {
    const intersections = [
      createIntersection(vec3(0.25, 0.25, 0.25), vec3(0, 1, 0)),
      createIntersection(vec3(0.75, 0.75, 0.75), vec3(0, 1, 0))
    ];

    const vertex = placeDualContouringCellVertex(intersections, {
      min: vec3(0, 0, 0),
      max: vec3(1, 1, 1)
    });

    deepEqual(vertex, vec3(0.5, 0.5, 0.5));
  });

  it("falls back to the centroid when QEF placement leaves the owning cell", () => {
    const intersections = [
      createIntersection(vec3(1, 0.5, 0.5), vec3(1, 0, 0)),
      createIntersection(vec3(0, 0.5, 0.5), normalize(vec3(1, 0.01, 0))),
      createIntersection(vec3(0.5, 0.5, 0.5), vec3(0, 0, 1))
    ];

    const vertex = placeDualContouringCellVertex(intersections, {
      min: vec3(0, 0, 0),
      max: vec3(1, 1, 1)
    });

    deepEqual(vertex, vec3(0.5, 0.5, 0.5));
  });

  it("clamps centroid placement to the owning cell bounds", () => {
    const intersections = [
      createIntersection(vec3(-1, 0.5, 0.5), vec3(1, 0, 0)),
      createIntersection(vec3(0.5, 2, 0.5), vec3(0, 1, 0)),
      createIntersection(vec3(0.5, 0.5, 0.5), vec3(0, 0, 1))
    ];

    const vertex = placeDualContouringCellVertex(intersections, {
      min: vec3(0, 0, 0),
      max: vec3(1, 1, 1)
    }, { placement: "centroid" });

    deepEqual(vertex, vec3(0, 1, 0.5));
  });

  it("returns undefined for a cell without Hermite crossings", () => {
    const vertex = placeDualContouringCellVertex([], {
      min: vec3(0, 0, 0),
      max: vec3(1, 1, 1)
    });

    equal(vertex, undefined);
  });

  it("extracts sane Hermite planes from the procedural terrain field", () => {
    const source = createSeedTerrainField();
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));
    let checkedIntersections = 0;

    scan:
    for (let z = 0; z < TERRAIN_CHUNK_CELLS_PER_AXIS; z += 1) {
      for (let y = 0; y < TERRAIN_CHUNK_CELLS_PER_AXIS; y += 1) {
        for (let x = 0; x < TERRAIN_CHUNK_CELLS_PER_AXIS; x += 1) {
          const cell = { x, y, z };
          const bounds = dualContouringCellBounds(chunk, cell);
          const intersections = extractHermiteIntersections(chunk, cell, source);

          for (const intersection of intersections) {
            ok(intersection.t >= 0 && intersection.t <= 1);
            ok(intersection.position.x >= bounds.min.x - 1e-6);
            ok(intersection.position.x <= bounds.max.x + 1e-6);
            ok(intersection.position.y >= bounds.min.y - 1e-6);
            ok(intersection.position.y <= bounds.max.y + 1e-6);
            ok(intersection.position.z >= bounds.min.z - 1e-6);
            ok(intersection.position.z <= bounds.max.z + 1e-6);
            ok(Math.abs(source.densityAt(intersection.position)) < 0.75);
            ok(Math.abs(length(intersection.normal) - 1) < 1e-6);
            assertFiniteVec3(intersection.position);
            assertFiniteVec3(intersection.normal);
            checkedIntersections += 1;
          }

          if (checkedIntersections >= 128) {
            break scan;
          }
        }
      }
    }

    ok(checkedIntersections >= 32);
  });

  it("meshes a flat plane into cell vertices and edge quads", () => {
    const source = createPlaneSource(vec3(0, 1, 0), 1.5);
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkDualContouring(chunk, source);

    const vertexCount = mesh.vertices.length / getFloatsPerVertex();
    equal(vertexCount, TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS);
    equal(mesh.indices.length, (TERRAIN_CHUNK_CELLS_PER_AXIS - 1) * (TERRAIN_CHUNK_CELLS_PER_AXIS - 1) * 6);
    assertMeshHasValidTriangles(mesh);
    for (let offset = 0; offset < mesh.vertices.length; offset += getFloatsPerVertex()) {
      ok(Math.abs(mesh.vertices[offset + 1] - 1.5) < 1e-6);
      ok(Math.abs(mesh.vertices[offset + 6]) < 1e-6);
      ok(Math.abs(mesh.vertices[offset + 7] - 1) < 1e-6);
      ok(Math.abs(mesh.vertices[offset + 8]) < 1e-6);
    }
  });

  it("meshes a diagonal plane without invalid indices", () => {
    const normal = normalize(vec3(1, 1, 1));
    const source = createPlaneSource(normal, dot(normal, vec3(16, 16, 16)));
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkDualContouring(chunk, source);

    const vertexCount = mesh.vertices.length / getFloatsPerVertex();
    ok(vertexCount > 0);
    ok(mesh.indices.length > 0);
    for (const index of mesh.indices) {
      ok(index < vertexCount);
    }
    assertMeshHasValidTriangles(mesh);
  });

  it("writes world-space vertex data for scaled offset chunks", () => {
    const source = createPlaneSource(vec3(0, 1, 0), -15.25);
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(1, -1, 2), {
      cellSize: 0.5
    });

    const mesh = meshChunkDualContouring(chunk, source, { placement: "centroid" });

    equal(mesh.vertices.length / getFloatsPerVertex(), TERRAIN_CHUNK_CELLS_PER_AXIS * TERRAIN_CHUNK_CELLS_PER_AXIS);
    for (let offset = 0; offset < mesh.vertices.length; offset += getFloatsPerVertex()) {
      ok(mesh.vertices[offset + 0] >= 16 && mesh.vertices[offset + 0] <= 32);
      ok(Math.abs(mesh.vertices[offset + 1] + 15.25) < 1e-6);
      ok(mesh.vertices[offset + 2] >= 32 && mesh.vertices[offset + 2] <= 48);
      assertVecApprox(vec3(
        mesh.vertices[offset + 6],
        mesh.vertices[offset + 7],
        mesh.vertices[offset + 8]
      ), vec3(0, 1, 0));
      ok(mesh.vertices[offset + 9] >= 0 && mesh.vertices[offset + 9] <= 1);
      ok(mesh.vertices[offset + 10] >= 0 && mesh.vertices[offset + 10] <= 1);
    }
  });

  it("reverses triangle winding when density signs are reversed", () => {
    const solidBelow = createPlaneSource(vec3(0, 1, 0), 1.5);
    const solidAbove = createPlaneSource(vec3(0, -1, 0), -1.5);
    const solidBelowChunk = generateTerrainDensityChunk(solidBelow, terrainChunkCoord(0, 0, 0));
    const solidAboveChunk = generateTerrainDensityChunk(solidAbove, terrainChunkCoord(0, 0, 0));

    const solidBelowMesh = meshChunkDualContouring(solidBelowChunk, solidBelow);
    const solidAboveMesh = meshChunkDualContouring(solidAboveChunk, solidAbove);

    deepEqual([...solidBelowMesh.indices.slice(0, 6)], [0, 32, 1, 1, 32, 33]);
    deepEqual([...solidAboveMesh.indices.slice(0, 6)], [0, 1, 32, 1, 33, 32]);
  });

  it("meshes multiple chunks into one stitched mesh", () => {
    const source = createPlaneSource(vec3(0, 1, 0), 1.5);
    const left = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));
    const right = generateTerrainDensityChunk(source, terrainChunkCoord(1, 0, 0));
    const separateLeft = meshChunkDualContouring(left, source);
    const separateRight = meshChunkDualContouring(right, source);

    const stitched = meshChunksDualContouring([right, left], source);

    equal(
      stitched.vertices.length / getFloatsPerVertex(),
      separateLeft.vertices.length / getFloatsPerVertex() +
      separateRight.vertices.length / getFloatsPerVertex()
    );
    ok(stitched.indices.length > separateLeft.indices.length + separateRight.indices.length);
    assertMeshHasValidTriangles(stitched);
  });

  it("rejects empty and differently scaled multi-chunk meshes", () => {
    const source = createPlaneSource(vec3(0, 1, 0), 1.5);
    const first = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0), {
      cellSize: 1
    });
    const second = generateTerrainDensityChunk(source, terrainChunkCoord(1, 0, 0), {
      cellSize: 0.5
    });

    throws(() => meshChunksDualContouring([], source), /at least one/);
    throws(() => meshChunksDualContouring([first, second], source), /cellSize/);
  });

  it("returns an empty mesh for a chunk without a surface", () => {
    const source: TerrainDensitySource = {
      densityAt: () => 1,
      sampleAt: () => ({ density: 1, gradient: vec3(0, 1, 0) })
    };
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    const mesh = meshChunkDualContouring(chunk, source);

    equal(mesh.vertices.length, 0);
    equal(mesh.indices.length, 0);
  });

  it("rejects invalid cell coordinates", () => {
    const source = createPlaneSource(vec3(0, 1, 0), 1.5);
    const chunk = generateTerrainDensityChunk(source, terrainChunkCoord(0, 0, 0));

    throws(() => extractHermiteIntersections(chunk, { x: -1, y: 0, z: 0 }, source), /0 to 31/);
    throws(() => dualContouringCellBounds(chunk, { x: 0, y: 32, z: 0 }), /0 to 31/);
  });
});

function createPlaneSource(normal: Vec3, offset: number): TerrainDensitySource {
  return {
    densityAt(position) {
      return dot(normal, position) - offset;
    },
    sampleAt(position) {
      return {
        density: dot(normal, position) - offset,
        gradient: normal
      };
    }
  };
}

function assertVecApprox(actual: Vec3, expected: Vec3, epsilon = 1e-12): void {
  ok(Math.abs(actual.x - expected.x) < epsilon);
  ok(Math.abs(actual.y - expected.y) < epsilon);
  ok(Math.abs(actual.z - expected.z) < epsilon);
}

function assertFiniteVec3(value: Vec3): void {
  ok(Number.isFinite(value.x));
  ok(Number.isFinite(value.y));
  ok(Number.isFinite(value.z));
}

function assertMeshHasValidTriangles(mesh: {
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
}): void {
  const vertexCount = mesh.vertices.length / getFloatsPerVertex();
  equal(mesh.vertices.length % getFloatsPerVertex(), 0);
  equal(mesh.indices.length % 3, 0);
  for (let indexOffset = 0; indexOffset < mesh.indices.length; indexOffset += 3) {
    const a = mesh.indices[indexOffset + 0];
    const b = mesh.indices[indexOffset + 1];
    const c = mesh.indices[indexOffset + 2];
    ok(a < vertexCount);
    ok(b < vertexCount);
    ok(c < vertexCount);
    ok(a !== b);
    ok(a !== c);
    ok(b !== c);
  }
}

function createSphereSource(center: Vec3, radius: number): TerrainDensitySource {
  return {
    densityAt(position) {
      return length(vec3(
        position.x - center.x,
        position.y - center.y,
        position.z - center.z
      )) - radius;
    },
    sampleAt(position) {
      const fromCenter = vec3(
        position.x - center.x,
        position.y - center.y,
        position.z - center.z
      );

      return {
        density: length(fromCenter) - radius,
        gradient: normalize(fromCenter)
      };
    }
  };
}

function createIntersection(position: Vec3, normal: Vec3): HermiteIntersection {
  return {
    edgeIndex: 0,
    startSample: { x: 0, y: 0, z: 0 },
    endSample: { x: 1, y: 0, z: 0 },
    t: 0,
    position,
    normal
  };
}
