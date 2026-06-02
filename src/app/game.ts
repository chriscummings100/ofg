import { InputTracker } from "../engine/input/inputTracker.js";
import { computeFrameDeltaSeconds } from "./frameTiming.js";
import {
  EngineCoreWasmHandle,
  loadEngineCoreWasm
} from "../engine/core/engineCoreWasm.js";
import { vec3, type Vec3 } from "../engine/math/vec3.js";
import { vec4 } from "../engine/math/vec4.js";
import { MATERIAL_FLAG_TRIPLANAR_ALBEDO, Material } from "../engine/render/Material.js";
import { Mesh } from "../engine/render/Mesh.js";
import { MeshRenderer } from "../engine/render/MeshRenderer.js";
import { SceneRenderExtractor } from "../engine/render/SceneRenderExtractor.js";
import { createTerrainCoreRenderPacketStore } from "../engine/render/TerrainCoreRenderPackets.js";
import {
  cameraFrameFromEnginePacket,
  directionalLightFromEnginePacket
} from "../engine/render/engineRenderPackets.js";
import { loadTerrainMaterialTextures } from "../engine/render/terrainTextures.js";
import { WebGpuRenderer } from "../engine/render/webgpuRenderer.js";
import { createDirectionalLight } from "../engine/render/Lighting.js";
import { createScene } from "../engine/scene/activeScene.js";
import type { Entity } from "../engine/scene/Entity.js";
import {
  isTerrainDebugOverlayMode,
  type TerrainDebugOverlayState
} from "../engine/world/terrainDebugOverlay.js";
import { TerrainCoreWorkerStreamer } from "../game/components/TerrainCoreWorkerStreamer.js";
import { createBoxMesh } from "../engine/world/primitiveMesh.js";
import {
  createSeedWorldDescriptor,
  createTerrainGenerator,
  TERRAIN_PRESET_IDS,
  type TerrainPresetId,
  type WorldDescriptor
} from "../engine/world/terrainGenerator.js";
import { createTerrainCoreDensityChunkStore } from "../engine/world/terrainCoreDensityChunkStore.js";
import {
  loadTerrainCoreWasm,
  type TerrainCoreWasmInstance
} from "../engine/world/terrainCoreWasm.js";
import { createTerrainCoreStreamScheduler } from "../engine/world/terrainCoreStreamScheduler.js";
import {
  type TerrainChunkWorkerClient,
  createTerrainChunkWorkerClient
} from "../engine/world/terrainChunkWorkerClient.js";
import {
  POSITION_COLOR_NORMAL_UV_LAYOUT,
  type MeshData
} from "../engine/world/terrainMesh.js";
import {
  type PlayerMode,
  type PlayerMovementIntent
} from "../game/components/playerTypes.js";
import { RustPlayerController } from "../game/components/RustPlayerController.js";
import { TerrainDebugOverlayView } from "./terrainDebugOverlayView.js";

type GameElements = {
  readonly canvas: HTMLCanvasElement;
  readonly terrainDebugOverlay: HTMLCanvasElement;
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
      getTerrainStreamStatus: () => ReturnType<TerrainCoreWorkerStreamer["getStreamStatus"]>;
      getTerrainStreamerRuntime: () => "rust";
      getTerrainStreamSchedulerRuntime: () => "rust";
      getTerrainDensityStoreRuntime: () => "rust";
      getTerrainWorkerPoolRuntime: () => "rust" | "typescript";
      getRenderPacketRuntime: () => "rust" | "typescript";
      getTerrainRenderPacketRuntime: () => "rust";
      getTerrainWorkerCount: () => number;
      getTerrainDebugOverlayMode: () => TerrainDebugOverlayState;
      getPlayerControllerRuntime: () => "rust";
      setTerrainDebugOverlayMode: (mode: TerrainDebugOverlayState) => void;
      cycleTerrainDebugOverlayMode: () => TerrainDebugOverlayState;
      resetTerrainStreaming: () => void;
      getTerrainHeight: (x: number, z: number) => number;
      setCameraMode: (mode: PlayerMode) => void;
      setDebugCamera: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
      setPlayerPosition: (x: number, z: number) => void;
    };
  }
}

export async function startGame(elements: GameElements): Promise<void> {
  const scene = createScene();
  const renderer = new WebGpuRenderer(elements.canvas);
  const input = new InputTracker();
  const descriptor = readWorldDescriptor();
  const field = createTerrainGenerator(descriptor);
  const terrainCore = await loadRequiredTerrainCore();
  const engineCore = await loadRequiredEngineCore();
  const terrainWorker = createRequiredTerrainWorker(descriptor, terrainCore);
  const terrainStreamConfig = {
    horizontalRadius: 1,
    verticalChunkOffsets: [-2, -1, 0, 1],
    cellSize: 1
  } as const;
  const terrainStreamScheduler = createTerrainCoreStreamScheduler(terrainCore, {
    horizontalRadius: terrainStreamConfig.horizontalRadius,
    verticalChunkOffsets: terrainStreamConfig.verticalChunkOffsets,
    maxInFlightJobs: terrainWorker.workerCount
  });
  const terrainDensityChunkStore = createTerrainCoreDensityChunkStore(terrainCore, descriptor);
  const terrainDebugOverlay = new TerrainDebugOverlayView(
    elements.terrainDebugOverlay,
    readTerrainDebugOverlayState()
  );
  scene.mainLight = createDirectionalLight({
    direction: vec3(0.89, 0.25, 0.38),
    color: vec3(1, 0.96, 0.88),
    intensity: 1,
    ambient: 0.34
  });
  const playerMarker = meshFromData(
    "mesh:player.marker",
    createBoxMesh(vec3(0, 0.9, 0), vec3(0.28, 0.9, 0.22), vec3(0.96, 0.7, 0.24))
  );
  const terrainTextures = await loadTerrainMaterialTextures();
  const terrainMaterial = new Material("material:terrain.seed", {
    albedoTexture: terrainTextures.albedo.id,
    normalTexture: terrainTextures.normal.id,
    materialTexture: terrainTextures.material.id,
    albedoFactor: vec4(1, 1, 1, 1),
    specular: vec3(0.55, 0.58, 0.52),
    specularFactor: 0.04,
    flags: MATERIAL_FLAG_TRIPLANAR_ALBEDO,
    textureScale: 0.08
  });
  const playerMarkerMaterial = new Material("material:player.marker", {
    albedoFactor: vec4(1, 1, 1, 1),
    specular: vec3(1, 0.92, 0.65),
    specularFactor: 0.45
  });
  const playerEntity = scene.createEntity("Player");
  const terrainEntity = scene.createEntity("Terrain");
  const playerMarkerEntity = scene.createEntity("Player marker");
  const initialPlayerPosition = vec3(0, field.heightAt(0, 0), 0);
  const initialDebugPosition = vec3(14, field.heightAt(0, 0) + 12, 18);

  scene.resources.addMesh(playerMarker);
  scene.resources.addTexture(terrainTextures.albedo);
  scene.resources.addTexture(terrainTextures.normal);
  scene.resources.addTexture(terrainTextures.material);
  scene.resources.addMaterial(terrainMaterial);
  scene.resources.addMaterial(playerMarkerMaterial);
  const terrainRenderPackets = createTerrainCoreRenderPacketStore(terrainCore, {
    material: terrainMaterial.id,
    itemIdPrefix: "terrain:rust",
    meshIdPrefix: "mesh:terrain.chunk"
  });
  const terrainStreamer = terrainEntity.addComponent(new TerrainCoreWorkerStreamer(
    terrainRenderPackets,
    terrainStreamScheduler,
    terrainDensityChunkStore,
    terrainWorker,
    {
      target: playerEntity,
      material: terrainMaterial.id,
      cellSize: terrainStreamConfig.cellSize
    }
  ));
  playerEntity.transform.setPosition(initialPlayerPosition);
  const playerController = createPlayerController(
    playerEntity,
    engineCore,
    initialPlayerPosition,
    initialDebugPosition,
    (x, z) => field.heightAt(x, z)
  );

  const markerRenderer = playerMarkerEntity.addComponent(
    new MeshRenderer(playerMarker.id, playerMarkerMaterial.id)
  );
  markerRenderer.visible = false;
  playerEntity.addChild(playerMarkerEntity);
  terrainStreamer.syncAround(playerEntity.transform.getWorldPosition());
  let renderPacketRuntime: "rust" | "typescript" = "typescript";
  window.__ofgDebug = {
    getLoadedTerrainChunkKeys: () => terrainStreamer.getLoadedChunkKeys(),
    getTerrainChunkKeys: () => terrainRenderPackets.chunks.map((chunk) => chunk.key).sort(),
    getTerrainPreset: () => descriptor.terrainPreset,
    getTerrainSeed: () => descriptor.seed,
    getTerrainStreamStatus: () => terrainStreamer.getStreamStatus(),
    getTerrainStreamerRuntime: () => terrainStreamer.runtime,
    getTerrainStreamSchedulerRuntime: () => "rust",
    getTerrainDensityStoreRuntime: () => terrainDensityChunkStore.runtime,
    getTerrainWorkerPoolRuntime: () => terrainWorker.workerPoolRuntime,
    getRenderPacketRuntime: () => renderPacketRuntime,
    getTerrainRenderPacketRuntime: () => "rust",
    getTerrainWorkerCount: () => terrainWorker.workerCount,
    getTerrainDebugOverlayMode: () => terrainDebugOverlay.getState(),
    getPlayerControllerRuntime: () => "rust",
    setTerrainDebugOverlayMode(mode) {
      terrainDebugOverlay.setState(validateTerrainDebugOverlayState(mode));
      terrainDebugOverlay.render(field, playerEntity.transform.getWorldPosition());
    },
    cycleTerrainDebugOverlayMode() {
      const mode = terrainDebugOverlay.cycleState();
      terrainDebugOverlay.render(field, playerEntity.transform.getWorldPosition());
      return mode;
    },
    resetTerrainStreaming() {
      terrainStreamer.resetStreaming(playerEntity.transform.getWorldPosition());
    },
    getTerrainHeight(x, z) {
      return field.heightAt(x, z);
    },
    setCameraMode(mode) {
      playerController.mode = validatePlayerMode(mode);
    },
    setDebugCamera(x, y, z, yaw, pitch) {
      playerController.setDebugCamera(vec3(x, y, z), yaw, pitch);
    },
    setPlayerPosition(x, z) {
      playerController.setPlayerPosition(vec3(x, field.heightAt(x, z), z));
      terrainStreamer.syncAround(playerEntity.transform.getWorldPosition());
      terrainDebugOverlay.render(field, playerEntity.transform.getWorldPosition());
    }
  };

  await renderer.initialize();
  input.attach(elements.canvas);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const deltaSeconds = computeFrameDeltaSeconds(timestamp, lastTimestamp);
    lastTimestamp = timestamp;

    if (input.consumePress("KeyC") || input.consumePress("F1")) {
      playerController.toggleCameraMode();
    }

    if (input.consumePress("F2")) {
      terrainDebugOverlay.cycleState();
    }

    const snapshot = input.consumeFrameSnapshot();
    const intent = readMovementIntent(input, snapshot.mouseDeltaX, snapshot.mouseDeltaY);

    playerController.setMovementIntent(intent);
    scene.update(deltaSeconds);
    terrainStreamer.syncAround(playerEntity.transform.getWorldPosition());
    terrainDebugOverlay.update(deltaSeconds, field, playerEntity.transform.getWorldPosition());

    const aspect = renderer.getAspectRatio();
    const renderSnapshot = engineCore.renderSnapshot();
    if (renderSnapshot === undefined) {
      throw new Error("Rust engine did not produce a render snapshot.");
    }
    markerRenderer.visible = renderSnapshot.playerMarker.visible;
    renderPacketRuntime = "rust";
    renderer.render(SceneRenderExtractor.buildRenderWorld(aspect, {
      camera: cameraFrameFromEnginePacket(renderSnapshot.camera, aspect),
      mainLight: directionalLightFromEnginePacket(renderSnapshot.mainLight),
      additionalItems: terrainRenderPackets.getRenderItems(scene.resources)
    }));

    elements.cameraMode.textContent = playerController.mode === "firstPerson" ? "FIRST" : "FLY";
    elements.cameraMode.dataset.mode = playerController.mode;
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

async function loadRequiredTerrainCore(): Promise<TerrainCoreWasmInstance> {
  return loadTerrainCoreWasm();
}

function createRequiredTerrainWorker(
  descriptor: WorldDescriptor,
  terrainCore: TerrainCoreWasmInstance
): TerrainChunkWorkerClient {
  const worker = createTerrainChunkWorkerClient(descriptor, terrainCore);
  if (worker === undefined) {
    throw new Error("Terrain workers are required for the playable Rust terrain runtime.");
  }

  return worker;
}

async function loadRequiredEngineCore(): Promise<EngineCoreWasmHandle> {
  const handle = new EngineCoreWasmHandle(await loadEngineCoreWasm());
  handle.reset();
  return handle;
}

function readTerrainPreset(value: string | null): TerrainPresetId | undefined {
  if (value === null || value.trim() === "") {
    return undefined;
  }

  if (TERRAIN_PRESET_IDS.some((terrainPreset) => terrainPreset === value)) {
    return value as TerrainPresetId;
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

function readTerrainDebugOverlayState(): TerrainDebugOverlayState {
  const params = new URLSearchParams(window.location.search);
  const value = params.get("terrainDebug");

  if (value === null || value.trim() === "" || value === "off") {
    return "off";
  }

  if (isTerrainDebugOverlayMode(value)) {
    return value;
  }

  console.warn(`Unknown terrain debug overlay '${value}', hiding the debug overlay.`);
  return "off";
}

function validateTerrainDebugOverlayState(mode: string): TerrainDebugOverlayState {
  if (mode === "off" || isTerrainDebugOverlayMode(mode)) {
    return mode;
  }

  throw new Error(`Unknown terrain debug overlay '${mode}'.`);
}

function validatePlayerMode(mode: string): PlayerMode {
  if (mode === "firstPerson" || mode === "debugFly") {
    return mode;
  }

  throw new Error(`Unknown player camera mode '${mode}'.`);
}

function createPlayerController(
  playerEntity: Entity,
  engineCore: EngineCoreWasmHandle,
  initialPlayerPosition: Vec3,
  initialDebugPosition: Vec3,
  terrainHeightAt: (x: number, z: number) => number | undefined
): RustPlayerController {
  return playerEntity.addComponent(new RustPlayerController(engineCore, {
    initialPosition: initialPlayerPosition,
    initialYaw: Math.PI * 0.18,
    initialPitch: -0.08,
    initialDebugPosition,
    initialDebugYaw: Math.PI * 1.24,
    initialDebugPitch: -0.48,
    terrainHeightAt
  }));
}

function meshFromData(id: string, data: MeshData): Mesh {
  return new Mesh(id, data.vertices, data.indices, POSITION_COLOR_NORMAL_UV_LAYOUT);
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
