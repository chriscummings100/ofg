import type { DirectionalLight } from "./Lighting.js";
import type { CameraFrame } from "./CameraFrame.js";

export const FRAME_UNIFORM_FLOATS = 44;
export const FRAME_UNIFORM_BYTES = FRAME_UNIFORM_FLOATS * Float32Array.BYTES_PER_ELEMENT;

export function buildFrameUniformValues(
  camera: CameraFrame,
  mainLight: DirectionalLight,
  target: Float32Array<ArrayBufferLike> = new Float32Array(FRAME_UNIFORM_FLOATS)
): Float32Array<ArrayBufferLike> {
  target.set(camera.viewProjection, 0);
  target.set(camera.inverseViewProjection, 16);
  target[32] = camera.eye.x;
  target[33] = camera.eye.y;
  target[34] = camera.eye.z;
  target[35] = 1;
  target[36] = mainLight.direction.x;
  target[37] = mainLight.direction.y;
  target[38] = mainLight.direction.z;
  target[39] = mainLight.intensity;
  target[40] = mainLight.color.x;
  target[41] = mainLight.color.y;
  target[42] = mainLight.color.z;
  target[43] = mainLight.ambient;

  return target;
}
