import { equal, ok } from "node:assert/strict";
import { readFileSync } from "node:fs";
import { EngineCoreWasmHandle, instantiateEngineCoreWasm } from "../../engine/core/engineCoreWasm.js";
import { vec3 } from "../../engine/math/vec3.js";
import { ENGINE_CORE_WASM_METADATA } from "../../generated/engine/engineCoreWasm.js";
import { RustPlayerController } from "./RustPlayerController.js";

describe("RustPlayerController", () => {
  it("creates a Rust player during construction", async () => {
    const engine = await loadEngineHandle();
    const controller = new RustPlayerController(engine, {
      initialPosition: vec3(1, 2, 3),
      initialYaw: 0.75,
      initialPitch: -0.25
    });

    equal(engine.hasPlayer(), true);
    assertClose(controller.getPlayerPosition().x, 1);
    assertClose(controller.getPlayerPosition().y, 2);
    assertClose(controller.getPlayerPosition().z, 3);
    assertClose(controller.getEyeTransform().position.y, 3.65);
    assertClose(controller.getEyeTransform().yaw, 0.75);
  });

  it("updates first-person movement using the Rust preview position for terrain grounding", async () => {
    const engine = await loadEngineHandle();
    const controller = new RustPlayerController(engine, {
      initialPosition: vec3(0, 3, 0),
      terrainHeightAt: (_x, z) => z + 10
    });

    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    controller.update(1);

    assertClose(controller.getPlayerPosition().x, 0);
    assertClose(controller.getPlayerPosition().y, 15.5);
    assertClose(controller.getPlayerPosition().z, 5.5);
    assertClose(controller.getEyeTransform().position.y, 17.15);
  });

  it("moves the debug-fly camera without moving the Rust player position", async () => {
    const engine = await loadEngineHandle();
    const controller = new RustPlayerController(engine, {
      initialPosition: vec3(0, 2, 0),
      initialDebugPosition: vec3(0, 10, 0),
      initialMode: "debugFly",
      terrainHeightAt: () => 100
    });

    controller.setMovementIntent({
      forward: 0,
      right: 0,
      up: 1,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    controller.update(1);

    assertClose(controller.getPlayerPosition().y, 2);
    assertClose(controller.getEyeTransform().position.y, 21);
    equal(controller.mode, "debugFly");
  });

  it("supports debug camera and player position commands for browser debug hooks", async () => {
    const engine = await loadEngineHandle();
    const controller = new RustPlayerController(engine);

    controller.setPlayerPosition(vec3(4, 5, 6));
    controller.setPlayerView(0.25, -0.5);
    assertClose(controller.getPlayerPosition().x, 4);
    assertClose(controller.getEyeTransform().yaw, 0.25);

    controller.setDebugCamera(vec3(7, 8, 9), 0.75, -0.25);
    equal(controller.mode, "debugFly");
    assertClose(controller.getEyeTransform().position.x, 7);
    assertClose(controller.getEyeTransform().position.y, 8);
    assertClose(controller.getEyeTransform().yaw, 0.75);
  });

  it("does not update the Rust player when disabled", async () => {
    const engine = await loadEngineHandle();
    const controller = new RustPlayerController(engine, {
      initialPosition: vec3(0, 0, 0)
    });
    controller.enabled = false;
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    controller.update(1);

    assertClose(controller.getPlayerPosition().z, 0);
    ok(engine.hasPlayer());
  });

  it("recovers if the Rust engine loses its player between browser frames", async () => {
    const engine = await loadEngineHandle();
    const controller = new RustPlayerController(engine, {
      initialPosition: vec3(0, 0, 0)
    });
    controller.setMovementIntent({
      forward: 1,
      right: 0,
      up: 0,
      fast: false,
      lookDeltaX: 0,
      lookDeltaY: 0
    });

    engine.reset();
    controller.update(1);

    ok(engine.hasPlayer());
    assertClose(controller.getPlayerPosition().z, 5.5);
  });
});

async function loadEngineHandle(): Promise<EngineCoreWasmHandle> {
  const bytes = readFileSync(ENGINE_CORE_WASM_METADATA.assetPath);
  const wasmBytes = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength
  ) as ArrayBuffer;
  const instance = await instantiateEngineCoreWasm(wasmBytes);
  const handle = new EngineCoreWasmHandle(instance);
  handle.reset();

  return handle;
}

function assertClose(actual: number, expected: number): void {
  const epsilon = 0.00001;
  ok(
    Math.abs(actual - expected) <= epsilon,
    `Expected ${actual} to be within ${epsilon} of ${expected}`
  );
}
