import { equal } from "node:assert/strict";
import { InputTracker } from "./inputTracker.js";

type Listener = (event: any) => void;

class FakeDocument {
  pointerLockElement: FakeElement | undefined;
  readonly listeners = new Map<string, Listener[]>();

  addEventListener(type: string, listener: Listener): void {
    this.listeners.set(type, [...(this.listeners.get(type) ?? []), listener]);
  }

  dispatch(type: string, event: any): void {
    for (const listener of this.listeners.get(type) ?? []) {
      listener(event);
    }
  }
}

class FakeElement {
  readonly listeners = new Map<string, Listener[]>();
  pointerLockRequests = 0;

  addEventListener(type: string, listener: Listener): void {
    this.listeners.set(type, [...(this.listeners.get(type) ?? []), listener]);
  }

  dispatch(type: string, event: any = {}): void {
    for (const listener of this.listeners.get(type) ?? []) {
      listener(event);
    }
  }

  requestPointerLock(): void {
    this.pointerLockRequests += 1;
  }
}

describe("InputTracker", () => {
  it("tracks key down and key up state", () => {
    const { input, document } = createHarness();

    document.dispatch("keydown", { code: "KeyW" });
    equal(input.isDown("KeyW"), true);

    document.dispatch("keyup", { code: "KeyW" });
    equal(input.isDown("KeyW"), false);
  });

  it("consumes key presses once", () => {
    const { input, document } = createHarness();

    document.dispatch("keydown", { code: "KeyW" });

    equal(input.consumePress("KeyW"), true);
    equal(input.consumePress("KeyW"), false);
  });

  it("does not repeat key presses while a key is held", () => {
    const { input, document } = createHarness();

    document.dispatch("keydown", { code: "KeyW" });
    document.dispatch("keydown", { code: "KeyW" });
    equal(input.consumePress("KeyW"), true);
    equal(input.consumePress("KeyW"), false);
  });

  it("registers a new key press after key up", () => {
    const { input, document } = createHarness();

    document.dispatch("keydown", { code: "KeyW" });
    equal(input.consumePress("KeyW"), true);
    document.dispatch("keyup", { code: "KeyW" });
    document.dispatch("keydown", { code: "KeyW" });

    equal(input.consumePress("KeyW"), true);
  });

  it("accumulates mouse movement only while pointer locked", () => {
    const { input, document, target } = createHarness();

    document.dispatch("mousemove", { movementX: 10, movementY: 20 });
    document.pointerLockElement = target;
    document.dispatch("mousemove", { movementX: 3, movementY: 4 });
    document.dispatch("mousemove", { movementX: -1, movementY: 2 });

    const snapshot = input.consumeFrameSnapshot();
    equal(snapshot.mouseDeltaX, 2);
    equal(snapshot.mouseDeltaY, 6);
  });

  it("consumeFrameSnapshot clears mouse deltas and key presses", () => {
    const { input, document, target } = createHarness();
    document.pointerLockElement = target;
    document.dispatch("keydown", { code: "KeyW" });
    document.dispatch("mousemove", { movementX: 3, movementY: 4 });

    input.consumeFrameSnapshot();
    const nextSnapshot = input.consumeFrameSnapshot();

    equal(input.consumePress("KeyW"), false);
    equal(nextSnapshot.mouseDeltaX, 0);
    equal(nextSnapshot.mouseDeltaY, 0);
  });

  it("clicking the target requests pointer lock", () => {
    const { target } = createHarness();

    target.dispatch("click");

    equal(target.pointerLockRequests, 1);
  });
});

function createHarness(): {
  readonly input: InputTracker;
  readonly document: FakeDocument;
  readonly target: FakeElement;
} {
  const input = new InputTracker();
  const document = new FakeDocument();
  const target = new FakeElement();
  input.attach(target as unknown as HTMLElement, document as unknown as Document);

  return { input, document, target };
}
