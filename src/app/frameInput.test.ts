import { deepEqual, equal } from "node:assert/strict";
import { buildBrowserFrameInput, clampFrameAxis } from "./frameInput.js";

describe("buildBrowserFrameInput", () => {
  it("clamps combined keyboard and touch movement axes", () => {
    const frame = buildBrowserFrameInput({
      deltaSeconds: 0.016,
      keyboardForward: 1,
      keyboardRight: -1,
      keyboardUp: 0,
      fast: false,
      mouseDeltaX: 0,
      mouseDeltaY: 0,
      touchLookDeltaX: 0,
      touchLookDeltaY: 0,
      touchLookStickX: 0,
      touchLookStickY: 0,
      touchMovementForward: 1,
      touchMovementRight: -1,
      touchMovementMagnitude: 1
    });

    deepEqual(frame.movement, {
      forward: 1,
      right: 1,
      up: 0,
      fast: true
    });
  });

  it("inverts lateral browser input to match current player strafe behavior", () => {
    const frame = buildBrowserFrameInput({
      deltaSeconds: 0.016,
      keyboardForward: 0,
      keyboardRight: 1,
      keyboardUp: 0,
      fast: false,
      mouseDeltaX: 0,
      mouseDeltaY: 0,
      touchLookDeltaX: 0,
      touchLookDeltaY: 0,
      touchLookStickX: 0,
      touchLookStickY: 0,
      touchMovementForward: 0,
      touchMovementRight: 0,
      touchMovementMagnitude: 0
    });

    equal(frame.movement.right, -1);
  });

  it("adds pointer-lock mouse look and touch-look deltas", () => {
    const frame = buildBrowserFrameInput({
      deltaSeconds: 0.016,
      keyboardForward: 0,
      keyboardRight: 0,
      keyboardUp: 1,
      fast: true,
      mouseDeltaX: 4,
      mouseDeltaY: -2,
      touchLookDeltaX: 6,
      touchLookDeltaY: 3,
      touchLookStickX: 0,
      touchLookStickY: 0,
      touchMovementForward: 0,
      touchMovementRight: 0,
      touchMovementMagnitude: 0
    });

    equal(frame.look.deltaX, 10);
    equal(frame.look.deltaY, 1);
    equal(frame.movement.up, 1);
    equal(frame.movement.fast, true);
  });

  it("converts normalized touch look stick axes into frame look deltas", () => {
    const frame = buildBrowserFrameInput({
      deltaSeconds: 0.5,
      keyboardForward: 0,
      keyboardRight: 0,
      keyboardUp: 0,
      fast: false,
      mouseDeltaX: 0,
      mouseDeltaY: 0,
      touchLookDeltaX: 0,
      touchLookDeltaY: 0,
      touchLookStickX: 1,
      touchLookStickY: -0.5,
      touchMovementForward: 0,
      touchMovementRight: 0,
      touchMovementMagnitude: 0
    });

    equal(frame.look.deltaX, 450);
    equal(frame.look.deltaY, -225);
  });

  it("treats a full touch movement stick like holding shift to run", () => {
    const frame = buildBrowserFrameInput({
      deltaSeconds: 0.016,
      keyboardForward: 0,
      keyboardRight: 0,
      keyboardUp: 0,
      fast: false,
      mouseDeltaX: 0,
      mouseDeltaY: 0,
      touchLookDeltaX: 0,
      touchLookDeltaY: 0,
      touchLookStickX: 0,
      touchLookStickY: 0,
      touchMovementForward: 1,
      touchMovementRight: 0,
      touchMovementMagnitude: 1
    });

    equal(frame.movement.fast, true);
  });

  it("keeps partial touch movement at normal walk speed", () => {
    const frame = buildBrowserFrameInput({
      deltaSeconds: 0.016,
      keyboardForward: 0,
      keyboardRight: 0,
      keyboardUp: 0,
      fast: false,
      mouseDeltaX: 0,
      mouseDeltaY: 0,
      touchLookDeltaX: 0,
      touchLookDeltaY: 0,
      touchLookStickX: 0,
      touchLookStickY: 0,
      touchMovementForward: 0.5,
      touchMovementRight: 0,
      touchMovementMagnitude: 0.5
    });

    equal(frame.movement.fast, false);
  });

  it("ignores non-finite touch movement magnitude for fast movement", () => {
    const frame = buildBrowserFrameInput({
      deltaSeconds: 0.016,
      keyboardForward: 0,
      keyboardRight: 0,
      keyboardUp: 0,
      fast: false,
      mouseDeltaX: 0,
      mouseDeltaY: 0,
      touchLookDeltaX: 0,
      touchLookDeltaY: 0,
      touchLookStickX: 0,
      touchLookStickY: 0,
      touchMovementForward: 0,
      touchMovementRight: 0,
      touchMovementMagnitude: Number.POSITIVE_INFINITY
    });

    equal(frame.movement.fast, false);
  });

  it("converts non-finite movement axes to zero before Rust receives them", () => {
    equal(clampFrameAxis(Number.NaN), 0);
    equal(clampFrameAxis(Number.POSITIVE_INFINITY), 0);
    equal(clampFrameAxis(Number.NEGATIVE_INFINITY), 0);
  });
});
