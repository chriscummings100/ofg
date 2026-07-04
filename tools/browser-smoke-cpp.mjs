// Focused Playwright smoke for the C++/WASM WebGPU runtime fixture.
// It proves canvas WebGPU setup and demo-scene rendering.

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
const artifactsDir = resolve(root, "artifacts/browser-smoke-cpp");
const reportPath = resolve(artifactsDir, "webgpu-init-report.json");
const screenshotPath = resolve(artifactsDir, "scene.png");
const width = 640;
const height = 360;
const resizeWidth = 320;
const resizeHeight = 180;

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
    viewport: { width, height },
    deviceScaleFactor: 1
  });
  const response = await page.goto(`${url}/tools/cpp-webgpu-smoke.html`, {
    waitUntil: "load"
  });
  if (response === null || !response.ok()) {
    throw new Error(`C++ WebGPU smoke failed to load fixture: ${response?.status()}`);
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
    throw new Error("C++ WebGPU smoke requires navigator.gpu.");
  }
  if (!browserSignals.isolated) {
    throw new Error("C++ WebGPU smoke requires crossOriginIsolated.");
  }

  await page.evaluate(async ({ width: canvasWidth, height: canvasHeight }) => {
    const canvas = document.querySelector("#ofg-cpp-smoke-canvas");
    if (!(canvas instanceof HTMLCanvasElement)) {
      throw new Error("C++ smoke fixture canvas was not found.");
    }
    canvas.width = canvasWidth;
    canvas.height = canvasHeight;

    const moduleFactory = (await import("/assets/wasm/ofg_cpp/ofg_cpp.js")).default;
    const module = await moduleFactory({
      locateFile(path) {
        return `/assets/wasm/ofg_cpp/${path}`;
      }
    });
    const game = await Promise.resolve(module.BrowserGame.create(canvas));
    globalThis.__ofgCppGame = game;
    game.resize(canvasWidth, canvasHeight, 1);
  }, { width, height });

  await waitForCppStatus(page, {
    initialized: true,
    canvasWidth: width,
    canvasHeight: height,
    backend: "BrowserWebGpu",
    adapterNameNot: "Unavailable",
    surfaceFormatNot: "Unavailable",
    minSurfaceConfigureCount: 1,
    lastError: null
  });
  const initialStatus = await readCppStatus(page);
  assertRendererCounters(initialStatus);

  await page.evaluate(({ width: canvasWidth, height: canvasHeight }) => {
    globalThis.__ofgCppGame.resize(canvasWidth, canvasHeight, 1);
  }, { width: resizeWidth, height: resizeHeight });
  await waitForCppStatus(page, {
    initialized: true,
    canvasWidth: resizeWidth,
    canvasHeight: resizeHeight,
    surfaceConfigureCountGreaterThan: initialStatus.surfaceConfigureCount,
    lastError: null
  });
  const resizedStatus = await readCppStatus(page);
  assertRendererCounters(resizedStatus);

  await page.evaluate(() => {
    globalThis.__ofgCppGame.resize(0, 180, 1);
  });
  const zeroSizeStatus = await readCppStatus(page);
  if (
    zeroSizeStatus.initialized !== false ||
    zeroSizeStatus.canvasWidth !== 0 ||
    zeroSizeStatus.lastError !== null
  ) {
    throw new Error(`Unexpected zero-size C++ status: ${JSON.stringify(zeroSizeStatus)}`);
  }
  assertRendererCounters(zeroSizeStatus);

  await page.evaluate(({ width: canvasWidth, height: canvasHeight }) => {
    globalThis.__ofgCppGame.resize(canvasWidth, canvasHeight, 1);
  }, { width, height });
  await waitForCppStatus(page, {
    initialized: true,
    canvasWidth: width,
    canvasHeight: height,
    surfaceConfigureCountGreaterThan: resizedStatus.surfaceConfigureCount,
    lastError: null
  });
  const recoveredStatus = await readCppStatus(page);
  assertRendererCounters(recoveredStatus);
  await page.evaluate(() => new Promise((resolveFrame) => {
    requestAnimationFrame((timeMs) => {
      globalThis.__ofgCppGame.frame(timeMs);
      requestAnimationFrame(resolveFrame);
    });
  }));
  const canvas = page.locator("#ofg-cpp-smoke-canvas");
  await canvas.screenshot({ path: screenshotPath });
  const pixelReport = inspectSceneScreenshot(screenshotPath);

  await page.evaluate(() => {
    globalThis.__ofgCppGame.dispose();
    globalThis.__ofgCppGame.delete();
  });

  writeFileSync(
    reportPath,
    `${JSON.stringify(
      {
        url,
        headers,
        browserSignals,
        initialStatus,
        resizedStatus,
        zeroSizeStatus,
        recoveredStatus,
        screenshotPath,
        pixels: pixelReport
      },
      null,
      2
    )}\n`
  );
} finally {
  await browser?.close().catch((error) => {
    console.warn(`Failed to close browser cleanly: ${error}`);
  });
  server.kill();
}

// Polls the fixture until the C++ runtime debug status satisfies expectations.
async function waitForCppStatus(page, expected) {
  await page.waitForFunction(
    (requirements) => {
      const game = globalThis.__ofgCppGame;
      if (game === undefined) {
        return false;
      }
      game.frame(performance.now());
      const status = JSON.parse(game.debug_status_json());
      globalThis.__ofgCppStatus = status;
      return matchesCppStatus(status, requirements);

      // Checks the subset of debug-status fields required by this wait.
      function matchesCppStatus(value, expected) {
        if (
          "initialized" in expected &&
          value.initialized !== expected.initialized
        ) {
          return false;
        }
        if (
          "canvasWidth" in expected &&
          value.canvasWidth !== expected.canvasWidth
        ) {
          return false;
        }
        if (
          "canvasHeight" in expected &&
          value.canvasHeight !== expected.canvasHeight
        ) {
          return false;
        }
        if ("backend" in expected && value.backend !== expected.backend) {
          return false;
        }
        if (
          "adapterNameNot" in expected &&
          value.adapterName === expected.adapterNameNot
        ) {
          return false;
        }
        if (
          "surfaceFormatNot" in expected &&
          value.surfaceFormat === expected.surfaceFormatNot
        ) {
          return false;
        }
        if (
          "minSurfaceConfigureCount" in expected &&
          value.surfaceConfigureCount < expected.minSurfaceConfigureCount
        ) {
          return false;
        }
        if (
          "surfaceConfigureCountGreaterThan" in expected &&
          value.surfaceConfigureCount <= expected.surfaceConfigureCountGreaterThan
        ) {
          return false;
        }
        if ("lastError" in expected && value.lastError !== expected.lastError) {
          return false;
        }
        return true;
      }
    },
    expected,
    { timeout: 15000 }
  );
}

// Reads the latest C++ runtime debug status from the browser fixture.
async function readCppStatus(page) {
  return page.evaluate(() => {
    const game = globalThis.__ofgCppGame;
    if (game === undefined) {
      throw new Error("C++ smoke runtime was not created.");
    }
    game.frame(performance.now());
    return JSON.parse(game.debug_status_json());
  });
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
      reject(new Error(`Dev server exited before C++ smoke could run: ${code}`));
    });
  });
}

// Finds the Chromium-family browser used for focused WebGPU smoke tests.
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

// Asserts that deployment/security headers required for WebGPU are present.
function assertHeader(headers, name, expected) {
  if (headers[name] !== expected) {
    throw new Error(`Expected ${name}: ${expected}; got ${headers[name] ?? "<missing>"}.`);
  }
}

// Verifies durable renderer resources were created once, not per frame.
function assertRendererCounters(status) {
  if (status.pipelineCreateCount < 1 || status.bufferCreateCount < 1) {
    throw new Error(
      `Expected initialized durable renderer resources; got ${JSON.stringify(status)}.`
    );
  }
}

// Samples the screenshot and verifies it matches the shared scene contract.
function inspectSceneScreenshot(path) {
  const png = PNG.sync.read(readFileSync(path));
  const backgroundReferenceRgba8 =
    smokeContract.backgroundReferenceRgba8 ?? smokeContract.clearColorRgba8;
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
        colorDistance(pixel, backgroundReferenceRgba8) <=
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
