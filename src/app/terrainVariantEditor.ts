// In-browser terrain variant editor UI. It presents Rust-authored flat terrain
// descriptors, forwards edits through the Rust command lane, and never samples
// or generates terrain in TypeScript.

import type {
  GameDebugSnapshot,
  RustBrowserGameCommand,
  TerrainPresetCatalogEntry,
  TerrainVariantFlatValues
} from "../engine/web/browserGameTypes.js";

const TERRAIN_VARIANT_VALUE_COUNT = 32;
const ORIGIN_PREVIEW_CAMERA = {
  x: 24,
  y: 22,
  z: 24,
  yaw: -2.36,
  pitch: -0.46
} as const;

export type TerrainVariantEditorElements = {
  readonly panelToggle: HTMLButtonElement;
  readonly panel: HTMLElement;
  readonly draftSelect: HTMLSelectElement;
  readonly draftNameInput: HTMLInputElement;
  readonly fieldGrid: HTMLElement;
  readonly applyButton: HTMLButtonElement;
  readonly resetButton: HTMLButtonElement;
  readonly duplicateButton: HTMLButtonElement;
  readonly previewOriginButton: HTMLButtonElement;
  readonly exportButton: HTMLButtonElement;
  readonly importButton: HTMLButtonElement;
  readonly jsonText: HTMLTextAreaElement;
  readonly status: HTMLElement;
};

export type TerrainVariantEditor = {
  update(snapshot: GameDebugSnapshot): void;
  togglePanel(): void;
};

export type TerrainVariantEditorCallbacks = {
  command(command: RustBrowserGameCommand): void;
  focusCanvas(): void;
};

type TerrainVariantDraft = {
  readonly sourceId: string;
  name: string;
  presetCode: number;
  terrainVariant: number[];
};

type TerrainVariantFieldDefinition = {
  readonly id: string;
  readonly label: string;
  readonly description: string;
  readonly index: number;
  readonly step: number;
  readonly integer?: boolean;
  readonly min?: number;
  readonly max?: number;
};

const TERRAIN_BASE_HEIGHT_MIN = -4096;
const TERRAIN_BASE_HEIGHT_MAX = 4096;
const TERRAIN_HEIGHT_SCALE_MIN = -2048;
const TERRAIN_HEIGHT_SCALE_MAX = 2048;
const TERRAIN_RIDGE_HEIGHT_SCALE_MAX = 2048;
const TERRAIN_CELLULAR_HEIGHT_SCALE_MAX = 1024;
const TERRAIN_DETAIL_AMPLITUDE_MAX = 512;
const TERRAIN_WARP_AMPLITUDE_MAX = 8192;

const FIELD_DEFINITIONS: readonly TerrainVariantFieldDefinition[] = [
  { id: "baseHeight", label: "Base", description: "Average terrain elevation in meters.", index: 2, step: 0.25, min: TERRAIN_BASE_HEIGHT_MIN, max: TERRAIN_BASE_HEIGHT_MAX },
  { id: "heightScale", label: "Relief", description: "Overall macro height variation.", index: 3, step: 0.25, min: TERRAIN_HEIGHT_SCALE_MIN, max: TERRAIN_HEIGHT_SCALE_MAX },
  { id: "largeOctaves", label: "Large oct", description: "Number of broad terrain noise layers.", index: 4, step: 1, integer: true, min: 1, max: 8 },
  { id: "largeFrequency", label: "Large freq", description: "Frequency of broad terrain undulation.", index: 5, step: 0.0001, min: 0.000001, max: 1 },
  { id: "largeLacunarity", label: "Large lac", description: "Scale multiplier between broad terrain octaves.", index: 6, step: 0.05, min: 1, max: 8 },
  { id: "largePersistence", label: "Large pers", description: "Strength retained by each broad terrain octave.", index: 7, step: 0.01, min: 0, max: 1.5 },
  { id: "ridgeHeight", label: "Ridge", description: "Height contribution from ridge noise.", index: 8, step: 0.25, min: 0, max: TERRAIN_RIDGE_HEIGHT_SCALE_MAX },
  { id: "ridgeOctaves", label: "Ridge oct", description: "Number of ridge noise layers.", index: 9, step: 1, integer: true, min: 1, max: 8 },
  { id: "ridgeFrequency", label: "Ridge freq", description: "Frequency of ridge features.", index: 10, step: 0.0001, min: 0.000001, max: 1 },
  { id: "ridgeLacunarity", label: "Ridge lac", description: "Scale multiplier between ridge octaves.", index: 11, step: 0.05, min: 1, max: 8 },
  { id: "ridgePersistence", label: "Ridge pers", description: "Strength retained by each ridge octave.", index: 12, step: 0.01, min: 0, max: 1.5 },
  { id: "ridgeOffset", label: "Ridge off", description: "Ridge inversion offset before sharpening.", index: 13, step: 0.05, min: 0.000001, max: 4 },
  { id: "ridgeSharpness", label: "Ridge sharp", description: "Ridge contrast and narrowness.", index: 14, step: 0.05, min: 0.1, max: 8 },
  { id: "warpOctaves", label: "Warp oct", description: "Number of domain-warp noise layers.", index: 15, step: 1, integer: true, min: 1, max: 8 },
  { id: "warpFrequency", label: "Warp freq", description: "Frequency of terrain domain warping.", index: 16, step: 0.0001, min: 0.000001, max: 1 },
  { id: "warpLacunarity", label: "Warp lac", description: "Scale multiplier between warp octaves.", index: 17, step: 0.05, min: 1, max: 8 },
  { id: "warpPersistence", label: "Warp pers", description: "Strength retained by each warp octave.", index: 18, step: 0.01, min: 0, max: 1.5 },
  { id: "warpAmplitude", label: "Warp amp", description: "Horizontal distortion applied before sampling terrain.", index: 19, step: 0.25, min: 0, max: TERRAIN_WARP_AMPLITUDE_MAX },
  { id: "cellFrequency", label: "Cell freq", description: "Frequency of cellular basin and edge features.", index: 20, step: 0.0001, min: 0.000001, max: 1 },
  { id: "cellHeight", label: "Cell h", description: "Height contribution from cellular features.", index: 21, step: 0.25, min: 0, max: TERRAIN_CELLULAR_HEIGHT_SCALE_MAX },
  { id: "detailOctaves", label: "Detail oct", description: "Number of small detail noise layers.", index: 22, step: 1, integer: true, min: 1, max: 8 },
  { id: "detailFrequency", label: "Detail freq", description: "Frequency of small surface detail.", index: 23, step: 0.0001, min: 0.000001, max: 1 },
  { id: "detailLacunarity", label: "Detail lac", description: "Scale multiplier between detail octaves.", index: 24, step: 0.05, min: 1, max: 8 },
  { id: "detailPersistence", label: "Detail pers", description: "Strength retained by each detail octave.", index: 25, step: 0.01, min: 0, max: 1.5 },
  { id: "detailAmplitude", label: "Detail amp", description: "Fine surface displacement strength.", index: 26, step: 0.25, min: 0, max: TERRAIN_DETAIL_AMPLITUDE_MAX },
  { id: "matMeadow", label: "Grass", description: "Multiplier for meadow and grass material candidates.", index: 27, step: 0.05, min: 0, max: 4 },
  { id: "matDryGround", label: "Dry", description: "Multiplier for dry soil and badland material candidates.", index: 28, step: 0.05, min: 0, max: 4 },
  { id: "matWetland", label: "Wet", description: "Multiplier for wetland and muddy material candidates.", index: 29, step: 0.05, min: 0, max: 4 },
  { id: "matRock", label: "Rock", description: "Multiplier for rock, scree, and cliff material candidates.", index: 30, step: 0.05, min: 0, max: 4 },
  { id: "matSnow", label: "Snow", description: "Multiplier for snow material candidates.", index: 31, step: 0.05, min: 0, max: 4 }
];

export function createTerrainVariantEditor(
  elements: TerrainVariantEditorElements,
  callbacks: TerrainVariantEditorCallbacks
): TerrainVariantEditor {
  let initialized = false;
  let drafts: TerrainVariantDraft[] = [];
  let selectedDraftIndex = 0;
  let latestSnapshot: GameDebugSnapshot | undefined;

  elements.panelToggle.addEventListener("click", () => {
    togglePanel();
    callbacks.focusCanvas();
  });
  elements.draftSelect.addEventListener("change", () => {
    selectedDraftIndex = Math.max(0, elements.draftSelect.selectedIndex);
    renderDraftFields();
    applySelectedDraft();
  });
  elements.draftNameInput.addEventListener("change", () => {
    const draft = selectedDraft();
    draft.name = elements.draftNameInput.value.trim() || draft.name;
    renderDraftOptions();
  });
  elements.fieldGrid.addEventListener("keydown", (event) => {
    if (!(event.target instanceof HTMLInputElement) || event.key !== "Enter") {
      return;
    }
    event.preventDefault();
    commitFieldInput(event.target);
  });
  elements.fieldGrid.addEventListener("change", (event) => {
    if (event.target instanceof HTMLInputElement) {
      commitFieldInput(event.target);
    }
  });
  elements.applyButton.addEventListener("click", () => {
    applySelectedDraft();
    callbacks.focusCanvas();
  });
  elements.resetButton.addEventListener("click", () => {
    resetSelectedDraftFromCatalog();
    applySelectedDraft();
    callbacks.focusCanvas();
  });
  elements.duplicateButton.addEventListener("click", () => {
    duplicateSelectedDraft();
  });
  elements.previewOriginButton.addEventListener("click", () => {
    previewSelectedDraftAtOrigin();
  });
  elements.exportButton.addEventListener("click", () => {
    elements.jsonText.value = JSON.stringify(exportDrafts(drafts), null, 2);
  });
  elements.importButton.addEventListener("click", () => {
    importDraftsFromJson(elements.jsonText.value);
  });

  function update(snapshot: GameDebugSnapshot): void {
    latestSnapshot = snapshot;
    if (!initialized) {
      initializeDrafts(snapshot);
      initialized = true;
    }
    elements.status.textContent = formatStatus(snapshot);
    elements.status.title = "Terrain revision, stream readiness, rendered nodes, sampled height range, and sampled slope range.";
  }

  function togglePanel(): void {
    elements.panel.hidden = !elements.panel.hidden;
    elements.panelToggle.dataset.active = String(!elements.panel.hidden);
    elements.panelToggle.setAttribute("aria-expanded", String(!elements.panel.hidden));
  }

  function initializeDrafts(snapshot: GameDebugSnapshot): void {
    drafts = snapshot.terrainPresetCatalog.map(catalogEntryToDraft);
    selectedDraftIndex = Math.max(
      0,
      drafts.findIndex((draft) => draft.sourceId === snapshot.terrainPreset)
    );
    if (drafts.length === 0) {
      drafts = [catalogEntryToDraft({
        code: 0,
        id: snapshot.terrainPreset,
        name: snapshot.terrainPreset,
        terrainVariant: snapshot.terrainVariant
      })];
    }
    renderDraftOptions();
    renderDraftFields();
  }

  function renderDraftOptions(): void {
    elements.draftSelect.replaceChildren(
      ...drafts.map((draft) => {
        const option = document.createElement("option");
        option.textContent = draft.name;
        option.value = draft.sourceId;
        return option;
      })
    );
    elements.draftSelect.selectedIndex = selectedDraftIndex;
    elements.draftNameInput.value = selectedDraft().name;
  }

  function renderDraftFields(): void {
    const draft = selectedDraft();
    elements.draftNameInput.value = draft.name;
    elements.fieldGrid.replaceChildren(
      ...FIELD_DEFINITIONS.flatMap((field) => {
        const label = document.createElement("label");
        label.htmlFor = `terrain-variant-${field.id}`;
        label.textContent = field.label;
        label.title = field.description;
        const input = document.createElement("input");
        input.id = `terrain-variant-${field.id}`;
        input.type = "number";
        input.title = field.description;
        input.setAttribute("aria-label", field.description);
        input.step = String(field.step);
        if (field.min !== undefined) {
          input.min = String(field.min);
        }
        if (field.max !== undefined) {
          input.max = String(field.max);
        }
        input.value = formatNumber(draft.terrainVariant[field.index]);
        input.dataset.variantIndex = String(field.index);
        input.dataset.variantField = field.id;
        return [label, input];
      })
    );
  }

  function commitFieldInput(input: HTMLInputElement): void {
    const draft = selectedDraft();
    const index = Number(input.dataset.variantIndex);
    const field = FIELD_DEFINITIONS.find((definition) => definition.index === index);
    if (field === undefined) {
      return;
    }
    const parsed = parseFieldValue(input.value, field);
    draft.terrainVariant[index] = parsed;
    input.value = formatNumber(parsed);
    applySelectedDraft();
  }

  function applySelectedDraft(): void {
    const snapshot = latestSnapshot;
    if (snapshot === undefined) {
      return;
    }
    const draft = selectedDraft();
    validateTerrainVariantValues(draft.terrainVariant);
    callbacks.command({
      type: "setTerrainVariant",
      terrainSeed: snapshot.terrainSeed,
      terrainPreset: draft.presetCode,
      terrainVariant: [...draft.terrainVariant]
    });
  }

  function previewSelectedDraftAtOrigin(): void {
    applySelectedDraft();
    callbacks.command({ type: "setPlayerMode", mode: "debugFly" });
    callbacks.command({ type: "setPlayerPosition", x: 0, z: 0 });
    callbacks.command({ type: "setDebugCamera", ...ORIGIN_PREVIEW_CAMERA });
    callbacks.focusCanvas();
  }

  function resetSelectedDraftFromCatalog(): void {
    const snapshot = latestSnapshot;
    if (snapshot === undefined) {
      return;
    }
    const draft = selectedDraft();
    const catalogEntry = snapshot.terrainPresetCatalog.find((entry) => entry.code === draft.presetCode);
    if (catalogEntry === undefined) {
      return;
    }
    draft.terrainVariant = [...catalogEntry.terrainVariant];
    renderDraftFields();
  }

  function duplicateSelectedDraft(): void {
    const draft = selectedDraft();
    drafts.splice(selectedDraftIndex + 1, 0, {
      sourceId: `${draft.sourceId}-copy-${drafts.length + 1}`,
      name: `${draft.name} Copy`,
      presetCode: draft.presetCode,
      terrainVariant: [...draft.terrainVariant]
    });
    selectedDraftIndex += 1;
    renderDraftOptions();
    renderDraftFields();
  }

  function importDraftsFromJson(text: string): void {
    const imported = importDrafts(text);
    if (imported.length === 0) {
      return;
    }
    drafts = imported;
    selectedDraftIndex = 0;
    renderDraftOptions();
    renderDraftFields();
    applySelectedDraft();
  }

  function selectedDraft(): TerrainVariantDraft {
    return drafts[Math.min(selectedDraftIndex, drafts.length - 1)];
  }

  return {
    update,
    togglePanel
  };
}

export function terrainVariantFieldIndex(fieldId: string): number {
  const field = FIELD_DEFINITIONS.find((definition) => definition.id === fieldId);
  if (field === undefined) {
    throw new Error(`Unknown terrain variant field '${fieldId}'.`);
  }

  return field.index;
}

export function updateTerrainVariantField(
  values: TerrainVariantFlatValues,
  fieldId: string,
  value: number
): number[] {
  const next = [...values];
  const field = FIELD_DEFINITIONS.find((definition) => definition.id === fieldId);
  if (field === undefined) {
    throw new Error(`Unknown terrain variant field '${fieldId}'.`);
  }
  next[field.index] = coerceFieldValue(value, field);
  validateTerrainVariantValues(next);

  return next;
}

function catalogEntryToDraft(entry: TerrainPresetCatalogEntry): TerrainVariantDraft {
  return {
    sourceId: entry.id,
    name: entry.name,
    presetCode: entry.code,
    terrainVariant: [...entry.terrainVariant]
  };
}

function parseFieldValue(value: string, field: TerrainVariantFieldDefinition): number {
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) {
    throw new Error(`Invalid terrain variant value for ${field.id}.`);
  }

  return coerceFieldValue(parsed, field);
}

function coerceFieldValue(value: number, field: TerrainVariantFieldDefinition): number {
  let next = field.integer === true ? Math.round(value) : value;
  if (field.min !== undefined) {
    next = Math.max(field.min, next);
  }
  if (field.max !== undefined) {
    next = Math.min(field.max, next);
  }

  return next;
}

function validateTerrainVariantValues(values: readonly number[]): void {
  if (values.length !== TERRAIN_VARIANT_VALUE_COUNT) {
    throw new Error(`Terrain variant expected ${TERRAIN_VARIANT_VALUE_COUNT} values.`);
  }
  if (values.some((value) => !Number.isFinite(value))) {
    throw new Error("Terrain variant values must be finite.");
  }
}

function formatNumber(value: number): string {
  if (Number.isInteger(value)) {
    return String(value);
  }

  return Number(value.toFixed(6)).toString();
}

function formatStatus(snapshot: GameDebugSnapshot): string {
  const status = snapshot.terrainStreamStatus;
  const probe = snapshot.terrainVariantProbe;
  const stream = status.pending ? "pending" : "ready";
  return `rev ${snapshot.terrainVariantRevision} | ${stream} | ${status.renderedNodeCount}/${status.desiredRenderNodeCount} | h ${probe.heightMin.toFixed(1)}..${probe.heightMax.toFixed(1)} | s ${probe.slopeMin.toFixed(2)}..${probe.slopeMax.toFixed(2)}`;
}

function exportDrafts(drafts: readonly TerrainVariantDraft[]): unknown[] {
  return drafts.map((draft) => ({
    name: draft.name,
    presetCode: draft.presetCode,
    terrainVariant: draft.terrainVariant
  }));
}

function importDrafts(text: string): TerrainVariantDraft[] {
  const parsed = JSON.parse(text) as unknown;
  const items = Array.isArray(parsed) ? parsed : [parsed];

  return items.map((item, index) => {
    if (typeof item !== "object" || item === null) {
      throw new Error("Imported terrain variant draft must be an object.");
    }
    const record = item as {
      readonly name?: unknown;
      readonly presetCode?: unknown;
      readonly terrainVariant?: unknown;
    };
    if (typeof record.name !== "string") {
      throw new Error("Imported terrain variant draft name must be a string.");
    }
    const presetCode = Number(record.presetCode);
    if (!Number.isInteger(presetCode) || presetCode < 0) {
      throw new Error("Imported terrain variant draft presetCode must be a non-negative integer.");
    }
    if (!Array.isArray(record.terrainVariant)) {
      throw new Error("Imported terrain variant draft terrainVariant must be an array.");
    }
    const terrainVariant = record.terrainVariant.map((value) => Number(value));
    validateTerrainVariantValues(terrainVariant);

    return {
      sourceId: `imported-${index + 1}`,
      name: record.name,
      presetCode,
      terrainVariant
    };
  });
}
