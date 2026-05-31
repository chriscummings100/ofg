import type { CameraFrame } from "../camera/cameraRig.js";
import type { DirectionalLight } from "./Lighting.js";

export const FRAME_UNIFORM_FLOATS = 44;
export const FRAME_UNIFORM_BYTES = FRAME_UNIFORM_FLOATS * Float32Array.BYTES_PER_ELEMENT;

export function buildFrameUniformValues(
  camera: CameraFrame,
  mainLight: DirectionalLight
): Float32Array {
  const values = new Float32Array(FRAME_UNIFORM_FLOATS);

  values.set(camera.viewProjection, 0);
  values.set(camera.inverseViewProjection, 16);
  values[32] = camera.eye.x;
  values[33] = camera.eye.y;
  values[34] = camera.eye.z;
  values[35] = 1;
  values[36] = mainLight.direction.x;
  values[37] = mainLight.direction.y;
  values[38] = mainLight.direction.z;
  values[39] = mainLight.intensity;
  values[40] = mainLight.color.x;
  values[41] = mainLight.color.y;
  values[42] = mainLight.color.z;
  values[43] = mainLight.ambient;

  return values;
}
