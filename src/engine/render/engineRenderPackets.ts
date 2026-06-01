import { vec3 } from "../math/vec3.js";
import { inverseMat4, lookAtMat4, multiplyMat4, perspectiveMat4 } from "../math/mat4.js";
import type {
  EngineCoreRenderCameraPacket,
  EngineCoreRenderLightPacket
} from "../core/engineCoreWasm.js";
import type { CameraFrame } from "./CameraFrame.js";
import type { DirectionalLight } from "./Lighting.js";

export function cameraFrameFromEnginePacket(
  packet: EngineCoreRenderCameraPacket,
  aspect: number
): CameraFrame {
  const eye = vec3(packet.eye.x, packet.eye.y, packet.eye.z);
  const target = vec3(packet.target.x, packet.target.y, packet.target.z);
  const projection = perspectiveMat4(
    packet.fovYRadians,
    aspect,
    packet.nearPlane,
    packet.farPlane
  );
  const view = lookAtMat4(eye, target);
  const viewProjection = multiplyMat4(projection, view);

  return {
    eye,
    target,
    viewProjection,
    inverseViewProjection: inverseMat4(viewProjection)
  };
}

export function directionalLightFromEnginePacket(
  packet: EngineCoreRenderLightPacket
): DirectionalLight {
  return {
    direction: vec3(packet.direction.x, packet.direction.y, packet.direction.z),
    color: vec3(packet.color.x, packet.color.y, packet.color.z),
    intensity: packet.intensity,
    ambient: packet.ambient
  };
}
