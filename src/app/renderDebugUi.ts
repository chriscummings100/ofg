// Browser render-debug controls and live performance overlay for OFG.
// This module only displays Rust-owned debug data and forwards commands through
// callbacks owned by the browser game shell.

import { buildPerfOverlayText, type CombinedPerfStats } from "./perfDebug.js";
import type {
  PostProcessDebugView,
  RenderDebugOptions,
  RenderDebugOptionsUpdate
} from "../engine/web/browserGameTypes.js";
import type { EngineWebRendererStatus } from "../engine/web/engineWebWasm.js";

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
  readonly postDebugViewSelect: HTMLSelectElement;
  readonly postToneMappingCheckbox: HTMLInputElement;
  readonly postExposureInput: HTMLInputElement;
  readonly postBloomCheckbox: HTMLInputElement;
  readonly postBloomThresholdInput: HTMLInputElement;
  readonly postBloomIntensityInput: HTMLInputElement;
  readonly postDofCheckbox: HTMLInputElement;
  readonly postDofFocusInput: HTMLInputElement;
  readonly postDofRangeInput: HTMLInputElement;
  readonly postDofBlurInput: HTMLInputElement;
  readonly postResetButton: HTMLButtonElement;
  readonly resetButton: HTMLButtonElement;
  readonly resetPerfButton: HTMLButtonElement;
  readonly perfOverlay: HTMLElement;
};

export type PostProcessDebugState = Pick<
  EngineWebRendererStatus,
  | "postProcessDebugView"
  | "postProcessExposure"
  | "postProcessToneMappingEnabled"
  | "postProcessBloomEnabled"
  | "postProcessBloomThreshold"
  | "postProcessBloomIntensity"
  | "postProcessDofEnabled"
  | "postProcessDofFocusDistance"
  | "postProcessDofFocusRange"
  | "postProcessDofMaxBlurPixels"
>;

export type RenderDebugUiCallbacks = {
  readonly getRenderDebugOptions: () => RenderDebugOptions;
  readonly getPostProcessState: () => PostProcessDebugState;
  readonly setRenderDebugOptions: (options: RenderDebugOptionsUpdate) => void;
  readonly resetRenderDebugOptions: () => void;
  readonly setPostProcessDebugView: (view: PostProcessDebugView) => void;
  readonly setPostProcessToneMapping: (enabled: boolean, exposure: number) => void;
  readonly setPostProcessBloom: (
    enabled: boolean,
    threshold: number,
    intensity: number
  ) => void;
  readonly setPostProcessDepthOfField: (
    enabled: boolean,
    focusDistance: number,
    focusRange: number,
    maxBlurPixels: number
  ) => void;
  readonly resetPostProcess: () => void;
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
  elements.postDebugViewSelect.addEventListener("change", () => {
    callbacks.setPostProcessDebugView(
      elements.postDebugViewSelect.value as PostProcessDebugView
    );
    callbacks.focusCanvas();
  });
  elements.postToneMappingCheckbox.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessToneMapping(
      elements.postToneMappingCheckbox.checked,
      readNumberInput(elements.postExposureInput, state.postProcessExposure)
    );
    callbacks.focusCanvas();
  });
  elements.postExposureInput.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessToneMapping(
      state.postProcessToneMappingEnabled,
      readNumberInput(elements.postExposureInput, state.postProcessExposure)
    );
    callbacks.focusCanvas();
  });
  elements.postBloomCheckbox.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessBloom(
      elements.postBloomCheckbox.checked,
      readNumberInput(elements.postBloomThresholdInput, state.postProcessBloomThreshold),
      readNumberInput(elements.postBloomIntensityInput, state.postProcessBloomIntensity)
    );
    callbacks.focusCanvas();
  });
  elements.postBloomThresholdInput.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessBloom(
      state.postProcessBloomEnabled,
      readNumberInput(elements.postBloomThresholdInput, state.postProcessBloomThreshold),
      state.postProcessBloomIntensity
    );
    callbacks.focusCanvas();
  });
  elements.postBloomIntensityInput.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessBloom(
      state.postProcessBloomEnabled,
      state.postProcessBloomThreshold,
      readNumberInput(elements.postBloomIntensityInput, state.postProcessBloomIntensity)
    );
    callbacks.focusCanvas();
  });
  elements.postDofCheckbox.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessDepthOfField(
      elements.postDofCheckbox.checked,
      readNumberInput(elements.postDofFocusInput, state.postProcessDofFocusDistance),
      readNumberInput(elements.postDofRangeInput, state.postProcessDofFocusRange),
      readNumberInput(elements.postDofBlurInput, state.postProcessDofMaxBlurPixels)
    );
    callbacks.focusCanvas();
  });
  elements.postDofFocusInput.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessDepthOfField(
      state.postProcessDofEnabled,
      readNumberInput(elements.postDofFocusInput, state.postProcessDofFocusDistance),
      state.postProcessDofFocusRange,
      state.postProcessDofMaxBlurPixels
    );
    callbacks.focusCanvas();
  });
  elements.postDofRangeInput.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessDepthOfField(
      state.postProcessDofEnabled,
      state.postProcessDofFocusDistance,
      readNumberInput(elements.postDofRangeInput, state.postProcessDofFocusRange),
      state.postProcessDofMaxBlurPixels
    );
    callbacks.focusCanvas();
  });
  elements.postDofBlurInput.addEventListener("change", () => {
    const state = callbacks.getPostProcessState();
    callbacks.setPostProcessDepthOfField(
      state.postProcessDofEnabled,
      state.postProcessDofFocusDistance,
      state.postProcessDofFocusRange,
      readNumberInput(elements.postDofBlurInput, state.postProcessDofMaxBlurPixels)
    );
    callbacks.focusCanvas();
  });
  elements.postResetButton.addEventListener("click", () => {
    callbacks.resetPostProcess();
    syncPostProcessControls(elements, callbacks.getPostProcessState());
    callbacks.focusCanvas();
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
  syncPostProcessControls(elements, callbacks.getPostProcessState());
  setPanelVisible(elements, false);
  setPerfOverlayVisible(elements, false);

  return {
    update(stats) {
      syncControls(elements, stats.renderDebugOptions);
      syncPostProcessControls(elements, stats.rendererStatus);
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

/// Synchronizes DOM controls from the latest Rust-owned post-process state.
function syncPostProcessControls(
  elements: RenderDebugUiElements,
  state: PostProcessDebugState
): void {
  elements.postDebugViewSelect.value = state.postProcessDebugView;
  elements.postToneMappingCheckbox.checked = state.postProcessToneMappingEnabled;
  elements.postBloomCheckbox.checked = state.postProcessBloomEnabled;
  elements.postDofCheckbox.checked = state.postProcessDofEnabled;
  setNumberInputValue(elements.postExposureInput, state.postProcessExposure);
  setNumberInputValue(elements.postBloomThresholdInput, state.postProcessBloomThreshold);
  setNumberInputValue(elements.postBloomIntensityInput, state.postProcessBloomIntensity);
  setNumberInputValue(elements.postDofFocusInput, state.postProcessDofFocusDistance);
  setNumberInputValue(elements.postDofRangeInput, state.postProcessDofFocusRange);
  setNumberInputValue(elements.postDofBlurInput, state.postProcessDofMaxBlurPixels);
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

/// Reads a finite numeric input value, falling back to the current Rust state.
function readNumberInput(input: HTMLInputElement, fallback: number): number {
  const value = Number(input.value);
  return Number.isFinite(value) ? value : fallback;
}

/// Updates a number input without interrupting in-progress keyboard edits.
function setNumberInputValue(input: HTMLInputElement, value: number): void {
  if (document.activeElement === input) {
    return;
  }

  input.value = Number.isFinite(value) ? String(value) : "0";
}
