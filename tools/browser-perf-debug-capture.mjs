// Deterministic browser capture for OFG render performance diagnostics.
// It records baseline and one-toggle-at-a-time perf summaries under artifacts/perf-debug/.

import { spawn } from "node:child_process";
import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_PERF_PORT ?? "5175", 10);
const headed = process.env.OFG_PERF_HEADED === "1";
const skipBuild = process.env.OFG_PERF_SKIP_BUILD === "1";
const warmupFrames = Number.parseInt(process.env.OFG_PERF_WARMUP_FRAMES ?? "30", 10);
const sampleFrames = Number.parseInt(process.env.OFG_PERF_SAMPLE_FRAMES ?? "120", 10);
const artifactRoot = resolve(root, "artifacts", "perf-debug");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);
const minMultiKmTerrainSpanMeters = 4096;

const experiments = [
  {
    id: "baseline",
    label: "Production defaults",
    options: {}
  },
  {
    id: "sky-off",
    label: "Sky disabled",
    options: { skyEnabled: false }
  },
  {
    id: "shadow-pass-off",
    label: "Shadow-map passes disabled",
    options: { shadowPassEnabled: false }
  },
  {
    id: "shadow-sampling-off",
    label: "Shadow map sampling disabled",
    options: { shadowSamplingEnabled: false }
  },
  {
    id: "sun-overhead",
    label: "Forced overhead sun",
    options: { shadowSunMode: "overhead" }
  },
  {
    id: "sun-angled",
    label: "Forced angled sun",
    options: { shadowSunMode: "angled" }
  },
  {
    id: "sun-low",
    label: "Forced low sun fade",
    options: { shadowSunMode: "low" }
  },
  {
    id: "shadow-cascade-0",
    label: "Only shadow cascade 0 rendered",
    options: { shadowCascadeMask: 0b0001 }
  },
  {
    id: "shadow-cascade-1",
    label: "Only shadow cascade 1 rendered",
    options: { shadowCascadeMask: 0b0010 }
  },
  {
    id: "shadow-cascade-2",
    label: "Only shadow cascade 2 rendered",
    options: { shadowCascadeMask: 0b0100 }
  },
  {
    id: "shadow-cascade-3",
    label: "Only shadow cascade 3 rendered",
    options: { shadowCascadeMask: 0b1000 }
  },
  {
    id: "white-textures",
    label: "Texture sampling replaced with white diagnostics",
    options: { whiteTexturesEnabled: true }
  },
  {
    id: "lambert-material",
    label: "Basic Lambert material mode",
    options: { materialMode: "lambert" }
  },
  {
    id: "terrain-lod-0",
    label: "Only terrain LOD 0 rendered",
    options: { terrainLodMask: 0b000001 }
  },
  {
    id: "terrain-lod-1",
    label: "Only terrain LOD 1 rendered",
    options: { terrainLodMask: 0b000010 }
  },
  {
    id: "terrain-lod-2",
    label: "Only terrain LOD 2 rendered",
    options: { terrainLodMask: 0b000100 }
  },
  {
    id: "terrain-lod-3-plus",
    label: "Only terrain LOD 3+ rendered",
    options: { terrainLodMask: 0xFFFFFFF8 }
  },
  {
    id: "terrain-lod-0-shadow-off",
    label: "Only terrain LOD 0 rendered, shadows disabled",
    options: { terrainLodMask: 0b000001, shadowPassEnabled: false }
  },
  {
    id: "terrain-lod-1-shadow-off",
    label: "Only terrain LOD 1 rendered, shadows disabled",
    options: { terrainLodMask: 0b000010, shadowPassEnabled: false }
  },
  {
    id: "terrain-lod-2-shadow-off",
    label: "Only terrain LOD 2 rendered, shadows disabled",
    options: { terrainLodMask: 0b000100, shadowPassEnabled: false }
  },
  {
    id: "terrain-lod-3-plus-shadow-off",
    label: "Only terrain LOD 3+ rendered, shadows disabled",
    options: { terrainLodMask: 0xFFFFFFF8, shadowPassEnabled: false }
  }
];

mkdirSync(artifactDir, { recursive: true });

if (!skipBuild) {
  const buildCommand = npmBuildCommand();
  await runCommand(buildCommand.command, buildCommand.args);
}

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  const url = `http://127.0.0.1:${port}/?terrainSeed=24681357&terrainPreset=rockyHighland`;
  await waitForHttp(url);
  const capture = await runPerfCapture(url);
  const summary = summarizeCapture(capture);
  const summaryText = formatSummaryText(summary);

  writeFileSync(resolve(artifactDir, "samples.json"), `${JSON.stringify(capture, null, 2)}\n`);
  writeFileSync(resolve(artifactDir, "summary.json"), `${JSON.stringify(summary, null, 2)}\n`);
  writeFileSync(resolve(artifactDir, "summary.txt"), `${summaryText}\n`);

  console.log("Browser perf debug capture complete.");
  console.log(`Artifacts: ${reportPath(artifactDir)}`);
  console.log(summaryText);
} finally {
  server.kill();
}

/// Runs the browser capture scenario and returns raw samples.
async function runPerfCapture(url) {
  const browserPath = findBrowserPath();
  const browser = await chromium.launch({
    executablePath: browserPath,
    headless: !headed,
    args: [
      "--enable-unsafe-webgpu",
      "--ignore-gpu-blocklist",
      "--disable-gpu-sandbox"
    ]
  });
  const consoleMessages = [];

  try {
    const page = await browser.newPage({
      viewport: { width: 1280, height: 720 },
      deviceScaleFactor: 1
    });
    page.on("console", (message) => {
      consoleMessages.push(`${message.type()}: ${message.text()}`);
    });
    page.on("pageerror", (error) => {
      consoleMessages.push(`pageerror: ${error.message}`);
    });

    const response = await page.goto(url, { waitUntil: "load" });
    assertResponseHeaders(response);
    await waitForBrowserFrame(page);
    await waitForTerrainLodFrame(page);
    assertNoBrowserFailures(consoleMessages);

    const captures = [];
    for (const experiment of experiments) {
      captures.push(await captureExperiment(page, experiment, consoleMessages));
    }

    await page.evaluate(() => {
      window.__ofgDebug?.resetRenderDebugOptions?.();
    });

    return {
      kind: "browser-perf-debug-capture",
      capturedAt: new Date().toISOString(),
      url,
      artifactDir: reportPath(artifactDir),
      browserPath,
      headed,
      warmupFrames,
      sampleFrames,
      captures,
      consoleMessages
    };
  } finally {
    await browser.close();
  }
}

/// Captures one experiment after applying one diagnostic render option set.
async function captureExperiment(page, experiment, consoleMessages) {
  await page.evaluate(() => {
    window.__ofgDebug?.resetRenderDebugOptions?.();
    window.__ofgDebug?.resetPerfStats?.();
  });
  if (Object.keys(experiment.options).length > 0) {
    await page.evaluate((options) => {
      window.__ofgDebug?.setRenderDebugOptions?.(options);
    }, experiment.options);
  }

  await waitForFrames(page, warmupFrames);
  await page.evaluate(() => {
    window.__ofgDebug?.resetPerfStats?.();
  });
  await waitForFrames(page, sampleFrames);
  await waitForBrowserFrame(page);
  assertNoBrowserFailures(consoleMessages);

  return page.evaluate((selectedExperiment) => {
    const debug = window.__ofgDebug;
    const perfStats = debug?.getPerfStats?.();
    return {
      id: selectedExperiment.id,
      label: selectedExperiment.label,
      requestedOptions: selectedExperiment.options,
      capturedAt: new Date().toISOString(),
      perfStats,
      renderDebugOptions: debug?.getRenderDebugOptions?.(),
      rendererStatus: debug?.getRendererStatus?.(),
      terrainStreamStatus: debug?.getTerrainStreamStatus?.()
    };
  }, experiment);
}

/// Builds a compact report from raw experiment captures.
function summarizeCapture(capture) {
  const rows = capture.captures.map((sample) => summarizeExperiment(sample));
  const baseline = rows.find((row) => row.id === "baseline");
  const rowsWithDelta = rows.map((row) => ({
    ...row,
    deltaVsBaseline: baseline === undefined ? undefined : {
      browserCpuAverageMs: delta(row.browserCpuAverageMs, baseline.browserCpuAverageMs),
      rustCpuAverageMs: delta(row.rustCpuAverageMs, baseline.rustCpuAverageMs),
      gpuTotalAverageMs: delta(row.gpuTotalAverageMs, baseline.gpuTotalAverageMs),
      gpuSceneAverageMs: delta(row.gpuSceneAverageMs, baseline.gpuSceneAverageMs),
      visibleDrawAverage: delta(row.visibleDrawAverage, baseline.visibleDrawAverage),
      shadowDrawAverage: delta(row.shadowDrawAverage, baseline.shadowDrawAverage),
      submittedVertexAverage: delta(
        row.submittedVertexAverage,
        baseline.submittedVertexAverage
      ),
      submittedIndexAverage: delta(row.submittedIndexAverage, baseline.submittedIndexAverage),
      submittedTriangleAverage: delta(
        row.submittedTriangleAverage,
        baseline.submittedTriangleAverage
      )
    }
  }));
  const terrainLodAnalysis = analyzeTerrainLodCosts(rowsWithDelta);

  return {
    kind: "browser-perf-debug-summary",
    capturedAt: capture.capturedAt,
    artifactDir: capture.artifactDir,
    url: capture.url,
    browserPath: capture.browserPath,
    headed: capture.headed,
    warmupFrames: capture.warmupFrames,
    sampleFrames: capture.sampleFrames,
    gpuTimerStatus: baseline?.gpuTimerStatus,
    terrainLodAnalysis,
    experiments: rowsWithDelta
  };
}

/// Summarizes the sections most useful for a first diagnosis.
function summarizeExperiment(sample) {
  const stats = sample.perfStats;
  const browserCpu = stats?.browserCpu?.browserCpu;
  const rustCpu = stats?.rustCpu;
  const gpu = stats?.gpu;
  const counters = stats?.rendererCounters;
  const renderer = sample.rendererStatus;

  return {
    id: sample.id,
    label: sample.label,
    requestedOptions: sample.requestedOptions,
    activeOptions: sample.renderDebugOptions,
    browserSampleCount: stats?.browserCpu?.sampleCount ?? 0,
    rustSampleCount: stats?.rustPerfSampleCount ?? stats?.latest?.frameIndex ?? 0,
    gpuTimerStatus: gpu?.timerStatus,
    browserCpuAverageMs: round(summaryAverage(browserCpu?.totalFrameMs)),
    browserCpuP95Ms: round(summaryP95(browserCpu?.totalFrameMs)),
    rustCpuAverageMs: round(summaryAverage(rustCpu?.totalFrameMs)),
    rustCpuP95Ms: round(summaryP95(rustCpu?.totalFrameMs)),
    rustRenderFrameAverageMs: round(summaryAverage(rustCpu?.renderFrameMs)),
    rustRenderPacketBuildAverageMs: round(summaryAverage(rustCpu?.renderPacketBuildMs)),
    rustRendererPrepareAverageMs: round(summaryAverage(rustCpu?.rendererPrepareMs)),
    rustRendererShadowAverageMs: round(summaryAverage(rustCpu?.rendererShadowCpuMs)),
    rustRendererSceneAverageMs: round(summaryAverage(rustCpu?.rendererSceneCpuMs)),
    rustRendererPostAverageMs: round(summaryAverage(rustCpu?.rendererPostCpuMs)),
    rustRendererSubmitAverageMs: round(summaryAverage(rustCpu?.rendererSubmitMs)),
    gpuTotalAverageMs: round(summaryAverage(gpu?.totalMeasuredMs)),
    gpuSceneAverageMs: round(summaryAverage(gpu?.sceneMs)),
    gpuBloomAverageMs: round(summaryAverage(gpu?.bloomMs)),
    gpuPostProcessAverageMs: round(summaryAverage(gpu?.postProcessMs)),
    visibleDrawAverage: round(summaryAverage(counters?.frameVisibleDrawCount)),
    culledDrawAverage: round(summaryAverage(counters?.frameCulledCount)),
    shadowDrawAverage: round(summaryAverage(counters?.frameShadowDrawCount)),
    terrainDrawAverage: round(summaryAverage(counters?.terrainDrawCount)),
    modelDrawAverage: round(summaryAverage(counters?.modelDrawCount)),
    skyDrawAverage: round(summaryAverage(counters?.skyDrawCount)),
    postProcessDrawAverage: round(summaryAverage(counters?.postProcessDrawCount)),
    submittedVertexAverage: round(summaryAverage(counters?.submittedVertexCount)),
    submittedIndexAverage: round(summaryAverage(counters?.submittedIndexCount)),
    submittedTriangleAverage: round(summaryAverage(counters?.submittedTriangleCount)),
    terrainLodCounters: stats?.terrainLodCounters ?? [],
    terrainLodBreakdown: summarizeTerrainLodCounters(stats?.terrainLodCounters ?? []),
    dominantTerrainLodByVertices: dominantLod(stats?.terrainLodCounters ?? [], "vertexCount"),
    dominantTerrainLodByTriangles: dominantLod(stats?.terrainLodCounters ?? [], "triangleCount"),
    shadowCascadeCounters: stats?.shadowCascadeCounters ?? [],
    shadowCascadeBreakdown: summarizeShadowCascadeCounters(stats?.shadowCascadeCounters ?? []),
    shadowMaxDistanceMeters: renderer?.shadowMaxDistanceMeters,
    shadowStrength: renderer?.shadowStrength,
    shadowEffectiveSunElevation: renderer?.shadowEffectiveSunElevation,
    shadowEffectiveSunDirection: renderer?.shadowEffectiveSunDirection,
    terrainStreamStatus: sample.terrainStreamStatus,
    rendererStatus: {
      frameIndex: renderer?.frameIndex,
      meshCount: renderer?.meshCount,
      textureCount: renderer?.textureCount,
      objectCount: renderer?.objectCount,
      frameDrawCount: renderer?.frameDrawCount,
      frameVisibleDrawCount: renderer?.frameVisibleDrawCount,
      frameShadowDrawCount: renderer?.frameShadowDrawCount,
      frameCulledDrawCount: renderer?.frameCulledDrawCount,
      frameSubmittedVertexCount: renderer?.frameSubmittedVertexCount,
      frameSubmittedIndexCount: renderer?.frameSubmittedIndexCount,
      frameSubmittedTriangleCount: renderer?.frameSubmittedTriangleCount,
      shadowMaxDistanceMeters: renderer?.shadowMaxDistanceMeters,
      shadowStrength: renderer?.shadowStrength,
      shadowEffectiveSunElevation: renderer?.shadowEffectiveSunElevation,
      shadowEffectiveSunDirection: renderer?.shadowEffectiveSunDirection
    }
  };
}

/// Formats a short text summary suitable for quick terminal inspection.
function formatSummaryText(summary) {
  const lines = [
    `Browser perf debug capture (${summary.sampleFrames} sampled frames per experiment)`,
    `GPU timers: ${summary.gpuTimerStatus?.available ? "available" : "unavailable"} ` +
      `(${summary.gpuTimerStatus?.unavailableReason ?? "no reason"})`,
    ""
  ];

  for (const experiment of summary.experiments) {
    const deltaSuffix = experiment.deltaVsBaseline === undefined
      ? ""
      : `, gpuDelta=${formatDelta(experiment.deltaVsBaseline.gpuTotalAverageMs)}ms` +
        `, shadowDrawDelta=${formatDelta(experiment.deltaVsBaseline.shadowDrawAverage)}`;
    lines.push(
      `${experiment.id}: browser=${experiment.browserCpuAverageMs}ms, ` +
      `rust=${experiment.rustCpuAverageMs}ms, gpu=${experiment.gpuTotalAverageMs}ms, ` +
      `visibleDraws=${experiment.visibleDrawAverage}, ` +
      `shadowDraws=${experiment.shadowDrawAverage}, ` +
      `shadowStrength=${experiment.shadowStrength}, ` +
      `vertices=${experiment.submittedVertexAverage}${deltaSuffix}`
    );
  }
  const analysis = summary.terrainLodAnalysis;
  lines.push("");
  lines.push("Baseline terrain LOD breakdown:");
  for (const lod of analysis.baselineTerrainLodBreakdown) {
    lines.push(
      `lod${lod.lod}: draws=${lod.drawCount}, vertices=${lod.vertexCount} ` +
      `(${lod.vertexSharePercent}%), triangles=${lod.triangleCount} ` +
      `(${lod.triangleSharePercent}%)`
    );
  }
  lines.push(
    `Dominant terrain LOD by vertices: ${formatDominantLod(analysis.baselineDominantByVertices)}`
  );
  lines.push("LOD mask render cost:");
  for (const lodExperiment of analysis.lodMaskExperiments) {
    lines.push(formatLodExperiment(lodExperiment));
  }
  lines.push("LOD mask scene-only cost with shadows disabled:");
  for (const lodExperiment of analysis.sceneOnlyLodMaskExperiments) {
    lines.push(formatLodExperiment(lodExperiment));
  }

  return lines.join("\n");
}

/// Waits until the browser shell has rendered a Rust/wgpu frame.
async function waitForBrowserFrame(page) {
  await page.waitForSelector("#camera-mode");
  await page.waitForFunction(() => {
    const frameTime = document.querySelector("#frame-time")?.textContent;
    const status = window.__ofgDebug?.getRendererStatus?.();
    return window.__ofgDebug !== undefined &&
      frameTime !== undefined &&
      frameTime !== "0.0 ms" &&
      status !== undefined &&
      status.configured === true &&
      status.frameDrawCount > 0 &&
      status.frameVisibleDrawCount > 0;
  }, null, { timeout: 20000 });
}

/// Waits until Rust terrain streaming exposes a settled mixed-LOD frame.
async function waitForTerrainLodFrame(page) {
  await page.waitForFunction((minSpanMeters) => {
    const debug = window.__ofgDebug;
    const status = debug?.getTerrainStreamStatus?.();
    const terrainNodeKeys = debug?.getTerrainNodeKeys?.() ?? [];
    return status !== undefined &&
      status.pending === false &&
      status.renderedChunkCount > 0 &&
      status.renderedNodeCount > status.renderedChunkCount &&
      status.maxRenderedLod >= 3 &&
      status.visibleWorldSpanXMeters >= minSpanMeters &&
      status.visibleWorldSpanZMeters >= minSpanMeters &&
      terrainNodeKeys.some((key) => key.startsWith("lod0:")) &&
      terrainNodeKeys.some((key) => key.startsWith("lod3:") || key.startsWith("lod4:"));
  }, minMultiKmTerrainSpanMeters, { timeout: 120000 });
}

/// Waits for a fixed count of browser animation frames.
async function waitForFrames(page, frameCount) {
  await page.evaluate((frames) => new Promise((resolveFrames) => {
    let remaining = frames;
    function onFrame() {
      remaining -= 1;
      if (remaining <= 0) {
        resolveFrames(undefined);
        return;
      }
      requestAnimationFrame(onFrame);
    }
    requestAnimationFrame(onFrame);
  }), frameCount);
}

/// Validates dev-server headers required for wasm threads and WebGPU assets.
function assertResponseHeaders(response) {
  if (response === null) {
    throw new Error("Browser perf capture did not receive a page response.");
  }

  const headers = response.headers();
  if (headers["cross-origin-opener-policy"] !== "same-origin") {
    throw new Error(`Missing COOP header: ${JSON.stringify(headers)}`);
  }
  if (headers["cross-origin-embedder-policy"] !== "require-corp") {
    throw new Error(`Missing COEP header: ${JSON.stringify(headers)}`);
  }
}

/// Fails if the browser reported runtime errors.
function assertNoBrowserFailures(consoleMessages) {
  const failures = consoleMessages.filter((message) =>
    message.startsWith("pageerror:") ||
    message.startsWith("error:") ||
    message.includes("Rust engine rejected")
  );
  if (failures.length > 0) {
    throw new Error(`Browser reported runtime failures: ${JSON.stringify(failures)}`);
  }
}

/// Runs a command and streams output to the current process.
function runCommand(command, args) {
  return new Promise((resolveCommand, rejectCommand) => {
    const child = spawn(command, args, {
      cwd: root,
      stdio: "inherit"
    });
    child.on("exit", (code) => {
      if (code === 0) {
        resolveCommand();
        return;
      }
      rejectCommand(new Error(`${command} ${args.join(" ")} exited with code ${code}`));
    });
    child.on("error", rejectCommand);
  });
}

/// Starts the local static dev server.
function startDevServer(port) {
  const child = spawn(process.execPath, ["tools/dev-server.mjs"], {
    cwd: root,
    env: { ...process.env, PORT: String(port) },
    stdio: ["ignore", "pipe", "pipe"]
  });

  child.stdout.on("data", (chunk) => process.stdout.write(`[dev-server] ${chunk}`));
  child.stderr.on("data", (chunk) => process.stderr.write(`[dev-server] ${chunk}`));
  child.on("exit", (code) => {
    if (code !== null && code !== 0) {
      console.error(`Dev server exited with code ${code}`);
    }
  });

  return child;
}

/// Waits for the local dev server to answer HTTP requests.
async function waitForHttp(url) {
  const deadline = Date.now() + 10000;
  let lastError;

  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) {
        return;
      }
    } catch (error) {
      lastError = error;
    }

    await sleep(100);
  }

  throw new Error(`Timed out waiting for ${url}: ${lastError}`);
}

/// Finds an available local port starting at the preferred port.
async function findAvailablePort(start) {
  for (let port = start; port < start + 100; port += 1) {
    if (await canListen(port)) {
      return port;
    }
  }

  throw new Error(`No available port found starting at ${start}.`);
}

/// Returns whether a local TCP port can be bound.
function canListen(port) {
  return new Promise((resolveCanListen) => {
    const server = createServer();
    server.once("error", () => resolveCanListen(false));
    server.once("listening", () => {
      server.close(() => resolveCanListen(true));
    });
    server.listen(port, "127.0.0.1");
  });
}

/// Finds an installed Chromium-based browser executable.
function findBrowserPath() {
  const candidates = [
    process.env.OFG_BROWSER_PATH,
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
  ].filter(Boolean);

  const match = candidates.find((candidate) => existsSync(candidate));
  if (match === undefined) {
    throw new Error(
      "Could not find Chrome or Edge. Set OFG_BROWSER_PATH to a Chromium-based browser executable."
    );
  }

  return match;
}

/// Normalizes a path for JSON reports and console output.
function reportPath(path) {
  return path.replace(/\\/g, "/");
}

/// Resolves after the requested delay.
function sleep(ms) {
  return new Promise((resolveSleep) => setTimeout(resolveSleep, ms));
}

/// Returns an npm build invocation that works from direct Node execution.
function npmBuildCommand() {
  if (process.platform === "win32") {
    return { command: "cmd.exe", args: ["/d", "/s", "/c", "npm run build"] };
  }

  return { command: "npm", args: ["run", "build"] };
}

function summaryAverage(summary) {
  return Number.isFinite(summary?.average) ? summary.average : 0;
}

function summaryP95(summary) {
  return Number.isFinite(summary?.p95) ? summary.p95 : 0;
}

function delta(value, baseline) {
  if (!Number.isFinite(value) || !Number.isFinite(baseline)) {
    return 0;
  }
  return round(value - baseline);
}

function round(value) {
  return Math.round(value * 1000) / 1000;
}

function formatDelta(value) {
  if (!Number.isFinite(value)) {
    return "0";
  }
  return value >= 0 ? `+${value}` : `${value}`;
}

/// Builds terrain LOD analysis from summarized experiment rows.
function analyzeTerrainLodCosts(rows) {
  const baseline = rows.find((row) => row.id === "baseline");
  const lodMaskExperiments = rows
    .filter((row) => row.id.startsWith("terrain-lod-") && !row.id.endsWith("-shadow-off"))
    .map(projectLodCostRow);
  const sceneOnlyLodMaskExperiments = rows
    .filter((row) => row.id.startsWith("terrain-lod-") && row.id.endsWith("-shadow-off"))
    .map(projectLodCostRow);

  return {
    baselineTerrainLodBreakdown: baseline?.terrainLodBreakdown ?? [],
    baselineDominantByVertices: baseline?.dominantTerrainLodByVertices,
    baselineDominantByTriangles: baseline?.dominantTerrainLodByTriangles,
    lodMaskExperiments,
    sceneOnlyLodMaskExperiments
  };
}

/// Returns a compact row for comparing terrain LOD mask experiment costs.
function projectLodCostRow(row) {
  return {
    id: row.id,
    label: row.label,
    gpuTotalAverageMs: row.gpuTotalAverageMs,
    gpuSceneAverageMs: row.gpuSceneAverageMs,
    rustRenderFrameAverageMs: row.rustRenderFrameAverageMs,
    visibleDrawAverage: row.visibleDrawAverage,
    shadowDrawAverage: row.shadowDrawAverage,
    submittedVertexAverage: row.submittedVertexAverage,
    submittedTriangleAverage: row.submittedTriangleAverage,
    dominantTerrainLodByVertices: row.dominantTerrainLodByVertices,
    terrainLodBreakdown: row.terrainLodBreakdown,
    deltaVsBaseline: row.deltaVsBaseline
  };
}

/// Summarizes per-LOD terrain counters with share percentages.
function summarizeTerrainLodCounters(counters) {
  const totalVertices = counters.reduce((sum, counter) => sum + counter.vertexCount, 0);
  const totalTriangles = counters.reduce((sum, counter) => sum + counter.triangleCount, 0);

  return counters.map((counter) => ({
    ...counter,
    vertexSharePercent: percent(counter.vertexCount, totalVertices),
    triangleSharePercent: percent(counter.triangleCount, totalTriangles)
  }));
}

/// Summarizes per-cascade counters with share percentages.
function summarizeShadowCascadeCounters(counters) {
  const totalVertices = counters.reduce((sum, counter) => sum + counter.vertexCount, 0);
  const totalTriangles = counters.reduce((sum, counter) => sum + counter.triangleCount, 0);

  return counters.map((counter) => ({
    ...counter,
    vertexSharePercent: percent(counter.vertexCount, totalVertices),
    triangleSharePercent: percent(counter.triangleCount, totalTriangles)
  }));
}

/// Finds the terrain LOD with the largest selected counter.
function dominantLod(counters, key) {
  if (counters.length === 0) {
    return undefined;
  }

  const dominant = counters.reduce((best, counter) =>
    counter[key] > best[key] ? counter : best
  );
  return {
    lod: dominant.lod,
    drawCount: dominant.drawCount,
    vertexCount: dominant.vertexCount,
    triangleCount: dominant.triangleCount
  };
}

/// Formats a terrain LOD experiment for terminal summaries.
function formatLodExperiment(experiment) {
  const delta = experiment.deltaVsBaseline;
  return `${experiment.id}: gpu=${experiment.gpuTotalAverageMs}ms ` +
    `scene=${experiment.gpuSceneAverageMs}ms ` +
    `draws=${experiment.visibleDrawAverage} shadows=${experiment.shadowDrawAverage} ` +
    `vertices=${experiment.submittedVertexAverage} triangles=${experiment.submittedTriangleAverage}` +
    (delta === undefined
      ? ""
      : `, gpuDelta=${formatDelta(delta.gpuTotalAverageMs)}ms` +
        `, vertexDelta=${formatDelta(delta.submittedVertexAverage)}`);
}

/// Formats a dominant LOD object for terminal summaries.
function formatDominantLod(lod) {
  if (lod === undefined) {
    return "none";
  }

  return `lod${lod.lod} vertices=${lod.vertexCount} triangles=${lod.triangleCount}`;
}

/// Returns a rounded percentage while handling empty totals.
function percent(value, total) {
  if (!Number.isFinite(value) || !Number.isFinite(total) || total <= 0) {
    return 0;
  }

  return round((value / total) * 100);
}
