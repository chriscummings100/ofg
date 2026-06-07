import { startGame, type GameTouchControlElements } from "./app/game.js";

const canvas = document.querySelector<HTMLCanvasElement>("#game-canvas");
const cameraMode = document.querySelector<HTMLElement>("#camera-mode");
const characterToggle = document.querySelector<HTMLButtonElement>("#character-toggle");
const frameTime = document.querySelector<HTMLElement>("#frame-time");
const touchControls = readTouchControls();

if (
  canvas === null ||
  cameraMode === null ||
  characterToggle === null ||
  frameTime === null ||
  touchControls === null
) {
  throw new Error("OFG could not find its root DOM elements.");
}

startGame({ canvas, cameraMode, characterToggle, frameTime, touchControls }).catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  cameraMode.textContent = "WEBGPU";
  frameTime.textContent = "Unavailable";
  console.error(message);
});

/// Reads the mobile touch-control DOM elements required by the browser input layer.
function readTouchControls(): GameTouchControlElements | null {
  const root = document.querySelector<HTMLElement>("#touch-controls");
  const moveZone = document.querySelector<HTMLElement>("#touch-move-zone");
  const moveBase = document.querySelector<HTMLElement>("#touch-move-base");
  const moveThumb = document.querySelector<HTMLElement>("#touch-move-thumb");
  const lookZone = document.querySelector<HTMLElement>("#touch-look-zone");
  const lookBase = document.querySelector<HTMLElement>("#touch-look-base");
  const lookThumb = document.querySelector<HTMLElement>("#touch-look-thumb");
  const cameraToggle = document.querySelector<HTMLButtonElement>("#touch-camera-toggle");

  if (
    root === null ||
    moveZone === null ||
    moveBase === null ||
    moveThumb === null ||
    lookZone === null ||
    lookBase === null ||
    lookThumb === null ||
    cameraToggle === null
  ) {
    return null;
  }

  return {
    root,
    moveZone,
    moveBase,
    moveThumb,
    lookZone,
    lookBase,
    lookThumb,
    cameraToggle
  };
}
