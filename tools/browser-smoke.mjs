// Playwright Core smoke test for the browser WebGPU demo scene. It verifies
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
const screenshotPath = resolve(artifactsDir, "opaque-demo.png");
const debugModeScreenshotPath = resolve(artifactsDir, "player-box-debug.png");
const firstPersonScreenshotPath = resolve(artifactsDir, "first-person-mode.png");
const thirdPersonScreenshotPath = resolve(artifactsDir, "third-person-mode.png");
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
  await page.waitForFunction(
    () => {
      const status = window.__ofgDebugStatus?.();
      return status?.playerModelLoaded === true && status.modelLoadingState === "loaded";
    },
    undefined,
    { timeout: 60000 }
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
  const warmCounters = await page.evaluate(() => {
    const status = window.__ofgDebugStatus?.();
    if (status === undefined || status === null) {
      throw new Error("Runtime debug status is unavailable.");
    }
    return {
      pipelineCreateCount: status.pipelineCreateCount,
      bufferCreateCount: status.bufferCreateCount,
      cameraMode: status.cameraMode
    };
  });
  if (warmCounters.cameraMode !== "debug") {
    throw new Error(`Expected debug camera mode after warmup, got ${warmCounters.cameraMode}.`);
  }
  await canvas.screenshot({ path: debugModeScreenshotPath });
  await dispatchKeyCode(page, "Backquote", "`");
  await waitForCameraMode(page, "first_person");
  await dispatchKeyDown(page, "KeyW", "w");
  await waitForAnimationFrames(page, 8);
  await dispatchKeyUp(page, "KeyW", "w");
  await waitForAnimationFrames(page, 2);
  await canvas.screenshot({ path: firstPersonScreenshotPath });
  await dispatchKeyCode(page, "Backquote", "`");
  await waitForCameraMode(page, "third_person");
  await waitForAnimationFrames(page, 4);
  await canvas.screenshot({ path: thirdPersonScreenshotPath });
  await dispatchKeyCode(page, "Backquote", "`");
  await waitForCameraMode(page, "debug");
  await waitForAnimationFrames(page, 2);
  const modeExerciseStatus = await page.evaluate(() => window.__ofgDebugStatus?.() ?? null);
  assertStableRendererCounters(warmCounters, modeExerciseStatus);
  await canvas.screenshot({ path: screenshotPath });
  const pixelReport = inspectSceneScreenshot(screenshotPath);
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
    modeExercise: {
      warmCounters,
      modeExerciseStatus,
      debugModeScreenshotPath,
      firstPersonScreenshotPath,
      thirdPersonScreenshotPath
    },
    pixels: pixelReport
  };
  writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
} finally {
  await browser?.close().catch((error) => {
    console.warn(`Failed to close browser cleanly: ${error}`);
  });
  server.kill();
}

// Dispatches a keydown/keyup pair with a stable KeyboardEvent.code value.
async function dispatchKeyCode(page, code, key) {
  await dispatchKeyDown(page, code, key);
  await dispatchKeyUp(page, code, key);
}

// Dispatches one keydown event at the window listener used by the app.
async function dispatchKeyDown(page, code, key) {
  await page.evaluate(
    ({ code: eventCode, key: eventKey }) => {
      window.dispatchEvent(
        new KeyboardEvent("keydown", {
          code: eventCode,
          key: eventKey,
          bubbles: true
        })
      );
    },
    { code, key }
  );
}

// Dispatches one keyup event at the window listener used by the app.
async function dispatchKeyUp(page, code, key) {
  await page.evaluate(
    ({ code: eventCode, key: eventKey }) => {
      window.dispatchEvent(
        new KeyboardEvent("keyup", {
          code: eventCode,
          key: eventKey,
          bubbles: true
        })
      );
    },
    { code, key }
  );
}

// Waits until the runtime reports a specific camera mode.
async function waitForCameraMode(page, cameraMode) {
  await page.waitForFunction(
    (expectedMode) => window.__ofgDebugStatus?.().cameraMode === expectedMode,
    cameraMode,
    { timeout: 5000 }
  );
}

// Waits for a fixed number of browser animation frames.
async function waitForAnimationFrames(page, frameCount) {
  await page.evaluate(
    (count) =>
      new Promise((resolveFrame) => {
        let remaining = count;
        function nextFrame() {
          remaining -= 1;
          if (remaining <= 0) {
            resolveFrame();
            return;
          }
          requestAnimationFrame(nextFrame);
        }
        requestAnimationFrame(nextFrame);
      }),
    frameCount
  );
}

// Verifies mode cycling and movement did not create steady-state renderer resources.
function assertStableRendererCounters(before, after) {
  if (after === null) {
    throw new Error("Runtime debug status is unavailable after mode exercise.");
  }
  if (after.pipelineCreateCount !== before.pipelineCreateCount) {
    throw new Error(
      `Pipeline count changed after mode exercise: ${before.pipelineCreateCount} -> ${after.pipelineCreateCount}.`
    );
  }
  if (after.bufferCreateCount !== before.bufferCreateCount) {
    throw new Error(
      `Buffer count changed after mode exercise: ${before.bufferCreateCount} -> ${after.bufferCreateCount}.`
    );
  }
}

// Waits for the local dev server to print its review URL.
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

// Finds the Chromium-family browser used for WebGPU smoke tests.
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

// Asserts that a required deployment/security header is present.
function assertHeader(headers, name, expected) {
  if (headers[name] !== expected) {
    throw new Error(`Expected ${name}: ${expected}; got ${headers[name] ?? "<missing>"}.`);
  }
}

// Samples the screenshot and verifies it matches the shared scene contract.
function inspectSceneScreenshot(path) {
  const png = PNG.sync.read(readFileSync(path));
  let backgroundPixels = 0;
  let scenePixels = 0;
  let groundPixels = 0;
  let coloredPixels = 0;
  let lowerHalfSampledPixels = 0;
  let lowerHalfScenePixels = 0;
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
      if (y >= png.height / 2) {
        lowerHalfSampledPixels += 1;
      }
      if (
        colorDistance(pixel, smokeContract.clearColorRgba8) <=
        smokeContract.colorDistanceTolerance
      ) {
        backgroundPixels += 1;
      } else {
        scenePixels += 1;
        if (y >= png.height / 2) {
          lowerHalfScenePixels += 1;
        }
        if (isGroundLikePixel(pixel)) {
          groundPixels += 1;
        } else {
          coloredPixels += 1;
        }
        buckets.add(
          `${Math.floor(pixel[0] / smokeContract.bucketDivisor)}:${Math.floor(pixel[1] / smokeContract.bucketDivisor)}:${Math.floor(pixel[2] / smokeContract.bucketDivisor)}`
        );
      }
    }
  }

  const sampledPixels = backgroundPixels + scenePixels;
  const sceneRatio = scenePixels / sampledPixels;
  const backgroundRatio = backgroundPixels / sampledPixels;
  const groundRatio = groundPixels / sampledPixels;
  const coloredRatio = coloredPixels / sampledPixels;
  const lowerHalfSceneRatio =
    lowerHalfSampledPixels === 0 ? 0 : lowerHalfScenePixels / lowerHalfSampledPixels;
  if (sceneRatio < smokeContract.minSceneRatio) {
    throw new Error(`Scene coverage too low: ${sceneRatio}`);
  }
  if (backgroundRatio < smokeContract.minBackgroundRatio) {
    throw new Error(`Background coverage too low: ${backgroundRatio}`);
  }
  if (groundRatio < smokeContract.minGroundRatio) {
    throw new Error(`Ground coverage too low: ${groundRatio}`);
  }
  if (coloredRatio < smokeContract.minColoredRatio) {
    throw new Error(`Colored cube coverage too low: ${coloredRatio}`);
  }
  if (lowerHalfSceneRatio < smokeContract.minLowerHalfSceneRatio) {
    throw new Error(`Lower-half scene coverage too low: ${lowerHalfSceneRatio}`);
  }
  if (buckets.size < smokeContract.minNonBackgroundColorBuckets) {
    throw new Error(
      `Expected at least ${smokeContract.minNonBackgroundColorBuckets} non-background color buckets; got ${buckets.size}.`
    );
  }

  return {
    width: png.width,
    height: png.height,
    sampledPixels,
    scenePixels,
    backgroundPixels,
    groundPixels,
    coloredPixels,
    lowerHalfSampledPixels,
    lowerHalfScenePixels,
    sceneRatio,
    backgroundRatio,
    groundRatio,
    coloredRatio,
    lowerHalfSceneRatio,
    nonBackgroundColorBuckets: buckets.size
  };
}

// Reports whether a non-background pixel looks like neutral checker ground.
function isGroundLikePixel(pixel) {
  const maxChannel = Math.max(pixel[0], pixel[1], pixel[2]);
  const minChannel = Math.min(pixel[0], pixel[1], pixel[2]);
  const brightness = pixel[0] + pixel[1] + pixel[2];
  return maxChannel - minChannel <= 30 && brightness >= 90 && brightness <= 690;
}

// Computes RGB distance while ignoring alpha, matching native smoke behavior.
function colorDistance(left, right) {
  const dr = left[0] - right[0];
  const dg = left[1] - right[1];
  const db = left[2] - right[2];
  return Math.sqrt(dr * dr + dg * dg + db * db);
}
