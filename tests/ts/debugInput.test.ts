// Tests for browser debug-input collection.
//
// These tests keep DOM input ownership in TypeScript limited to raw keyboard,
// mouse, and pointer-lock snapshots for the C++ camera controller.
import assert from "node:assert/strict";
import { Window as HappyWindow } from "happy-dom";
import { createDebugInputCollector } from "../../src/app/debugInput.js";

describe("debug input collector", () => {
  it("maps key state to movement axes and modifiers", () => {
    const { window, document, canvas } = createHarness();
    const collector = createDebugInputCollector(canvas, { document, window });

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
      slow: true
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
    const collector = createDebugInputCollector(canvas, { document, window });

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

  it("clears key and mouse state on blur", () => {
    const { window, document, canvas, setPointerLockElement } = createHarness();
    setPointerLockElement(canvas);
    const collector = createDebugInputCollector(canvas, { document, window });

    window.dispatchEvent(keyboardEvent(window, "keydown", "KeyW"));
    document.dispatchEvent(mouseEvent(window, "mousemove", 5, 5));
    window.dispatchEvent(new window.Event("blur"));

    const snapshot = collector.consumeSnapshot();
    assert.equal(snapshot.moveZ, 0);
    assert.equal(snapshot.lookDeltaX, 0);
    assert.equal(snapshot.lookDeltaY, 0);
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
    const collector = createDebugInputCollector(canvas, { document, window });

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
  code: string
): KeyboardEvent {
  return new window.KeyboardEvent(type, { code, bubbles: true }) as unknown as KeyboardEvent;
}

function mouseEvent(
  window: HappyWindow,
  type: "mousemove",
  movementX: number,
  movementY: number
): Event {
  const event = new window.MouseEvent(type, { bubbles: true }) as unknown as MouseEvent;
  Object.defineProperty(event, "movementX", { configurable: true, value: movementX });
  Object.defineProperty(event, "movementY", { configurable: true, value: movementY });
  return event as Event;
}
