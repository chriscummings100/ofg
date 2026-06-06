import { equal } from "node:assert/strict";
import type { TerrainRenderMeshPacket } from "../render/terrainRenderChunkSink.js";
import type { RgbaTextureArray } from "../render/textureLoader.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

describe("RustBrowserGameAdapter", () => {
  it("uploads terrain texture bytes and renders through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const terrainTextures = fakeTerrainTextures();

    adapter.setTerrainTextures(terrainTextures);
    withFakeWindow(() => adapter.renderGameFrame());

    equal(fake.upsertedTerrainTextures.length, 1);
    equal(fake.upsertedTerrainTextures[0]?.width, 1);
    equal(fake.upsertedTerrainTextures[0]?.layers, 1);
    equal(fake.upsertedTerrainTextures[0]?.formatCode, 1);
    equal(fake.upsertedTerrainTextures[0]?.albedoData[0], 255);
    equal(fake.upsertedTerrainTextures[0]?.normalData[1], 255);
    equal(fake.upsertedTerrainTextures[0]?.materialData[2], 255);
    equal(fake.lastRender?.aspect, 640 / 480);
  });

  it("forwards browser frame input and player controls to the Rust game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);

    adapter.resetGame(0x0F6, 1);
    adapter.tick(0.25, {
      forward: 1,
      right: -1,
      up: 0,
      fast: true,
      lookDeltaX: 3,
      lookDeltaY: -2
    });
    equal(adapter.toggleCameraMode(), "debugFly");
    adapter.setPlayerMode("firstPerson");
    adapter.setPlayerPosition(96, 12);
    adapter.setDebugCamera({ x: 1, y: 2, z: 3 }, 0.25, -0.5);

    equal(fake.resetGameCalls[0]?.terrainSeed, 0x0F6);
    equal(fake.resetGameCalls[0]?.terrainPreset, 1);
    equal(fake.tickCalls[0]?.deltaSeconds, 0.25);
    equal(fake.tickCalls[0]?.forward, 1);
    equal(fake.tickCalls[0]?.right, -1);
    equal(fake.tickCalls[0]?.fast, true);
    equal(fake.setPlayerModeCalls.join(","), "0");
    equal(fake.setPlayerPositionCalls[0]?.x, 96);
    equal(fake.setDebugCameraCalls[0]?.z, 3);
    equal(adapter.getPlayerMode(), "firstPerson");
    equal(adapter.getPlayerPosition().x, 96);
  });

  it("acts as a terrain chunk sink over the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const keptMesh = fakeMeshPacket();
    const goneMesh = fakeMeshPacket();

    adapter.addChunk({ key: "kept", mesh: keptMesh });
    adapter.addChunk({ key: "gone", ...goneMesh });
    adapter.retainChunks(["kept"]);
    equal(adapter.chunkKeys().join(","), "kept");
    equal(adapter.removeChunk("gone"), false);
    equal(adapter.removeChunk("kept"), true);
    adapter.clear();

    equal(fake.upsertedTerrainMeshes.length, 2);
    equal(fake.upsertedTerrainMeshes[0]?.chunkKey, "kept");
    equal(fake.upsertedTerrainMeshes[1]?.chunkKey, "gone");
    equal(fake.retainedTerrainMeshSets.length, 1);
    equal(fake.retainedTerrainMeshSets[0]?.join(","), "kept");
    equal(fake.destroyedTerrainMeshes.join(","), "gone,kept");
    equal(fake.clearedTerrainMeshes, 1);
    equal(adapter.chunkKeys().length, 0);
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
  resetGameCalls: {
    readonly terrainSeed: number;
    readonly terrainPreset: number;
  }[];
  tickCalls: {
    readonly deltaSeconds: number;
    readonly forward: number;
    readonly right: number;
    readonly up: number;
    readonly fast: boolean;
    readonly lookDeltaX: number;
    readonly lookDeltaY: number;
  }[];
  setPlayerModeCalls: number[];
  setPlayerPositionCalls: { readonly x: number; readonly z: number }[];
  setDebugCameraCalls: {
    readonly x: number;
    readonly y: number;
    readonly z: number;
    readonly yaw: number;
    readonly pitch: number;
  }[];
  lastRender?: {
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
    resetGameCalls: [],
    tickCalls: [],
    setPlayerModeCalls: [],
    setPlayerPositionCalls: [],
    setDebugCameraCalls: [],
    resize() {},
    resetGame(terrainSeed, terrainPreset) {
      this.resetGameCalls.push({ terrainSeed, terrainPreset });
    },
    tick(deltaSeconds, forward, right, up, fast, lookDeltaX, lookDeltaY) {
      this.tickCalls.push({
        deltaSeconds,
        forward,
        right,
        up,
        fast,
        lookDeltaX,
        lookDeltaY
      });
    },
    togglePlayerMode() {
      return 1;
    },
    playerMode() {
      return 0;
    },
    setPlayerMode(mode) {
      this.setPlayerModeCalls.push(mode);
    },
    playerX() {
      return 96;
    },
    playerY() {
      return 7;
    },
    playerZ() {
      return 12;
    },
    setPlayerPosition(x, z) {
      this.setPlayerPositionCalls.push({ x, z });
    },
    setDebugCamera(x, y, z, yaw, pitch) {
      this.setDebugCameraCalls.push({ x, y, z, yaw, pitch });
    },
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
    renderGameFrame(aspect) {
      this.lastRender = {
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
