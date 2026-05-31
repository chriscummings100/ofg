import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";
import { PNG } from "pngjs";
import { TERRAIN_DEBUG_OVERLAY_MODES } from "../dist/engine/world/terrainDebugOverlay.js";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_SMOKE_PORT ?? "5194", 10);
const headed = process.env.OFG_SMOKE_HEADED === "1";
const terrainPreset = process.env.OFG_TERRAIN_DEBUG_PRESET ?? "rockyHighland";
const artifactRoot = resolve(root, "artifacts", "terrain-debug-smoke");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);

mkdirSync(artifactDir, { recursive: true });

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  const baseUrl = `http://127.0.0.1:${port}/`;
  await waitForHttp(baseUrl);
  const result = await runTerrainDebugSmoke(baseUrl);
  writeFileSync(resolve(artifactDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);

  console.log("Terrain debug smoke passed.");
  console.log(`Artifacts: ${artifactDir}`);
  for (const overlay of result.overlays) {
    console.log(`Screenshot: ${overlay.screenshot}`);
  }
} finally {
  server.kill();
}

async function runTerrainDebugSmoke(baseUrl) {
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

    const url = `${baseUrl}?terrainPreset=${encodeURIComponent(terrainPreset)}` +
      `&terrainDebug=${encodeURIComponent(TERRAIN_DEBUG_OVERLAY_MODES[0])}`;
    await page.goto(url, { waitUntil: "load" });
    await waitForRenderedFrame(page);
    await page.waitForFunction(() => window.__ofgDebug !== undefined);
    await page.evaluate(() => window.__ofgDebug?.setPlayerPosition(64, 0));

    const overlays = [];
    const seenHashes = new Set();
    for (const mode of TERRAIN_DEBUG_OVERLAY_MODES) {
      await page.evaluate((overlayMode) =>
        window.__ofgDebug?.setTerrainDebugOverlayMode(overlayMode),
      mode);
      await page.waitForFunction(
        (overlayMode) => window.__ofgDebug?.getTerrainDebugOverlayMode?.() === overlayMode,
        mode,
        { timeout: 10000 }
      );
      await page.waitForTimeout(300);

      const screenshot = await saveOverlayScreenshot(page, `${mode}.png`);
      const stats = analyzePng(screenshot.buffer);
      assertPixelStats(stats, mode, consoleMessages);
      const hash = hashPixels(screenshot.buffer);
      seenHashes.add(hash);
      overlays.push({
        mode,
        screenshot: screenshot.path,
        pixelStats: stats,
        hash
      });
    }

    if (seenHashes.size < TERRAIN_DEBUG_OVERLAY_MODES.length - 1) {
      throw new Error(
        `Terrain debug overlays are not visually distinct enough: ` +
        `${JSON.stringify(overlays.map((overlay) => ({ mode: overlay.mode, hash: overlay.hash })))}`
      );
    }

    return {
      url,
      browserPath,
      headed,
      terrainPreset,
      overlays,
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
  }, null, { timeout: 10000 });
  await page.waitForTimeout(250);
}

async function saveOverlayScreenshot(page, fileName) {
  const path = resolve(artifactDir, fileName);
  const locator = page.locator("#terrain-debug-overlay");
  await locator.waitFor({ state: "visible", timeout: 10000 });
  const buffer = await locator.screenshot({ path });
  return { path, buffer };
}

function analyzePng(buffer) {
  const png = PNG.sync.read(buffer);
  const buckets = new Map();
  let sampledPixels = 0;
  let opaquePixels = 0;
  let sumR = 0;
  let sumG = 0;
  let sumB = 0;

  for (let y = 0; y < png.height; y += 2) {
    for (let x = 0; x < png.width; x += 2) {
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
      `${label} overlay is not mostly opaque: ${JSON.stringify(stats)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }

  if (stats.uniqueColorBuckets < 4) {
    throw new Error(
      `${label} overlay has too little color variation: ${JSON.stringify(stats)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }

  if (stats.dominantColorRatio > 0.96) {
    throw new Error(
      `${label} overlay looks like a solid fill: ${JSON.stringify(stats)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }
}

function hashPixels(buffer) {
  const png = PNG.sync.read(buffer);
  let hash = 2166136261;

  for (let offset = 0; offset < png.data.length; offset += 16) {
    hash ^= png.data[offset];
    hash = Math.imul(hash, 16777619);
    hash ^= png.data[offset + 1];
    hash = Math.imul(hash, 16777619);
    hash ^= png.data[offset + 2];
    hash = Math.imul(hash, 16777619);
  }

  return (hash >>> 0).toString(16);
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
