// Tests for browser control-input collection.
//
// These tests keep DOM input ownership in TypeScript limited to raw keyboard,
// mouse, pointer-lock, and one-frame action snapshots for the C++ runtime.
import assert from "node:assert/strict";
import { Window as HappyWindow } from "happy-dom";
import { createControlInputCollector } from "../../src/app/controlInput.js";

describe("control input collector", () => {
  it("maps key state to movement axes and modifiers", () => {
    const { window, document, canvas } = createHarness();
    const collector = createControlInputCollector(canvas, { document, window });

    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyW"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyD"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "Space"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "ShiftLeft"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "ControlRight"));

    assert.deepEqual(collector.consumeSnapshot(), {
      moveX: 1,
      moveY: 1,
      moveZ: 1,
      lookDeltaX: 0,
      lookDeltaY: 0,
      lookActive: false,
      fast: true,
      slow: true,
      cycleCameraMode: false,
      toggleOverheadSun: false
    });

    window.dispatchEvent(keyboardEvent(window, "keyup", "KeyD"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyA"));
    window.dispatchEvent(keyboardEvent(window, "keyup", "Space"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyC"));
    window.dispatchEvent(keyboardEvent(window, "keyup", "ShiftLeft"));
    window.dispatchEvent(keyboardEvent(window, "keyup", "ControlRight"));

    const snapshot = collector.consumeSnapshot();
    assert.equal(snapshot.moveX, -1);
    assert.equal(snapshot.moveY, -1);
    assert.equal(snapshot.moveZ, 1);
    assert.equal(snapshot.fast, false);
    assert.equal(snapshot.slow, false);
    assert.equal(snapshot.cycleCameraMode, false);
    assert.equal(snapshot.toggleOverheadSun, false);
  });

  it("reports debug keys as one-frame action edges", () => {
    const { window, canvas } = createHarness();
    const collector = createControlInputCollector(canvas, { document: canvas.ownerDocument, window });

    window.dispatchEvent(keyboardEvent(window, "keydown", "Backquote"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyO"));
    const snapshot = collector.consumeSnapshot();
    assert.equal(snapshot.cycleCameraMode, true);
    assert.equal(snapshot.toggleOverheadSun, true);

    const cleared = collector.consumeSnapshot();
    assert.equal(cleared.cycleCameraMode, false);
    assert.equal(cleared.toggleOverheadSun, false);

    window.dispatchEvent(keyboardEvent(window, "keydown", "Backquote"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyO"));
    const repeated = collector.consumeSnapshot();
    assert.equal(repeated.cycleCameraMode, false);
    assert.equal(repeated.toggleOverheadSun, false);

    window.dispatchEvent(keyboardEvent(window, "keyup", "Backquote"));
    window.dispatchEvent(keyboardEvent(window, "keyup", "KeyO"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "Backquote"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyO"));
    const repressed = collector.consumeSnapshot();
    assert.equal(repressed.cycleCameraMode, true);
    assert.equal(repressed.toggleOverheadSun, true);
  });

  it("requests pointer lock and accumulates mouse movement until consumed", () => {
    const { window, document, canvas, setPointerLockElement } = createHarness();
    let pointerLockRequests = 0;
    Object.defineProperty(canvas, "requestPointerLock", {
      configurable: true,
      value() {
        pointerLockRequests += 1;
        setPointerLockElement(canvas);
        document.dispatchEvent(new window.Event("pointerlockchange") as unknown as Event);
      }
    });
    const collector = createControlInputCollector(canvas, { document, window });

    document.dispatchEvent(mouseEvent(window, "mousemove", 4, 6));
    assert.equal(collector.consumeSnapshot().lookDeltaX, 0);

    canvas.dispatchEvent(new window.MouseEvent("click") as unknown as Event);
    assert.equal(pointerLockRequests, 1);
    document.dispatchEvent(mouseEvent(window, "mousemove", 5, -2));
    document.dispatchEvent(mouseEvent(window, "mousemove", 7, 3));

    const snapshot = collector.consumeSnapshot();
    assert.equal(snapshot.lookActive, true);
    assert.equal(snapshot.lookDeltaX, 12);
    assert.equal(snapshot.lookDeltaY, 1);
    assert.equal(collector.consumeSnapshot().lookDeltaX, 0);

    setPointerLockElement(null);
    document.dispatchEvent(new window.Event("pointerlockchange") as unknown as Event);
    assert.equal(collector.consumeSnapshot().lookActive, false);
  });

  it("reports raw debug UI input and clears one-frame debug edges", () => {
    const { window, document, canvas } = createHarness();
    setCanvasRect(canvas, 10, 20, 100, 80);
    const collector = createControlInputCollector(canvas, { document, window });

    document.dispatchEvent(mouseEvent(window, "mousemove", 0, 0, 40, 50));
    document.dispatchEvent(mouseButtonEvent(window, "mousedown", 1, 40, 50));
    canvas.dispatchEvent(wheelEvent(window, -50, 100, 40, 50));
    window.dispatchEvent(keyboardEvent(window, "keydown", "F1", "F1"));
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyA", "a"));
    window.dispatchEvent(keyboardEvent(window, "keyup", "KeyA", "a"));

    const snapshot = collector.consumeDebugSnapshot();
    assert.equal(snapshot.hasFocus, true);
    assert.equal(snapshot.pointerLocked, false);
    assert.equal(snapshot.mousePositionValid, true);
    assert.equal(snapshot.mouseX, 30);
    assert.equal(snapshot.mouseY, 30);
    assert.equal(snapshot.mouseButtons, 1);
    assert.equal(snapshot.wheelX, 0.5);
    assert.equal(snapshot.wheelY, -1);
    assert.equal(snapshot.toggleVisibility, true);
    assert.deepEqual(snapshot.keyDownCodes, ["F1"]);
    assert.deepEqual(snapshot.keyPressedCodes, ["F1", "KeyA"]);
    assert.deepEqual(snapshot.keyReleasedCodes, ["KeyA"]);
    assert.equal(snapshot.textInput, "a");

    const cleared = collector.consumeDebugSnapshot();
    assert.equal(cleared.wheelX, 0);
    assert.equal(cleared.wheelY, 0);
    assert.equal(cleared.toggleVisibility, false);
    assert.deepEqual(cleared.keyPressedCodes, []);
    assert.deepEqual(cleared.keyReleasedCodes, []);
    assert.equal(cleared.textInput, "");
    assert.deepEqual(cleared.keyDownCodes, ["F1"]);
  });

  it("blocks and exits pointer lock while debug UI captures pointer input", () => {
    const { window, document, canvas, setPointerLockElement } = createHarness();
    let pointerLockRequests = 0;
    let pointerLockExits = 0;
    Object.defineProperty(canvas, "requestPointerLock", {
      configurable: true,
      value() {
        pointerLockRequests += 1;
        setPointerLockElement(canvas);
      }
    });
    Object.defineProperty(document, "exitPointerLock", {
      configurable: true,
      value() {
        pointerLockExits += 1;
        setPointerLockElement(null);
      }
    });
    const collector = createControlInputCollector(canvas, { document, window });

    canvas.dispatchEvent(new window.MouseEvent("click") as unknown as Event);
    assert.equal(pointerLockRequests, 1);
    assert.equal(document.pointerLockElement, canvas);

    collector.setDebugUiPointerLockBlocked(true);
    assert.equal(pointerLockExits, 1);
    assert.equal(document.pointerLockElement, null);
    canvas.dispatchEvent(new window.MouseEvent("click") as unknown as Event);
    assert.equal(pointerLockRequests, 1);

    collector.setDebugUiPointerLockBlocked(false);
    canvas.dispatchEvent(new window.MouseEvent("click") as unknown as Event);
    assert.equal(pointerLockRequests, 2);
  });

  it("clears key and mouse state on blur", () => {
    const { window, document, canvas, setPointerLockElement } = createHarness();
    setPointerLockElement(canvas);
    const collector = createControlInputCollector(canvas, { document, window });

    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyW"));
    document.dispatchEvent(mouseEvent(window, "mousemove", 5, 5));
    window.dispatchEvent(new window.Event("blur"));

    const snapshot = collector.consumeSnapshot();
    assert.equal(snapshot.moveZ, 0);
    assert.equal(snapshot.lookDeltaX, 0);
    assert.equal(snapshot.lookDeltaY, 0);
    assert.deepEqual(collector.consumeDebugSnapshot().keyDownCodes, []);
    assert.equal(snapshot.cycleCameraMode, false);
    assert.equal(snapshot.toggleOverheadSun, false);
  });

  it("removes listeners on dispose", () => {
    const { window, document, canvas } = createHarness();
    let pointerLockRequests = 0;
    Object.defineProperty(canvas, "requestPointerLock", {
      configurable: true,
      value() {
        pointerLockRequests += 1;
      }
    });
    const collector = createControlInputCollector(canvas, { document, window });

    collector.dispose();
    canvas.dispatchEvent(new window.MouseEvent("click") as unknown as Event);
    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyW"));

    assert.equal(pointerLockRequests, 0);
    assert.throws(() => collector.consumeSnapshot(), /collector has been disposed/);
  });
});

function createHarness(): {
  readonly window: HappyWindow & globalThis.Window;
  readonly document: Document;
  readonly canvas: HTMLCanvasElement;
  readonly setPointerLockElement: (element: Element | null) => void;
} {
  const window = new HappyWindow({
    url: "http://127.0.0.1:5173/"
  }) as HappyWindow & globalThis.Window;
  const document = window.document as unknown as Document;
  const canvas = document.createElement("canvas");
  document.body.appendChild(canvas);
  let pointerLockElement: Element | null = null;
  Object.defineProperty(document, "pointerLockElement", {
    configurable: true,
    get() {
      return pointerLockElement;
    }
  });
  return {
    window,
    document,
    canvas,
    setPointerLockElement(element: Element | null) {
      pointerLockElement = element;
    }
  };
}

function keyboardEvent(
  window: HappyWindow,
  type: "keydown" | "keyup",
  code: string,
  key = ""
): KeyboardEvent {
  return new window.KeyboardEvent(type, { code, key, bubbles: true }) as unknown as KeyboardEvent;
}

function mouseEvent(
  window: HappyWindow,
  type: "mousemove",
  movementX: number,
  movementY: number,
  clientX = 0,
  clientY = 0
): Event {
  const event = new window.MouseEvent(type, { bubbles: true, clientX, clientY }) as unknown as MouseEvent;
  Object.defineProperty(event, "movementX", { configurable: true, value: movementX });
  Object.defineProperty(event, "movementY", { configurable: true, value: movementY });
  return event as Event;
}

function mouseButtonEvent(
  window: HappyWindow,
  type: "mousedown" | "mouseup",
  buttons: number,
  clientX: number,
  clientY: number
): Event {
  const event = new window.MouseEvent(type, { bubbles: true, clientX, clientY }) as unknown as MouseEvent;
  Object.defineProperty(event, "buttons", { configurable: true, value: buttons });
  return event as Event;
}

function wheelEvent(
  window: HappyWindow,
  deltaX: number,
  deltaY: number,
  clientX: number,
  clientY: number
): Event {
  const event = new window.WheelEvent("wheel", {
    bubbles: true,
    cancelable: true,
    deltaX,
    deltaY,
    deltaMode: 0
  }) as unknown as WheelEvent;
  Object.defineProperty(event, "clientX", { configurable: true, value: clientX });
  Object.defineProperty(event, "clientY", { configurable: true, value: clientY });
  return event as Event;
}

function setCanvasRect(
  canvas: HTMLCanvasElement,
  left: number,
  top: number,
  width: number,
  height: number
): void {
  Object.defineProperty(canvas, "getBoundingClientRect", {
    configurable: true,
    value() {
      return {
        x: left,
        y: top,
        left,
        top,
        right: left + width,
        bottom: top + height,
        width,
        height,
        toJSON() {
          return {};
        }
      };
    }
  });
}
