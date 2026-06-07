import {
  InputTracker,
  type InputSnapshot,
  type TouchControlElements
} from "../engine/input/inputTracker.js";
import { buildBrowserFrameInput } from "./frameInput.js";
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
  type PlayerAnimationTuning,
  type PlayerCharacterId,
  type PlayerMode,
  type PostProcessDebugView,
  type ShadowDebugView
} from "../engine/web/browserGameTypes.js";

export type GameTouchControlElements = TouchControlElements & {
  readonly cameraToggle: HTMLButtonElement;
};

type GameElements = {
  readonly canvas: HTMLCanvasElement;
  readonly cameraMode: HTMLElement;
  readonly characterToggle: HTMLButtonElement;
  readonly frameTime: HTMLElement;
  readonly touchControls: GameTouchControlElements;
};

declare global {
  interface Window {
    __ofgDebug?: {
      getLoadedTerrainChunkKeys: () => string[];
      getLoadedTerrainNodeKeys: () => string[];
      getTerrainChunkKeys: () => string[];
      getTerrainNodeKeys: () => string[];
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
      getShadowDebugView: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["shadowDebugView"];
      getSkyRuntime: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["skyRuntime"];
      getSkyDayPhase: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["skyDayPhase"];
      getSkySunElevation: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["skySunElevation"];
      getSkyCloudCoverage: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["skyCloudCoverage"];
      getSkyStarIntensity: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["skyStarIntensity"];
      getPostProcessDebugView: () => PostProcessDebugView;
      getPostProcessExposure: () => number;
      getPostProcessToneMappingEnabled: () => boolean;
      getPostProcessBloomEnabled: () => boolean;
      getPostProcessBloomThreshold: () => number;
      getPostProcessBloomIntensity: () => number;
      getPostProcessDofEnabled: () => boolean;
      getPostProcessDofFocusDistance: () => number;
      getPostProcessDofFocusRange: () => number;
      getPostProcessDofMaxBlurPixels: () => number;
      getTerrainWorkerCount: () => number;
      getPlayerControllerRuntime: () => "rust";
      getPlayerCharacterId: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["playerCharacterId"];
      getPlayerCharacterLabel: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["playerCharacterLabel"];
      getPlayerCharacterRuntime: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["playerCharacterRuntime"];
      getPlayerCharacterVisible: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["playerCharacterVisible"];
      getPlayerCharacterFollowsPlayer: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["playerCharacterFollowsPlayer"];
      getDebugPlayerMarkerVisible: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["debugPlayerMarkerVisible"];
      getModelPrimitiveCount: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelPrimitiveCount"];
      getModelMaterialCount: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelMaterialCount"];
      getModelTextureCount: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelTextureCount"];
      getModelNonFallbackAlbedoPartCount: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelNonFallbackAlbedoPartCount"];
      getModelAnimationRuntime: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationRuntime"];
      getActiveModelAnimationClip: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["activeModelAnimationClip"];
      getNextModelAnimationClip: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["nextModelAnimationClip"];
      getModelAnimationTimeSeconds: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationTimeSeconds"];
      getModelAnimationDurationSeconds: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationDurationSeconds"];
      getModelAnimationBlendWeight: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationBlendWeight"];
      getModelAnimationWalkRunBlendWeight: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationWalkRunBlendWeight"];
      getModelAnimationPlaybackScale: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationPlaybackScale"];
      getModelAnimationLocomotionSpeedMetersPerSecond: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationLocomotionSpeedMetersPerSecond"];
      getModelAnimationWalkSpeedMetersPerSecond: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationWalkSpeedMetersPerSecond"];
      getModelAnimationRunSpeedMetersPerSecond: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationRunSpeedMetersPerSecond"];
      getModelAnimationIdlePlaybackScale: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationIdlePlaybackScale"];
      getModelAnimationWalkPlaybackScale: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationWalkPlaybackScale"];
      getModelAnimationRunPlaybackScale: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelAnimationRunPlaybackScale"];
      getModelSkinningRuntime: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelSkinningRuntime"];
      getModelSkinningJointCount: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["modelSkinningJointCount"];
      getPlayerPosition: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["playerPosition"];
      resetTerrainStreaming: () => void;
      setCameraMode: (mode: PlayerMode) => void;
      setDebugCamera: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
      setShadowDebugView: (view: ShadowDebugView) => void;
      setPostProcessDebugView: (view: PostProcessDebugView) => void;
      setPostProcessToneMapping: (enabled: boolean, exposure: number) => void;
      setPostProcessBloom: (enabled: boolean, threshold: number, intensity: number) => void;
      setPostProcessDepthOfField: (
        enabled: boolean,
        focusDistance: number,
        focusRange: number,
        maxBlurPixels: number
      ) => void;
      setPlayerAnimationTuning: (tuning: Partial<PlayerAnimationTuning>) => void;
      setPlayerCharacter: (character: PlayerCharacterId) => void;
      setPlayerPosition: (x: number, z: number) => void;
      togglePlayerCharacter: () => void;
    };
  }
}

export async function startGame(elements: GameElements): Promise<void> {
  const input = new InputTracker();
  const descriptor = readWorldDescriptor();
  const game = await createRustBrowserGameRuntime(elements.canvas, descriptor);
  window.__ofgDebug = {
    getLoadedTerrainChunkKeys: () => game.debugSnapshot().loadedTerrainChunkKeys,
    getLoadedTerrainNodeKeys: () => game.debugSnapshot().loadedTerrainNodeKeys,
    getTerrainChunkKeys: () => game.debugSnapshot().terrainChunkKeys,
    getTerrainNodeKeys: () => game.debugSnapshot().terrainNodeKeys,
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
    getShadowDebugView: () => game.debugSnapshot().shadowDebugView,
    getSkyRuntime: () => game.debugSnapshot().skyRuntime,
    getSkyDayPhase: () => game.debugSnapshot().skyDayPhase,
    getSkySunElevation: () => game.debugSnapshot().skySunElevation,
    getSkyCloudCoverage: () => game.debugSnapshot().skyCloudCoverage,
    getSkyStarIntensity: () => game.debugSnapshot().skyStarIntensity,
    getPostProcessDebugView: () => game.debugSnapshot().rendererStatus.postProcessDebugView,
    getPostProcessExposure: () => game.debugSnapshot().rendererStatus.postProcessExposure,
    getPostProcessToneMappingEnabled: () =>
      game.debugSnapshot().rendererStatus.postProcessToneMappingEnabled,
    getPostProcessBloomEnabled: () => game.debugSnapshot().rendererStatus.postProcessBloomEnabled,
    getPostProcessBloomThreshold: () =>
      game.debugSnapshot().rendererStatus.postProcessBloomThreshold,
    getPostProcessBloomIntensity: () =>
      game.debugSnapshot().rendererStatus.postProcessBloomIntensity,
    getPostProcessDofEnabled: () => game.debugSnapshot().rendererStatus.postProcessDofEnabled,
    getPostProcessDofFocusDistance: () =>
      game.debugSnapshot().rendererStatus.postProcessDofFocusDistance,
    getPostProcessDofFocusRange: () =>
      game.debugSnapshot().rendererStatus.postProcessDofFocusRange,
    getPostProcessDofMaxBlurPixels: () =>
      game.debugSnapshot().rendererStatus.postProcessDofMaxBlurPixels,
    getTerrainWorkerCount: () => game.debugSnapshot().terrainWorkerCount,
    getPlayerControllerRuntime: () => game.debugSnapshot().playerControllerRuntime,
    getPlayerCharacterId: () => game.debugSnapshot().playerCharacterId,
    getPlayerCharacterLabel: () => game.debugSnapshot().playerCharacterLabel,
    getPlayerCharacterRuntime: () => game.debugSnapshot().playerCharacterRuntime,
    getPlayerCharacterVisible: () => game.debugSnapshot().playerCharacterVisible,
    getPlayerCharacterFollowsPlayer: () => game.debugSnapshot().playerCharacterFollowsPlayer,
    getDebugPlayerMarkerVisible: () => game.debugSnapshot().debugPlayerMarkerVisible,
    getModelAnimationRuntime: () => game.debugSnapshot().modelAnimationRuntime,
    getActiveModelAnimationClip: () => game.debugSnapshot().activeModelAnimationClip,
    getNextModelAnimationClip: () => game.debugSnapshot().nextModelAnimationClip,
    getModelAnimationTimeSeconds: () => game.debugSnapshot().modelAnimationTimeSeconds,
    getModelAnimationDurationSeconds: () => game.debugSnapshot().modelAnimationDurationSeconds,
    getModelAnimationBlendWeight: () => game.debugSnapshot().modelAnimationBlendWeight,
    getModelAnimationWalkRunBlendWeight: () => game.debugSnapshot().modelAnimationWalkRunBlendWeight,
    getModelAnimationPlaybackScale: () => game.debugSnapshot().modelAnimationPlaybackScale,
    getModelAnimationLocomotionSpeedMetersPerSecond: () =>
      game.debugSnapshot().modelAnimationLocomotionSpeedMetersPerSecond,
    getModelAnimationWalkSpeedMetersPerSecond: () =>
      game.debugSnapshot().modelAnimationWalkSpeedMetersPerSecond,
    getModelAnimationRunSpeedMetersPerSecond: () =>
      game.debugSnapshot().modelAnimationRunSpeedMetersPerSecond,
    getModelAnimationIdlePlaybackScale: () =>
      game.debugSnapshot().modelAnimationIdlePlaybackScale,
    getModelAnimationWalkPlaybackScale: () =>
      game.debugSnapshot().modelAnimationWalkPlaybackScale,
    getModelAnimationRunPlaybackScale: () =>
      game.debugSnapshot().modelAnimationRunPlaybackScale,
    getModelPrimitiveCount: () => game.debugSnapshot().modelPrimitiveCount,
    getModelMaterialCount: () => game.debugSnapshot().modelMaterialCount,
    getModelTextureCount: () => game.debugSnapshot().modelTextureCount,
    getModelNonFallbackAlbedoPartCount: () =>
      game.debugSnapshot().modelNonFallbackAlbedoPartCount,
    getModelSkinningRuntime: () => game.debugSnapshot().modelSkinningRuntime,
    getModelSkinningJointCount: () => game.debugSnapshot().modelSkinningJointCount,
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
    setShadowDebugView(view) {
      game.command({ type: "setShadowDebugView", view: validateShadowDebugView(view) });
    },
    setPostProcessDebugView(view) {
      game.command({ type: "setPostProcessDebugView", view: validatePostProcessDebugView(view) });
    },
    setPostProcessToneMapping(enabled, exposure) {
      game.command({
        type: "setPostProcessToneMapping",
        enabled,
        exposure: validatePostProcessExposure(exposure)
      });
    },
    setPostProcessBloom(enabled, threshold, intensity) {
      game.command({
        type: "setPostProcessBloom",
        enabled,
        threshold: validatePostProcessBloomThreshold(threshold),
        intensity: validatePostProcessBloomIntensity(intensity)
      });
    },
    setPostProcessDepthOfField(enabled, focusDistance, focusRange, maxBlurPixels) {
      game.command({
        type: "setPostProcessDepthOfField",
        enabled,
        focusDistance: validatePostProcessDofFocusDistance(focusDistance),
        focusRange: validatePostProcessDofFocusRange(focusRange),
        maxBlurPixels: validatePostProcessDofMaxBlurPixels(maxBlurPixels)
      });
    },
    setPlayerAnimationTuning(tuning) {
      game.command({
        type: "setPlayerAnimationTuning",
        ...playerAnimationTuningFromSnapshot(game.debugSnapshot(), tuning)
      });
    },
    setPlayerCharacter(character) {
      game.command({ type: "setPlayerCharacter", character: validatePlayerCharacterId(character) });
    },
    setPlayerPosition(x, z) {
      game.command({ type: "setPlayerPosition", x, z });
    },
    togglePlayerCharacter() {
      game.command({ type: "togglePlayerCharacter" });
    }
  };

  elements.characterToggle.addEventListener("click", () => {
    game.command({ type: "togglePlayerCharacter" });
    updateCharacterToggle(elements.characterToggle, game.debugSnapshot());
    elements.canvas.focus({ preventScroll: true });
  });
  elements.touchControls.cameraToggle.addEventListener("click", () => {
    game.command({ type: "togglePlayerMode" });
    elements.canvas.focus({ preventScroll: true });
  });
  updateCharacterToggle(elements.characterToggle, game.debugSnapshot());
  input.attach(elements.canvas, document, elements.touchControls);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const deltaSeconds = computeFrameDeltaSeconds(timestamp, lastTimestamp);
    lastTimestamp = timestamp;

    if (input.consumePress("KeyC") || input.consumePress("F1")) {
      game.command({ type: "togglePlayerMode" });
    }

    const snapshot = input.consumeFrameSnapshot();
    const frameInput = readFrameInput(input, deltaSeconds, snapshot);

    game.tick(frameInput);

    const debugSnapshot = game.debugSnapshot();
    const playerMode = debugSnapshot.playerMode;
    elements.cameraMode.textContent = cameraModeLabel(playerMode);
    elements.cameraMode.dataset.mode = playerMode;
    updateCharacterToggle(elements.characterToggle, debugSnapshot);
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
  if (mode === "firstPerson" || mode === "thirdPerson" || mode === "debugFly") {
    return mode;
  }

  throw new Error(`Unknown player camera mode '${mode}'.`);
}

function validatePlayerCharacterId(character: string): PlayerCharacterId {
  if (character === "male" || character === "female") {
    return character;
  }

  throw new Error(`Unknown player character '${character}'.`);
}

function validateShadowDebugView(view: string): ShadowDebugView {
  if (
    view === "off" ||
    view === "cascadeIndex" ||
    view === "shadowVisibility" ||
    view === "shadowDepthCascade0" ||
    view === "shadowDepthCascade1" ||
    view === "shadowDepthCascade2" ||
    view === "shadowDepthCascade3"
  ) {
    return view;
  }

  throw new Error(`Unknown shadow debug view '${view}'.`);
}

function validatePostProcessDebugView(view: string): PostProcessDebugView {
  if (
    view === "final" ||
    view === "sceneColor" ||
    view === "linearDepth" ||
    view === "postToneMap" ||
    view === "bloom" ||
    view === "dofCoc" ||
    view === "dofBlurred"
  ) {
    return view;
  }

  throw new Error(`Unknown post-process debug view '${view}'.`);
}

function validatePostProcessExposure(exposure: number): number {
  if (Number.isFinite(exposure) && exposure >= 0 && exposure <= 16) {
    return exposure;
  }

  throw new Error(`Invalid post-process exposure '${exposure}'.`);
}

function validatePostProcessBloomThreshold(threshold: number): number {
  if (Number.isFinite(threshold) && threshold >= 0 && threshold <= 64) {
    return threshold;
  }

  throw new Error(`Invalid post-process bloom threshold '${threshold}'.`);
}

function validatePostProcessBloomIntensity(intensity: number): number {
  if (Number.isFinite(intensity) && intensity >= 0 && intensity <= 4) {
    return intensity;
  }

  throw new Error(`Invalid post-process bloom intensity '${intensity}'.`);
}

function validatePostProcessDofFocusDistance(focusDistance: number): number {
  if (Number.isFinite(focusDistance) && focusDistance >= 0.1 && focusDistance <= 512) {
    return focusDistance;
  }

  throw new Error(`Invalid post-process DoF focus distance '${focusDistance}'.`);
}

function validatePostProcessDofFocusRange(focusRange: number): number {
  if (Number.isFinite(focusRange) && focusRange >= 0.1 && focusRange <= 256) {
    return focusRange;
  }

  throw new Error(`Invalid post-process DoF focus range '${focusRange}'.`);
}

function validatePostProcessDofMaxBlurPixels(maxBlurPixels: number): number {
  if (Number.isFinite(maxBlurPixels) && maxBlurPixels >= 0 && maxBlurPixels <= 32) {
    return maxBlurPixels;
  }

  throw new Error(`Invalid post-process DoF max blur pixels '${maxBlurPixels}'.`);
}

function updateCharacterToggle(
  toggle: HTMLButtonElement,
  snapshot: ReturnType<RustBrowserGameRuntime["debugSnapshot"]>
): void {
  const character = snapshot.playerCharacterId ?? "male";
  toggle.textContent = snapshot.playerCharacterLabel ?? characterLabel(character);
  toggle.dataset.character = character;
}

function playerAnimationTuningFromSnapshot(
  snapshot: ReturnType<RustBrowserGameRuntime["debugSnapshot"]>,
  tuning: Partial<PlayerAnimationTuning>
): PlayerAnimationTuning {
  return {
    walkSpeedMetersPerSecond:
      tuning.walkSpeedMetersPerSecond ??
      snapshot.modelAnimationWalkSpeedMetersPerSecond ??
      5.5,
    runSpeedMetersPerSecond:
      tuning.runSpeedMetersPerSecond ??
      snapshot.modelAnimationRunSpeedMetersPerSecond ??
      16.5,
    idlePlaybackScale:
      tuning.idlePlaybackScale ?? snapshot.modelAnimationIdlePlaybackScale ?? 1,
    walkPlaybackScale:
      tuning.walkPlaybackScale ?? snapshot.modelAnimationWalkPlaybackScale ?? 1,
    runPlaybackScale:
      tuning.runPlaybackScale ?? snapshot.modelAnimationRunPlaybackScale ?? 1
  };
}

function characterLabel(character: PlayerCharacterId): string {
  switch (character) {
    case "male":
      return "Male";
    case "female":
      return "Female";
  }
}

function cameraModeLabel(mode: PlayerMode): string {
  switch (mode) {
    case "firstPerson":
      return "FIRST";
    case "thirdPerson":
      return "THIRD";
    case "debugFly":
      return "FLY";
  }
}

function readFrameInput(
  input: InputTracker,
  deltaSeconds: number,
  snapshot: InputSnapshot
): BrowserFrameInput {
  return buildBrowserFrameInput({
    deltaSeconds,
    keyboardForward: axis(input, "KeyW", "KeyS"),
    keyboardRight: axis(input, "KeyD", "KeyA"),
    keyboardUp: axis(input, "Space", "ControlLeft"),
    fast: input.isDown("ShiftLeft") || input.isDown("ShiftRight"),
    mouseDeltaX: snapshot.mouseDeltaX,
    mouseDeltaY: snapshot.mouseDeltaY,
    touchLookDeltaX: snapshot.touchLookDeltaX,
    touchLookDeltaY: snapshot.touchLookDeltaY,
    touchLookStickX: snapshot.touchLookStickX,
    touchLookStickY: snapshot.touchLookStickY,
    touchMovementForward: snapshot.touchMovementForward,
    touchMovementRight: snapshot.touchMovementRight,
    touchMovementMagnitude: snapshot.touchMovementMagnitude
  });
}

function axis(input: InputTracker, positiveCode: string, negativeCode: string): number {
  return Number(input.isDown(positiveCode)) - Number(input.isDown(negativeCode));
}
