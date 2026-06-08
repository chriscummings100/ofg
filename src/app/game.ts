import {
  InputTracker,
  type InputSnapshot,
  type TouchControlElements
} from "../engine/input/inputTracker.js";
import { buildBrowserFrameInput } from "./frameInput.js";
import { computeFrameDeltaSeconds } from "./frameTiming.js";
import {
  BrowserPerfTracker,
  buildPerfStats,
  dumpPerfStats
} from "./perfDebug.js";
import {
  createRenderDebugUi,
  type RenderDebugUiElements
} from "./renderDebugUi.js";
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
  type RenderDebugOptions,
  type RenderDebugOptionsUpdate,
  type ShadowDebugView,
  type ShadowSunMode
} from "../engine/web/browserGameTypes.js";

export type GameTouchControlElements = TouchControlElements & {
  readonly cameraToggle: HTMLButtonElement;
};

export type GameRenderDebugUiElements = RenderDebugUiElements;

type GameElements = {
  readonly canvas: HTMLCanvasElement;
  readonly cameraMode: HTMLElement;
  readonly characterToggle: HTMLButtonElement;
  readonly frameTime: HTMLElement;
  readonly touchControls: GameTouchControlElements;
  readonly renderDebugUi: GameRenderDebugUiElements;
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
      getTerrainWorkerPoolRuntime: () => ReturnType<RustBrowserGameRuntime["debugSnapshot"]>["terrainWorkerPoolRuntime"];
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
      getPerfStats: () => ReturnType<typeof buildPerfStats>;
      dumpPerfStats: () => ReturnType<typeof buildPerfStats>;
      resetPerfStats: () => void;
      getRenderDebugOptions: () => RenderDebugOptions;
      setRenderDebugOptions: (options: RenderDebugOptionsUpdate) => void;
      resetRenderDebugOptions: () => void;
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
  const browserPerf = new BrowserPerfTracker();
  let latestDebugSnapshot = game.debugSnapshot();
  const readDebugSnapshot = () => latestDebugSnapshot;
  const runDebugCommand = (command: Parameters<RustBrowserGameRuntime["command"]>[0]) => {
    game.command(command);
    latestDebugSnapshot = game.debugSnapshot();
  };
  window.__ofgDebug = {
    getLoadedTerrainChunkKeys: () => readDebugSnapshot().loadedTerrainChunkKeys,
    getLoadedTerrainNodeKeys: () => readDebugSnapshot().loadedTerrainNodeKeys,
    getTerrainChunkKeys: () => readDebugSnapshot().terrainChunkKeys,
    getTerrainNodeKeys: () => readDebugSnapshot().terrainNodeKeys,
    getTerrainPreset: () => readDebugSnapshot().terrainPreset,
    getTerrainSeed: () => readDebugSnapshot().terrainSeed,
    getTerrainStreamStatus: () => readDebugSnapshot().terrainStreamStatus,
    getTerrainStreamerRuntime: () => readDebugSnapshot().terrainStreamerRuntime,
    getTerrainStreamSchedulerRuntime: () => readDebugSnapshot().terrainStreamSchedulerRuntime,
    getTerrainDensityStoreRuntime: () => readDebugSnapshot().terrainDensityStoreRuntime,
    getTerrainWorkerPoolRuntime: () => readDebugSnapshot().terrainWorkerPoolRuntime,
    getRenderPacketRuntime: () => readDebugSnapshot().renderPacketRuntime,
    getTerrainRenderPacketRuntime: () => readDebugSnapshot().terrainRenderPacketRuntime,
    getRendererRuntime: () => readDebugSnapshot().rendererRuntime,
    getRendererStatus: () => readDebugSnapshot().rendererStatus,
    getShadowDebugView: () => readDebugSnapshot().shadowDebugView,
    getSkyRuntime: () => readDebugSnapshot().skyRuntime,
    getSkyDayPhase: () => readDebugSnapshot().skyDayPhase,
    getSkySunElevation: () => readDebugSnapshot().skySunElevation,
    getSkyCloudCoverage: () => readDebugSnapshot().skyCloudCoverage,
    getSkyStarIntensity: () => readDebugSnapshot().skyStarIntensity,
    getPostProcessDebugView: () => readDebugSnapshot().rendererStatus.postProcessDebugView,
    getPostProcessExposure: () => readDebugSnapshot().rendererStatus.postProcessExposure,
    getPostProcessToneMappingEnabled: () =>
      readDebugSnapshot().rendererStatus.postProcessToneMappingEnabled,
    getPostProcessBloomEnabled: () => readDebugSnapshot().rendererStatus.postProcessBloomEnabled,
    getPostProcessBloomThreshold: () =>
      readDebugSnapshot().rendererStatus.postProcessBloomThreshold,
    getPostProcessBloomIntensity: () =>
      readDebugSnapshot().rendererStatus.postProcessBloomIntensity,
    getPostProcessDofEnabled: () => readDebugSnapshot().rendererStatus.postProcessDofEnabled,
    getPostProcessDofFocusDistance: () =>
      readDebugSnapshot().rendererStatus.postProcessDofFocusDistance,
    getPostProcessDofFocusRange: () =>
      readDebugSnapshot().rendererStatus.postProcessDofFocusRange,
    getPostProcessDofMaxBlurPixels: () =>
      readDebugSnapshot().rendererStatus.postProcessDofMaxBlurPixels,
    getTerrainWorkerCount: () => readDebugSnapshot().terrainWorkerCount,
    getPlayerControllerRuntime: () => readDebugSnapshot().playerControllerRuntime,
    getPlayerCharacterId: () => readDebugSnapshot().playerCharacterId,
    getPlayerCharacterLabel: () => readDebugSnapshot().playerCharacterLabel,
    getPlayerCharacterRuntime: () => readDebugSnapshot().playerCharacterRuntime,
    getPlayerCharacterVisible: () => readDebugSnapshot().playerCharacterVisible,
    getPlayerCharacterFollowsPlayer: () => readDebugSnapshot().playerCharacterFollowsPlayer,
    getDebugPlayerMarkerVisible: () => readDebugSnapshot().debugPlayerMarkerVisible,
    getModelAnimationRuntime: () => readDebugSnapshot().modelAnimationRuntime,
    getActiveModelAnimationClip: () => readDebugSnapshot().activeModelAnimationClip,
    getNextModelAnimationClip: () => readDebugSnapshot().nextModelAnimationClip,
    getModelAnimationTimeSeconds: () => readDebugSnapshot().modelAnimationTimeSeconds,
    getModelAnimationDurationSeconds: () => readDebugSnapshot().modelAnimationDurationSeconds,
    getModelAnimationBlendWeight: () => readDebugSnapshot().modelAnimationBlendWeight,
    getModelAnimationWalkRunBlendWeight: () =>
      readDebugSnapshot().modelAnimationWalkRunBlendWeight,
    getModelAnimationPlaybackScale: () => readDebugSnapshot().modelAnimationPlaybackScale,
    getModelAnimationLocomotionSpeedMetersPerSecond: () =>
      readDebugSnapshot().modelAnimationLocomotionSpeedMetersPerSecond,
    getModelAnimationWalkSpeedMetersPerSecond: () =>
      readDebugSnapshot().modelAnimationWalkSpeedMetersPerSecond,
    getModelAnimationRunSpeedMetersPerSecond: () =>
      readDebugSnapshot().modelAnimationRunSpeedMetersPerSecond,
    getModelAnimationIdlePlaybackScale: () =>
      readDebugSnapshot().modelAnimationIdlePlaybackScale,
    getModelAnimationWalkPlaybackScale: () =>
      readDebugSnapshot().modelAnimationWalkPlaybackScale,
    getModelAnimationRunPlaybackScale: () =>
      readDebugSnapshot().modelAnimationRunPlaybackScale,
    getModelPrimitiveCount: () => readDebugSnapshot().modelPrimitiveCount,
    getModelMaterialCount: () => readDebugSnapshot().modelMaterialCount,
    getModelTextureCount: () => readDebugSnapshot().modelTextureCount,
    getModelNonFallbackAlbedoPartCount: () =>
      readDebugSnapshot().modelNonFallbackAlbedoPartCount,
    getModelSkinningRuntime: () => readDebugSnapshot().modelSkinningRuntime,
    getModelSkinningJointCount: () => readDebugSnapshot().modelSkinningJointCount,
    getPlayerPosition: () => readDebugSnapshot().playerPosition,
    getPerfStats() {
      return buildPerfStats(browserPerf.summary(), readDebugSnapshot());
    },
    dumpPerfStats() {
      const stats = buildPerfStats(browserPerf.summary(), readDebugSnapshot());
      return dumpPerfStats(stats);
    },
    resetPerfStats() {
      browserPerf.reset();
      runDebugCommand({ type: "resetPerfStats" });
    },
    getRenderDebugOptions() {
      return readDebugSnapshot().renderDebugOptions;
    },
    setRenderDebugOptions(options) {
      runDebugCommand({
        type: "setRenderDebugOptions",
        ...validateRenderDebugOptionsUpdate(options)
      });
    },
    resetRenderDebugOptions() {
      runDebugCommand({ type: "resetRenderDebugOptions" });
    },
    resetTerrainStreaming() {
      runDebugCommand({ type: "resetStreaming" });
    },
    setCameraMode(mode) {
      runDebugCommand({ type: "setPlayerMode", mode: validatePlayerMode(mode) });
    },
    setDebugCamera(x, y, z, yaw, pitch) {
      runDebugCommand({ type: "setDebugCamera", x, y, z, yaw, pitch });
    },
    setShadowDebugView(view) {
      runDebugCommand({ type: "setShadowDebugView", view: validateShadowDebugView(view) });
    },
    setPostProcessDebugView(view) {
      runDebugCommand({
        type: "setPostProcessDebugView",
        view: validatePostProcessDebugView(view)
      });
    },
    setPostProcessToneMapping(enabled, exposure) {
      runDebugCommand({
        type: "setPostProcessToneMapping",
        enabled,
        exposure: validatePostProcessExposure(exposure)
      });
    },
    setPostProcessBloom(enabled, threshold, intensity) {
      runDebugCommand({
        type: "setPostProcessBloom",
        enabled,
        threshold: validatePostProcessBloomThreshold(threshold),
        intensity: validatePostProcessBloomIntensity(intensity)
      });
    },
    setPostProcessDepthOfField(enabled, focusDistance, focusRange, maxBlurPixels) {
      runDebugCommand({
        type: "setPostProcessDepthOfField",
        enabled,
        focusDistance: validatePostProcessDofFocusDistance(focusDistance),
        focusRange: validatePostProcessDofFocusRange(focusRange),
        maxBlurPixels: validatePostProcessDofMaxBlurPixels(maxBlurPixels)
      });
    },
    setPlayerAnimationTuning(tuning) {
      runDebugCommand({
        type: "setPlayerAnimationTuning",
        ...playerAnimationTuningFromSnapshot(readDebugSnapshot(), tuning)
      });
    },
    setPlayerCharacter(character) {
      runDebugCommand({
        type: "setPlayerCharacter",
        character: validatePlayerCharacterId(character)
      });
    },
    setPlayerPosition(x, z) {
      runDebugCommand({ type: "setPlayerPosition", x, z });
    },
    togglePlayerCharacter() {
      runDebugCommand({ type: "togglePlayerCharacter" });
    }
  };
  const renderDebugUi = createRenderDebugUi(elements.renderDebugUi, {
    getRenderDebugOptions: () => readDebugSnapshot().renderDebugOptions,
    setRenderDebugOptions: (options) => {
      runDebugCommand({
        type: "setRenderDebugOptions",
        ...validateRenderDebugOptionsUpdate(options)
      });
    },
    resetRenderDebugOptions: () => {
      runDebugCommand({ type: "resetRenderDebugOptions" });
    },
    resetPerfStats: () => {
      browserPerf.reset();
      runDebugCommand({ type: "resetPerfStats" });
    },
    focusCanvas: () => {
      elements.canvas.focus({ preventScroll: true });
    }
  });

  elements.characterToggle.addEventListener("click", () => {
    runDebugCommand({ type: "togglePlayerCharacter" });
    updateCharacterToggle(elements.characterToggle, latestDebugSnapshot);
    elements.canvas.focus({ preventScroll: true });
  });
  elements.touchControls.cameraToggle.addEventListener("click", () => {
    runDebugCommand({ type: "togglePlayerMode" });
    elements.canvas.focus({ preventScroll: true });
  });
  updateCharacterToggle(elements.characterToggle, latestDebugSnapshot);
  input.attach(elements.canvas, document, elements.touchControls);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const frameStartedAt = performance.now();
    const deltaSeconds = computeFrameDeltaSeconds(timestamp, lastTimestamp);
    lastTimestamp = timestamp;

    if (input.consumePress("KeyC") || input.consumePress("F1")) {
      game.command({ type: "togglePlayerMode" });
    }
    if (input.consumePress("F8")) {
      renderDebugUi.togglePanel();
    }
    if (input.consumePress("F9")) {
      renderDebugUi.togglePerfOverlay();
    }

    const inputStartedAt = performance.now();
    const snapshot = input.consumeFrameSnapshot();
    const frameInput = readFrameInput(input, deltaSeconds, snapshot);
    const inputAndFrameBuildMs = performance.now() - inputStartedAt;

    const gameTickStartedAt = performance.now();
    game.tick(frameInput);
    const gameTickMs = performance.now() - gameTickStartedAt;

    const debugSnapshotStartedAt = performance.now();
    latestDebugSnapshot = game.debugSnapshot();
    const debugSnapshotMs = performance.now() - debugSnapshotStartedAt;
    const hudStartedAt = performance.now();
    const debugSnapshot = latestDebugSnapshot;
    const playerMode = debugSnapshot.playerMode;
    elements.cameraMode.textContent = cameraModeLabel(playerMode);
    elements.cameraMode.dataset.mode = playerMode;
    updateCharacterToggle(elements.characterToggle, debugSnapshot);
    elements.frameTime.textContent = `${(deltaSeconds * 1000).toFixed(1)} ms`;
    const hudUpdateMs = performance.now() - hudStartedAt;
    browserPerf.record({
      totalFrameMs: performance.now() - frameStartedAt,
      inputAndFrameBuildMs,
      gameTickMs,
      debugSnapshotMs,
      hudUpdateMs
    });
    renderDebugUi.update(buildPerfStats(browserPerf.summary(), debugSnapshot));

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

function validateRenderDebugOptionsUpdate(
  options: RenderDebugOptionsUpdate
): RenderDebugOptionsUpdate {
  const update: MutableRenderDebugOptionsUpdate = {};
  if (options.terrainLodMask !== undefined) {
    if (
      !Number.isInteger(options.terrainLodMask) ||
      options.terrainLodMask <= 0 ||
      options.terrainLodMask > 0xFFFFFFFF
    ) {
      throw new Error(`Invalid terrain LOD mask '${options.terrainLodMask}'.`);
    }
    update.terrainLodMask = options.terrainLodMask >>> 0;
  }
  if (options.skyEnabled !== undefined) {
    update.skyEnabled = validateBoolean(options.skyEnabled, "skyEnabled");
  }
  if (options.shadowPassEnabled !== undefined) {
    update.shadowPassEnabled = validateBoolean(options.shadowPassEnabled, "shadowPassEnabled");
  }
  if (options.shadowCascadeMask !== undefined) {
    if (
      !Number.isInteger(options.shadowCascadeMask) ||
      options.shadowCascadeMask <= 0 ||
      options.shadowCascadeMask > 0b1111
    ) {
      throw new Error(`Invalid shadow cascade mask '${options.shadowCascadeMask}'.`);
    }
    update.shadowCascadeMask = options.shadowCascadeMask;
  }
  if (options.shadowSamplingEnabled !== undefined) {
    update.shadowSamplingEnabled = validateBoolean(
      options.shadowSamplingEnabled,
      "shadowSamplingEnabled"
    );
  }
  if (options.shadowSunMode !== undefined) {
    update.shadowSunMode = validateShadowSunMode(options.shadowSunMode);
  }
  if (options.whiteTexturesEnabled !== undefined) {
    update.whiteTexturesEnabled = validateBoolean(
      options.whiteTexturesEnabled,
      "whiteTexturesEnabled"
    );
  }
  if (options.materialMode !== undefined) {
    if (options.materialMode !== "full" && options.materialMode !== "lambert") {
      throw new Error(`Invalid material debug mode '${options.materialMode}'.`);
    }
    update.materialMode = options.materialMode;
  }

  return update;
}

type MutableRenderDebugOptionsUpdate = {
  terrainLodMask?: RenderDebugOptions["terrainLodMask"];
  skyEnabled?: RenderDebugOptions["skyEnabled"];
  shadowPassEnabled?: RenderDebugOptions["shadowPassEnabled"];
  shadowCascadeMask?: RenderDebugOptions["shadowCascadeMask"];
  shadowSamplingEnabled?: RenderDebugOptions["shadowSamplingEnabled"];
  shadowSunMode?: RenderDebugOptions["shadowSunMode"];
  whiteTexturesEnabled?: RenderDebugOptions["whiteTexturesEnabled"];
  materialMode?: RenderDebugOptions["materialMode"];
};

function validateShadowSunMode(mode: string): ShadowSunMode {
  if (mode === "production" || mode === "overhead" || mode === "angled" || mode === "low") {
    return mode;
  }

  throw new Error(`Invalid shadow sun mode '${mode}'.`);
}

function validateBoolean(value: boolean, label: string): boolean {
  if (typeof value === "boolean") {
    return value;
  }

  throw new Error(`Invalid boolean render debug option '${label}'.`);
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
