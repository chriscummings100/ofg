// Movement-focused browser capture for terrain streaming CPU spikes.
// It drives real player movement and writes per-frame terrain streaming metrics.

import { spawn } from "node:child_process";
import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_STREAM_CPU_PORT ?? "5176", 10);
const headed = process.env.OFG_STREAM_CPU_HEADED === "1";
const skipBuild = process.env.OFG_STREAM_CPU_SKIP_BUILD === "1";
const warmupFrames = Number.parseInt(process.env.OFG_STREAM_CPU_WARMUP_FRAMES ?? "30", 10);
const sampleFrames = Number.parseInt(process.env.OFG_STREAM_CPU_SAMPLE_FRAMES ?? "540", 10);
const artifactRoot = resolve(root, "artifacts", "terrain-stream-cpu");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);
const minMultiKmTerrainSpanMeters = 4096;

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
  const capture = await runTerrainStreamCpuCapture(url);
  const summary = summarizeCapture(capture);
  const summaryText = formatSummaryText(summary);

  writeFileSync(resolve(artifactDir, "samples.json"), `${JSON.stringify(capture, null, 2)}\n`);
  writeFileSync(resolve(artifactDir, "summary.json"), `${JSON.stringify(summary, null, 2)}\n`);
  writeFileSync(resolve(artifactDir, "summary.txt"), `${summaryText}\n`);

  console.log("Browser terrain stream CPU capture complete.");
  console.log(`Artifacts: ${reportPath(artifactDir)}`);
  console.log(summaryText);
} finally {
  server.kill();
}

/// Runs the movement scenario and returns raw per-frame samples.
async function runTerrainStreamCpuCapture(url) {
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
    await page.evaluate(() => {
      window.__ofgDebug?.resetPerfStats?.();
    });
    await waitForFrames(page, warmupFrames);

    const samples = await recordMovementSamples(page, sampleFrames);
    await waitForBrowserFrame(page);
    await waitForTerrainLodFrame(page);
    assertNoBrowserFailures(consoleMessages);

    return {
      kind: "browser-terrain-stream-cpu-capture",
      runId,
      url,
      warmupFrames,
      sampleFrames,
      consoleMessages,
      samples,
      settledDebug: await readDebugSnapshot(page)
    };
  } finally {
    await browser.close();
  }
}

async function recordMovementSamples(page, frameCount) {
  await page.keyboard.down("ShiftLeft");
  await page.keyboard.down("KeyW");
  try {
    return await page.evaluate((frames) => new Promise((resolveSamples) => {
      const collected = [];
      let previousNow;

      function sample(now) {
        const debug = window.__ofgDebug;
        const perfStats = debug?.getPerfStats?.();
        const terrain = debug?.getTerrainStreamStatus?.();
        const renderer = debug?.getRendererStatus?.();
        const player = debug?.getPlayerPosition?.();
        const latestRust = perfStats?.latest?.rustCpu;
        const browserTerrain = perfStats?.browserTerrainFrame;
        collected.push({
          timeMs: now,
          frameDeltaMs: previousNow === undefined ? 0 : now - previousNow,
          frameIndex: Number(renderer?.frameIndex ?? 0),
          playerPosition: player === undefined
            ? undefined
            : { x: player.x, y: player.y, z: player.z },
          rustTotalFrameMs: latestRust?.totalFrameMs ?? 0,
          rustTerrainStreamUpdateMs: latestRust?.terrainStreamUpdateMs ?? 0,
          rustTerrainCompletionIngestMs: latestRust?.terrainCompletionIngestMs ?? 0,
          rustTerrainStreamTickMs: latestRust?.terrainStreamTickMs ?? 0,
          rustTerrainStreamSyncMs: latestRust?.terrainStreamSyncMs ?? 0,
          rustTerrainStreamSchedulerMs: latestRust?.terrainStreamSchedulerMs ?? 0,
          rustTerrainStreamWorkerQueueMs: latestRust?.terrainStreamWorkerQueueMs ?? 0,
          rustTerrainStreamVisibilityMs: latestRust?.terrainStreamVisibilityMs ?? 0,
          rustTerrainStreamVisibilitySelectMs: latestRust?.terrainStreamVisibilitySelectMs ?? 0,
          rustTerrainStreamVisibilityStatusMs: latestRust?.terrainStreamVisibilityStatusMs ?? 0,
          rustTerrainStreamVisibilityApplyMs: latestRust?.terrainStreamVisibilityApplyMs ?? 0,
          rustTerrainMeshDestroyMs: latestRust?.terrainMeshDestroyMs ?? 0,
          rustTerrainMeshUploadMs: latestRust?.terrainMeshUploadMs ?? 0,
          rustRenderFrameMs: latestRust?.renderFrameMs ?? 0,
          browserTakeCompletionsMs: browserTerrain?.takeCompletionsMs ?? 0,
          browserCompleteTerrainBuildsMs: browserTerrain?.completeTerrainBuildsMs ?? 0,
          browserGameTickMs: browserTerrain?.gameTickMs ?? 0,
          browserTakeRequestsMs: browserTerrain?.takeRequestsMs ?? 0,
          browserSubmitRequestsMs: browserTerrain?.submitRequestsMs ?? 0,
          completionBudget: browserTerrain?.completionBudget ?? 0,
          pendingCompletionCountBefore: browserTerrain?.pendingCompletionCountBefore ?? 0,
          pendingCompletionCountAfter: browserTerrain?.pendingCompletionCountAfter ?? 0,
          drainedCompletionCount: browserTerrain?.drainedCompletionCount ?? 0,
          drainedCompletionVertexBytes: browserTerrain?.drainedCompletionVertexBytes ?? 0,
          drainedCompletionIndexBytes: browserTerrain?.drainedCompletionIndexBytes ?? 0,
          submittedRequestCount: browserTerrain?.submittedRequestCount ?? 0,
          workerInFlightRequestCount: browserTerrain?.workerInFlightRequestCount ?? 0,
          workerCompletedCount: terrain?.terrainWorkerCompletedCount ?? 0,
          workerFailedCount: terrain?.terrainWorkerFailedCount ?? 0,
          workerStaleCompletionCount: terrain?.terrainWorkerStaleCompletionCount ?? 0,
          synchronousBuildCount: terrain?.synchronousBuildCount ?? 0,
          workerInFlightCount: terrain?.terrainWorkerInFlightCount ?? 0,
          workerQueuedRequestCount: terrain?.terrainWorkerQueuedRequestCount ?? 0,
          missingNodeCount: terrain?.missingNodeCount ?? 0,
          renderedNodeCount: terrain?.renderedNodeCount ?? 0,
          maxRenderedLod: terrain?.maxRenderedLod ?? 0,
          meshCount: renderer?.meshCount ?? 0,
          objectCount: renderer?.objectCount ?? 0,
          frameDrawCount: renderer?.frameDrawCount ?? 0,
          frameVisibleDrawCount: renderer?.frameVisibleDrawCount ?? 0,
          terrainUpdateTotalMs: renderer?.terrainUpdateTotalMs ?? 0,
          terrainUpdateUpsertedMeshCount: renderer?.terrainUpdateUpsertedMeshCount ?? 0,
          terrainUpdateRemovedMeshCount: renderer?.terrainUpdateRemovedMeshCount ?? 0,
          terrainUpdateUploadedVertexFloatCount:
            renderer?.terrainUpdateUploadedVertexFloatCount ?? 0,
          terrainUpdateUploadedIndexCount: renderer?.terrainUpdateUploadedIndexCount ?? 0,
          terrainUpdateDeferredUploadCount: renderer?.terrainUpdateDeferredUploadCount ?? 0,
          terrainUpdateDeferredRemovalCount: renderer?.terrainUpdateDeferredRemovalCount ?? 0,
          terrainUpdateUploadBudgetHit: renderer?.terrainUpdateUploadBudgetHit ?? false,
          terrainUpdateRemovalBudgetHit: renderer?.terrainUpdateRemovalBudgetHit ?? false
        });
        previousNow = now;

        if (collected.length >= frames) {
          resolveSamples(collected);
          return;
        }

        requestAnimationFrame(sample);
      }

      requestAnimationFrame(sample);
    }), frameCount);
  } finally {
    await page.keyboard.up("KeyW");
    await page.keyboard.up("ShiftLeft");
  }
}

function summarizeCapture(capture) {
  const samples = capture.samples;
  const derived = samples.map((sample, index) => ({
    ...sample,
    sampleIndex: index,
    workerCompletedBurst: adjacentDelta(samples, index, "workerCompletedCount"),
    meshCountDelta: adjacentDelta(samples, index, "meshCount"),
    objectCountDelta: adjacentDelta(samples, index, "objectCount")
  }));
  const first = samples[0] ?? {};
  const last = samples[samples.length - 1] ?? {};

  return {
    kind: "browser-terrain-stream-cpu-summary",
    runId: capture.runId,
    sampleCount: samples.length,
    frameDeltaMs: durationStats(samples.map((sample) => sample.frameDeltaMs).filter((value) => value > 0)),
    rustTerrainStreamUpdateMs: durationStats(samples.map((sample) => sample.rustTerrainStreamUpdateMs)),
    rustTerrainCompletionIngestMs: durationStats(samples.map((sample) => sample.rustTerrainCompletionIngestMs)),
    rustTerrainStreamTickMs: durationStats(samples.map((sample) => sample.rustTerrainStreamTickMs)),
    rustTerrainStreamSyncMs: durationStats(samples.map((sample) => sample.rustTerrainStreamSyncMs)),
    rustTerrainStreamSchedulerMs: durationStats(samples.map((sample) => sample.rustTerrainStreamSchedulerMs)),
    rustTerrainStreamWorkerQueueMs: durationStats(samples.map((sample) => sample.rustTerrainStreamWorkerQueueMs)),
    rustTerrainStreamVisibilityMs: durationStats(samples.map((sample) => sample.rustTerrainStreamVisibilityMs)),
    rustTerrainStreamVisibilitySelectMs: durationStats(samples.map((sample) => sample.rustTerrainStreamVisibilitySelectMs)),
    rustTerrainStreamVisibilityStatusMs: durationStats(samples.map((sample) => sample.rustTerrainStreamVisibilityStatusMs)),
    rustTerrainStreamVisibilityApplyMs: durationStats(samples.map((sample) => sample.rustTerrainStreamVisibilityApplyMs)),
    rustTerrainMeshDestroyMs: durationStats(samples.map((sample) => sample.rustTerrainMeshDestroyMs)),
    rustTerrainMeshUploadMs: durationStats(samples.map((sample) => sample.rustTerrainMeshUploadMs)),
    browserCompleteTerrainBuildsMs: durationStats(samples.map((sample) => sample.browserCompleteTerrainBuildsMs)),
    browserGameTickMs: durationStats(samples.map((sample) => sample.browserGameTickMs)),
    movementDistanceMeters: distance3(first.playerPosition, last.playerPosition),
    workerCompletedDelta: numericDelta(first.workerCompletedCount, last.workerCompletedCount),
    workerFailedDelta: numericDelta(first.workerFailedCount, last.workerFailedCount),
    workerStaleCompletionDelta: numericDelta(
      first.workerStaleCompletionCount,
      last.workerStaleCompletionCount
    ),
    synchronousBuildDelta: numericDelta(first.synchronousBuildCount, last.synchronousBuildCount),
    maxCompletedBurst: maxSampleValue(derived, "workerCompletedBurst"),
    maxDrainedCompletionCount: maxSampleValue(samples, "drainedCompletionCount"),
    maxPendingCompletionCountAfter: maxSampleValue(samples, "pendingCompletionCountAfter"),
    maxSubmittedRequestCount: maxSampleValue(samples, "submittedRequestCount"),
    maxTerrainUpdateUpsertedMeshCount: maxSampleValue(samples, "terrainUpdateUpsertedMeshCount"),
    maxTerrainUpdateRemovedMeshCount: maxSampleValue(samples, "terrainUpdateRemovedMeshCount"),
    maxTerrainUpdateUploadedVertexFloatCount:
      maxSampleValue(samples, "terrainUpdateUploadedVertexFloatCount"),
    maxTerrainUpdateUploadedIndexCount:
      maxSampleValue(samples, "terrainUpdateUploadedIndexCount"),
    maxTerrainUpdateDeferredUploadCount:
      maxSampleValue(samples, "terrainUpdateDeferredUploadCount"),
    maxTerrainUpdateDeferredRemovalCount:
      maxSampleValue(samples, "terrainUpdateDeferredRemovalCount"),
    uploadBudgetHitFrameCount: samples.filter((sample) => sample.terrainUpdateUploadBudgetHit).length,
    removalBudgetHitFrameCount:
      samples.filter((sample) => sample.terrainUpdateRemovalBudgetHit).length,
    settledMissingNodeCount: capture.settledDebug?.terrainStreamStatus?.missingNodeCount ?? 0,
    settledMaxRenderedLod: capture.settledDebug?.terrainStreamStatus?.maxRenderedLod ?? 0,
    worstFrameDeltas: topSamples(derived, "frameDeltaMs", 12),
    worstTerrainUpdates: topSamples(derived, "terrainUpdateTotalMs", 12),
    worstRustTerrainStreamUpdates: topSamples(derived, "rustTerrainStreamUpdateMs", 12),
    worstRustTerrainVisibility: topSamples(derived, "rustTerrainStreamVisibilityMs", 12),
    worstRustTerrainScheduler: topSamples(derived, "rustTerrainStreamSchedulerMs", 12),
    largestCompletionDrains: topSamples(derived, "drainedCompletionCount", 12),
    largestUploads: topSamples(derived, "terrainUpdateUploadedIndexCount", 12),
    largestRemovals: topSamples(derived, "terrainUpdateRemovedMeshCount", 12)
  };
}

function formatSummaryText(summary) {
  return [
    `Browser terrain stream CPU capture (${summary.sampleCount} frames)`,
    `frame delta avg=${round(summary.frameDeltaMs.mean)}ms p95=${round(summary.frameDeltaMs.p95)}ms max=${round(summary.frameDeltaMs.max)}ms`,
    `rust terrain update avg=${round(summary.rustTerrainStreamUpdateMs.mean)}ms p95=${round(summary.rustTerrainStreamUpdateMs.p95)}ms max=${round(summary.rustTerrainStreamUpdateMs.max)}ms`,
    `stream split tick=${round(summary.rustTerrainStreamTickMs.max)}ms sync=${round(summary.rustTerrainStreamSyncMs.max)}ms sched=${round(summary.rustTerrainStreamSchedulerMs.max)}ms queue=${round(summary.rustTerrainStreamWorkerQueueMs.max)}ms vis=${round(summary.rustTerrainStreamVisibilityMs.max)}ms`,
    `browser complete builds avg=${round(summary.browserCompleteTerrainBuildsMs.mean)}ms p95=${round(summary.browserCompleteTerrainBuildsMs.p95)}ms max=${round(summary.browserCompleteTerrainBuildsMs.max)}ms`,
    `movement=${round(summary.movementDistanceMeters)}m completed=${summary.workerCompletedDelta} failed=${summary.workerFailedDelta} stale=${summary.workerStaleCompletionDelta} sync=${summary.synchronousBuildDelta}`,
    `bursts completed=${summary.maxCompletedBurst} drained=${summary.maxDrainedCompletionCount} pendingAfter=${summary.maxPendingCompletionCountAfter} submittedRequests=${summary.maxSubmittedRequestCount}`,
    `terrain bursts upsert=${summary.maxTerrainUpdateUpsertedMeshCount} removed=${summary.maxTerrainUpdateRemovedMeshCount} vertexFloats=${summary.maxTerrainUpdateUploadedVertexFloatCount} indices=${summary.maxTerrainUpdateUploadedIndexCount}`,
    `deferred upload=${summary.maxTerrainUpdateDeferredUploadCount} removal=${summary.maxTerrainUpdateDeferredRemovalCount} budgetHitFrames upload=${summary.uploadBudgetHitFrameCount} removal=${summary.removalBudgetHitFrameCount}`,
    `settled missing=${summary.settledMissingNodeCount} maxLod=${summary.settledMaxRenderedLod}`,
    "Worst terrain update frames:",
    ...summary.worstTerrainUpdates.slice(0, 6).map(formatWorstSample)
  ].join("\n");
}

function formatWorstSample(sample) {
  return `#${sample.sampleIndex} frame=${round(sample.frameDeltaMs)}ms terrain=${round(sample.terrainUpdateTotalMs)}ms rustStream=${round(sample.rustTerrainStreamUpdateMs)}ms sync=${round(sample.rustTerrainStreamSyncMs)}ms sched=${round(sample.rustTerrainStreamSchedulerMs)}ms vis=${round(sample.rustTerrainStreamVisibilityMs)}ms drained=${sample.drainedCompletionCount} upsert=${sample.terrainUpdateUpsertedMeshCount} remove=${sample.terrainUpdateRemovedMeshCount} vf=${sample.terrainUpdateUploadedVertexFloatCount} idx=${sample.terrainUpdateUploadedIndexCount}`;
}

function durationStats(values) {
  const finite = values.filter((value) => Number.isFinite(value));
  if (finite.length === 0) {
    return { count: 0, mean: 0, p50: 0, p90: 0, p95: 0, p99: 0, max: 0 };
  }

  const sorted = [...finite].sort((left, right) => left - right);
  const sum = finite.reduce((total, value) => total + value, 0);
  return {
    count: finite.length,
    mean: sum / finite.length,
    p50: percentile(sorted, 0.5),
    p90: percentile(sorted, 0.9),
    p95: percentile(sorted, 0.95),
    p99: percentile(sorted, 0.99),
    max: sorted[sorted.length - 1] ?? 0
  };
}

function percentile(sorted, fraction) {
  if (sorted.length === 0) {
    return 0;
  }
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1)] ?? 0;
}

function topSamples(samples, field, count) {
  return [...samples]
    .sort((left, right) => (right[field] ?? 0) - (left[field] ?? 0))
    .slice(0, count)
    .map((sample) => ({
      sampleIndex: sample.sampleIndex,
      timeMs: round(sample.timeMs),
      frameDeltaMs: round(sample.frameDeltaMs),
      rustTerrainStreamUpdateMs: round(sample.rustTerrainStreamUpdateMs),
      rustTerrainCompletionIngestMs: round(sample.rustTerrainCompletionIngestMs),
      rustTerrainStreamTickMs: round(sample.rustTerrainStreamTickMs),
      rustTerrainStreamSyncMs: round(sample.rustTerrainStreamSyncMs),
      rustTerrainStreamSchedulerMs: round(sample.rustTerrainStreamSchedulerMs),
      rustTerrainStreamWorkerQueueMs: round(sample.rustTerrainStreamWorkerQueueMs),
      rustTerrainStreamVisibilityMs: round(sample.rustTerrainStreamVisibilityMs),
      rustTerrainStreamVisibilitySelectMs: round(sample.rustTerrainStreamVisibilitySelectMs),
      rustTerrainStreamVisibilityStatusMs: round(sample.rustTerrainStreamVisibilityStatusMs),
      rustTerrainStreamVisibilityApplyMs: round(sample.rustTerrainStreamVisibilityApplyMs),
      rustTerrainMeshDestroyMs: round(sample.rustTerrainMeshDestroyMs),
      rustTerrainMeshUploadMs: round(sample.rustTerrainMeshUploadMs),
      browserCompleteTerrainBuildsMs: round(sample.browserCompleteTerrainBuildsMs),
      terrainUpdateTotalMs: round(sample.terrainUpdateTotalMs),
      drainedCompletionCount: sample.drainedCompletionCount,
      workerCompletedBurst: sample.workerCompletedBurst,
      pendingCompletionCountAfter: sample.pendingCompletionCountAfter,
      terrainUpdateUpsertedMeshCount: sample.terrainUpdateUpsertedMeshCount,
      terrainUpdateRemovedMeshCount: sample.terrainUpdateRemovedMeshCount,
      terrainUpdateUploadedVertexFloatCount: sample.terrainUpdateUploadedVertexFloatCount,
      terrainUpdateUploadedIndexCount: sample.terrainUpdateUploadedIndexCount,
      terrainUpdateDeferredUploadCount: sample.terrainUpdateDeferredUploadCount,
      terrainUpdateDeferredRemovalCount: sample.terrainUpdateDeferredRemovalCount,
      terrainUpdateUploadBudgetHit: sample.terrainUpdateUploadBudgetHit,
      terrainUpdateRemovalBudgetHit: sample.terrainUpdateRemovalBudgetHit,
      missingNodeCount: sample.missingNodeCount,
      workerInFlightCount: sample.workerInFlightCount
    }));
}

function maxSampleValue(samples, field) {
  return samples.reduce((max, sample) => {
    const value = sample[field];
    return Number.isFinite(value) ? Math.max(max, value) : max;
  }, 0);
}

function adjacentDelta(samples, index, field) {
  return index > 0 ? numericDelta(samples[index - 1]?.[field], samples[index]?.[field]) : 0;
}

function numericDelta(start, end) {
  if (!Number.isFinite(start) || !Number.isFinite(end)) {
    return 0;
  }
  return end - start;
}

function distance3(a, b) {
  if (a === undefined || b === undefined) {
    return 0;
  }

  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const dz = b.z - a.z;
  return Math.hypot(dx, dy, dz);
}

function round(value) {
  return Number.isFinite(value) ? Number(value.toFixed(3)) : 0;
}

async function readDebugSnapshot(page) {
  return page.evaluate(() => ({
    terrainStreamStatus: window.__ofgDebug?.getTerrainStreamStatus?.(),
    rendererStatus: window.__ofgDebug?.getRendererStatus?.(),
    perfStats: window.__ofgDebug?.getPerfStats?.()
  }));
}

async function waitForBrowserFrame(page) {
  await page.waitForFunction(() => {
    const frameTime = document.querySelector("#frame-time")?.textContent;
    const status = window.__ofgDebug?.getRendererStatus?.();
    return (
      frameTime !== undefined &&
      frameTime !== "0.0 ms" &&
      status !== undefined &&
      status.frameDrawCount > 0 &&
      status.frameVisibleDrawCount > 0
    );
  }, undefined, { timeout: 30000 });
}

async function waitForTerrainLodFrame(page) {
  await page.waitForFunction((spanMeters) => {
    const status = window.__ofgDebug?.getTerrainStreamStatus?.();
    return (
      status !== undefined &&
      !status.pending &&
      status.missingNodeCount === 0 &&
      status.renderedNodeCount > status.renderedChunkCount &&
      status.maxRenderedLod >= 4 &&
      status.visibleWorldSpanXMeters >= spanMeters &&
      status.visibleWorldSpanZMeters >= spanMeters
    );
  }, minMultiKmTerrainSpanMeters, { timeout: 60000 });
}

async function waitForFrames(page, frameCount) {
  await page.evaluate((frames) => new Promise((resolveFrames) => {
    let remaining = frames;
    function next() {
      remaining -= 1;
      if (remaining <= 0) {
        resolveFrames();
        return;
      }
      requestAnimationFrame(next);
    }
    requestAnimationFrame(next);
  }), frameCount);
}

function assertNoBrowserFailures(consoleMessages) {
  const failures = consoleMessages.filter((message) =>
    message.startsWith("error:") || message.startsWith("pageerror:")
  );
  if (failures.length > 0) {
    throw new Error(`Browser terrain stream CPU capture saw console failures: ${failures.join("\n")}`);
  }
}

function assertResponseHeaders(response) {
  if (response === null) {
    throw new Error("Browser terrain stream CPU capture did not receive a page response.");
  }
  const headers = response.headers();
  if (
    headers["cross-origin-opener-policy"] !== "same-origin" ||
    headers["cross-origin-embedder-policy"] !== "require-corp"
  ) {
    throw new Error(`Browser isolation headers are missing: ${JSON.stringify(headers)}`);
  }
}

function startDevServer(port) {
  return spawn(process.execPath, ["tools/dev-server.mjs"], {
    cwd: root,
    stdio: ["ignore", "inherit", "inherit"],
    env: {
      ...process.env,
      PORT: String(port)
    }
  });
}

async function waitForHttp(url) {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) {
        return;
      }
    } catch {
      // Server is still starting.
    }
    await new Promise((resolveWait) => setTimeout(resolveWait, 100));
  }

  throw new Error(`Timed out waiting for ${url}`);
}

function findAvailablePort(preferred) {
  return new Promise((resolvePort, rejectPort) => {
    const server = createServer();
    server.on("error", (error) => {
      if (error.code === "EADDRINUSE") {
        resolvePort(findAvailablePort(preferred + 1));
      } else {
        rejectPort(error);
      }
    });
    server.listen(preferred, "127.0.0.1", () => {
      const address = server.address();
      server.close(() => resolvePort(typeof address === "object" && address !== null
        ? address.port
        : preferred));
    });
  });
}

function npmBuildCommand() {
  return process.platform === "win32"
    ? { command: "cmd", args: ["/c", "npm", "run", "build"] }
    : { command: "npm", args: ["run", "build"] };
}

function runCommand(command, args) {
  return new Promise((resolveCommand, rejectCommand) => {
    const child = spawn(command, args, {
      cwd: root,
      stdio: "inherit"
    });
    child.on("exit", (code) => {
      if (code === 0) {
        resolveCommand();
      } else {
        rejectCommand(new Error(`${command} ${args.join(" ")} failed with exit code ${code}`));
      }
    });
  });
}

function findBrowserPath() {
  if (process.env.OFG_BROWSER_PATH && existsSync(process.env.OFG_BROWSER_PATH)) {
    return process.env.OFG_BROWSER_PATH;
  }

  const candidates = process.platform === "win32"
    ? [
        "C:/Program Files/Google/Chrome/Application/chrome.exe",
        "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
        "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
        "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
      ]
    : [
        "/usr/bin/google-chrome",
        "/usr/bin/chromium",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"
      ];

  const found = candidates.find((candidate) => existsSync(candidate));
  if (found === undefined) {
    throw new Error("No Chrome/Edge executable found. Set OFG_BROWSER_PATH to run terrain stream CPU capture.");
  }
  return found;
}

function reportPath(path) {
  return path.replaceAll("\\", "/");
}
