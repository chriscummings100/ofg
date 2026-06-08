// Movement-delta terrain performance smoke for the browser integration harness.
// It drives real keyboard movement in Chrome and records frame, worker, and
// terrain upload metrics while Rust-owned terrain streaming updates.

import { writeFileSync } from "node:fs";
import { resolve } from "node:path";

const movementPerformanceFrameCount = 360;
const maxMovementP95FrameDeltaMs = 250;
const maxMovementFrameDeltaMs = 1500;
const maxMovementTerrainUpdateMs = 500;
const minMovementDistanceMeters = 30;

/// Runs the player forward at running speed and records frame/terrain streaming metrics.
export async function runMovementPerformanceSmoke({
  page,
  artifactDir,
  consoleMessages,
  waitForBrowserFrame,
  waitForTerrainLodFrame,
  assertNoBrowserFailures,
  readDebugContract,
  assertDebugContract
}) {
  const samples = [];
  await page.keyboard.down("ShiftLeft");
  await page.keyboard.down("KeyW");
  try {
    samples.push(...await page.evaluate((frameCount) => new Promise((resolveSamples) => {
      const collected = [];
      let previousNow;

      function sample(now) {
        const debug = window.__ofgDebug;
        const terrain = debug?.getTerrainStreamStatus?.();
        const renderer = debug?.getRendererStatus?.();
        const player = debug?.getPlayerPosition?.();
        collected.push({
          timeMs: now,
          frameDeltaMs: previousNow === undefined ? 0 : now - previousNow,
          frameIndex: Number(renderer?.frameIndex ?? 0),
          playerPosition: player === undefined
            ? undefined
            : { x: player.x, y: player.y, z: player.z },
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
          terrainUpdateUploadedIndexCount: renderer?.terrainUpdateUploadedIndexCount ?? 0
        });
        previousNow = now;

        if (collected.length >= frameCount) {
          resolveSamples(collected);
          return;
        }

        requestAnimationFrame(sample);
      }

      requestAnimationFrame(sample);
    }), movementPerformanceFrameCount));
  } finally {
    await page.keyboard.up("KeyW");
    await page.keyboard.up("ShiftLeft");
  }

  await waitForBrowserFrame(page);
  await waitForTerrainLodFrame(page);
  assertNoBrowserFailures(consoleMessages);
  const settledDebug = await readDebugContract(page);
  assertDebugContract(settledDebug);
  const summary = summarizeMovementPerformance(samples, settledDebug);
  assertMovementPerformance(summary, consoleMessages);
  writeFileSync(
    resolve(artifactDir, "movement-performance-samples.json"),
    `${JSON.stringify(samples, null, 2)}\n`
  );

  return summary;
}

/// Summarizes movement-delta frame and terrain streaming samples.
function summarizeMovementPerformance(samples, settledDebug) {
  const frameDeltas = samples
    .map((sample) => sample.frameDeltaMs)
    .filter((delta) => Number.isFinite(delta) && delta > 0);
  const first = samples[0] ?? {};
  const last = samples[samples.length - 1] ?? {};
  const movementDistanceMeters = distance3(first.playerPosition, last.playerPosition);
  const completedDelta = numericDelta(first.workerCompletedCount, last.workerCompletedCount);
  const failedDelta = numericDelta(first.workerFailedCount, last.workerFailedCount);
  const staleDelta = numericDelta(
    first.workerStaleCompletionCount,
    last.workerStaleCompletionCount
  );
  const synchronousDelta = numericDelta(first.synchronousBuildCount, last.synchronousBuildCount);
  const terrainUpdateTotals = samples.reduce((totals, sample) => ({
    upsertedMeshCount: totals.upsertedMeshCount + sample.terrainUpdateUpsertedMeshCount,
    removedMeshCount: totals.removedMeshCount + sample.terrainUpdateRemovedMeshCount,
    uploadedVertexFloatCount:
      totals.uploadedVertexFloatCount + sample.terrainUpdateUploadedVertexFloatCount,
    uploadedIndexCount: totals.uploadedIndexCount + sample.terrainUpdateUploadedIndexCount
  }), {
    upsertedMeshCount: 0,
    removedMeshCount: 0,
    uploadedVertexFloatCount: 0,
    uploadedIndexCount: 0
  });

  return {
    sampleCount: samples.length,
    frameIndexDelta: numericDelta(first.frameIndex, last.frameIndex),
    frameDeltaMs: durationStats(frameDeltas),
    movementDistanceMeters,
    workerCompletedDelta: completedDelta,
    workerFailedDelta: failedDelta,
    workerStaleCompletionDelta: staleDelta,
    synchronousBuildDelta: synchronousDelta,
    maxWorkerInFlightCount: maxSampleValue(samples, "workerInFlightCount"),
    maxWorkerQueuedRequestCount: maxSampleValue(samples, "workerQueuedRequestCount"),
    maxCompletedBurst: maxAdjacentDelta(samples, "workerCompletedCount"),
    maxTerrainUpdateTotalMs: maxSampleValue(samples, "terrainUpdateTotalMs"),
    maxTerrainUpdateUpsertedMeshCount: maxSampleValue(samples, "terrainUpdateUpsertedMeshCount"),
    maxTerrainUpdateRemovedMeshCount: maxSampleValue(samples, "terrainUpdateRemovedMeshCount"),
    terrainUpdateTotals,
    meshCountDelta: numericDelta(first.meshCount, last.meshCount),
    objectCountDelta: numericDelta(first.objectCount, last.objectCount),
    maxMissingNodeCount: maxSampleValue(samples, "missingNodeCount"),
    settledMissingNodeCount: settledDebug.terrainStreamStatus?.missingNodeCount ?? 0,
    settledMaxRenderedLod: settledDebug.terrainStreamStatus?.maxRenderedLod ?? 0
  };
}

/// Validates movement streaming stayed threaded and within generous stutter bounds.
function assertMovementPerformance(summary, consoleMessages) {
  if (
    summary.sampleCount < movementPerformanceFrameCount ||
    summary.frameIndexDelta < movementPerformanceFrameCount * 0.5 ||
    summary.movementDistanceMeters < minMovementDistanceMeters ||
    summary.workerCompletedDelta <= 0 ||
    summary.synchronousBuildDelta !== 0 ||
    summary.workerFailedDelta !== 0 ||
    summary.workerStaleCompletionDelta !== 0 ||
    summary.terrainUpdateTotals.upsertedMeshCount <= 0 ||
    summary.terrainUpdateTotals.uploadedIndexCount <= 0 ||
    summary.settledMissingNodeCount !== 0 ||
    summary.settledMaxRenderedLod < 4 ||
    summary.frameDeltaMs.p95 > maxMovementP95FrameDeltaMs ||
    summary.frameDeltaMs.max > maxMovementFrameDeltaMs ||
    summary.maxTerrainUpdateTotalMs > maxMovementTerrainUpdateMs
  ) {
    throw new Error(
      `Movement terrain performance smoke failed: ${JSON.stringify(summary)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }
}

/// Returns summary statistics for a numeric duration series.
function durationStats(values) {
  if (values.length === 0) {
    return { count: 0, mean: 0, p95: 0, max: 0 };
  }

  const sorted = [...values].sort((a, b) => a - b);
  const sum = values.reduce((total, value) => total + value, 0);
  return {
    count: values.length,
    mean: sum / values.length,
    p95: sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * 0.95) - 1)],
    max: sorted[sorted.length - 1]
  };
}

/// Computes a safe numeric delta between two sample fields.
function numericDelta(start, end) {
  if (!Number.isFinite(start) || !Number.isFinite(end)) {
    return 0;
  }

  return end - start;
}

/// Returns the largest finite sample value for one numeric field.
function maxSampleValue(samples, field) {
  return samples.reduce((max, sample) => {
    const value = sample[field];
    return Number.isFinite(value) ? Math.max(max, value) : max;
  }, 0);
}

/// Returns the largest positive adjacent delta for a monotonically increasing sample field.
function maxAdjacentDelta(samples, field) {
  let maxDelta = 0;
  for (let index = 1; index < samples.length; index += 1) {
    const delta = numericDelta(samples[index - 1]?.[field], samples[index]?.[field]);
    maxDelta = Math.max(maxDelta, delta);
  }

  return maxDelta;
}

/// Computes 3D distance between optional player-position samples.
function distance3(a, b) {
  if (a === undefined || b === undefined) {
    return 0;
  }

  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const dz = b.z - a.z;
  return Math.hypot(dx, dy, dz);
}
