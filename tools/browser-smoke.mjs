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
const minMultiKmTerrainSpanMeters = 4096;

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

    const diagnosticRenderOptions = {
      skyEnabled: false,
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
    const perfOverlayUi = await exercisePerfOverlayUi(page);
    assertNoBrowserFailures(consoleMessages);

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
        cascadeDebugImage,
        visibilityDebugImage,
        linearDepthImage,
        bloomImage,
        postToneMapImage,
        dofCocImage,
        dofBlurredImage,
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
      cascadeDebug,
      visibilityDebug,
      depthDebug,
      linearDepthDebug,
      bloomDebug,
      postToneMapDebug,
      dofCocDebug,
      dofBlurredDebug,
      renderDebugDisabled,
      renderDebugReset,
      renderDebugUi,
      renderDebugUiReset,
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

/// Reads the current Rust/wgpu renderer frame index.
async function rendererFrameIndex(page) {
  return page.evaluate(() =>
    window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0
  );
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
      status.maxRenderedLod >= 3 &&
      status.visibleWorldSpanXMeters >= minSpanMeters &&
      status.visibleWorldSpanZMeters >= minSpanMeters &&
      terrainNodeKeys.some((key) => key.startsWith("lod0:")) &&
      terrainNodeKeys.some((key) => key.startsWith("lod3:") || key.startsWith("lod4:"));
  }, minMultiKmTerrainSpanMeters, { timeout: 120000 });
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
            frameIndex: status.frameIndex.toString(),
            frameDrawCount: status.frameDrawCount,
            frameVisibleDrawCount: status.frameVisibleDrawCount,
            frameShadowDrawCount: status.frameShadowDrawCount,
            frameCulledDrawCount: status.frameCulledDrawCount,
            frameSubmittedVertexCount: status.frameSubmittedVertexCount,
            frameSubmittedIndexCount: status.frameSubmittedIndexCount,
            frameSubmittedTriangleCount: status.frameSubmittedTriangleCount,
            terrainUpdateTotalMs: status.terrainUpdateTotalMs,
            terrainUpdateUpsertedMeshCount: status.terrainUpdateUpsertedMeshCount,
            terrainUpdateRemovedMeshCount: status.terrainUpdateRemovedMeshCount,
            terrainUpdateUploadedVertexFloatCount: status.terrainUpdateUploadedVertexFloatCount,
            terrainUpdateUploadedIndexCount: status.terrainUpdateUploadedIndexCount,
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
    postProcessDebugView = "final",
    postProcessExposure = 1.0,
    bloomThreshold = 1.0,
    bloomIntensity = 0.08,
    dofEnabled = false,
    dofFocusDistance = 30,
    dofFocusRange = 8,
    dofMaxBlurPixels = 6,
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
    debug.debugUi?.hasPerfOverlay !== true
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
    terrainStatus.maxRenderedLod < 3 ||
    terrainStatus.visibleWorldSpanXMeters < minMultiKmTerrainSpanMeters ||
    terrainStatus.visibleWorldSpanZMeters < minMultiKmTerrainSpanMeters ||
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
    status.postProcessToneMappingEnabled !== true ||
    !Number.isFinite(status.postProcessExposure) ||
    Math.abs(status.postProcessExposure - postProcessExposure) > 0.0001 ||
    status.postProcessBloomEnabled !== true ||
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
    !Number.isFinite(status.terrainUpdateUpsertedMeshCount) ||
    status.terrainUpdateUpsertedMeshCount < 0 ||
    !Number.isFinite(status.terrainUpdateRemovedMeshCount) ||
    status.terrainUpdateRemovedMeshCount < 0 ||
    !Number.isFinite(status.terrainUpdateUploadedVertexFloatCount) ||
    status.terrainUpdateUploadedVertexFloatCount < 0 ||
    !Number.isFinite(status.terrainUpdateUploadedIndexCount) ||
    status.terrainUpdateUploadedIndexCount < 0 ||
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
    shadowPassEnabled: true,
    shadowCascadeMask: 0b1111,
    shadowSamplingEnabled: true,
    shadowSunMode: "production",
    whiteTexturesEnabled: false,
    materialMode: "full"
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
