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
const maxDefaultTempBufferBytes = 16 * 1024 * 1024;
const debugUiSampleExclusion = {
  width: 320
};

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
      cameraMode: status.cameraMode,
      bloomActiveLevelCount: status.bloomActiveLevelCount,
      bloomEncodedPassCount: status.bloomEncodedPassCount,
      bloomEstimatedReadBytes: status.bloomEstimatedReadBytes,
      bloomEstimatedWriteBytes: status.bloomEstimatedWriteBytes,
      bloomSkipped: status.bloomSkipped,
      debugUi: status.debugUi,
      shadow: status.shadow,
      tempBufferPeakBytes: status.tempBufferPeakBytes,
      tempBufferReusableCount: status.tempBufferReusableCount
    };
  });
  if (warmCounters.cameraMode !== "debug") {
    throw new Error(`Expected debug camera mode after warmup, got ${warmCounters.cameraMode}.`);
  }
  assertBloomDiagnostics(warmCounters);
  assertDebugUiDiagnostics(warmCounters.debugUi);
  assertShadowDiagnostics(warmCounters.shadow);
  await dispatchKeyCode(page, "F1", "F1");
  await waitForAnimationFrames(page, 2);
  const hiddenDebugUiStatus = await page.evaluate(() => window.__ofgDebugStatus?.() ?? null);
  if (hiddenDebugUiStatus?.debugUi?.visible !== false) {
    throw new Error(`Expected F1 to hide debug UI: ${JSON.stringify(hiddenDebugUiStatus?.debugUi)}.`);
  }
  await dispatchKeyCode(page, "F1", "F1");
  await waitForAnimationFrames(page, 2);
  const restoredDebugUiStatus = await page.evaluate(() => window.__ofgDebugStatus?.() ?? null);
  assertDebugUiDiagnostics(restoredDebugUiStatus?.debugUi ?? null);
  await page.mouse.move(40, 40);
  await waitForAnimationFrames(page, 2);
  const mouseCaptureDebugUiStatus = await page.evaluate(() => window.__ofgDebugStatus?.() ?? null);
  if (mouseCaptureDebugUiStatus?.debugUi?.wantsCaptureMouse !== true) {
    throw new Error(
      `Expected ImGui to capture mouse over the debug window: ${JSON.stringify(mouseCaptureDebugUiStatus?.debugUi)}.`
    );
  }
  await page.mouse.move(smokeContract.width - 20, 40);
  await page.waitForFunction(
    () => window.__ofgDebugStatus?.().debugUi.wantsCaptureMouse === false,
    { timeout: 5000 }
  );
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
  const demoScene = debugStatus?.demoScene ?? null;
  assertDemoSceneDiagnostics(demoScene);
  assertRenderCullingDiagnostics(debugStatus?.renderCulling ?? null, demoScene);
  assertDebugUiDiagnostics(debugStatus?.debugUi ?? null);
  assertShadowDiagnostics(debugStatus?.shadow ?? null);
  const report = {
    url,
    screenshotPath,
    headers,
    browserSignals,
    smokeContract,
    demoScene,
    renderCulling: debugStatus.renderCulling,
    debugUi: debugStatus.debugUi,
    shadow: debugStatus.shadow,
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
    debugUiToggleProbe: {
      hiddenDebugUiStatus,
      restoredDebugUiStatus,
      mouseCaptureDebugUiStatus
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

// Verifies renderer culling ran against the large default scene.
function assertRenderCullingDiagnostics(culling, demoScene) {
  if (culling === null) {
    throw new Error("Runtime debug status is missing render-culling diagnostics.");
  }
  if (culling.extractedObjectCount < demoScene.boxCount) {
    throw new Error(`Expected extracted render objects to cover the demo boxes: ${JSON.stringify(culling)}.`);
  }
  if (culling.cameraVisibleObjectCount + culling.cameraCulledObjectCount !== culling.extractedObjectCount) {
    throw new Error(`Culling counts do not balance: ${JSON.stringify(culling)}.`);
  }
  if (culling.cameraVisibleObjectCount < 1 || culling.cameraCulledObjectCount < 1) {
    throw new Error(`Expected visible and culled render objects: ${JSON.stringify(culling)}.`);
  }
}

// Verifies the default large renderer validation scene is active.
function assertDemoSceneDiagnostics(scene) {
  if (scene === null) {
    throw new Error("Runtime debug status is missing demo-scene diagnostics.");
  }
  if (scene.name !== "large-default-culling-shadow-validation") {
    throw new Error(`Unexpected demo scene name: ${scene.name}.`);
  }
  if (scene.boxCount < 150) {
    throw new Error(`Expected at least 150 validation boxes; got ${scene.boxCount}.`);
  }
  if (scene.nearBoxCount < 20 || scene.midBoxCount < 50 || scene.farBoxCount < 50) {
    throw new Error(`Validation box distribution is too sparse: ${JSON.stringify(scene)}.`);
  }
  if (scene.partlyBelowGroundCount < 12 || scene.overlapClusterBoxCount < 20) {
    throw new Error(`Validation scene lacks intersection coverage: ${JSON.stringify(scene)}.`);
  }
  if (scene.offCameraCandidateCount < 12) {
    throw new Error(`Validation scene lacks off-camera candidates: ${JSON.stringify(scene)}.`);
  }
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
  assertBloomDiagnostics(after);
  assertDebugUiDiagnostics(after.debugUi ?? null);
  assertShadowDiagnostics(after.shadow ?? null);
}

// Verifies the integrated bloom path and temp-buffer pool were exercised.
function assertBloomDiagnostics(status) {
  if (status.bloomSkipped) {
    throw new Error(`Bloom unexpectedly skipped: ${JSON.stringify(status)}.`);
  }
  if (status.bloomActiveLevelCount < 1 || status.bloomEncodedPassCount < 1) {
    throw new Error(`Expected bloom passes to run; got ${JSON.stringify(status)}.`);
  }
  if (status.bloomEncodedPassCount > 11) {
    throw new Error(`Bloom pass budget exceeded: ${JSON.stringify(status)}.`);
  }
  if (status.bloomEstimatedReadBytes < 1 || status.bloomEstimatedWriteBytes < 1) {
    throw new Error(`Expected bloom byte estimates; got ${JSON.stringify(status)}.`);
  }
  if (status.tempBufferPeakBytes < 1 || status.tempBufferReusableCount < 1) {
    throw new Error(`Expected temp-buffer reuse diagnostics; got ${JSON.stringify(status)}.`);
  }
  if (status.tempBufferPeakBytes > maxDefaultTempBufferBytes) {
    throw new Error(`Temp-buffer budget exceeded: ${JSON.stringify(status)}.`);
  }
}

// Verifies current-sun cascaded shadow diagnostics from the runtime status.
function assertShadowDiagnostics(shadow) {
  if (shadow === null || shadow === undefined) {
    throw new Error("Runtime debug status is missing shadow diagnostics.");
  }
  if (!shadow.enabled) {
    throw new Error(`Expected current-sun shadows to be enabled: ${JSON.stringify(shadow)}.`);
  }
  if (
    shadow.cascadeCount !== smokeContract.expectedShadowCascadeCount ||
    shadow.encodedPassCount !== smokeContract.expectedShadowEncodedPassCount
  ) {
    throw new Error(`Shadow cascade/pass counts do not match contract: ${JSON.stringify(shadow)}.`);
  }
  if (shadow.mapSize !== smokeContract.expectedShadowMapSize) {
    throw new Error(`Shadow map size does not match contract: ${JSON.stringify(shadow)}.`);
  }
  if (
    shadow.pcfMode !== smokeContract.expectedShadowPcfMode ||
    shadow.pcfSampleCount !== smokeContract.expectedShadowPcfSampleCount
  ) {
    throw new Error(`Shadow PCF mode does not match contract: ${JSON.stringify(shadow)}.`);
  }
  if (
    shadow.estimatedDepthBytes < 1 ||
    shadow.estimatedDepthBytes > smokeContract.maxShadowEstimatedDepthBytes
  ) {
    throw new Error(`Shadow depth byte estimate is outside budget: ${JSON.stringify(shadow)}.`);
  }
  if (shadow.effectiveIntensity < smokeContract.minShadowEffectiveIntensity) {
    throw new Error(`Shadow effective intensity is too low: ${JSON.stringify(shadow)}.`);
  }
  if (
    shadow.totalAcceptedCasterCount < smokeContract.minShadowAcceptedCasterCount ||
    shadow.totalDrawCount !== shadow.totalAcceptedCasterCount ||
    shadow.totalSubmeshCount < 1 ||
    shadow.totalIndexCount < 1
  ) {
    throw new Error(`Shadow caster draw diagnostics are incomplete: ${JSON.stringify(shadow)}.`);
  }
  if (!Array.isArray(shadow.cascades) || shadow.cascades.length !== shadow.cascadeCount) {
    throw new Error(`Shadow cascade diagnostics are incomplete: ${JSON.stringify(shadow)}.`);
  }
  for (const cascade of shadow.cascades) {
    if (
      cascade.testedCasterCount < 1 ||
      cascade.acceptedCasterCount + cascade.rejectedCasterCount !== cascade.testedCasterCount
    ) {
      throw new Error(`Shadow cascade caster counts do not balance: ${JSON.stringify(shadow)}.`);
    }
  }
}

// Verifies renderer-owned ImGui diagnostics from the runtime status.
function assertDebugUiDiagnostics(debugUi) {
  if (debugUi === null || debugUi === undefined) {
    throw new Error("Runtime debug status is missing debug UI diagnostics.");
  }
  if (!debugUi.visible || debugUi.overlayPassCount < 1) {
    throw new Error(`Expected a visible ImGui debug overlay pass: ${JSON.stringify(debugUi)}.`);
  }
  if (
    debugUi.drawListCount < 1 ||
    debugUi.drawCommandCount < 1 ||
    debugUi.menuTreeGeneration < 2 ||
    debugUi.vertexCount < 1 ||
    debugUi.indexCount < 1
  ) {
    throw new Error(`Debug UI draw diagnostics are incomplete: ${JSON.stringify(debugUi)}.`);
  }
  if (
    debugUi.uploadedVertexBytes < 1 ||
    debugUi.uploadedIndexBytes < 1 ||
    debugUi.vertexBufferCapacity < debugUi.vertexCount ||
    debugUi.indexBufferCapacity < debugUi.indexCount ||
    debugUi.fontTextureCreateCount < 1
  ) {
    throw new Error(`Debug UI upload diagnostics are incomplete: ${JSON.stringify(debugUi)}.`);
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
      if (isDebugUiSampleExcluded(x, y, png)) {
        continue;
      }
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
  if (sampledPixels === 0) {
    throw new Error("No scene pixels were sampled after debug UI exclusion.");
  }
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
    throw new Error(`Terrain surface coverage too low: ${groundRatio}`);
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

// Keeps scene smoke metrics independent from the renderer-owned debug overlay.
function isDebugUiSampleExcluded(x, _y, png) {
  return x < Math.min(debugUiSampleExclusion.width, png.width);
}

// Reports whether a non-background pixel looks like neutral checker ground.
function isNeutralGroundPixel(pixel) {
  const maxChannel = Math.max(pixel[0], pixel[1], pixel[2]);
  const minChannel = Math.min(pixel[0], pixel[1], pixel[2]);
  const brightness = pixel[0] + pixel[1] + pixel[2];
  return maxChannel - minChannel <= 30 && brightness >= 90 && brightness <= 690;
}

// Reports whether a non-background pixel looks like terrain height debug output.
function isTerrainHeightDebugPixel(pixel) {
  const [red, green, blue] = pixel;
  const redSurface = red >= 35 && red * 4 >= green * 5 && red >= blue * 2;
  const greenSurface = green >= 35 && green * 4 >= red * 5 && green >= blue * 2;
  return redSurface || greenSurface;
}

// Reports whether a non-background pixel looks like authored terrain or ground.
function isGroundLikePixel(pixel) {
  return isNeutralGroundPixel(pixel) || isTerrainHeightDebugPixel(pixel);
}

// Computes RGB distance while ignoring alpha, matching native smoke behavior.
function colorDistance(left, right) {
  const dr = left[0] - right[0];
  const dg = left[1] - right[1];
  const db = left[2] - right[2];
  return Math.sqrt(dr * dr + dg * dg + db * db);
}
