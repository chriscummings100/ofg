import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { performance } from "node:perf_hooks";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const assetPath = "assets/wasm/terrain_core.wasm";
const outputPath = readStringArg("--output") ??
  `artifacts/terrain-wasm-bench/${new Date().toISOString().replace(/[:.]/g, "-")}/report.json`;
const iterations = readPositiveIntegerArg("--iterations") ?? 6;
const meshIterations = readPositiveIntegerArg("--mesh-iterations") ?? 2;
const warmupIterations = readPositiveIntegerArg("--warmup") ?? 1;
const seed = readNonNegativeIntegerArg("--seed") ?? 0x0F6;
const cellSize = readPositiveNumberArg("--cell-size") ?? 1;

const presetCodes = Object.freeze({
  seed: 0,
  rollingHills: 1,
  mountainValley: 2,
  rockyHighland: 3
});
const chunkCoords = Object.freeze([
  Object.freeze({ x: 0, y: 0, z: 0 }),
  Object.freeze({ x: -1, y: 0, z: 2 }),
  Object.freeze({ x: 3, y: -1, z: -2 })
]);
const streamingCenters = Object.freeze([
  Object.freeze({ x: 0, y: 0, z: 0 }),
  Object.freeze({ x: 1, y: 0, z: 0 }),
  Object.freeze({ x: 2, y: 0, z: 0 }),
  Object.freeze({ x: 3, y: 0, z: 0 })
]);
const streamingHorizontalRadius = 1;
const streamingVerticalChunkOffsets = Object.freeze([-2, -1, 0, 1]);

const wasmBytes = readFileSync(resolve(root, assetPath));
const wasm = await WebAssembly.instantiate(wasmBytes, {});
const terrain = validateTerrainExports(wasm.instance.exports);
const sampleCount = terrain.ofg_density_chunk_sample_count();
const densityBuffer = new Float32Array(
  terrain.memory.buffer,
  terrain.ofg_density_chunk_buffer_ptr(),
  sampleCount
);
const scenarios = buildScenarios();
const streamingWindows = buildStreamingWindows();

console.log(`Benchmarking ${scenarios.length} terrain WASM density chunk scenarios.`);
console.log(`Warmup: ${warmupIterations} pass(es). Density iterations: ${iterations}. Mesh iterations: ${meshIterations}.`);
warmUp(terrain, scenarios, warmupIterations);
resetDensityStore(terrain);
console.log("Warmup complete.");

console.log("Running fill-only benchmark...");
const fillOnly = benchmark("fillOnly", scenarios, (scenario) => {
  fillChunk(terrain, scenario);
  return densityBuffer[0] + densityBuffer[32768] + densityBuffer[sampleCount - 1];
});
console.log("Running fill-plus-copy benchmark...");
const fillAndCopy = benchmark("fillAndCopy", scenarios, (scenario) => {
  fillChunk(terrain, scenario);
  const copy = new Float32Array(densityBuffer);
  return copy[0] + copy[32768] + copy[copy.length - 1];
});
console.log("Running neighbor-apron fill benchmark...");
const apronFillOnly = benchmark("apronFillOnly", scenarios, (scenario) => {
  let checksum = 0;
  for (const chunk of neighborApronChunks(scenario.chunk)) {
    terrain.ofg_fill_density_chunk(
      scenario.seed,
      scenario.presetCode,
      chunk.x,
      chunk.y,
      chunk.z,
      scenario.cellSize
    );
    checksum += densityBuffer[0] + densityBuffer[32768] + densityBuffer[sampleCount - 1];
  }

  return checksum;
});
console.log("Running retained density-window prepare benchmark...");
resetDensityStore(terrain);
const densityWindowPrepareRetained = benchmark(
  "densityWindowPrepareRetained",
  streamingWindows,
  (scenario) => {
    const prepared = prepareDensityWindow(terrain, scenario);
    const stats = densityStoreStats(terrain);
    return prepared + stats.entries + stats.reuses + stats.generations - stats.evictions;
  }
);
const densityWindowStoreStats = densityStoreStats(terrain);
console.log("Running cold mesh-build-plus-copy benchmark...");
const meshBuildAndCopyCold = benchmark("meshBuildAndCopyCold", scenarios, (scenario) => {
  resetDensityStore(terrain);
  terrain.ofg_build_chunk_mesh(
    scenario.seed,
    scenario.presetCode,
    scenario.chunk.x,
    scenario.chunk.y,
    scenario.chunk.z,
    scenario.cellSize
  );
  const vertices = new Float32Array(
    terrain.memory.buffer,
    terrain.ofg_mesh_vertex_buffer_ptr(),
    terrain.ofg_mesh_vertex_buffer_len()
  );
  const indices = new Uint32Array(
    terrain.memory.buffer,
    terrain.ofg_mesh_index_buffer_ptr(),
    terrain.ofg_mesh_index_buffer_len()
  );
  const vertexCopy = new Float32Array(vertices);
  const indexCopy = new Uint32Array(indices);

  return vertexCopy.length + indexCopy.length + (vertexCopy[0] ?? 0) + (indexCopy[0] ?? 0);
}, meshIterations);
console.log("Running prepared mesh-build-plus-copy benchmark...");
const meshBuildAndCopyPrepared = benchmark("meshBuildAndCopyPrepared", scenarios, (scenario) => {
  terrain.ofg_build_chunk_mesh(
    scenario.seed,
    scenario.presetCode,
    scenario.chunk.x,
    scenario.chunk.y,
    scenario.chunk.z,
    scenario.cellSize
  );
  const vertices = new Float32Array(
    terrain.memory.buffer,
    terrain.ofg_mesh_vertex_buffer_ptr(),
    terrain.ofg_mesh_vertex_buffer_len()
  );
  const indices = new Uint32Array(
    terrain.memory.buffer,
    terrain.ofg_mesh_index_buffer_ptr(),
    terrain.ofg_mesh_index_buffer_len()
  );
  const vertexCopy = new Float32Array(vertices);
  const indexCopy = new Uint32Array(indices);

  return vertexCopy.length + indexCopy.length + (vertexCopy[0] ?? 0) + (indexCopy[0] ?? 0);
}, meshIterations, {
  beforeScenario: (scenario) => {
    resetDensityStore(terrain);
    prepareMeshDensityWindow(terrain, scenario);
  }
});
const preparedMeshStoreStats = densityStoreStats(terrain);
const phaseEstimate = {
  medianApronDensityShareOfMesh:
    meshBuildAndCopyCold.medianMs <= 0 ? 0 : apronFillOnly.medianMs / meshBuildAndCopyCold.medianMs,
  medianPreparedMeshShareOfColdMesh:
    meshBuildAndCopyCold.medianMs <= 0 ? 0 : meshBuildAndCopyPrepared.medianMs / meshBuildAndCopyCold.medianMs,
  medianMeshResidualMs: meshBuildAndCopyPrepared.medianMs,
  meanApronDensityShareOfMesh:
    meshBuildAndCopyCold.meanMs <= 0 ? 0 : apronFillOnly.meanMs / meshBuildAndCopyCold.meanMs,
  meanPreparedMeshShareOfColdMesh:
    meshBuildAndCopyCold.meanMs <= 0 ? 0 : meshBuildAndCopyPrepared.meanMs / meshBuildAndCopyCold.meanMs,
  meanMeshResidualMs: meshBuildAndCopyPrepared.meanMs
};
const report = {
  benchmark: "terrain-wasm-chunk-pipeline",
  assetPath,
  seed,
  cellSize,
  sampleCount,
  samplesPerChunk: "33x33x33",
  scenarioCount: scenarios.length,
  iterations,
  meshIterations,
  warmupIterations,
  chunksPerBenchmark: scenarios.length * iterations,
  chunksPerMeshBenchmark: scenarios.length * meshIterations,
  streamingWindowCount: streamingWindows.length,
  streamingHorizontalRadius,
  streamingVerticalChunkOffsets,
  results: {
    fillOnly,
    fillAndCopy,
    apronFillOnly,
    densityWindowPrepareRetained,
    meshBuildAndCopyCold,
    meshBuildAndCopyPrepared
  },
  densityStore: {
    afterRetainedWindowPrepare: densityWindowStoreStats,
    afterPreparedMesh: preparedMeshStoreStats
  },
  phaseEstimate,
  scenarios,
  streamingWindows
};
const absoluteOutputPath = resolve(root, outputPath);

mkdirSync(dirname(absoluteOutputPath), { recursive: true });
writeFileSync(absoluteOutputPath, `${JSON.stringify(report, null, 2)}\n`);

console.log(`Terrain WASM chunk benchmark (${scenarios.length * iterations} density chunks)`);
console.log(`  fill only:    median ${formatMs(fillOnly.medianMs)} ms/chunk, p95 ${formatMs(fillOnly.p95Ms)}, mean ${formatMs(fillOnly.meanMs)}`);
console.log(`  fill + copy:  median ${formatMs(fillAndCopy.medianMs)} ms/chunk, p95 ${formatMs(fillAndCopy.p95Ms)}, mean ${formatMs(fillAndCopy.meanMs)}`);
console.log(`  apron fill:   median ${formatMs(apronFillOnly.medianMs)} ms/mesh, p95 ${formatMs(apronFillOnly.p95Ms)}, mean ${formatMs(apronFillOnly.meanMs)}`);
console.log(`  density window prepare: median ${formatMs(densityWindowPrepareRetained.medianMs)} ms/window, p95 ${formatMs(densityWindowPrepareRetained.p95Ms)}, mean ${formatMs(densityWindowPrepareRetained.meanMs)}`);
console.log(`  mesh + copy cold:       median ${formatMs(meshBuildAndCopyCold.medianMs)} ms/chunk, p95 ${formatMs(meshBuildAndCopyCold.p95Ms)}, mean ${formatMs(meshBuildAndCopyCold.meanMs)}`);
console.log(`  mesh + copy prepared:   median ${formatMs(meshBuildAndCopyPrepared.medianMs)} ms/chunk, p95 ${formatMs(meshBuildAndCopyPrepared.p95Ms)}, mean ${formatMs(meshBuildAndCopyPrepared.meanMs)}`);
console.log(`  prepared mesh residual: median ${formatMs(phaseEstimate.medianMeshResidualMs)} ms/chunk (${formatPercent(phaseEstimate.medianPreparedMeshShareOfColdMesh)} of cold median mesh time)`);
console.log(`  retained density store: ${densityWindowStoreStats.entries} entries, ${densityWindowStoreStats.reuses} reuses, ${densityWindowStoreStats.generations} generations, ${densityWindowStoreStats.evictions} evictions`);
console.log(`Report: ${absoluteOutputPath}`);

function buildScenarios() {
  const scenarios = [];
  for (const [preset, presetCode] of Object.entries(presetCodes)) {
    for (const coord of chunkCoords) {
      scenarios.push(Object.freeze({
        seed,
        preset,
        presetCode,
        chunk: coord,
        cellSize
      }));
    }
  }

  return scenarios;
}

function buildStreamingWindows() {
  const scenarios = [];
  for (const [preset, presetCode] of Object.entries(presetCodes)) {
    for (const center of streamingCenters) {
      scenarios.push(Object.freeze({
        seed,
        preset,
        presetCode,
        center,
        bounds: densityWindowBounds(center),
        cellSize
      }));
    }
  }

  return scenarios;
}

function warmUp(terrain, scenarios, passes) {
  for (let pass = 0; pass < passes; pass += 1) {
    for (const scenario of scenarios) {
      fillChunk(terrain, scenario);
    }
  }
}

function benchmark(name, scenarios, runScenario, timedIterations = iterations, options = {}) {
  const durations = [];
  let checksum = 0;
  const startedAt = performance.now();

  for (let iteration = 0; iteration < timedIterations; iteration += 1) {
    for (const scenario of scenarios) {
      options.beforeScenario?.(scenario, iteration);
      const start = performance.now();
      checksum += runScenario(scenario);
      durations.push(performance.now() - start);
    }
  }

  const totalMs = performance.now() - startedAt;
  const sorted = [...durations].sort((a, b) => a - b);
  const sum = durations.reduce((total, duration) => total + duration, 0);

  return {
    name,
    chunkCount: durations.length,
    totalMs,
    meanMs: sum / durations.length,
    medianMs: percentile(sorted, 0.5),
    p95Ms: percentile(sorted, 0.95),
    minMs: sorted[0],
    maxMs: sorted[sorted.length - 1],
    checksum
  };
}

function prepareMeshDensityWindow(terrain, scenario) {
  const bounds = {
    minX: scenario.chunk.x,
    minY: scenario.chunk.y,
    minZ: scenario.chunk.z,
    maxX: scenario.chunk.x + 1,
    maxY: scenario.chunk.y + 1,
    maxZ: scenario.chunk.z + 1
  };

  return terrain.ofg_prepare_density_chunk_window(
    scenario.seed,
    scenario.presetCode,
    bounds.minX,
    bounds.minY,
    bounds.minZ,
    bounds.maxX,
    bounds.maxY,
    bounds.maxZ,
    scenario.cellSize
  );
}

function prepareDensityWindow(terrain, scenario) {
  return terrain.ofg_prepare_density_chunk_window(
    scenario.seed,
    scenario.presetCode,
    scenario.bounds.minX,
    scenario.bounds.minY,
    scenario.bounds.minZ,
    scenario.bounds.maxX,
    scenario.bounds.maxY,
    scenario.bounds.maxZ,
    scenario.cellSize
  );
}

function densityWindowBounds(center) {
  const minVerticalOffset = Math.min(...streamingVerticalChunkOffsets);
  const maxVerticalOffset = Math.max(...streamingVerticalChunkOffsets);

  return Object.freeze({
    minX: center.x - streamingHorizontalRadius,
    minY: center.y + minVerticalOffset,
    minZ: center.z - streamingHorizontalRadius,
    maxX: center.x + streamingHorizontalRadius + 1,
    maxY: center.y + maxVerticalOffset + 1,
    maxZ: center.z + streamingHorizontalRadius + 1
  });
}

function resetDensityStore(terrain) {
  terrain.ofg_reset_density_chunk_store();
}

function densityStoreStats(terrain) {
  return {
    entries: terrain.ofg_density_chunk_store_entry_count(),
    maxEntries: terrain.ofg_density_chunk_store_max_entries(),
    reuses: terrain.ofg_density_chunk_store_reuse_count(),
    generations: terrain.ofg_density_chunk_store_generation_count(),
    evictions: terrain.ofg_density_chunk_store_eviction_count()
  };
}

function fillChunk(terrain, scenario) {
  terrain.ofg_fill_density_chunk(
    scenario.seed,
    scenario.presetCode,
    scenario.chunk.x,
    scenario.chunk.y,
    scenario.chunk.z,
    scenario.cellSize
  );
}

function neighborApronChunks(chunk) {
  const chunks = [];
  for (let dz = 0; dz <= 1; dz += 1) {
    for (let dy = 0; dy <= 1; dy += 1) {
      for (let dx = 0; dx <= 1; dx += 1) {
        chunks.push({
          x: chunk.x + dx,
          y: chunk.y + dy,
          z: chunk.z + dz
        });
      }
    }
  }

  return chunks;
}

function validateTerrainExports(exports) {
  const expectedFunctions = [
    "ofg_density_chunk_sample_count",
    "ofg_density_chunk_buffer_ptr",
    "ofg_density_chunk_store_max_entries",
    "ofg_density_chunk_store_entry_count",
    "ofg_density_chunk_store_reuse_count",
    "ofg_density_chunk_store_generation_count",
    "ofg_density_chunk_store_eviction_count",
    "ofg_reset_density_chunk_store",
    "ofg_store_density_chunk_buffer",
    "ofg_retain_density_chunk_store_window",
    "ofg_prepare_density_chunk_window",
    "ofg_fill_density_chunk",
    "ofg_build_chunk_mesh",
    "ofg_mesh_vertex_buffer_ptr",
    "ofg_mesh_vertex_buffer_len",
    "ofg_mesh_index_buffer_ptr",
    "ofg_mesh_index_buffer_len"
  ];

  if (!(exports.memory instanceof WebAssembly.Memory)) {
    throw new Error("Terrain WASM benchmark requires exported memory.");
  }

  for (const name of expectedFunctions) {
    if (typeof exports[name] !== "function") {
      throw new Error(`Terrain WASM benchmark requires export '${name}'.`);
    }
  }

  return exports;
}

function percentile(sorted, fraction) {
  const index = Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * fraction) - 1));
  return sorted[index];
}

function readStringArg(name) {
  const index = process.argv.indexOf(name);
  if (index === -1) {
    return undefined;
  }

  const value = process.argv[index + 1];
  if (value === undefined || value.startsWith("--")) {
    throw new Error(`${name} requires a value.`);
  }

  return value;
}

function readPositiveIntegerArg(name) {
  const value = readStringArg(name);
  if (value === undefined) {
    return undefined;
  }

  const number = Number(value);
  if (!Number.isInteger(number) || number <= 0) {
    throw new Error(`${name} must be a positive integer.`);
  }

  return number;
}

function readNonNegativeIntegerArg(name) {
  const value = readStringArg(name);
  if (value === undefined) {
    return undefined;
  }

  const number = Number(value);
  if (!Number.isInteger(number) || number < 0) {
    throw new Error(`${name} must be a non-negative integer.`);
  }

  return number;
}

function readPositiveNumberArg(name) {
  const value = readStringArg(name);
  if (value === undefined) {
    return undefined;
  }

  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0) {
    throw new Error(`${name} must be a positive number.`);
  }

  return number;
}

function formatMs(value) {
  return value.toFixed(3);
}

function formatPercent(value) {
  return `${(value * 100).toFixed(1)}%`;
}
