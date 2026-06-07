import { startGame } from "./app/game.js";

const canvas = document.querySelector<HTMLCanvasElement>("#game-canvas");
const cameraMode = document.querySelector<HTMLElement>("#camera-mode");
const characterToggle = document.querySelector<HTMLButtonElement>("#character-toggle");
const frameTime = document.querySelector<HTMLElement>("#frame-time");

if (canvas === null || cameraMode === null || characterToggle === null || frameTime === null) {
  throw new Error("OFG could not find its root DOM elements.");
}

startGame({ canvas, cameraMode, characterToggle, frameTime }).catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  cameraMode.textContent = "WEBGPU";
  frameTime.textContent = "Unavailable";
  console.error(message);
});
