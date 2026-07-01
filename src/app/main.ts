// Browser entry point for the OFG bootstrap.
//
// The app owns the canvas host and status text, while the C++/WASM runtime owns
// frame state, WebGPU resources, and draw submission.

import { createCanvasHost } from "./canvasHost.js";
import { createDebugInputCollector, type DebugInputCollector } from "./debugInput.js";
import {
  createBrowserGameRuntime,
  type BrowserGameRuntime,
  type RuntimeDebugStatus
} from "./wasmRuntime.js";

declare global {
  interface Window {
    __ofgDebugStatus?: () => RuntimeDebugStatus | null;
  }
}

// Creates the canvas/runtime pair and starts the animation loop.
async function main(): Promise<void> {
  const status = document.getElementById("ofg-status");

  try {
    const host = createCanvasHost();
    const runtime = await createBrowserGameRuntime(host.canvas);
    const debugInput = createDebugInputCollector(host.canvas);
    runtime.resize(
      host.size.physicalWidth,
      host.size.physicalHeight,
      host.size.devicePixelRatio
    );

    window.__ofgDebugStatus = () => runtime.debugStatus();
    window.addEventListener("beforeunload", () => {
      debugInput.dispose();
      runtime.dispose();
    });
    requestAnimationFrame((timeMs) =>
      renderFrame(host, runtime, debugInput, status, timeMs)
    );
  } catch (error) {
    statusMessage(status, error instanceof Error ? error.message : String(error));
  }
}

// Handles one animation frame, including host resize and runtime rendering.
function renderFrame(
  host: ReturnType<typeof createCanvasHost>,
  runtime: BrowserGameRuntime,
  debugInput: DebugInputCollector,
  status: HTMLElement | null,
  timeMs: number
): void {
  try {
    const size = host.resize();
    if (size.changed) {
      runtime.resize(size.physicalWidth, size.physicalHeight, size.devicePixelRatio);
    }
    runtime.setDebugCameraInput(debugInput.consumeSnapshot());
    runtime.frame(timeMs);
    const debugStatus = runtime.debugStatus();
    statusMessage(
      status,
      `C++/WASM WebGPU frame ${debugStatus.frameCount} - ${debugStatus.canvasWidth}x${debugStatus.canvasHeight} - ${debugStatus.surfaceFormat}`
    );
  } catch (error) {
    statusMessage(status, error instanceof Error ? error.message : String(error));
  }

  requestAnimationFrame((nextTimeMs) =>
    renderFrame(host, runtime, debugInput, status, nextTimeMs)
  );
}

// Writes transient runtime status text when the status element exists.
function statusMessage(element: HTMLElement | null, message: string): void {
  if (element !== null) {
    element.textContent = message;
  }
}

void main();
