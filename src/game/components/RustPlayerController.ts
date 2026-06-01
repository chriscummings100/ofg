import {
  EngineCoreWasmHandle,
  type EngineCorePlayerMode
} from "../../engine/core/engineCoreWasm.js";
import { quatFromYaw } from "../../engine/math/quat.js";
import { vec3, type Vec3 } from "../../engine/math/vec3.js";
import { Component } from "../../engine/scene/Component.js";
import type {
  PlayerMode,
  PlayerMovementIntent,
  TransformSnapshot
} from "./PlayerController.js";

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

export class RustPlayerController extends Component {
  private movementIntent: PlayerMovementIntent = ZERO_INTENT;
  private readonly terrainHeightAt?: (x: number, z: number) => number | undefined;
  private readonly initialPosition?: Vec3;
  private readonly initialYaw: number;
  private readonly initialPitch: number;
  private readonly initialDebugPosition?: Vec3;
  private readonly initialDebugYaw: number;
  private readonly initialDebugPitch: number;
  private readonly initialMode: PlayerMode;

  constructor(
    private readonly engine: EngineCoreWasmHandle,
    options: RustPlayerControllerOptions = {}
  ) {
    super();
    this.terrainHeightAt = options.terrainHeightAt;
    this.initialPosition = options.initialPosition;
    this.initialYaw = options.initialYaw ?? 0;
    this.initialPitch = options.initialPitch ?? 0;
    this.initialDebugPosition = options.initialDebugPosition;
    this.initialDebugYaw = options.initialDebugYaw ?? 0;
    this.initialDebugPitch = options.initialDebugPitch ?? -0.35;
    this.initialMode = options.initialMode ?? "firstPerson";
  }

  get mode(): PlayerMode {
    return fromEngineMode(this.engine.playerMode() ?? "firstPerson");
  }

  set mode(mode: PlayerMode) {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setPlayerMode(toEngineMode(mode)))) {
      throw new Error("Rust engine rejected player camera mode update.");
    }
    this.syncEntityFromRust();
  }

  override onAttach(): void {
    this.ensurePlayer();
    this.syncEntityFromRust();
  }

  setMovementIntent(intent: PlayerMovementIntent): void {
    this.movementIntent = intent;
  }

  override update(deltaSeconds: number): void {
    if (!this.enabled || this.entity === undefined) {
      return;
    }

    this.ensurePlayer();
    if (!this.setPlayerIntentWithRecovery()) {
      throw new Error("Rust engine rejected player movement intent.");
    }

    let preview = this.engine.previewPlayerPosition(deltaSeconds);
    if (!isFiniteVec3(preview)) {
      this.recreatePlayerFromEntity();
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

    this.syncEntityFromRust();
  }

  toggleCameraMode(): void {
    this.ensurePlayer();
    let nextMode = this.engine.togglePlayerMode();
    if (nextMode === undefined) {
      this.recreatePlayerFromEntity();
      nextMode = this.engine.togglePlayerMode();
    }
    if (nextMode === undefined) {
      throw new Error("Rust engine rejected player camera mode toggle.");
    }
    this.syncEntityFromRust();
  }

  setPlayerPosition(position: Vec3): void {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setPlayerPosition(position))) {
      throw new Error("Rust engine rejected player position update.");
    }
    this.syncEntityFromRust();
  }

  setPlayerView(yaw: number, pitch: number): void {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setPlayerView(yaw, pitch))) {
      throw new Error("Rust engine rejected player view update.");
    }
    this.syncEntityFromRust();
  }

  setDebugCamera(position: Vec3, yaw: number, pitch: number): void {
    this.ensurePlayer();
    if (!this.withPlayerRecovery(() => this.engine.setDebugCamera(position, yaw, pitch))) {
      throw new Error("Rust engine rejected debug camera update.");
    }
    this.syncEntityFromRust();
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

  private ensurePlayer(): void {
    if (this.engine.hasPlayer()) {
      return;
    }

    const initialPosition = this.initialPosition ?? this.entity?.transform.position ?? vec3(0, 0, 0);
    this.createPlayer(initialPosition);
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
  }

  private recreatePlayerFromEntity(): void {
    const position = this.entity?.transform.position ?? this.initialPosition ?? vec3(0, 0, 0);
    this.engine.reset();
    this.createPlayer(position);
  }

  private withPlayerRecovery(action: () => boolean): boolean {
    if (action()) {
      return true;
    }

    this.recreatePlayerFromEntity();
    return action();
  }

  private setPlayerIntentWithRecovery(): boolean {
    return this.withPlayerRecovery(() => this.engine.setPlayerIntent(this.movementIntent));
  }

  private updatePlayerWithRecovery(deltaSeconds: number, terrainHeight: number | undefined): boolean {
    if (this.engine.updatePlayer(deltaSeconds, terrainHeight)) {
      return true;
    }

    this.recreatePlayerFromEntity();
    if (!this.engine.setPlayerIntent(this.movementIntent)) {
      return false;
    }

    return this.engine.updatePlayer(deltaSeconds, terrainHeight);
  }

  private syncEntityFromRust(): void {
    if (this.entity === undefined || !this.engine.hasPlayer()) {
      return;
    }

    const position = this.engine.playerPosition();
    const eye = this.engine.playerEyeTransform();
    this.entity.transform.setPosition(vec3(position.x, position.y, position.z));
    this.entity.transform.setRotation(quatFromYaw(eye.yaw));
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
