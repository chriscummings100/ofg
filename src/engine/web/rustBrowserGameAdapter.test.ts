import { equal } from "node:assert/strict";
import type {
  TerrainRenderMeshPacket,
  TerrainRenderSource
} from "../render/TerrainCoreRenderPackets.js";
import type { RgbaTextureArray } from "../render/textureLoader.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type { TerrainChunkKey } from "../world/terrainChunk.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

describe("RustBrowserGameAdapter", () => {
  it("uploads terrain chunk mesh and texture bytes then renders chunk keys through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const mesh = fakeMeshPacket();
    const terrainTextures = fakeTerrainTextures();
    const snapshot = sampleEngineRenderSnapshot();

    withFakeWindow(() => adapter.renderEngineFrame(
      snapshot,
      fakeTerrainRenderSource(mesh, terrainTextures)
    ));

    equal(fake.upsertedTerrainMeshes.length, 1);
    equal(fake.upsertedTerrainMeshes[0]?.chunkKey, "0,0,0");
    equal(fake.upsertedTerrainMeshes[0]?.floatsPerVertex, 19);
    equal(fake.upsertedTerrainTextures.length, 1);
    equal(fake.upsertedTerrainTextures[0]?.width, 1);
    equal(fake.upsertedTerrainTextures[0]?.layers, 1);
    equal(fake.upsertedTerrainTextures[0]?.formatCode, 1);
    equal(fake.upsertedTerrainTextures[0]?.albedoData[0], 255);
    equal(fake.upsertedTerrainTextures[0]?.normalData[1], 255);
    equal(fake.upsertedTerrainTextures[0]?.materialData[2], 255);
    equal(fake.lastRender?.engineSnapshot[0], 1);
    equal(fake.lastRender?.aspect, 640 / 480);
    equal(fake.lastRender?.chunkKeys[0], "0,0,0");
  });

  it("destroys uploaded terrain meshes that disappear from render packets", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake) as unknown as {
      uploadedTerrainMeshes: Map<TerrainChunkKey, TerrainRenderMeshPacket>;
      pruneUploadedTerrainMeshes(seenChunkKeys: Set<TerrainChunkKey>): void;
    };
    const keptMesh = fakeMeshPacket();
    const goneMesh = fakeMeshPacket();

    adapter.uploadedTerrainMeshes.set("kept", keptMesh);
    adapter.uploadedTerrainMeshes.set("gone", goneMesh);

    adapter.pruneUploadedTerrainMeshes(new Set(["kept"]));

    equal(adapter.uploadedTerrainMeshes.has("kept"), true);
    equal(adapter.uploadedTerrainMeshes.has("gone"), false);
    equal(fake.destroyedTerrainMeshes.join(","), "gone");
  });
});

type FakeBrowserGame = EngineWebBrowserGame & {
  upsertedTerrainMeshes: {
    readonly chunkKey: string;
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
  destroyedTerrainMeshes: string[];
  lastRender?: {
    readonly engineSnapshot: Float32Array;
    readonly aspect: number;
    readonly chunkKeys: string[];
  };
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
    upsertedTerrainMeshes: [],
    upsertedTerrainTextures: [],
    destroyedTerrainMeshes: [],
    resize() {},
    upsertTerrainMesh(chunkKey, _vertices, _indices, floatsPerVertex) {
      this.upsertedTerrainMeshes.push({ chunkKey, floatsPerVertex });
    },
    destroyTerrainMesh(chunkKey) {
      this.destroyedTerrainMeshes.push(chunkKey);
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
      chunkKeys
    ) {
      this.lastRender = {
        engineSnapshot: new Float32Array(engineSnapshot),
        aspect,
        chunkKeys: [...chunkKeys]
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
  mesh: TerrainRenderMeshPacket,
  terrainTextures: TerrainMaterialTextures
): TerrainRenderSource {
  return {
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

function fakeMeshPacket(): TerrainRenderMeshPacket {
  return {
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
