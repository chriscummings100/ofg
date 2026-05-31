import { InputTracker } from "../engine/input/inputTracker.js";
import { quatFromYawPitch } from "../engine/math/quat.js";
import { vec3 } from "../engine/math/vec3.js";
import { vec4 } from "../engine/math/vec4.js";
import { MATERIAL_FLAG_TRIPLANAR_ALBEDO, Material } from "../engine/render/Material.js";
import { Mesh } from "../engine/render/Mesh.js";
import { MeshRenderer } from "../engine/render/MeshRenderer.js";
import { SceneRenderExtractor } from "../engine/render/SceneRenderExtractor.js";
import { TerrainRenderer } from "../engine/render/TerrainRenderer.js";
import { loadTerrainAlbedoTexture } from "../engine/render/terrainTextures.js";
import { WebGpuRenderer } from "../engine/render/webgpuRenderer.js";
import { createDirectionalLight } from "../engine/render/Lighting.js";
import { createScene } from "../engine/scene/activeScene.js";
import type { Entity } from "../engine/scene/Entity.js";
import {
  isTerrainDebugOverlayMode,
  type TerrainDebugOverlayState
} from "../engine/world/terrainDebugOverlay.js";
import { TerrainChunkStreamer } from "../game/components/TerrainChunkStreamer.js";
import { createBoxMesh } from "../engine/world/primitiveMesh.js";
import { EditableTerrainDensitySource } from "../engine/world/terrainChunk.js";
import {
  createSeedWorldDescriptor,
  createTerrainGenerator,
  TERRAIN_PRESET_IDS,
  type TerrainPresetId,
  type WorldDescriptor
} from "../engine/world/terrainGenerator.js";
import {
  POSITION_COLOR_NORMAL_UV_LAYOUT,
  type MeshData
} from "../engine/world/terrainMesh.js";
import {
  PlayerController,
  type PlayerMovementIntent,
  type TransformSnapshot
} from "../game/components/PlayerController.js";
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
      getTerrainDebugOverlayMode: () => TerrainDebugOverlayState;
      setTerrainDebugOverlayMode: (mode: TerrainDebugOverlayState) => void;
      cycleTerrainDebugOverlayMode: () => TerrainDebugOverlayState;
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
  const terrainDebugOverlay = new TerrainDebugOverlayView(
    elements.terrainDebugOverlay,
    readTerrainDebugOverlayState()
  );
  const terrainSource = new EditableTerrainDensitySource(field);
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
  const terrainAlbedo = await loadTerrainAlbedoTexture();
  const terrainMaterial = new Material("material:terrain.seed", {
    albedoTexture: terrainAlbedo.id,
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
  const cameraEntity = scene.createEntity("Camera");

  scene.resources.addMesh(playerMarker);
  scene.resources.addTexture(terrainAlbedo);
  scene.resources.addMaterial(terrainMaterial);
  scene.resources.addMaterial(playerMarkerMaterial);
  const terrainRenderer = terrainEntity.addComponent(new TerrainRenderer(field));
  const terrainStreamer = terrainEntity.addComponent(new TerrainChunkStreamer(
    terrainRenderer,
    terrainSource,
    {
      target: playerEntity,
      material: terrainMaterial.id,
      horizontalRadius: 1,
      verticalChunkOffsets: [-2, -1, 0, 1],
      cellSize: 1
    }
  ));
  playerEntity.transform.setPosition(vec3(0, field.heightAt(0, 0), 0));
  const playerController = playerEntity.addComponent(new PlayerController());
  playerController.yaw = Math.PI * 0.18;
  playerController.pitch = -0.08;
  playerController.debugPosition = vec3(14, field.heightAt(0, 0) + 12, 18);
  playerController.debugYaw = Math.PI * 1.24;
  playerController.debugPitch = -0.48;

  const markerRenderer = playerMarkerEntity.addComponent(
    new MeshRenderer(playerMarker.id, playerMarkerMaterial.id)
  );
  markerRenderer.visible = false;
  playerEntity.addChild(playerMarkerEntity);
  scene.activeCamera = cameraEntity;
  syncCameraEntity(cameraEntity, playerController.getEyeTransform());
  terrainStreamer.syncAround(playerEntity.transform.getWorldPosition());
  window.__ofgDebug = {
    getLoadedTerrainChunkKeys: () => terrainStreamer.getLoadedChunkKeys(),
    getTerrainChunkKeys: () => terrainRenderer.chunks.map((chunk) => chunk.key).sort(),
    getTerrainPreset: () => descriptor.terrainPreset,
    getTerrainDebugOverlayMode: () => terrainDebugOverlay.getState(),
    setTerrainDebugOverlayMode(mode) {
      terrainDebugOverlay.setState(validateTerrainDebugOverlayState(mode));
      terrainDebugOverlay.render(field, playerEntity.transform.getWorldPosition());
    },
    cycleTerrainDebugOverlayMode() {
      const mode = terrainDebugOverlay.cycleState();
      terrainDebugOverlay.render(field, playerEntity.transform.getWorldPosition());
      return mode;
    },
    setPlayerPosition(x, z) {
      playerEntity.transform.setPosition(vec3(x, field.heightAt(x, z), z));
      terrainStreamer.syncAround(playerEntity.transform.getWorldPosition());
      syncCameraEntity(cameraEntity, playerController.getEyeTransform());
      terrainDebugOverlay.render(field, playerEntity.transform.getWorldPosition());
    }
  };

  await renderer.initialize();
  input.attach(elements.canvas);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const deltaSeconds = Math.min(0.05, (timestamp - lastTimestamp) / 1000);
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
    markerRenderer.visible = playerController.mode === "debugFly";
    syncCameraEntity(cameraEntity, playerController.getEyeTransform());
    terrainDebugOverlay.update(deltaSeconds, field, playerEntity.transform.getWorldPosition());

    renderer.render(SceneRenderExtractor.buildRenderWorld(renderer.getAspectRatio()));

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

  if (terrainPreset === undefined) {
    return createSeedWorldDescriptor();
  }

  return createSeedWorldDescriptor(undefined, { terrainPreset });
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

function meshFromData(id: string, data: MeshData): Mesh {
  return new Mesh(id, data.vertices, data.indices, POSITION_COLOR_NORMAL_UV_LAYOUT);
}

function syncCameraEntity(cameraEntity: Entity, eye: TransformSnapshot): void {
  cameraEntity.transform.setPosition(eye.position);
  cameraEntity.transform.setRotation(quatFromYawPitch(eye.yaw, eye.pitch));
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
