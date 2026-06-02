import {
  EngineCoreWasmHandle,
  type EngineCorePlayerMode
} from "../../engine/core/engineCoreWasm.js";
import { vec3, type Vec3 } from "../../engine/math/vec3.js";
import type {
  PlayerMode,
  PlayerMovementIntent,
  TransformSnapshot
} from "./playerTypes.js";

export type RustPlayerControllerOptions = {
  readonly initialPosition?: Vec3;
  readonly initialYaw?: number;
  readonly initialPitch?: number;
  readonly initialDebugPosition?: Vec3;
  readonly initialDebugYaw?: number;
  readonly initialDebugPitch?: number;
  readonly initialMode?: PlayerMode;
  readonly terrainHeightAt?: (x: number, z: number) => number | undefined;
};

const ZERO_INTENT: PlayerMovementIntent = Object.freeze({
  forward: 0,
  right: 0,
  up: 0,
  fast: false,
  lookDeltaX: 0,
  lookDeltaY: 0
});

export class RustPlayerController {
  enabled = true;
  private movementIntent: PlayerMovementIntent = ZERO_INTENT;
  private readonly terrainHeightAt?: (x: number, z: number) => number | undefined;
  private readonly initialYaw: number;
  private readonly initialPitch: number;
  private readonly initialDebugPosition?: Vec3;
  private readonly initialDebugYaw: number;
  private readonly initialDebugPitch: number;
  private readonly initialMode: PlayerMode;
  private lastPlayerPosition: Vec3;

  constructor(
    private readonly engine: EngineCoreWasmHandle,
    options: RustPlayerControllerOptions = {}
  ) {
    this.terrainHeightAt = options.terrainHeightAt;
    const initialPosition = options.initialPosition ?? vec3(0, 0, 0);
    this.initialYaw = options.initialYaw ?? 0;
    this.initialPitch = options.initialPitch ?? 0;
    this.initialDebugPosition = options.initialDebugPosition;
    this.initialDebugYaw = options.initialDebugYaw ?? 0;
    this.initialDebugPitch = options.initialDebugPitch ?? -0.35;
    this.initialMode = options.initialMode ?? "firstPerson";
    this.lastPlayerPosition = initialPosition;
    this.ensurePlayer();
  }

  get mode(): PlayerMode {
    return fromEngineMode(this.engine.playerMode() ?? "firstPerson");
  }

  set mode(mode: PlayerMode) {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setPlayerMode(toEngineMode(mode)))) {
      throw new Error("Rust engine rejected player camera mode update.");
    }
  }

  setMovementIntent(intent: PlayerMovementIntent): void {
    this.movementIntent = intent;
  }

  update(deltaSeconds: number): void {
    if (!this.enabled) {
      return;
    }

    this.ensurePlayer();
    if (!this.setPlayerIntentWithRecovery()) {
      throw new Error("Rust engine rejected player movement intent.");
    }

    let preview = this.engine.previewPlayerPosition(deltaSeconds);
    if (!isFiniteVec3(preview)) {
      this.recreatePlayer();
      if (!this.setPlayerIntentWithRecovery()) {
        throw new Error("Rust engine rejected player movement intent after recovery.");
      }
      preview = this.engine.previewPlayerPosition(deltaSeconds);
    }
    const terrainHeight = this.mode === "firstPerson"
      ? this.terrainHeightAt?.(preview.x, preview.z)
      : undefined;
    const finiteTerrainHeight = terrainHeight === undefined || !Number.isFinite(terrainHeight)
      ? undefined
      : terrainHeight;

    if (!this.updatePlayerWithRecovery(deltaSeconds, finiteTerrainHeight)) {
      throw new Error("Rust engine rejected player update.");
    }

    this.syncPlayerPositionFromRust();
  }

  toggleCameraMode(): void {
    this.ensurePlayer();
    let nextMode = this.engine.togglePlayerMode();
    if (nextMode === undefined) {
      this.recreatePlayer();
      nextMode = this.engine.togglePlayerMode();
    }
    if (nextMode === undefined) {
      throw new Error("Rust engine rejected player camera mode toggle.");
    }
  }

  setPlayerPosition(position: Vec3): void {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setPlayerPosition(position))) {
      throw new Error("Rust engine rejected player position update.");
    }
    this.syncPlayerPositionFromRust();
  }

  setPlayerView(yaw: number, pitch: number): void {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setPlayerView(yaw, pitch))) {
      throw new Error("Rust engine rejected player view update.");
    }
  }

  setDebugCamera(position: Vec3, yaw: number, pitch: number): void {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setDebugCamera(position, yaw, pitch))) {
      throw new Error("Rust engine rejected debug camera update.");
    }
  }

  getEyeTransform(): TransformSnapshot {
    this.ensurePlayer();
    const eye = this.engine.playerEyeTransform();

    return {
      position: vec3(eye.position.x, eye.position.y, eye.position.z),
      yaw: eye.yaw,
      pitch: eye.pitch
    };
  }

  getPlayerPosition(): Vec3 {
    this.ensurePlayer();
    this.syncPlayerPositionFromRust();

    return vec3(this.lastPlayerPosition.x, this.lastPlayerPosition.y, this.lastPlayerPosition.z);
  }

  private ensurePlayer(): void {
    if (this.engine.hasPlayer()) {
      return;
    }

    this.createPlayer(this.lastPlayerPosition);
  }

  private createPlayer(position: Vec3): void {
    if (!isFiniteVec3(position)) {
      throw new Error("Rust engine rejected non-finite player position.");
    }

    this.engine.createPlayer(position);
    if (!this.engine.hasPlayer()) {
      throw new Error("Rust engine failed to create player.");
    }

    if (!this.engine.setPlayerView(this.initialYaw, this.initialPitch)) {
      throw new Error("Rust engine rejected player view during initialization.");
    }

    if (this.initialDebugPosition !== undefined) {
      if (!this.engine.setDebugCamera(
        this.initialDebugPosition,
        this.initialDebugYaw,
        this.initialDebugPitch
      )) {
        throw new Error("Rust engine rejected debug camera during initialization.");
      }
    }

    if (!this.engine.setPlayerMode(toEngineMode(this.initialMode))) {
      throw new Error("Rust engine rejected player mode during initialization.");
    }

    this.syncPlayerPositionFromRust();
  }

  private recreatePlayer(): void {
    this.engine.reset();
    this.createPlayer(this.lastPlayerPosition);
  }

  private withPlayerRecovery(action: () => boolean): boolean {
    if (action()) {
      return true;
    }

    this.recreatePlayer();
    return action();
  }

  private setPlayerIntentWithRecovery(): boolean {
    return this.withPlayerRecovery(() => this.engine.setPlayerIntent(this.movementIntent));
  }

  private updatePlayerWithRecovery(deltaSeconds: number, terrainHeight: number | undefined): boolean {
    if (this.engine.updatePlayer(deltaSeconds, terrainHeight)) {
      return true;
    }

    this.recreatePlayer();
    if (!this.engine.setPlayerIntent(this.movementIntent)) {
      return false;
    }

    return this.engine.updatePlayer(deltaSeconds, terrainHeight);
  }

  private syncPlayerPositionFromRust(): void {
    if (!this.engine.hasPlayer()) {
      return;
    }

    const position = this.engine.playerPosition();
    this.lastPlayerPosition = vec3(position.x, position.y, position.z);
  }
}

function toEngineMode(mode: PlayerMode): EngineCorePlayerMode {
  return mode === "firstPerson" ? "firstPerson" : "debugFly";
}

function fromEngineMode(mode: EngineCorePlayerMode): PlayerMode {
  return mode === "firstPerson" ? "firstPerson" : "debugFly";
}

function isFiniteVec3(position: Vec3): boolean {
  return Number.isFinite(position.x) &&
    Number.isFinite(position.y) &&
    Number.isFinite(position.z);
}
