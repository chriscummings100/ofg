# Browser/Rust API Contract

This document defines the target boundary between TypeScript and Rust for the
browser game. It is a living contract. If the boundary changes, update this file,
`docs/RUST_ENGINE_PLAN.md`, and `docs/TYPESCRIPT_REDUCTION_AUDIT.md`.

The purpose of this contract is to make migration progress measurable. Progress
is not "less TypeScript than before"; progress is the removal of public
TypeScript-to-Rust calls and Rust-to-browser services that are not listed here.

## Boundary Rules

- TypeScript owns browser page concerns: loading the WASM module, finding the
  canvas, collecting DOM input, forwarding UI/debug commands, updating HTML HUD,
  and running browser smoke hooks.
- Rust owns engine concerns: world state, player/camera, terrain generation,
  terrain streaming, worker job semantics, simulation, save/load schemas, render
  extraction, WebGPU resources, and draw submission.
- TypeScript must not know about density chunks, terrain LOD stages, terrain
  mesh packets, terrain material layer selection, renderer resource handles,
  scene graphs, ECS storage, factory simulation, or per-frame draw lists.
- Public calls across the TypeScript/Rust boundary should be coarse and batched.
  If TypeScript calls a function once per entity, chunk, mesh, material, or draw,
  the boundary has regressed.
- Debug and smoke visibility should come from Rust snapshots. TypeScript may
  display or assert those snapshots, but it should not assemble them from
  multiple terrain/render internals.

## Target TypeScript-To-Rust API

This is the final public API shape TypeScript should use. Names may change during
implementation only by updating this document first.

```ts
export type OfgGameModule = {
  create(canvas: HTMLCanvasElement, init: GameInit): Promise<OfgGame>;
};

export type OfgGame = {
  tick(frame: BrowserFrameInput): void;
  resize(viewport: BrowserViewport): void;
  command(command: GameCommand): void;
  debugSnapshot(): GameDebugSnapshot;
  save(): Uint8Array;
  load(saveBytes: Uint8Array): void;
  dispose(): void;
};
```

`create(canvas, init)` initializes the Rust-owned game, terrain, renderer, assets,
and worker/threading runtime. TypeScript passes the canvas and startup config; it
does not separately initialize terrain, textures, render resources, workers, or
player state.

`tick(frame)` is the normal frame call. It advances input, player/camera,
simulation, terrain streaming, worker result consumption, GPU uploads, and render
submission. TypeScript should not call a separate render function in the target
API.

`resize(viewport)` forwards browser viewport size and device-pixel-ratio changes.
Rust owns canvas backing size validation and renderer reconfiguration.

`command(command)` is the single control lane for UI, debug, and tuning actions.
It replaces many narrow functions such as `setPlayerPosition`,
`setDebugCamera`, `setTerrainConfig`, `resetStreaming`, and camera mode toggles.
Commands are explicit Rust-owned data, not arbitrary stringly hooks.

`debugSnapshot()` returns a Rust-assembled snapshot for HUDs, tuning UI, smoke
tests, and screenshots. It may include player mode, player position, stream
status, renderer status, active chunk counts, worker timings, and tuning values.
TypeScript reads the snapshot but does not derive it.

`save()` and `load(saveBytes)` are final-shape APIs for durable game state. They
can remain unimplemented until save/load work begins, but save ownership belongs
to Rust.

`dispose()` releases Rust-owned renderer resources, workers, event-side handles,
and memory. It should make repeated browser smoke runs and future game switching
safe.

## Target Input And Command Data

The target frame input is one object, not a growing parameter list.

```ts
export type BrowserFrameInput = {
  readonly deltaSeconds: number;
  readonly movement: {
    readonly forward: number;
    readonly right: number;
    readonly up: number;
    readonly fast: boolean;
  };
  readonly look: {
    readonly deltaX: number;
    readonly deltaY: number;
  };
  readonly buttons?: readonly BrowserButtonEvent[];
};
```

The command lane should cover UI and debug control without adding new public
methods for every feature:

```ts
export type GameCommand =
  | { readonly type: "setPlayerMode"; readonly mode: "firstPerson" | "debugFly" }
  | { readonly type: "togglePlayerMode" }
  | { readonly type: "setPlayerPosition"; readonly x: number; readonly y?: number; readonly z: number }
  | { readonly type: "setDebugCamera"; readonly x: number; readonly y: number; readonly z: number; readonly yaw: number; readonly pitch: number }
  | { readonly type: "setWorldConfig"; readonly config: WorldConfig }
  | { readonly type: "resetStreaming" }
  | { readonly type: "setTerrainTuning"; readonly tuning: TerrainTuningConfig };
```

This union should grow deliberately as UI becomes real. Growing the command union
is preferable to adding many ad hoc methods, because every new command remains
observable, testable, serializable, and Rust-owned.

## Target Rust-To-Browser Interactions

Rust may interact with browser capabilities, but TypeScript should mediate as
little as possible.

| Browser capability | Target owner | TypeScript role |
|---|---|---|
| WASM module loading | TypeScript bootstrap | Load the module and call `create`. |
| Canvas element | TypeScript bootstrap passes it once | Rust owns WebGPU surface configuration after `create`. |
| WebGPU | Rust through `wgpu` | No TypeScript mediation. |
| DOM input events | TypeScript | Collect events and pass compact frame input to `tick`. |
| HTML HUD/debug UI | TypeScript | Read `debugSnapshot()` and send `command()` values. |
| Workers / threading | Rust-owned semantics, browser-provided execution | Prefer Rust-created workers or wasm threads. If TypeScript must host Workers, it must expose only an opaque worker service; payloads are bytes or shared buffers, never terrain-specific messages. |
| Asset loading | Rust-owned asset system | Prefer Rust using browser fetch/decode APIs directly. If TypeScript must help, expose a generic byte/image service with no terrain material semantics. |
| Console/panic hooks | Rust during development | Allowed for diagnostics; not part of game state. |

## Opaque Worker Service Fallback

If Rust cannot directly create/manage browser Workers in the current toolchain,
TypeScript may provide this temporary service. This is a browser primitive, not a
terrain API:

```ts
export type BrowserWorkerHost = {
  createPool(options: WorkerPoolOptions): WorkerPoolId;
  post(pool: WorkerPoolId, workerIndex: number, requestId: number, payload: Uint8Array, transfer?: Transferable[]): void;
  poll(pool: WorkerPoolId): readonly WorkerCompletion[];
  reset(pool: WorkerPoolId, generation: number): void;
  destroy(pool: WorkerPoolId): void;
};
```

The fallback has hard rules:

- TypeScript does not choose terrain work.
- TypeScript does not know density, LOD, chunk keys, material layers, or mesh
  packet structure.
- TypeScript does not inspect worker payloads.
- Rust owns request IDs, priorities, cancellation generations, dependency rules,
  retries, and result interpretation.

When Rust-owned wasm threads or Rust-created Workers become practical, this
fallback should disappear from the public TypeScript surface.

## Current API Gap

The current public browser-facing API in `src/engine/web/engineWebWasm.ts` is:

```ts
create(canvas)
resize(width, height)
resetGame(seed, preset)
tick(deltaSeconds, forward, right, up, fast, lookDeltaX, lookDeltaY)
togglePlayerMode()
playerMode()
setPlayerMode(mode)
playerX()
playerY()
playerZ()
setPlayerPosition(x, z)
setDebugCamera(x, y, z, yaw, pitch)
upsertTerrainMesh(chunkKey, vertices, indices)
destroyTerrainMesh(chunkKey)
retainTerrainMeshes(chunkKeys)
clearTerrainMeshes()
upsertTerrainTextures(width, height, layers, formatCode, albedo, normal, material)
renderGameFrame(aspect)
status()
```

Measured against the target API, the current deviations are:

| Current call/category | Target replacement | Migration meaning |
|---|---|---|
| `renderGameFrame(aspect)` | `tick(frame)` plus `resize(viewport)` | Rendering should be part of the coarse Rust frame. |
| Scalar `tick(...)` arguments | `tick(BrowserFrameInput)` | Frame input should be a stable object packet. |
| `resetGame(seed, preset)` | `create(..., init)` or `command({ type: "setWorldConfig" })` | World reset/config should be Rust-owned command data. |
| `togglePlayerMode`, `playerMode`, `setPlayerMode` | `command(...)` and `debugSnapshot()` | Controls and reads should use command/snapshot lanes. |
| `playerX/Y/Z` | `debugSnapshot()` | TypeScript should not poll individual state fields. |
| `setPlayerPosition`, `setDebugCamera` | `command(...)` | Debug/player controls should be command data. |
| `upsertTerrainMesh`, `destroyTerrainMesh`, `retainTerrainMeshes`, `clearTerrainMeshes` | No public TypeScript call | Rust terrain streaming should own mesh upload and retention. |
| `upsertTerrainTextures(...)` | No public TypeScript call | Rust asset/render system should own texture loading and upload. |
| `status()` | `debugSnapshot()` | Status should be one Rust snapshot, not renderer-only state. |
| Direct `terrain_core.wasm` TypeScript calls | No public TypeScript call | `terrain_core` should be internal to Rust game/worker execution. |

## Progress Scorecard

Use this table to measure migration progress:

| Target item | Current state | Status |
|---|---|---|
| TypeScript creates one Rust game facade | `createRustBrowserGameRuntime` wraps `RustBrowserGame` and other TS terrain systems. | Partial |
| TypeScript calls one frame method | App calls `game.tick(...)` and `game.renderFrame()`. | Partial |
| Frame input is one object packet | TS adapter expands input into scalar wasm parameters. | Pending |
| UI/debug uses command lane | Several direct methods remain. | Pending |
| Debug/status uses one Rust snapshot | Status is split between TS streamer, TS chunk-key mirrors, and Rust renderer status. | Pending |
| No public terrain mesh upload calls | `upsertTerrainMesh` and retention calls remain. | Pending |
| No public terrain texture upload calls | `upsertTerrainTextures` remains. | Pending |
| No direct TypeScript `terrain_core.wasm` runtime calls | `RustBrowserGameRuntime` and Workers still load/call `terrain_core.wasm`. | Pending |
| Worker semantics are Rust-owned and opaque to TS | Rust owns scheduler/pool, but TS messages still name density and LOD jobs. | Partial |
| Rust owns WebGPU directly | Rust/wgpu owns browser rendering. | Complete |

The next migration slice should improve this scorecard by removing at least one
pending row or turning one partial row into complete.
