// Converts browser input sources into the stable Rust browser-frame packet.
// This keeps touch-specific details out of `BrowserFrameInput`.

import type { BrowserFrameInput } from "../engine/web/browserGameTypes.js";

export type BrowserFrameInputSources = {
  readonly deltaSeconds: number;
  readonly keyboardForward: number;
  readonly keyboardRight: number;
  readonly keyboardUp: number;
  readonly fast: boolean;
  readonly mouseDeltaX: number;
  readonly mouseDeltaY: number;
  readonly touchLookDeltaX: number;
  readonly touchLookDeltaY: number;
  readonly touchLookStickX: number;
  readonly touchLookStickY: number;
  readonly touchMovementForward: number;
  readonly touchMovementRight: number;
  readonly touchMovementMagnitude: number;
};

const TOUCH_LOOK_STICK_PIXELS_PER_SECOND = 900;
const TOUCH_MOVEMENT_RUN_THRESHOLD = 0.8;

/// Builds one Rust-facing frame input from browser keyboard, mouse, and touch sources.
export function buildBrowserFrameInput(sources: BrowserFrameInputSources): BrowserFrameInput {
  const lookStickScale = TOUCH_LOOK_STICK_PIXELS_PER_SECOND * sources.deltaSeconds;

  return {
    deltaSeconds: sources.deltaSeconds,
    movement: {
      forward: clampFrameAxis(sources.keyboardForward + sources.touchMovementForward),
      right: -clampFrameAxis(sources.keyboardRight + sources.touchMovementRight),
      up: clampFrameAxis(sources.keyboardUp),
      fast: sources.fast || isTouchMovementFast(sources.touchMovementMagnitude)
    },
    look: {
      deltaX: sources.mouseDeltaX +
        sources.touchLookDeltaX +
        sources.touchLookStickX * lookStickScale,
      deltaY: sources.mouseDeltaY +
        sources.touchLookDeltaY +
        sources.touchLookStickY * lookStickScale
    }
  };
}

/// Clamps movement axes to the range accepted by the Rust player controller.
export function clampFrameAxis(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }

  return Math.max(-1, Math.min(1, value));
}

/// Returns whether browser-local touch movement should enter the Rust fast lane.
function isTouchMovementFast(magnitude: number): boolean {
  return Number.isFinite(magnitude) && magnitude >= TOUCH_MOVEMENT_RUN_THRESHOLD;
}
