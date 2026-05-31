import { equal, ok } from "node:assert/strict";
import { createCameraRig, toggleCameraMode, updateCameraRig } from "./cameraRig.js";

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
});
