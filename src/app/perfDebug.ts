// Browser-shell performance diagnostics for the OFG frame loop.
// Rust-owned timings stay in `debugSnapshot().rustPerfStats`; this module only
// records TypeScript browser work and formats combined debug dumps.

import type {
  RenderDebugOptions,
  RustBrowserGameDebugSnapshot,
  TerrainStreamStatus
} from "../engine/web/browserGameTypes.js";
import type { EngineWebRendererStatus } from "../engine/web/engineWebWasm.js";

export type BrowserCpuFrameSample = {
  readonly totalFrameMs: number;
  readonly inputAndFrameBuildMs: number;
  readonly gameTickMs: number;
  readonly debugSnapshotMs: number;
  readonly hudUpdateMs: number;
};

export type NumericPerfSummary = {
  readonly latest: number;
  readonly min: number;
  readonly max: number;
  readonly average: number;
  readonly p95: number;
};

export type BrowserCpuSummary = {
  readonly totalFrameMs: NumericPerfSummary;
  readonly inputAndFrameBuildMs: NumericPerfSummary;
  readonly gameTickMs: NumericPerfSummary;
  readonly debugSnapshotMs: NumericPerfSummary;
  readonly hudUpdateMs: NumericPerfSummary;
};

export type BrowserPerfSummary = {
  readonly sampleCount: number;
  readonly capacity: number;
  readonly latest?: BrowserCpuFrameSample;
  readonly browserCpu: BrowserCpuSummary;
};

export type CombinedPerfStats = {
  readonly browserCpu: BrowserPerfSummary;
  readonly rustPerfSampleCount: number;
  readonly rustPerfCapacity: number;
  readonly rustCpu: RustBrowserGameDebugSnapshot["rustPerfStats"]["rustCpu"];
  readonly gpu: RustBrowserGameDebugSnapshot["rustPerfStats"]["gpu"] & {
    readonly timerStatus: RustBrowserGameDebugSnapshot["rustPerfStats"]["gpuTimerStatus"];
  };
  readonly rendererCounters: RustBrowserGameDebugSnapshot["rustPerfStats"]["rendererCounters"];
  readonly terrainLodCounters: RustBrowserGameDebugSnapshot["rustPerfStats"]["terrainLodCounters"];
  readonly shadowCascadeCounters:
    RustBrowserGameDebugSnapshot["rustPerfStats"]["shadowCascadeCounters"];
  readonly renderDebugOptions: RenderDebugOptions;
  readonly terrainStreamStatus: TerrainStreamStatus;
  readonly rendererStatus: EngineWebRendererStatus;
  readonly browserTerrainFrame?: RustBrowserGameDebugSnapshot["browserTerrainFrame"];
  readonly latest?: RustBrowserGameDebugSnapshot["rustPerfStats"]["latest"];
};

export class BrowserPerfTracker {
  private readonly samples: BrowserCpuFrameSample[] = [];
  private nextIndex = 0;

  constructor(private readonly capacity = 600) {}

  record(sample: BrowserCpuFrameSample): void {
    if (this.samples.length < this.capacity) {
      this.samples.push(sample);
      this.nextIndex = this.samples.length % this.capacity;
      return;
    }

    this.samples[this.nextIndex] = sample;
    this.nextIndex = (this.nextIndex + 1) % this.capacity;
  }

  reset(): void {
    this.samples.length = 0;
    this.nextIndex = 0;
  }

  summary(): BrowserPerfSummary {
    const samples = this.orderedSamples();

    return {
      sampleCount: samples.length,
      capacity: this.capacity,
      latest: samples.at(-1),
      browserCpu: {
        totalFrameMs: summarize(samples.map((sample) => sample.totalFrameMs)),
        inputAndFrameBuildMs: summarize(samples.map((sample) => sample.inputAndFrameBuildMs)),
        gameTickMs: summarize(samples.map((sample) => sample.gameTickMs)),
        debugSnapshotMs: summarize(samples.map((sample) => sample.debugSnapshotMs)),
        hudUpdateMs: summarize(samples.map((sample) => sample.hudUpdateMs))
      }
    };
  }

  private orderedSamples(): BrowserCpuFrameSample[] {
    if (this.samples.length < this.capacity) {
      return [...this.samples];
    }

    return [
      ...this.samples.slice(this.nextIndex),
      ...this.samples.slice(0, this.nextIndex)
    ];
  }
}

export function buildPerfStats(
  browserCpu: BrowserPerfSummary,
  snapshot: RustBrowserGameDebugSnapshot
): CombinedPerfStats {
  return {
    browserCpu,
    rustPerfSampleCount: snapshot.rustPerfStats.sampleCount,
    rustPerfCapacity: snapshot.rustPerfStats.capacity,
    rustCpu: snapshot.rustPerfStats.rustCpu,
    gpu: {
      ...snapshot.rustPerfStats.gpu,
      timerStatus: snapshot.rustPerfStats.gpuTimerStatus
    },
    rendererCounters: snapshot.rustPerfStats.rendererCounters,
    terrainLodCounters: snapshot.rustPerfStats.terrainLodCounters,
    shadowCascadeCounters: snapshot.rustPerfStats.shadowCascadeCounters,
    renderDebugOptions: snapshot.renderDebugOptions,
    terrainStreamStatus: snapshot.terrainStreamStatus,
    rendererStatus: snapshot.rendererStatus,
    browserTerrainFrame: snapshot.browserTerrainFrame,
    latest: snapshot.rustPerfStats.latest
  };
}

export function dumpPerfStats(
  stats: CombinedPerfStats,
  output: Pick<Console, "log" | "table"> = console
): CombinedPerfStats {
  output.log("OFG perf stats", stats);
  output.table(summaryTable(stats.browserCpu.browserCpu));
  output.table(summaryTable(stats.rustCpu));
  output.table(summaryTable(stats.rendererCounters));
  output.table(gpuSummaryTable(stats.gpu));
  output.table(stats.terrainLodCounters);
  output.table(stats.shadowCascadeCounters);

  return stats;
}

/// Builds compact live-overlay lines from browser and Rust-owned perf stats.
export function buildPerfOverlayLines(stats: CombinedPerfStats): string[] {
  const browserFrame = stats.browserCpu.browserCpu.totalFrameMs;
  const rustFrame = stats.rustCpu.totalFrameMs;
  const rustRender = stats.rustCpu.renderFrameMs;
  const gpuTotal = stats.gpu.totalMeasuredMs;
  const gpuScene = stats.gpu.sceneMs;
  const gpuShadowAverage = stats.gpu.shadowCascadeMs.reduce(
    (sum, summary) => sum + summary.average,
    0
  );
  const counters = stats.rendererCounters;

  return [
    `Frame br ${formatSummary(browserFrame)} | rust ${formatSummary(rustFrame)}`,
    `Render cpu ${formatSummary(rustRender)} | gpu ${formatSummary(gpuTotal)}`,
    `GPU scene avg ${formatMs(gpuScene.average)} | shadow avg ${formatMs(gpuShadowAverage)}`,
    `Draws vis ${formatCount(counters.frameVisibleDrawCount.latest)} ` +
      `cull ${formatCount(counters.frameCulledCount.latest)} ` +
      `shadow ${formatCount(counters.frameShadowDrawCount.latest)}`,
    `Submit v ${formatCount(counters.submittedVertexCount.latest)} ` +
      `i ${formatCount(counters.submittedIndexCount.latest)} ` +
      `tri ${formatCount(counters.submittedTriangleCount.latest)}`,
    `LOD ${formatTerrainLodCounters(stats.terrainLodCounters)}`,
    `Casc ${formatShadowCascadeCounters(stats.shadowCascadeCounters)}`,
    `Post view=${stats.rendererStatus.postProcessDebugView} ` +
      `tone=${formatOnOff(stats.rendererStatus.postProcessToneMappingEnabled)} ` +
      `exp=${round(stats.rendererStatus.postProcessExposure)} ` +
      `bloom=${formatOnOff(stats.rendererStatus.postProcessBloomEnabled)} ` +
      `dof=${formatOnOff(stats.rendererStatus.postProcessDofEnabled)} ` +
      `fog=${formatOnOff(stats.rendererStatus.postProcessFogEnabled)}`,
    `Debug ${formatRenderDebugOptions(stats.renderDebugOptions)}`
  ];
}

/// Builds the live-overlay text block shown in the browser UI.
export function buildPerfOverlayText(stats: CombinedPerfStats): string {
  return buildPerfOverlayLines(stats).join("\n");
}

function summarize(values: readonly number[]): NumericPerfSummary {
  const finite = values.filter((value) => Number.isFinite(value));
  if (finite.length === 0) {
    return { latest: 0, min: 0, max: 0, average: 0, p95: 0 };
  }

  const latest = finite[finite.length - 1] ?? 0;
  const sorted = [...finite].sort((left, right) => left - right);
  const sum = finite.reduce((total, value) => total + value, 0);
  const p95Index = Math.min(sorted.length - 1, Math.ceil(sorted.length * 0.95) - 1);

  return {
    latest,
    min: sorted[0] ?? 0,
    max: sorted[sorted.length - 1] ?? 0,
    average: sum / finite.length,
    p95: sorted[Math.max(0, p95Index)] ?? 0
  };
}

function summaryTable(
  summary: Record<string, NumericPerfSummary>
): Array<Record<string, string | number>> {
  return Object.entries(summary).map(([name, values]) => ({
    metric: name,
    latest: round(values.latest),
    min: round(values.min),
    max: round(values.max),
    average: round(values.average),
    p95: round(values.p95)
  }));
}

function gpuSummaryTable(
  gpu: CombinedPerfStats["gpu"]
): Array<Record<string, string | number | boolean>> {
  return [
    {
      metric: "timerAvailable",
      latest: gpu.timerStatus.available,
      min: "",
      max: "",
      average: "",
      p95: ""
    },
    ...summaryTable({
      sceneMs: gpu.sceneMs,
      bloomMs: gpu.bloomMs,
      postProcessMs: gpu.postProcessMs,
      totalMeasuredMs: gpu.totalMeasuredMs
    }),
    ...gpu.shadowCascadeMs.map((summary, index) => ({
      metric: `shadowCascade${index}Ms`,
      latest: round(summary.latest),
      min: round(summary.min),
      max: round(summary.max),
      average: round(summary.average),
      p95: round(summary.p95)
    }))
  ];
}

function round(value: number): number {
  return Number.isFinite(value) ? Number(value.toFixed(3)) : 0;
}

/// Formats a timing summary as latest/average/min/max/p95 milliseconds.
function formatSummary(summary: NumericPerfSummary): string {
  return `${formatMs(summary.latest)} avg ${formatMs(summary.average)} ` +
    `min ${formatMs(summary.min)} max ${formatMs(summary.max)} p95 ${formatMs(summary.p95)}`;
}

/// Formats a millisecond value for dense debug display.
function formatMs(value: number): string {
  return `${round(value).toFixed(3)}ms`;
}

/// Formats a count without hiding large terrain vertex magnitudes.
function formatCount(value: number): string {
  if (!Number.isFinite(value)) {
    return "0";
  }
  if (Math.abs(value) >= 1_000_000) {
    return `${round(value / 1_000_000)}m`;
  }
  if (Math.abs(value) >= 1_000) {
    return `${round(value / 1_000)}k`;
  }
  return `${round(value)}`;
}

/// Formats the latest Rust-owned per-LOD terrain counters.
function formatTerrainLodCounters(
  counters: CombinedPerfStats["terrainLodCounters"]
): string {
  if (counters.length === 0) {
    return "none";
  }

  return counters
    .map((counter) =>
      `${counter.lod}:d${counter.drawCount}/v${formatCount(counter.vertexCount)}` +
      `/t${formatCount(counter.triangleCount)}`
    )
    .join(" ");
}

/// Formats the latest Rust-owned per-cascade shadow counters.
function formatShadowCascadeCounters(
  counters: CombinedPerfStats["shadowCascadeCounters"]
): string {
  if (counters.length === 0) {
    return "none";
  }

  return counters
    .map((counter) =>
      `${counter.cascadeIndex}:${counter.enabled ? "on" : "off"}` +
      `/d${counter.drawCount}/c${counter.culledCount}` +
      `/v${formatCount(counter.vertexCount)}`
    )
    .join(" ");
}

/// Formats the active Rust-owned render debug state for the overlay.
function formatRenderDebugOptions(options: RenderDebugOptions): string {
  return `lod=${formatMask(options.terrainLodMask, 8)} ` +
    `sky=${formatOnOff(options.skyEnabled)} ` +
    `cloud=${formatOnOff(options.skyCloudNoiseEnabled)} ` +
    `shPass=${formatOnOff(options.shadowPassEnabled)} ` +
    `shSamp=${formatOnOff(options.shadowSamplingEnabled)} ` +
    `casc=${formatMask(options.shadowCascadeMask, 4)} ` +
    `sun=${options.shadowSunMode} ` +
    `tex=${options.whiteTexturesEnabled ? "white" : "full"} ` +
    `mat=${options.materialMode}`;
}

/// Formats a low-bit mask for dense debug display.
function formatMask(mask: number, width: number): string {
  return (mask >>> 0).toString(2).padStart(width, "0").slice(-width);
}

/// Formats a boolean option as a compact on/off token.
function formatOnOff(value: boolean): string {
  return value ? "on" : "off";
}
