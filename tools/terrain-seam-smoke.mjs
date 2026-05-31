import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";
import { PNG } from "pngjs";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_SMOKE_PORT ?? "5204", 10);
const headed = process.env.OFG_SMOKE_HEADED === "1";
const terrainPreset = process.env.OFG_TERRAIN_SEAM_PRESET ?? "rockyHighland";
const artifactRoot = resolve(root, "artifacts", "terrain-seam-smoke");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);

const scenarios = [
  {
    name: "x-seam-grazing",
    player: { x: 32, z: 0 },
    camera: { x: 8, z: -18, heightOffset: 2.3 },
    target: { x: 32, z: 0, heightOffset: 1.1 },
    coverage: { axis: "x", low: 0, high: 1 }
  },
  {
    name: "z-seam-grazing",
    player: { x: 0, z: 32 },
    camera: { x: -18, z: 8, heightOffset: 2.3 },
    target: { x: 0, z: 32, heightOffset: 1.1 },
    coverage: { axis: "z", low: 0, high: 1 }
  },
  {
    name: "chunk-corner-oblique",
    player: { x: 32, z: 32 },
    camera: { x: 10, z: 8, heightOffset: 3.2 },
    target: { x: 32, z: 32, heightOffset: 1.4 },
    coverage: { axis: "corner", xLow: 0, xHigh: 1, zLow: 0, zHigh: 1 }
  }
];

mkdirSync(artifactDir, { recursive: true });

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  const baseUrl = `http://127.0.0.1:${port}/`;
  await waitForHttp(baseUrl);
  const result = await runTerrainSeamSmoke(baseUrl);
  writeFileSync(resolve(artifactDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);

  console.log("Terrain seam smoke passed.");
  console.log(`Artifacts: ${artifactDir}`);
  for (const scenario of result.scenarios) {
    console.log(`Screenshot: ${scenario.screenshot}`);
  }
} finally {
  server.kill();
}

async function runTerrainSeamSmoke(baseUrl) {
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

    const url = `${baseUrl}?terrainPreset=${encodeURIComponent(terrainPreset)}`;
    await page.goto(url, { waitUntil: "load" });
    await waitForRenderedFrame(page);
    await page.waitForFunction(() => window.__ofgDebug !== undefined);

    const captures = [];
    for (const scenario of scenarios) {
      await placeSeamCamera(page, scenario);
      await page.waitForTimeout(350);

      const debug = await readTerrainDebug(page);
      assertTerrainCoverage(debug, scenario);
      const screenshot = await saveScreenshot(page, `${scenario.name}.png`);
      assertPixelStats(screenshot.stats, scenario.name, consoleMessages);
      captures.push({
        name: scenario.name,
        screenshot: screenshot.path,
        pixelStats: screenshot.stats,
        debug
      });
    }

    assertNoConsoleErrors(consoleMessages);

    return {
      url,
      browserPath,
      headed,
      terrainPreset,
      scenarios: captures,
      consoleMessages
    };
  } finally {
    await browser.close();
  }
}

async function placeSeamCamera(page, scenario) {
  await page.evaluate((player) => window.__ofgDebug?.setPlayerPosition(player.x, player.z), scenario.player);
  await page.waitForFunction(
    (player) => window.__ofgDebug
      ?.getLoadedTerrainChunkKeys()
      .some((key) => key.startsWith(`${Math.floor(player.x / 32)},`)),
    scenario.player,
    { timeout: 10000 }
  );

  const heights = await page.evaluate((cameraSetup) => {
    const debug = window.__ofgDebug;
    if (debug === undefined) {
      throw new Error("Debug API is unavailable.");
    }

    return {
      cameraY: debug.getTerrainHeight(cameraSetup.camera.x, cameraSetup.camera.z) +
        cameraSetup.camera.heightOffset,
      targetY: debug.getTerrainHeight(cameraSetup.target.x, cameraSetup.target.z) +
        cameraSetup.target.heightOffset
    };
  }, scenario);
  const from = {
    x: scenario.camera.x,
    y: heights.cameraY,
    z: scenario.camera.z
  };
  const target = {
    x: scenario.target.x,
    y: heights.targetY,
    z: scenario.target.z
  };
  const orientation = lookAtYawPitch(from, target);

  await page.evaluate((pose) => {
    window.__ofgDebug?.setDebugCamera(
      pose.from.x,
      pose.from.y,
      pose.from.z,
      pose.orientation.yaw,
      pose.orientation.pitch
    );
  }, { from, orientation });
  await page.waitForFunction(() => document.querySelector("#camera-mode")?.textContent === "FLY");
}

async function waitForRenderedFrame(page) {
  await page.waitForSelector("#camera-mode");
  await page.waitForFunction(() => {
    const mode = document.querySelector("#camera-mode")?.textContent;
    const frameTime = document.querySelector("#frame-time")?.textContent;
    return mode === "WEBGPU" || (mode === "FIRST" && frameTime !== "0.0 ms");
  }, null, { timeout: 10000 });
  await page.waitForTimeout(250);
}

async function readTerrainDebug(page) {
  return page.evaluate(() => ({
    loadedChunkKeys: window.__ofgDebug?.getLoadedTerrainChunkKeys() ?? [],
    renderChunkKeys: window.__ofgDebug?.getTerrainChunkKeys() ?? []
  }));
}

function assertTerrainCoverage(debug, scenario) {
  if (debug.loadedChunkKeys.length === 0 || debug.renderChunkKeys.length === 0) {
    throw new Error(`${scenario.name} has no terrain chunks: ${JSON.stringify(debug)}`);
  }

  const renderCoords = debug.renderChunkKeys.map(parseChunkKey);
  if (scenario.coverage.axis === "corner") {
    const hasLowX = renderCoords.some((coord) => coord.x === scenario.coverage.xLow);
    const hasHighX = renderCoords.some((coord) => coord.x === scenario.coverage.xHigh);
    const hasLowZ = renderCoords.some((coord) => coord.z === scenario.coverage.zLow);
    const hasHighZ = renderCoords.some((coord) => coord.z === scenario.coverage.zHigh);
    if (!hasLowX || !hasHighX || !hasLowZ || !hasHighZ) {
      throw new Error(`${scenario.name} does not render both sides of the chunk corner: ${JSON.stringify(debug)}`);
    }
    return;
  }

  const axis = scenario.coverage.axis;
  const hasLow = renderCoords.some((coord) => coord[axis] === scenario.coverage.low);
  const hasHigh = renderCoords.some((coord) => coord[axis] === scenario.coverage.high);
  if (!hasLow || !hasHigh) {
    throw new Error(`${scenario.name} does not render both sides of the ${axis} seam: ${JSON.stringify(debug)}`);
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
    message.startsWith("error:") || message.startsWith("pageerror:")
  );
  if (errors.length > 0) {
    throw new Error(`Console errors during seam smoke: ${JSON.stringify(errors)}`);
  }
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

function parseChunkKey(key) {
  const parts = key.split(",").map((part) => Number.parseInt(part, 10));
  if (parts.length !== 3 || parts.some((part) => !Number.isInteger(part))) {
    throw new Error(`Invalid terrain chunk key '${key}'.`);
  }

  return { x: parts[0], y: parts[1], z: parts[2] };
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
