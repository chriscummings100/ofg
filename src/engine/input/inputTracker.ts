// Browser input collector for OFG. It keeps raw DOM keyboard, pointer-lock
// mouse, and mobile touch-control state out of the Rust-facing frame contract.

export type TouchControlElements = {
  readonly root: HTMLElement;
  readonly moveZone: HTMLElement;
  readonly moveBase: HTMLElement;
  readonly moveThumb: HTMLElement;
  readonly lookZone: HTMLElement;
};

export type InputSnapshot = {
  readonly mouseDeltaX: number;
  readonly mouseDeltaY: number;
  readonly touchLookDeltaX: number;
  readonly touchLookDeltaY: number;
  readonly touchMovementForward: number;
  readonly touchMovementRight: number;
};

type PointerPoint = {
  readonly x: number;
  readonly y: number;
};

const TOUCH_JOYSTICK_RADIUS_PIXELS = 54;
const TOUCH_JOYSTICK_DEAD_ZONE = 0.12;

export class InputTracker {
  private readonly keysDown = new Set<string>();
  private readonly keysPressed = new Set<string>();
  private mouseDeltaX = 0;
  private mouseDeltaY = 0;
  private touchLookDeltaX = 0;
  private touchLookDeltaY = 0;
  private touchMovementForward = 0;
  private touchMovementRight = 0;
  private touchMovePointerId: number | undefined;
  private touchMoveOrigin: PointerPoint | undefined;
  private touchLookPointerId: number | undefined;
  private touchLookPrevious: PointerPoint | undefined;
  private touchControls: TouchControlElements | undefined;

  /// Attaches keyboard, pointer-lock mouse, and optional touch-control listeners.
  attach(
    target: HTMLElement,
    documentRef: Document = document,
    touchControls?: TouchControlElements
  ): void {
    this.touchControls = touchControls;

    documentRef.addEventListener("keydown", (event) => {
      if (!this.keysDown.has(event.code)) {
        this.keysPressed.add(event.code);
      }

      this.keysDown.add(event.code);
    });

    documentRef.addEventListener("keyup", (event) => {
      this.keysDown.delete(event.code);
    });

    documentRef.addEventListener("mousemove", (event) => {
      if (documentRef.pointerLockElement !== target) {
        return;
      }

      this.mouseDeltaX += event.movementX;
      this.mouseDeltaY += event.movementY;
    });

    documentRef.addEventListener("pointerdown", (event) => {
      if (event.pointerType === "touch") {
        documentRef.documentElement?.classList.add("touch-input");
      }
    }, { passive: true });

    target.addEventListener("click", () => {
      void target.requestPointerLock();
    });

    if (touchControls !== undefined) {
      this.attachTouchControls(touchControls, documentRef);
    }
  }

  /// Returns whether a keyboard code is currently held down.
  isDown(code: string): boolean {
    return this.keysDown.has(code);
  }

  /// Consumes one edge-triggered key press for commands such as camera toggles.
  consumePress(code: string): boolean {
    const wasPressed = this.keysPressed.has(code);
    this.keysPressed.delete(code);
    return wasPressed;
  }

  /// Returns frame-local deltas and current touch axes, then clears transient deltas.
  consumeFrameSnapshot(): InputSnapshot {
    const snapshot = {
      mouseDeltaX: this.mouseDeltaX,
      mouseDeltaY: this.mouseDeltaY,
      touchLookDeltaX: this.touchLookDeltaX,
      touchLookDeltaY: this.touchLookDeltaY,
      touchMovementForward: this.touchMovementForward,
      touchMovementRight: this.touchMovementRight
    };

    this.mouseDeltaX = 0;
    this.mouseDeltaY = 0;
    this.touchLookDeltaX = 0;
    this.touchLookDeltaY = 0;
    this.keysPressed.clear();

    return snapshot;
  }

  /// Wires Pointer Events for the mobile movement and look regions.
  private attachTouchControls(controls: TouchControlElements, documentRef: Document): void {
    controls.root.addEventListener("contextmenu", preventDefault);
    controls.moveZone.addEventListener("pointerdown", (event) => this.beginTouchMove(event));
    controls.moveZone.addEventListener("pointermove", (event) => this.updateTouchMove(event));
    controls.moveZone.addEventListener("pointerup", (event) => this.endTouchPointer(event));
    controls.moveZone.addEventListener("pointercancel", (event) => this.endTouchPointer(event));
    controls.moveZone.addEventListener("lostpointercapture", (event) =>
      this.endTouchPointer(event)
    );

    controls.lookZone.addEventListener("pointerdown", (event) => this.beginTouchLook(event));
    controls.lookZone.addEventListener("pointermove", (event) => this.updateTouchLook(event));
    controls.lookZone.addEventListener("pointerup", (event) => this.endTouchPointer(event));
    controls.lookZone.addEventListener("pointercancel", (event) => this.endTouchPointer(event));
    controls.lookZone.addEventListener("lostpointercapture", (event) =>
      this.endTouchPointer(event)
    );

    documentRef.addEventListener("pointermove", (event) => {
      this.updateTouchMove(event);
      this.updateTouchLook(event);
    });
    documentRef.addEventListener("pointerup", (event) => this.endTouchPointer(event));
    documentRef.addEventListener("pointercancel", (event) => this.endTouchPointer(event));
  }

  /// Starts the floating movement joystick at the pointer-down position.
  private beginTouchMove(event: PointerEvent): void {
    if (this.touchMovePointerId !== undefined || isNonPrimaryButton(event)) {
      return;
    }

    markHandled(event);
    this.touchMovePointerId = event.pointerId;
    this.touchMoveOrigin = pointerPoint(event);
    this.touchMovementForward = 0;
    this.touchMovementRight = 0;
    trySetPointerCapture(event.currentTarget, event.pointerId);
    this.updateTouchMoveVisuals({ x: 0, y: 0 });
  }

  /// Updates the active joystick axes from a pointer location.
  private updateTouchMove(event: PointerEvent): void {
    if (event.pointerId !== this.touchMovePointerId || this.touchMoveOrigin === undefined) {
      return;
    }

    markHandled(event);
    const offset = clampOffset({
      x: event.clientX - this.touchMoveOrigin.x,
      y: event.clientY - this.touchMoveOrigin.y
    }, TOUCH_JOYSTICK_RADIUS_PIXELS);
    const axes = joystickAxesFromOffset(offset);
    this.touchMovementForward = axes.forward;
    this.touchMovementRight = axes.right;
    this.updateTouchMoveVisuals(offset);
  }

  /// Starts accumulating camera-look deltas from the right-side touch region.
  private beginTouchLook(event: PointerEvent): void {
    if (this.touchLookPointerId !== undefined || isNonPrimaryButton(event)) {
      return;
    }

    markHandled(event);
    this.touchLookPointerId = event.pointerId;
    this.touchLookPrevious = pointerPoint(event);
    trySetPointerCapture(event.currentTarget, event.pointerId);
  }

  /// Accumulates touch-look deltas until the next frame snapshot consumes them.
  private updateTouchLook(event: PointerEvent): void {
    if (event.pointerId !== this.touchLookPointerId || this.touchLookPrevious === undefined) {
      return;
    }

    markHandled(event);
    const current = pointerPoint(event);
    this.touchLookDeltaX += current.x - this.touchLookPrevious.x;
    this.touchLookDeltaY += current.y - this.touchLookPrevious.y;
    this.touchLookPrevious = current;
  }

  /// Clears a movement or look pointer after release, cancel, or capture loss.
  private endTouchPointer(event: PointerEvent): void {
    if (event.pointerId === this.touchMovePointerId) {
      markHandled(event);
      this.touchMovePointerId = undefined;
      this.touchMoveOrigin = undefined;
      this.touchMovementForward = 0;
      this.touchMovementRight = 0;
      this.resetTouchMoveVisuals();
    }

    if (event.pointerId === this.touchLookPointerId) {
      markHandled(event);
      this.touchLookPointerId = undefined;
      this.touchLookPrevious = undefined;
    }
  }

  /// Moves the joystick base/thumb to match the active pointer.
  private updateTouchMoveVisuals(offset: PointerPoint): void {
    if (this.touchControls === undefined || this.touchMoveOrigin === undefined) {
      return;
    }

    const zoneRect = this.touchControls.moveZone.getBoundingClientRect();
    this.touchControls.root.dataset.touchMove = "active";
    this.touchControls.moveBase.style.left = `${this.touchMoveOrigin.x - zoneRect.left}px`;
    this.touchControls.moveBase.style.top = `${this.touchMoveOrigin.y - zoneRect.top}px`;
    this.touchControls.moveThumb.style.transform =
      `translate(calc(-50% + ${offset.x}px), calc(-50% + ${offset.y}px))`;
  }

  /// Resets joystick visuals to the CSS-defined idle position.
  private resetTouchMoveVisuals(): void {
    if (this.touchControls === undefined) {
      return;
    }

    delete this.touchControls.root.dataset.touchMove;
    this.touchControls.moveBase.style.removeProperty("left");
    this.touchControls.moveBase.style.removeProperty("top");
    this.touchControls.moveThumb.style.removeProperty("transform");
  }
}

/// Converts a clamped joystick offset into normalized movement axes.
function joystickAxesFromOffset(offset: PointerPoint): { readonly forward: number; readonly right: number } {
  const rawRight = offset.x / TOUCH_JOYSTICK_RADIUS_PIXELS;
  const rawForward = -offset.y / TOUCH_JOYSTICK_RADIUS_PIXELS;
  const magnitude = Math.hypot(rawRight, rawForward);

  if (magnitude <= TOUCH_JOYSTICK_DEAD_ZONE) {
    return { forward: 0, right: 0 };
  }

  const scaledMagnitude = (magnitude - TOUCH_JOYSTICK_DEAD_ZONE) / (1 - TOUCH_JOYSTICK_DEAD_ZONE);
  const scale = scaledMagnitude / magnitude;
  return {
    forward: clampAxis(rawForward * scale),
    right: clampAxis(rawRight * scale)
  };
}

/// Limits a pointer offset to the configured joystick radius.
function clampOffset(offset: PointerPoint, radius: number): PointerPoint {
  const magnitude = Math.hypot(offset.x, offset.y);
  if (magnitude <= radius || magnitude === 0) {
    return offset;
  }

  const scale = radius / magnitude;
  return {
    x: offset.x * scale,
    y: offset.y * scale
  };
}

/// Clamps one movement axis to the browser frame-input range.
function clampAxis(value: number): number {
  const clamped = Math.max(-1, Math.min(1, value));
  return Object.is(clamped, -0) ? 0 : clamped;
}

/// Reads the fields needed from a PointerEvent as a tiny immutable point.
function pointerPoint(event: PointerEvent): PointerPoint {
  return {
    x: event.clientX,
    y: event.clientY
  };
}

/// Keeps touch controls from selecting text, scrolling, or opening context menus.
function markHandled(event: PointerEvent): void {
  event.preventDefault();
}

/// Handles context-menu cancellation from long-press gestures.
function preventDefault(event: Event): void {
  event.preventDefault();
}

/// Ignores secondary mouse buttons while allowing touch and pen pointers.
function isNonPrimaryButton(event: PointerEvent): boolean {
  return event.button !== undefined && event.button > 0;
}

/// Uses pointer capture when the browser accepts the active pointer ID.
function trySetPointerCapture(target: EventTarget | null, pointerId: number): void {
  const element = target as HTMLElement | null;
  try {
    element?.setPointerCapture?.(pointerId);
  } catch {
    // Synthetic PointerEvents in tests and smoke checks may not be active browser pointers.
  }
}
