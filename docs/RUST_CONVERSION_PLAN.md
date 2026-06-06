# Rust Conversion Plan

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible.
Return to the user only for critical input that cannot be safely inferred, or
when the plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it.

This is the single active plan for moving OFG to an almost entirely Rust-owned
browser application. It replaces the older split Rust engine plan, TypeScript
reduction audit, browser/Rust API note, and the reduction ExecPlan. The archived
`docs/archived/RUST_TYPESCRIPT_REDUCTION_EXECPLAN.md` was the source of truth
for this consolidation; its purpose, observations, decisions, outcomes, context,
work plan, concrete commands, validation, recovery guidance, and target
interfaces are preserved here.

## Purpose / Big Picture

The Rust migration has already moved most high-volume engine responsibilities out
of TypeScript, but the remaining TypeScript files still make the boundary look
larger and less final than it should. After this work, a developer or AI agent
can open this one plan and see exactly:

- which systems are already Rust-owned,
- which TypeScript files remain only as browser substrate,
- which files are test-only or historical,
- what the final TypeScript/Rust API is,
- what implementation slices delete whole categories instead of repeatedly
  removing half of the remainder.

The user-visible outcome is project clarity and migration momentum. The next
agent should not need to rediscover the same state of play before continuing the
Rust-first plan.

The final target is a clear, minimal browser boundary:

- TypeScript owns browser shell concerns: module loading, canvas lookup, DOM
  input collection, HTML HUD/debug UI, URL parameters, and browser smoke hooks.
- Rust owns engine concerns: world state, player/camera, terrain generation,
  terrain streaming, worker job semantics, simulation, save/load schemas, render
  extraction, WebGPU resources, GPU uploads, and draw submission.

Migration progress is measured by how closely the code matches the target API
and scorecard in this document, not by vague TypeScript line-count reduction.

## Progress

- [x] (2026-06-06) Read `PLANS.md`, the old `docs/RUST_ENGINE_PLAN.md`,
  `docs/terrainplan.md`, `docs/ARCHITECTURE.md`, and `AGENTS.md`.
- [x] (2026-06-06) Listed compiled TypeScript files under `src/` and searched
  live references for terrain/render/engine bridge modules.
- [x] (2026-06-06) Created the now-archived
  `docs/archived/TYPESCRIPT_REDUCTION_AUDIT.md` to classify remaining
  TypeScript by runtime role, Rust ownership state, redundancy, and deletion
  path.
- [x] (2026-06-06) Updated `AGENTS.md`, `docs/ARCHITECTURE.md`,
  `docs/TERRAIN_PLAN.md`, and the historical scene-model documentation so they
  agreed on current ownership and the finite burn-down.
- [x] (2026-06-06) Added the now-archived
  `docs/archived/BROWSER_RUST_API.md` to define the exact target
  TypeScript-to-Rust API, allowed Rust-to-browser interactions, current API gap,
  and progress scorecard.
- [x] (2026-06-06) Re-ran `git -c safe.directory=C:/dev/ofg diff --check`
  after the API contract update; it passed with only line-ending warnings.
- [x] (2026-06-06) Consolidated the active Rust conversion docs into this single
  ExecPlan, with older split plans moved to `docs/archived/`.
- [x] (2026-06-06) Added the operational rule to proceed independently for as
  long as possible, returning only for critical input or plan completion.
- [ ] Delete or demote test-only compiled TypeScript: `primitiveMesh.ts` and the
  legacy `TerrainCoreRenderPacketStore` surface after splitting live sink/packet
  types.
- [ ] Move app-facing calls toward the target API: one frame input object,
  command lane, and Rust debug snapshot.
- [ ] Collapse terrain Worker semantics behind Rust or an opaque generic browser
  worker host.
- [ ] Move terrain texture asset ownership behind Rust and delete public
  terrain texture upload calls.
- [ ] Remove public terrain mesh upload calls by making Rust terrain streaming
  own mesh upload, retention, pruning, and debug visibility.
- [ ] Demote or delete standalone WASM wrappers that are not playable runtime,
  especially the standalone `engine_core.wasm` TypeScript wrapper if it is no
  longer a supported dev/test artifact.

## Surprises & Discoveries

- Observation: Before the docs cleanup, `AGENTS.md` described an older runtime
  where `engine_core.wasm` owned active player movement and TypeScript still
  submitted WebGPU draws.
  Evidence: the pre-edit file mentioned a temporary TypeScript `RenderWorld` and
  `WebGpuRenderer`, while the current architecture says Rust/wgpu owns draw
  submission through `crates/engine_web`.
- Observation: `src/engine/render/TerrainCoreRenderPackets.ts` is no longer on
  the playable browser mesh handoff, but it still provides shared sink and packet
  types imported by the live browser game adapter/runtime.
  Evidence: `rg` found runtime imports from
  `src/engine/web/rustBrowserGameAdapter.ts`,
  `src/engine/web/rustBrowserGameRuntime.ts`, and
  `src/engine/web/terrainCoreWorkerStreamer.ts`.
- Observation: `src/engine/world/primitiveMesh.ts` appears to be compiled only
  for its test, not runtime.
  Evidence: `rg` found imports only from `src/engine/world/primitiveMesh.test.ts`.
- Observation: The active docs had become plan-shaped overlap: an engine plan,
  a reduction audit, an API contract, a scene-model plan, and a reduction
  ExecPlan were all close enough to confuse the source of truth.
  Evidence: the active docs were consolidated on 2026-06-06 into
  `ARCHITECTURE.md`, `RUST_CONVERSION_PLAN.md`, `TERRAIN_PLAN.md`, and
  `TERRAIN_GEN_RESEARCH.md`, with retired docs moved under `docs/archived/`.

## Decision Log

- Decision: Treat the remaining migration as category deletion, not wrapper
  shrinkage.
  Rationale: Repeatedly moving half of the remaining code leaves TypeScript
  permanently terrain-aware. The plan must name whole remaining categories and
  delete or demote them as units.
  Date/Author: 2026-06-06 / Codex.
- Decision: Measure migration progress against an exact browser/Rust API.
  Rationale: The migration should converge on a known boundary: TypeScript calls
  a small Rust game facade and Rust only uses browser capabilities directly or
  through opaque browser services. This prevents "half of the remainder" slices
  from looking like completion.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep one active Rust conversion plan.
  Rationale: Split active plans made the source of truth blurry. This file now
  contains the API contract, current TypeScript audit, scorecard, next slices,
  validation gates, and recovery guidance for Rust conversion work.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep `docs/TERRAIN_PLAN.md` separate.
  Rationale: Terrain realism is a different active task from the TypeScript to
  Rust conversion. Terrain work should still follow this plan for ownership, but
  its believable-terrain research, progress, and milestones live in the terrain
  plan.
  Date/Author: 2026-06-06 / Codex.
- Decision: Execute the Rust conversion plan autonomously once started.
  Rationale: The plan is explicit enough to keep moving through milestones
  without repeated approval, while still returning for critical input that
  cannot be safely inferred.
  Date/Author: 2026-06-06 / Codex.

## Outcomes & Retrospective

The docs now separate completed Rust ownership from remaining TypeScript browser
substrate, and this plan gives the exact target boundary. The main remaining
implementation gap is terrain-aware Worker and density transfer code in
TypeScript, plus texture asset loading, split frame/render calls, direct
player/status getters, public terrain mesh and texture upload calls, and a few
legacy/test adapters.

The recommended next slice is to delete test-only compiled TypeScript first,
then remove terrain-aware Worker semantics as a whole category.

The previous docs cleanup validated with:

    git -c safe.directory=C:/dev/ofg diff --check

## Context and Orientation

The repository root is `C:\dev\ofg`. The current browser game is a lightweight
factory-game prototype with generated voxel terrain and Rust/wgpu rendering.

`crates/terrain_core` is the Rust terrain crate. It owns terrain sampling,
density chunk filling, Dual Contouring mesh emission, material classification,
stream scheduling, retained density storage, worker-pool request bookkeeping,
and a legacy mesh packet store.

`crates/engine_core` is the browser-free Rust engine logic crate. It owns tested
player, camera, world ID, transform, and render snapshot logic.

`crates/engine_web` is the browser-facing Rust/WASM crate. It composes
`engine_core` and `terrain_core` for the active playable runtime, owns the active
player/camera tick, owns Rust/wgpu WebGPU resources and draw submission, owns
terrain mesh/texture handles, and exposes the `RustBrowserGame` wasm-bindgen
facade.

`src/app` is the TypeScript browser shell. It creates the canvas, collects DOM
input, owns the HUD/debug wiring, parses URL seed/preset values, and calls the
coarse TypeScript runtime facade for `tick` and `renderFrame`.

`src/engine/web` contains the remaining TypeScript browser/WASM shell around
Rust. `RustBrowserGameRuntime` is the current coarse shell, but it still starts
terrain workers, loads texture assets, and wires worker mesh results into
`RustBrowserGame`.

`src/engine/world` contains terrain descriptor types, chunk-key utilities, and
thin TypeScript adapters to `terrain_core.wasm`. It also still contains terrain
worker request/response types and worker transport code.

The key architectural goal is that TypeScript should eventually call something
close to `game.tick(frame)` and should not understand terrain scheduling,
density chunks, LOD stages, mesh packets, render resources, or world simulation.

Active docs are:

- `docs/ARCHITECTURE.md`: current architecture overview.
- `docs/RUST_CONVERSION_PLAN.md`: this active Rust conversion ExecPlan.
- `docs/TERRAIN_PLAN.md`: active terrain realism plan.
- `docs/TERRAIN_GEN_RESEARCH.md`: terrain generation research reference.

`docs/archived/` contains retired plans and reference snapshots. Documents in
that folder are not active instructions. Use them only for historical context
when explicitly needed.

## Target TypeScript-To-Rust API

TypeScript should eventually call only this small game facade:

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

`create(canvas, init)` initializes the Rust-owned game, terrain, renderer,
assets, and worker/threading runtime. TypeScript passes the canvas and startup
config; it does not separately initialize terrain, textures, render resources,
workers, or player state.

`tick(frame)` is the normal frame call. It advances input, player/camera,
simulation, terrain streaming, worker result consumption, GPU uploads, and render
submission. TypeScript should not call a separate render function in the target
API.

`resize(viewport)` forwards browser viewport size and device-pixel-ratio
changes. Rust owns canvas backing size validation and renderer reconfiguration.

`command(command)` is the single control lane for UI, debug, and tuning actions.
It replaces many narrow functions such as `setPlayerPosition`,
`setDebugCamera`, `setTerrainConfig`, `resetStreaming`, and camera mode toggles.
Commands are explicit Rust-owned data, not arbitrary string hooks.

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

## Target Frame And Command Data

Frame input should be one object, not a growing scalar parameter list:

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
| Workers/threading | Rust-owned semantics, browser-provided execution | Prefer Rust-created workers or wasm threads. If TypeScript must host Workers, it must expose only an opaque worker service. |
| Asset loading | Rust-owned asset system | Prefer Rust using browser fetch/decode APIs directly. If TypeScript must help, expose a generic byte/image service with no terrain material semantics. |
| Console/panic hooks | Rust during development | Allowed for diagnostics; not part of game state. |

If Rust cannot directly create/manage browser Workers in the current toolchain,
TypeScript may temporarily provide an opaque worker host:

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

## Current State

Already Rust-owned:

| System | Rust owner | Current TypeScript status |
|---|---|---|
| Terrain height/density sampling | `crates/terrain_core` | Runtime TypeScript generator/noise code deleted. |
| Density chunk filling | `crates/terrain_core` | Browser bridge still copies density payloads between WASM instances. |
| Dual Contouring mesh emission | `crates/terrain_core` | Runtime TypeScript meshing code deleted. |
| Terrain material/biome classification | `crates/terrain_core` | Runtime classification is Rust-owned; TypeScript still has material asset metadata. |
| Terrain stream scheduling | `crates/terrain_core/src/stream.rs` | TypeScript bridge calls the scheduler but does not choose jobs itself. |
| Terrain retained density store | `crates/terrain_core/src/store.rs` | TypeScript adapter still moves buffers between main and worker WASM instances. |
| Terrain worker-pool bookkeeping | `crates/terrain_core/src/worker_pool.rs` | TypeScript still constructs browser Workers and posts terrain-specific messages. |
| Player/camera tick state | `crates/engine_web`, backed by `crates/engine_core` | Playable app no longer loads `engine_core.wasm` for active player movement. |
| WebGPU renderer | `crates/engine_web/src/wgpu_renderer.rs` | TypeScript no longer creates devices, pipelines, buffers, render passes, or draw calls. |
| Terrain GPU mesh/texture handles | `crates/engine_web` | TypeScript still uploads terrain mesh bytes and decoded texture arrays into Rust. |
| Active terrain draw set | `crates/engine_web` | TypeScript adapter mirrors chunk keys for debug/smoke only. |
| Debug player marker mesh/material | `crates/engine_web` | TypeScript primitive marker mesh is no longer runtime-used. |

Current public browser-facing Rust API in `src/engine/web/engineWebWasm.ts`:

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

Current runtime TypeScript that remains:

| Group | Runtime role | Target fate |
|---|---|---|
| App shell | Starts game, tracks input, updates HUD, exposes debug hooks, reads URL params, calls `game.tick()` and `game.renderFrame()`. | Keep as browser shell, but use target API. |
| WASM loading | Loads `engine_web` and `terrain_core.wasm` for the temporary worker bridge. | Keep only generic game module loading; remove runtime `terrain_core.wasm` calls. |
| Terrain worker transport | Creates module Workers, posts terrain-specific density/mesh jobs, resolves results, resets Workers. | Replace with Rust-owned worker/threading runtime or opaque worker host. |
| Density payload movement | Moves/shared-buffers density chunks between main and worker `terrain_core.wasm` instances. | Delete when worker memory/job payload ownership is Rust-managed. |
| Texture asset decode | Fetches checked-in JPGs, draws them to canvas, reads RGBA pixels, uploads arrays into Rust. | Move terrain asset ownership behind Rust; TS may remain generic byte/image helper only. |
| Debug/smoke mirrors | Tracks live chunk keys and exposes terrain/renderer/player status through `window.__ofgDebug`. | Replace with Rust `debugSnapshot()`. |
| Legacy/test adapters | `TerrainCoreRenderPacketStore`, primitive mesh, standalone `engineCoreWasm` wrapper, TS density/material helpers. | Delete or demote to explicit test support. |

## Current Scorecard

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

## Plan of Work

Work from the scorecard. Each slice should remove a named TypeScript category or
move one public call category to the target API.

First, delete test-only compiled TypeScript. Split the live terrain sink/packet
types out of `src/engine/render/TerrainCoreRenderPackets.ts`, then delete or
demote the old packet-store class if Rust/WASM coverage is sufficient. Delete
`src/engine/world/primitiveMesh.ts` and its test if no runtime import appears.

Second, move app-facing calls to the target shape. Introduce frame input packets,
a command lane, and a Rust debug snapshot facade. The app should stop calling
separate player/debug/status methods and should stop calling separate
`renderFrame()` once Rust can render from `tick()`.

Third, collapse terrain Worker semantics behind Rust. Replace
`TerrainCoreWorkerStreamer`, `terrainChunkWorkerClient`,
`terrainChunkWorkerTypes`, and density-transfer wrappers with Rust-owned
wasm-thread/Worker support or a generic opaque worker host where TypeScript sees
bytes and request IDs only.

Fourth, move terrain texture asset ownership behind Rust. Delete
`upsertTerrainTextures` from the public TypeScript-to-Rust API. Rust should own
material manifests, layer ordering, texture-array validation, and upload
decisions. TypeScript may remain only as a generic browser asset helper if Rust
cannot directly perform one browser-only step.

Fifth, remove public terrain mesh upload calls. Delete `upsertTerrainMesh`,
`destroyTerrainMesh`, `retainTerrainMeshes`, and `clearTerrainMeshes` from the
public API by making Rust terrain streaming own mesh upload, retention, pruning,
and debug visibility.

Sixth, demote or delete standalone WASM wrappers that are not playable runtime.
Decide whether `engine_core.wasm` remains a supported dev/test artifact. If not,
remove the wrapper, generated metadata, build output, and tests.

## Concrete Steps

Re-audit current TypeScript from `C:\dev\ofg` before deleting files:

    rg --files src | Sort-Object
    rg -n "TerrainCoreRenderPacket|primitiveMesh|engineCoreWasm|TerrainCoreWorkerStreamer" src docs AGENTS.md

For docs-only edits:

    git -c safe.directory=C:/dev/ofg diff --check

For TypeScript cleanup or API changes:

    npm test

For Rust/WASM boundary changes:

    npm run check:wasm
    cargo test -p engine_web
    cargo test -p terrain_core

For rendering, browser startup, worker, input, camera, HUD, or visual changes:

    npm run smoke:browser

After browser smoke, inspect `artifacts/browser-smoke/<run-id>/` screenshots and
`report.json`. A black screen after refresh is a failure.

## Validation and Acceptance

This plan is complete when the current scorecard is all complete:

- TypeScript creates one Rust game facade.
- TypeScript calls one frame method.
- Frame input is one object packet.
- UI/debug uses the command lane.
- Debug/status uses one Rust snapshot.
- There are no public terrain mesh upload calls.
- There are no public terrain texture upload calls.
- Runtime TypeScript does not instantiate or call `terrain_core.wasm` directly.
- Worker semantics are Rust-owned and opaque to TypeScript.
- Rust continues to own WebGPU directly.

A migration slice is acceptable only when it includes tests near the moved
behavior and passes the relevant commands from Concrete Steps. Browser smoke and
screenshot inspection are mandatory for rendering, browser startup, worker,
input, camera, HUD, or visual changes.

## Idempotence and Recovery

The documentation parts of this plan are safe to rerun. If the audit becomes
stale, rerun the `rg --files src` and reference searches, then update Current
State, Current Scorecard, Progress, Surprises & Discoveries, and Outcomes &
Retrospective.

Do not delete TypeScript files based only on this plan. Before any deletion,
search imports with `rg`, remove or replace tests intentionally, and run
`npm test`. If rendering, browser integration, terrain streaming, or input is
affected, run `npm run smoke:browser` and inspect the screenshots.

Do not reintroduce a TypeScript scene graph, ECS, terrain generator, terrain
manager, renderer, render packet assembler, or terrain-aware Worker scheduler.
If a browser capability truly requires TypeScript, keep the TypeScript API
generic and opaque, then record the reason here.

## Artifacts and Notes

The archived reduction ExecPlan recorded these useful artifacts:

    M AGENTS.md
    ?? PLANS.md

It also recorded that empty source directories were removed after being found
empty:

    src/engine/camera
    src/engine/scene
    src/game/components

The active-doc cleanup moved retired plans and reference snapshots under
`docs/archived/`. Those archived files are not active instructions. They are
available for historical context if needed.

## Interfaces and Dependencies

Stable target interface:

    game.tick(frameInput)
    game.resize(viewport)
    game.command(command)
    game.debugSnapshot()
    game.save()
    game.load(saveBytes)
    game.dispose()

Temporary or current interface elements that must disappear from the public
TypeScript/Rust boundary:

    game.renderFrame()
    renderGameFrame(aspect)
    resetGame(seed, preset)
    togglePlayerMode()
    playerMode()
    playerX()
    playerY()
    playerZ()
    setPlayerMode(mode)
    setPlayerPosition(x, z)
    setDebugCamera(x, y, z, yaw, pitch)
    upsertTerrainMesh(chunkKey, vertices, indices)
    destroyTerrainMesh(chunkKey)
    retainTerrainMeshes(chunkKeys)
    clearTerrainMeshes()
    upsertTerrainTextures(width, height, layers, formatCode, albedo, normal, material)
    status()

New TypeScript files must not become terrain schedulers, terrain mesh stores,
render resource owners, scene graphs, ECS systems, factory simulation owners, or
terrain-specific Worker protocols.

## Revision Note

2026-06-06: This plan was rewritten as a full ExecPlan after consolidating active
docs. It now preserves the content of the archived reduction ExecPlan and embeds
the browser/Rust API contract and TypeScript reduction audit directly.
