import { Scene } from "./Scene.js";

let activeScene: Scene | undefined;

export function createScene(): Scene {
  activeScene = new Scene();
  return activeScene;
}

export function getScene(): Scene {
  if (activeScene === undefined) {
    throw new Error("No active scene has been created.");
  }

  return activeScene;
}

export function setScene(scene: Scene): void {
  activeScene = scene;
}

export function resetScene(): Scene {
  activeScene = new Scene();
  return activeScene;
}
