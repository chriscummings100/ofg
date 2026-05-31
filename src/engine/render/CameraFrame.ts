import type { Mat4 } from "../math/mat4.js";
import type { Vec3 } from "../math/vec3.js";

export type CameraFrame = {
  readonly eye: Vec3;
  readonly target: Vec3;
  readonly viewProjection: Mat4;
  readonly inverseViewProjection: Mat4;
};
