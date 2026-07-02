// Tests for the default C++/WASM TypeScript runtime adapter.
//
// These tests run without loading real WASM. They validate Embind ownership,
// async module creation, and the shared debug-status parser used by the app.
import assert from "node:assert/strict";
import {
  createBrowserGameRuntimeFromModule,
  createBrowserGameRuntimeFromRaw,
  parseRuntimeDebugStatus,
  type GeneratedWasmModule,
  type RawBrowserGame
} from "../../src/app/wasmRuntime.js";

describe("wasm runtime wrapper", () => {
  // Verifies the parser accepts the C++ runtime debug-status contract.
  it("parses runtime debug status JSON from C++", () => {
    const status = parseRuntimeDebugStatus(JSON.stringify(validStatusPayload()));

    assert.equal(status.initialized, true);
    assert.equal(status.lifecycleState, "ready");
    assert.equal(status.frameCount, 2);
    assert.equal(status.canvasWidth, 800);
    assert.equal(status.modelLoadingState, "loaded");
    assert.equal(status.playerModelLoaded, true);
    assert.equal(status.pipelineCreateCount, 1);
  });

  // Verifies missing fields fail with useful parser errors.
  it("rejects missing runtime debug status fields", () => {
    assert.throws(
      () => parseRuntimeDebugStatus(JSON.stringify({ initialized: true })),
      /field lifecycleState must be a string/
    );
  });

  // Verifies numeric contract validation catches invalid counters.
  it("rejects runtime debug status fields with invalid types", () => {
    const payload = validStatusPayload();
    payload.frameCount = -1;

    assert.throws(
      () => parseRuntimeDebugStatus(JSON.stringify(payload)),
      /field frameCount must be a non-negative integer/
    );
  });

  // Verifies wrapper calls delegate to the raw Embind runtime and delete once.
  it("delegates lifecycle calls to the raw Embind runtime", () => {
    const calls: string[] = [];
    const raw = fakeRawBrowserGame(calls);

    const runtime = createBrowserGameRuntimeFromRaw(raw);
    runtime.resize(800, 450, 1);
    runtime.setControlInput({
      moveX: 1,
      moveY: 0,
      moveZ: -1,
      lookDeltaX: 2,
      lookDeltaY: 3,
      lookActive: true,
      fast: true,
      slow: false,
      cycleCameraMode: true
    });
    runtime.frame(16.5);
    runtime.loadPlayerModel(new Uint8Array([1, 2, 3]), new Uint8Array([4, 5]));
    runtime.reportPlayerModelLoadError("fetch failed");
    assert.equal(runtime.debugStatus().frameCount, 2);
    runtime.dispose();
    runtime.dispose();

    assert.deepEqual(calls, [
      "resize:800:450:1",
      "controlInput:1:0:-1:2:3:true:true:false:true",
      "frame:16.5",
      "loadPlayerModel:3:2",
      "modelError:fetch failed",
      "debug",
      "dispose",
      "delete"
    ]);
    assert.throws(
      () => runtime.frame(33),
      /Browser game runtime has been disposed/
    );
  });

  // Verifies invalid control input is rejected before Embind forwarding.
  it("rejects non-finite control input", () => {
    const calls: string[] = [];
    const runtime = createBrowserGameRuntimeFromRaw(fakeRawBrowserGame(calls));

    assert.throws(
      () =>
        runtime.setControlInput({
          moveX: Number.POSITIVE_INFINITY,
          moveY: 0,
          moveZ: 0,
          lookDeltaX: 0,
          lookDeltaY: 0,
          lookActive: false,
          fast: false,
          slow: false,
          cycleCameraMode: false
        }),
      /field moveX must be a finite number/
    );
    assert.deepEqual(calls, []);
    runtime.dispose();
  });

  // Verifies Embind's create result can be sync or promise-like.
  it("normalizes async C++ module creation", async () => {
    const calls: string[] = [];
    const canvas = document.createElement("canvas");
    const module: GeneratedWasmModule = {
      BrowserGame: {
        async create(receivedCanvas: HTMLCanvasElement): Promise<RawBrowserGame> {
          calls.push(receivedCanvas === canvas ? "canvas" : "wrong canvas");
          return fakeRawBrowserGame(calls);
        }
      }
    };

    const runtime = await createBrowserGameRuntimeFromModule(module, canvas);
    runtime.frame(1);
    runtime.dispose();

    assert.deepEqual(calls, ["canvas", "frame:1", "dispose", "delete"]);
  });

  // Verifies non-object JSON payloads fail before field validation.
  it("rejects non-object runtime debug status payloads", () => {
    assert.throws(() => parseRuntimeDebugStatus("null"), /must be an object/);
  });

  // Verifies scalar field type errors remain precise for diagnostics.
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

// Builds a fake raw Embind object that records every lifecycle call.
function fakeRawBrowserGame(calls: string[]): RawBrowserGame {
  return {
    // Records resize forwarding.
    resize(width, height, devicePixelRatio) {
      calls.push(`resize:${width}:${height}:${devicePixelRatio}`);
    },
    // Records frame forwarding.
    frame(timeMs) {
      calls.push(`frame:${timeMs}`);
    },
    // Records control input forwarding.
    set_control_input(
      moveX,
      moveY,
      moveZ,
      lookDeltaX,
      lookDeltaY,
      lookActive,
      fast,
      slow,
      cycleCameraMode
    ) {
      calls.push(
        `controlInput:${moveX}:${moveY}:${moveZ}:${lookDeltaX}:${lookDeltaY}:${lookActive}:${fast}:${slow}:${cycleCameraMode}`
      );
    },
    // Records debug-status reads and returns a valid status payload.
    debug_status_json() {
      calls.push("debug");
      return JSON.stringify(validStatusPayload());
    },
    // Records player model byte transport.
    load_player_model(playerBytes, animationBytes) {
      calls.push(`loadPlayerModel:${playerBytes.byteLength}:${animationBytes.byteLength}`);
    },
    // Records player model loading errors.
    report_player_model_load_error(message) {
      calls.push(`modelError:${message}`);
    },
    // Records runtime disposal.
    dispose() {
      calls.push("dispose");
    },
    // Records Embind wrapper deletion.
    delete() {
      calls.push("delete");
    }
  };
}

// Returns a valid C++ runtime debug-status payload for parser tests.
function validStatusPayload(): Record<string, unknown> {
  return {
    initialized: true,
    lifecycleState: "ready",
    frameCount: 2,
    canvasWidth: 800,
    canvasHeight: 450,
    devicePixelRatio: 1,
    surfaceFormat: "Bgra8UnormSrgb",
    adapterName: "test adapter",
    backend: "BrowserWebGpu",
    cameraMode: "debug",
    modelLoadingState: "loaded",
    playerModelLoaded: true,
    pipelineCreateCount: 1,
    bufferCreateCount: 1,
    surfaceConfigureCount: 1,
    lastError: null
  };
}
