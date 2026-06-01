import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ENGINE_CORE_WASM_METADATA } from "../../generated/engine/engineCoreWasm.js";
import {
  EngineCoreWasmHandle,
  decodeEngineCoreEntityId,
  instantiateEngineCoreWasm,
  loadEngineCoreWasm
} from "./engineCoreWasm.js";

describe("engine core WASM", () => {
  it("exposes deterministic engine core artifact metadata", () => {
    equal(ENGINE_CORE_WASM_METADATA.id, "engine_core");
    equal(ENGINE_CORE_WASM_METADATA.sourceCrate, "crates/engine_core");
    equal(ENGINE_CORE_WASM_METADATA.assetPath, "assets/wasm/engine_core.wasm");
    equal(ENGINE_CORE_WASM_METADATA.target, "wasm32-unknown-unknown");
    ok(/^sha256-[0-9a-f]{64}$/.test(ENGINE_CORE_WASM_METADATA.artifactHash));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_core_version"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_create"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_create_entity"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_create_player"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_set_player_intent"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_update_player"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_player_eye_y"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_update"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_tick"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_elapsed_seconds"));
    ok(ENGINE_CORE_WASM_METADATA.exports.includes("ofg_engine_entity_count"));
  });

  it("instantiates the generated WASM artifact", async () => {
    const wasm = await loadEngineCore();

    equal(wasm.exports.ofg_engine_core_version(), 1);
    equal(wasm.exports.ofg_engine_tick(), 0n);
    equal(wasm.exports.ofg_engine_entity_count(), 0);
    ok(wasm.exports.memory.buffer.byteLength > 0);
  });

  it("creates entities and exposes debug snapshots through the handle", async () => {
    const handle = new EngineCoreWasmHandle(await loadEngineCore());

    handle.reset();
    equal(handle.debugSnapshot().version, 1);
    equal(handle.debugSnapshot().tick, 0n);
    equal(handle.debugSnapshot().entityCount, 0);

    const first = handle.createEntity();
    equal(first.raw, 0n);
    equal(first.index, 0);
    equal(first.generation, 0);

    equal(handle.update(0.25), true);
    equal(handle.update(Number.POSITIVE_INFINITY), false);

    const afterUpdate = handle.debugSnapshot();
    equal(afterUpdate.tick, 1n);
    equal(afterUpdate.elapsedSeconds, 0.25);
    equal(afterUpdate.entityCount, 1);

    handle.reset();
    const afterReset = handle.debugSnapshot();
    equal(afterReset.tick, 0n);
    equal(afterReset.entityCount, 0);
  });

  it("creates a Rust-owned player and camera rig", async () => {
    const handle = new EngineCoreWasmHandle(await loadEngineCore());

    handle.reset();
    equal(handle.hasPlayer(), false);
    const rig = handle.createPlayer({ x: 1, y: 2, z: 3 });

    equal(handle.hasPlayer(), true);
    equal(rig.playerEntity.index, 0);
    equal(rig.cameraEntity.index, 1);
    equal(handle.debugSnapshot().entityCount, 2);
    equal(handle.playerMode(), "firstPerson");

    const eye = handle.playerEyeTransform();
    equal(eye.position.x, 1);
    assertClose(eye.position.y, 3.65);
    equal(eye.position.z, 3);
    equal(eye.yaw, 0);
    equal(eye.pitch, 0);
  });

  it("updates first-person player movement and terrain grounding in WASM", async () => {
    const handle = new EngineCoreWasmHandle(await loadEngineCore());

    handle.reset();
    handle.createPlayer({ x: 0, y: 3, z: 0 });
    equal(handle.setPlayerIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    }), true);
    equal(handle.updatePlayer(1, 4), true);

    const player = handle.playerPosition();
    equal(player.x, 0);
    equal(player.y, 4);
    equal(player.z, 5.5);
    assertClose(handle.playerEyeTransform().position.y, 5.65);
  });

  it("updates debug-fly player movement without terrain grounding in WASM", async () => {
    const handle = new EngineCoreWasmHandle(await loadEngineCore());

    handle.reset();
    handle.createPlayer({ x: 0, y: 2, z: 0 });
    equal(handle.togglePlayerMode(), "debugFly");
    equal(handle.setPlayerIntent({
      forward: 0,
      right: 0,
      up: 1,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    }), true);
    equal(handle.updatePlayer(1, 100), true);

    equal(handle.playerPosition().y, 2);
    equal(handle.playerEyeTransform().position.y, 25);
    equal(handle.setPlayerMode("firstPerson"), true);
    equal(handle.playerMode(), "firstPerson");
  });

  it("reports missing player updates as unsuccessful", async () => {
    const handle = new EngineCoreWasmHandle(await loadEngineCore());

    handle.reset();
    equal(handle.updatePlayer(1), false);
    equal(handle.setPlayerIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    }), false);
    ok(Number.isNaN(handle.playerEyeTransform().position.x));
    equal(handle.playerMode(), undefined);
  });

  it("decodes packed generational entity ids", () => {
    equal(decodeEngineCoreEntityId(0n).index, 0);
    equal(decodeEngineCoreEntityId(0n).generation, 0);

    const decoded = decodeEngineCoreEntityId((7n << 32n) | 42n);
    equal(decoded.raw, 30064771114n);
    equal(decoded.index, 42);
    equal(decoded.generation, 7);
  });

  it("rejects missing exports before returning an instance", async () => {
    const module = new WebAssembly.Module(wasmBytesForMissingExportTest());
    const bytes = WebAssembly.Module.customSections(module, "unused");
    ok(Array.isArray(bytes));

    await assertRejectsWithMessage(
      () => instantiateEngineCoreWasm(wasmBytesForMissingExportTest()),
      /Engine WASM export is missing: memory/
    );
  });

  it("reports failed fetches with the requested asset path", async () => {
    await assertRejectsWithMessage(
      () =>
        loadEngineCoreWasm("missing-engine.wasm", async () =>
          new Response(new ArrayBuffer(0), { status: 404 })
        ),
      /Failed to load engine WASM artifact 'missing-engine\.wasm': 404/
    );
  });
});

async function loadEngineCore() {
  const bytes = readFileSync(ENGINE_CORE_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;

  return instantiateEngineCoreWasm(wasmBytes);
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

function assertClose(actual: number, expected: number): void {
  const epsilon = 0.00001;
  ok(
    Math.abs(actual - expected) <= epsilon,
    `Expected ${actual} to be within ${epsilon} of ${expected}`
  );
}
