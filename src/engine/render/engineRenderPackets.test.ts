import { equal, ok } from "node:assert/strict";
import {
  cameraFrameFromEnginePacket,
  directionalLightFromEnginePacket
} from "./engineRenderPackets.js";

describe("engine render packets", () => {
  it("builds a camera frame from a Rust engine camera packet", () => {
    const frame = cameraFrameFromEnginePacket({
      eye: { x: 1, y: 2, z: 3 },
      target: { x: 1, y: 2, z: 4 },
      yaw: 0,
      pitch: 0,
      fovYRadians: 70 * Math.PI / 180,
      nearPlane: 0.05,
      farPlane: 500
    }, 16 / 9);

    equal(frame.eye.x, 1);
    equal(frame.eye.y, 2);
    equal(frame.eye.z, 3);
    equal(frame.target.z, 4);
    equal(frame.viewProjection.length, 16);
    equal(frame.inverseViewProjection.length, 16);
    ok(Number.isFinite(frame.viewProjection[0]));
    ok(Number.isFinite(frame.inverseViewProjection[0]));
  });

  it("converts a Rust engine light packet to render light data", () => {
    const light = directionalLightFromEnginePacket({
      direction: { x: 0.1, y: 0.9, z: 0.2 },
      color: { x: 1, y: 0.96, z: 0.88 },
      intensity: 1.25,
      ambient: 0.4
    });

    equal(light.direction.x, 0.1);
    equal(light.direction.y, 0.9);
    equal(light.direction.z, 0.2);
    equal(light.color.x, 1);
    equal(light.intensity, 1.25);
    equal(light.ambient, 0.4);
  });
});
