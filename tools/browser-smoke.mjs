// Playwright Core smoke test for the browser WebGPU bootstrap. It verifies
// headers, runtime status, backing size, and rendered pixel coverage.

import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { spawn } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { PNG } from "pngjs";
import { chromium } from "playwright-core";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const smokeContract = JSON.parse(
  readFileSync(resolve(root, "tools/smoke-contract.json"), "utf8")
);
const artifactsDir = resolve(root, "artifacts/browser-smoke");
const screenshotPath = resolve(artifactsDir, "bootstrap.png");
const reportPath = resolve(artifactsDir, "report.json");

mkdirSync(artifactsDir, { recursive: true });

const server = spawn(process.execPath, ["tools/dev-server.mjs"], {
  cwd: root,
  stdio: ["ignore", "pipe", "pipe"]
});
let browser;

try {
  const url = await waitForServerUrl(server);
  const browserPath = findBrowserPath();
  browser = await chromium.launch({
    executablePath: browserPath,
    headless: process.env.OFG_SMOKE_HEADED !== "1",
    args: ["--enable-unsafe-webgpu"]
  });
  const page = await browser.newPage({
    viewport: { width: smokeContract.width, height: smokeContract.height },
    deviceScaleFactor: 1
  });
  const response = await page.goto(url, { waitUntil: "load" });
  if (response === null || !response.ok()) {
    throw new Error(`Browser smoke failed to load ${url}: ${response?.status()}`);
  }

  const headers = response.headers();
  assertHeader(headers, "cross-origin-embedder-policy", "require-corp");
  assertHeader(headers, "cross-origin-opener-policy", "same-origin");
  assertHeader(headers, "cross-origin-resource-policy", "same-origin");

  const browserSignals = await page.evaluate(() => ({
    webgpu: "gpu" in navigator,
    isolated: globalThis.crossOriginIsolated === true
  }));
  if (!browserSignals.webgpu) {
    throw new Error("Browser smoke requires navigator.gpu.");
  }
  if (!browserSignals.isolated) {
    throw new Error("Browser smoke requires crossOriginIsolated.");
  }

  await page.waitForFunction(
    () => {
      const status = window.__ofgDebugStatus?.();
      return status?.initialized === true && status.frameCount >= 2;
    },
    undefined,
    { timeout: 15000 }
  );
  const initialDebugStatus = await page.evaluate(() => window.__ofgDebugStatus?.() ?? null);
  await page.setViewportSize({
    width: smokeContract.resizeProbeWidth,
    height: smokeContract.resizeProbeHeight
  });
  await page.waitForFunction(
    (expected) => {
      const status = window.__ofgDebugStatus?.();
      return (
        status?.canvasWidth === expected.width &&
        status.canvasHeight === expected.height
      );
    },
    {
      width: smokeContract.resizeProbeWidth,
      height: smokeContract.resizeProbeHeight
    },
    { timeout: 15000 }
  );
  const resizedDebugStatus = await page.evaluate(() => window.__ofgDebugStatus?.() ?? null);
  await page.setViewportSize({ width: smokeContract.width, height: smokeContract.height });
  await page.waitForFunction(
    (expected) => {
      const status = window.__ofgDebugStatus?.();
      return (
        status?.canvasWidth === expected.width &&
        status.canvasHeight === expected.height
      );
    },
    {
      width: smokeContract.width,
      height: smokeContract.height
    },
    { timeout: 15000 }
  );
  await page.locator("#ofg-status").evaluate((element) => {
    element.setAttribute("hidden", "true");
  });
  const canvas = page.locator("#ofg-canvas");
  const box = await canvas.boundingBox();
  if (box === null) {
    throw new Error("Browser smoke could not locate the canvas bounds.");
  }
  const backingSize = await canvas.evaluate((element) => ({
    width: element.width,
    height: element.height
  }));
  if (backingSize.width !== smokeContract.width || backingSize.height !== smokeContract.height) {
    throw new Error(
      `Expected ${smokeContract.width}x${smokeContract.height} canvas backing size, got ${backingSize.width}x${backingSize.height}.`
    );
  }
  await canvas.screenshot({ path: screenshotPath });
  const pixelReport = inspectScreenshot(screenshotPath);
  const debugStatus = await page.evaluate(() => window.__ofgDebugStatus?.() ?? null);
  const report = {
    url,
    screenshotPath,
    headers,
    browserSignals,
    smokeContract,
    resizeProbe: {
      initialDebugStatus,
      resizedDebugStatus
    },
    canvasBounds: box,
    backingSize,
    debugStatus,
    pixels: pixelReport
  };
  writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
} finally {
  await browser?.close().catch((error) => {
    console.warn(`Failed to close browser cleanly: ${error}`);
  });
  server.kill();
}

function waitForServerUrl(child) {
  return new Promise((resolveUrl, reject) => {
    const timeout = setTimeout(() => {
      reject(new Error("Timed out waiting for dev server URL."));
    }, 15000);

    child.stdout.on("data", (chunk) => {
      const text = chunk.toString();
      const match = text.match(/http:\/\/127\.0\.0\.1:\d+/);
      if (match !== null) {
        clearTimeout(timeout);
        resolveUrl(match[0]);
      }
    });
    child.stderr.on("data", (chunk) => {
      process.stderr.write(chunk);
    });
    child.on("exit", (code) => {
      reject(new Error(`Dev server exited before smoke could run: ${code}`));
    });
  });
}

function findBrowserPath() {
  if (process.env.OFG_BROWSER_PATH !== undefined) {
    return process.env.OFG_BROWSER_PATH;
  }

  const candidates = [
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
  ];
  const found = candidates.find((candidate) => existsSync(candidate));
  if (found === undefined) {
    throw new Error("Set OFG_BROWSER_PATH to a Chromium-based browser executable.");
  }
  return found;
}

function assertHeader(headers, name, expected) {
  if (headers[name] !== expected) {
    throw new Error(`Expected ${name}: ${expected}; got ${headers[name] ?? "<missing>"}.`);
  }
}

function inspectScreenshot(path) {
  const png = PNG.sync.read(readFileSync(path));
  let backgroundPixels = 0;
  let trianglePixels = 0;
  const buckets = new Set();

  for (let y = 0; y < png.height; y += smokeContract.sampleStep) {
    for (let x = 0; x < png.width; x += smokeContract.sampleStep) {
      const index = (png.width * y + x) << 2;
      const pixel = [
        png.data[index],
        png.data[index + 1],
        png.data[index + 2],
        png.data[index + 3]
      ];
      if (
        colorDistance(pixel, smokeContract.clearColorRgba8) <=
        smokeContract.colorDistanceTolerance
      ) {
        backgroundPixels += 1;
      } else {
        trianglePixels += 1;
        buckets.add(
          `${Math.floor(pixel[0] / smokeContract.bucketDivisor)}:${Math.floor(pixel[1] / smokeContract.bucketDivisor)}:${Math.floor(pixel[2] / smokeContract.bucketDivisor)}`
        );
      }
    }
  }

  const sampledPixels = backgroundPixels + trianglePixels;
  const triangleRatio = trianglePixels / sampledPixels;
  const backgroundRatio = backgroundPixels / sampledPixels;
  if (triangleRatio < smokeContract.minTriangleRatio) {
    throw new Error(`Triangle coverage too low: ${triangleRatio}`);
  }
  if (backgroundRatio < smokeContract.minBackgroundRatio) {
    throw new Error(`Background coverage too low: ${backgroundRatio}`);
  }
  if (buckets.size < smokeContract.minNonBackgroundColorBuckets) {
    throw new Error(`Expected at least 3 non-background color buckets; got ${buckets.size}.`);
  }

  return {
    width: png.width,
    height: png.height,
    sampledPixels,
    trianglePixels,
    backgroundPixels,
    triangleRatio,
    backgroundRatio,
    nonBackgroundColorBuckets: buckets.size
  };
}

function colorDistance(left, right) {
  const dr = left[0] - right[0];
  const dg = left[1] - right[1];
  const db = left[2] - right[2];
  return Math.sqrt(dr * dr + dg * dg + db * db);
}
