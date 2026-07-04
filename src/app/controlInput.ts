// Browser control input collection for the C++ game runtime.
//
// This module owns DOM keyboard, mouse, and pointer-lock event listeners. It
// does not mutate scene state; callers consume raw control snapshots and pass
// them into the C++ runtime once per animation frame.

import type { ControlInput, DebugUiInput } from "./wasmRuntime.js";

export interface ControlInputCollector {
  // Returns the latest raw input snapshot and clears accumulated mouse deltas.
  consumeSnapshot(): ControlInput;
  // Returns the latest raw debug UI input snapshot and clears per-frame debug deltas/edges.
  consumeDebugSnapshot(): DebugUiInput;
  // Blocks gameplay pointer lock while the debug UI is visible or capturing mouse input.
  setDebugUiPointerLockBlocked(blocked: boolean): void;
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
  "Backquote",
  "F1",
  "KeyO"
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
  readonly #debugPressedCodes = new Set<string>();
  readonly #debugPressedEdges = new Set<string>();
  readonly #debugReleasedEdges = new Set<string>();
  #lookDeltaX = 0;
  #lookDeltaY = 0;
  #mousePositionValid = false;
  #mouseX = 0;
  #mouseY = 0;
  #mouseButtons = 0;
  #wheelX = 0;
  #wheelY = 0;
  #debugTextInput = "";
  #toggleDebugUiVisibility = false;
  #cycleCameraMode = false;
  #toggleOverheadSun = false;
  #debugUiPointerLockBlocked = false;
  #disposed = false;

  // Registers DOM listeners for one canvas/runtime pair.
  constructor(canvas: HTMLCanvasElement, documentRef: Document, windowRef: Window) {
    this.#canvas = canvas;
    this.#document = documentRef;
    this.#window = windowRef;
    this.#canvas.addEventListener("click", this.#handleCanvasClick);
    this.#canvas.addEventListener("wheel", this.#handleWheel);
    this.#document.addEventListener("pointerlockchange", this.#handlePointerLockChange);
    this.#document.addEventListener("mousemove", this.#handleMouseMove);
    this.#document.addEventListener("mousedown", this.#handleMouseButtons);
    this.#document.addEventListener("mouseup", this.#handleMouseButtons);
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
      cycleCameraMode: this.#cycleCameraMode,
      toggleOverheadSun: this.#toggleOverheadSun
    };
    this.#lookDeltaX = 0;
    this.#lookDeltaY = 0;
    this.#cycleCameraMode = false;
    this.#toggleOverheadSun = false;
    return snapshot;
  }

  // Returns the latest raw debug UI input snapshot and clears per-frame debug deltas/edges.
  consumeDebugSnapshot(): DebugUiInput {
    this.#assertLive();
    const snapshot: DebugUiInput = {
      hasFocus: typeof this.#document.hasFocus === "function" ? this.#document.hasFocus() : true,
      pointerLocked: this.#document.pointerLockElement === this.#canvas,
      mousePositionValid: this.#mousePositionValid,
      mouseX: this.#mouseX,
      mouseY: this.#mouseY,
      mouseButtons: this.#mouseButtons,
      wheelX: this.#wheelX,
      wheelY: this.#wheelY,
      toggleVisibility: this.#toggleDebugUiVisibility,
      keyDownCodes: [...this.#debugPressedCodes],
      keyPressedCodes: [...this.#debugPressedEdges],
      keyReleasedCodes: [...this.#debugReleasedEdges],
      textInput: this.#debugTextInput
    };
    this.#wheelX = 0;
    this.#wheelY = 0;
    this.#toggleDebugUiVisibility = false;
    this.#debugPressedEdges.clear();
    this.#debugReleasedEdges.clear();
    this.#debugTextInput = "";
    return snapshot;
  }

  // Blocks gameplay pointer lock while the debug UI is visible or capturing mouse input.
  setDebugUiPointerLockBlocked(blocked: boolean): void {
    this.#assertLive();
    this.#debugUiPointerLockBlocked = blocked;
    if (blocked && this.#document.pointerLockElement === this.#canvas) {
      const exitPointerLock = this.#document.exitPointerLock;
      if (typeof exitPointerLock === "function") {
        exitPointerLock.call(this.#document);
      }
    }
  }

  // Removes all DOM listeners owned by this collector.
  dispose(): void {
    if (this.#disposed) {
      return;
    }
    this.#canvas.removeEventListener("click", this.#handleCanvasClick);
    this.#canvas.removeEventListener("wheel", this.#handleWheel);
    this.#document.removeEventListener("pointerlockchange", this.#handlePointerLockChange);
    this.#document.removeEventListener("mousemove", this.#handleMouseMove);
    this.#document.removeEventListener("mousedown", this.#handleMouseButtons);
    this.#document.removeEventListener("mouseup", this.#handleMouseButtons);
    this.#window.removeEventListener("keydown", this.#handleKeyDown);
    this.#window.removeEventListener("keyup", this.#handleKeyUp);
    this.#window.removeEventListener("blur", this.#handleBlur);
    this.#pressedCodes.clear();
    this.#debugPressedCodes.clear();
    this.#debugPressedEdges.clear();
    this.#debugReleasedEdges.clear();
    this.#lookDeltaX = 0;
    this.#lookDeltaY = 0;
    this.#mousePositionValid = false;
    this.#mouseButtons = 0;
    this.#wheelX = 0;
    this.#wheelY = 0;
    this.#debugTextInput = "";
    this.#toggleDebugUiVisibility = false;
    this.#cycleCameraMode = false;
    this.#toggleOverheadSun = false;
    this.#disposed = true;
  }

  readonly #handleCanvasClick = (): void => {
    if (this.#debugUiPointerLockBlocked) {
      return;
    }
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
    this.#updateMousePosition(event);
    if (this.#document.pointerLockElement !== this.#canvas) {
      return;
    }
    this.#lookDeltaX += event.movementX;
    this.#lookDeltaY += event.movementY;
  };

  readonly #handleMouseButtons = (event: MouseEvent): void => {
    this.#updateMousePosition(event);
    this.#mouseButtons = event.buttons;
  };

  readonly #handleWheel = (event: WheelEvent): void => {
    this.#updateMousePosition(event);
    const scale = wheelDeltaScale(event.deltaMode);
    this.#wheelX += -event.deltaX * scale;
    this.#wheelY += -event.deltaY * scale;
    event.preventDefault();
  };

  readonly #handleKeyDown = (event: KeyboardEvent): void => {
    if (!this.#debugPressedCodes.has(event.code)) {
      this.#debugPressedEdges.add(event.code);
      if (event.code === "F1") {
        this.#toggleDebugUiVisibility = true;
      }
    }
    this.#debugPressedCodes.add(event.code);
    if (event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
      this.#debugTextInput += event.key;
    }
    if (!HANDLED_CODES.has(event.code)) {
      return;
    }
    if (event.code === "Backquote" && !this.#pressedCodes.has("Backquote")) {
      this.#cycleCameraMode = true;
    }
    if (event.code === "KeyO" && !this.#pressedCodes.has("KeyO")) {
      this.#toggleOverheadSun = true;
    }
    this.#pressedCodes.add(event.code);
    event.preventDefault();
  };

  readonly #handleKeyUp = (event: KeyboardEvent): void => {
    this.#debugPressedCodes.delete(event.code);
    this.#debugReleasedEdges.add(event.code);
    if (!HANDLED_CODES.has(event.code)) {
      return;
    }
    this.#pressedCodes.delete(event.code);
    event.preventDefault();
  };

  readonly #handleBlur = (): void => {
    this.#pressedCodes.clear();
    this.#debugPressedCodes.clear();
    this.#debugPressedEdges.clear();
    this.#debugReleasedEdges.clear();
    this.#lookDeltaX = 0;
    this.#lookDeltaY = 0;
    this.#mouseButtons = 0;
    this.#wheelX = 0;
    this.#wheelY = 0;
    this.#debugTextInput = "";
    this.#toggleDebugUiVisibility = false;
    this.#cycleCameraMode = false;
    this.#toggleOverheadSun = false;
  };

  // Throws the stable disposed-collector error used by tests.
  #assertLive(): void {
    if (this.#disposed) {
      throw new Error("Control input collector has been disposed.");
    }
  }

  // Stores a canvas-relative CSS-pixel mouse position when the event is inside the canvas.
  #updateMousePosition(event: MouseEvent): void {
    if (this.#document.pointerLockElement === this.#canvas) {
      this.#mousePositionValid = false;
      return;
    }
    const rect = this.#canvas.getBoundingClientRect();
    const width = rect.width || this.#canvas.clientWidth || this.#canvas.width;
    const height = rect.height || this.#canvas.clientHeight || this.#canvas.height;
    this.#mouseX = event.clientX - rect.left;
    this.#mouseY = event.clientY - rect.top;
    this.#mousePositionValid =
      this.#mouseX >= 0 &&
      this.#mouseY >= 0 &&
      (width === 0 || this.#mouseX <= width) &&
      (height === 0 || this.#mouseY <= height);
  }
}

// Converts positive/negative key state into a signed movement axis.
function axis(positive: boolean, negative: boolean): number {
  return (positive ? 1 : 0) - (negative ? 1 : 0);
}

// Converts browser wheel deltas into ImGui-style line-ish units.
function wheelDeltaScale(deltaMode: number): number {
  if (deltaMode === 1) {
    return 1;
  }
  if (deltaMode === 2) {
    return 24;
  }
  return 1 / 100;
}
