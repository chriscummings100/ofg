import { equal, ok } from "node:assert/strict";
import type { RenderMeshPacket } from "../render/RenderPackets.js";
import type { TerrainRenderSource } from "../render/TerrainCoreRenderPackets.js";
import type { RgbaTextureArray } from "../render/textureLoader.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

describe("RustBrowserGameAdapter", () => {
  it("uploads mesh and texture bytes then renders item IDs through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const mesh = fakeMeshPacket("mesh:test");
    const terrainTextures = fakeTerrainTextures();
    const snapshot = sampleEngineRenderSnapshot();

    withFakeWindow(() => adapter.renderEngineFrame(
      snapshot,
      fakeTerrainRenderSource(mesh, terrainTextures)
    ));

    equal(fake.upsertedMeshes.length, 1);
    equal(fake.upsertedMeshes[0]?.id, "mesh:test");
    equal(fake.upsertedMeshes[0]?.floatsPerVertex, 19);
    equal(fake.upsertedTerrainTextures.length, 1);
    equal(fake.upsertedTerrainTextures[0]?.width, 1);
    equal(fake.upsertedTerrainTextures[0]?.layers, 1);
    equal(fake.upsertedTerrainTextures[0]?.formatCode, 1);
    equal(fake.upsertedTerrainTextures[0]?.albedoData[0], 255);
    equal(fake.upsertedTerrainTextures[0]?.normalData[1], 255);
    equal(fake.upsertedTerrainTextures[0]?.materialData[2], 255);
    equal(fake.lastRender?.engineSnapshot[0], 1);
    equal(fake.lastRender?.aspect, 640 / 480);
    equal(fake.lastRender?.itemIds[0], "terrain:test:0,0,0");
    equal(fake.lastRender?.meshIds[0], "mesh:test");
    almostEqual(fake.lastRender?.worldMatrices[0], 1);
  });

  it("destroys uploaded meshes that disappear from render packets", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake) as unknown as {
      uploadedMeshes: Map<string, RenderMeshPacket>;
      pruneUploadedMeshes(seenMeshes: Set<RenderMeshPacket>): void;
    };
    const keptMesh = fakeMeshPacket("mesh:kept");
    const goneMesh = fakeMeshPacket("mesh:gone");

    adapter.uploadedMeshes.set(keptMesh.id, keptMesh);
    adapter.uploadedMeshes.set(goneMesh.id, goneMesh);

    adapter.pruneUploadedMeshes(new Set([keptMesh]));

    equal(adapter.uploadedMeshes.has(keptMesh.id), true);
    equal(adapter.uploadedMeshes.has(goneMesh.id), false);
    equal(fake.destroyedMeshes.join(","), "mesh:gone");
  });
});

type FakeBrowserGame = EngineWebBrowserGame & {
  upsertedMeshes: {
    readonly id: string;
    readonly floatsPerVertex: number;
  }[];
  upsertedTerrainTextures: {
    readonly width: number;
    readonly layers: number;
    readonly formatCode: number;
    readonly albedoData: Uint8Array;
    readonly normalData: Uint8Array;
    readonly materialData: Uint8Array;
  }[];
  destroyedMeshes: string[];
  lastRender?: {
    readonly engineSnapshot: Float32Array;
    readonly aspect: number;
    readonly itemIds: string[];
    readonly meshIds: string[];
    readonly worldMatrices: Float32Array;
  };
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
    upsertedMeshes: [],
    upsertedTerrainTextures: [],
    destroyedMeshes: [],
    resize() {},
    upsertMesh(id, _vertices, _indices, floatsPerVertex) {
      this.upsertedMeshes.push({ id, floatsPerVertex });
    },
    destroyMesh(id) {
      this.destroyedMeshes.push(id);
    },
    upsertTerrainTextures(width, _height, layers, formatCode, albedoData, normalData, materialData) {
      this.upsertedTerrainTextures.push({
        width,
        layers,
        formatCode,
        albedoData,
        normalData,
        materialData
      });
    },
    renderEngineFrame(
      engineSnapshot,
      aspect,
      itemIds,
      meshIds,
      worldMatrices
    ) {
      this.lastRender = {
        engineSnapshot: new Float32Array(engineSnapshot),
        aspect,
        itemIds: [...itemIds],
        meshIds: [...meshIds],
        worldMatrices: new Float32Array(worldMatrices)
      };
    },
    status() {
      return {
        version: 1,
        runtime: "rust-wgpu",
        configured: true,
        canvasWidth: 640,
        canvasHeight: 480,
        maxTextureArrayLayers: 16,
        requiredTextureArrayLayers: 16,
        meshCount: 1,
        textureCount: 3,
        objectCount: 1,
        frameIndex: 1,
        frameDrawCount: 1
      };
    }
  };
}

function fakeTerrainRenderSource(
  mesh: RenderMeshPacket,
  terrainTextures: TerrainMaterialTextures
): TerrainRenderSource {
  return {
    itemIdPrefix: "terrain:test",
    terrainTextures,
    chunks: [
      {
        key: "0,0,0",
        mesh
      }
    ]
  };
}

function fakeTerrainTextures(): TerrainMaterialTextures {
  return {
    albedo: fakeTextureArray([255, 0, 0, 255]),
    normal: fakeTextureArray([0, 255, 0, 255]),
    material: fakeTextureArray([0, 0, 255, 255])
  };
}

function fakeTextureArray(bytes: readonly number[]): RgbaTextureArray {
  return {
    width: 1,
    height: 1,
    layers: 1,
    data: new Uint8Array(bytes)
  };
}

function fakeMeshPacket(id: string): RenderMeshPacket {
  return {
    id,
    vertices: new Float32Array(19 * 3),
    indices: new Uint32Array([0, 1, 2]),
    floatsPerVertex: 19
  };
}

function sampleEngineRenderSnapshot(): Float32Array {
  return new Float32Array([
    1, 2, 3,
    1, 2, 2,
    0, 0,
    70 * Math.PI / 180,
    0.05,
    500,
    0, 1, 0,
    1, 1, 1,
    2,
    0.2,
    1,
    1, 2, 3,
    0
  ]);
}

function fakeCanvas(): HTMLCanvasElement {
  return {
    clientWidth: 640,
    clientHeight: 480,
    width: 0,
    height: 0
  } as HTMLCanvasElement;
}

function withFakeWindow(action: () => void): void {
  const globalWithWindow = globalThis as unknown as {
    window?: { devicePixelRatio: number };
  };
  const previousWindow = globalWithWindow.window;
  globalWithWindow.window = { devicePixelRatio: 1 };
  try {
    action();
  } finally {
    globalWithWindow.window = previousWindow;
  }
}

function almostEqual(actual: number | undefined, expected: number): void {
  if (actual === undefined) {
    ok(false, "Expected a numeric value.");
    return;
  }
  ok(Math.abs(actual - expected) < 0.00001, `Expected ${actual} to be close to ${expected}.`);
}
