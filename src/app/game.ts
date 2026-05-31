import { InputTracker } from "../engine/input/inputTracker.js";
import { quatFromYawPitch } from "../engine/math/quat.js";
import { vec3 } from "../engine/math/vec3.js";
import { vec4 } from "../engine/math/vec4.js";
import { Material } from "../engine/render/Material.js";
import { Mesh } from "../engine/render/Mesh.js";
import { MeshRenderer } from "../engine/render/MeshRenderer.js";
import { SceneRenderExtractor } from "../engine/render/SceneRenderExtractor.js";
import { TerrainRenderer } from "../engine/render/TerrainRenderer.js";
import { Texture } from "../engine/render/Texture.js";
import { WebGpuRenderer } from "../engine/render/webgpuRenderer.js";
import { createDirectionalLight } from "../engine/render/Lighting.js";
import { createScene } from "../engine/scene/activeScene.js";
import type { Entity } from "../engine/scene/Entity.js";
import { createBoxMesh } from "../engine/world/primitiveMesh.js";
import { createSeedTerrainField } from "../engine/world/scalarField.js";
import { terrainChunkCoord, terrainChunkKey } from "../engine/world/terrainChunk.js";
import {
  buildHeightfieldMesh,
  getFloatsPerVertex,
  type MeshData
} from "../engine/world/terrainMesh.js";
import {
  PlayerController,
  type PlayerMovementIntent,
  type TransformSnapshot
} from "../game/components/PlayerController.js";

type GameElements = {
  readonly canvas: HTMLCanvasElement;
  readonly cameraMode: HTMLElement;
  readonly frameTime: HTMLElement;
};

const POSITION_COLOR_LAYOUT = {
  floatsPerVertex: getFloatsPerVertex(),
  attributes: [
    { name: "position", offset: 0, size: 3 },
    { name: "color", offset: 3, size: 3 },
    { name: "normal", offset: 6, size: 3 },
    { name: "uv", offset: 9, size: 2 }
  ]
} as const;

export async function startGame(elements: GameElements): Promise<void> {
  const scene = createScene();
  const renderer = new WebGpuRenderer(elements.canvas);
  const input = new InputTracker();
  const field = createSeedTerrainField();
  scene.mainLight = createDirectionalLight({
    direction: vec3(0.89, 0.25, 0.38),
    color: vec3(1, 0.96, 0.88),
    intensity: 1,
    ambient: 0.34
  });
  const terrainMesh = buildHeightfieldMesh(field, {
    halfExtent: 64,
    cellsPerAxis: 96
  });
  const terrain = meshFromData("mesh:terrain.seed", terrainMesh);
  const playerMarker = meshFromData(
    "mesh:player.marker",
    createBoxMesh(vec3(0, 0.9, 0), vec3(0.28, 0.9, 0.22), vec3(0.96, 0.7, 0.24))
  );
  const terrainAlbedo = new Texture("texture:terrain.albedo", 1, 1, "rgba8unorm", {
    data: new Uint8Array([255, 255, 255, 255])
  });
  const terrainMaterial = new Material("material:terrain.seed", {
    albedoTexture: terrainAlbedo.id,
    albedoFactor: vec4(1, 1, 1, 1),
    specular: vec3(0.55, 0.58, 0.52),
    specularFactor: 0.04
  });
  const playerMarkerMaterial = new Material("material:player.marker", {
    albedoFactor: vec4(1, 1, 1, 1),
    specular: vec3(1, 0.92, 0.65),
    specularFactor: 0.45
  });
  const terrainEntity = scene.createEntity("Terrain");
  const playerEntity = scene.createEntity("Player");
  const playerMarkerEntity = scene.createEntity("Player marker");
  const cameraEntity = scene.createEntity("Camera");

  scene.resources.addMesh(terrain);
  scene.resources.addMesh(playerMarker);
  scene.resources.addTexture(terrainAlbedo);
  scene.resources.addMaterial(terrainMaterial);
  scene.resources.addMaterial(playerMarkerMaterial);
  terrainEntity.addComponent(new TerrainRenderer(
    field,
    [{
      key: terrainChunkKey(terrainChunkCoord(0, 0, 0)),
      mesh: terrain,
      material: terrainMaterial.id
    }]
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

  await renderer.initialize();
  input.attach(elements.canvas);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const deltaSeconds = Math.min(0.05, (timestamp - lastTimestamp) / 1000);
    lastTimestamp = timestamp;

    if (input.consumePress("KeyC") || input.consumePress("F1")) {
      playerController.toggleCameraMode();
    }

    const snapshot = input.consumeFrameSnapshot();
    const intent = readMovementIntent(input, snapshot.mouseDeltaX, snapshot.mouseDeltaY);

    playerController.setMovementIntent(intent);
    scene.update(deltaSeconds);
    markerRenderer.visible = playerController.mode === "debugFly";
    syncCameraEntity(cameraEntity, playerController.getEyeTransform());

    renderer.render(SceneRenderExtractor.buildRenderWorld(renderer.getAspectRatio()));

    elements.cameraMode.textContent = playerController.mode === "firstPerson" ? "FIRST" : "FLY";
    elements.cameraMode.dataset.mode = playerController.mode;
    elements.frameTime.textContent = `${(deltaSeconds * 1000).toFixed(1)} ms`;

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}

function meshFromData(id: string, data: MeshData): Mesh {
  return new Mesh(id, data.vertices, data.indices, POSITION_COLOR_LAYOUT);
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
