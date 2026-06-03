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
  type PlayerMode,
  type PlayerMovementIntent
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
      getTerrainStreamStatus: () => ReturnType<RustBrowserGameRuntime["getTerrainStreamStatus"]>;
      getTerrainStreamerRuntime: () => "rust";
      getTerrainStreamSchedulerRuntime: () => "rust";
      getTerrainDensityStoreRuntime: () => "rust";
      getTerrainWorkerPoolRuntime: () => "rust" | "typescript";
      getRenderPacketRuntime: () => "rust" | "typescript";
      getTerrainRenderPacketRuntime: () => "rust";
      getRendererRuntime: () => "rust-wgpu";
      getRendererStatus: () => EngineWebRendererStatus;
      getTerrainWorkerCount: () => number;
      getPlayerControllerRuntime: () => "rust";
      resetTerrainStreaming: () => void;
      getTerrainHeight: (x: number, z: number) => number;
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
    getLoadedTerrainChunkKeys: () => game.getLoadedTerrainChunkKeys(),
    getTerrainChunkKeys: () => game.getTerrainChunkKeys(),
    getTerrainPreset: () => game.getTerrainPreset(),
    getTerrainSeed: () => game.getTerrainSeed(),
    getTerrainStreamStatus: () => game.getTerrainStreamStatus(),
    getTerrainStreamerRuntime: () => game.getTerrainStreamerRuntime(),
    getTerrainStreamSchedulerRuntime: () => game.terrainStreamSchedulerRuntime,
    getTerrainDensityStoreRuntime: () => game.getTerrainDensityStoreRuntime(),
    getTerrainWorkerPoolRuntime: () => game.getTerrainWorkerPoolRuntime(),
    getRenderPacketRuntime: () => game.renderPacketRuntime,
    getTerrainRenderPacketRuntime: () => game.terrainRenderPacketRuntime,
    getRendererRuntime: () => game.rendererRuntime,
    getRendererStatus: () => game.getRendererStatus(),
    getTerrainWorkerCount: () => game.getTerrainWorkerCount(),
    getPlayerControllerRuntime: () => game.playerControllerRuntime,
    resetTerrainStreaming() {
      game.resetTerrainStreaming();
    },
    getTerrainHeight(x, z) {
      return game.getTerrainHeight(x, z);
    },
    setCameraMode(mode) {
      game.setPlayerMode(validatePlayerMode(mode));
    },
    setDebugCamera(x, y, z, yaw, pitch) {
      game.setDebugCamera(x, y, z, yaw, pitch);
    },
    setPlayerPosition(x, z) {
      game.setPlayerPosition(x, z);
    }
  };

  input.attach(elements.canvas);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const deltaSeconds = computeFrameDeltaSeconds(timestamp, lastTimestamp);
    lastTimestamp = timestamp;

    if (input.consumePress("KeyC") || input.consumePress("F1")) {
      game.toggleCameraMode();
    }

    const snapshot = input.consumeFrameSnapshot();
    const intent = readMovementIntent(input, snapshot.mouseDeltaX, snapshot.mouseDeltaY);

    game.tick(deltaSeconds, intent);
    game.renderFrame();

    const playerMode = game.getPlayerMode();
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

function readMovementIntent(
  input: InputTracker,
  lookDeltaX: number,
  lookDeltaY: number
): PlayerMovementIntent {
  return {
    forward: axis(input, "KeyW", "KeyS"),
    right: axis(input, "KeyD", "KeyA"),
    up: axis(input, "Space", "ControlLeft"),
    fast: input.isDown("ShiftLeft") || input.isDown("ShiftRight"),
    lookDeltaX,
    lookDeltaY
  };
}

function axis(input: InputTracker, positiveCode: string, negativeCode: string): number {
  return Number(input.isDown(positiveCode)) - Number(input.isDown(negativeCode));
}
