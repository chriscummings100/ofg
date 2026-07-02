// Browser control input collection for the C++ game runtime.
//
// This module owns DOM keyboard, mouse, and pointer-lock event listeners. It
// does not mutate scene state; callers consume raw control snapshots and pass
// them into the C++ runtime once per animation frame.

import type { ControlInput } from "./wasmRuntime.js";

export interface ControlInputCollector {
  // Returns the latest raw input snapshot and clears accumulated mouse deltas.
  consumeSnapshot(): ControlInput;
  // Removes all DOM listeners owned by this collector.
  dispose(): void;
}

interface ControlInputOptions {
  readonly document?: Document;
  readonly window?: Window;
}

const HANDLED_CODES = new Set([
  "KeyW",
  "KeyA",
  "KeyS",
  "KeyD",
  "Space",
  "KeyC",
  "ShiftLeft",
  "ShiftRight",
  "ControlLeft",
  "ControlRight",
  "Backquote"
]);

// Creates a DOM-backed control input collector for one canvas.
export function createControlInputCollector(
  canvas: HTMLCanvasElement,
  options: ControlInputOptions = {}
): ControlInputCollector {
  const documentRef = options.document ?? canvas.ownerDocument;
  const windowRef = options.window ?? documentRef.defaultView ?? window;
  return new BrowserControlInputCollector(canvas, documentRef, windowRef);
}

class BrowserControlInputCollector implements ControlInputCollector {
  readonly #canvas: HTMLCanvasElement;
  readonly #document: Document;
  readonly #window: Window;
  readonly #pressedCodes = new Set<string>();
  #lookDeltaX = 0;
  #lookDeltaY = 0;
  #cycleCameraMode = false;
  #disposed = false;

  // Registers DOM listeners for one canvas/runtime pair.
  constructor(canvas: HTMLCanvasElement, documentRef: Document, windowRef: Window) {
    this.#canvas = canvas;
    this.#document = documentRef;
    this.#window = windowRef;
    this.#canvas.addEventListener("click", this.#handleCanvasClick);
    this.#document.addEventListener("pointerlockchange", this.#handlePointerLockChange);
    this.#document.addEventListener("mousemove", this.#handleMouseMove);
    this.#window.addEventListener("keydown", this.#handleKeyDown);
    this.#window.addEventListener("keyup", this.#handleKeyUp);
    this.#window.addEventListener("blur", this.#handleBlur);
  }

  // Returns the latest raw input snapshot and clears accumulated mouse deltas.
  consumeSnapshot(): ControlInput {
    this.#assertLive();
    const snapshot: ControlInput = {
      moveX: axis(this.#pressedCodes.has("KeyD"), this.#pressedCodes.has("KeyA")),
      moveY: axis(this.#pressedCodes.has("Space"), this.#pressedCodes.has("KeyC")),
      moveZ: axis(this.#pressedCodes.has("KeyW"), this.#pressedCodes.has("KeyS")),
      lookDeltaX: this.#lookDeltaX,
      lookDeltaY: this.#lookDeltaY,
      lookActive: this.#document.pointerLockElement === this.#canvas,
      fast: this.#pressedCodes.has("ShiftLeft") || this.#pressedCodes.has("ShiftRight"),
      slow:
        this.#pressedCodes.has("ControlLeft") ||
        this.#pressedCodes.has("ControlRight"),
      cycleCameraMode: this.#cycleCameraMode
    };
    this.#lookDeltaX = 0;
    this.#lookDeltaY = 0;
    this.#cycleCameraMode = false;
    return snapshot;
  }

  // Removes all DOM listeners owned by this collector.
  dispose(): void {
    if (this.#disposed) {
      return;
    }
    this.#canvas.removeEventListener("click", this.#handleCanvasClick);
    this.#document.removeEventListener("pointerlockchange", this.#handlePointerLockChange);
    this.#document.removeEventListener("mousemove", this.#handleMouseMove);
    this.#window.removeEventListener("keydown", this.#handleKeyDown);
    this.#window.removeEventListener("keyup", this.#handleKeyUp);
    this.#window.removeEventListener("blur", this.#handleBlur);
    this.#pressedCodes.clear();
    this.#lookDeltaX = 0;
    this.#lookDeltaY = 0;
    this.#cycleCameraMode = false;
    this.#disposed = true;
  }

  readonly #handleCanvasClick = (): void => {
    const requestPointerLock = this.#canvas.requestPointerLock;
    if (typeof requestPointerLock === "function") {
      requestPointerLock.call(this.#canvas);
    }
  };

  readonly #handlePointerLockChange = (): void => {
    if (this.#document.pointerLockElement !== this.#canvas) {
      this.#lookDeltaX = 0;
      this.#lookDeltaY = 0;
    }
  };

  readonly #handleMouseMove = (event: MouseEvent): void => {
    if (this.#document.pointerLockElement !== this.#canvas) {
      return;
    }
    this.#lookDeltaX += event.movementX;
    this.#lookDeltaY += event.movementY;
  };

  readonly #handleKeyDown = (event: KeyboardEvent): void => {
    if (!HANDLED_CODES.has(event.code)) {
      return;
    }
    if (event.code === "Backquote" && !this.#pressedCodes.has("Backquote")) {
      this.#cycleCameraMode = true;
    }
    this.#pressedCodes.add(event.code);
    event.preventDefault();
  };

  readonly #handleKeyUp = (event: KeyboardEvent): void => {
    if (!HANDLED_CODES.has(event.code)) {
      return;
    }
    this.#pressedCodes.delete(event.code);
    event.preventDefault();
  };

  readonly #handleBlur = (): void => {
    this.#pressedCodes.clear();
    this.#lookDeltaX = 0;
    this.#lookDeltaY = 0;
    this.#cycleCameraMode = false;
  };

  // Throws the stable disposed-collector error used by tests.
  #assertLive(): void {
    if (this.#disposed) {
      throw new Error("Control input collector has been disposed.");
    }
  }
}

// Converts positive/negative key state into a signed movement axis.
function axis(positive: boolean, negative: boolean): number {
  return (positive ? 1 : 0) - (negative ? 1 : 0);
}
