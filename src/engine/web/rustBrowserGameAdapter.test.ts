import { equal } from "node:assert/strict";
import type { TerrainRenderMeshPacket } from "../render/TerrainCoreRenderPackets.js";
import type { RgbaTextureArray } from "../render/textureLoader.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

describe("RustBrowserGameAdapter", () => {
  it("uploads terrain texture bytes and renders through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const terrainTextures = fakeTerrainTextures();
    const snapshot = sampleEngineRenderSnapshot();

    adapter.setTerrainTextures(terrainTextures);
    withFakeWindow(() => adapter.renderEngineFrame(snapshot));

    equal(fake.upsertedTerrainTextures.length, 1);
    equal(fake.upsertedTerrainTextures[0]?.width, 1);
    equal(fake.upsertedTerrainTextures[0]?.layers, 1);
    equal(fake.upsertedTerrainTextures[0]?.formatCode, 1);
    equal(fake.upsertedTerrainTextures[0]?.albedoData[0], 255);
    equal(fake.upsertedTerrainTextures[0]?.normalData[1], 255);
    equal(fake.upsertedTerrainTextures[0]?.materialData[2], 255);
    equal(fake.lastRender?.engineSnapshot[0], 1);
    equal(fake.lastRender?.aspect, 640 / 480);
  });

  it("acts as a terrain chunk sink over the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const keptMesh = fakeMeshPacket();
    const goneMesh = fakeMeshPacket();

    adapter.addChunk({ key: "kept", mesh: keptMesh });
    adapter.addChunk({ key: "gone", ...goneMesh });
    adapter.retainChunks(["kept"]);
    adapter.removeChunk("gone");
    adapter.clear();

    equal(fake.upsertedTerrainMeshes.length, 2);
    equal(fake.upsertedTerrainMeshes[0]?.chunkKey, "kept");
    equal(fake.upsertedTerrainMeshes[1]?.chunkKey, "gone");
    equal(fake.retainedTerrainMeshSets.length, 1);
    equal(fake.retainedTerrainMeshSets[0]?.join(","), "kept");
    equal(fake.destroyedTerrainMeshes.join(","), "gone");
    equal(fake.clearedTerrainMeshes, 1);
  });
});

type FakeBrowserGame = EngineWebBrowserGame & {
  upsertedTerrainMeshes: {
    readonly chunkKey: string;
  }[];
  upsertedTerrainTextures: {
    readonly width: number;
    readonly layers: number;
    readonly formatCode: number;
    readonly albedoData: Uint8Array;
    readonly normalData: Uint8Array;
    readonly materialData: Uint8Array;
  }[];
  destroyedTerrainMeshes: string[];
  retainedTerrainMeshSets: string[][];
  clearedTerrainMeshes: number;
  lastRender?: {
    readonly engineSnapshot: Float32Array;
    readonly aspect: number;
  };
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
    upsertedTerrainMeshes: [],
    upsertedTerrainTextures: [],
    destroyedTerrainMeshes: [],
    retainedTerrainMeshSets: [],
    clearedTerrainMeshes: 0,
    resize() {},
    upsertTerrainMesh(chunkKey) {
      this.upsertedTerrainMeshes.push({ chunkKey });
    },
    destroyTerrainMesh(chunkKey) {
      this.destroyedTerrainMeshes.push(chunkKey);
    },
    retainTerrainMeshes(chunkKeys) {
      this.retainedTerrainMeshSets.push([...chunkKeys]);
    },
    clearTerrainMeshes() {
      this.clearedTerrainMeshes += 1;
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
      aspect
    ) {
      this.lastRender = {
        engineSnapshot: new Float32Array(engineSnapshot),
        aspect
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

function fakeMeshPacket(): TerrainRenderMeshPacket {
  return {
    vertices: new Float32Array(19 * 3),
    indices: new Uint32Array([0, 1, 2])
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
