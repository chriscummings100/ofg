import assert from "node:assert/strict";
import {
  createBrowserGameRuntimeFromRaw,
  parseRuntimeDebugStatus,
  type RawBrowserGame
} from "../../src/app/wasmRuntime.js";

describe("wasm runtime wrapper", () => {
  it("parses runtime debug status JSON from Rust", () => {
    const status = parseRuntimeDebugStatus(
      JSON.stringify({
        initialized: true,
        frameCount: 2,
        canvasWidth: 800,
        canvasHeight: 450,
        devicePixelRatio: 1,
        surfaceFormat: "Bgra8UnormSrgb",
        adapterName: "test adapter",
        backend: "BrowserWebGpu",
        pipelineCreateCount: 1,
        bufferCreateCount: 1,
        surfaceConfigureCount: 1,
        lastError: null
      })
    );

    assert.equal(status.initialized, true);
    assert.equal(status.frameCount, 2);
    assert.equal(status.canvasWidth, 800);
    assert.equal(status.pipelineCreateCount, 1);
  });

  it("rejects missing runtime debug status fields", () => {
    assert.throws(
      () => parseRuntimeDebugStatus(JSON.stringify({ initialized: true })),
      /field frameCount must be a finite number/
    );
  });

  it("rejects runtime debug status fields with invalid types", () => {
    const payload = validStatusPayload();
    payload.frameCount = -1;

    assert.throws(
      () => parseRuntimeDebugStatus(JSON.stringify(payload)),
      /field frameCount must be a non-negative integer/
    );
  });

  it("delegates lifecycle calls to the raw wasm-bindgen runtime", () => {
    const calls: string[] = [];
    const raw: RawBrowserGame = {
      resize(width, height, devicePixelRatio) {
        calls.push(`resize:${width}:${height}:${devicePixelRatio}`);
      },
      frame(timeMs) {
        calls.push(`frame:${timeMs}`);
      },
      debug_status_json() {
        calls.push("debug");
        return JSON.stringify(validStatusPayload());
      },
      dispose() {
        calls.push("dispose");
      },
      free() {
        calls.push("free");
      }
    };

    const runtime = createBrowserGameRuntimeFromRaw(raw);
    runtime.resize(800, 450, 1);
    runtime.frame(16.5);
    assert.equal(runtime.debugStatus().frameCount, 2);
    runtime.dispose();
    runtime.dispose();

    assert.deepEqual(calls, [
      "resize:800:450:1",
      "frame:16.5",
      "debug",
      "dispose",
      "free"
    ]);
    assert.throws(
      () => runtime.frame(33),
      /Browser game runtime has been disposed/
    );
  });

  it("rejects non-object runtime debug status payloads", () => {
    assert.throws(
      () => parseRuntimeDebugStatus("null"),
      /must be an object/
    );
  });

  it("rejects invalid boolean, string, and nullable string fields", () => {
    const invalidInitialized = validStatusPayload();
    invalidInitialized.initialized = "yes";
    assert.throws(
      () => parseRuntimeDebugStatus(JSON.stringify(invalidInitialized)),
      /field initialized must be a boolean/
    );

    const invalidFormat = validStatusPayload();
    invalidFormat.surfaceFormat = 1;
    assert.throws(
      () => parseRuntimeDebugStatus(JSON.stringify(invalidFormat)),
      /field surfaceFormat must be a string/
    );

    const invalidLastError = validStatusPayload();
    invalidLastError.lastError = false;
    assert.throws(
      () => parseRuntimeDebugStatus(JSON.stringify(invalidLastError)),
      /field lastError must be a string or null/
    );
  });
});

function validStatusPayload(): Record<string, unknown> {
  return {
    initialized: true,
    frameCount: 2,
    canvasWidth: 800,
    canvasHeight: 450,
    devicePixelRatio: 1,
    surfaceFormat: "Bgra8UnormSrgb",
    adapterName: "test adapter",
    backend: "BrowserWebGpu",
    pipelineCreateCount: 1,
    bufferCreateCount: 1,
    surfaceConfigureCount: 1,
    lastError: null
  };
}
