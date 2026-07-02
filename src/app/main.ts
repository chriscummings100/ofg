// Browser entry point for the OFG bootstrap.
//
// The app owns the canvas host and status text, while the C++/WASM runtime owns
// frame state, WebGPU resources, and draw submission.

import { createCanvasHost } from "./canvasHost.js";
import { createControlInputCollector, type ControlInputCollector } from "./controlInput.js";
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

const DEFAULT_PLAYER_MODEL_URL = "/assets/models/player/quaternius-superhero-male.glb";
const DEFAULT_PLAYER_ANIMATION_URL = "/assets/models/player/quaternius-ual1-standard.glb";

// Creates the canvas/runtime pair and starts the animation loop.
async function main(): Promise<void> {
  const status = document.getElementById("ofg-status");

  try {
    const host = createCanvasHost();
    const runtime = await createBrowserGameRuntime(host.canvas);
    const controlInput = createControlInputCollector(host.canvas);
    runtime.resize(
      host.size.physicalWidth,
      host.size.physicalHeight,
      host.size.devicePixelRatio
    );
    void loadDefaultPlayerModel(runtime, status);

    window.__ofgDebugStatus = () => runtime.debugStatus();
    window.addEventListener("beforeunload", () => {
      controlInput.dispose();
      runtime.dispose();
    });
    requestAnimationFrame((timeMs) =>
      renderFrame(host, runtime, controlInput, status, timeMs)
    );
  } catch (error) {
    statusMessage(status, error instanceof Error ? error.message : String(error));
  }
}

// Handles one animation frame, including host resize and runtime rendering.
function renderFrame(
  host: ReturnType<typeof createCanvasHost>,
  runtime: BrowserGameRuntime,
  controlInput: ControlInputCollector,
  status: HTMLElement | null,
  timeMs: number
): void {
  try {
    const size = host.resize();
    if (size.changed) {
      runtime.resize(size.physicalWidth, size.physicalHeight, size.devicePixelRatio);
    }
    runtime.setControlInput(controlInput.consumeSnapshot());
    runtime.frame(timeMs);
    const debugStatus = runtime.debugStatus();
    statusMessage(
      status,
      `C++/WASM WebGPU frame ${debugStatus.frameCount} - ${debugStatus.canvasWidth}x${debugStatus.canvasHeight} - ${debugStatus.surfaceFormat} - model ${debugStatus.modelLoadingState}`
    );
  } catch (error) {
    statusMessage(status, error instanceof Error ? error.message : String(error));
  }

  requestAnimationFrame((nextTimeMs) =>
    renderFrame(host, runtime, controlInput, status, nextTimeMs)
  );
}

// Fetches the default player GLBs and passes raw bytes to C++.
async function loadDefaultPlayerModel(
  runtime: BrowserGameRuntime,
  status: HTMLElement | null
): Promise<void> {
  try {
    const [playerBytes, animationBytes] = await Promise.all([
      fetchAssetBytes(DEFAULT_PLAYER_MODEL_URL),
      fetchAssetBytes(DEFAULT_PLAYER_ANIMATION_URL)
    ]);
    runtime.loadPlayerModel(playerBytes, animationBytes);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    try {
      runtime.reportPlayerModelLoadError(message);
    } catch {
      // The runtime may already be disposed during page teardown.
    }
    statusMessage(status, message);
  }
}

// Fetches one binary asset as a Uint8Array for Embind transport.
async function fetchAssetBytes(url: string): Promise<Uint8Array> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to fetch ${url}: ${response.status} ${response.statusText}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

// Writes transient runtime status text when the status element exists.
function statusMessage(element: HTMLElement | null, message: string): void {
  if (element !== null) {
    element.textContent = message;
  }
}

void main();
