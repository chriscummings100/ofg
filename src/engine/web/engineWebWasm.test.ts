import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ENGINE_WEB_WASM_METADATA } from "../../generated/web/engineWebWasm.js";
import {
  ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM,
  EngineWebGpuBridge,
  instantiateEngineWebWasm,
  loadEngineWebWasm
} from "./engineWebWasm.js";

describe("engine web WASM", () => {
  it("exposes deterministic engine web artifact metadata", () => {
    equal(ENGINE_WEB_WASM_METADATA.id, "engine_web");
    equal(ENGINE_WEB_WASM_METADATA.sourceCrate, "crates/engine_web");
    equal(ENGINE_WEB_WASM_METADATA.assetPath, "assets/wasm/engine_web.wasm");
    equal(ENGINE_WEB_WASM_METADATA.target, "wasm32-unknown-unknown");
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_WEB_WASM_METADATA.artifactHash));
    ok(ENGINE_WEB_WASM_METADATA.exports.includes("ofg_engine_web_version"));
    ok(ENGINE_WEB_WASM_METADATA.exports.includes("ofg_engine_web_configure"));
    ok(ENGINE_WEB_WASM_METADATA.exports.includes("ofg_engine_web_register_mesh"));
    ok(ENGINE_WEB_WASM_METADATA.exports.includes("ofg_engine_web_register_texture"));
    ok(ENGINE_WEB_WASM_METADATA.exports.includes("ofg_engine_web_note_draw"));
  });

  it("instantiates and tracks renderer bridge resources", async () => {
    const bridge = new EngineWebGpuBridge(await loadEngineWeb());

    bridge.reset();
    equal(bridge.status().version, 1);
    equal(bridge.status().configured, false);
    equal(bridge.configure(1280, 720, 16), true);
    equal(bridge.status().configured, true);
    equal(bridge.status().requiredTextureArrayLayers, 16);

    const mesh = bridge.registerMesh({
      vertexFloatCount: 19 * 3,
      indexCount: 3,
      floatsPerVertex: 19
    });
    const texture = bridge.registerTexture({
      width: 64,
      height: 64,
      layers: 16,
      formatCode: ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM
    });
    const object = bridge.registerObject();

    ok(mesh !== undefined);
    ok(texture !== undefined);
    ok(object !== undefined);
    if (mesh === undefined || texture === undefined || object === undefined) {
      ok(false, "Expected renderer bridge resources to be registered.");
      return;
    }
    equal(bridge.status().meshCount, 1);
    equal(bridge.status().textureCount, 1);
    equal(bridge.status().objectCount, 1);
    equal(bridge.beginFrame(1920, 1080), true);
    equal(bridge.noteDraw(mesh, object), true);
    equal(bridge.status().frameIndex, 1n);
    equal(bridge.status().frameDrawCount, 1);
    equal(bridge.status().canvasWidth, 1920);

    equal(bridge.destroyMesh(mesh), true);
    equal(bridge.noteDraw(mesh, object), false);
    equal(bridge.status().lastErrorCode, 7);
    equal(bridge.destroyTexture(texture), true);
    equal(bridge.destroyObject(object), true);
    equal(bridge.status().meshCount, 0);
  });

  it("rejects invalid WebGPU bridge configuration and resources", async () => {
    const bridge = new EngineWebGpuBridge(await loadEngineWeb());

    bridge.reset();
    equal(bridge.configure(1280, 720, 15), false);
    equal(bridge.status().lastErrorCode, 3);
    equal(bridge.registerObject(), undefined);
    equal(bridge.status().lastErrorCode, 1);

    equal(bridge.configure(1280, 720, 16), true);
    equal(bridge.registerMesh({
      vertexFloatCount: 18 * 3,
      indexCount: 3,
      floatsPerVertex: 18
    }), undefined);
    equal(bridge.status().lastErrorCode, 4);
    equal(bridge.registerTexture({
      width: 64,
      height: 64,
      layers: 17,
      formatCode: ENGINE_WEB_TEXTURE_FORMAT_RGBA8_UNORM
    }), undefined);
    equal(bridge.status().lastErrorCode, 5);
  });

  it("rejects missing exports before returning an instance", async () => {
    await assertRejectsWithMessage(
      () => instantiateEngineWebWasm(wasmBytesForMissingExportTest()),
      /Engine Web WASM export is missing: memory/
    );
  });

  it("reports failed fetches with the requested asset path", async () => {
    await assertRejectsWithMessage(
      () =>
        loadEngineWebWasm("missing-engine-web.wasm", async () =>
          new Response(new ArrayBuffer(0), { status: 404 })
        ),
      /Failed to load engine web WASM artifact 'missing-engine-web\.wasm': 404/
    );
  });
});

async function loadEngineWeb() {
  const bytes = readFileSync(ENGINE_WEB_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;

  return instantiateEngineWebWasm(wasmBytes);
}

function wasmBytesForMissingExportTest(): ArrayBuffer {
  const bytes = new Uint8Array([
    0x00,
    0x61,
    0x73,
    0x6d,
    0x01,
    0x00,
    0x00,
    0x00
  ]);

  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
}

async function assertRejectsWithMessage(
  action: () => Promise<unknown>,
  pattern: RegExp
): Promise<void> {
  try {
    await action();
  } catch (error) {
    if (!(error instanceof Error)) {
      ok(false, `Expected an Error, got ${String(error)}`);
      return;
    }
    ok(pattern.test(error.message), `Expected '${error.message}' to match ${pattern}`);
    return;
  }

  ok(false, "Expected promise to reject.");
}
