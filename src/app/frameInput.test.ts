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
      touchMovementForward: 1,
      touchMovementRight: -1
    });

    deepEqual(frame.movement, {
      forward: 1,
      right: -1,
      up: 0,
      fast: false
    });
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
      touchMovementForward: 0,
      touchMovementRight: 0
    });

    equal(frame.look.deltaX, 10);
    equal(frame.look.deltaY, 1);
    equal(frame.movement.up, 1);
    equal(frame.movement.fast, true);
  });

  it("converts non-finite movement axes to zero before Rust receives them", () => {
    equal(clampFrameAxis(Number.NaN), 0);
    equal(clampFrameAxis(Number.POSITIVE_INFINITY), 0);
    equal(clampFrameAxis(Number.NEGATIVE_INFINITY), 0);
  });
});
