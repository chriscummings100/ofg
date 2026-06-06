import { InputTracker } from "../engine/input/inputTracker.js";
import { computeFrameDeltaSeconds } from "./frameTiming.js";
import type { EngineWebRendererStatus } from "../engine/web/engineWebWasm.js";
import {
  createRustBrowserGameRuntime,
  type RustBrowserGameRuntime
} from "../engine/web/rustBrowserGameRuntime.js";
import {
  createSeedWorldDescriptor,
  isTerrainPresetId,
  type TerrainPresetId,
  type WorldDescriptor
} from "../engine/world/terrainDescriptor.js";
import {
  type BrowserFrameInput,
  type PlayerMode
} from "../engine/web/browserGameTypes.js";

type GameElements = {
  readonly canvas: HTMLCanvasElement;
  readonly cameraMode: HTMLElement;
  readonly frameTime: HTMLElement;
};

declare global {
  interface Window {
    __ofgDebug?: {
      getLoadedTerrainChunkKeys: () => string[];
      getTerrainChunkKeys: () => string[];
      getTerrainPreset: () => TerrainPresetId;
      getTerrainSeed: () => number;
      getTerrainStreamStatus: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["terrainStreamStatus"];
      getTerrainStreamerRuntime: () => "rust";
      getTerrainStreamSchedulerRuntime: () => "rust";
      getTerrainDensityStoreRuntime: () => "rust";
      getTerrainWorkerPoolRuntime: () => "rust";
      getRenderPacketRuntime: () => "rust";
      getTerrainRenderPacketRuntime: () => "rust";
      getRendererRuntime: () => "rust-wgpu";
      getRendererStatus: () => EngineWebRendererStatus;
      getTerrainWorkerCount: () => number;
      getPlayerControllerRuntime: () => "rust";
      getPlayerPosition: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["playerPosition"];
      resetTerrainStreaming: () => void;
      setCameraMode: (mode: PlayerMode) => void;
      setDebugCamera: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
      setPlayerPosition: (x: number, z: number) => void;
    };
  }
}

export async function startGame(elements: GameElements): Promise<void> {
  const input = new InputTracker();
  const descriptor = readWorldDescriptor();
  const game = await createRustBrowserGameRuntime(elements.canvas, descriptor);
  window.__ofgDebug = {
    getLoadedTerrainChunkKeys: () => game.debugSnapshot().loadedTerrainChunkKeys,
    getTerrainChunkKeys: () => game.debugSnapshot().terrainChunkKeys,
    getTerrainPreset: () => game.debugSnapshot().terrainPreset,
    getTerrainSeed: () => game.debugSnapshot().terrainSeed,
    getTerrainStreamStatus: () => game.debugSnapshot().terrainStreamStatus,
    getTerrainStreamerRuntime: () => game.debugSnapshot().terrainStreamerRuntime,
    getTerrainStreamSchedulerRuntime: () => game.debugSnapshot().terrainStreamSchedulerRuntime,
    getTerrainDensityStoreRuntime: () => game.debugSnapshot().terrainDensityStoreRuntime,
    getTerrainWorkerPoolRuntime: () => game.debugSnapshot().terrainWorkerPoolRuntime,
    getRenderPacketRuntime: () => game.debugSnapshot().renderPacketRuntime,
    getTerrainRenderPacketRuntime: () => game.debugSnapshot().terrainRenderPacketRuntime,
    getRendererRuntime: () => game.debugSnapshot().rendererRuntime,
    getRendererStatus: () => game.debugSnapshot().rendererStatus,
    getTerrainWorkerCount: () => game.debugSnapshot().terrainWorkerCount,
    getPlayerControllerRuntime: () => game.debugSnapshot().playerControllerRuntime,
    getPlayerPosition: () => game.debugSnapshot().playerPosition,
    resetTerrainStreaming() {
      game.command({ type: "resetStreaming" });
    },
    setCameraMode(mode) {
      game.command({ type: "setPlayerMode", mode: validatePlayerMode(mode) });
    },
    setDebugCamera(x, y, z, yaw, pitch) {
      game.command({ type: "setDebugCamera", x, y, z, yaw, pitch });
    },
    setPlayerPosition(x, z) {
      game.command({ type: "setPlayerPosition", x, z });
    }
  };

  input.attach(elements.canvas);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const deltaSeconds = computeFrameDeltaSeconds(timestamp, lastTimestamp);
    lastTimestamp = timestamp;

    if (input.consumePress("KeyC") || input.consumePress("F1")) {
      game.command({ type: "togglePlayerMode" });
    }

    const snapshot = input.consumeFrameSnapshot();
    const frameInput = readFrameInput(
      input,
      deltaSeconds,
      snapshot.mouseDeltaX,
      snapshot.mouseDeltaY
    );

    game.tick(frameInput);

    const playerMode = game.debugSnapshot().playerMode;
    elements.cameraMode.textContent = playerMode === "firstPerson" ? "FIRST" : "FLY";
    elements.cameraMode.dataset.mode = playerMode;
    elements.frameTime.textContent = `${(deltaSeconds * 1000).toFixed(1)} ms`;

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}

function readWorldDescriptor(): WorldDescriptor {
  const params = new URLSearchParams(window.location.search);
  const terrainPreset = readTerrainPreset(params.get("terrainPreset"));
  const terrainSeed = readTerrainSeed(params.get("terrainSeed"));

  return createSeedWorldDescriptor(
    terrainSeed,
    terrainPreset === undefined ? {} : { terrainPreset }
  );
}

function readTerrainPreset(value: string | null): TerrainPresetId | undefined {
  if (value === null || value.trim() === "") {
    return undefined;
  }

  if (isTerrainPresetId(value)) {
    return value;
  }

  console.warn(`Unknown terrain preset '${value}', using the default preset.`);
  return undefined;
}

function readTerrainSeed(value: string | null): number | undefined {
  if (value === null || value.trim() === "") {
    return undefined;
  }

  const seed = Number(value);
  if (Number.isInteger(seed) && seed >= 0) {
    return seed;
  }

  console.warn(`Invalid terrain seed '${value}', using the default seed.`);
  return undefined;
}

function validatePlayerMode(mode: string): PlayerMode {
  if (mode === "firstPerson" || mode === "debugFly") {
    return mode;
  }

  throw new Error(`Unknown player camera mode '${mode}'.`);
}

function readFrameInput(
  input: InputTracker,
  deltaSeconds: number,
  lookDeltaX: number,
  lookDeltaY: number
): BrowserFrameInput {
  return {
    deltaSeconds,
    movement: {
      forward: axis(input, "KeyW", "KeyS"),
      right: axis(input, "KeyD", "KeyA"),
      up: axis(input, "Space", "ControlLeft"),
      fast: input.isDown("ShiftLeft") || input.isDown("ShiftRight")
    },
    look: {
      deltaX: lookDeltaX,
      deltaY: lookDeltaY
    }
  };
}

function axis(input: InputTracker, positiveCode: string, negativeCode: string): number {
  return Number(input.isDown(positiveCode)) - Number(input.isDown(negativeCode));
}
