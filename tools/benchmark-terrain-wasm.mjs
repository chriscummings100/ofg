import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { performance } from "node:perf_hooks";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const assetPath = "assets/wasm/terrain_core.wasm";
const outputPath = readStringArg("--output") ??
  `artifacts/terrain-wasm-bench/${new Date().toISOString().replace(/[:.]/g, "-")}/report.json`;
const iterations = readPositiveIntegerArg("--iterations") ?? 6;
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

console.log(`Benchmarking ${scenarios.length} terrain WASM density chunk scenarios.`);
console.log(`Warmup: ${warmupIterations} pass(es). Timed iterations: ${iterations}.`);
warmUp(terrain, scenarios, warmupIterations);
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
const report = {
  benchmark: "terrain-wasm-density-chunk",
  assetPath,
  seed,
  cellSize,
  sampleCount,
  samplesPerChunk: "33x33x33",
  scenarioCount: scenarios.length,
  iterations,
  warmupIterations,
  chunksPerBenchmark: scenarios.length * iterations,
  results: {
    fillOnly,
    fillAndCopy
  },
  scenarios
};
const absoluteOutputPath = resolve(root, outputPath);

mkdirSync(dirname(absoluteOutputPath), { recursive: true });
writeFileSync(absoluteOutputPath, `${JSON.stringify(report, null, 2)}\n`);

console.log(`Terrain WASM density chunk benchmark (${scenarios.length * iterations} chunks)`);
console.log(`  fill only:    median ${formatMs(fillOnly.medianMs)} ms/chunk, p95 ${formatMs(fillOnly.p95Ms)}, mean ${formatMs(fillOnly.meanMs)}`);
console.log(`  fill + copy:  median ${formatMs(fillAndCopy.medianMs)} ms/chunk, p95 ${formatMs(fillAndCopy.p95Ms)}, mean ${formatMs(fillAndCopy.meanMs)}`);
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

function warmUp(terrain, scenarios, passes) {
  for (let pass = 0; pass < passes; pass += 1) {
    for (const scenario of scenarios) {
      fillChunk(terrain, scenario);
    }
  }
}

function benchmark(name, scenarios, runScenario) {
  const durations = [];
  let checksum = 0;
  const startedAt = performance.now();

  for (let iteration = 0; iteration < iterations; iteration += 1) {
    for (const scenario of scenarios) {
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

function validateTerrainExports(exports) {
  const expectedFunctions = [
    "ofg_density_chunk_sample_count",
    "ofg_density_chunk_buffer_ptr",
    "ofg_fill_density_chunk"
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
