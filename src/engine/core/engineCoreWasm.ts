import { ENGINE_CORE_WASM_METADATA } from "../../generated/engine/engineCoreWasm.js";

export type EngineCoreWasmExports = {
  readonly memory: WebAssembly.Memory;
  readonly ofg_engine_core_version: () => number;
  readonly ofg_engine_create: () => void;
  readonly ofg_engine_create_entity: () => bigint;
  readonly ofg_engine_create_player: (x: number, y: number, z: number) => bigint;
  readonly ofg_engine_has_player: () => number;
  readonly ofg_engine_player_camera_entity: () => bigint;
  readonly ofg_engine_player_mode: () => number;
  readonly ofg_engine_set_player_mode: (mode: number) => number;
  readonly ofg_engine_toggle_player_mode: () => number;
  readonly ofg_engine_set_player_intent: (
    forward: number,
    right: number,
    up: number,
    fast: number,
    lookDeltaX: number,
    lookDeltaY: number
  ) => number;
  readonly ofg_engine_set_player_position: (x: number, y: number, z: number) => number;
  readonly ofg_engine_set_player_view: (yaw: number, pitch: number) => number;
  readonly ofg_engine_set_debug_camera: (
    x: number,
    y: number,
    z: number,
    yaw: number,
    pitch: number
  ) => number;
  readonly ofg_engine_update_player: (
    deltaSeconds: number,
    terrainHeight: number,
    hasTerrain: number
  ) => number;
  readonly ofg_engine_preview_player_x: (deltaSeconds: number) => number;
  readonly ofg_engine_preview_player_y: (deltaSeconds: number) => number;
  readonly ofg_engine_preview_player_z: (deltaSeconds: number) => number;
  readonly ofg_engine_update: (deltaSeconds: number) => number;
  readonly ofg_engine_tick: () => bigint;
  readonly ofg_engine_elapsed_seconds: () => number;
  readonly ofg_engine_entity_count: () => number;
  readonly ofg_engine_player_eye_x: () => number;
  readonly ofg_engine_player_eye_y: () => number;
  readonly ofg_engine_player_eye_z: () => number;
  readonly ofg_engine_player_eye_yaw: () => number;
  readonly ofg_engine_player_eye_pitch: () => number;
  readonly ofg_engine_player_x: () => number;
  readonly ofg_engine_player_y: () => number;
  readonly ofg_engine_player_z: () => number;
  readonly ofg_engine_render_snapshot_f32_count: () => number;
  readonly ofg_engine_render_snapshot_f32_ptr: () => number;
  readonly ofg_engine_write_render_snapshot: () => number;
};

export type EngineCoreWasmInstance = {
  readonly exports: EngineCoreWasmExports;
};

export type EngineCoreEntityId = {
  readonly raw: bigint;
  readonly index: number;
  readonly generation: number;
};

export type EngineCorePlayerMode = "firstPerson" | "debugFly";

export type EngineCoreVec3 = {
  readonly x: number;
  readonly y: number;
  readonly z: number;
};

export type EngineCorePlayerRig = {
  readonly playerEntity: EngineCoreEntityId;
  readonly cameraEntity: EngineCoreEntityId;
};

export type EngineCorePlayerIntent = {
  readonly forward: number;
  readonly right: number;
  readonly up: number;
  readonly fast: boolean;
  readonly lookDeltaX: number;
  readonly lookDeltaY: number;
};

export type EngineCoreEyeTransform = {
  readonly position: EngineCoreVec3;
  readonly yaw: number;
  readonly pitch: number;
};

export type EngineCoreDebugSnapshot = {
  readonly version: number;
  readonly tick: bigint;
  readonly elapsedSeconds: number;
  readonly entityCount: number;
};

export type EngineCoreRenderCameraPacket = {
  readonly eye: EngineCoreVec3;
  readonly target: EngineCoreVec3;
  readonly yaw: number;
  readonly pitch: number;
  readonly fovYRadians: number;
  readonly nearPlane: number;
  readonly farPlane: number;
};

export type EngineCoreRenderLightPacket = {
  readonly direction: EngineCoreVec3;
  readonly color: EngineCoreVec3;
  readonly intensity: number;
  readonly ambient: number;
};

export type EngineCoreRenderDebugMarkerPacket = {
  readonly visible: boolean;
  readonly position: EngineCoreVec3;
};

export type EngineCoreRenderSnapshot = {
  readonly camera: EngineCoreRenderCameraPacket;
  readonly mainLight: EngineCoreRenderLightPacket;
  readonly playerMarker: EngineCoreRenderDebugMarkerPacket;
};

export const ENGINE_CORE_RENDER_SNAPSHOT_FLOAT_COUNT = 24;

export class EngineCoreWasmHandle {
  readonly #exports: EngineCoreWasmExports;

  constructor(instance: EngineCoreWasmInstance) {
    this.#exports = instance.exports;
  }

  reset(): void {
    this.#exports.ofg_engine_create();
  }

  createEntity(): EngineCoreEntityId {
    return decodeEngineCoreEntityId(this.#exports.ofg_engine_create_entity());
  }

  createPlayer(position: EngineCoreVec3): EngineCorePlayerRig {
    const playerEntity = decodeEngineCoreEntityId(
      this.#exports.ofg_engine_create_player(position.x, position.y, position.z)
    );
    const cameraEntity = decodeEngineCoreEntityId(
      this.#exports.ofg_engine_player_camera_entity()
    );

    return Object.freeze({ playerEntity, cameraEntity });
  }

  hasPlayer(): boolean {
    return this.#exports.ofg_engine_has_player() === 1;
  }

  playerMode(): EngineCorePlayerMode | undefined {
    return playerModeFromCode(this.#exports.ofg_engine_player_mode());
  }

  setPlayerMode(mode: EngineCorePlayerMode): boolean {
    return this.#exports.ofg_engine_set_player_mode(playerModeToCode(mode)) === 1;
  }

  togglePlayerMode(): EngineCorePlayerMode | undefined {
    return playerModeFromCode(this.#exports.ofg_engine_toggle_player_mode());
  }

  setPlayerIntent(intent: EngineCorePlayerIntent): boolean {
    return this.#exports.ofg_engine_set_player_intent(
      intent.forward,
      intent.right,
      intent.up,
      intent.fast ? 1 : 0,
      intent.lookDeltaX,
      intent.lookDeltaY
    ) === 1;
  }

  setPlayerPosition(position: EngineCoreVec3): boolean {
    return this.#exports.ofg_engine_set_player_position(
      position.x,
      position.y,
      position.z
    ) === 1;
  }

  setPlayerView(yaw: number, pitch: number): boolean {
    return this.#exports.ofg_engine_set_player_view(yaw, pitch) === 1;
  }

  setDebugCamera(position: EngineCoreVec3, yaw: number, pitch: number): boolean {
    return this.#exports.ofg_engine_set_debug_camera(
      position.x,
      position.y,
      position.z,
      yaw,
      pitch
    ) === 1;
  }

  previewPlayerPosition(deltaSeconds: number): EngineCoreVec3 {
    return Object.freeze({
      x: this.#exports.ofg_engine_preview_player_x(deltaSeconds),
      y: this.#exports.ofg_engine_preview_player_y(deltaSeconds),
      z: this.#exports.ofg_engine_preview_player_z(deltaSeconds)
    });
  }

  updatePlayer(deltaSeconds: number, terrainHeight?: number): boolean {
    return this.#exports.ofg_engine_update_player(
      deltaSeconds,
      terrainHeight ?? 0,
      terrainHeight === undefined ? 0 : 1
    ) === 1;
  }

  playerPosition(): EngineCoreVec3 {
    return Object.freeze({
      x: this.#exports.ofg_engine_player_x(),
      y: this.#exports.ofg_engine_player_y(),
      z: this.#exports.ofg_engine_player_z()
    });
  }

  playerEyeTransform(): EngineCoreEyeTransform {
    return Object.freeze({
      position: Object.freeze({
        x: this.#exports.ofg_engine_player_eye_x(),
        y: this.#exports.ofg_engine_player_eye_y(),
        z: this.#exports.ofg_engine_player_eye_z()
      }),
      yaw: this.#exports.ofg_engine_player_eye_yaw(),
      pitch: this.#exports.ofg_engine_player_eye_pitch()
    });
  }

  update(deltaSeconds: number): boolean {
    return this.#exports.ofg_engine_update(deltaSeconds) === 1;
  }

  debugSnapshot(): EngineCoreDebugSnapshot {
    return {
      version: this.#exports.ofg_engine_core_version(),
      tick: this.#exports.ofg_engine_tick(),
      elapsedSeconds: this.#exports.ofg_engine_elapsed_seconds(),
      entityCount: this.#exports.ofg_engine_entity_count()
    };
  }

  renderSnapshot(): EngineCoreRenderSnapshot | undefined {
    const values = this.renderSnapshotPacket();
    if (values === undefined) {
      return undefined;
    }

    return Object.freeze({
      camera: Object.freeze({
        eye: freezeVec3(values[0], values[1], values[2]),
        target: freezeVec3(values[3], values[4], values[5]),
        yaw: values[6],
        pitch: values[7],
        fovYRadians: values[8],
        nearPlane: values[9],
        farPlane: values[10]
      }),
      mainLight: Object.freeze({
        direction: freezeVec3(values[11], values[12], values[13]),
        color: freezeVec3(values[14], values[15], values[16]),
        intensity: values[17],
        ambient: values[18]
      }),
      playerMarker: Object.freeze({
        visible: values[19] >= 0.5,
        position: freezeVec3(values[20], values[21], values[22])
      })
    });
  }

  renderSnapshotPacket(): Float32Array | undefined {
    if (this.#exports.ofg_engine_write_render_snapshot() !== 1) {
      return undefined;
    }

    const count = this.#exports.ofg_engine_render_snapshot_f32_count();
    if (count !== ENGINE_CORE_RENDER_SNAPSHOT_FLOAT_COUNT) {
      throw new Error(
        `Engine render snapshot layout changed: expected ` +
        `${ENGINE_CORE_RENDER_SNAPSHOT_FLOAT_COUNT} floats, saw ${count}.`
      );
    }

    const ptr = this.#exports.ofg_engine_render_snapshot_f32_ptr();
    return new Float32Array(new Float32Array(this.#exports.memory.buffer, ptr, count));
  }
}

export async function instantiateEngineCoreWasm(
  bytes: ArrayBuffer
): Promise<EngineCoreWasmInstance> {
  const wasm = await WebAssembly.instantiate(bytes, {});
  const exports = wasm.instance.exports as EngineCoreWasmExports;
  assertEngineCoreExports(exports);

  return Object.freeze({ exports });
}

export async function loadEngineCoreWasm(
  assetPath = ENGINE_CORE_WASM_METADATA.assetPath,
  fetchWasm: typeof fetch = fetch
): Promise<EngineCoreWasmInstance> {
  const response = await fetchWasm(assetPath);
  if (!response.ok) {
    throw new Error(`Failed to load engine WASM artifact '${assetPath}': ${response.status}`);
  }

  return instantiateEngineCoreWasm(await response.arrayBuffer());
}

export function decodeEngineCoreEntityId(raw: bigint): EngineCoreEntityId {
  return Object.freeze({
    raw,
    index: Number(raw & 0xffff_ffffn),
    generation: Number(raw >> 32n)
  });
}

function assertEngineCoreExports(exports: WebAssembly.Exports): asserts exports is EngineCoreWasmExports {
  if (!(exports.memory instanceof WebAssembly.Memory)) {
    throw new Error("Engine WASM export is missing: memory");
  }

  const expectedFunctionNames = [
    "ofg_engine_core_version",
    "ofg_engine_create",
    "ofg_engine_create_entity",
    "ofg_engine_create_player",
    "ofg_engine_has_player",
    "ofg_engine_player_camera_entity",
    "ofg_engine_player_mode",
    "ofg_engine_set_player_mode",
    "ofg_engine_toggle_player_mode",
    "ofg_engine_set_player_intent",
    "ofg_engine_set_player_position",
    "ofg_engine_set_player_view",
    "ofg_engine_set_debug_camera",
    "ofg_engine_update_player",
    "ofg_engine_preview_player_x",
    "ofg_engine_preview_player_y",
    "ofg_engine_preview_player_z",
    "ofg_engine_update",
    "ofg_engine_tick",
    "ofg_engine_elapsed_seconds",
    "ofg_engine_entity_count",
    "ofg_engine_player_eye_x",
    "ofg_engine_player_eye_y",
    "ofg_engine_player_eye_z",
    "ofg_engine_player_eye_yaw",
    "ofg_engine_player_eye_pitch",
    "ofg_engine_player_x",
    "ofg_engine_player_y",
    "ofg_engine_player_z",
    "ofg_engine_render_snapshot_f32_count",
    "ofg_engine_render_snapshot_f32_ptr",
    "ofg_engine_write_render_snapshot"
  ] as const;

  for (const name of expectedFunctionNames) {
    if (typeof exports[name] !== "function") {
      throw new Error(`Engine WASM export is missing: ${name}`);
    }
  }
}

function playerModeToCode(mode: EngineCorePlayerMode): number {
  return mode === "firstPerson" ? 0 : 1;
}

function playerModeFromCode(code: number): EngineCorePlayerMode | undefined {
  if (code === 0) {
    return "firstPerson";
  }

  if (code === 1) {
    return "debugFly";
  }

  return undefined;
}

function freezeVec3(x: number, y: number, z: number): EngineCoreVec3 {
  return Object.freeze({ x, y, z });
}
