// Browser integration smoke for OFG. It validates that the browser shell can
// load engine_web.wasm, initialize WebGPU, render nonblank frames, forward a
// keyboard command, survive reload, and expose only black-box debug sentinels.

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

mkdirSync(artifactDir, { recursive: true });

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  await waitForHttp(`http://127.0.0.1:${port}/`);
  const result = await runBrowserSmoke(`http://127.0.0.1:${port}/`);
  writeFileSync(resolve(artifactDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);

  console.log("Browser integration smoke passed.");
  console.log(`Artifacts: ${artifactDir}`);
  for (const image of result.images) {
    console.log(`Screenshot: ${image.path}`);
  }
} finally {
  server.kill();
}

/// Runs the browser-only integration smoke scenario.
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
    assertNoBrowserFailures(consoleMessages);

    const firstHud = await readHud(page);
    assertHud(firstHud, "FIRST", consoleMessages);
    const firstDebug = await readDebugContract(page);
    assertDebugContract(firstDebug);
    const firstImage = await saveScreenshot(page, "browser-first-person.png");
    assertPixelStats(firstImage.pixelStats, "browser first-person", consoleMessages);

    await page.keyboard.press("KeyC");
    await page.waitForFunction(() => document.querySelector("#camera-mode")?.textContent === "THIRD");
    await waitForBrowserFrame(page);
    assertNoBrowserFailures(consoleMessages);
    const toggledHud = await readHud(page);
    assertHud(toggledHud, "THIRD", consoleMessages);
    const toggledDebug = await readDebugContract(page);
    assertDebugContract(toggledDebug);
    const toggledImage = await saveScreenshot(page, "browser-camera-toggle.png");
    assertPixelStats(toggledImage.pixelStats, "browser camera toggle", consoleMessages);

    const reloadResponse = await page.reload({ waitUntil: "load" });
    assertResponseHeaders(reloadResponse);
    await waitForBrowserFrame(page);
    assertNoBrowserFailures(consoleMessages);
    const reloadedHud = await readHud(page);
    assertHud(reloadedHud, "FIRST", consoleMessages);
    const reloadedDebug = await readDebugContract(page);
    assertDebugContract(reloadedDebug);
    const reloadedImage = await saveScreenshot(page, "browser-reloaded.png");
    assertPixelStats(reloadedImage.pixelStats, "browser reload", consoleMessages);

    return {
      kind: "browser-integration-smoke",
      url,
      artifactDir: reportPath(artifactDir),
      browserPath,
      headed,
      images: [firstImage, toggledImage, reloadedImage],
      firstHud,
      toggledHud,
      reloadedHud,
      firstDebug,
      toggledDebug,
      reloadedDebug,
      consoleMessages
    };
  } finally {
    await browser.close();
  }
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
      status.frameDrawCount > 0;
  }, null, { timeout: 10000 });
  await page.waitForTimeout(250);
}

/// Reads browser HUD values relevant to shell integration.
async function readHud(page) {
  return page.evaluate(() => ({
    cameraMode: document.querySelector("#camera-mode")?.textContent ?? "",
    frameTime: document.querySelector("#frame-time")?.textContent ?? "",
    hasWebGpu: navigator.gpu !== undefined,
    crossOriginIsolated: globalThis.crossOriginIsolated === true,
    sharedArrayBufferAvailable: typeof SharedArrayBuffer !== "undefined",
    canvasWidth: document.querySelector("canvas")?.width ?? 0,
    canvasHeight: document.querySelector("canvas")?.height ?? 0
  }));
}

/// Reads black-box Rust runtime sentinels from the debug API.
async function readDebugContract(page) {
  return page.evaluate(() => {
    const debug = window.__ofgDebug;
    const status = debug?.getRendererStatus?.();

    return {
      hasDebug: debug !== undefined,
      apiKeys: debug === undefined ? [] : Object.keys(debug).sort(),
      playerControllerRuntime: debug?.getPlayerControllerRuntime?.() ?? "missing",
      renderPacketRuntime: debug?.getRenderPacketRuntime?.() ?? "missing",
      terrainStreamerRuntime: debug?.getTerrainStreamerRuntime?.() ?? "missing",
      terrainStreamSchedulerRuntime: debug?.getTerrainStreamSchedulerRuntime?.() ?? "missing",
      terrainDensityStoreRuntime: debug?.getTerrainDensityStoreRuntime?.() ?? "missing",
      terrainRenderPacketRuntime: debug?.getTerrainRenderPacketRuntime?.() ?? "missing",
      rendererRuntime: debug?.getRendererRuntime?.() ?? "missing",
      rendererStatus: status === undefined
        ? undefined
        : {
            version: status.version,
            runtime: status.runtime,
            configured: status.configured,
            canvasWidth: status.canvasWidth,
            canvasHeight: status.canvasHeight,
            meshCount: status.meshCount,
            textureCount: status.textureCount,
            objectCount: status.objectCount,
            frameIndex: status.frameIndex.toString(),
            frameDrawCount: status.frameDrawCount,
            requiredTextureArrayLayers: status.requiredTextureArrayLayers,
            maxTextureArrayLayers: status.maxTextureArrayLayers
          }
    };
  });
}

/// Validates dev-server headers required for wasm threads and WebGPU assets.
function assertResponseHeaders(response) {
  if (response === null) {
    throw new Error("Browser smoke did not receive a page response.");
  }

  const headers = response.headers();
  if (headers["cross-origin-opener-policy"] !== "same-origin") {
    throw new Error(`Missing COOP header: ${JSON.stringify(headers)}`);
  }
  if (headers["cross-origin-embedder-policy"] !== "require-corp") {
    throw new Error(`Missing COEP header: ${JSON.stringify(headers)}`);
  }
}

/// Validates HUD state after a browser-rendered frame.
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
  if (!hud.crossOriginIsolated || !hud.sharedArrayBufferAvailable) {
    throw new Error(`Browser shell is not cross-origin isolated: ${JSON.stringify(hud)}`);
  }
  if (hud.canvasWidth <= 0 || hud.canvasHeight <= 0) {
    throw new Error(`Canvas has invalid dimensions: ${hud.canvasWidth}x${hud.canvasHeight}`);
  }
}

/// Validates that debug hooks are black-box integration hooks only.
function assertDebugContract(debug) {
  if (!debug.hasDebug) {
    throw new Error("Debug API is unavailable.");
  }

  const expectedRustSentinels = {
    playerControllerRuntime: "rust",
    renderPacketRuntime: "rust",
    terrainStreamerRuntime: "rust",
    terrainStreamSchedulerRuntime: "rust",
    terrainDensityStoreRuntime: "rust",
    terrainRenderPacketRuntime: "rust",
    rendererRuntime: "rust-wgpu"
  };
  for (const [key, expected] of Object.entries(expectedRustSentinels)) {
    if (debug[key] !== expected) {
      throw new Error(`Expected ${key}=${expected}, saw ${debug[key]}: ${JSON.stringify(debug)}`);
    }
  }

  const forbiddenDebugNames = [
    "terrainCoreWasm",
    "DensityChunkBuffer",
    "MeshBuffer",
    "RawTerrain",
    "TerrainGenerator"
  ];
  const forbiddenMatches = debug.apiKeys.filter((key) =>
    forbiddenDebugNames.some((forbidden) => key.includes(forbidden))
  );
  if (forbiddenMatches.length > 0) {
    throw new Error(`Debug API exposes terrain internals: ${forbiddenMatches.join(", ")}`);
  }

  const status = debug.rendererStatus;
  if (
    status === undefined ||
    !status.configured ||
    status.runtime !== "rust-wgpu" ||
    status.frameDrawCount <= 0 ||
    status.meshCount <= 0 ||
    status.textureCount <= 0 ||
    status.objectCount <= 0 ||
    status.requiredTextureArrayLayers !== 16 ||
    status.maxTextureArrayLayers < status.requiredTextureArrayLayers
  ) {
    throw new Error(`Renderer status is not a valid Rust/wgpu frame: ${JSON.stringify(debug)}`);
  }
}

/// Saves a browser screenshot and computes pixel statistics for it.
async function saveScreenshot(page, fileName) {
  const path = resolve(artifactDir, fileName);
  const buffer = await page.screenshot({ path, fullPage: false });
  return {
    name: fileName.replace(/\.png$/, ""),
    path: reportPath(path),
    width: page.viewportSize()?.width ?? 0,
    height: page.viewportSize()?.height ?? 0,
    pixelStats: analyzePng(buffer)
  };
}

/// Computes screenshot pixel statistics.
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

/// Fails when a screenshot looks blank, transparent, or solid.
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

/// Normalizes a path for JSON reports and console output.
function reportPath(path) {
  return path.replace(/\\/g, "/");
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

/// Resolves after the requested delay.
function sleep(ms) {
  return new Promise((resolveSleep) => setTimeout(resolveSleep, ms));
}
