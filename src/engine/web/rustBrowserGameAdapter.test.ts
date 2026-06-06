import { equal } from "node:assert/strict";
import type { RgbaTextureArray } from "../render/textureLoader.js";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type { BrowserFrameInput } from "./browserGameTypes.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

describe("RustBrowserGameAdapter", () => {
  it("uploads terrain texture bytes and ticks through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const terrainTextures = fakeTerrainTextures();
    const frame = fakeFrameInput();

    adapter.setTerrainTextures(terrainTextures);
    withFakeWindow(() => adapter.tick(frame));

    equal(fake.upsertedTerrainTextures.length, 1);
    equal(fake.upsertedTerrainTextures[0]?.width, 1);
    equal(fake.upsertedTerrainTextures[0]?.layers, 1);
    equal(fake.upsertedTerrainTextures[0]?.formatCode, 1);
    equal(fake.upsertedTerrainTextures[0]?.albedoData[0], 255);
    equal(fake.upsertedTerrainTextures[0]?.normalData[1], 255);
    equal(fake.upsertedTerrainTextures[0]?.materialData[2], 255);
    equal(fake.tickCalls[0], frame);
    equal(fake.resizeCalls[0]?.width, 640);
    equal(fake.resizeCalls[0]?.height, 480);
  });

  it("forwards browser frame input and player controls to the Rust game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);

    adapter.command({ type: "resetGame", terrainSeed: 0x0F6, terrainPreset: 1 });
    withFakeWindow(() => adapter.tick({
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
    }));
    adapter.command({ type: "togglePlayerMode" });
    adapter.command({ type: "setPlayerMode", mode: "firstPerson" });
    adapter.command({ type: "setPlayerPosition", x: 96, z: 12 });
    adapter.command({ type: "setDebugCamera", x: 1, y: 2, z: 3, yaw: 0.25, pitch: -0.5 });
    const snapshot = adapter.getDebugSnapshot();

    equal(fake.commandCalls[0]?.type, "resetGame");
    equal(fake.tickCalls[0]?.deltaSeconds, 0.25);
    equal(fake.tickCalls[0]?.movement.forward, 1);
    equal(fake.tickCalls[0]?.movement.right, -1);
    equal(fake.tickCalls[0]?.movement.fast, true);
    equal(fake.commandCalls[1]?.type, "togglePlayerMode");
    equal(fake.commandCalls[2]?.type, "setPlayerMode");
    equal(fake.commandCalls[3]?.type, "setPlayerPosition");
    equal(fake.commandCalls[4]?.type, "setDebugCamera");
    equal(snapshot.playerMode, "firstPerson");
    equal(snapshot.playerPosition.x, 96);
    equal(snapshot.loadedTerrainChunkKeys[0], "0,0,0");
    equal(adapter.terrainHeightAt(4, 9), 12);
  });
});

type FakeBrowserGame = EngineWebBrowserGame & {
  upsertedTerrainTextures: {
    readonly width: number;
    readonly layers: number;
    readonly formatCode: number;
    readonly albedoData: Uint8Array;
    readonly normalData: Uint8Array;
    readonly materialData: Uint8Array;
  }[];
  resizeCalls: Array<Parameters<EngineWebBrowserGame["resize"]>[0]>;
  tickCalls: BrowserFrameInput[];
  commandCalls: Array<Parameters<EngineWebBrowserGame["command"]>[0]>;
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
    upsertedTerrainTextures: [],
    resizeCalls: [],
    tickCalls: [],
    commandCalls: [],
    resize(viewport) {
      this.resizeCalls.push(viewport);
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
        },
        loadedTerrainChunkKeys: ["0,0,0"],
        terrainChunkKeys: ["0,0,0"],
        terrainPreset: "rollingHills",
        terrainSeed: 0x0F6,
        terrainStreamStatus: {
          generation: 0,
          pending: false,
          loadedChunkCount: 1,
          densityReadyChunkCount: 1,
          sharedDensityChunkCount: 1,
          inFlightDensityCount: 0,
          missingDensityCount: 0,
          desiredRenderChunkCount: 1,
          renderedChunkCount: 1,
          emptyChunkCount: 0,
          inFlightChunkCount: 0,
          missingChunkCount: 0,
          maxConcurrentChunkJobs: 6,
          workerPoolRuntime: "rust"
        },
        terrainStreamerRuntime: "rust",
        terrainStreamSchedulerRuntime: "rust",
        terrainDensityStoreRuntime: "rust",
        terrainWorkerPoolRuntime: "rust",
        renderPacketRuntime: "rust",
        terrainRenderPacketRuntime: "rust",
        rendererRuntime: "rust-wgpu",
        rendererStatus: {
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
        },
        terrainWorkerCount: 6,
        playerControllerRuntime: "rust"
      };
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
    terrainHeightAt(x, z) {
      return x + z - 1;
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

function fakeFrameInput(): BrowserFrameInput {
  return {
    deltaSeconds: 0.25,
    movement: {
      forward: 0,
      right: 0,
      up: 0,
      fast: false
    },
    look: {
      deltaX: 0,
      deltaY: 0
    }
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
