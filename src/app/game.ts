import {
  createCameraRig,
  getCameraFrame,
  getPlayerMarkerCenter,
  toggleCameraMode,
  updateCameraRig,
  type MovementIntent
} from "../engine/camera/cameraRig.js";
import { InputTracker } from "../engine/input/inputTracker.js";
import { vec3 } from "../engine/math/vec3.js";
import { WebGpuRenderer } from "../engine/render/webgpuRenderer.js";
import { createBoxMesh } from "../engine/world/primitiveMesh.js";
import { createSeedTerrainField } from "../engine/world/scalarField.js";
import { buildHeightfieldMesh } from "../engine/world/terrainMesh.js";

type GameElements = {
  readonly canvas: HTMLCanvasElement;
  readonly cameraMode: HTMLElement;
  readonly frameTime: HTMLElement;
};

export async function startGame(elements: GameElements): Promise<void> {
  const renderer = new WebGpuRenderer(elements.canvas);
  const input = new InputTracker();
  const field = createSeedTerrainField();
  const rig = createCameraRig(field.heightAt(0, 0));
  const terrainMesh = buildHeightfieldMesh(field, {
    halfExtent: 64,
    cellsPerAxis: 96
  });

  await renderer.initialize();
  renderer.setTerrainMesh(terrainMesh);
  input.attach(elements.canvas);

  let lastTimestamp = performance.now();

  function frame(timestamp: number): void {
    const deltaSeconds = Math.min(0.05, (timestamp - lastTimestamp) / 1000);
    lastTimestamp = timestamp;

    if (input.consumePress("KeyC") || input.consumePress("F1")) {
      toggleCameraMode(rig);
    }

    const snapshot = input.consumeFrameSnapshot();
    const intent = readMovementIntent(input, snapshot.mouseDeltaX, snapshot.mouseDeltaY);

    updateCameraRig(rig, intent, deltaSeconds, field.heightAt);
    renderer.setActorMesh(rig.mode === "debugFly"
      ? createBoxMesh(
        getPlayerMarkerCenter(rig),
        vec3(0.28, 0.9, 0.22),
        vec3(0.96, 0.7, 0.24)
      )
      : undefined
    );

    const camera = getCameraFrame(rig, renderer.getAspectRatio());
    renderer.render(camera.viewProjection);

    elements.cameraMode.textContent = rig.mode === "firstPerson" ? "FIRST" : "FLY";
    elements.cameraMode.dataset.mode = rig.mode;
    elements.frameTime.textContent = `${(deltaSeconds * 1000).toFixed(1)} ms`;

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}

function readMovementIntent(
  input: InputTracker,
  lookDeltaX: number,
  lookDeltaY: number
): MovementIntent {
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
