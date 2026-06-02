import { InputTracker } from "../engine/input/inputTracker.js";
import { computeFrameDeltaSeconds } from "./frameTiming.js";
import {
  EngineCoreWasmHandle,
  loadEngineCoreWasm,
  type EngineCoreRenderDebugMarkerPacket
} from "../engine/core/engineCoreWasm.js";
import { identityMat4, type Mat4 } from "../engine/math/mat4.js";
import { vec3, type Vec3 } from "../engine/math/vec3.js";
import { vec4 } from "../engine/math/vec4.js";
import { MATERIAL_FLAG_TRIPLANAR_ALBEDO, Material } from "../engine/render/Material.js";
import { Mesh } from "../engine/render/Mesh.js";
import { createTerrainCoreRenderPacketStore } from "../engine/render/TerrainCoreRenderPackets.js";
import {
  cameraFrameFromEnginePacket,
  directionalLightFromEnginePacket
} from "../engine/render/engineRenderPackets.js";
import { loadTerrainMaterialTextures } from "../engine/render/terrainTextures.js";
import { WebGpuRenderer } from "../engine/render/webgpuRenderer.js";
import type { RenderItem, RenderWorld } from "../engine/render/RenderWorld.js";
import { TerrainCoreWorkerStreamer } from "../game/components/TerrainCoreWorkerStreamer.js";
import { createBoxMesh } from "../engine/world/primitiveMesh.js";
import {
  createSeedWorldDescriptor,
  isTerrainPresetId,
  type TerrainPresetId,
  type WorldDescriptor
} from "../engine/world/terrainDescriptor.js";
import { createTerrainCoreDensityChunkStore } from "../engine/world/terrainCoreDensityChunkStore.js";
import {
  loadTerrainCoreWasm,
  terrainPresetToWasmCode,
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

type GameElements = {
  readonly canvas: HTMLCanvasElement;
  readonly cameraMode: HTMLElement;
  readonly frameTime: HTMLElement;
};

type TerrainHeightSampler = (x: number, z: number) => number;

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
  const renderer = new WebGpuRenderer(elements.canvas);
  const input = new InputTracker();
  const descriptor = readWorldDescriptor();
  const terrainCore = await loadRequiredTerrainCore();
  const terrainHeightAt = createTerrainHeightSampler(terrainCore, descriptor);
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
  const terrainRenderPackets = createTerrainCoreRenderPacketStore(terrainCore, {
    material: terrainMaterial,
    albedoTexture: terrainTextures.albedo,
    normalTexture: terrainTextures.normal,
    materialTexture: terrainTextures.material,
    itemIdPrefix: "terrain:rust",
    meshIdPrefix: "mesh:terrain.chunk"
  });
  const initialPlayerPosition = vec3(0, terrainHeightAt(0, 0), 0);
  const initialDebugPosition = vec3(14, terrainHeightAt(0, 0) + 12, 18);
  const playerController = createPlayerController(
    engineCore,
    initialPlayerPosition,
    initialDebugPosition,
    terrainHeightAt
  );
  const terrainStreamer = new TerrainCoreWorkerStreamer(
    terrainRenderPackets,
    terrainStreamScheduler,
    terrainDensityChunkStore,
    terrainWorker,
    {
      getTargetPosition: () => playerController.getPlayerPosition(),
      material: terrainMaterial.id,
      cellSize: terrainStreamConfig.cellSize
    }
  );

  terrainStreamer.syncAround(playerController.getPlayerPosition());
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
    getPlayerControllerRuntime: () => "rust",
    resetTerrainStreaming() {
      terrainStreamer.resetStreaming(playerController.getPlayerPosition());
    },
    getTerrainHeight(x, z) {
      return terrainHeightAt(x, z);
    },
    setCameraMode(mode) {
      playerController.mode = validatePlayerMode(mode);
    },
    setDebugCamera(x, y, z, yaw, pitch) {
      playerController.setDebugCamera(vec3(x, y, z), yaw, pitch);
    },
    setPlayerPosition(x, z) {
      playerController.setPlayerPosition(vec3(x, terrainHeightAt(x, z), z));
      terrainStreamer.syncAround(playerController.getPlayerPosition());
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

    const snapshot = input.consumeFrameSnapshot();
    const intent = readMovementIntent(input, snapshot.mouseDeltaX, snapshot.mouseDeltaY);

    playerController.setMovementIntent(intent);
    playerController.update(deltaSeconds);
    terrainStreamer.update();

    const aspect = renderer.getAspectRatio();
    const renderSnapshot = engineCore.renderSnapshot();
    if (renderSnapshot === undefined) {
      throw new Error("Rust engine did not produce a render snapshot.");
    }
    renderPacketRuntime = "rust";
    const renderItems = terrainRenderPackets.getRenderItems();
    const markerItem = playerMarkerRenderItem(
      playerMarker,
      playerMarkerMaterial,
      renderSnapshot.playerMarker
    );
    if (markerItem !== undefined) {
      renderItems.push(markerItem);
    }
    const renderWorld: RenderWorld = {
      camera: cameraFrameFromEnginePacket(renderSnapshot.camera, aspect),
      mainLight: directionalLightFromEnginePacket(renderSnapshot.mainLight),
      items: renderItems
    };
    renderer.render(renderWorld);

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

function createTerrainHeightSampler(
  terrainCore: TerrainCoreWasmInstance,
  descriptor: WorldDescriptor
): TerrainHeightSampler {
  const preset = terrainPresetToWasmCode(descriptor.terrainPreset);

  return (x, z) => terrainCore.exports.ofg_height_at(descriptor.seed, preset, x, z);
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

function createPlayerController(
  engineCore: EngineCoreWasmHandle,
  initialPlayerPosition: Vec3,
  initialDebugPosition: Vec3,
  terrainHeightAt: TerrainHeightSampler
): RustPlayerController {
  return new RustPlayerController(engineCore, {
    initialPosition: initialPlayerPosition,
    initialYaw: Math.PI * 0.18,
    initialPitch: -0.08,
    initialDebugPosition,
    initialDebugYaw: Math.PI * 1.24,
    initialDebugPitch: -0.48,
    terrainHeightAt
  });
}

function playerMarkerRenderItem(
  mesh: Mesh,
  material: Material,
  packet: EngineCoreRenderDebugMarkerPacket
): RenderItem | undefined {
  if (!packet.visible) {
    return undefined;
  }

  return {
    id: "player.marker",
    mesh,
    material,
    worldMatrix: translationMat4(vec3(packet.position.x, packet.position.y, packet.position.z))
  };
}

function translationMat4(position: Vec3): Mat4 {
  const matrix = identityMat4();
  matrix[12] = position.x;
  matrix[13] = position.y;
  matrix[14] = position.z;

  return matrix;
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
