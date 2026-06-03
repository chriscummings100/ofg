import type { Vec3 } from "../math/vec3.js";

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
