import { deepEqual, equal } from "node:assert/strict";
import type { TerrainMaterialTextures } from "../render/terrainTextures.js";
import type { EngineWebRendererStatus } from "./engineWebWasm.js";
import {
  RustBrowserGameRuntime,
  type RustBrowserGameRenderer,
  type TerrainWorkerStreamer
} from "./rustBrowserGameRuntime.js";
import { createSeedWorldDescriptor } from "../world/terrainDescriptor.js";
import type { Vec3 } from "../math/vec3.js";
import type { BrowserFrameInput, RustBrowserGameCommand } from "./browserGameTypes.js";

describe("RustBrowserGameRuntime", () => {
  it("coordinates Rust ticking, terrain streaming, rendering, and debug hooks", () => {
    const renderer = fakeRenderer();
    const streamer = fakeStreamer();
    const runtime = new RustBrowserGameRuntime({
      descriptor: createSeedWorldDescriptor(0x0F6, { terrainPreset: "rockyHighland" }),
      renderer,
      terrainStreamer: streamer,
      terrainWorker: {
        workerCount: 4,
        workerPoolRuntime: "rust"
      },
      terrainDensityChunkStore: {
        runtime: "rust"
      },
      terrainHeightAt: (x, z) => x - z
    });
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
    runtime.command({ type: "setPlayerPosition", x: 32, z: 16 });
    runtime.command({ type: "resetStreaming" });
    runtime.command({ type: "setPlayerMode", mode: "debugFly" });
    runtime.command({ type: "setDebugCamera", x: 1, y: 2, z: 3, yaw: 0.5, pitch: -0.25 });
    runtime.command({ type: "togglePlayerMode" });
    const snapshot = runtime.debugSnapshot();

    deepEqual(renderer.tickCalls[0], frame);
    equal(streamer.updateCount, 1);
    equal(renderer.renderCount, 1);
    deepEqual(renderer.commandCalls[0], { type: "setPlayerPosition", x: 32, z: 16 });
    deepEqual(streamer.syncCenters[0], { x: 32, y: 8, z: 16 });
    deepEqual(streamer.resetCenters[0], { x: 32, y: 8, z: 16 });
    deepEqual(renderer.commandCalls[1], { type: "setPlayerMode", mode: "debugFly" });
    deepEqual(renderer.commandCalls[2], {
      type: "setDebugCamera",
      x: 1,
      y: 2,
      z: 3,
      yaw: 0.5,
      pitch: -0.25
    });
    deepEqual(renderer.commandCalls[3], { type: "togglePlayerMode" });
    deepEqual(snapshot.loadedTerrainChunkKeys, ["0,0,0"]);
    deepEqual(snapshot.terrainChunkKeys, ["0,0,0", "1,0,0"]);
    equal(snapshot.terrainPreset, "rockyHighland");
    equal(snapshot.terrainSeed, 0x0F6);
    equal(snapshot.terrainStreamStatus.workerPoolRuntime, "rust");
    equal(snapshot.terrainStreamerRuntime, "rust");
    equal(snapshot.terrainDensityStoreRuntime, "rust");
    equal(snapshot.terrainWorkerPoolRuntime, "rust");
    equal(snapshot.terrainWorkerCount, 4);
    equal(runtime.getTerrainHeight(10, 4), 6);
    equal(snapshot.rendererStatus.runtime, "rust-wgpu");
    equal(snapshot.playerMode, "debugFly");
    deepEqual(snapshot.playerPosition, { x: 32, y: 8, z: 16 });
  });
});

type FakeRenderer = RustBrowserGameRenderer & {
  readonly tickCalls: BrowserFrameInput[];
  readonly commandCalls: RustBrowserGameCommand[];
  renderCount: number;
};

function fakeRenderer(): FakeRenderer {
  const renderer: FakeRenderer = {
    runtime: "rust-wgpu",
    tickCalls: [],
    commandCalls: [],
    renderCount: 0,
    setTerrainTextures(_textures: TerrainMaterialTextures) {},
    tick(frame) {
      this.tickCalls.push(frame);
    },
    renderFrame() {
      this.renderCount += 1;
    },
    command(command) {
      this.commandCalls.push(command);
    },
    getDebugSnapshot() {
      return {
        playerMode: "debugFly",
        playerPosition: { x: 32, y: 8, z: 16 }
      };
    },
    getStatus() {
      return fakeRendererStatus();
    },
    addChunk() {},
    removeChunk() {
      return false;
    },
    clear() {},
    retainChunks() {},
    chunkKeys() {
      return ["0,0,0", "1,0,0"];
    }
  };

  return renderer;
}

type FakeStreamer = TerrainWorkerStreamer & {
  updateCount: number;
  readonly syncCenters: Vec3[];
  readonly resetCenters: Vec3[];
};

function fakeStreamer(): FakeStreamer {
  return {
    runtime: "rust",
    updateCount: 0,
    syncCenters: [],
    resetCenters: [],
    syncAround(center) {
      this.syncCenters.push(center);
    },
    update() {
      this.updateCount += 1;
    },
    resetStreaming(center) {
      if (center !== undefined) {
        this.resetCenters.push(center);
      }
    },
    getLoadedChunkKeys() {
      return ["0,0,0"];
    },
    getStreamStatus() {
      return {
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
        maxConcurrentChunkJobs: 4,
        workerPoolRuntime: "rust"
      };
    }
  };
}

function fakeRendererStatus(): EngineWebRendererStatus {
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
