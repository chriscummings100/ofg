import { startGame } from "./app/game.js";

const canvas = document.querySelector<HTMLCanvasElement>("#game-canvas");
const terrainDebugOverlay = document.querySelector<HTMLCanvasElement>("#terrain-debug-overlay");
const cameraMode = document.querySelector<HTMLElement>("#camera-mode");
const frameTime = document.querySelector<HTMLElement>("#frame-time");

if (canvas === null || terrainDebugOverlay === null || cameraMode === null || frameTime === null) {
  throw new Error("OFG could not find its root DOM elements.");
}

startGame({ canvas, terrainDebugOverlay, cameraMode, frameTime }).catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  cameraMode.textContent = "WEBGPU";
  frameTime.textContent = "Unavailable";
  console.error(message);
});
