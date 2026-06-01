import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";
import { PNG } from "pngjs";
import {
  createSeedWorldDescriptor,
  createTerrainGenerator,
  DEFAULT_TERRAIN_SEED,
  TERRAIN_PRESET_IDS
} from "../dist/engine/world/terrainGenerator.js";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_SMOKE_PORT ?? "5214", 10);
const headed = process.env.OFG_SMOKE_HEADED === "1";
const artifactRoot = resolve(root, "artifacts", "terrain-variation-smoke");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);
const terrainSeeds = parseSeeds(
  process.env.OFG_TERRAIN_VARIATION_SEEDS ?? `${DEFAULT_TERRAIN_SEED},7001,112358,424242`
);
const surveyRange = Number.parseInt(process.env.OFG_TERRAIN_VARIATION_RANGE ?? "1536", 10);
const surveyStep = Number.parseInt(process.env.OFG_TERRAIN_VARIATION_STEP ?? "64", 10);

const targetSpecs = [
  {
    id: "meadow-lowland",
    label: "Meadow lowland",
    expectedMaterials: ["meadowGrass", "dryGround"],
    expectedBiomes: ["grassland"],
    minScore: 0.55,
    cameraAngle: Math.PI * 0.22,
    score: (candidate) =>
      materialWeight(candidate, "meadowGrass") * 1.1 +
      materialWeight(candidate, "dryGround") * 0.25 +
      biomeWeight(candidate, "grassland") * 0.25 +
      flatness(candidate) * 0.2 +
      (1 - candidate.macro.mountainness) * 0.12
  },
  {
    id: "wet-lowland",
    label: "Wet lowland",
    expectedMaterials: ["wetMud", "sand", "bareSoil"],
    expectedBiomes: ["wetland", "coastBeach"],
    minScore: 0.45,
    cameraAngle: Math.PI * 0.36,
    score: (candidate) =>
      materialWeight(candidate, "wetMud") * 1.2 +
      materialWeight(candidate, "sand") * 0.65 +
      materialWeight(candidate, "bareSoil") * 0.25 +
      biomeWeight(candidate, "wetland") * 0.95 +
      biomeWeight(candidate, "coastBeach") * 0.65 +
      nearSea(candidate) * 0.22 +
      flatness(candidate) * 0.1
  },
  {
    id: "dry-soil",
    label: "Dry soil",
    expectedMaterials: ["dryGround", "redSoil"],
    expectedBiomes: ["dryBadland"],
    minScore: 0.45,
    cameraAngle: Math.PI * 0.53,
    score: (candidate) =>
      materialWeight(candidate, "dryGround") * 0.8 +
      materialWeight(candidate, "redSoil") * 1.25 +
      biomeWeight(candidate, "dryBadland") * 0.9 +
      candidate.macro.continentality * 0.25 +
      flatness(candidate) * 0.08
  },
  {
    id: "mossy-ridge",
    label: "Mossy ridge",
    expectedMaterials: ["mossRock", "forestGround"],
    expectedBiomes: ["temperateForest", "highMountainRock", "alpineMeadow"],
    minScore: 0.5,
    cameraAngle: Math.PI * 0.7,
    score: (candidate) =>
      materialWeight(candidate, "mossRock") * 1.35 +
      materialWeight(candidate, "forestGround") * 0.35 +
      biomeWeight(candidate, "temperateForest") * 0.45 +
      biomeWeight(candidate, "highMountainRock") * 0.35 +
      biomeWeight(candidate, "alpineMeadow") * 0.25 +
      candidate.macro.ridge * 0.22 +
      candidate.macro.mountainness * 0.12
  },
  {
    id: "rocky-slope",
    label: "Rocky slope",
    expectedMaterials: ["rockyGround", "cliffRock", "scree"],
    expectedBiomes: ["highMountainRock", "dryBadland"],
    minScore: 0.45,
    cameraAngle: Math.PI * 0.9,
    score: (candidate) =>
      materialWeight(candidate, "rockyGround") * 1.05 +
      materialWeight(candidate, "cliffRock") * 1.15 +
      materialWeight(candidate, "scree") * 0.8 +
      biomeWeight(candidate, "highMountainRock") * 0.75 +
      biomeWeight(candidate, "dryBadland") * 0.35 +
      candidate.slope * 0.32 +
      candidate.macro.mountainness * 0.12
  },
  {
    id: "red-cliff",
    label: "Red cliff",
    expectedMaterials: ["redSoil", "cliffRock"],
    expectedBiomes: ["dryBadland", "highMountainRock"],
    minScore: 0.45,
    cameraAngle: Math.PI * 1.1,
    score: (candidate) =>
      materialWeight(candidate, "redSoil") * 1.2 +
      materialWeight(candidate, "cliffRock") * 0.85 +
      biomeWeight(candidate, "dryBadland") * 0.75 +
      biomeWeight(candidate, "highMountainRock") * 0.35 +
      candidate.debug.cellular * 0.18 +
      candidate.slope * 0.12
  }
];

mkdirSync(artifactDir, { recursive: true });

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  const baseUrl = `http://127.0.0.1:${port}/`;
  await waitForHttp(baseUrl);
  const survey = buildTerrainVariationSurvey();
  const result = await runTerrainVariationSmoke(baseUrl, survey);
  writeFileSync(resolve(artifactDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);

  console.log("Terrain variation smoke passed.");
  console.log(`Artifacts: ${artifactDir}`);
  for (const target of result.targets) {
    console.log(`Screenshot: ${target.screenshot}`);
  }
} finally {
  server.kill();
}

function buildTerrainVariationSurvey() {
  const candidates = [];
  for (const terrainPreset of TERRAIN_PRESET_IDS) {
    for (const seed of terrainSeeds) {
      const descriptor = createSeedWorldDescriptor(seed, { terrainPreset });
      const generator = createTerrainGenerator(descriptor);
      for (let x = -surveyRange; x <= surveyRange; x += surveyStep) {
        for (let z = -surveyRange; z <= surveyRange; z += surveyStep) {
          candidates.push(sampleCandidate(generator, terrainPreset, seed, x, z));
        }
      }
    }
  }

  const targets = [];
  for (const spec of targetSpecs) {
    const ranked = candidates
      .map((candidate) => ({
        ...candidate,
        targetId: spec.id,
        targetLabel: spec.label,
        expectedMaterials: spec.expectedMaterials,
        expectedBiomes: spec.expectedBiomes,
        score: spec.score(candidate)
      }))
      .sort((a, b) => b.score - a.score);
    const selected = ranked.find((candidate) => isDistinctTarget(candidate, targets)) ?? ranked[0];
    if (selected === undefined) {
      throw new Error(`No terrain survey candidates were found for ${spec.id}.`);
    }

    if (selected.score < spec.minScore) {
      throw new Error(
        `${spec.id} did not reach the minimum survey score: ` +
        `${selected.score.toFixed(3)} < ${spec.minScore}. ` +
        `Best=${JSON.stringify(trimCandidate(selected))}`
      );
    }

    targets.push({
      ...trimCandidate(selected),
      targetId: spec.id,
      targetLabel: spec.label,
      expectedMaterials: spec.expectedMaterials,
      expectedBiomes: spec.expectedBiomes,
      score: selected.score,
      cameraAngle: spec.cameraAngle
    });
  }

  assertSurveyDiversity(targets);

  return {
    seeds: terrainSeeds,
    terrainPresets: [...TERRAIN_PRESET_IDS],
    surveyRange,
    surveyStep,
    sampledCandidates: candidates.length,
    targets
  };
}

function sampleCandidate(generator, terrainPreset, seed, x, z) {
  const height = generator.heightAt(x, z);
  const position = { x, y: height, z };
  const surface = generator.surfaceAt(position);
  const gradientLength = Math.hypot(surface.gradient.x, surface.gradient.y, surface.gradient.z);
  const normalY = gradientLength === 0 ? 1 : surface.gradient.y / gradientLength;
  return {
    terrainPreset,
    seed,
    x,
    z,
    height,
    slope: clamp(1 - normalY, 0, 1),
    normalY,
    macro: generator.macroAt(position),
    biome: generator.biomeAt(position),
    debug: surface.debug,
    materialWeights: [...surface.materialWeights].sort((a, b) => b.weight - a.weight)
  };
}

async function runTerrainVariationSmoke(baseUrl, survey) {
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
    const captures = [];
    for (const target of survey.targets) {
      let page;
      try {
        page = await browser.newPage({
          viewport: { width: 1280, height: 720 },
          deviceScaleFactor: 1
        });
        page.on("console", (message) => {
          consoleMessages.push(`${target.targetId} ${message.type()}: ${message.text()}`);
        });
        page.on("pageerror", (error) => {
          consoleMessages.push(`${target.targetId} pageerror: ${error.message}`);
        });

        const url = `${baseUrl}?terrainPreset=${encodeURIComponent(target.terrainPreset)}` +
          `&terrainSeed=${encodeURIComponent(String(target.seed))}`;
        await page.goto(url, { waitUntil: "load" });
        await waitForRenderedFrame(page);
        await page.waitForFunction(
          (expected) =>
            window.__ofgDebug?.getTerrainPreset?.() === expected.terrainPreset &&
            window.__ofgDebug?.getTerrainSeed?.() === expected.seed,
          { terrainPreset: target.terrainPreset, seed: target.seed },
          { timeout: 10000 }
        );
        await page.evaluate((targetPosition) =>
          window.__ofgDebug?.setPlayerPosition(targetPosition.x, targetPosition.z),
        target);
        await waitForTargetChunk(page, target);
        await placeSurveyCamera(page, target);
        await page.waitForTimeout(350);

        const debug = await readTerrainDebug(page);
        assertTerrainDebug(debug, target);
        const screenshot = await saveScreenshot(page, screenshotName(target));
        assertPixelStats(screenshot.stats, target.targetId, consoleMessages);
        captures.push({
          ...target,
          url,
          screenshot: screenshot.path,
          pixelStats: screenshot.stats,
          debug
        });
      } catch (error) {
        const hud = await readHud(page).catch((hudError) => ({
          error: hudError instanceof Error ? hudError.message : String(hudError)
        }));
        const message = error instanceof Error ? error.message : String(error);
        throw new Error(
          `Terrain variation smoke failed for ${target.targetId}: ${message}. ` +
          `HUD=${JSON.stringify(hud)} console=${JSON.stringify(consoleMessages)}`
        );
      } finally {
        await page?.close().catch(() => undefined);
      }
    }

    assertNoConsoleErrors(consoleMessages);

    return {
      baseUrl,
      browserPath,
      headed,
      survey: {
        ...survey,
        targets: survey.targets.map((target) => omitCameraAngle(target))
      },
      targets: captures.map((target) => omitCameraAngle(target)),
      consoleMessages
    };
  } finally {
    await browser.close();
  }
}

async function waitForRenderedFrame(page) {
  await page.waitForSelector("#camera-mode");
  await page.waitForFunction(() => {
    const mode = document.querySelector("#camera-mode")?.textContent;
    const frameTime = document.querySelector("#frame-time")?.textContent;
    return mode === "WEBGPU" || (mode === "FIRST" && frameTime !== "0.0 ms");
  }, null, { timeout: 30000 });
  await page.waitForTimeout(250);
}

async function waitForTargetChunk(page, target) {
  await page.waitForFunction(() =>
    (window.__ofgDebug?.getLoadedTerrainChunkKeys?.() ?? []).length > 0 &&
    (window.__ofgDebug?.getTerrainChunkKeys?.() ?? []).length > 0,
  target, { timeout: 30000 });
  await page.waitForTimeout(250);
}

async function placeSurveyCamera(page, target) {
  const pose = await page.evaluate((surveyTarget) => {
    const debug = window.__ofgDebug;
    if (debug === undefined) {
      throw new Error("Debug API is unavailable.");
    }

    const distance = surveyTarget.targetId.includes("cliff") ||
      surveyTarget.targetId.includes("slope")
      ? 54
      : 46;
    const cameraX = surveyTarget.x - Math.cos(surveyTarget.cameraAngle) * distance;
    const cameraZ = surveyTarget.z - Math.sin(surveyTarget.cameraAngle) * distance;
    const targetY = debug.getTerrainHeight(surveyTarget.x, surveyTarget.z) + 2.1;
    const cameraTerrainY = debug.getTerrainHeight(cameraX, cameraZ);
    const cameraY = Math.max(cameraTerrainY + 18, targetY + 13);
    return {
      from: { x: cameraX, y: cameraY, z: cameraZ },
      target: { x: surveyTarget.x, y: targetY, z: surveyTarget.z }
    };
  }, target);
  const orientation = lookAtYawPitch(pose.from, pose.target);

  await page.evaluate((cameraPose) => {
    window.__ofgDebug?.setDebugCamera(
      cameraPose.from.x,
      cameraPose.from.y,
      cameraPose.from.z,
      cameraPose.orientation.yaw,
      cameraPose.orientation.pitch
    );
  }, { from: pose.from, orientation });
  await page.waitForFunction(() => document.querySelector("#camera-mode")?.textContent === "FLY");
}

async function readHud(page) {
  return page.evaluate(() => ({
    cameraMode: document.querySelector("#camera-mode")?.textContent ?? "",
    frameTime: document.querySelector("#frame-time")?.textContent ?? "",
    terrainPreset: window.__ofgDebug?.getTerrainPreset?.() ?? "",
    terrainSeed: window.__ofgDebug?.getTerrainSeed?.() ?? null,
    hasDebug: window.__ofgDebug !== undefined,
    hasWebGpu: navigator.gpu !== undefined
  }));
}

async function readTerrainDebug(page) {
  return page.evaluate(() => ({
    terrainPreset: window.__ofgDebug?.getTerrainPreset?.() ?? "",
    terrainSeed: window.__ofgDebug?.getTerrainSeed?.() ?? null,
    loadedChunkKeys: window.__ofgDebug?.getLoadedTerrainChunkKeys?.() ?? [],
    renderChunkKeys: window.__ofgDebug?.getTerrainChunkKeys?.() ?? []
  }));
}

function assertTerrainDebug(debug, target) {
  if (debug.terrainPreset !== target.terrainPreset || debug.terrainSeed !== target.seed) {
    throw new Error(
      `Expected ${target.terrainPreset}/${target.seed}, saw ` +
      `${debug.terrainPreset}/${debug.terrainSeed}: ${JSON.stringify(debug)}`
    );
  }

  if (debug.loadedChunkKeys.length === 0 || debug.renderChunkKeys.length === 0) {
    throw new Error(`${target.targetId} has no terrain chunks: ${JSON.stringify(debug)}`);
  }
}

async function saveScreenshot(page, fileName) {
  const path = resolve(artifactDir, fileName);
  const buffer = await page.screenshot({ path, fullPage: false });
  return {
    path,
    stats: analyzePng(buffer)
  };
}

function analyzePng(buffer) {
  const png = PNG.sync.read(buffer);
  const buckets = new Map();
  let sampledPixels = 0;
  let opaquePixels = 0;
  let sumR = 0;
  let sumG = 0;
  let sumB = 0;

  for (let y = 0; y < png.height; y += 4) {
    for (let x = 0; x < png.width; x += 4) {
      if (x < 180 && y < 80) {
        continue;
      }

      const offset = (png.width * y + x) * 4;
      const r = png.data[offset];
      const g = png.data[offset + 1];
      const b = png.data[offset + 2];
      const a = png.data[offset + 3];
      const key = `${r >> 4},${g >> 4},${b >> 4}`;
      buckets.set(key, (buckets.get(key) ?? 0) + 1);
      sampledPixels += 1;
      if (a > 0) {
        opaquePixels += 1;
      }

      sumR += r;
      sumG += g;
      sumB += b;
    }
  }

  const dominantBucketCount = Math.max(...buckets.values());
  return {
    width: png.width,
    height: png.height,
    sampledPixels,
    opaquePixels,
    uniqueColorBuckets: buckets.size,
    dominantColorRatio: dominantBucketCount / sampledPixels,
    meanColor: {
      r: sumR / sampledPixels,
      g: sumG / sampledPixels,
      b: sumB / sampledPixels
    }
  };
}

function assertPixelStats(stats, label, consoleMessages = []) {
  if (stats.opaquePixels < stats.sampledPixels * 0.99) {
    throw new Error(
      `${label} screenshot is not mostly opaque: ${JSON.stringify(stats)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }

  if (stats.uniqueColorBuckets < 8) {
    throw new Error(
      `${label} screenshot has too little color variation: ${JSON.stringify(stats)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }

  if (stats.dominantColorRatio > 0.9) {
    throw new Error(
      `${label} screenshot looks like a solid fill: ${JSON.stringify(stats)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }
}

function assertNoConsoleErrors(consoleMessages) {
  const errors = consoleMessages.filter((message) =>
    message.includes(" error:") || message.includes(" pageerror:")
  );
  if (errors.length > 0) {
    throw new Error(`Console errors during terrain variation smoke: ${JSON.stringify(errors)}`);
  }
}

function assertSurveyDiversity(targets) {
  const topMaterials = new Set(targets.map((target) => target.materialWeights[0]?.material ?? ""));
  const topBiomes = new Set(targets.map((target) => dominantBiome(target)?.biome ?? ""));
  const seedPresetPairs = new Set(targets.map((target) => `${target.terrainPreset}:${target.seed}`));
  if (topMaterials.size < 4) {
    throw new Error(
      `Terrain variation survey selected too few dominant materials: ` +
      `${JSON.stringify(targets.map((target) => [target.targetId, target.materialWeights[0]]))}`
    );
  }

  if (topBiomes.size < 3) {
    throw new Error(
      `Terrain variation survey selected too few dominant biomes: ` +
      `${JSON.stringify(targets.map((target) => [target.targetId, dominantBiome(target)]))}`
    );
  }

  if (seedPresetPairs.size < 3) {
    throw new Error(
      `Terrain variation survey selected too few preset/seed regions: ` +
      `${JSON.stringify([...seedPresetPairs])}`
    );
  }
}

function isDistinctTarget(candidate, selectedTargets) {
  return selectedTargets.every((target) => {
    if (target.terrainPreset !== candidate.terrainPreset || target.seed !== candidate.seed) {
      return true;
    }

    return Math.hypot(target.x - candidate.x, target.z - candidate.z) >= 256;
  });
}

function trimCandidate(candidate) {
  return {
    terrainPreset: candidate.terrainPreset,
    seed: candidate.seed,
    x: candidate.x,
    z: candidate.z,
    height: candidate.height,
    slope: candidate.slope,
    normalY: candidate.normalY,
    macro: candidate.macro,
    biome: candidate.biome,
    debug: candidate.debug,
    materialWeights: candidate.materialWeights.slice(0, 6)
  };
}

function omitCameraAngle(target) {
  const { cameraAngle, ...reportTarget } = target;
  return reportTarget;
}

function materialWeight(candidate, material) {
  return candidate.materialWeights.find((weight) => weight.material === material)?.weight ?? 0;
}

function biomeWeight(candidate, biome) {
  return candidate.biome.weights.find((weight) => weight.biome === biome)?.weight ?? 0;
}

function dominantBiome(candidate) {
  return candidate.biome.weights.reduce(
    (best, weight) => weight.weight > best.weight ? weight : best,
    candidate.biome.weights[0]
  );
}

function flatness(candidate) {
  return 1 - candidate.slope;
}

function nearSea(candidate) {
  return clamp(1 - Math.abs(candidate.height) / 10, 0, 1);
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function lookAtYawPitch(from, target) {
  const dx = target.x - from.x;
  const dy = target.y - from.y;
  const dz = target.z - from.z;
  const length = Math.hypot(dx, dy, dz);
  return {
    yaw: Math.atan2(dx, dz),
    pitch: Math.asin(dy / length)
  };
}

function screenshotName(target) {
  return `${target.targetId}-${target.terrainPreset}-${target.seed}.png`;
}

function parseSeeds(value) {
  const seeds = value.split(",")
    .map((part) => Number(part.trim()))
    .filter((seed) => Number.isInteger(seed) && seed >= 0);
  if (seeds.length === 0) {
    throw new Error(`No valid terrain variation seeds in '${value}'.`);
  }

  return seeds;
}

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

async function findAvailablePort(start) {
  for (let port = start; port < start + 100; port += 1) {
    if (await canListen(port)) {
      return port;
    }
  }

  throw new Error(`No available port found starting at ${start}.`);
}

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

function sleep(ms) {
  return new Promise((resolveSleep) => setTimeout(resolveSleep, ms));
}
