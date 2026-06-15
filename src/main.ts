import {
  startGame,
  type GameRenderDebugUiElements,
  type GameTerrainVariantEditorElements,
  type GameTouchControlElements
} from "./app/game.js";

const canvas = document.querySelector<HTMLCanvasElement>("#game-canvas");
const cameraMode = document.querySelector<HTMLElement>("#camera-mode");
const characterToggle = document.querySelector<HTMLButtonElement>("#character-toggle");
const playerCoordinates = document.querySelector<HTMLElement>("#player-coordinates");
const frameTime = document.querySelector<HTMLElement>("#frame-time");
const touchControls = readTouchControls();
const renderDebugUi = readRenderDebugUi();
const terrainVariantEditor = readTerrainVariantEditor();

if (
  canvas === null ||
  cameraMode === null ||
  characterToggle === null ||
  playerCoordinates === null ||
  frameTime === null ||
  touchControls === null ||
  renderDebugUi === null ||
  terrainVariantEditor === null
) {
  throw new Error("OFG could not find its root DOM elements.");
}

startGame({
  canvas,
  cameraMode,
  characterToggle,
  playerCoordinates,
  frameTime,
  touchControls,
  renderDebugUi,
  terrainVariantEditor
}).catch((error: unknown) => {
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

/// Reads the terrain variant editor DOM elements.
function readTerrainVariantEditor(): GameTerrainVariantEditorElements | null {
  const panelToggle = document.querySelector<HTMLButtonElement>("#terrain-variant-panel-toggle");
  const panel = document.querySelector<HTMLElement>("#terrain-variant-panel");
  const draftSelect = document.querySelector<HTMLSelectElement>("#terrain-variant-draft");
  const draftNameInput = document.querySelector<HTMLInputElement>("#terrain-variant-name");
  const fieldGrid = document.querySelector<HTMLElement>("#terrain-variant-fields");
  const applyButton = document.querySelector<HTMLButtonElement>("#terrain-variant-apply");
  const resetButton = document.querySelector<HTMLButtonElement>("#terrain-variant-reset");
  const duplicateButton = document.querySelector<HTMLButtonElement>("#terrain-variant-duplicate");
  const previewOriginButton = document.querySelector<HTMLButtonElement>(
    "#terrain-variant-preview-origin"
  );
  const exportButton = document.querySelector<HTMLButtonElement>("#terrain-variant-export");
  const importButton = document.querySelector<HTMLButtonElement>("#terrain-variant-import");
  const jsonText = document.querySelector<HTMLTextAreaElement>("#terrain-variant-json");
  const status = document.querySelector<HTMLElement>("#terrain-variant-status");

  if (
    panelToggle === null ||
    panel === null ||
    draftSelect === null ||
    draftNameInput === null ||
    fieldGrid === null ||
    applyButton === null ||
    resetButton === null ||
    duplicateButton === null ||
    previewOriginButton === null ||
    exportButton === null ||
    importButton === null ||
    jsonText === null ||
    status === null
  ) {
    return null;
  }

  return {
    panelToggle,
    panel,
    draftSelect,
    draftNameInput,
    fieldGrid,
    applyButton,
    resetButton,
    duplicateButton,
    previewOriginButton,
    exportButton,
    importButton,
    jsonText,
    status
  };
}

/// Reads the render-debug controls and live perf overlay DOM elements.
function readRenderDebugUi(): GameRenderDebugUiElements | null {
  const panelToggle = document.querySelector<HTMLButtonElement>("#render-debug-panel-toggle");
  const perfToggle = document.querySelector<HTMLButtonElement>("#perf-overlay-toggle");
  const panel = document.querySelector<HTMLElement>("#render-debug-panel");
  const terrainLodSelect = document.querySelector<HTMLSelectElement>("#render-debug-terrain-lod");
  const skyCheckbox = document.querySelector<HTMLInputElement>("#render-debug-sky");
  const skyCloudNoiseCheckbox = document.querySelector<HTMLInputElement>(
    "#render-debug-sky-cloud-noise"
  );
  const shadowPassCheckbox = document.querySelector<HTMLInputElement>("#render-debug-shadow-pass");
  const shadowCascadeCheckboxes = Array.from(
    document.querySelectorAll<HTMLInputElement>("[data-shadow-cascade]")
  );
  const shadowSamplingCheckbox = document.querySelector<HTMLInputElement>(
    "#render-debug-shadow-sampling"
  );
  const shadowSunModeSelect = document.querySelector<HTMLSelectElement>("#render-debug-sun");
  const whiteTexturesCheckbox = document.querySelector<HTMLInputElement>(
    "#render-debug-white-textures"
  );
  const materialModeSelect = document.querySelector<HTMLSelectElement>(
    "#render-debug-material"
  );
  const postDebugViewSelect = document.querySelector<HTMLSelectElement>("#post-debug-view");
  const postToneMappingCheckbox = document.querySelector<HTMLInputElement>("#post-tone-mapping");
  const postExposureInput = document.querySelector<HTMLInputElement>("#post-exposure");
  const postBloomCheckbox = document.querySelector<HTMLInputElement>("#post-bloom");
  const postBloomThresholdInput = document.querySelector<HTMLInputElement>(
    "#post-bloom-threshold"
  );
  const postBloomIntensityInput = document.querySelector<HTMLInputElement>(
    "#post-bloom-intensity"
  );
  const postDofCheckbox = document.querySelector<HTMLInputElement>("#post-dof");
  const postDofFocusInput = document.querySelector<HTMLInputElement>("#post-dof-focus");
  const postDofRangeInput = document.querySelector<HTMLInputElement>("#post-dof-range");
  const postDofBlurInput = document.querySelector<HTMLInputElement>("#post-dof-blur");
  const postFogCheckbox = document.querySelector<HTMLInputElement>("#post-fog");
  const postFogStartRange = document.querySelector<HTMLInputElement>("#post-fog-start-range");
  const postFogStartInput = document.querySelector<HTMLInputElement>("#post-fog-start");
  const postFogEndRange = document.querySelector<HTMLInputElement>("#post-fog-end-range");
  const postFogEndInput = document.querySelector<HTMLInputElement>("#post-fog-end");
  const postFogDensityRange = document.querySelector<HTMLInputElement>("#post-fog-density-range");
  const postFogDensityInput = document.querySelector<HTMLInputElement>("#post-fog-density");
  const postFogCurveRange = document.querySelector<HTMLInputElement>("#post-fog-curve-range");
  const postFogCurveInput = document.querySelector<HTMLInputElement>("#post-fog-curve");
  const postFogRInput = document.querySelector<HTMLInputElement>("#post-fog-r");
  const postFogGInput = document.querySelector<HTMLInputElement>("#post-fog-g");
  const postFogBInput = document.querySelector<HTMLInputElement>("#post-fog-b");
  const postResetButton = document.querySelector<HTMLButtonElement>("#post-debug-reset");
  const waterDebugViewSelect = document.querySelector<HTMLSelectElement>("#water-debug-view");
  const waterEnabledCheckbox = document.querySelector<HTMLInputElement>("#water-enabled");
  const waterReflectionCheckbox = document.querySelector<HTMLInputElement>("#water-reflection");
  const waterStatus = document.querySelector<HTMLElement>("#water-debug-status");
  const resetButton = document.querySelector<HTMLButtonElement>("#render-debug-reset");
  const resetPerfButton = document.querySelector<HTMLButtonElement>("#perf-debug-reset");
  const perfOverlay = document.querySelector<HTMLElement>("#perf-overlay");

  if (
    panelToggle === null ||
    perfToggle === null ||
    panel === null ||
    terrainLodSelect === null ||
    skyCheckbox === null ||
    skyCloudNoiseCheckbox === null ||
    shadowPassCheckbox === null ||
    shadowCascadeCheckboxes.length !== 4 ||
    shadowSamplingCheckbox === null ||
    shadowSunModeSelect === null ||
    whiteTexturesCheckbox === null ||
    materialModeSelect === null ||
    postDebugViewSelect === null ||
    postToneMappingCheckbox === null ||
    postExposureInput === null ||
    postBloomCheckbox === null ||
    postBloomThresholdInput === null ||
    postBloomIntensityInput === null ||
    postDofCheckbox === null ||
    postDofFocusInput === null ||
    postDofRangeInput === null ||
    postDofBlurInput === null ||
    postFogCheckbox === null ||
    postFogStartRange === null ||
    postFogStartInput === null ||
    postFogEndRange === null ||
    postFogEndInput === null ||
    postFogDensityRange === null ||
    postFogDensityInput === null ||
    postFogCurveRange === null ||
    postFogCurveInput === null ||
    postFogRInput === null ||
    postFogGInput === null ||
    postFogBInput === null ||
    postResetButton === null ||
    waterDebugViewSelect === null ||
    waterEnabledCheckbox === null ||
    waterReflectionCheckbox === null ||
    waterStatus === null ||
    resetButton === null ||
    resetPerfButton === null ||
    perfOverlay === null
  ) {
    return null;
  }

  return {
    panelToggle,
    perfToggle,
    panel,
    terrainLodSelect,
    skyCheckbox,
    skyCloudNoiseCheckbox,
    shadowPassCheckbox,
    shadowCascadeCheckboxes,
    shadowSamplingCheckbox,
    shadowSunModeSelect,
    whiteTexturesCheckbox,
    materialModeSelect,
    postDebugViewSelect,
    postToneMappingCheckbox,
    postExposureInput,
    postBloomCheckbox,
    postBloomThresholdInput,
    postBloomIntensityInput,
    postDofCheckbox,
    postDofFocusInput,
    postDofRangeInput,
    postDofBlurInput,
    postFogCheckbox,
    postFogStartRange,
    postFogStartInput,
    postFogEndRange,
    postFogEndInput,
    postFogDensityRange,
    postFogDensityInput,
    postFogCurveRange,
    postFogCurveInput,
    postFogRInput,
    postFogGInput,
    postFogBInput,
    postResetButton,
    waterDebugViewSelect,
    waterEnabledCheckbox,
    waterReflectionCheckbox,
    waterStatus,
    resetButton,
    resetPerfButton,
    perfOverlay
  };
}
