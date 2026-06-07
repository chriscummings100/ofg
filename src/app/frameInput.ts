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
  readonly touchMovementForward: number;
  readonly touchMovementRight: number;
};

/// Builds one Rust-facing frame input from browser keyboard, mouse, and touch sources.
export function buildBrowserFrameInput(sources: BrowserFrameInputSources): BrowserFrameInput {
  return {
    deltaSeconds: sources.deltaSeconds,
    movement: {
      forward: clampFrameAxis(sources.keyboardForward + sources.touchMovementForward),
      right: clampFrameAxis(sources.keyboardRight + sources.touchMovementRight),
      up: clampFrameAxis(sources.keyboardUp),
      fast: sources.fast
    },
    look: {
      deltaX: sources.mouseDeltaX + sources.touchLookDeltaX,
      deltaY: sources.mouseDeltaY + sources.touchLookDeltaY
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
