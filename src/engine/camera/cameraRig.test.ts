import { equal, ok } from "node:assert/strict";
import {
  createCameraRig,
  getCameraFrame,
  getPlayerMarkerCenter,
  toggleCameraMode,
  updateCameraRig
} from "./cameraRig.js";

describe("cameraRig", () => {
  it("toggles between first-person and debug fly modes", () => {
    const rig = createCameraRig(3);

    equal(rig.mode, "firstPerson");
    toggleCameraMode(rig);
    equal(rig.mode, "debugFly");
  });

  it("grounds first-person player on sampled terrain", () => {
    const rig = createCameraRig(0);

    updateCameraRig(
      rig,
      {
        forward: 1,
        right: 0,
        up: 0,
        fast: false,
        lookDeltaX: 0,
        lookDeltaY: 0
      },
      0.5,
      (x, z) => x * 0.1 + z * 0.2
    );

    equal(rig.playerPosition.y, rig.playerPosition.x * 0.1 + rig.playerPosition.z * 0.2);
  });

  it("first-person movement ignores vertical intent", () => {
    const rig = createCameraRig(2);

    updateCameraRig(
      rig,
      {
        forward: 0,
        right: 0,
        up: 1,
        fast: false,
        lookDeltaX: 0,
        lookDeltaY: 0
      },
      1,
      () => 2
    );

    equal(rig.playerPosition.y, 2);
  });

  it("clamps first-person pitch", () => {
    const rig = createCameraRig(0);

    updateCameraRig(
      rig,
      {
        forward: 0,
        right: 0,
        up: 0,
        fast: false,
        lookDeltaX: 0,
        lookDeltaY: 100000
      },
      1,
      () => 0
    );

    ok(rig.playerPitch > -Math.PI / 2);
    ok(rig.playerPitch < -1);
  });

  it("moves debug fly camera vertically", () => {
    const rig = createCameraRig(0);
    toggleCameraMode(rig);
    const initialY = rig.debugPosition.y;

    updateCameraRig(
      rig,
      {
        forward: 0,
        right: 0,
        up: 1,
        fast: false,
        lookDeltaX: 0,
        lookDeltaY: 0
      },
      1,
      () => 0
    );

    ok(rig.debugPosition.y > initialY);
  });

  it("fast debug fly movement travels farther", () => {
    const slowRig = createCameraRig(0);
    const fastRig = createCameraRig(0);
    toggleCameraMode(slowRig);
    toggleCameraMode(fastRig);

    const intent = {
      forward: 1,
      right: 0,
      up: 0,
      lookDeltaX: 0,
      lookDeltaY: 0
    };
    updateCameraRig(slowRig, { ...intent, fast: false }, 1, () => 0);
    updateCameraRig(fastRig, { ...intent, fast: true }, 1, () => 0);

    ok(Math.hypot(fastRig.debugPosition.x - 14, fastRig.debugPosition.z - 18) >
      Math.hypot(slowRig.debugPosition.x - 14, slowRig.debugPosition.z - 18));
  });

  it("returns first-person camera frame at eye height", () => {
    const rig = createCameraRig(3);
    const frame = getCameraFrame(rig, 1);

    equal(frame.eye.y, 4.65);
    equal(frame.inverseViewProjection.length, 16);
  });

  it("returns debug camera frame from debug position", () => {
    const rig = createCameraRig(3);
    toggleCameraMode(rig);
    const frame = getCameraFrame(rig, 1);

    equal(frame.eye, rig.debugPosition);
  });

  it("returns player marker center above player position", () => {
    const rig = createCameraRig(3);
    const center = getPlayerMarkerCenter(rig);

    equal(center.x, rig.playerPosition.x);
    equal(center.y, rig.playerPosition.y + 0.9);
    equal(center.z, rig.playerPosition.z);
  });
});
