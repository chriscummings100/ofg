# Terrain Stream CPU Spike Optimization

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `C:\dev\ofg\PLANS.md`. It is a new active plan for main-thread CPU spikes during terrain streaming. It does not replace `C:\dev\ofg\docs\TERRAIN_SKY_FILL_RATE_OPTIMIZATION_PLAN.md`, which remains the active source of truth for terrain/sky fill-rate and shader-cost work.

## Purpose / Big Picture

Terrain rendering is healthier after shadow culling, mipmaps, sky ordering, and post-process fixes, but moving through the world still causes visible main-thread spikes when terrain streams. The goal of this plan is to make terrain streaming smooth under movement by identifying the exact CPU costs, then spreading expensive terrain completion, visibility, mesh destruction, and GPU upload work across frames.

After this work, a developer should be able to run a browser movement capture, see which terrain-streaming stage caused each worst frame, and verify that streaming does not process unbounded completion or mesh upload bursts in a single animation frame. The desired user-visible result is less movement stutter while terrain continues to converge to the same visible set.

This plan is deliberately not about further GPU shader optimization. Cloud noise should become production-default off later, but that is out of scope here unless a capture shows it is coupled to CPU spike diagnosis.

## Progress

- [x] (2026-06-09 06:48Z) Created this ExecPlan after browser movement testing showed terrain streaming can upload several large meshes in one frame.
- [ ] Milestone 1: Add tighter movement-capture diagnostics for terrain streaming CPU spikes without changing runtime behavior.
- [ ] Milestone 2: Split Rust and TypeScript terrain-streaming timings so completion ingest, scheduler/visibility work, mesh destruction, mesh registration/upload, and request submission can be read separately.
- [ ] Milestone 3: Remove the heavy mesh clone from visible terrain update handoff, or prove with timers that it is not material.
- [ ] Milestone 4: Add per-frame budgets for worker completion ingest, terrain mesh upload/registration, and mesh destruction while preserving visible terrain coverage.
- [ ] Milestone 5: Tune default budgets from captures, expose useful debug status, and keep aggressive budget controls available for diagnosis.
- [ ] Milestone 6: Run milestone review, smoke, coverage, and final movement captures; record before/after spike evidence.

## Surprises & Discoveries

- Observation: The existing movement smoke is useful but too permissive to catch the stutter the user observes.
  Evidence: `C:\dev\ofg\tools\browser-smoke-movement-performance.mjs` allows `frameDeltaMs.p95 <= 250`, `frameDeltaMs.max <= 1500`, and `maxTerrainUpdateTotalMs <= 500`, so it passes even if terrain work visibly hitches on a slower device.

- Observation: A local movement smoke on 2026-06-09 stayed frame-paced, but still revealed bursty terrain streaming work.
  Evidence: `C:\dev\ofg\artifacts\browser-smoke\2026-06-09T06-38-22-596Z\movement-performance-samples.json` showed `frameDeltaMs.max=16.995ms`, `terrainUpdateTotalMs.max=6ms`, completion bursts up to 6, upsert bursts up to 4 meshes, removal bursts up to 15 meshes, and upload bursts up to `629166` vertex floats plus `33114` indices in one frame.

- Observation: The current stream update path can clone complete mesh vectors before uploading them.
  Evidence: `BrowserTerrainStream::sync_visible_meshes(...)` in `C:\dev\ofg\crates\engine_web\src\terrain_stream.rs` pushes `BrowserTerrainMeshUpdate { key, mesh: mesh.clone() }`. `MeshData` owns `Vec<f32>` vertices and `Vec<u32>` indices, so this can duplicate large buffers on the main thread.

- Observation: Worker completions are drained and handed to Rust without an explicit per-frame count or byte budget.
  Evidence: `RustBrowserGameAdapter.tick(...)` in `C:\dev\ofg\src\engine\web\rustBrowserGameAdapter.ts` calls `this.terrainWorkers?.takeCompletions() ?? []` and immediately passes the whole array to `game.completeTerrainBuilds(completions)`.

- Observation: Mesh upload/registration is immediate and unbudgeted once a mesh becomes newly visible.
  Evidence: `RustBrowserGame.update_terrain_stream(...)` in `C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` iterates all `update.upserted_meshes` and calls `self.upsert_terrain_mesh(...)`, which calls `BrowserWgpuRenderer::register_mesh(...)`. `register_mesh(...)` creates fresh vertex and index buffers with `create_buffer_init(...)`.

## Decision Log

- Decision: Optimize terrain-streaming CPU spikes only after adding more detailed spike attribution.
  Rationale: The current `terrain_stream_update_ms` counter is too coarse. Without finer timers, we cannot distinguish worker completion conversion, Rust stream scheduling, mesh clone, visibility selection, mesh destruction, GPU buffer creation, or request submission.
  Date/Author: 2026-06-09 / Codex

- Decision: Keep terrain scheduling, visibility, mesh semantics, and renderer ownership in Rust; keep TypeScript limited to browser worker transport and browser-side frame timing.
  Rationale: `OFG-API-001`, `OFG-API-003`, `OFG-API-004`, and `OFG-API-009` keep the playable terrain stream and renderer behavior Rust-owned. TypeScript may route opaque build requests and report timings, but it must not own terrain scheduling or mesh semantics.
  Date/Author: 2026-06-09 / Codex

- Decision: Prefer smoothing burst work before reducing terrain worker count.
  Rationale: Lowering worker count can reduce completion bursts but also slows terrain convergence. Per-frame budgets for completions, uploads, and removals target the actual main-thread spike mechanism while preserving worker throughput.
  Date/Author: 2026-06-09 / Codex

- Decision: Treat cloud-noise-default-off as a separate later default change.
  Rationale: Cloud noise is a GPU shader-cost default. This plan is about CPU spikes while moving and should not mix independent production-default visual changes into the evidence.
  Date/Author: 2026-06-09 / Codex

## Outcomes & Retrospective

No implementation has landed yet. The starting hypothesis is that visible stutter comes from bursty main-thread streaming work rather than from a single always-on per-frame cost. The first proof target is a capture that points to the specific stage responsible for each worst frame.

## Contract and Quality Baseline

This work must preserve the Rust-owned runtime architecture documented in `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\ARCHITECTURE.md`.

`OFG-API-001: Browser Shell To Rust Browser Game` remains active. New runtime controls must go through `game.command(...)`, `debugSnapshot()`, or existing facade methods. Do not add ad hoc direct wasm scalar calls from app code.

`OFG-API-003: Debug And Smoke-Test Hooks` will likely need updates. New perf fields and movement-capture summaries must be mirrored in `C:\dev\ofg\src\engine\web\browserGameTypes.ts`, surfaced through `window.__ofgDebug`, and covered by smoke or TypeScript tests.

`OFG-API-004: Terrain Vertex And Material Layout` must remain stable. This plan must not change the terrain vertex stride, index format, material slots, or shader vertex layout.

`OFG-API-009: Runtime Ownership Rules` remains active. Rust owns terrain streaming, render extraction, mesh handles, WebGPU resources, and draw submission. TypeScript owns DOM input, browser worker transport, and generic debug UI/capture plumbing only.

Quality gates from `C:\dev\ofg\PLANS.md` apply. After every implementation milestone, run the repo-local `milestone-review` skill before marking the milestone complete. For implementation work, run `npm run coverage:rust` before completion and confirm modified Rust implementation files do not appear in the default filtered coverage attention report unless this plan records an explicit exception.

## Context and Orientation

The working directory is `C:\dev\ofg`.

Terrain mesh generation runs in browser Web Workers through `C:\dev\ofg\src\engine\web\terrainWorkerClient.ts` and `C:\dev\ofg\src\engine\web\terrainBuildWorker.ts`. TypeScript routes opaque Rust-issued requests to workers and returns typed-array completions. The worker calls `terrain_core.wasm` to build one node mesh and transfers `Float32Array` vertices plus `Uint32Array` indices back to the main thread.

`C:\dev\ofg\src\engine\web\rustBrowserGameAdapter.ts` owns the browser-side order of operations each frame: resize, drain worker completions, call `game.completeTerrainBuilds(...)`, tick the Rust game, then submit new Rust-issued worker requests.

`C:\dev\ofg\crates\engine_web\src\terrain_stream.rs` owns the Rust browser terrain stream. `BrowserTerrainStream::tick_for_workers(...)` syncs the stream center, asks `TerrainStreamScheduler` for work, queues worker build requests, selects visible nodes, and returns removed or upserted visible mesh updates.

`C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` owns the browser `RustBrowserGame` facade and Rust/wgpu renderer. `RustBrowserGame::update_terrain_stream(...)` calls `terrain_stream.tick_for_workers(...)`, destroys removed terrain meshes, and registers newly visible terrain meshes with the renderer. `BrowserWgpuRenderer::register_mesh(...)` creates fresh GPU buffers with `wgpu::util::DeviceExt::create_buffer_init(...)`.

`C:\dev\ofg\crates\engine_web\src\perf.rs` currently records coarse Rust CPU timings in `RustCpuFrameTimings`, including `terrain_stream_update_ms`, plus renderer CPU and GPU timing summaries. The current counter is not enough to isolate spikes inside terrain streaming.

`C:\dev\ofg\tools\browser-smoke-movement-performance.mjs` already drives real movement for 360 frames and records coarse terrain worker and upload metrics. This is the right starting point, but its pass/fail thresholds are intentionally broad and its summary needs more spike attribution.

## Plan of Work

Milestone 1 creates a dedicated movement CPU spike capture path before changing behavior. Either extend `C:\dev\ofg\tools\browser-smoke-movement-performance.mjs` with richer summaries or add a focused script such as `C:\dev\ofg\tools\browser-terrain-stream-cpu-capture.mjs`. The capture should record per-frame browser delta, Rust CPU timings, worker completion bursts, typed-array byte counts, terrain stream status, mesh/object counts, visible draw counts, upload counts, and the top worst frames by frame delta and terrain update time. It should write `samples.json`, `summary.json`, and a concise console summary under `C:\dev\ofg\artifacts\terrain-stream-cpu\`.

Milestone 2 splits timing at the Rust and TypeScript boundaries. Add fields to `RustCpuFrameTimings`, `RustCpuFrameSummary`, `FramePerfSample`, and JS conversion helpers for terrain completion ingest, scheduler/stream tick, visibility selection, mesh clone or handoff, mesh destruction, mesh upload/registration, and request creation. Add browser-side timing in `RustBrowserGameAdapter.tick(...)` for `takeCompletions`, `completeTerrainBuilds`, `game.tick`, `takeTerrainBuildRequests`, and `submitRequests`, then expose those values through the existing browser perf aggregation in `C:\dev\ofg\src\app\perfDebug.ts` or a narrow adjacent helper.

Milestone 3 removes avoidable main-thread memory churn. The likely first fix is to stop cloning `MeshData` in `BrowserTerrainStream::sync_visible_meshes(...)`. A good implementation is to store generated meshes in a cheap shared handle such as `Arc<MeshData>` inside `mesh_cache`, return cheap handles in `BrowserTerrainMeshUpdate`, and keep renderer registration reading slices from the shared mesh data. If local timers prove the clone is negligible, record that surprise and skip this milestone's code change.

Milestone 4 adds burst smoothing. Add an explicit completion budget to `TerrainWorkerClient.takeCompletions(...)` or to `RustBrowserGameAdapter.tick(...)`, so only a bounded number of completed meshes are handed into Rust per frame and the rest remain queued in TypeScript. Add Rust-side terrain update budgets for mesh upload/registration and mesh destruction, measured by mesh count and preferably by vertex/index count. Preserve visible coverage during transitions by keeping old parent meshes visible until replacement child meshes have been uploaded. Removals may be deferred for a few frames if that prevents a spike; overdraw for a short period is preferable to a stutter.

Milestone 5 tunes and exposes defaults. Choose default budgets from repeated captures on the local machine and from user feedback. The debug snapshot should show pending completion count, deferred upload count, deferred removal count, uploaded vertices/indices this frame, removed meshes this frame, and whether a budget was hit. If useful, expose a conservative/aggressive budget mode in debug commands and UI, but do not add UI before the core budgets and status are proven.

Milestone 6 validates and documents. Run browser smoke, the new movement CPU capture, Rust and TypeScript tests, Rust coverage, and milestone review. Update `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\ARCHITECTURE.md` if new debug fields, timing contracts, or terrain-streaming ownership details change.

## Concrete Steps

Start from the repo root:

    cd C:\dev\ofg
    npm run smoke:browser

Use the current smoke artifact as a pre-change reference. The 2026-06-09 reference is:

    C:\dev\ofg\artifacts\browser-smoke\2026-06-09T06-38-22-596Z\movement-performance-samples.json

Milestone 1 capture script:

    cd C:\dev\ofg
    node tools/browser-terrain-stream-cpu-capture.mjs
    npm run test:ts

Expected result: a new directory under `C:\dev\ofg\artifacts\terrain-stream-cpu\` with `samples.json` and `summary.json`. The summary should list the top worst frames and include at least `frameDeltaMs`, `terrainStreamUpdateMs`, completion count, completion bytes, upsert count, removal count, uploaded vertex floats, uploaded indices, and worker in-flight count.

Milestone 2 timing split:

    cd C:\dev\ofg
    cargo test -p engine_web perf_tests
    npm run test:ts
    npm run smoke:browser

Expected result: perf tests cover the new timing summary fields, TypeScript debug typing accepts them, and smoke validates the debug contract.

Milestone 3 mesh clone removal:

    cd C:\dev\ofg
    cargo test -p engine_web terrain_stream
    cargo test -p engine_web
    npm run smoke:browser

Expected result: terrain stream tests still pass, movement smoke renders nonblank terrain, and the capture shows reduced terrain stream CPU time on upload frames if the clone was material.

Milestone 4 budgets:

    cd C:\dev\ofg
    cargo test -p engine_web terrain_stream
    npm run test:ts
    npm run smoke:browser
    node tools/browser-terrain-stream-cpu-capture.mjs

Expected result: captures show bounded per-frame completions, bounded terrain uploads/removals, no synchronous terrain builds, no worker failures, no stale completions after normal movement, and terrain eventually settles to zero missing nodes.

Milestone 5 tuning:

    cd C:\dev\ofg
    node tools/browser-terrain-stream-cpu-capture.mjs
    npm run smoke:browser

Expected result: the default budget is high enough that terrain converges quickly and low enough that the largest terrain update frames are materially smaller than the pre-change reference. Record exact before/after values in Surprises & Discoveries.

Milestone 6 final validation:

    cd C:\dev\ofg
    npm test
    npm run smoke:browser
    npm run coverage:rust
    node tools/browser-terrain-stream-cpu-capture.mjs
    git -c safe.directory=C:/dev/ofg diff --check

Expected result: tests pass; browser smoke passes; coverage reports no modified Rust implementation files below the default filtered coverage threshold; the capture artifact records reduced spike magnitude; and this plan's living sections are current.

## Milestone Review

After each implementation milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective.
2. Update `C:\dev\ofg\docs\API_CONTRACTS.md` or `C:\dev\ofg\docs\ARCHITECTURE.md` if debug contracts, timing fields, ownership rules, or terrain streaming behavior changed.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands and record command names, results, artifacts, and remaining risks.

## Validation and Acceptance

This plan is accepted when all of the following are true:

- A movement CPU spike capture can be run outside smoke and writes `samples.json` plus `summary.json` under `C:\dev\ofg\artifacts\terrain-stream-cpu\`.
- The capture reports worst frames with enough detail to identify whether the spike came from browser completion handling, Rust completion ingest, scheduler/visibility, mesh destruction, mesh upload/registration, or render submission.
- Rust perf stats and TypeScript debug types expose terrain-streaming sub-timings without breaking existing debug consumers.
- The terrain stream no longer performs large `MeshData` vector clones when handing newly visible meshes to the renderer, unless measured evidence proves that clone is not material and this plan records the rationale for skipping it.
- Worker completion processing is bounded per frame, and deferred completions remain queued without being dropped.
- Terrain mesh upload/registration and destruction are bounded per frame by count or bytes.
- LOD transition correctness is preserved: old coarser meshes remain visible until replacement finer meshes are ready and uploaded; terrain eventually settles to zero missing nodes after movement stops.
- Browser movement captures show materially lower maximum terrain update CPU time and smaller upload/removal bursts than the 2026-06-09 reference capture.
- `npm test`, `npm run smoke:browser`, `npm run coverage:rust`, the new capture command, and `git -c safe.directory=C:/dev/ofg diff --check` pass before final completion.

## Idempotence and Recovery

The capture scripts are safe to rerun; they should create timestamped artifact directories and avoid mutating source files.

If new timing fields break TypeScript consumers, revert the JS conversion/type additions together and keep Rust timing fields internal until the browser contract is updated in one coherent change.

If upload budgeting creates holes or visible terrain popping, keep old visible meshes longer and reduce only newly visible upserts first. Do not remove parent/coarser meshes until replacement child/finer meshes have GPU handles.

If completion budgeting causes terrain to converge too slowly, increase the default completion budget while keeping upload and removal budgets in place. The goal is to smooth main-thread work, not to starve terrain streaming.

If shared mesh ownership complicates lifetimes, an acceptable fallback is to return upsert keys and borrow mesh data from `mesh_cache` during renderer upload, but only if the Rust borrow structure remains clear and tested.

## Artifacts and Notes

Pre-plan local movement smoke artifact:

    C:\dev\ofg\artifacts\browser-smoke\2026-06-09T06-38-22-596Z\movement-performance-samples.json

Extracted reference numbers from that artifact:

    frameDeltaMs.max = 16.995
    terrainUpdateTotalMs.max = 6
    maxCompletedBurst = 6
    maxTerrainUpdateUpsertedMeshCount = 4
    maxTerrainUpdateRemovedMeshCount = 15
    max uploaded vertex floats in one frame = 629166
    max uploaded indices in one frame = 33114

These local numbers are not bad enough to reproduce the user's visible stutter, but they prove the stream can produce large one-frame bursts. The optimization should be judged against both local captures and the user's observed browser/device behavior.

## Interfaces and Dependencies

Expected Rust additions or changes:

- `C:\dev\ofg\crates\engine_web\src\perf.rs` extends `RustCpuFrameTimings` and `RustCpuFrameSummary` with terrain-streaming sub-timings.
- `C:\dev\ofg\crates\engine_web\src\terrain_stream.rs` exposes enough per-tick update detail to count selected, deferred, upserted, and removed nodes without cloning large mesh vectors.
- `C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` records completion, stream tick, removal, upload, and budget-hit stats in renderer status and perf samples.
- Existing tests in `C:\dev\ofg\crates\engine_web\src\tests.rs` are extended to cover budgeted streaming, deferred uploads, and coverage-preserving LOD transitions.

Expected TypeScript additions or changes:

- `C:\dev\ofg\src\engine\web\terrainWorkerClient.ts` supports bounded completion draining or exposes queued completion counts.
- `C:\dev\ofg\src\engine\web\rustBrowserGameAdapter.ts` records browser-side completion/request timing around the Rust facade.
- `C:\dev\ofg\src\engine\web\browserGameTypes.ts` mirrors new debug and perf fields.
- `C:\dev\ofg\src\app\perfDebug.ts` includes the new fields in live overlay or console dump where useful.
- `C:\dev\ofg\tools\browser-terrain-stream-cpu-capture.mjs` or an equivalent extension of `browser-smoke-movement-performance.mjs` writes repeatable movement CPU spike artifacts.

Revision note, 2026-06-09: Initial plan created from user-observed main-thread spikes during terrain streaming and local movement-smoke evidence of large one-frame upload/removal bursts.
