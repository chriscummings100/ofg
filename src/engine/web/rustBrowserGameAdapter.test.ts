import { equal } from "node:assert/strict";
import type { TerrainRenderMeshPacket } from "../render/terrainRenderChunkSink.js";
import type { RgbaTextureArray } from "../render/textureLoader.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type { BrowserFrameInput } from "./browserGameTypes.js";
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
    adapter.tick({
      deltaSeconds: 0.25,
      movement: {
        forward: 1,
        right: -1,
        up: 0,
        fast: true
      },
      look: {
        deltaX: 3,
        deltaY: -2
      }
    });
    adapter.command({ type: "togglePlayerMode" });
    adapter.command({ type: "setPlayerMode", mode: "firstPerson" });
    adapter.command({ type: "setPlayerPosition", x: 96, z: 12 });
    adapter.command({ type: "setDebugCamera", x: 1, y: 2, z: 3, yaw: 0.25, pitch: -0.5 });
    const snapshot = adapter.getDebugSnapshot();

    equal(fake.resetGameCalls[0]?.terrainSeed, 0x0F6);
    equal(fake.resetGameCalls[0]?.terrainPreset, 1);
    equal(fake.tickCalls[0]?.deltaSeconds, 0.25);
    equal(fake.tickCalls[0]?.movement.forward, 1);
    equal(fake.tickCalls[0]?.movement.right, -1);
    equal(fake.tickCalls[0]?.movement.fast, true);
    equal(fake.commandCalls[0]?.type, "togglePlayerMode");
    equal(fake.commandCalls[1]?.type, "setPlayerMode");
    equal(fake.commandCalls[2]?.type, "setPlayerPosition");
    equal(fake.commandCalls[3]?.type, "setDebugCamera");
    equal(snapshot.playerMode, "firstPerson");
    equal(snapshot.playerPosition.x, 96);
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
  tickCalls: BrowserFrameInput[];
  commandCalls: Array<Parameters<EngineWebBrowserGame["command"]>[0]>;
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
    commandCalls: [],
    resize() {},
    resetGame(terrainSeed, terrainPreset) {
      this.resetGameCalls.push({ terrainSeed, terrainPreset });
    },
    tick(frame) {
      this.tickCalls.push(frame);
    },
    command(command) {
      this.commandCalls.push(command);
    },
    debugSnapshot() {
      return {
        playerMode: "firstPerson",
        playerPosition: {
          x: 96,
          y: 7,
          z: 12
        }
      };
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
