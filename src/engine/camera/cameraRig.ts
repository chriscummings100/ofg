import {
  add,
  clamp,
  normalize,
  scale,
  vec3,
  VEC3_UP,
  yawPitchForward,
  yawRight,
  type Vec3
} from "../math/vec3.js";
import { inverseMat4, lookAtMat4, multiplyMat4, perspectiveMat4, type Mat4 } from "../math/mat4.js";

export type CameraMode = "firstPerson" | "debugFly";

export type MovementIntent = {
  readonly forward: number;
  readonly right: number;
  readonly up: number;
  readonly fast: boolean;
  readonly lookDeltaX: number;
  readonly lookDeltaY: number;
};

export type CameraRig = {
  mode: CameraMode;
  playerPosition: Vec3;
  playerYaw: number;
  playerPitch: number;
  debugPosition: Vec3;
  debugYaw: number;
  debugPitch: number;
};

export type CameraFrame = {
  readonly eye: Vec3;
  readonly target: Vec3;
  readonly viewProjection: Mat4;
  readonly inverseViewProjection: Mat4;
};

const PLAYER_EYE_HEIGHT = 1.65;
const PLAYER_SPEED = 5.5;
const DEBUG_SPEED = 11;
const FAST_MULTIPLIER = 3;
const LOOK_SENSITIVITY = 0.0025;
const MAX_PITCH = Math.PI * 0.48;

export function createCameraRig(groundHeight: number): CameraRig {
  return {
    mode: "firstPerson",
    playerPosition: vec3(0, groundHeight, 0),
    playerYaw: Math.PI * 0.18,
    playerPitch: -0.08,
    debugPosition: vec3(14, groundHeight + 12, 18),
    debugYaw: Math.PI * 1.24,
    debugPitch: -0.48
  };
}

export function toggleCameraMode(rig: CameraRig): void {
  rig.mode = rig.mode === "firstPerson" ? "debugFly" : "firstPerson";
}

export function updateCameraRig(
  rig: CameraRig,
  intent: MovementIntent,
  deltaSeconds: number,
  groundHeightAt: (x: number, z: number) => number
): void {
  if (rig.mode === "firstPerson") {
    updateFirstPerson(rig, intent, deltaSeconds, groundHeightAt);
    return;
  }

  updateDebugFly(rig, intent, deltaSeconds);
}

export function getCameraFrame(rig: CameraRig, aspect: number): CameraFrame {
  const eye = rig.mode === "firstPerson"
    ? add(rig.playerPosition, vec3(0, PLAYER_EYE_HEIGHT, 0))
    : rig.debugPosition;
  const forward = yawPitchForward(
    rig.mode === "firstPerson" ? rig.playerYaw : rig.debugYaw,
    rig.mode === "firstPerson" ? rig.playerPitch : rig.debugPitch
  );
  const target = add(eye, forward);
  const projection = perspectiveMat4((70 * Math.PI) / 180, aspect, 0.05, 500);
  const view = lookAtMat4(eye, target, VEC3_UP);
  const viewProjection = multiplyMat4(projection, view);

  return {
    eye,
    target,
    viewProjection,
    inverseViewProjection: inverseMat4(viewProjection)
  };
}

export function getPlayerMarkerCenter(rig: CameraRig): Vec3 {
  return add(rig.playerPosition, vec3(0, 0.9, 0));
}

function updateFirstPerson(
  rig: CameraRig,
  intent: MovementIntent,
  deltaSeconds: number,
  groundHeightAt: (x: number, z: number) => number
): void {
  rig.playerYaw -= intent.lookDeltaX * LOOK_SENSITIVITY;
  rig.playerPitch = clamp(
    rig.playerPitch - intent.lookDeltaY * LOOK_SENSITIVITY,
    -MAX_PITCH,
    MAX_PITCH
  );

  const forward = yawPitchForward(rig.playerYaw, 0);
  const right = yawRight(rig.playerYaw);
  const planarMove = normalize(add(scale(forward, intent.forward), scale(right, intent.right)));
  const nextPosition = add(rig.playerPosition, scale(planarMove, PLAYER_SPEED * deltaSeconds));

  rig.playerPosition = vec3(
    nextPosition.x,
    groundHeightAt(nextPosition.x, nextPosition.z),
    nextPosition.z
  );
}

function updateDebugFly(rig: CameraRig, intent: MovementIntent, deltaSeconds: number): void {
  rig.debugYaw -= intent.lookDeltaX * LOOK_SENSITIVITY;
  rig.debugPitch = clamp(
    rig.debugPitch - intent.lookDeltaY * LOOK_SENSITIVITY,
    -MAX_PITCH,
    MAX_PITCH
  );

  const speed = DEBUG_SPEED * (intent.fast ? FAST_MULTIPLIER : 1);
  const forward = yawPitchForward(rig.debugYaw, rig.debugPitch);
  const right = yawRight(rig.debugYaw);
  const move = normalize(add(
    add(scale(forward, intent.forward), scale(right, intent.right)),
    scale(VEC3_UP, intent.up)
  ));

  rig.debugPosition = add(rig.debugPosition, scale(move, speed * deltaSeconds));
}
