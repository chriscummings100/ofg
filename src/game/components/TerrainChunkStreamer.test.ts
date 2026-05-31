import { equal, ok, throws } from "node:assert/strict";
import { vec3 } from "../../engine/math/vec3.js";
import { TerrainRenderer } from "../../engine/render/TerrainRenderer.js";
import { resetScene } from "../../engine/scene/activeScene.js";
import type { TerrainField } from "../../engine/world/scalarField.js";
import { TerrainChunkStreamer } from "./TerrainChunkStreamer.js";

describe("TerrainChunkStreamer", () => {
  it("generates a render chunk around the target entity", () => {
    const scene = resetScene();
    const target = scene.createEntity("Player");
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      target,
      material: "material:terrain",
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(target.transform.getWorldPosition());

    equal(streamer.getLoadedChunkKeys().join(","), "0,0,0");
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,0,0");
    equal(terrain.chunks[0].material, "material:terrain");
    equal(terrain.chunks[0].mesh.id, "mesh:terrain.chunk:0,0,0");
    ok(terrain.chunks[0].mesh.indices.length > 0);
  });

  it("generates square xz neighborhoods for every requested vertical chunk offset", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 1,
      verticalChunkOffsets: [-1, 0]
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(streamer.getLoadedChunkKeys().length, 18);
    equal(streamer.getLoadedChunkKeys().includes("-1,0,-1"), true);
    equal(streamer.getLoadedChunkKeys().includes("1,-1,1"), true);
    equal(terrain.chunks.length, 9);
    equal(terrain.chunks.every((chunk) => streamer.getLoadedChunkKeys().includes(chunk.key)), true);
    equal(terrain.chunks.some((chunk) => chunk.key === "0,0,0"), true);
  });

  it("moves the loaded chunk window as the target crosses chunk boundaries", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));
    const firstMesh = terrain.chunks[0].mesh;
    streamer.syncAround(vec3(1, 0, 1));

    equal(terrain.chunks[0].mesh, firstMesh);

    streamer.syncAround(vec3(32, 0, 0));

    equal(streamer.getLoadedChunkKeys().join(","), "1,0,0");
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "1,0,0");
    equal(terrain.getChunk("0,0,0"), undefined);
  });

  it("skips render chunks with no surface while remembering they were loaded", () => {
    let sampleCount = 0;
    const source: TerrainField = {
      heightAt: () => 0,
      densityAt: () => {
        sampleCount += 1;
        return 1;
      },
      normalAt: () => vec3(0, 1, 0)
    };
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));
    streamer.syncAround(vec3(1, 0, 1));

    equal(streamer.getLoadedChunkKeys().join(","), "0,0,0");
    equal(terrain.chunks.length, 0);
    equal(sampleCount, 33 * 33 * 33);
  });

  it("uses terrain density sample gradients when building chunk meshes", () => {
    const source: TerrainField = {
      heightAt: () => 0,
      densityAt: (position) => position.y,
      sampleAt: (position) => ({
        density: position.y,
        gradient: vec3(0, 0, 2)
      }),
      normalAt: () => vec3(0, 1, 0)
    };
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].mesh.vertices[6], 0);
    equal(terrain.chunks[0].mesh.vertices[7], 0);
    equal(terrain.chunks[0].mesh.vertices[8], 1);
  });

  it("uses stable centroid placement for runtime Dual Contouring vertices", () => {
    const source: TerrainField = {
      heightAt: () => 0.5,
      densityAt: (position) => position.y - 0.5,
      sampleAt: (position) => ({
        density: position.y - 0.5,
        gradient: stressNormalForPlacement(position)
      }),
      normalAt: () => vec3(0, 1, 0)
    };
    const terrain = new TerrainRenderer(source);
    const streamer = new TerrainChunkStreamer(terrain, source, {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });

    streamer.syncAround(vec3(0, 0, 0));

    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].mesh.vertices[0], 0.5);
    equal(terrain.chunks[0].mesh.vertices[1], 0.5);
    equal(terrain.chunks[0].mesh.vertices[2], 0.5);
  });

  it("can rebuild an already loaded chunk", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    });
    streamer.syncAround(vec3(0, 0, 0));
    const firstMesh = terrain.chunks[0].mesh;

    streamer.rebuildChunk("0,0,0");

    equal(streamer.getLoadedChunkKeys().join(","), "0,0,0");
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,0,0");
    ok(terrain.chunks[0].mesh !== firstMesh);
  });

  it("can invalidate all streamed chunks", () => {
    const terrain = new TerrainRenderer(createFlatField(0));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: 1,
      verticalChunkOffsets: [0]
    });
    streamer.syncAround(vec3(0, 0, 0));

    streamer.invalidateAll();

    equal(streamer.getLoadedChunkKeys().length, 0);
    equal(terrain.chunks.length, 0);
  });

  it("updates from scene traversal when attached as a component", () => {
    const scene = resetScene();
    const target = scene.createEntity("Player");
    const terrainEntity = scene.createEntity("Terrain");
    const terrain = terrainEntity.addComponent(new TerrainRenderer(createFlatField(0)));
    terrainEntity.addComponent(new TerrainChunkStreamer(terrain, createFlatField(0), {
      target,
      horizontalRadius: 0,
      verticalChunkOffsets: [0]
    }));

    scene.update(0);

    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,0,0");
  });

  it("validates streaming options", () => {
    const terrain = new TerrainRenderer(createFlatField(0));

    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      horizontalRadius: -1
    }), /horizontalRadius/);
    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      verticalChunkOffsets: []
    }), /verticalChunkOffsets/);
    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      verticalChunkOffsets: [0, 0]
    }), /verticalChunkOffsets/);
    throws(() => new TerrainChunkStreamer(terrain, createFlatField(0), {
      cellSize: 0
    }), /cellSize/);
  });

  it("centers vertical chunk offsets on the target y coordinate", () => {
    const terrain = new TerrainRenderer(createFlatField(-70));
    const streamer = new TerrainChunkStreamer(terrain, createFlatField(-70), {
      horizontalRadius: 0,
      verticalChunkOffsets: [-1, 0, 1]
    });

    streamer.syncAround(vec3(0, -70, 0));

    equal(streamer.getLoadedChunkKeys().length, 3);
    equal(streamer.getLoadedChunkKeys().includes("0,-3,0"), true);
    equal(terrain.chunks.length, 1);
    equal(terrain.chunks[0].key, "0,-3,0");
    ok(terrain.chunks[0].mesh.indices.length > 0);
  });
});

function createFlatField(height: number): TerrainField {
  return {
    heightAt: () => height,
    densityAt: (position) => position.y - height,
    normalAt: () => vec3(0, 1, 0)
  };
}

function stressNormalForPlacement(position: { readonly x: number; readonly z: number }) {
  if (position.x < 0.5 && position.z < 0.5) {
    return vec3(1, 0, 0);
  }

  if (position.z > 0.5) {
    return vec3(0, 0, 1);
  }

  return vec3(0, 1, 0);
}
