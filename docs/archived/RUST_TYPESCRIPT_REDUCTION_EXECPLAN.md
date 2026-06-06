# Clarify Rust Ownership And TypeScript Reduction

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it.

## Purpose / Big Picture

The Rust migration has already moved most high-volume engine responsibilities out
of TypeScript, but the remaining TypeScript files still make the boundary look
larger and less final than it should. After this work, a developer or AI agent
can open the architecture docs and see exactly which systems are already Rust
owned, which TypeScript files remain only as browser substrate, which files are
test-only or historical, and which implementation slices delete whole categories
instead of repeatedly reducing the remainder by half.

The user-visible outcome is project clarity and migration momentum: the next
agent should not need to rediscover the same state of play before continuing the
Rust-first plan.

## Progress

- [x] (2026-06-06) Read `PLANS.md`, `docs/RUST_ENGINE_PLAN.md`,
  `docs/terrainplan.md`, `docs/ARCHITECTURE.md`, and `AGENTS.md`.
- [x] (2026-06-06) Listed compiled TypeScript files under `src/` and searched
  live references for terrain/render/engine bridge modules.
- [x] (2026-06-06) Created `docs/TYPESCRIPT_REDUCTION_AUDIT.md` to classify
  remaining TypeScript by runtime role, Rust ownership state, redundancy, and
  deletion path.
- [x] (2026-06-06) Updated `AGENTS.md`, `docs/ARCHITECTURE.md`,
  `docs/RUST_ENGINE_PLAN.md`, `docs/terrainplan.md`, and the historical
  `docs/SCENE_MODEL_PLAN.md` so they agree on current ownership and the finite
  burn-down.
- [x] (2026-06-06) Ran `git -c safe.directory=C:/dev/ofg diff --check`;
  it passed with only line-ending warnings.
- [x] (2026-06-06) Added `docs/BROWSER_RUST_API.md` to define the exact target
  TypeScript-to-Rust API, allowed Rust-to-browser interactions, current API gap,
  and progress scorecard.
- [x] (2026-06-06) Linked `docs/BROWSER_RUST_API.md` from `AGENTS.md`,
  `docs/ARCHITECTURE.md`, `docs/RUST_ENGINE_PLAN.md`,
  `docs/TYPESCRIPT_REDUCTION_AUDIT.md`, and `docs/terrainplan.md`.
- [x] (2026-06-06) Re-ran `git -c safe.directory=C:/dev/ofg diff --check`
  after the API contract update; it passed with only line-ending warnings.

## Surprises & Discoveries

- Observation: Before this cleanup, `AGENTS.md` described an older runtime where
  `engine_core.wasm` owns active player movement and TypeScript still submits
  WebGPU draws.
  Evidence: the pre-edit file mentioned a temporary TypeScript `RenderWorld`
  and `WebGpuRenderer`, while `docs/ARCHITECTURE.md` said Rust/wgpu owns draw
  submission through `crates/engine_web`. `AGENTS.md` has now been updated.
- Observation: `src/engine/render/TerrainCoreRenderPackets.ts` is no longer on
  the playable browser mesh handoff, but it still provides shared sink and packet
  types imported by the live browser game adapter/runtime.
  Evidence: `rg` finds runtime imports from `src/engine/web/rustBrowserGameAdapter.ts`,
  `src/engine/web/rustBrowserGameRuntime.ts`, and
  `src/engine/web/terrainCoreWorkerStreamer.ts`.
- Observation: `src/engine/world/primitiveMesh.ts` appears to be compiled only
  for its test, not runtime.
  Evidence: `rg` finds imports only from `src/engine/world/primitiveMesh.test.ts`.

## Decision Log

- Decision: Treat the remaining migration as category deletion, not wrapper
  shrinkage.
  Rationale: The user's complaint is correct: repeatedly moving half of the
  remaining code leaves TypeScript permanently terrain-aware. The plan must name
  the whole remaining categories and delete or demote them as units.
  Date/Author: 2026-06-06 / Codex.
- Decision: Add a dedicated TypeScript reduction audit instead of burying the
  inventory only in the long Rust migration plan.
  Rationale: `docs/RUST_ENGINE_PLAN.md` is a chronological migration record. A
  compact audit table gives future agents a faster current-state entry point.
  Date/Author: 2026-06-06 / Codex.
- Decision: Measure migration progress against an exact browser/Rust API.
  Rationale: The migration should converge on a known boundary: TypeScript calls
  a small Rust game facade and Rust only uses browser capabilities directly or
  through opaque browser services. This prevents "half of the remainder" slices
  from looking like completion.
  Date/Author: 2026-06-06 / Codex.

## Outcomes & Retrospective

The docs now separate completed Rust ownership from remaining TypeScript browser
substrate, and `docs/BROWSER_RUST_API.md` gives the exact target boundary. The
main remaining implementation gap is terrain-aware Worker and density transfer
code in TypeScript, plus texture asset loading, split frame/render calls, direct
player/status getters, and a few legacy/test adapters. The recommended next
slice is to delete test-only compiled TypeScript first, then remove terrain-aware
Worker semantics as a whole category.

Validation passed with:

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

## Plan of Work

First, write a current-state TypeScript reduction audit in
`docs/TYPESCRIPT_REDUCTION_AUDIT.md`. It must classify remaining TypeScript by
source group: browser shell that can stay, temporary browser substrate that must
be made generic or moved behind Rust, live terrain-aware bridge code that must be
deleted as a category, test-only or historical adapters, and already-Rust-owned
systems.

Second, update `AGENTS.md` so the first-stop instructions no longer claim that
TypeScript submits WebGPU draws or assembles a `RenderWorld`.

Third, update `docs/RUST_ENGINE_PLAN.md` with a short current-state burn-down and
a strict next-slice rule: do not move another half-wrapper; delete one named
category at a time.

Fourth, update `docs/terrainplan.md` so terrain's current TypeScript gap is
stated as worker/asset transport and debug/status shell, not terrain generation
or rendering ownership.

Finally, run documentation validation. If this work remains documentation-only,
`git diff --check` is the acceptance gate. If code files are deleted or moved in
the same change, also run `npm test` and `npm run smoke:browser`.

## Concrete Steps

From `C:\dev\ofg`:

    rg --files src | Sort-Object
    rg -n "TerrainCoreRenderPacket|primitiveMesh|engineCoreWasm|TerrainCoreWorkerStreamer" src docs AGENTS.md
    git -c safe.directory=C:/dev/ofg diff --check

If code cleanup follows:

    npm test
    npm run smoke:browser

## Validation and Acceptance

This documentation cleanup is accepted when:

- `docs/TYPESCRIPT_REDUCTION_AUDIT.md` exists and names the remaining TypeScript
  categories, including deletion paths and immediate candidates.
- `AGENTS.md`, `docs/ARCHITECTURE.md`, `docs/RUST_ENGINE_PLAN.md`, and
  `docs/terrainplan.md` agree that Rust/wgpu owns browser draw submission and
  that TypeScript no longer owns scene/render/terrain algorithms.
- The next implementation slice is explicit enough that it deletes or demotes a
  whole TypeScript category.
- `git diff --check` succeeds.

## Idempotence and Recovery

The documentation edits are safe to rerun. If the audit becomes stale, rerun the
`rg --files src` and reference searches, then update the tables and record the
change in this plan's Progress and Outcomes sections.

Do not delete TypeScript files based only on the audit. Before any deletion,
search imports with `rg`, remove or replace tests intentionally, and run
`npm test`. If rendering, browser integration, terrain streaming, or input is
affected, run `npm run smoke:browser` and inspect the screenshots.

## Artifacts and Notes

Current pending non-code repository changes before this plan:

    M AGENTS.md
    ?? PLANS.md

The empty source directories `src/engine/camera`, `src/engine/scene`, and
`src/game/components` were removed earlier when they were found empty.

## Interfaces and Dependencies

The desired long-term TypeScript boundary is a coarse browser facade:

    game.tick(frameInput)
    game.renderFrame()
    game.resize(width, height, scale)
    game.setTerrainConfig(config)
    game.resetStreaming()
    game.debugSnapshot()

The names may change, but the boundary should stay coarse. New TypeScript files
must not become terrain schedulers, terrain mesh stores, render resource owners,
scene graphs, ECS systems, or simulation authorities.
