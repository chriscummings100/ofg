export type InputSnapshot = {
  readonly mouseDeltaX: number;
  readonly mouseDeltaY: number;
};

export class InputTracker {
  private readonly keysDown = new Set<string>();
  private readonly keysPressed = new Set<string>();
  private mouseDeltaX = 0;
  private mouseDeltaY = 0;

  attach(target: HTMLElement, documentRef: Document = document): void {
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

    target.addEventListener("click", () => {
      void target.requestPointerLock();
    });
  }

  isDown(code: string): boolean {
    return this.keysDown.has(code);
  }

  consumePress(code: string): boolean {
    const wasPressed = this.keysPressed.has(code);
    this.keysPressed.delete(code);
    return wasPressed;
  }

  consumeFrameSnapshot(): InputSnapshot {
    const snapshot = {
      mouseDeltaX: this.mouseDeltaX,
      mouseDeltaY: this.mouseDeltaY
    };

    this.mouseDeltaX = 0;
    this.mouseDeltaY = 0;
    this.keysPressed.clear();

    return snapshot;
  }
}
