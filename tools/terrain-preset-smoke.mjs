import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";
import { PNG } from "pngjs";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_SMOKE_PORT ?? "5184", 10);
const headed = process.env.OFG_SMOKE_HEADED === "1";
const artifactRoot = resolve(root, "artifacts", "terrain-preset-smoke");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);
const terrainPresets = ["seed", "rollingHills", "mountainValley", "rockyHighland"];

mkdirSync(artifactDir, { recursive: true });

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  const baseUrl = `http://127.0.0.1:${port}/`;
  await waitForHttp(baseUrl);
  const result = await runTerrainPresetSmoke(baseUrl);
  writeFileSync(resolve(artifactDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);

  console.log("Terrain preset smoke passed.");
  console.log(`Artifacts: ${artifactDir}`);
  for (const preset of result.presets) {
    console.log(`Screenshot: ${preset.screenshot}`);
  }
} finally {
  server.kill();
}

async function runTerrainPresetSmoke(baseUrl) {
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
    const presets = [];
    for (const terrainPreset of terrainPresets) {
      let page;
      try {
        page = await browser.newPage({
          viewport: { width: 1280, height: 720 },
          deviceScaleFactor: 1
        });
        page.on("console", (message) => {
          consoleMessages.push(`${terrainPreset} ${message.type()}: ${message.text()}`);
        });
        page.on("pageerror", (error) => {
          consoleMessages.push(`${terrainPreset} pageerror: ${error.message}`);
        });

        const url = `${baseUrl}?terrainPreset=${encodeURIComponent(terrainPreset)}`;
        await page.goto(url, { waitUntil: "load" });
        await waitForRenderedFrame(page);
        await page.waitForFunction(
          (expectedPreset) => window.__ofgDebug?.getTerrainPreset?.() === expectedPreset,
          terrainPreset,
          { timeout: 10000 }
        );
        await page.evaluate(() => window.__ofgDebug?.setPlayerPosition(64, 0));
        await page.waitForFunction(() =>
          (window.__ofgDebug?.getTerrainChunkKeys?.() ?? []).length > 0
        );
        await page.waitForTimeout(250);

        const debug = await readTerrainDebug(page);
        assertTerrainDebug(debug, terrainPreset);
        const screenshot = await saveScreenshot(page, `${terrainPreset}.png`);
        assertPixelStats(screenshot.stats, terrainPreset, consoleMessages);
        presets.push({
          terrainPreset,
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
          `Terrain preset smoke failed for ${terrainPreset}: ${message}. ` +
          `HUD=${JSON.stringify(hud)} console=${JSON.stringify(consoleMessages)}`
        );
      } finally {
        await page?.close().catch(() => undefined);
      }
    }

    return {
      baseUrl,
      browserPath,
      headed,
      presets,
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
    hasDebug: window.__ofgDebug !== undefined,
    hasWebGpu: navigator.gpu !== undefined
  }));
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
    terrainPreset: window.__ofgDebug?.getTerrainPreset?.() ?? "",
    loadedChunkKeys: window.__ofgDebug?.getLoadedTerrainChunkKeys?.() ?? [],
    renderChunkKeys: window.__ofgDebug?.getTerrainChunkKeys?.() ?? []
  }));
}

function assertTerrainDebug(debug, expectedTerrainPreset) {
  if (debug.terrainPreset !== expectedTerrainPreset) {
    throw new Error(
      `Expected terrain preset ${expectedTerrainPreset}, saw ` +
      `${debug.terrainPreset}: ${JSON.stringify(debug)}`
    );
  }

  if (debug.loadedChunkKeys.length === 0) {
    throw new Error(`${expectedTerrainPreset} has no loaded terrain chunks.`);
  }

  if (debug.renderChunkKeys.length === 0) {
    throw new Error(`${expectedTerrainPreset} has no rendered terrain chunks.`);
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
