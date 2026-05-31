import { Component } from "../../engine/scene/Component.js";
import { getScene } from "../../engine/scene/activeScene.js";
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
} from "../../engine/math/vec3.js";
import { quatFromYaw } from "../../engine/math/quat.js";

export type PlayerMode = "firstPerson" | "debugFly";

export type PlayerMovementIntent = {
  readonly forward: number;
  readonly right: number;
  readonly up: number;
  readonly fast: boolean;
  readonly lookDeltaX: number;
  readonly lookDeltaY: number;
};

export type TransformSnapshot = {
  readonly position: Vec3;
  readonly yaw: number;
  readonly pitch: number;
};

const ZERO_INTENT: PlayerMovementIntent = Object.freeze({
  forward: 0,
  right: 0,
  up: 0,
  fast: false,
  lookDeltaX: 0,
  lookDeltaY: 0
});

const FAST_MULTIPLIER = 3;
const LOOK_SENSITIVITY = 0.0025;
const MAX_PITCH = Math.PI * 0.48;

export class PlayerController extends Component {
  moveSpeed = 5.5;
  debugFlySpeed = 11;
  eyeHeight = 1.65;
  mode: PlayerMode = "firstPerson";
  yaw = 0;
  pitch = 0;
  debugPosition: Vec3 = vec3(0, 12, 0);
  debugYaw = 0;
  debugPitch = -0.35;

  private movementIntent: PlayerMovementIntent = ZERO_INTENT;

  setMovementIntent(intent: PlayerMovementIntent): void {
    this.movementIntent = intent;
  }

  override update(deltaSeconds: number): void {
    if (!this.enabled || this.entity === undefined) {
      return;
    }

    if (this.mode === "firstPerson") {
      this.yaw -= this.movementIntent.lookDeltaX * LOOK_SENSITIVITY;
      this.pitch = clamp(
        this.pitch - this.movementIntent.lookDeltaY * LOOK_SENSITIVITY,
        -MAX_PITCH,
        MAX_PITCH
      );
      this.updateFirstPerson(deltaSeconds);
      this.entity.transform.setRotation(quatFromYaw(this.yaw));
    } else {
      this.debugYaw -= this.movementIntent.lookDeltaX * LOOK_SENSITIVITY;
      this.debugPitch = clamp(
        this.debugPitch - this.movementIntent.lookDeltaY * LOOK_SENSITIVITY,
        -MAX_PITCH,
        MAX_PITCH
      );
      this.updateDebugFly(deltaSeconds);
    }
  }

  toggleCameraMode(): void {
    this.mode = this.mode === "firstPerson" ? "debugFly" : "firstPerson";
  }

  getEyeTransform(): TransformSnapshot {
    if (this.entity === undefined) {
      throw new Error("PlayerController must be attached to an entity before reading eye transform.");
    }

    if (this.mode === "debugFly") {
      return {
        position: this.debugPosition,
        yaw: this.debugYaw,
        pitch: this.debugPitch
      };
    }

    return {
      position: add(this.entity.transform.getWorldPosition(), vec3(0, this.eyeHeight, 0)),
      yaw: this.yaw,
      pitch: this.pitch
    };
  }

  private updateFirstPerson(deltaSeconds: number): void {
    const entity = this.requireEntity();
    const forward = yawPitchForward(this.yaw, 0);
    const right = yawRight(this.yaw);
    const planarMove = normalize(add(
      scale(forward, this.movementIntent.forward),
      scale(right, this.movementIntent.right)
    ));
    const nextPosition = add(
      entity.transform.position,
      scale(planarMove, this.moveSpeed * speedMultiplier(this.movementIntent) * deltaSeconds)
    );
    const terrainHeight = getScene().getTerrainHeight(nextPosition.x, nextPosition.z);

    entity.transform.setPosition(vec3(
      nextPosition.x,
      terrainHeight ?? nextPosition.y,
      nextPosition.z
    ));
  }

  private updateDebugFly(deltaSeconds: number): void {
    const forward = yawPitchForward(this.debugYaw, this.debugPitch);
    const right = yawRight(this.debugYaw);
    const move = normalize(add(
      add(scale(forward, this.movementIntent.forward), scale(right, this.movementIntent.right)),
      scale(VEC3_UP, this.movementIntent.up)
    ));

    this.debugPosition = add(
      this.debugPosition,
      scale(move, this.debugFlySpeed * speedMultiplier(this.movementIntent) * deltaSeconds)
    );
  }

  private requireEntity() {
    if (this.entity === undefined) {
      throw new Error("PlayerController must be attached to an entity before updating.");
    }

    return this.entity;
  }
}

function speedMultiplier(intent: PlayerMovementIntent): number {
  return intent.fast ? FAST_MULTIPLIER : 1;
}
