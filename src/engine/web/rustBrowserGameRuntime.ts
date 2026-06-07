// Browser shell facade for the Rust-owned game runtime. TypeScript keeps module
// loading and debug-hook convenience methods here; terrain streaming, mesh
// upload, and texture ownership live inside engine_web.wasm.

import type { TerrainPresetId, WorldDescriptor } from "../world/terrainDescriptor.js";
import type {
  BrowserFrameInput,
  GameCommand,
  GameDebugSnapshot,
  RustBrowserGameCommand,
  RustBrowserGameDebugSnapshot
} from "./browserGameTypes.js";
import { RustBrowserGameAdapter } from "./rustBrowserGameAdapter.js";

const TERRAIN_PRESET_CODES: Readonly<Record<TerrainPresetId, number>> = Object.freeze({
  seed: 0,
  rollingHills: 1,
  mountainValley: 2,
  rockyHighland: 3
});

export type RustBrowserGameRenderer = {
  readonly runtime: "rust-wgpu";
  tick(frame: BrowserFrameInput): void;
  command(command: RustBrowserGameCommand): void;
  getDebugSnapshot(): RustBrowserGameDebugSnapshot;
};

export type RustBrowserGameRuntimeDependencies = {
  readonly renderer: RustBrowserGameRenderer;
};

export class RustBrowserGameRuntime {
  constructor(
    private readonly dependencies: RustBrowserGameRuntimeDependencies
  ) {}

  tick(frame: BrowserFrameInput): void {
    this.dependencies.renderer.tick(frame);
  }

  command(command: GameCommand): void {
    this.dependencies.renderer.command(command);
  }

  debugSnapshot(): GameDebugSnapshot {
    return this.dependencies.renderer.getDebugSnapshot();
  }
}

export async function createRustBrowserGameRuntime(
  canvas: HTMLCanvasElement,
  descriptor: WorldDescriptor
): Promise<RustBrowserGameRuntime> {
  const renderer = await RustBrowserGameAdapter.create(canvas);
  renderer.command({
    type: "resetGame",
    terrainSeed: descriptor.seed,
    terrainPreset: terrainPresetToWasmCode(descriptor.terrainPreset)
  });

  return new RustBrowserGameRuntime({
    renderer
  });
}

export function terrainPresetToWasmCode(preset: TerrainPresetId): number {
  return TERRAIN_PRESET_CODES[preset];
}
