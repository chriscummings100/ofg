// Browser entry point for the OFG bootstrap. It hosts the canvas and delegates
// rendering to the Rust/WASM runtime.

import { createCanvasHost } from "./canvasHost.js";
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

async function main(): Promise<void> {
  const status = document.getElementById("ofg-status");

  try {
    const host = createCanvasHost();
    const runtime = await createBrowserGameRuntime(host.canvas);
    runtime.resize(
      host.size.physicalWidth,
      host.size.physicalHeight,
      host.size.devicePixelRatio
    );

    window.__ofgDebugStatus = () => runtime.debugStatus();
    requestAnimationFrame((timeMs) => renderFrame(host, runtime, status, timeMs));
  } catch (error) {
    statusMessage(status, error instanceof Error ? error.message : String(error));
  }
}

function renderFrame(
  host: ReturnType<typeof createCanvasHost>,
  runtime: BrowserGameRuntime,
  status: HTMLElement | null,
  timeMs: number
): void {
  try {
    const size = host.resize();
    if (size.changed) {
      runtime.resize(size.physicalWidth, size.physicalHeight, size.devicePixelRatio);
    }
    runtime.frame(timeMs);
    const debugStatus = runtime.debugStatus();
    statusMessage(
      status,
      `Rust/WASM WebGPU frame ${debugStatus.frameCount} - ${debugStatus.canvasWidth}x${debugStatus.canvasHeight} - ${debugStatus.surfaceFormat}`
    );
  } catch (error) {
    statusMessage(status, error instanceof Error ? error.message : String(error));
  }

  requestAnimationFrame((nextTimeMs) => renderFrame(host, runtime, status, nextTimeMs));
}

function statusMessage(element: HTMLElement | null, message: string): void {
  if (element !== null) {
    element.textContent = message;
  }
}

void main();
