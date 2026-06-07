import { deepEqual, equal } from "node:assert/strict";
import { InputTracker } from "./inputTracker.js";

type Listener = (event: any) => void;
type FakeRect = {
  readonly left: number;
  readonly top: number;
  readonly width: number;
  readonly height: number;
};

class FakeStyle {
  left = "";
  top = "";
  transform = "";

  removeProperty(property: string): void {
    if (property === "left") {
      this.left = "";
    }
    if (property === "top") {
      this.top = "";
    }
    if (property === "transform") {
      this.transform = "";
    }
  }
}

class FakeDocument {
  pointerLockElement: FakeElement | undefined;
  readonly listeners = new Map<string, Listener[]>();
  readonly documentElement = {
    classList: {
      added: [] as string[],
      add(className: string): void {
        this.added.push(className);
      }
    }
  };

  addEventListener(type: string, listener: Listener): void {
    this.listeners.set(type, [...(this.listeners.get(type) ?? []), listener]);
  }

  dispatch(type: string, event: any): void {
    event.currentTarget ??= this;
    for (const listener of this.listeners.get(type) ?? []) {
      listener(event);
    }
  }
}

class FakeElement {
  readonly listeners = new Map<string, Listener[]>();
  readonly capturedPointers: number[] = [];
  readonly dataset: Record<string, string> = {};
  readonly style = new FakeStyle();
  pointerLockRequests = 0;
  rect: FakeRect = {
    left: 0,
    top: 0,
    width: 168,
    height: 168
  };

  addEventListener(type: string, listener: Listener): void {
    this.listeners.set(type, [...(this.listeners.get(type) ?? []), listener]);
  }

  dispatch(type: string, event: any = {}): void {
    event.currentTarget ??= this;
    for (const listener of this.listeners.get(type) ?? []) {
      listener(event);
    }
  }

  requestPointerLock(): void {
    this.pointerLockRequests += 1;
  }

  setPointerCapture(pointerId: number): void {
    this.capturedPointers.push(pointerId);
  }

  getBoundingClientRect(): DOMRect {
    const right = this.rect.left + this.rect.width;
    const bottom = this.rect.top + this.rect.height;
    return {
      x: this.rect.left,
      y: this.rect.top,
      left: this.rect.left,
      top: this.rect.top,
      right,
      bottom,
      width: this.rect.width,
      height: this.rect.height,
      toJSON: () => ({})
    } as DOMRect;
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
    equal(nextSnapshot.touchLookDeltaX, 0);
    equal(nextSnapshot.touchLookDeltaY, 0);
    equal(nextSnapshot.touchLookStickX, 0);
    equal(nextSnapshot.touchLookStickY, 0);
  });

  it("clicking the target requests pointer lock", () => {
    const { target } = createHarness();

    target.dispatch("click");

    equal(target.pointerLockRequests, 1);
  });

  it("marks the document after touch input so controls can become visible", () => {
    const { document } = createHarness();

    document.dispatch("pointerdown", pointerEvent({ pointerType: "touch" }));

    deepEqual(document.documentElement.classList.added, ["touch-input"]);
  });

  it("normalizes upward joystick drag into forward touch movement", () => {
    const { input, touchControls } = createHarness({ withTouchControls: true });

    touchControls.moveZone.dispatch("pointerdown", pointerEvent({
      pointerId: 7,
      clientX: 100,
      clientY: 120
    }));
    touchControls.moveZone.dispatch("pointermove", pointerEvent({
      pointerId: 7,
      clientX: 100,
      clientY: 66
    }));

    const snapshot = input.consumeFrameSnapshot();

    equal(snapshot.touchMovementForward, 1);
    equal(snapshot.touchMovementRight, 0);
    deepEqual(touchControls.moveZone.capturedPointers, [7]);
    equal(touchControls.root.dataset.touchMove, "active");
  });

  it("normalizes sideways joystick drag into right touch movement", () => {
    const { input, touchControls } = createHarness({ withTouchControls: true });

    touchControls.moveZone.dispatch("pointerdown", pointerEvent({
      pointerId: 8,
      clientX: 100,
      clientY: 120
    }));
    touchControls.moveZone.dispatch("pointermove", pointerEvent({
      pointerId: 8,
      clientX: 154,
      clientY: 120
    }));

    const snapshot = input.consumeFrameSnapshot();

    equal(snapshot.touchMovementForward, 0);
    equal(snapshot.touchMovementRight, 1);
  });

  it("clears joystick movement on pointer release", () => {
    const { input, touchControls } = createHarness({ withTouchControls: true });

    touchControls.moveZone.dispatch("pointerdown", pointerEvent({
      pointerId: 9,
      clientX: 100,
      clientY: 120
    }));
    touchControls.moveZone.dispatch("pointermove", pointerEvent({
      pointerId: 9,
      clientX: 100,
      clientY: 66
    }));
    touchControls.moveZone.dispatch("pointerup", pointerEvent({ pointerId: 9 }));

    const snapshot = input.consumeFrameSnapshot();

    equal(snapshot.touchMovementForward, 0);
    equal(snapshot.touchMovementRight, 0);
    equal(touchControls.root.dataset.touchMove, undefined);
    equal(touchControls.moveThumb.style.transform, "");
  });

  it("clears joystick movement on pointer cancel from the document", () => {
    const { input, document, touchControls } = createHarness({ withTouchControls: true });

    touchControls.moveZone.dispatch("pointerdown", pointerEvent({
      pointerId: 10,
      clientX: 100,
      clientY: 120
    }));
    touchControls.moveZone.dispatch("pointermove", pointerEvent({
      pointerId: 10,
      clientX: 100,
      clientY: 66
    }));
    document.dispatch("pointercancel", pointerEvent({ pointerId: 10 }));

    const snapshot = input.consumeFrameSnapshot();

    equal(snapshot.touchMovementForward, 0);
    equal(snapshot.touchMovementRight, 0);
  });

  it("clears joystick movement on lost pointer capture", () => {
    const { input, touchControls } = createHarness({ withTouchControls: true });

    touchControls.moveZone.dispatch("pointerdown", pointerEvent({
      pointerId: 11,
      clientX: 100,
      clientY: 120
    }));
    touchControls.moveZone.dispatch("pointermove", pointerEvent({
      pointerId: 11,
      clientX: 100,
      clientY: 66
    }));
    touchControls.moveZone.dispatch("lostpointercapture", pointerEvent({ pointerId: 11 }));

    const snapshot = input.consumeFrameSnapshot();

    equal(snapshot.touchMovementForward, 0);
    equal(snapshot.touchMovementRight, 0);
  });

  it("normalizes the rotation stick and keeps it active after frame consumption", () => {
    const { input, touchControls } = createHarness({ withTouchControls: true });

    touchControls.lookZone.dispatch("pointerdown", pointerEvent({
      pointerId: 12,
      clientX: 84,
      clientY: 84
    }));
    touchControls.lookZone.dispatch("pointermove", pointerEvent({
      pointerId: 12,
      clientX: 138,
      clientY: 30
    }));

    const snapshot = input.consumeFrameSnapshot();
    const nextSnapshot = input.consumeFrameSnapshot();

    equal(snapshot.touchLookStickX > 0.69 && snapshot.touchLookStickX < 0.72, true);
    equal(snapshot.touchLookStickY < -0.69 && snapshot.touchLookStickY > -0.72, true);
    equal(nextSnapshot.touchLookStickX > 0.69 && nextSnapshot.touchLookStickX < 0.72, true);
    equal(nextSnapshot.touchLookStickY < -0.69 && nextSnapshot.touchLookStickY > -0.72, true);
    equal(touchControls.root.dataset.touchLook, "active");
    equal(touchControls.lookZone.capturedPointers[0], 12);
  });

  it("clears rotation stick movement on pointer release", () => {
    const { input, touchControls } = createHarness({ withTouchControls: true });

    touchControls.lookZone.dispatch("pointerdown", pointerEvent({
      pointerId: 14,
      clientX: 84,
      clientY: 84
    }));
    touchControls.lookZone.dispatch("pointermove", pointerEvent({
      pointerId: 14,
      clientX: 138,
      clientY: 84
    }));
    touchControls.lookZone.dispatch("pointerup", pointerEvent({ pointerId: 14 }));

    const snapshot = input.consumeFrameSnapshot();

    equal(snapshot.touchLookStickX, 0);
    equal(snapshot.touchLookStickY, 0);
    equal(touchControls.root.dataset.touchLook, undefined);
    equal(touchControls.lookThumb.style.transform, "");
  });

  it("keeps keyboard state while touch movement is active", () => {
    const { input, document, touchControls } = createHarness({ withTouchControls: true });

    document.dispatch("keydown", { code: "KeyW" });
    touchControls.moveZone.dispatch("pointerdown", pointerEvent({
      pointerId: 13,
      clientX: 100,
      clientY: 120
    }));
    touchControls.moveZone.dispatch("pointermove", pointerEvent({
      pointerId: 13,
      clientX: 100,
      clientY: 66
    }));

    const snapshot = input.consumeFrameSnapshot();

    equal(input.isDown("KeyW"), true);
    equal(snapshot.touchMovementForward, 1);
  });
});

function createHarness(options: { readonly withTouchControls?: boolean } = {}): {
  readonly input: InputTracker;
  readonly document: FakeDocument;
  readonly target: FakeElement;
  readonly touchControls: {
    readonly root: FakeElement;
    readonly moveZone: FakeElement;
    readonly moveBase: FakeElement;
    readonly moveThumb: FakeElement;
    readonly lookZone: FakeElement;
    readonly lookBase: FakeElement;
    readonly lookThumb: FakeElement;
  };
} {
  const input = new InputTracker();
  const document = new FakeDocument();
  const target = new FakeElement();
  const touchControls = {
    root: new FakeElement(),
    moveZone: new FakeElement(),
    moveBase: new FakeElement(),
    moveThumb: new FakeElement(),
    lookZone: new FakeElement(),
    lookBase: new FakeElement(),
    lookThumb: new FakeElement()
  };
  const controls = options.withTouchControls
    ? {
        root: touchControls.root as unknown as HTMLElement,
        moveZone: touchControls.moveZone as unknown as HTMLElement,
        moveBase: touchControls.moveBase as unknown as HTMLElement,
        moveThumb: touchControls.moveThumb as unknown as HTMLElement,
        lookZone: touchControls.lookZone as unknown as HTMLElement,
        lookBase: touchControls.lookBase as unknown as HTMLElement,
        lookThumb: touchControls.lookThumb as unknown as HTMLElement
      }
    : undefined;
  input.attach(target as unknown as HTMLElement, document as unknown as Document, controls);

  return { input, document, target, touchControls };
}

function pointerEvent(options: {
  readonly pointerId?: number;
  readonly pointerType?: string;
  readonly clientX?: number;
  readonly clientY?: number;
  readonly button?: number;
} = {}): PointerEvent {
  return {
    pointerId: options.pointerId ?? 1,
    pointerType: options.pointerType ?? "touch",
    clientX: options.clientX ?? 0,
    clientY: options.clientY ?? 0,
    button: options.button ?? 0,
    preventDefault() {}
  } as PointerEvent;
}
