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
import type { BrowserFrameInput } from "./browserGameTypes.js";

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
    runtime.renderFrame();
    runtime.command({ type: "setPlayerPosition", x: 32, z: 16 });
    runtime.command({ type: "resetStreaming" });
    runtime.command({ type: "setPlayerMode", mode: "debugFly" });
    runtime.command({ type: "setDebugCamera", x: 1, y: 2, z: 3, yaw: 0.5, pitch: -0.25 });
    runtime.command({ type: "togglePlayerMode" });
    const snapshot = runtime.debugSnapshot();

    deepEqual(renderer.tickCalls[0], frame);
    equal(streamer.updateCount, 1);
    equal(renderer.renderCount, 1);
    deepEqual(renderer.setPlayerPositionCalls[0], { x: 32, z: 16 });
    deepEqual(streamer.syncCenters[0], { x: 32, y: 8, z: 16 });
    deepEqual(streamer.resetCenters[0], { x: 32, y: 8, z: 16 });
    equal(renderer.setPlayerModes.join(","), "debugFly");
    deepEqual(renderer.debugCameraCalls[0], {
      position: { x: 1, y: 2, z: 3 },
      yaw: 0.5,
      pitch: -0.25
    });
    equal(renderer.toggleCount, 1);
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
  readonly setPlayerPositionCalls: Array<{ readonly x: number; readonly z: number }>;
  readonly setPlayerModes: string[];
  readonly debugCameraCalls: Array<{
    readonly position: Vec3;
    readonly yaw: number;
    readonly pitch: number;
  }>;
  renderCount: number;
  toggleCount: number;
};

function fakeRenderer(): FakeRenderer {
  const renderer: FakeRenderer = {
    runtime: "rust-wgpu",
    tickCalls: [],
    setPlayerPositionCalls: [],
    setPlayerModes: [],
    debugCameraCalls: [],
    renderCount: 0,
    toggleCount: 0,
    setTerrainTextures(_textures: TerrainMaterialTextures) {},
    resetGame(_terrainSeed: number, _terrainPreset: number) {},
    tick(frame) {
      this.tickCalls.push(frame);
    },
    renderGameFrame() {
      this.renderCount += 1;
    },
    toggleCameraMode() {
      this.toggleCount += 1;
      return "firstPerson";
    },
    getPlayerMode() {
      return "debugFly";
    },
    setPlayerMode(mode) {
      this.setPlayerModes.push(mode);
    },
    getPlayerPosition() {
      return { x: 32, y: 8, z: 16 };
    },
    setPlayerPosition(x, z) {
      this.setPlayerPositionCalls.push({ x, z });
    },
    setDebugCamera(position, yaw, pitch) {
      this.debugCameraCalls.push({ position, yaw, pitch });
    },
    getStatus() {
      return fakeRendererStatus();
    },
    addChunk() {},
    getChunk() {
      return undefined;
    },
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
        densityTransferMode: "shared",
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
