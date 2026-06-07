import { deepEqual, equal } from "node:assert/strict";
import type { BrowserFrameInput, RustBrowserGameCommand } from "./browserGameTypes.js";
import {
  RustBrowserGameRuntime,
  terrainPresetToWasmCode,
  type RustBrowserGameRenderer
} from "./rustBrowserGameRuntime.js";

describe("RustBrowserGameRuntime", () => {
  it("maps browser terrain preset ids to Rust reset-game preset codes", () => {
    equal(terrainPresetToWasmCode("seed"), 0);
    equal(terrainPresetToWasmCode("rollingHills"), 1);
    equal(terrainPresetToWasmCode("mountainValley"), 2);
    equal(terrainPresetToWasmCode("rockyHighland"), 3);
  });

  it("delegates frame input, commands, and snapshots to Rust", () => {
    const renderer = fakeRenderer();
    const runtime = new RustBrowserGameRuntime({ renderer });
    const frame: BrowserFrameInput = {
      deltaSeconds: 0.125,
      movement: {
        forward: 1,
        right: -1,
        up: 0,
        fast: true
      },
      look: {
        deltaX: 2,
        deltaY: -3
      }
    };

    runtime.tick(frame);
    runtime.command({ type: "resetStreaming" });
    runtime.command({ type: "setPlayerPosition", x: 32, z: 16 });
    const snapshot = runtime.debugSnapshot();

    deepEqual(renderer.tickCalls[0], frame);
    deepEqual(renderer.commandCalls[0], { type: "resetStreaming" });
    deepEqual(renderer.commandCalls[1], { type: "setPlayerPosition", x: 32, z: 16 });
    equal(snapshot.terrainStreamStatus.workerPoolRuntime, "rust");
    equal(snapshot.rendererStatus.runtime, "rust-wgpu");
    equal(snapshot.playerMode, "debugFly");
    deepEqual(snapshot.playerPosition, { x: 32, y: 8, z: 16 });
  });
});

type FakeRenderer = RustBrowserGameRenderer & {
  readonly tickCalls: BrowserFrameInput[];
  readonly commandCalls: RustBrowserGameCommand[];
};

function fakeRenderer(): FakeRenderer {
  return {
    runtime: "rust-wgpu",
    tickCalls: [],
    commandCalls: [],
    tick(frame) {
      this.tickCalls.push(frame);
    },
    command(command) {
      this.commandCalls.push(command);
    },
    getDebugSnapshot() {
      return {
        playerMode: "debugFly",
        playerPosition: { x: 32, y: 8, z: 16 },
        loadedTerrainChunkKeys: ["0,0,0"],
        terrainChunkKeys: ["0,0,0"],
        terrainPreset: "rollingHills",
        terrainSeed: 0x0F6,
        terrainStreamStatus: {
          generation: 1,
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
    }
  };
}
