// Browser integration smoke for OFG. It validates that the browser shell can
// load engine_web.wasm, initialize WebGPU, render nonblank frames, forward a
// keyboard command, survive reload, verify mobile touch controls, and expose
// only black-box debug sentinels.

import { createServer } from "node:net";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";
import { PNG } from "pngjs";
import { runMobileTouchSmoke } from "./browser-smoke-mobile-touch.mjs";
import { runMovementPerformanceSmoke } from "./browser-smoke-movement-performance.mjs";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_SMOKE_PORT ?? "5174", 10);
const headed = process.env.OFG_SMOKE_HEADED === "1";
const artifactRoot = resolve(root, "artifacts", "browser-smoke");
const runId = new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(artifactRoot, runId);
const minRealScaleTerrainSpanMeters = 7000;

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
    await waitForTerrainLodFrame(page);
    assertNoBrowserFailures(consoleMessages);

    const firstHud = await readHud(page);
    assertHud(firstHud, "FIRST", consoleMessages);
    const firstDebug = await readDebugContract(page);
    assertDebugContract(firstDebug);
    await page.waitForTimeout(1100);
    await waitForBrowserFrame(page);
    const advancedSkyDebug = await readDebugContract(page);
    assertDebugContract(advancedSkyDebug);
    assertSkyRemainsInspectable(firstDebug, advancedSkyDebug);
    const firstImage = await saveScreenshot(page, "browser-first-person.png");
    assertPixelStats(firstImage.pixelStats, "browser first-person", consoleMessages);

    await setFogVisibleDebugCamera(page);
    const smokeFog = {
      startDistance: 200,
      endDistance: 3000,
      density: 1,
      colorR: 1,
      colorG: 1,
      colorB: 1,
      curve: 1.35
    };
    await setPostProcessFog(
      page,
      false,
      smokeFog.startDistance,
      smokeFog.endDistance,
      smokeFog.density,
      smokeFog.colorR,
      smokeFog.colorG,
      smokeFog.colorB,
      smokeFog.curve
    );
    await setPostProcessDebugView(page, "final");
    assertNoBrowserFailures(consoleMessages);
    const fogOffDebug = await readDebugContract(page);
    assertDebugContract(fogOffDebug, {
      fogEnabled: false,
      fogStartDistance: smokeFog.startDistance,
      fogEndDistance: smokeFog.endDistance,
      fogDensity: smokeFog.density,
      fogColorR: smokeFog.colorR,
      fogColorG: smokeFog.colorG,
      fogColorB: smokeFog.colorB,
      fogCurve: smokeFog.curve
    });
    const fogOffImage = await saveScreenshot(page, "browser-fog-off.png");
    assertPixelStats(fogOffImage.pixelStats, "browser fog off", consoleMessages);
    await setPostProcessFog(
      page,
      true,
      smokeFog.startDistance,
      smokeFog.endDistance,
      smokeFog.density,
      smokeFog.colorR,
      smokeFog.colorG,
      smokeFog.colorB,
      smokeFog.curve
    );
    await setPostProcessDebugView(page, "final");
    assertNoBrowserFailures(consoleMessages);
    const fogOnDebug = await readDebugContract(page);
    assertDebugContract(fogOnDebug, {
      fogStartDistance: smokeFog.startDistance,
      fogEndDistance: smokeFog.endDistance,
      fogDensity: smokeFog.density,
      fogColorR: smokeFog.colorR,
      fogColorG: smokeFog.colorG,
      fogColorB: smokeFog.colorB,
      fogCurve: smokeFog.curve
    });
    const fogOnImage = await saveScreenshot(page, "browser-fog-on.png");
    assertPixelStats(fogOnImage.pixelStats, "browser fog on", consoleMessages);
    assertFogChangesFinalPixels(fogOffImage.pixelStats, fogOnImage.pixelStats, consoleMessages);
    await setPostProcessDebugView(page, "fogFactor");
    assertNoBrowserFailures(consoleMessages);
    const fogFactorDebug = await readDebugContract(page);
    assertDebugContract(fogFactorDebug, {
      postProcessDebugView: "fogFactor",
      fogStartDistance: smokeFog.startDistance,
      fogEndDistance: smokeFog.endDistance,
      fogDensity: smokeFog.density,
      fogColorR: smokeFog.colorR,
      fogColorG: smokeFog.colorG,
      fogColorB: smokeFog.colorB,
      fogCurve: smokeFog.curve
    });
    const fogFactorImage = await saveScreenshot(page, "browser-fog-factor.png");
    assertFogFactorPixels(fogFactorImage.pixelStats, consoleMessages);
    await setPostProcessFog(page, true, 200, 3000, 1, 1, 1, 1, 1.35);
    await setPostProcessDebugView(page, "final");

    await setShadowDebugView(page, "cascadeIndex");
    assertNoBrowserFailures(consoleMessages);
    const cascadeDebug = await readDebugContract(page);
    assertDebugContract(cascadeDebug, { shadowDebugView: "cascadeIndex" });
    const cascadeDebugImage = await saveScreenshot(page, "browser-shadow-cascade-index.png");
    assertPixelStats(
      cascadeDebugImage.pixelStats,
      "browser shadow cascade debug",
      consoleMessages
    );
    await setShadowDebugView(page, "shadowVisibility");
    assertNoBrowserFailures(consoleMessages);
    const visibilityDebug = await readDebugContract(page);
    assertDebugContract(visibilityDebug, { shadowDebugView: "shadowVisibility" });
    const visibilityDebugImage = await saveScreenshot(page, "browser-shadow-visibility.png");
    assertPixelStats(
      visibilityDebugImage.pixelStats,
      "browser shadow visibility debug",
      consoleMessages
    );
    await setShadowDebugView(page, "shadowDepthCascade0");
    assertNoBrowserFailures(consoleMessages);
    const depthDebug = await readDebugContract(page);
    assertDebugContract(depthDebug, { shadowDebugView: "shadowDepthCascade0" });
    await setShadowDebugView(page, "off");
    assertNoBrowserFailures(consoleMessages);

    await setPostProcessDebugView(page, "linearDepth");
    assertNoBrowserFailures(consoleMessages);
    const linearDepthDebug = await readDebugContract(page);
    assertDebugContract(linearDepthDebug, { postProcessDebugView: "linearDepth" });
    const linearDepthImage = await saveScreenshot(page, "browser-linear-depth.png");
    assertPixelStats(linearDepthImage.pixelStats, "browser linear depth", consoleMessages);
    await setPostProcessBloom(page, true, 0.2, 0.6);
    await setPostProcessDebugView(page, "bloom");
    assertNoBrowserFailures(consoleMessages);
    const bloomDebug = await readDebugContract(page);
    assertDebugContract(bloomDebug, {
      postProcessDebugView: "bloom",
      bloomThreshold: 0.2,
      bloomIntensity: 0.6
    });
    const bloomImage = await saveScreenshot(page, "browser-bloom.png");
    assertPixelStats(bloomImage.pixelStats, "browser bloom", consoleMessages);
    await setPostProcessToneMapping(page, true, 1.1);
    await setPostProcessDebugView(page, "postToneMap");
    assertNoBrowserFailures(consoleMessages);
    const postToneMapDebug = await readDebugContract(page);
    assertDebugContract(postToneMapDebug, {
      postProcessDebugView: "postToneMap",
      postProcessExposure: 1.1,
      bloomThreshold: 0.2,
      bloomIntensity: 0.6
    });
    const postToneMapImage = await saveScreenshot(page, "browser-post-tone-map.png");
    assertPixelStats(postToneMapImage.pixelStats, "browser post tone map", consoleMessages);
    await setPostProcessDepthOfField(page, true, 8, 1, 12);
    await setPostProcessDebugView(page, "dofCoc");
    assertNoBrowserFailures(consoleMessages);
    const dofCocDebug = await readDebugContract(page);
    assertDebugContract(dofCocDebug, {
      postProcessDebugView: "dofCoc",
      postProcessExposure: 1.1,
      bloomThreshold: 0.2,
      bloomIntensity: 0.6,
      dofEnabled: true,
      dofFocusDistance: 8,
      dofFocusRange: 1,
      dofMaxBlurPixels: 12
    });
    const dofCocImage = await saveScreenshot(page, "browser-dof-coc.png");
    assertPixelStats(dofCocImage.pixelStats, "browser DoF CoC", consoleMessages);
    await setPostProcessDebugView(page, "dofBlurred");
    assertNoBrowserFailures(consoleMessages);
    const dofBlurredDebug = await readDebugContract(page);
    assertDebugContract(dofBlurredDebug, {
      postProcessDebugView: "dofBlurred",
      postProcessExposure: 1.1,
      bloomThreshold: 0.2,
      bloomIntensity: 0.6,
      dofEnabled: true,
      dofFocusDistance: 8,
      dofFocusRange: 1,
      dofMaxBlurPixels: 12
    });
    const dofBlurredImage = await saveScreenshot(page, "browser-dof-blurred.png");
    assertPixelStats(dofBlurredImage.pixelStats, "browser DoF blurred", consoleMessages);
    await setPostProcessToneMapping(page, true, 1.0);
    await setPostProcessBloom(page, true, 1.0, 0.08);
    await setPostProcessDepthOfField(page, false, 30, 8, 6);
    await setPostProcessDebugView(page, "final");

    await setWaterVisibleDebugCamera(page);
    assertNoBrowserFailures(consoleMessages);
    const waterFinalDebug = await readDebugContract(page);
    assertDebugContract(waterFinalDebug);
    const waterFinalImage = await saveScreenshot(page, "browser-water-final.png");
    assertPixelStats(waterFinalImage.pixelStats, "browser water final", consoleMessages);
    assertWaterFinalPixels(waterFinalImage.pixelStats, consoleMessages);

    await setWaterDebugView(page, "bottomDepth");
    assertNoBrowserFailures(consoleMessages);
    const waterBottomDepthDebug = await readDebugContract(page);
    assertDebugContract(waterBottomDepthDebug, { waterDebugView: "bottomDepth" });
    const waterBottomDepthImage = await saveScreenshot(page, "browser-water-bottom-depth.png");
    assertPixelStats(waterBottomDepthImage.pixelStats, "browser water bottom depth", consoleMessages);
    assertWaterBottomDepthPixels(waterBottomDepthImage.pixelStats, consoleMessages);
    await setWaterDebugView(page, "final");
    assertNoBrowserFailures(consoleMessages);

    const diagnosticRenderOptions = {
      skyEnabled: false,
      skyCloudNoiseEnabled: false,
      shadowCascadeMask: 0b0001,
      shadowSamplingEnabled: false,
      whiteTexturesEnabled: true,
      materialMode: "lambert"
    };
    await setRenderDebugOptions(page, diagnosticRenderOptions);
    assertNoBrowserFailures(consoleMessages);
    const renderDebugDisabled = await readDebugContract(page);
    assertDebugContract(renderDebugDisabled, {
      renderDebugOptions: {
        ...defaultRenderDebugOptions(),
        ...diagnosticRenderOptions
      }
    });
    await resetRenderDebugOptions(page);
    assertNoBrowserFailures(consoleMessages);
    const renderDebugReset = await readDebugContract(page);
    assertDebugContract(renderDebugReset);
    const renderDebugUi = await exerciseRenderDebugUi(page);
    assertNoBrowserFailures(consoleMessages);
    const renderDebugUiReset = await readDebugContract(page);
    assertDebugContract(renderDebugUiReset);
    const postProcessUi = await exercisePostProcessUi(page);
    assertNoBrowserFailures(consoleMessages);
    const postProcessUiReset = await readDebugContract(page);
    assertDebugContract(postProcessUiReset);
    const waterDebugUi = await exerciseWaterDebugUi(page);
    assertNoBrowserFailures(consoleMessages);
    const waterDebugUiReset = await readDebugContract(page);
    assertDebugContract(waterDebugUiReset);
    const perfOverlayUi = await exercisePerfOverlayUi(page);
    assertNoBrowserFailures(consoleMessages);

    await setCameraMode(page, "firstPerson", "FIRST");
    await page.keyboard.press("KeyC");
    await page.waitForFunction(() => document.querySelector("#camera-mode")?.textContent === "THIRD");
    await waitForBrowserFrame(page);
    await waitForTerrainLodFrame(page);
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
    await waitForTerrainLodFrame(page);
    assertNoBrowserFailures(consoleMessages);
    const reloadedHud = await readHud(page);
    assertHud(reloadedHud, "FIRST", consoleMessages);
    const reloadedDebug = await readDebugContract(page);
    assertDebugContract(reloadedDebug);
    const reloadedImage = await saveScreenshot(page, "browser-reloaded.png");
    assertPixelStats(reloadedImage.pixelStats, "browser reload", consoleMessages);

    const movementPerformance = await runMovementPerformanceSmoke({
      page,
      artifactDir,
      consoleMessages,
      waitForBrowserFrame,
      waitForTerrainLodFrame,
      assertNoBrowserFailures,
      readDebugContract,
      assertDebugContract
    });
    const movementImage = await saveScreenshot(page, "browser-movement-performance.png");
    assertPixelStats(movementImage.pixelStats, "browser movement performance", consoleMessages);

    const mobileTouch = await runMobileTouchSmoke({
      browser,
      url,
      assertResponseHeaders,
      waitForBrowserFrame,
      waitForTerrainLodFrame,
      assertNoBrowserFailures,
      readHud,
      assertHud,
      readDebugContract,
      assertDebugContract,
      saveScreenshot,
      assertPixelStats
    });

    return {
      kind: "browser-integration-smoke",
      url,
      artifactDir: reportPath(artifactDir),
      browserPath,
      headed,
      images: [
        firstImage,
        fogOffImage,
        fogOnImage,
        fogFactorImage,
        cascadeDebugImage,
        visibilityDebugImage,
        linearDepthImage,
        bloomImage,
        postToneMapImage,
        dofCocImage,
        dofBlurredImage,
        waterFinalImage,
        waterBottomDepthImage,
        toggledImage,
        reloadedImage,
        movementImage,
        mobileTouch.image
      ],
      firstHud,
      toggledHud,
      reloadedHud,
      firstDebug,
      advancedSkyDebug,
      fogOffDebug,
      fogOnDebug,
      fogFactorDebug,
      cascadeDebug,
      visibilityDebug,
      depthDebug,
      linearDepthDebug,
      bloomDebug,
      postToneMapDebug,
      dofCocDebug,
      dofBlurredDebug,
      waterFinalDebug,
      renderDebugDisabled,
      renderDebugReset,
      renderDebugUi,
      renderDebugUiReset,
      postProcessUi,
      postProcessUiReset,
      waterBottomDepthDebug,
      waterDebugUi,
      waterDebugUiReset,
      perfOverlayUi,
      toggledDebug,
      reloadedDebug,
      movementPerformance,
      mobileTouch,
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
      status.frameDrawCount > 0 &&
      status.frameVisibleDrawCount > 0;
  }, null, { timeout: 20000 });
  await page.waitForTimeout(250);
}

/// Selects a post-process debug view and waits for Rust/wgpu to report it.
async function setPostProcessDebugView(page, view) {
  const startingFrameIndex = await page.evaluate(() =>
    window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0
  );
  await page.evaluate((selectedView) => {
    window.__ofgDebug?.setPostProcessDebugView?.(selectedView);
  }, view);
  await page.waitForFunction(({ selectedView, frameIndex }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    const debugView = window.__ofgDebug?.getPostProcessDebugView?.();
    return debugView === selectedView &&
      status?.postProcessDebugView === selectedView &&
      status.frameIndex > frameIndex;
  }, { selectedView: view, frameIndex: startingFrameIndex }, { timeout: 10000 });
  await page.waitForTimeout(250);
}

/// Updates tone-map settings and waits for a frame with those settings.
async function setPostProcessToneMapping(page, enabled, exposure) {
  const startingFrameIndex = await page.evaluate(() =>
    window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0
  );
  await page.evaluate(({ selectedEnabled, selectedExposure }) => {
    window.__ofgDebug?.setPostProcessToneMapping?.(selectedEnabled, selectedExposure);
  }, { selectedEnabled: enabled, selectedExposure: exposure });
  await page.waitForFunction(({ selectedEnabled, selectedExposure, frameIndex }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.postProcessToneMappingEnabled === selectedEnabled &&
      Math.abs(status.postProcessExposure - selectedExposure) < 0.0001 &&
      status.frameIndex > frameIndex;
  }, { selectedEnabled: enabled, selectedExposure: exposure, frameIndex: startingFrameIndex }, {
    timeout: 10000
  });
  await page.waitForTimeout(250);
}

/// Updates bloom settings and waits for a frame with those settings.
async function setPostProcessBloom(page, enabled, threshold, intensity) {
  const startingFrameIndex = await page.evaluate(() =>
    window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0
  );
  await page.evaluate(({ selectedEnabled, selectedThreshold, selectedIntensity }) => {
    window.__ofgDebug?.setPostProcessBloom?.(
      selectedEnabled,
      selectedThreshold,
      selectedIntensity
    );
  }, { selectedEnabled: enabled, selectedThreshold: threshold, selectedIntensity: intensity });
  await page.waitForFunction(({
    selectedEnabled,
    selectedThreshold,
    selectedIntensity,
    frameIndex
  }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.postProcessBloomEnabled === selectedEnabled &&
      Math.abs(status.postProcessBloomThreshold - selectedThreshold) < 0.0001 &&
      Math.abs(status.postProcessBloomIntensity - selectedIntensity) < 0.0001 &&
      status.frameIndex > frameIndex;
  }, {
    selectedEnabled: enabled,
    selectedThreshold: threshold,
    selectedIntensity: intensity,
    frameIndex: startingFrameIndex
  }, { timeout: 10000 });
  await page.waitForTimeout(250);
}

/// Updates depth-of-field settings and waits for a frame with those settings.
async function setPostProcessDepthOfField(
  page,
  enabled,
  focusDistance,
  focusRange,
  maxBlurPixels
) {
  const startingFrameIndex = await page.evaluate(() =>
    window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0
  );
  await page.evaluate(({
    selectedEnabled,
    selectedFocusDistance,
    selectedFocusRange,
    selectedMaxBlurPixels
  }) => {
    window.__ofgDebug?.setPostProcessDepthOfField?.(
      selectedEnabled,
      selectedFocusDistance,
      selectedFocusRange,
      selectedMaxBlurPixels
    );
  }, {
    selectedEnabled: enabled,
    selectedFocusDistance: focusDistance,
    selectedFocusRange: focusRange,
    selectedMaxBlurPixels: maxBlurPixels
  });
  await page.waitForFunction(({
    selectedEnabled,
    selectedFocusDistance,
    selectedFocusRange,
    selectedMaxBlurPixels,
    frameIndex
  }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.postProcessDofEnabled === selectedEnabled &&
      Math.abs(status.postProcessDofFocusDistance - selectedFocusDistance) < 0.0001 &&
      Math.abs(status.postProcessDofFocusRange - selectedFocusRange) < 0.0001 &&
      Math.abs(status.postProcessDofMaxBlurPixels - selectedMaxBlurPixels) < 0.0001 &&
      status.frameIndex > frameIndex;
  }, {
    selectedEnabled: enabled,
    selectedFocusDistance: focusDistance,
    selectedFocusRange: focusRange,
    selectedMaxBlurPixels: maxBlurPixels,
    frameIndex: startingFrameIndex
  }, { timeout: 10000 });
  await page.waitForTimeout(250);
}

/// Updates fog settings and waits for a frame with those settings.
async function setPostProcessFog(
  page,
  enabled,
  startDistance,
  endDistance,
  density,
  colorR,
  colorG,
  colorB,
  curve
) {
  const startingFrameIndex = await rendererFrameIndex(page);
  const expected = { enabled, startDistance, endDistance, density, colorR, colorG, colorB, curve };
  await page.evaluate((settings) => {
    window.__ofgDebug?.setPostProcessFog?.(settings);
  }, expected);
  await page.waitForFunction(({ expectedSettings, frameIndex }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.postProcessFogEnabled === expectedSettings.enabled &&
      Math.abs(status.postProcessFogStartDistance - expectedSettings.startDistance) < 0.0001 &&
      Math.abs(status.postProcessFogEndDistance - expectedSettings.endDistance) < 0.0001 &&
      Math.abs(status.postProcessFogDensity - expectedSettings.density) < 0.0001 &&
      Math.abs(status.postProcessFogColorR - expectedSettings.colorR) < 0.0001 &&
      Math.abs(status.postProcessFogColorG - expectedSettings.colorG) < 0.0001 &&
      Math.abs(status.postProcessFogColorB - expectedSettings.colorB) < 0.0001 &&
      Math.abs(status.postProcessFogCurve - expectedSettings.curve) < 0.0001 &&
      status.frameIndex > frameIndex;
  }, { expectedSettings: expected, frameIndex: startingFrameIndex }, { timeout: 10000 });
  await page.waitForTimeout(250);
}

/// Selects a Rust-owned water debug view and waits for the renderer status.
async function setWaterDebugView(page, view) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate((selectedView) => {
    window.__ofgDebug?.setWaterDebugView?.(selectedView);
  }, view);
  await waitForWaterSettings(page, { waterDebugView: view }, startingFrameIndex);
  await page.waitForTimeout(250);
}

/// Updates Rust-owned water options and waits for renderer status.
async function setWaterOptions(page, options) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate((selectedOptions) => {
    window.__ofgDebug?.setWaterOptions?.(selectedOptions);
  }, options);
  await waitForWaterSettings(page, options, startingFrameIndex);
  await page.waitForTimeout(250);
}

/// Sets a Rust-owned camera mode and waits for the HUD to reflect it.
async function setCameraMode(page, mode, hudLabel) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate((selectedMode) => {
    window.__ofgDebug?.setCameraMode?.(selectedMode);
  }, mode);
  await page.waitForFunction(({ frameIndex, expectedLabel }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.frameIndex > frameIndex &&
      document.querySelector("#camera-mode")?.textContent === expectedLabel;
  }, {
    frameIndex: startingFrameIndex,
    expectedLabel: hudLabel
  }, { timeout: 10000 });
  await page.waitForTimeout(250);
}

/// Places the Rust debug-fly camera at a deterministic long-horizon terrain view.
async function setFogVisibleDebugCamera(page) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate(({ x, y, z, yaw, pitch }) => {
    window.__ofgDebug?.setCameraMode?.("debugFly");
    window.__ofgDebug?.setDebugCamera?.(x, y, z, yaw, pitch);
  }, {
    x: 220.0,
    y: 160.0,
    z: 260.0,
    yaw: -2.44,
    pitch: -0.34
  });
  await page.waitForFunction((frameIndex) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.frameIndex > frameIndex &&
      document.querySelector("#camera-mode")?.textContent === "FLY";
  }, startingFrameIndex, { timeout: 10000 });
  await waitForTerrainLodFrame(page);
  await page.waitForTimeout(350);
}

/// Places the Rust debug-fly camera above a deterministic water patch.
async function setWaterVisibleDebugCamera(page) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate(({ x, y, z, yaw, pitch }) => {
    window.__ofgDebug?.setCameraMode?.("debugFly");
    window.__ofgDebug?.setDebugCamera?.(x, y, z, yaw, pitch);
  }, {
    x: -512.0,
    y: 120.0,
    z: -1024.0,
    yaw: 0.0,
    pitch: -1.35
  });
  await page.waitForFunction((frameIndex) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.frameIndex > frameIndex &&
      document.querySelector("#camera-mode")?.textContent === "FLY";
  }, startingFrameIndex, { timeout: 10000 });
  await waitForTerrainLodFrame(page);
  await page.waitForTimeout(350);
}

/// Updates render diagnostic options and waits for Rust/wgpu to report them.
async function setRenderDebugOptions(page, options) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate((selectedOptions) => {
    window.__ofgDebug?.setRenderDebugOptions?.(selectedOptions);
  }, options);
  await waitForRenderDebugOptions(page, options, startingFrameIndex);
  await page.waitForTimeout(250);
}

/// Resets render diagnostic options and waits for production defaults.
async function resetRenderDebugOptions(page) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate(() => {
    window.__ofgDebug?.resetRenderDebugOptions?.();
  });
  await waitForRenderDebugOptions(page, defaultRenderDebugOptions(), startingFrameIndex);
  await page.waitForTimeout(250);
}

/// Exercises the visible render-debug controls and verifies Rust-owned state changes.
async function exerciseRenderDebugUi(page) {
  await page.click("#render-debug-panel-toggle");
  await page.waitForFunction(() =>
    document.querySelector("#render-debug-panel")?.hidden === false
  );
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.selectOption("#render-debug-terrain-lod", "lod2");
  await page.uncheck("#render-debug-sky");
  await page.uncheck("#render-debug-sky-cloud-noise");
  await page.uncheck("#render-debug-shadow-pass");
  await page.uncheck("#render-debug-shadow-sampling");
  await page.check("#render-debug-white-textures");
  await page.selectOption("#render-debug-sun", "overhead");
  await page.selectOption("#render-debug-material", "lambert");
  await page.uncheck('[data-shadow-cascade="1"]');
  await page.uncheck('[data-shadow-cascade="2"]');
  await page.uncheck('[data-shadow-cascade="3"]');

  const expectedOptions = {
    terrainLodMask: 0b000100,
    skyEnabled: false,
    skyCloudNoiseEnabled: false,
    shadowPassEnabled: false,
    shadowCascadeMask: 0b0001,
    shadowSamplingEnabled: false,
    shadowSunMode: "overhead",
    whiteTexturesEnabled: true,
    materialMode: "lambert"
  };
  await waitForRenderDebugOptions(page, expectedOptions, startingFrameIndex);
  const enabledState = await page.evaluate(() => ({
    panelHidden: document.querySelector("#render-debug-panel")?.hidden,
    terrainLodMode: document.querySelector("#render-debug-terrain-lod")?.value,
    activeOptions: window.__ofgDebug?.getRenderDebugOptions?.()
  }));

  const resetFrameIndex = await rendererFrameIndex(page);
  await page.click("#render-debug-reset");
  await waitForRenderDebugOptions(page, defaultRenderDebugOptions(), resetFrameIndex);
  await page.click("#render-debug-panel-toggle");
  await page.waitForFunction(() =>
    document.querySelector("#render-debug-panel")?.hidden === true
  );

  return {
    enabledState,
    resetOptions: await page.evaluate(() => window.__ofgDebug?.getRenderDebugOptions?.()),
    panelHiddenAfterClose: await page.evaluate(() =>
      document.querySelector("#render-debug-panel")?.hidden
    )
  };
}

/// Exercises visible post-process debug controls and verifies Rust-owned state changes.
async function exercisePostProcessUi(page) {
  await page.click("#render-debug-panel-toggle");
  await page.waitForFunction(() =>
    document.querySelector("#render-debug-panel")?.hidden === false
  );
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.selectOption("#post-debug-view", "bloom");
  await page.uncheck("#post-tone-mapping");
  await fillAndCommit(page, "#post-exposure", "0.75");
  await page.uncheck("#post-bloom");
  await fillAndCommit(page, "#post-bloom-threshold", "0.2");
  await fillAndCommit(page, "#post-bloom-intensity", "0.6");
  await page.check("#post-dof");
  await fillAndCommit(page, "#post-dof-focus", "8");
  await fillAndCommit(page, "#post-dof-range", "1");
  await fillAndCommit(page, "#post-dof-blur", "12");
  await page.uncheck("#post-fog");
  await fillAndCommit(page, "#post-fog-start", "6400");
  await fillAndCommit(page, "#post-fog-end", "10800");
  await fillAndCommit(page, "#post-fog-density", "0.8");
  await fillAndCommit(page, "#post-fog-curve", "1.6");
  await fillAndCommit(page, "#post-fog-r", "0.5");
  await fillAndCommit(page, "#post-fog-g", "0.6");
  await fillAndCommit(page, "#post-fog-b", "0.7");

  const expected = {
    postProcessDebugView: "bloom",
    postProcessToneMappingEnabled: false,
    postProcessExposure: 0.75,
    postProcessBloomEnabled: false,
    postProcessBloomThreshold: 0.2,
    postProcessBloomIntensity: 0.6,
    postProcessDofEnabled: true,
    postProcessDofFocusDistance: 8,
    postProcessDofFocusRange: 1,
    postProcessDofMaxBlurPixels: 12,
    postProcessFogEnabled: false,
    postProcessFogStartDistance: 6400,
    postProcessFogEndDistance: 10800,
    postProcessFogDensity: 0.8,
    postProcessFogColorR: 0.5,
    postProcessFogColorG: 0.6,
    postProcessFogColorB: 0.7,
    postProcessFogCurve: 1.6
  };
  await waitForPostProcessSettings(page, expected, startingFrameIndex);
  const enabledState = await page.evaluate(() => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return {
      panelHidden: document.querySelector("#render-debug-panel")?.hidden,
      postDebugView: document.querySelector("#post-debug-view")?.value,
      toneMappingChecked: document.querySelector("#post-tone-mapping")?.checked,
      bloomChecked: document.querySelector("#post-bloom")?.checked,
      dofChecked: document.querySelector("#post-dof")?.checked,
      fogChecked: document.querySelector("#post-fog")?.checked,
      status: status === undefined
        ? undefined
        : {
            postProcessDebugView: status.postProcessDebugView,
            postProcessToneMappingEnabled: status.postProcessToneMappingEnabled,
            postProcessExposure: status.postProcessExposure,
            postProcessBloomEnabled: status.postProcessBloomEnabled,
            postProcessBloomThreshold: status.postProcessBloomThreshold,
            postProcessBloomIntensity: status.postProcessBloomIntensity,
            postProcessDofEnabled: status.postProcessDofEnabled,
            postProcessDofFocusDistance: status.postProcessDofFocusDistance,
            postProcessDofFocusRange: status.postProcessDofFocusRange,
            postProcessDofMaxBlurPixels: status.postProcessDofMaxBlurPixels,
            postProcessFogEnabled: status.postProcessFogEnabled,
            postProcessFogStartDistance: status.postProcessFogStartDistance,
            postProcessFogEndDistance: status.postProcessFogEndDistance,
            postProcessFogDensity: status.postProcessFogDensity,
            postProcessFogColorR: status.postProcessFogColorR,
            postProcessFogColorG: status.postProcessFogColorG,
            postProcessFogColorB: status.postProcessFogColorB,
            postProcessFogCurve: status.postProcessFogCurve
          }
    };
  });

  const resetFrameIndex = await rendererFrameIndex(page);
  await page.click("#post-debug-reset");
  await waitForPostProcessSettings(page, defaultPostProcessSettings(), resetFrameIndex);
  await page.click("#render-debug-panel-toggle");
  await page.waitForFunction(() =>
    document.querySelector("#render-debug-panel")?.hidden === true
  );

  return {
    enabledState,
    resetStatus: await page.evaluate(() => {
      const status = window.__ofgDebug?.getRendererStatus?.();
      return status === undefined
        ? undefined
        : {
            postProcessDebugView: status.postProcessDebugView,
            postProcessToneMappingEnabled: status.postProcessToneMappingEnabled,
            postProcessExposure: status.postProcessExposure,
            postProcessBloomEnabled: status.postProcessBloomEnabled,
            postProcessBloomThreshold: status.postProcessBloomThreshold,
            postProcessBloomIntensity: status.postProcessBloomIntensity,
            postProcessDofEnabled: status.postProcessDofEnabled,
            postProcessDofFocusDistance: status.postProcessDofFocusDistance,
            postProcessDofFocusRange: status.postProcessDofFocusRange,
            postProcessDofMaxBlurPixels: status.postProcessDofMaxBlurPixels,
            postProcessFogEnabled: status.postProcessFogEnabled,
            postProcessFogStartDistance: status.postProcessFogStartDistance,
            postProcessFogEndDistance: status.postProcessFogEndDistance,
            postProcessFogDensity: status.postProcessFogDensity,
            postProcessFogColorR: status.postProcessFogColorR,
            postProcessFogColorG: status.postProcessFogColorG,
            postProcessFogColorB: status.postProcessFogColorB,
            postProcessFogCurve: status.postProcessFogCurve
          };
    }),
    panelHiddenAfterClose: await page.evaluate(() =>
      document.querySelector("#render-debug-panel")?.hidden
    )
  };
}

/// Exercises visible water debug controls and verifies Rust-owned state changes.
async function exerciseWaterDebugUi(page) {
  await page.click("#render-debug-panel-toggle");
  await page.waitForFunction(() =>
    document.querySelector("#render-debug-panel")?.hidden === false
  );
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.selectOption("#water-debug-view", "fresnel");
  await page.check("#water-reflection");

  const expected = {
    waterDebugView: "fresnel",
    waterReflectionEnabled: true
  };
  await waitForWaterSettings(page, expected, startingFrameIndex);
  const enabledState = await page.evaluate(() => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return {
      panelHidden: document.querySelector("#render-debug-panel")?.hidden,
      waterDebugView: document.querySelector("#water-debug-view")?.value,
      waterEnabledChecked: document.querySelector("#water-enabled")?.checked,
      waterReflectionChecked: document.querySelector("#water-reflection")?.checked,
      waterStatusText: document.querySelector("#water-debug-status")?.textContent ?? "",
      status: status === undefined
        ? undefined
        : {
            waterDebugView: status.waterDebugView,
            waterEnabled: status.waterEnabled,
            waterReflectionEnabled: status.waterReflectionEnabled,
            waterBathymetryRuntime: status.waterBathymetryRuntime,
            waterBathymetryGridSize: status.waterBathymetryGridSize
          }
    };
  });

  const resetFrameIndex = await rendererFrameIndex(page);
  await page.selectOption("#water-debug-view", "final");
  await page.check("#water-enabled");
  await page.uncheck("#water-reflection");
  await waitForWaterSettings(page, defaultWaterSettings(), resetFrameIndex);
  await page.click("#render-debug-panel-toggle");
  await page.waitForFunction(() =>
    document.querySelector("#render-debug-panel")?.hidden === true
  );

  return {
    enabledState,
    resetStatus: await page.evaluate(() => {
      const status = window.__ofgDebug?.getRendererStatus?.();
      return status === undefined
        ? undefined
        : {
            waterDebugView: status.waterDebugView,
            waterEnabled: status.waterEnabled,
            waterReflectionEnabled: status.waterReflectionEnabled
          };
    }),
    panelHiddenAfterClose: await page.evaluate(() =>
      document.querySelector("#render-debug-panel")?.hidden
    )
  };
}

/// Exercises the visible live perf overlay toggle and verifies metric text.
async function exercisePerfOverlayUi(page) {
  await page.click("#perf-overlay-toggle");
  await page.waitForFunction(() => {
    const overlay = document.querySelector("#perf-overlay");
    const text = overlay?.textContent ?? "";
    return overlay?.hidden === false &&
      text.includes("Frame br") &&
      text.includes("LOD") &&
      text.includes("Casc") &&
      text.includes("Debug");
  }, null, { timeout: 10000 });
  const visibleText = await page.evaluate(() =>
    document.querySelector("#perf-overlay")?.textContent?.slice(0, 600) ?? ""
  );
  await page.click("#perf-overlay-toggle");
  await page.waitForFunction(() =>
    document.querySelector("#perf-overlay")?.hidden === true
  );

  return {
    visibleText,
    hiddenAfterToggle: await page.evaluate(() =>
      document.querySelector("#perf-overlay")?.hidden
    )
  };
}

/// Fills an input and dispatches change so the app receives the committed value.
async function fillAndCommit(page, selector, value) {
  await page.fill(selector, value);
  await page.dispatchEvent(selector, "change");
}

/// Reads the current Rust/wgpu renderer frame index.
async function rendererFrameIndex(page) {
  return page.evaluate(() =>
    window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0
  );
}

/// Waits for expected post-process settings through renderer status.
async function waitForPostProcessSettings(page, expectedSettings, startingFrameIndex) {
  await page.waitForFunction(({ expected, frameIndex }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    if (status === undefined || status.frameIndex <= frameIndex) {
      return false;
    }

    return Object.entries(expected).every(([key, expectedValue]) => {
      const actual = status[key];
      if (typeof expectedValue === "number") {
        return Math.abs(actual - expectedValue) < 0.0001;
      }
      return actual === expectedValue;
    });
  }, { expected: expectedSettings, frameIndex: startingFrameIndex }, { timeout: 10000 });
}

/// Waits for expected water settings through renderer status.
async function waitForWaterSettings(page, expectedSettings, startingFrameIndex) {
  const normalized = Object.fromEntries(
    Object.entries(expectedSettings).map(([key, value]) => [
      key === "enabled"
        ? "waterEnabled"
        : key === "reflectionEnabled"
          ? "waterReflectionEnabled"
          : key === "seaLevelMeters"
            ? "waterSeaLevelMeters"
            : key,
      value
    ])
  );
  await page.waitForFunction(({ expected, frameIndex }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    if (status === undefined || status.frameIndex <= frameIndex) {
      return false;
    }

    return Object.entries(expected).every(([key, expectedValue]) => {
      const actual = status[key];
      if (typeof expectedValue === "number") {
        return Math.abs(actual - expectedValue) < 0.0001;
      }
      return actual === expectedValue;
    });
  }, { expected: normalized, frameIndex: startingFrameIndex }, { timeout: 10000 });
}

/// Waits for expected render debug options through both debug and renderer status.
async function waitForRenderDebugOptions(page, expectedOptions, startingFrameIndex) {
  await page.waitForFunction(({ expected, frameIndex }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    const activeOptions = window.__ofgDebug?.getRenderDebugOptions?.();
    const matches = (actual) => actual !== undefined &&
      Object.entries(expected).every(([key, expectedValue]) => actual[key] === expectedValue);
    return status !== undefined &&
      status.frameIndex > frameIndex &&
      matches(activeOptions) &&
      matches(status.renderDebugOptions);
  }, { expected: expectedOptions, frameIndex: startingFrameIndex }, { timeout: 10000 });
}

/// Waits until Rust terrain streaming exposes a settled mixed-LOD frame.
async function waitForTerrainLodFrame(page) {
  await page.waitForFunction((minSpanMeters) => {
    const debug = window.__ofgDebug;
    const status = debug?.getTerrainStreamStatus?.();
    const terrainNodeKeys = debug?.getTerrainNodeKeys?.() ?? [];
    return status !== undefined &&
      status.pending === false &&
      status.renderedChunkCount > 0 &&
      status.renderedNodeCount > status.renderedChunkCount &&
      status.maxRenderedLod >= 5 &&
      status.visibleWorldSpanXMeters >= minSpanMeters &&
      status.visibleWorldSpanZMeters >= minSpanMeters &&
      terrainNodeKeys.some((key) => key.startsWith("lod0:")) &&
      terrainNodeKeys.some((key) => key.startsWith("lod5:"));
  }, minRealScaleTerrainSpanMeters, { timeout: 120000 });
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

/// Sets a Rust-owned shadow debug view through the black-box browser hook.
async function setShadowDebugView(page, view) {
  const activeView = await page.evaluate((nextView) => {
    window.__ofgDebug?.setShadowDebugView?.(nextView);
    return window.__ofgDebug?.getShadowDebugView?.();
  }, view);
  if (activeView !== view) {
    throw new Error(`Expected shadow debug view '${view}', saw '${activeView}'.`);
  }
  await waitForBrowserFrame(page);
}

/// Reads black-box Rust runtime sentinels from the debug API.
async function readDebugContract(page) {
  return page.evaluate(() => {
    const debug = window.__ofgDebug;
    const status = debug?.getRendererStatus?.();
    const perfStats = debug?.getPerfStats?.();
    const renderDebugOptions = debug?.getRenderDebugOptions?.();

    return {
      hasDebug: debug !== undefined,
      apiKeys: debug === undefined ? [] : Object.keys(debug).sort(),
      hasDumpPerfStats: typeof debug?.dumpPerfStats === "function",
      hasResetPerfStats: typeof debug?.resetPerfStats === "function",
      hasSetRenderDebugOptions: typeof debug?.setRenderDebugOptions === "function",
      hasResetRenderDebugOptions: typeof debug?.resetRenderDebugOptions === "function",
      playerControllerRuntime: debug?.getPlayerControllerRuntime?.() ?? "missing",
      renderPacketRuntime: debug?.getRenderPacketRuntime?.() ?? "missing",
      terrainStreamerRuntime: debug?.getTerrainStreamerRuntime?.() ?? "missing",
      terrainStreamSchedulerRuntime: debug?.getTerrainStreamSchedulerRuntime?.() ?? "missing",
      terrainDensityStoreRuntime: debug?.getTerrainDensityStoreRuntime?.() ?? "missing",
      terrainWorkerPoolRuntime: debug?.getTerrainWorkerPoolRuntime?.() ?? "missing",
      terrainWorkerCount: debug?.getTerrainWorkerCount?.() ?? 0,
      terrainRenderPacketRuntime: debug?.getTerrainRenderPacketRuntime?.() ?? "missing",
      loadedTerrainNodeKeys: debug?.getLoadedTerrainNodeKeys?.() ?? [],
      terrainNodeKeys: debug?.getTerrainNodeKeys?.() ?? [],
      terrainStreamStatus: debug?.getTerrainStreamStatus?.(),
      rendererRuntime: debug?.getRendererRuntime?.() ?? "missing",
      shadowDebugView: debug?.getShadowDebugView?.() ?? "missing",
      skyRuntime: debug?.getSkyRuntime?.() ?? "missing",
      skyDayPhase: debug?.getSkyDayPhase?.(),
      skySunElevation: debug?.getSkySunElevation?.(),
      skyCloudCoverage: debug?.getSkyCloudCoverage?.(),
      skyStarIntensity: debug?.getSkyStarIntensity?.(),
      postProcessDebugView: debug?.getPostProcessDebugView?.() ?? "missing",
      postProcessFogEnabled: debug?.getPostProcessFogEnabled?.(),
      perfStats,
      renderDebugOptions,
      debugUi: {
        hasRenderDebugPanelToggle:
          document.querySelector("#render-debug-panel-toggle") instanceof HTMLButtonElement,
        hasRenderDebugPanel:
          document.querySelector("#render-debug-panel") instanceof HTMLElement,
        hasPerfOverlayToggle:
          document.querySelector("#perf-overlay-toggle") instanceof HTMLButtonElement,
        hasPerfOverlay:
          document.querySelector("#perf-overlay") instanceof HTMLElement,
        hasPostDebugView:
          document.querySelector("#post-debug-view") instanceof HTMLSelectElement,
        hasSkyCloudNoise:
          document.querySelector("#render-debug-sky-cloud-noise") instanceof HTMLInputElement,
        hasPostToneMapping:
          document.querySelector("#post-tone-mapping") instanceof HTMLInputElement,
        hasPostBloom:
          document.querySelector("#post-bloom") instanceof HTMLInputElement,
        hasPostDof:
          document.querySelector("#post-dof") instanceof HTMLInputElement,
        hasPostFog:
          document.querySelector("#post-fog") instanceof HTMLInputElement,
        hasPostReset:
          document.querySelector("#post-debug-reset") instanceof HTMLButtonElement,
        hasWaterDebugView:
          document.querySelector("#water-debug-view") instanceof HTMLSelectElement,
        hasWaterEnabled:
          document.querySelector("#water-enabled") instanceof HTMLInputElement,
        hasWaterReflection:
          document.querySelector("#water-reflection") instanceof HTMLInputElement,
        hasWaterStatus:
          document.querySelector("#water-debug-status") instanceof HTMLElement,
        renderDebugPanelHidden: document.querySelector("#render-debug-panel")?.hidden,
        perfOverlayHidden: document.querySelector("#perf-overlay")?.hidden
      },
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
            frameIndex: status.frameIndex,
            frameDrawCount: status.frameDrawCount,
            frameVisibleDrawCount: status.frameVisibleDrawCount,
            frameShadowDrawCount: status.frameShadowDrawCount,
            frameCulledDrawCount: status.frameCulledDrawCount,
            frameSubmittedVertexCount: status.frameSubmittedVertexCount,
            frameSubmittedIndexCount: status.frameSubmittedIndexCount,
            frameSubmittedTriangleCount: status.frameSubmittedTriangleCount,
            terrainUpdateTotalMs: status.terrainUpdateTotalMs,
            terrainCompletionIngestMs: status.terrainCompletionIngestMs,
            terrainWorkerRequestDrainMs: status.terrainWorkerRequestDrainMs,
            terrainStreamTickMs: status.terrainStreamTickMs,
            terrainStreamSyncMs: status.terrainStreamSyncMs,
            terrainStreamSchedulerMs: status.terrainStreamSchedulerMs,
            terrainStreamWorkerQueueMs: status.terrainStreamWorkerQueueMs,
            terrainStreamVisibilityMs: status.terrainStreamVisibilityMs,
            terrainStreamVisibilitySelectMs: status.terrainStreamVisibilitySelectMs,
            terrainStreamVisibilityStatusMs: status.terrainStreamVisibilityStatusMs,
            terrainStreamVisibilityApplyMs: status.terrainStreamVisibilityApplyMs,
            terrainMeshDestroyMs: status.terrainMeshDestroyMs,
            terrainMeshUploadMs: status.terrainMeshUploadMs,
            terrainCompletionCount: status.terrainCompletionCount,
            terrainCompletionAcceptedCount: status.terrainCompletionAcceptedCount,
            terrainCompletionVertexFloatCount: status.terrainCompletionVertexFloatCount,
            terrainCompletionIndexCount: status.terrainCompletionIndexCount,
            terrainWorkerRequestCount: status.terrainWorkerRequestCount,
            terrainUpdateUpsertedMeshCount: status.terrainUpdateUpsertedMeshCount,
            terrainUpdateRemovedMeshCount: status.terrainUpdateRemovedMeshCount,
            terrainUpdateUploadedVertexFloatCount: status.terrainUpdateUploadedVertexFloatCount,
            terrainUpdateUploadedIndexCount: status.terrainUpdateUploadedIndexCount,
            terrainUpdateDeferredUploadCount: status.terrainUpdateDeferredUploadCount,
            terrainUpdateDeferredRemovalCount: status.terrainUpdateDeferredRemovalCount,
            terrainUpdateUploadBudgetHit: status.terrainUpdateUploadBudgetHit,
            terrainUpdateRemovalBudgetHit: status.terrainUpdateRemovalBudgetHit,
            shadowCascadeCount: status.shadowCascadeCount,
            shadowMapSize: status.shadowMapSize,
            gpuTimerAvailable: status.gpuTimerAvailable,
            gpuTimerUnavailableReason: status.gpuTimerUnavailableReason,
            gpuTimestampPeriodNs: status.gpuTimestampPeriodNs,
            gpuTimerPendingReadbackCount: status.gpuTimerPendingReadbackCount,
            renderDebugOptions: status.renderDebugOptions,
            lastRenderCounters: status.lastRenderCounters,
            lastGpuPassTimings: status.lastGpuPassTimings,
            postProcessRuntime: status.postProcessRuntime,
            postProcessDebugView: status.postProcessDebugView,
            postProcessExposure: status.postProcessExposure,
            postProcessToneMappingEnabled: status.postProcessToneMappingEnabled,
            postProcessBloomEnabled: status.postProcessBloomEnabled,
            postProcessBloomThreshold: status.postProcessBloomThreshold,
            postProcessBloomIntensity: status.postProcessBloomIntensity,
            postProcessDofEnabled: status.postProcessDofEnabled,
            postProcessDofFocusDistance: status.postProcessDofFocusDistance,
            postProcessDofFocusRange: status.postProcessDofFocusRange,
            postProcessDofMaxBlurPixels: status.postProcessDofMaxBlurPixels,
            postProcessFogEnabled: status.postProcessFogEnabled,
            postProcessFogStartDistance: status.postProcessFogStartDistance,
            postProcessFogEndDistance: status.postProcessFogEndDistance,
            postProcessFogDensity: status.postProcessFogDensity,
            postProcessFogColorR: status.postProcessFogColorR,
            postProcessFogColorG: status.postProcessFogColorG,
            postProcessFogColorB: status.postProcessFogColorB,
            postProcessFogCurve: status.postProcessFogCurve,
            waterRuntime: status.waterRuntime,
            waterEnabled: status.waterEnabled,
            waterReflectionEnabled: status.waterReflectionEnabled,
            waterSeaLevelMeters: status.waterSeaLevelMeters,
            waterBathymetryRuntime: status.waterBathymetryRuntime,
            waterBathymetryGridSize: status.waterBathymetryGridSize,
            waterBathymetryWorldSpanMeters: status.waterBathymetryWorldSpanMeters,
            waterBathymetryCenterX: status.waterBathymetryCenterX,
            waterBathymetryCenterZ: status.waterBathymetryCenterZ,
            waterReflectionWidth: status.waterReflectionWidth,
            waterReflectionHeight: status.waterReflectionHeight,
            waterDebugView: status.waterDebugView,
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
function assertDebugContract(debug, expectations = {}) {
  const {
    shadowDebugView = "off",
    postProcessDebugView = defaultPostProcessSettings().postProcessDebugView,
    toneMappingEnabled = defaultPostProcessSettings().postProcessToneMappingEnabled,
    postProcessExposure = defaultPostProcessSettings().postProcessExposure,
    bloomEnabled = defaultPostProcessSettings().postProcessBloomEnabled,
    bloomThreshold = defaultPostProcessSettings().postProcessBloomThreshold,
    bloomIntensity = defaultPostProcessSettings().postProcessBloomIntensity,
    dofEnabled = defaultPostProcessSettings().postProcessDofEnabled,
    dofFocusDistance = defaultPostProcessSettings().postProcessDofFocusDistance,
    dofFocusRange = defaultPostProcessSettings().postProcessDofFocusRange,
    dofMaxBlurPixels = defaultPostProcessSettings().postProcessDofMaxBlurPixels,
    fogEnabled = defaultPostProcessSettings().postProcessFogEnabled,
    fogStartDistance = defaultPostProcessSettings().postProcessFogStartDistance,
    fogEndDistance = defaultPostProcessSettings().postProcessFogEndDistance,
    fogDensity = defaultPostProcessSettings().postProcessFogDensity,
    fogColorR = defaultPostProcessSettings().postProcessFogColorR,
    fogColorG = defaultPostProcessSettings().postProcessFogColorG,
    fogColorB = defaultPostProcessSettings().postProcessFogColorB,
    fogCurve = defaultPostProcessSettings().postProcessFogCurve,
    waterDebugView = defaultWaterSettings().waterDebugView,
    waterEnabled = defaultWaterSettings().waterEnabled,
    waterReflectionEnabled = defaultWaterSettings().waterReflectionEnabled,
    waterSeaLevelMeters = defaultWaterSettings().waterSeaLevelMeters,
    renderDebugOptions = defaultRenderDebugOptions()
  } = expectations;
  if (!debug.hasDebug) {
    throw new Error("Debug API is unavailable.");
  }
  if (
    !debug.hasDumpPerfStats ||
    !debug.hasResetPerfStats ||
    !debug.hasSetRenderDebugOptions ||
    !debug.hasResetRenderDebugOptions
  ) {
    throw new Error(`Perf/debug hooks are missing: ${JSON.stringify(debug)}`);
  }
  if (
    debug.debugUi?.hasRenderDebugPanelToggle !== true ||
    debug.debugUi?.hasRenderDebugPanel !== true ||
    debug.debugUi?.hasPerfOverlayToggle !== true ||
    debug.debugUi?.hasPerfOverlay !== true ||
    debug.debugUi?.hasPostDebugView !== true ||
    debug.debugUi?.hasSkyCloudNoise !== true ||
    debug.debugUi?.hasPostToneMapping !== true ||
    debug.debugUi?.hasPostBloom !== true ||
    debug.debugUi?.hasPostDof !== true ||
    debug.debugUi?.hasPostFog !== true ||
    debug.debugUi?.hasPostReset !== true ||
    debug.debugUi?.hasWaterDebugView !== true ||
    debug.debugUi?.hasWaterEnabled !== true ||
    debug.debugUi?.hasWaterReflection !== true ||
    debug.debugUi?.hasWaterStatus !== true
  ) {
    throw new Error(`Perf/debug UI is missing: ${JSON.stringify(debug.debugUi)}`);
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
  if (debug.shadowDebugView !== shadowDebugView) {
    throw new Error(
      `Expected shadow debug view ${shadowDebugView}, saw ${debug.shadowDebugView}.`
    );
  }
  assertRenderDebugOptions(debug.renderDebugOptions, renderDebugOptions, "debug API");
  assertPerfStats(debug.perfStats);

  const terrainStatus = debug.terrainStreamStatus;
  if (
    terrainStatus === undefined ||
    terrainStatus.pending !== false ||
    terrainStatus.renderedChunkCount <= 0 ||
    terrainStatus.renderedNodeCount <= terrainStatus.renderedChunkCount ||
    terrainStatus.maxRenderedLod < 5 ||
    terrainStatus.visibleWorldSpanXMeters < minRealScaleTerrainSpanMeters ||
    terrainStatus.visibleWorldSpanZMeters < minRealScaleTerrainSpanMeters ||
    terrainStatus.workerPoolRuntime !== "browser-worker" ||
    terrainStatus.terrainWorkerCount <= 1 ||
    terrainStatus.terrainWorkerCount !== debug.terrainWorkerCount ||
    terrainStatus.terrainWorkerCompletedCount <= 0 ||
    terrainStatus.terrainWorkerFailedCount !== 0 ||
    terrainStatus.terrainWorkerStaleCompletionCount !== 0 ||
    terrainStatus.synchronousBuildCount !== 0 ||
    !Array.isArray(terrainStatus.terrainLodSummary) ||
    terrainStatus.terrainLodSummary.filter((summary) => summary.renderedNodeCount > 0).length < 2
  ) {
    throw new Error(`Terrain stream status does not expose multiple rendered LODs: ${JSON.stringify(debug)}`);
  }
  if (debug.terrainWorkerPoolRuntime !== "browser-worker") {
    throw new Error(`Expected browser terrain workers, saw ${JSON.stringify(debug)}`);
  }
  if (
    !Array.isArray(debug.terrainNodeKeys) ||
    !debug.terrainNodeKeys.some((key) => key.startsWith("lod0:")) ||
    !debug.terrainNodeKeys.some((key) => key.startsWith("lod3:") || key.startsWith("lod4:"))
  ) {
    throw new Error(`Terrain node keys do not expose mixed LODs: ${JSON.stringify(debug)}`);
  }
  if (
    !Array.isArray(debug.loadedTerrainNodeKeys) ||
    !debug.loadedTerrainNodeKeys.some((key) => key.startsWith("lod4:"))
  ) {
    throw new Error(`Loaded terrain node keys do not expose coarse LODs: ${JSON.stringify(debug)}`);
  }

  const status = debug.rendererStatus;
  if (
    debug.skyRuntime !== "rust" ||
    !Number.isFinite(debug.skyDayPhase) ||
    debug.skyDayPhase < 0 ||
    debug.skyDayPhase >= 1 ||
    !Number.isFinite(debug.skySunElevation) ||
    debug.skySunElevation < -1 ||
    debug.skySunElevation > 1 ||
    !Number.isFinite(debug.skyCloudCoverage) ||
    debug.skyCloudCoverage < 0 ||
    debug.skyCloudCoverage > 1 ||
    !Number.isFinite(debug.skyStarIntensity) ||
    debug.skyStarIntensity < 0 ||
    debug.skyStarIntensity > 1
  ) {
    throw new Error(`Sky debug contract is not Rust-owned or has invalid values: ${JSON.stringify(debug)}`);
  }
  if (
    status === undefined ||
    !status.configured ||
    status.runtime !== "rust-wgpu" ||
    status.postProcessRuntime !== "rust-wgpu" ||
    status.postProcessDebugView !== postProcessDebugView ||
    debug.postProcessDebugView !== postProcessDebugView ||
    status.postProcessToneMappingEnabled !== toneMappingEnabled ||
    !Number.isFinite(status.postProcessExposure) ||
    Math.abs(status.postProcessExposure - postProcessExposure) > 0.0001 ||
    status.postProcessBloomEnabled !== bloomEnabled ||
    !Number.isFinite(status.postProcessBloomThreshold) ||
    Math.abs(status.postProcessBloomThreshold - bloomThreshold) > 0.0001 ||
    !Number.isFinite(status.postProcessBloomIntensity) ||
    Math.abs(status.postProcessBloomIntensity - bloomIntensity) > 0.0001 ||
    status.postProcessDofEnabled !== dofEnabled ||
    !Number.isFinite(status.postProcessDofFocusDistance) ||
    Math.abs(status.postProcessDofFocusDistance - dofFocusDistance) > 0.0001 ||
    !Number.isFinite(status.postProcessDofFocusRange) ||
    Math.abs(status.postProcessDofFocusRange - dofFocusRange) > 0.0001 ||
    !Number.isFinite(status.postProcessDofMaxBlurPixels) ||
    Math.abs(status.postProcessDofMaxBlurPixels - dofMaxBlurPixels) > 0.0001 ||
    status.postProcessFogEnabled !== fogEnabled ||
    debug.postProcessFogEnabled !== fogEnabled ||
    !Number.isFinite(status.postProcessFogStartDistance) ||
    Math.abs(status.postProcessFogStartDistance - fogStartDistance) > 0.0001 ||
    !Number.isFinite(status.postProcessFogEndDistance) ||
    Math.abs(status.postProcessFogEndDistance - fogEndDistance) > 0.0001 ||
    !Number.isFinite(status.postProcessFogDensity) ||
    Math.abs(status.postProcessFogDensity - fogDensity) > 0.0001 ||
    !Number.isFinite(status.postProcessFogColorR) ||
    Math.abs(status.postProcessFogColorR - fogColorR) > 0.0001 ||
    !Number.isFinite(status.postProcessFogColorG) ||
    Math.abs(status.postProcessFogColorG - fogColorG) > 0.0001 ||
    !Number.isFinite(status.postProcessFogColorB) ||
    Math.abs(status.postProcessFogColorB - fogColorB) > 0.0001 ||
    !Number.isFinite(status.postProcessFogCurve) ||
    Math.abs(status.postProcessFogCurve - fogCurve) > 0.0001 ||
    status.waterRuntime !== "rust-wgpu" ||
    status.waterEnabled !== waterEnabled ||
    status.waterReflectionEnabled !== waterReflectionEnabled ||
    Math.abs(status.waterSeaLevelMeters - waterSeaLevelMeters) > 0.0001 ||
    status.waterBathymetryRuntime !== "rust-heightfield" ||
    status.waterBathymetryGridSize !== 32 ||
    !Number.isFinite(status.waterBathymetryWorldSpanMeters) ||
    status.waterBathymetryWorldSpanMeters < 0 ||
    !Number.isFinite(status.waterBathymetryCenterX) ||
    !Number.isFinite(status.waterBathymetryCenterZ) ||
    !Number.isFinite(status.waterReflectionWidth) ||
    status.waterReflectionWidth <= 0 ||
    !Number.isFinite(status.waterReflectionHeight) ||
    status.waterReflectionHeight <= 0 ||
    status.waterDebugView !== waterDebugView ||
    status.frameDrawCount <= 0 ||
    status.frameVisibleDrawCount <= 0 ||
    status.frameShadowDrawCount <= 0 ||
    !Number.isFinite(status.frameCulledDrawCount) ||
    status.frameCulledDrawCount < 0 ||
    !Number.isFinite(status.frameSubmittedVertexCount) ||
    status.frameSubmittedVertexCount <= 0 ||
    !Number.isFinite(status.frameSubmittedIndexCount) ||
    status.frameSubmittedIndexCount <= 0 ||
    !Number.isFinite(status.frameSubmittedTriangleCount) ||
    status.frameSubmittedTriangleCount <= 0 ||
    !Number.isFinite(status.terrainUpdateTotalMs) ||
    status.terrainUpdateTotalMs < 0 ||
    !Number.isFinite(status.terrainCompletionIngestMs) ||
    status.terrainCompletionIngestMs < 0 ||
    !Number.isFinite(status.terrainWorkerRequestDrainMs) ||
    status.terrainWorkerRequestDrainMs < 0 ||
    !Number.isFinite(status.terrainStreamTickMs) ||
    status.terrainStreamTickMs < 0 ||
    !Number.isFinite(status.terrainStreamSyncMs) ||
    status.terrainStreamSyncMs < 0 ||
    !Number.isFinite(status.terrainStreamSchedulerMs) ||
    status.terrainStreamSchedulerMs < 0 ||
    !Number.isFinite(status.terrainStreamWorkerQueueMs) ||
    status.terrainStreamWorkerQueueMs < 0 ||
    !Number.isFinite(status.terrainStreamVisibilityMs) ||
    status.terrainStreamVisibilityMs < 0 ||
    !Number.isFinite(status.terrainStreamVisibilitySelectMs) ||
    status.terrainStreamVisibilitySelectMs < 0 ||
    !Number.isFinite(status.terrainStreamVisibilityStatusMs) ||
    status.terrainStreamVisibilityStatusMs < 0 ||
    !Number.isFinite(status.terrainStreamVisibilityApplyMs) ||
    status.terrainStreamVisibilityApplyMs < 0 ||
    !Number.isFinite(status.terrainMeshDestroyMs) ||
    status.terrainMeshDestroyMs < 0 ||
    !Number.isFinite(status.terrainMeshUploadMs) ||
    status.terrainMeshUploadMs < 0 ||
    !Number.isFinite(status.terrainCompletionCount) ||
    status.terrainCompletionCount < 0 ||
    !Number.isFinite(status.terrainCompletionAcceptedCount) ||
    status.terrainCompletionAcceptedCount < 0 ||
    !Number.isFinite(status.terrainCompletionVertexFloatCount) ||
    status.terrainCompletionVertexFloatCount < 0 ||
    !Number.isFinite(status.terrainCompletionIndexCount) ||
    status.terrainCompletionIndexCount < 0 ||
    !Number.isFinite(status.terrainWorkerRequestCount) ||
    status.terrainWorkerRequestCount < 0 ||
    !Number.isFinite(status.terrainUpdateUpsertedMeshCount) ||
    status.terrainUpdateUpsertedMeshCount < 0 ||
    !Number.isFinite(status.terrainUpdateRemovedMeshCount) ||
    status.terrainUpdateRemovedMeshCount < 0 ||
    !Number.isFinite(status.terrainUpdateUploadedVertexFloatCount) ||
    status.terrainUpdateUploadedVertexFloatCount < 0 ||
    !Number.isFinite(status.terrainUpdateUploadedIndexCount) ||
    status.terrainUpdateUploadedIndexCount < 0 ||
    !Number.isFinite(status.terrainUpdateDeferredUploadCount) ||
    status.terrainUpdateDeferredUploadCount < 0 ||
    !Number.isFinite(status.terrainUpdateDeferredRemovalCount) ||
    status.terrainUpdateDeferredRemovalCount < 0 ||
    typeof status.terrainUpdateUploadBudgetHit !== "boolean" ||
    typeof status.terrainUpdateRemovalBudgetHit !== "boolean" ||
    status.shadowCascadeCount !== 4 ||
    status.shadowMapSize !== 1024 ||
    status.meshCount <= 0 ||
    status.textureCount <= 0 ||
    status.objectCount <= 0 ||
    status.requiredTextureArrayLayers !== 16 ||
    status.maxTextureArrayLayers < status.requiredTextureArrayLayers
  ) {
    throw new Error(`Renderer status is not a valid Rust/wgpu frame: ${JSON.stringify(debug)}`);
  }
  if (
    typeof status.gpuTimerAvailable !== "boolean" ||
    !Number.isFinite(status.gpuTimestampPeriodNs) ||
    status.gpuTimestampPeriodNs < 0 ||
    !Number.isFinite(status.gpuTimerPendingReadbackCount) ||
    status.gpuTimerPendingReadbackCount < 0 ||
    (!status.gpuTimerAvailable && status.gpuTimerUnavailableReason.length === 0)
  ) {
    throw new Error(`GPU timer status is invalid: ${JSON.stringify(status)}`);
  }
  assertRenderDebugOptions(status.renderDebugOptions, renderDebugOptions, "renderer status");
  assertRenderCounters(status.lastRenderCounters);
  assertGpuPassTimings(status.lastGpuPassTimings, status.gpuTimerAvailable);
}

/// Returns production render-debug defaults expected after load/reset.
function defaultRenderDebugOptions() {
  return {
    terrainLodMask: 0xFFFFFFFF,
    skyEnabled: true,
    skyCloudNoiseEnabled: true,
    shadowPassEnabled: true,
    shadowCascadeMask: 0b1111,
    shadowSamplingEnabled: true,
    shadowSunMode: "production",
    whiteTexturesEnabled: false,
    materialMode: "full"
  };
}

/// Returns production post-process defaults expected after reset.
function defaultPostProcessSettings() {
  return {
    postProcessDebugView: "final",
    postProcessToneMappingEnabled: true,
    postProcessExposure: 1.0,
    postProcessBloomEnabled: true,
    postProcessBloomThreshold: 1.0,
    postProcessBloomIntensity: 0.08,
    postProcessDofEnabled: false,
    postProcessDofFocusDistance: 30,
    postProcessDofFocusRange: 8,
    postProcessDofMaxBlurPixels: 6,
    postProcessFogEnabled: true,
    postProcessFogStartDistance: 200,
    postProcessFogEndDistance: 3000,
    postProcessFogDensity: 1,
    postProcessFogColorR: 1,
    postProcessFogColorG: 1,
    postProcessFogColorB: 1,
    postProcessFogCurve: 1.35
  };
}

/// Returns production water defaults expected after load/reset.
function defaultWaterSettings() {
  return {
    waterDebugView: "final",
    waterEnabled: true,
    waterReflectionEnabled: false,
    waterSeaLevelMeters: 0
  };
}

/// Checks a render-debug option object against expected values.
function assertRenderDebugOptions(actual, expected, label) {
  if (actual === undefined) {
    throw new Error(`Missing ${label} render debug options.`);
  }
  for (const [key, expectedValue] of Object.entries(expected)) {
    if (actual[key] !== expectedValue) {
      throw new Error(
        `Expected ${label} renderDebugOptions.${key}=${expectedValue}, ` +
        `saw ${actual[key]}: ${JSON.stringify(actual)}`
      );
    }
  }
}

/// Checks combined browser/Rust perf stats exposed through the debug API.
function assertPerfStats(stats) {
  if (
    stats === undefined ||
    stats.browserCpu === undefined ||
    stats.rustCpu === undefined ||
    stats.rendererCounters === undefined ||
    stats.gpu === undefined ||
    stats.gpu.timerStatus === undefined
  ) {
    throw new Error(`Perf stats are missing required sections: ${JSON.stringify(stats)}`);
  }
  assertNumericSummary(stats.browserCpu.browserCpu.totalFrameMs, "browser total frame");
  assertNumericSummary(stats.rustCpu.totalFrameMs, "Rust total frame");
  assertNumericSummary(stats.rendererCounters.frameVisibleDrawCount, "visible draws");
  if (
    typeof stats.gpu.timerStatus.available !== "boolean" ||
    !Number.isFinite(stats.gpu.timerStatus.pendingReadbackCount)
  ) {
    throw new Error(`Perf GPU timer status is invalid: ${JSON.stringify(stats.gpu.timerStatus)}`);
  }
  if (!Array.isArray(stats.terrainLodCounters) || stats.terrainLodCounters.length === 0) {
    throw new Error(`Perf stats do not expose terrain LOD counters: ${JSON.stringify(stats)}`);
  }
  if (
    !Array.isArray(stats.shadowCascadeCounters) ||
    stats.shadowCascadeCounters.length !== 4
  ) {
    throw new Error(`Perf stats do not expose four shadow cascade counters: ${JSON.stringify(stats)}`);
  }
}

/// Checks that a numeric perf summary has stable finite values.
function assertNumericSummary(summary, label) {
  if (
    summary === undefined ||
    !Number.isFinite(summary.latest) ||
    !Number.isFinite(summary.min) ||
    !Number.isFinite(summary.max) ||
    !Number.isFinite(summary.average) ||
    !Number.isFinite(summary.p95)
  ) {
    throw new Error(`Invalid ${label} perf summary: ${JSON.stringify(summary)}`);
  }
}

/// Checks latest renderer counters exposed by Rust/wgpu.
function assertRenderCounters(counters) {
  if (
    counters === undefined ||
    counters.frameCandidateCount < counters.frameVisibleDrawCount ||
    counters.frameVisibleDrawCount <= 0 ||
    counters.frameCulledCount < 0 ||
    counters.frameShadowDrawCount <= 0 ||
    counters.submittedVertexCount <= 0 ||
    counters.submittedIndexCount <= 0 ||
    counters.submittedTriangleCount <= 0 ||
    !Array.isArray(counters.terrainLodCounters) ||
    counters.terrainLodCounters.length === 0 ||
    !Array.isArray(counters.shadowCascadeCounters) ||
    counters.shadowCascadeCounters.length !== 4
  ) {
    throw new Error(`Renderer counters are invalid: ${JSON.stringify(counters)}`);
  }
}

/// Checks latest GPU pass timing shape; values may be null when timestamps are unavailable.
function assertGpuPassTimings(timings, gpuTimerAvailable) {
  if (
    timings === undefined ||
    !Array.isArray(timings.shadowCascadeMs) ||
    timings.shadowCascadeMs.length !== 4
  ) {
    throw new Error(`GPU pass timings are invalid: ${JSON.stringify(timings)}`);
  }
  const values = [
    ...timings.shadowCascadeMs,
    timings.sceneMs,
    timings.bloomMs,
    timings.postProcessMs,
    timings.totalMeasuredMs
  ];
  for (const value of values) {
    if (value !== null && (!Number.isFinite(value) || value < 0)) {
      throw new Error(`GPU pass timing contains an invalid value: ${JSON.stringify(timings)}`);
    }
  }
  if (gpuTimerAvailable && timings.sceneMs === null) {
    throw new Error(`GPU timers are available but scene timing is absent: ${JSON.stringify(timings)}`);
  }
}

/// Fails if the Rust-owned sky cycle drifts out of inspectable daylight during smoke.
function assertSkyRemainsInspectable(before, after) {
  const delta = (after.skyDayPhase - before.skyDayPhase + 1) % 1;
  if (
    delta > 0.001 ||
    after.skySunElevation < 0.75 ||
    after.skyStarIntensity !== 0
  ) {
    throw new Error(
      `Sky day phase changed too quickly or left daylight during smoke: ${JSON.stringify({ before, after, delta })}`
    );
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
  let waterLikePixels = 0;
  let bottomDepthDebugPixels = 0;
  let fogFactorDarkPixels = 0;
  let fogFactorBrightPixels = 0;
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
      if (isWaterLikePixel(r, g, b)) {
        waterLikePixels += 1;
      }
      if (isBottomDepthDebugPixel(r, g, b)) {
        bottomDepthDebugPixels += 1;
      }
      if (isFogFactorDarkPixel(r, g, b)) {
        fogFactorDarkPixels += 1;
      }
      if (isFogFactorBrightPixel(r, g, b)) {
        fogFactorBrightPixels += 1;
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
    waterLikePixels,
    bottomDepthDebugPixels,
    fogFactorDarkPixels,
    fogFactorBrightPixels,
    uniqueColorBuckets: buckets.size,
    dominantColorRatio: dominantBucketCount / sampledPixels,
    meanColor: {
      r: sumR / sampledPixels,
      g: sumG / sampledPixels,
      b: sumB / sampledPixels
    }
  };
}

/// Returns true for the darker blue/cyan water range while excluding bright sky.
function isWaterLikePixel(r, g, b) {
  const mean = (r + g + b) / 3;
  return b > 70 &&
    g > 55 &&
    b > r + 18 &&
    g > r + 10 &&
    mean < 190;
}

/// Returns true for grayscale water bottom-depth debug pixels.
function isBottomDepthDebugPixel(r, g, b) {
  const maxChannel = Math.max(r, g, b);
  const minChannel = Math.min(r, g, b);
  return maxChannel - minChannel <= 6 &&
    maxChannel >= 24 &&
    maxChannel <= 245;
}

/// Returns true for near-black grayscale fog-factor pixels.
function isFogFactorDarkPixel(r, g, b) {
  const maxChannel = Math.max(r, g, b);
  const minChannel = Math.min(r, g, b);
  return maxChannel - minChannel <= 8 && maxChannel <= 48;
}

/// Returns true for bright grayscale fog-factor pixels.
function isFogFactorBrightPixel(r, g, b) {
  const maxChannel = Math.max(r, g, b);
  const minChannel = Math.min(r, g, b);
  return maxChannel - minChannel <= 8 && minChannel >= 140;
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

/// Fails if the final water capture does not contain a meaningful water-colored region.
function assertWaterFinalPixels(stats, consoleMessages = []) {
  const ratio = stats.waterLikePixels / stats.sampledPixels;
  if (ratio < 0.08) {
    throw new Error(
      `browser water final screenshot does not contain enough water-like pixels: ` +
      `${JSON.stringify({ ratio, stats })} console=${JSON.stringify(consoleMessages)}`
    );
  }
}

/// Fails if the bottom-depth debug view does not contain grayscale water debug pixels.
function assertWaterBottomDepthPixels(stats, consoleMessages = []) {
  const ratio = stats.bottomDepthDebugPixels / stats.sampledPixels;
  if (ratio < 0.04) {
    throw new Error(
      `browser water bottom-depth screenshot does not contain enough depth-debug pixels: ` +
      `${JSON.stringify({ ratio, stats })} console=${JSON.stringify(consoleMessages)}`
    );
  }
}

/// Fails if the fog factor debug capture does not contain near and far regions.
function assertFogFactorPixels(stats, consoleMessages = []) {
  const darkRatio = stats.fogFactorDarkPixels / stats.sampledPixels;
  const brightRatio = stats.fogFactorBrightPixels / stats.sampledPixels;
  if (darkRatio < 0.02 || brightRatio < 0.01) {
    throw new Error(
      `browser fog factor screenshot does not contain enough near/far fog range: ` +
      `${JSON.stringify({ darkRatio, brightRatio, stats })} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }
}

/// Fails if enabling fog has no measurable effect on the long-horizon capture.
function assertFogChangesFinalPixels(fogOffStats, fogOnStats, consoleMessages = []) {
  const delta =
    Math.abs(fogOffStats.meanColor.r - fogOnStats.meanColor.r) +
    Math.abs(fogOffStats.meanColor.g - fogOnStats.meanColor.g) +
    Math.abs(fogOffStats.meanColor.b - fogOnStats.meanColor.b);
  if (delta < 2.0) {
    throw new Error(
      `browser fog on/off screenshots are too similar: ` +
      `${JSON.stringify({ delta, fogOff: fogOffStats.meanColor, fogOn: fogOnStats.meanColor })} ` +
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
