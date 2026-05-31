import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";
import { PNG } from "pngjs";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_SMOKE_PORT ?? "5174", 10);
const headed = process.env.OFG_SMOKE_HEADED === "1";
const artifactRoot = resolve(root, "artifacts", "browser-smoke");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);
const expectedRenderChunkCount = 9;

mkdirSync(artifactDir, { recursive: true });

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  await waitForHttp(`http://127.0.0.1:${port}/`);
  const result = await runBrowserSmoke(`http://127.0.0.1:${port}/`);
  writeFileSync(resolve(artifactDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);

  console.log("Browser smoke passed.");
  console.log(`Artifacts: ${artifactDir}`);
  for (const screenshot of result.screenshots) {
    console.log(`Screenshot: ${screenshot}`);
  }
} finally {
  server.kill();
}

async function runBrowserSmoke(url) {
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

  const screenshots = [];
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

    await page.goto(url, { waitUntil: "load" });
    await page.waitForSelector("#camera-mode");
    await page.waitForFunction(() => {
      const mode = document.querySelector("#camera-mode")?.textContent;
      const frameTime = document.querySelector("#frame-time")?.textContent;
      return mode === "WEBGPU" || (mode === "FIRST" && frameTime !== "0.0 ms");
    }, null, { timeout: 10000 });
    await page.waitForTimeout(250);

    const firstHud = await readHud(page);
    assertHud(firstHud, "FIRST", consoleMessages);
    const firstScreenshot = await saveScreenshot(page, "first-person.png");
    screenshots.push(firstScreenshot.path);
    assertPixelStats(firstScreenshot.stats, "first-person", consoleMessages);
    const initialTerrain = await readTerrainDebug(page);
    assertTerrainDebug(initialTerrain, "initial terrain");

    await page.keyboard.press("KeyC");
    await page.waitForFunction(() => document.querySelector("#camera-mode")?.textContent === "FLY");
    await page.waitForTimeout(250);

    const flyHud = await readHud(page);
    assertHud(flyHud, "FLY", consoleMessages);
    const flyScreenshot = await saveScreenshot(page, "debug-fly.png");
    screenshots.push(flyScreenshot.path);
    assertPixelStats(flyScreenshot.stats, "debug-fly", consoleMessages);

    await page.keyboard.press("KeyC");
    await page.waitForFunction(() => document.querySelector("#camera-mode")?.textContent === "FIRST");

    await page.evaluate(() => window.__ofgDebug?.setPlayerPosition(96, 0));
    await page.waitForFunction(() => window.__ofgDebug
      ?.getLoadedTerrainChunkKeys()
      .includes("3,0,0"));
    await page.waitForTimeout(250);

    const streamedTerrain = await readTerrainDebug(page);
    assertTerrainDebug(streamedTerrain, "streamed terrain");
    assertTerrainStreamed(initialTerrain, streamedTerrain);
    const streamedScreenshot = await saveScreenshot(page, "streamed-first-person.png");
    screenshots.push(streamedScreenshot.path);
    assertPixelStats(streamedScreenshot.stats, "streamed-first-person", consoleMessages);

    return {
      url,
      browserPath,
      headed,
      screenshots,
      firstHud,
      flyHud,
      initialTerrain,
      streamedTerrain,
      firstPixelStats: firstScreenshot.stats,
      streamedPixelStats: streamedScreenshot.stats,
      flyPixelStats: flyScreenshot.stats,
      consoleMessages
    };
  } finally {
    await browser.close();
  }
}

async function readHud(page) {
  return page.evaluate(() => ({
    cameraMode: document.querySelector("#camera-mode")?.textContent ?? "",
    frameTime: document.querySelector("#frame-time")?.textContent ?? "",
    hasWebGpu: navigator.gpu !== undefined,
    canvasWidth: document.querySelector("canvas")?.width ?? 0,
    canvasHeight: document.querySelector("canvas")?.height ?? 0
  }));
}

async function readTerrainDebug(page) {
  return page.evaluate(() => ({
    hasDebug: window.__ofgDebug !== undefined,
    loadedChunkKeys: window.__ofgDebug?.getLoadedTerrainChunkKeys() ?? [],
    renderChunkKeys: window.__ofgDebug?.getTerrainChunkKeys() ?? []
  }));
}

function assertHud(hud, expectedMode, consoleMessages) {
  if (hud.cameraMode !== expectedMode) {
    throw new Error(
      `Expected HUD mode ${expectedMode}, saw ${hud.cameraMode}. ` +
      `HUD=${JSON.stringify(hud)} console=${JSON.stringify(consoleMessages)}`
    );
  }

  if (!hud.hasWebGpu) {
    throw new Error("Browser smoke requires WebGPU, but navigator.gpu is unavailable.");
  }

  if (hud.canvasWidth <= 0 || hud.canvasHeight <= 0) {
    throw new Error(`Canvas has invalid dimensions: ${hud.canvasWidth}x${hud.canvasHeight}`);
  }
}

function assertTerrainDebug(debug, label) {
  if (!debug.hasDebug) {
    throw new Error(`${label} debug API is unavailable.`);
  }

  if (debug.loadedChunkKeys.length === 0) {
    throw new Error(`${label} has no loaded terrain chunks: ${JSON.stringify(debug)}`);
  }

  if (debug.renderChunkKeys.length === 0) {
    throw new Error(`${label} has no rendered terrain chunks: ${JSON.stringify(debug)}`);
  }

  if (debug.renderChunkKeys.length !== expectedRenderChunkCount) {
    throw new Error(
      `${label} rendered ${debug.renderChunkKeys.length} terrain chunks; ` +
      `expected ${expectedRenderChunkCount}: ${JSON.stringify(debug)}`
    );
  }
}

function assertTerrainStreamed(initialTerrain, streamedTerrain) {
  if (initialTerrain.loadedChunkKeys.join("|") === streamedTerrain.loadedChunkKeys.join("|")) {
    throw new Error(
      `Terrain chunk keys did not change after moving player: ` +
      `${JSON.stringify({ initialTerrain, streamedTerrain })}`
    );
  }

  if (!streamedTerrain.loadedChunkKeys.includes("3,0,0")) {
    throw new Error(
      `Terrain did not stream the expected destination chunk: ${JSON.stringify(streamedTerrain)}`
    );
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
