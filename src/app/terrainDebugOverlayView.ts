import type { Vec3 } from "../engine/math/vec3.js";
import {
  buildTerrainDebugOverlay,
  nextTerrainDebugOverlayState,
  type TerrainDebugOverlayState
} from "../engine/world/terrainDebugOverlay.js";
import type { TerrainGenerator } from "../engine/world/terrainGenerator.js";

const UPDATE_INTERVAL_SECONDS = 0.2;
const OVERLAY_RESOLUTION = 72;
const OVERLAY_WORLD_SIZE = 96;

export class TerrainDebugOverlayView {
  private readonly canvas: HTMLCanvasElement;
  private readonly context: CanvasRenderingContext2D;
  private state: TerrainDebugOverlayState;
  private elapsedSinceUpdate = Number.POSITIVE_INFINITY;

  constructor(canvas: HTMLCanvasElement, initialState: TerrainDebugOverlayState = "off") {
    const context = canvas.getContext("2d", { alpha: false });
    if (context === null) {
      throw new Error("Unable to create terrain debug overlay canvas context.");
    }

    this.canvas = canvas;
    this.context = context;
    this.state = initialState;
    this.canvas.width = OVERLAY_RESOLUTION;
    this.canvas.height = OVERLAY_RESOLUTION;
    this.syncVisibility();
  }

  getState(): TerrainDebugOverlayState {
    return this.state;
  }

  setState(state: TerrainDebugOverlayState): void {
    this.state = state;
    this.elapsedSinceUpdate = Number.POSITIVE_INFINITY;
    this.syncVisibility();
  }

  cycleState(): TerrainDebugOverlayState {
    this.setState(nextTerrainDebugOverlayState(this.state));
    return this.state;
  }

  update(deltaSeconds: number, terrain: TerrainGenerator, center: Vec3): void {
    if (this.state === "off") {
      return;
    }

    this.elapsedSinceUpdate += deltaSeconds;
    if (this.elapsedSinceUpdate < UPDATE_INTERVAL_SECONDS) {
      return;
    }

    this.render(terrain, center);
  }

  render(terrain: TerrainGenerator, center: Vec3): void {
    if (this.state === "off") {
      return;
    }

    const overlay = buildTerrainDebugOverlay(terrain, {
      center,
      mode: this.state,
      resolution: OVERLAY_RESOLUTION,
      worldSize: OVERLAY_WORLD_SIZE
    });
    const imageData = this.context.createImageData(overlay.width, overlay.height);
    imageData.data.set(overlay.pixels);
    this.context.putImageData(imageData, 0, 0);
    this.canvas.dataset.mode = this.state;
    this.elapsedSinceUpdate = 0;
  }

  private syncVisibility(): void {
    this.canvas.hidden = this.state === "off";
    this.canvas.dataset.mode = this.state;
  }
}
