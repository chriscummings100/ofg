import { equal } from "node:assert/strict";
import type { BrowserFrameInput } from "./browserGameTypes.js";
import type { EngineWebBrowserGame } from "./engineWebWasm.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

describe("RustBrowserGameAdapter", () => {
  it("resizes and ticks through the Rust browser game facade", () => {
    const fake = fakeBrowserGame();
    const adapter = new RustBrowserGameAdapter(fakeCanvas(), fake);
    const frame = fakeFrameInput();

    withFakeWindow(() => adapter.tick(frame));

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
    adapter.command({ type: "togglePlayerCharacter" });
    adapter.command({ type: "setPlayerCharacter", character: "female" });
    adapter.command({
      type: "setPlayerAnimationTuning",
      walkSpeedMetersPerSecond: 5.5,
      runSpeedMetersPerSecond: 16.5,
      idlePlaybackScale: 1,
      walkPlaybackScale: 0.95,
      runPlaybackScale: 1.1
    });
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
    equal(fake.commandCalls[3]?.type, "togglePlayerCharacter");
    equal(fake.commandCalls[4]?.type, "setPlayerCharacter");
    equal(fake.commandCalls[5]?.type, "setPlayerAnimationTuning");
    equal(fake.commandCalls[6]?.type, "setPlayerPosition");
    equal(fake.commandCalls[7]?.type, "setDebugCamera");
    equal(snapshot.playerMode, "firstPerson");
    equal(snapshot.playerPosition.x, 96);
    equal(snapshot.loadedTerrainChunkKeys[0], "0,0,0");
    equal(snapshot.playerCharacterId, "female");
    equal(snapshot.playerCharacterLabel, "Female");
    equal(snapshot.modelAnimationRuntime, "rust");
    equal(snapshot.activeModelAnimationClip, "test-move");
    equal(snapshot.nextModelAnimationClip, "test-walk");
    equal(snapshot.modelAnimationTimeSeconds, 0.25);
    equal(snapshot.modelAnimationDurationSeconds, 2);
    equal(snapshot.modelAnimationBlendWeight, 0.5);
    equal(snapshot.modelAnimationWalkRunBlendWeight, 1);
    equal(snapshot.modelAnimationPlaybackScale, 1.1);
    equal(snapshot.modelAnimationLocomotionSpeedMetersPerSecond, 16.5);
    equal(snapshot.modelAnimationWalkSpeedMetersPerSecond, 5.5);
    equal(snapshot.modelAnimationRunSpeedMetersPerSecond, 16.5);
    equal(snapshot.modelAnimationIdlePlaybackScale, 1);
    equal(snapshot.modelAnimationWalkPlaybackScale, 0.95);
    equal(snapshot.modelAnimationRunPlaybackScale, 1.1);
    equal(snapshot.modelSkinningRuntime, "rust-cpu");
    equal(snapshot.modelSkinningJointCount, 2);
    equal(snapshot.playerCharacterRuntime, "rust");
    equal(snapshot.playerCharacterVisible, true);
    equal(snapshot.playerCharacterFollowsPlayer, true);
    equal(snapshot.debugPlayerMarkerVisible, false);
  });
});

type FakeBrowserGame = EngineWebBrowserGame & {
  resizeCalls: Array<Parameters<EngineWebBrowserGame["resize"]>[0]>;
  tickCalls: BrowserFrameInput[];
  commandCalls: Array<Parameters<EngineWebBrowserGame["command"]>[0]>;
};

function fakeBrowserGame(): FakeBrowserGame {
  return {
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
        playerControllerRuntime: "rust",
        playerCharacterId: "female",
        playerCharacterLabel: "Female",
        playerCharacterRuntime: "rust",
        playerCharacterVisible: true,
        playerCharacterFollowsPlayer: true,
        debugPlayerMarkerVisible: false,
        modelAnimationRuntime: "rust",
        activeModelAnimationClip: "test-move",
        nextModelAnimationClip: "test-walk",
        modelAnimationTimeSeconds: 0.25,
        modelAnimationDurationSeconds: 2,
        modelAnimationBlendWeight: 0.5,
        modelAnimationWalkRunBlendWeight: 1,
        modelAnimationPlaybackScale: 1.1,
        modelAnimationLocomotionSpeedMetersPerSecond: 16.5,
        modelAnimationWalkSpeedMetersPerSecond: 5.5,
        modelAnimationRunSpeedMetersPerSecond: 16.5,
        modelAnimationIdlePlaybackScale: 1,
        modelAnimationWalkPlaybackScale: 0.95,
        modelAnimationRunPlaybackScale: 1.1,
        modelSkinningRuntime: "rust-cpu",
        modelSkinningJointCount: 2
      };
    }
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
