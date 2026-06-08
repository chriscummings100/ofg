// Browser render-debug controls and live performance overlay for OFG.
// This module only displays Rust-owned debug data and forwards commands through
// callbacks owned by the browser game shell.

import { buildPerfOverlayText, type CombinedPerfStats } from "./perfDebug.js";
import type {
  RenderDebugOptions,
  RenderDebugOptionsUpdate
} from "../engine/web/browserGameTypes.js";

export type RenderDebugUiElements = {
  readonly panelToggle: HTMLButtonElement;
  readonly perfToggle: HTMLButtonElement;
  readonly panel: HTMLElement;
  readonly terrainLodSelect: HTMLSelectElement;
  readonly skyCheckbox: HTMLInputElement;
  readonly shadowPassCheckbox: HTMLInputElement;
  readonly shadowCascadeCheckboxes: readonly HTMLInputElement[];
  readonly shadowSamplingCheckbox: HTMLInputElement;
  readonly shadowSunModeSelect: HTMLSelectElement;
  readonly whiteTexturesCheckbox: HTMLInputElement;
  readonly materialModeSelect: HTMLSelectElement;
  readonly resetButton: HTMLButtonElement;
  readonly resetPerfButton: HTMLButtonElement;
  readonly perfOverlay: HTMLElement;
};

export type RenderDebugUiCallbacks = {
  readonly getRenderDebugOptions: () => RenderDebugOptions;
  readonly setRenderDebugOptions: (options: RenderDebugOptionsUpdate) => void;
  readonly resetRenderDebugOptions: () => void;
  readonly resetPerfStats: () => void;
  readonly focusCanvas: () => void;
};

export type RenderDebugUiController = {
  readonly update: (stats: CombinedPerfStats) => void;
  readonly togglePanel: () => void;
  readonly togglePerfOverlay: () => void;
};

export type TerrainLodUiMode = "all" | "lod0" | "lod1" | "lod2" | "lod3plus" | "custom";

const terrainLodModeMasks: Record<Exclude<TerrainLodUiMode, "custom">, number> = {
  all: 0xFFFFFFFF,
  lod0: 0b000001,
  lod1: 0b000010,
  lod2: 0b000100,
  lod3plus: 0xFFFFFFF8
};

/// Creates the debug UI controller and wires DOM events to Rust command callbacks.
export function createRenderDebugUi(
  elements: RenderDebugUiElements,
  callbacks: RenderDebugUiCallbacks
): RenderDebugUiController {
  elements.panelToggle.addEventListener("click", () => {
    setPanelVisible(elements, elements.panel.hidden);
    callbacks.focusCanvas();
  });
  elements.perfToggle.addEventListener("click", () => {
    setPerfOverlayVisible(elements, elements.perfOverlay.hidden);
    callbacks.focusCanvas();
  });
  elements.terrainLodSelect.addEventListener("change", () => {
    const mask = terrainLodModeToMask(elements.terrainLodSelect.value);
    applyDebugUpdate(callbacks, { terrainLodMask: mask });
  });
  elements.skyCheckbox.addEventListener("change", () => {
    applyDebugUpdate(callbacks, { skyEnabled: elements.skyCheckbox.checked });
  });
  elements.shadowPassCheckbox.addEventListener("change", () => {
    applyDebugUpdate(callbacks, { shadowPassEnabled: elements.shadowPassCheckbox.checked });
  });
  for (const checkbox of elements.shadowCascadeCheckboxes) {
    checkbox.addEventListener("change", () => {
      const fallback = callbacks.getRenderDebugOptions().shadowCascadeMask;
      const mask = shadowCascadeMaskFromChecks(
        elements.shadowCascadeCheckboxes.map((input) => input.checked),
        fallback
      );
      applyDebugUpdate(callbacks, { shadowCascadeMask: mask });
    });
  }
  elements.shadowSamplingCheckbox.addEventListener("change", () => {
    applyDebugUpdate(callbacks, {
      shadowSamplingEnabled: elements.shadowSamplingCheckbox.checked
    });
  });
  elements.shadowSunModeSelect.addEventListener("change", () => {
    applyDebugUpdate(callbacks, {
      shadowSunMode: elements.shadowSunModeSelect.value as RenderDebugOptions["shadowSunMode"]
    });
  });
  elements.whiteTexturesCheckbox.addEventListener("change", () => {
    applyDebugUpdate(callbacks, {
      whiteTexturesEnabled: elements.whiteTexturesCheckbox.checked
    });
  });
  elements.materialModeSelect.addEventListener("change", () => {
    applyDebugUpdate(callbacks, {
      materialMode: elements.materialModeSelect.value as RenderDebugOptions["materialMode"]
    });
  });
  elements.resetButton.addEventListener("click", () => {
    callbacks.resetRenderDebugOptions();
    syncControls(elements, callbacks.getRenderDebugOptions());
    callbacks.focusCanvas();
  });
  elements.resetPerfButton.addEventListener("click", () => {
    callbacks.resetPerfStats();
    callbacks.focusCanvas();
  });

  syncControls(elements, callbacks.getRenderDebugOptions());
  setPanelVisible(elements, false);
  setPerfOverlayVisible(elements, false);

  return {
    update(stats) {
      syncControls(elements, stats.renderDebugOptions);
      if (!elements.perfOverlay.hidden) {
        elements.perfOverlay.textContent = buildPerfOverlayText(stats);
      }
    },
    togglePanel() {
      setPanelVisible(elements, elements.panel.hidden);
    },
    togglePerfOverlay() {
      setPerfOverlayVisible(elements, elements.perfOverlay.hidden);
    }
  };
}

/// Converts a Rust terrain LOD bitmask to the nearest UI mode.
export function terrainLodMaskToMode(mask: number): TerrainLodUiMode {
  for (const [mode, modeMask] of Object.entries(terrainLodModeMasks)) {
    if ((mask >>> 0) === modeMask) {
      return mode as TerrainLodUiMode;
    }
  }

  return "custom";
}

/// Converts a UI terrain LOD mode to the Rust terrain LOD bitmask.
export function terrainLodModeToMask(mode: string): number {
  if (isTerrainLodMode(mode) && mode !== "custom") {
    return terrainLodModeMasks[mode] >>> 0;
  }

  throw new Error(`Unknown terrain LOD UI mode '${mode}'.`);
}

/// Builds a non-empty shadow cascade mask from checkbox states.
export function shadowCascadeMaskFromChecks(
  checked: readonly boolean[],
  fallbackMask: number
): number {
  const mask = checked.reduce((nextMask, isChecked, index) => {
    return isChecked ? nextMask | (1 << index) : nextMask;
  }, 0);

  return mask === 0 ? fallbackMask : mask;
}

/// Applies one debug option update through the browser shell callbacks.
function applyDebugUpdate(
  callbacks: RenderDebugUiCallbacks,
  update: RenderDebugOptionsUpdate
): void {
  callbacks.setRenderDebugOptions(update);
  callbacks.focusCanvas();
}

/// Synchronizes the DOM controls from the latest Rust-owned render debug state.
function syncControls(elements: RenderDebugUiElements, options: RenderDebugOptions): void {
  const terrainMode = terrainLodMaskToMode(options.terrainLodMask);
  elements.terrainLodSelect.value = terrainMode;
  elements.skyCheckbox.checked = options.skyEnabled;
  elements.shadowPassCheckbox.checked = options.shadowPassEnabled;
  elements.shadowSamplingCheckbox.checked = options.shadowSamplingEnabled;
  elements.shadowSunModeSelect.value = options.shadowSunMode;
  elements.whiteTexturesCheckbox.checked = options.whiteTexturesEnabled;
  elements.materialModeSelect.value = options.materialMode;

  elements.shadowCascadeCheckboxes.forEach((checkbox, index) => {
    checkbox.checked = (options.shadowCascadeMask & (1 << index)) !== 0;
  });
}

/// Shows or hides the render-debug controls panel.
function setPanelVisible(elements: RenderDebugUiElements, visible: boolean): void {
  elements.panel.hidden = !visible;
  elements.panelToggle.setAttribute("aria-expanded", String(visible));
  elements.panelToggle.dataset.active = String(visible);
}

/// Shows or hides the live perf overlay.
function setPerfOverlayVisible(elements: RenderDebugUiElements, visible: boolean): void {
  elements.perfOverlay.hidden = !visible;
  elements.perfToggle.setAttribute("aria-pressed", String(visible));
  elements.perfToggle.dataset.active = String(visible);
}

/// Returns whether a value names a known terrain LOD UI mode.
function isTerrainLodMode(mode: string): mode is TerrainLodUiMode {
  return mode === "all" ||
    mode === "lod0" ||
    mode === "lod1" ||
    mode === "lod2" ||
    mode === "lod3plus" ||
    mode === "custom";
}
