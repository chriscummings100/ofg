// Captures a short third-person walk in the browser, including frames, telemetry,
// and optional ffmpeg encodes for quick terrain/player grounding review.

import { spawn, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright-core";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_WALK_CAPTURE_PORT ?? "5177", 10);
const headed = process.env.OFG_WALK_CAPTURE_HEADED === "1";
const frameCount = Number.parseInt(process.env.OFG_WALK_CAPTURE_FRAMES ?? "50", 10);
const captureFps = Number.parseInt(process.env.OFG_WALK_CAPTURE_FPS ?? "5", 10);
const viewportWidth = Number.parseInt(process.env.OFG_WALK_CAPTURE_WIDTH ?? "720", 10);
const viewportHeight = Number.parseInt(process.env.OFG_WALK_CAPTURE_HEIGHT ?? "1280", 10);
const runName = process.env.OFG_WALK_CAPTURE_NAME ?? new Date().toISOString().replace(/[:.]/g, "-");
const artifactDir = resolve(root, "artifacts", "terrain-rebuild", runName);
const framesDir = resolve(artifactDir, "frames");

mkdirSync(framesDir, { recursive: true });

const port = await findAvailablePort(preferredPort);
const server = startDevServer(port);

try {
  const url = `http://127.0.0.1:${port}/?terrainSeed=246&terrainPreset=sineGrass`;
  await waitForHttp(url);
  const report = await runCapture(url);
  const encoded = encodeCapture();
  report.encoded = encoded;
  writeFileSync(resolve(artifactDir, "capture-report.json"), `${JSON.stringify(report, null, 2)}\n`);

  console.log("Third-person walk capture complete.");
  console.log(`Artifacts: ${reportPath(artifactDir)}`);
  if (encoded.gif !== undefined) {
    console.log(`GIF: ${reportPath(encoded.gif)}`);
  }
  if (encoded.mp4 !== undefined) {
    console.log(`MP4: ${reportPath(encoded.mp4)}`);
  }
} finally {
  server.kill();
}

/// Runs the Playwright-controlled browser capture and returns per-frame telemetry.
async function runCapture(url) {
  const browser = await chromium.launch({
    executablePath: findBrowserPath(),
    headless: !headed,
    args: [
      "--enable-unsafe-webgpu",
      "--ignore-gpu-blocklist",
      "--disable-gpu-sandbox"
    ]
  });
  const consoleMessages = [];
  let page;

  try {
    page = await browser.newPage({
      viewport: { width: viewportWidth, height: viewportHeight },
      deviceScaleFactor: 1
    });
    page.on("console", (message) => {
      consoleMessages.push(`${message.type()}: ${message.text()}`);
    });
    page.on("pageerror", (error) => {
      consoleMessages.push(`pageerror: ${error.message}`);
    });

    const response = await page.goto(url, { waitUntil: "load" });
    assertResponse(response);
    await waitForBrowserFrame(page);
    await waitForTerrainSettled(page);
    await configureReviewView(page);
    await waitForFrames(page, 30);

    const start = await readCaptureDebug(page);
    const frames = await captureWalkingFrames(page);
    await assertNoBrowserFailures(consoleMessages);

    return {
      kind: "browser-third-person-walk-capture",
      url,
      runName,
      viewport: { width: viewportWidth, height: viewportHeight },
      frameCount,
      captureFps,
      consoleMessages,
      start,
      frames,
      end: await readCaptureDebug(page)
    };
  } catch (error) {
    const errorReport = {
      message: error instanceof Error ? error.message : String(error),
      stack: error instanceof Error ? error.stack : undefined,
      consoleMessages,
      debug: page === undefined ? undefined : await readCaptureDebugSafely(page)
    };
    writeFileSync(
      resolve(artifactDir, "capture-error.json"),
      `${JSON.stringify(errorReport, null, 2)}\n`
    );
    if (page !== undefined) {
      await page.screenshot({ path: resolve(artifactDir, "capture-error.png") }).catch(() => {});
    }
    throw error;
  } finally {
    await browser.close();
  }
}

/// Switches to third person and disables depth-of-field blur for easier inspection.
async function configureReviewView(page) {
  const startingFrameIndex = await rendererFrameIndex(page);
  await page.evaluate(() => {
    window.__ofgDebug?.setCameraMode?.("thirdPerson");
    window.__ofgDebug?.setPostProcessDepthOfField?.(false, 30, 8, 6);
  });
  await page.waitForFunction(({ frameIndex }) => {
    const status = window.__ofgDebug?.getRendererStatus?.();
    return status?.frameIndex > frameIndex &&
      document.querySelector("#camera-mode")?.textContent === "THIRD";
  }, { frameIndex: startingFrameIndex }, { timeout: 10000 });
  await waitForTerrainSettled(page);
}

/// Holds forward movement and stores screenshots plus debug data at a fixed cadence.
async function captureWalkingFrames(page) {
  const frames = [];
  const frameDelayMs = Math.round(1000 / captureFps);

  await page.keyboard.down("ShiftLeft");
  await page.keyboard.down("KeyW");
  try {
    for (let index = 0; index < frameCount; index += 1) {
      await page.waitForTimeout(frameDelayMs);
      await waitForBrowserFrame(page);
      const path = resolve(framesDir, `frame_${index.toString().padStart(4, "0")}.png`);
      await page.screenshot({ path });
      frames.push({
        index,
        path,
        debug: await readCaptureDebug(page)
      });
    }
  } finally {
    await page.keyboard.up("KeyW");
    await page.keyboard.up("ShiftLeft");
  }

  return frames;
}

/// Reads the browser debug hooks relevant to player grounding and visible terrain.
async function readCaptureDebug(page) {
  return await page.evaluate(() => {
    const debug = window.__ofgDebug;
    return {
      hudCameraMode: document.querySelector("#camera-mode")?.textContent,
      player: debug?.getPlayerPosition?.(),
      terrainStreamStatus: debug?.getTerrainStreamStatus?.(),
      terrainNodeKeys: debug?.getTerrainNodeKeys?.(),
      rendererStatus: debug?.getRendererStatus?.(),
      playerCharacterVisible: debug?.getPlayerCharacterVisible?.(),
      playerCharacterFollowsPlayer: debug?.getPlayerCharacterFollowsPlayer?.()
    };
  });
}

/// Reads capture debug data without masking the original capture failure.
async function readCaptureDebugSafely(page) {
  try {
    return await readCaptureDebug(page);
  } catch (error) {
    return {
      error: error instanceof Error ? error.message : String(error)
    };
  }
}

/// Encodes MP4, GIF, and contact-sheet previews when ffmpeg is installed.
function encodeCapture() {
  const ffmpeg = findFfmpegPath();
  if (ffmpeg === undefined) {
    return { ffmpeg: undefined };
  }

  const inputPattern = resolve(framesDir, "frame_%04d.png");
  const mp4 = resolve(artifactDir, `${runName}-10s-5fps.mp4`);
  const gif = resolve(artifactDir, `${runName}-10s-5fps.gif`);
  const contact = resolve(artifactDir, "contact-first-20.png");
  runFfmpeg(ffmpeg, [
    "-y",
    "-framerate",
    String(captureFps),
    "-i",
    inputPattern,
    "-c:v",
    "libx264",
    "-pix_fmt",
    "yuv420p",
    mp4
  ]);
  runFfmpeg(ffmpeg, [
    "-y",
    "-framerate",
    String(captureFps),
    "-i",
    inputPattern,
    "-vf",
    "fps=5,scale=720:-1:flags=lanczos",
    gif
  ]);
  runFfmpeg(ffmpeg, [
    "-y",
    "-framerate",
    String(captureFps),
    "-i",
    inputPattern,
    "-frames:v",
    "1",
    "-vf",
    "select='lt(n,20)',tile=5x4:margin=8:padding=4:color=white,scale=1800:-1",
    contact
  ]);

  return { ffmpeg, mp4, gif, contact };
}

/// Runs ffmpeg and surfaces concise failures.
function runFfmpeg(ffmpeg, args) {
  const result = spawnSync(ffmpeg, args, {
    cwd: root,
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"]
  });
  if (result.status !== 0) {
    throw new Error(`ffmpeg failed: ${result.stderr || result.stdout}`);
  }
}

/// Finds an installed ffmpeg executable.
function findFfmpegPath() {
  const candidates = [
    process.env.OFG_FFMPEG_PATH,
    "C:/ffmpeg/bin/ffmpeg.exe",
    "C:/ffmpeg/ffmpeg.exe",
    "ffmpeg"
  ].filter(Boolean);

  return candidates.find((candidate) => {
    const probe = spawnSync(candidate, ["-version"], { stdio: "ignore" });
    return probe.status === 0;
  });
}

/// Waits until the renderer has produced a new frame.
async function waitForBrowserFrame(page) {
  const start = await rendererFrameIndex(page);
  await page.waitForFunction((frameIndex) => {
    return (window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0) > frameIndex;
  }, start, { timeout: 10000 });
}

/// Returns the current Rust/wgpu renderer frame index.
async function rendererFrameIndex(page) {
  return await page.evaluate(() => window.__ofgDebug?.getRendererStatus?.()?.frameIndex ?? 0);
}

/// Waits until terrain reports no pending or missing visible nodes.
async function waitForTerrainSettled(page) {
  await page.waitForFunction(() => {
    const status = window.__ofgDebug?.getTerrainStreamStatus?.();
    return status !== undefined &&
      status.pending === false &&
      status.missingNodeCount === 0 &&
      status.inFlightChunkCount === 0;
  }, undefined, { timeout: 30000 });
}

/// Waits for a number of browser animation frames.
async function waitForFrames(page, frames) {
  await page.evaluate((frameTotal) => new Promise((resolveFrames) => {
    let remaining = frameTotal;
    function tick() {
      remaining -= 1;
      if (remaining <= 0) {
        resolveFrames();
      } else {
        requestAnimationFrame(tick);
      }
    }
    requestAnimationFrame(tick);
  }), frames);
}

/// Throws if the page response is missing or unsuccessful.
function assertResponse(response) {
  if (response === null || !response.ok()) {
    throw new Error(`Browser capture did not receive a successful page response: ${response?.status()}`);
  }
}

/// Throws on browser console failures.
async function assertNoBrowserFailures(messages) {
  const failures = messages.filter((message) => {
    return message.startsWith("error:") ||
      message.startsWith("pageerror:") ||
      message.includes("panicked at");
  });
  if (failures.length > 0) {
    throw new Error(`Browser capture saw console failures:\n${failures.join("\n")}`);
  }
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
    throw new Error("No Chrome/Edge executable found. Set OFG_BROWSER_PATH.");
  }

  return match;
}

/// Starts the existing OFG static dev server.
function startDevServer(port) {
  const child = spawn(process.execPath, ["tools/dev-server.mjs"], {
    cwd: root,
    env: { ...process.env, PORT: String(port) },
    stdio: ["ignore", "pipe", "pipe"]
  });

  child.stdout.on("data", (chunk) => process.stdout.write(`[dev-server] ${chunk}`));
  child.stderr.on("data", (chunk) => process.stderr.write(`[dev-server] ${chunk}`));

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

/// Finds an available localhost port.
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

/// Resolves after a short timeout.
function sleep(ms) {
  return new Promise((resolveSleep) => setTimeout(resolveSleep, ms));
}

/// Normalizes paths for console and JSON reports.
function reportPath(path) {
  return path.replace(/\\/g, "/");
}
