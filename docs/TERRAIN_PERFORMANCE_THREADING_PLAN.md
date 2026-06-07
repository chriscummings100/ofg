# Terrain Distance, Benchmarking, and Threaded Generation

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

The hierarchy fix made terrain visually coherent again, but the playable view
distance is still too short and terrain generation currently hitches the browser
frame when new nodes are built. This plan extends the terrain horizon, measures
real generation cost across realistic chunk populations, and moves terrain node
generation off the browser main thread while preserving the Rust-owned
streaming contract.

After this work, the player should be able to stand on terrain and see terrain
multiple kilometers into the distance, move at running speed, and have terrain
generation happen on worker threads instead of inside the frame tick. The debug
snapshot and benchmark reports should make it obvious how many terrain nodes are
loaded, how far the visible terrain extends, how long node generation takes on
average and at p95, and which phase of generation is expensive.

For this plan, one terrain world unit is treated as one meter. LOD0 is the
highest-detail terrain. Higher LOD numbers are coarser. A node is one rendered
terrain chunk at a specific LOD; all LODs currently use `32 x 32 x 32` terrain
cells, but the world-space cell size doubles for each coarser LOD. A LOD3 node
therefore spans `32 * 2^3 = 256` meters per axis. "Main thread" means the
browser thread that runs input, JavaScript, WebGPU submission, and the
`RustBrowserGame` frame tick. "Worker" means a browser Web Worker running
terrain build code away from that main thread.

This plan does not solve thin cross-LOD geometric cracks. Those are still an
apron task. This plan must not reintroduce broad terrain holes, missing floor
patches, parent/child visible overlap, or TypeScript-owned terrain scheduling.

## Progress

- [x] (2026-06-07 22:15+01:00) Created this plan after completing and pushing
  `docs/TERRAIN_REGRESSION_PLAN.md`.
- [x] (2026-06-07 22:15+01:00) Audited the current defaults: the playable stream
  has LOD0 radius 1, LOD1 radius 2, LOD2 radius 3, vertical offsets
  `[-2, -1, 0, 1]`, base cell size 1.0, 32 cells per node axis, and a reported
  multi-LOD visible span of about 896 meters by 896 meters.
- [x] (2026-06-07 22:15+01:00) Confirmed the current browser path builds terrain
  synchronously in `BrowserTerrainStream::tick` by calling
  `complete_node_job`, which immediately calls `build_node_mesh`.
- [x] (2026-06-07 22:15+01:00) Confirmed the existing Rust benchmark command is
  `npm run bench:terrain:rust`, backed by
  `crates/ofg_test_harness/src/terrain_bench.rs` and
  `crates/ofg_test_harness/src/terrain_bench_lod.rs`.
- [ ] Milestone 1: build a trustworthy terrain generation benchmark that
  samples realistic chunk populations and reports cost distributions plus phase
  breakdowns.
- [ ] Milestone 2: add at least one extra terrain LOD and tune default horizon
  bands to reach a multi-kilometer visible span without breaking hierarchical
  streaming.
- [ ] Milestone 3: move node generation off the browser main thread using
  browser workers while keeping Rust scheduler ownership and stale-completion
  validation.
- [ ] Milestone 4: add browser stutter/performance validation that proves worker
  generation is active and records frame-time/upload behavior during movement.
- [ ] Milestone 5: run full validation, update contracts/docs, run
  `milestone-review`, and archive or supersede this plan when complete.

## Surprises & Discoveries

- Observation: `terrainWorkerCount` in the current debug snapshot is not proof
  that terrain generation is threaded.
  Evidence: `crates/engine_web/src/terrain_stream.rs` returns
  `self.scheduler.status().max_in_flight_jobs` from `worker_count`, while the
  same file's `tick` loop immediately completes each `BuildNode` job
  synchronously.
- Observation: the current multi-LOD horizon is less than a kilometer across,
  not multiple kilometers.
  Evidence: `npm run bench:terrain:rust` from the hierarchy fix reported the
  multi-LOD probe at about 896 meters by 896 meters, with `max_rendered_lod: 2`.
- Observation: the existing benchmark already measures several useful coarse
  phases, but it is not yet enough to explain all generation cost.
  Evidence: `terrain_bench.rs` measures fill-only, fill-and-copy, apron fill,
  retained density-window prepare, cold mesh build, prepared mesh build, and a
  multi-LOD probe. It does not yet report per-node breakdown for density,
  neighbor-apron preparation, Dual Contouring extraction, material expansion,
  transfer/copy, worker latency, Rust completion, or renderer upload.
- Observation: one or two selected chunks are not representative enough for
  terrain performance.
  Evidence: terrain cost can vary dramatically depending on whether a node is
  empty, solid, near the terrain surface, high-frequency rocky terrain, near a
  biome/material transition, or part of a moving streaming delta. Benchmarks
  must report distributions across a realistic sample set, not just a single
  chunk.

## Decision Log

- Decision: preserve Rust ownership of terrain scheduling and visibility.
  Rationale: `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` say TypeScript
  must not become a terrain scheduler or terrain manager again. Web Workers may
  transport opaque build requests and completions, but the ordered hierarchy,
  request identity, stale rejection, generated/empty classification, and
  renderer-visible node selection remain Rust responsibilities.
  Date/Author: 2026-06-07 / Codex.
- Decision: benchmark realistic distributions before using any single number as
  a target.
  Rationale: terrain generation cost is data dependent. The useful numbers are
  mean, median, p95, max, and phase shares over many chunks, LODs, presets, and
  movement deltas. A one-chunk average can hide the stutter-causing outliers.
  Date/Author: 2026-06-07 / Codex.
- Decision: set a horizon acceptance target in world meters, not only in LOD
  count.
  Rationale: adding LOD3 is mechanically easy but may still fall short if the
  radius is too small. The player-visible target is at least a 4 km horizontal
  terrain span, which corresponds to roughly 2 km from the player toward the
  nearest far edge when centered.
  Date/Author: 2026-06-07 / Codex.
- Decision: prefer dedicated browser Web Workers running terrain build WASM over
  making the whole `engine_web` runtime shared-memory threaded as the first
  implementation.
  Rationale: terrain node generation is pure CPU work, while `engine_web` also
  owns WebGPU and browser-facing state. Worker-isolated terrain build jobs are
  easier to validate, easier to keep away from WebGPU state, and can reuse the
  existing generic `BrowserWorkerHost` concept. If this path proves too costly
  because of WASM startup or transfer overhead, record the evidence and evaluate
  a `wasm-bindgen-rayon` shared-memory path as a follow-up.
  Date/Author: 2026-06-07 / Codex.
- Decision: measure and budget renderer upload separately from terrain build.
  Rationale: moving generation off-thread can still hitch if many completed
  meshes are accepted and uploaded to GPU in a single frame. The plan must
  expose worker build time, transfer time, Rust completion time, and GPU upload
  time as separate costs.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

Not started. Expected outcomes are:

- Default terrain uses at least one additional far LOD and reaches a
  multi-kilometer visible terrain span.
- `npm run bench:terrain:rust` reports realistic average and percentile costs
  for terrain generation across many representative nodes, with phase
  breakdowns that identify where time is spent.
- Browser terrain generation no longer runs `build_node_mesh` on the main
  frame path.
- Browser smoke or a dedicated performance smoke records worker generation,
  stale-completion handling, frame timing, and upload behavior during movement.

## Contract and Quality Baseline

This plan preserves the ownership rules in `docs/API_CONTRACTS.md` and
`docs/ARCHITECTURE.md`: Rust owns terrain streaming, terrain generation
semantics, mesh packet semantics, active draw-set selection, and renderer upload.
TypeScript may host browser workers and move opaque messages, but it must not
decide desired chunks, LOD swaps, terrain hierarchy, mesh validity, or material
semantics.

The active terrain hierarchy rules from `docs/TERRAIN_REGRESSION_PLAN.md` remain
mandatory:

- A node must be generated before its children can be generated.
- A node cannot be discarded before its children are discarded.
- A child group can replace its parent only after all eight siblings are
  generated or identified as empty.
- A coarser parent remains available for instant fallback while children are
  hidden or discarded.

The coverage gate from `PLANS.md` applies. Before this plan can be complete, run
`npm run coverage:rust` and confirm modified implementation files are absent
from the default filtered below-threshold report, or record an explicit
exception with rationale.

After each milestone, run the repo-local `milestone-review` skill before marking
that milestone complete. If tool policy does not permit sub-agent spawning,
perform the same review locally and record that constraint here.

## Context and Orientation

Current default terrain stream configuration lives in
`crates/engine_web/src/terrain_stream.rs`:

- `DEFAULT_TERRAIN_CELL_SIZE` is `1.0`.
- `TERRAIN_CHUNK_CELLS_PER_AXIS` is `32` in
  `crates/terrain_core/src/constants.rs`.
- Default vertical offsets are `[-2, -1, 0, 1]`.
- Default LOD bands are LOD0 radius 1, LOD1 radius 2, and LOD2 radius 3.
- `DEFAULT_TERRAIN_MAX_JOBS_PER_TICK` is `6`.

World-space node size is computed by
`terrain_core::terrain_node_cell_size(base_cell_size, lod) *
TERRAIN_CHUNK_CELLS_PER_AXIS`. With base cell size 1.0, node spans are:

- LOD0: 32 meters.
- LOD1: 64 meters.
- LOD2: 128 meters.
- LOD3: 256 meters.
- LOD4: 512 meters.

The current stutter cause is in `crates/engine_web/src/terrain_stream.rs`.
`BrowserTerrainStream::tick` calls `self.scheduler.tick()`, loops through the
returned `TerrainStreamJob::BuildNode` jobs, and calls `complete_node_job`.
`complete_node_job` immediately calls `terrain_core::build_node_mesh`. That
means mesh generation runs inside the browser frame tick.

There is existing worker-pool code in `crates/terrain_core/src/worker_pool.rs`
and facade exports in `crates/terrain_core/src/facade.rs`, but those are
request-state fixtures. They do not currently create browser workers or run
playable terrain builds off-thread.

The generic TypeScript worker substrate is
`src/engine/browser/browserWorkerHost.ts`. It can post opaque request envelopes
and receive completions. This is the right shape to reuse, provided terrain
meaning stays in Rust and the worker-side terrain WASM entrypoint.

The standalone `assets/wasm/terrain_core.wasm` currently exports
`ofg_build_chunk_mesh` plus mesh vertex/index buffer pointers. A worker can use
that raw WASM interface to build a node by passing the node coordinate and the
LOD-scaled cell size. If the raw interface is not sufficient, add a narrow
terrain-worker export such as `ofg_build_node_mesh` rather than making
TypeScript reconstruct terrain semantics.

## Plan of Work

Milestone 1 builds measurement before changing performance-sensitive behavior.
Extend `terrain_core::benchmark` and `crates/ofg_test_harness/src/terrain_bench`
so the benchmark samples a realistic terrain workload. The sample set should be
generated from actual stream scenarios: initial settle, running movement deltas,
multiple presets, multiple seeds, and the default LOD bands. It should include
empty nodes, solid nodes, surface-heavy nodes, and high-complexity nodes when
the terrain field produces them. The report must include mean, median, p95, min,
max, sample count, vertex/index counts, empty ratio, and bytes generated per
LOD and per phase.

Add a profile helper in `terrain_core::benchmark`, for example
`profile_node_mesh_build(seed, preset, key, base_cell_size)`, that reports:

- density generation time for the center chunk and neighbor apron chunks;
- retained-density reuse versus fresh generation counts;
- Dual Contouring vertex extraction and index emission time;
- material palette expansion time;
- mesh buffer sizes and copy/transfer simulation time;
- total cold build time and prepared-density build time.

If the current meshing functions do not expose those phases cleanly, split them
inside `crates/terrain_core/src/mesh.rs` without changing output geometry. Keep
production APIs simple and keep benchmark-specific structs under
`terrain_core::benchmark` where possible.

Milestone 2 extends the horizon. Add at least one additional far LOD to
`default_terrain_lod_bands` and update tests/smoke expectations so they do not
hard-code LOD2 as the farthest terrain. Start with LOD3, then choose LOD3
radius or a LOD4 horizon band based on the Milestone 1 benchmark. The acceptance
target is a visible horizontal span of at least 4096 meters in both X and Z
after the stream settles, with `max_rendered_lod >= 3`, no missing visible cover
at the player position, no parent/child visible overlap, and no broad sky-hole
regression in Rust smoke.

Milestone 3 moves node builds off the main frame path. Change the runtime shape
from "scheduler returns jobs and `tick` builds them immediately" to "scheduler
returns build requests, requests are submitted to a worker client, and
completions are accepted later." The Rust scheduler must still own request
identity and hierarchy state. Each request should include a request id,
generation, `TerrainNodeKey`, seed, preset, and base cell size or LOD-scaled
cell size. Each completion must echo the request id, generation, key, and a
generated-empty or generated-mesh result. Rust must reject stale or mismatched
completions.

Use browser Web Workers for the first implementation. A small worker script can
load terrain WASM, call the Rust terrain build export, copy the vertex and index
buffers into transferable `ArrayBuffer`s, and return them. TypeScript may route
these opaque envelopes with `BrowserWorkerHost`, but it must not pick LODs,
decide desired nodes, classify meshes, or alter terrain materials. The main
Rust runtime accepts completions, stores mesh data, and uploads to WebGPU on the
main thread.

Worker count should default from `navigator.hardwareConcurrency`, clamped to a
small practical range such as 2 to 12 workers, with room for a debug override.
The debug snapshot should report the actual worker runtime, actual worker
count, in-flight worker jobs, completed worker jobs, stale completions, failed
jobs, and any fallback to synchronous generation. Synchronous terrain generation
may remain as a test-only or unsupported fallback, but browser smoke must prove
the normal playable path uses workers.

Milestone 4 validates stutter. Add a browser smoke or performance-smoke path
that waits for initial terrain settle, moves the player at running speed, and
records frame deltas, worker queue depth, completion bursts, and terrain upload
counts per frame. The hard correctness gate is that terrain node generation does
not run on the main thread in the normal browser path. The performance report
should also identify whether remaining hitches come from worker completion
bursts, WASM-to-main copies, Rust acceptance, or WebGPU upload. If upload bursts
cause hitches, add a bounded per-frame upload budget and expose queue depth in
the debug snapshot.

Milestone 5 finishes validation and documentation. Update
`docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` to describe the worker
boundary precisely. Run full tests, smoke, coverage, benchmark, wasm checks, and
diff checks. Run `milestone-review` and fix required findings or record a
rejected finding with rationale.

## Concrete Steps

Use `C:\dev\ofg` as the working directory.

1. Capture current baseline:

   `npm run bench:terrain:rust -- --iterations 12 --mesh-iterations 6 --warmup 2`

   Expected: writes `artifacts/terrain-bench/<run>/report.json`, prints the
   current multi-LOD max LOD and visible span, and reports current mesh build
   timing.

2. Add benchmark profiling and tests:

   `cargo test -p terrain_core benchmark --no-fail-fast`

   `cargo test -p ofg_test_harness terrain_bench --no-fail-fast`

   Expected: benchmark helper tests pass, and report schema tests verify that
   multi-node sample distributions include more than one LOD and more than one
   chunk classification.

3. Run the improved benchmark:

   `npm run bench:terrain:rust -- --iterations 24 --mesh-iterations 12 --warmup 3`

   Expected: the JSON report contains per-LOD and aggregate timing
   distributions with `meanMs`, `medianMs`, `p95Ms`, `maxMs`, sample counts,
   and phase shares. It should not rely on a single chunk.

4. Add/tune far LOD bands and validate stream logic:

   `cargo test -p terrain_core stream_scheduler --no-fail-fast`

   `cargo test -p engine_web browser_terrain_stream --no-fail-fast`

   Expected: tests prove the new far LOD is present after settling, visible span
   is at least 4096 meters, and the hierarchy invariants still hold.

5. Validate render smoke:

   `npm run smoke:rust`

   Expected: multi-LOD reports show `maxRenderedLod >= 3`, visible X/Z span at
   least 4096 meters, no broad lower-center sky hole, no missing stream nodes
   after settle, and no parent/child visible overlap.

6. Implement browser worker generation and validate with targeted tests:

   `npm run test:ts`

   `cargo test -p engine_web terrain_worker --no-fail-fast`

   Expected: fake worker tests cover request routing, transfer payloads,
   generation/key validation, stale completion rejection, worker failure retry,
   and no synchronous build on the normal browser path.

7. Validate browser integration:

   `npm run smoke:browser`

   Expected: the browser debug snapshot reports a worker runtime, actual worker
   count greater than one when available, zero normal-path main-thread terrain
   builds, terrain settled after movement, mixed LODs including the new far LOD,
   and no black/blank frames.

8. Run completion gates:

   `npm test`

   `npm run coverage:rust`

   `npm run check:wasm`

   `npm run bench:terrain:rust`

   `git -c safe.directory=C:/dev/ofg diff --check`

   Expected: all commands pass. Coverage output does not list modified
   implementation files below the default threshold. WASM generated artifacts
   are current.

## Milestone Review

After each milestone:

1. Update this plan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective.
2. Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` if the milestone
   changes terrain, worker, debug snapshot, benchmark, or renderer contracts.
3. Run the repo-local `milestone-review` skill before marking the milestone
   complete. If sub-agent tools are unavailable or tool policy forbids spawning,
   record that and perform the same contract, code-quality, docs, legacy,
   validation, and ownership review locally.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run the relevant validation commands for that milestone and record the
   artifacts or concise output evidence here.

## Validation and Acceptance

This plan is complete only when all of the following are true:

- Default playable terrain renders at least one additional far LOD beyond LOD2.
- Settled visible terrain span is at least 4096 meters in X and Z in the Rust
  terrain benchmark and smoke report.
- Movement-delta terrain smoke still proves no missing cover at the player, no
  broad sky/floor holes, and no visible parent/child overlap.
- `npm run bench:terrain:rust` reports terrain generation cost over a realistic
  multi-chunk sample set, including mean, median, p95, max, sample counts, and
  phase breakdowns by LOD and aggregate.
- Benchmark samples include multiple coordinates, multiple presets or terrain
  classes, multiple LODs, and both settled and movement-delta streaming
  populations.
- Browser terrain generation uses worker threads on the normal playable path.
  Debug output must distinguish actual worker count from scheduler job limit.
- Normal browser smoke reports zero synchronous main-thread node builds after
  worker initialization, or records an explicit unsupported-browser fallback
  that is not used by the standard smoke environment.
- Stale worker completions are rejected by Rust using request id, generation,
  and `TerrainNodeKey`.
- Worker failures do not permanently wedge the stream; failed jobs are retried
  or marked failed in a way the scheduler can recover from.
- Mesh data returned by workers is transferred or copied intentionally, with
  byte counts and transfer/copy timings recorded in benchmarks or browser
  performance artifacts.
- Remaining frame hitches, if any, are attributed to a measured phase such as
  GPU upload bursts, and the plan records follow-up work or applies an upload
  budget.
- `npm test`, `npm run smoke:rust`, `npm run smoke:browser`,
  `npm run coverage:rust`, `npm run check:wasm`,
  `npm run bench:terrain:rust`, and
  `git -c safe.directory=C:/dev/ofg diff --check` pass.
- Modified implementation files do not appear in the default filtered
  `npm run coverage:rust` below-threshold report unless an explicit exception
  is recorded with rationale.

## Idempotence and Recovery

Benchmark changes are additive and can be rerun safely. Benchmark artifacts
under `artifacts/terrain-bench/` are generated output and should not be
committed.

LOD tuning should be introduced with tests that assert world-span outcomes
rather than fragile exact node counts. If a chosen radius or far LOD is too
expensive, revert only the configuration change or replace it with a coarser
horizon band while preserving the benchmark evidence in this plan.

Worker generation should land behind a narrow runtime boundary. If browser
worker initialization fails during development, keep a clearly named fallback
path for tests and diagnostics, but do not let the standard browser smoke pass
silently on synchronous generation. Record fallback use in debug snapshots.

Generated WASM artifacts should be regenerated with `npm run build:wasm` or
validated with `npm run check:wasm`. Do not hand-edit generated files under
`assets/wasm/` or `src/generated/`.

## Artifacts and Notes

Baseline facts from the previous completed work:

- Commit `a6ba4b3 Fix hierarchical terrain streaming` completed the hierarchy
  fix and pushed it to `origin/main`.
- Current default bands before this plan: LOD0 radius 1, LOD1 radius 2, LOD2
  radius 3, vertical offsets `[-2, -1, 0, 1]`.
- Current far span before this plan: about 896 meters by 896 meters from the
  multi-LOD benchmark probe.
- Current stutter suspect: `BrowserTerrainStream::tick` synchronously calls
  `build_node_mesh` through `complete_node_job`.

Expected benchmark artifact shape after Milestone 1:

    artifacts/terrain-bench/<run>/report.json
      aggregate mean/median/p95/max terrain node build time
      per-LOD mean/median/p95/max terrain node build time
      per-class empty/solid/surface/complex sample counts
      density/apron/dual-contouring/material/copy phase timings
      movement-delta and initial-settle stream populations

Expected browser performance artifact after Milestone 4:

    artifacts/browser-smoke/<run>/terrain-performance.json
      worker count and hardware concurrency
      main-thread terrain build count
      worker job count, failures, stale completions, retries
      completion-to-upload latency
      mesh uploads per frame
      frame delta mean/median/p95/max during movement

## Interfaces and Dependencies

Planned Rust-side interfaces:

- `terrain_core::benchmark::profile_node_mesh_build(seed, preset, key,
  base_cell_size) -> TerrainNodeBuildProfile`.
- `TerrainNodeBuildProfile` records phase durations, density store hits,
  generated bytes, vertex count, index count, and empty/non-empty
  classification.
- `engine_web::BrowserTerrainStream` no longer synchronously calls
  `build_node_mesh` in the normal browser path.
- `engine_web` exposes terrain build requests and completion acceptance through
  a narrow API that includes request id, generation, `TerrainNodeKey`, and
  generated-empty or generated-mesh result.
- Debug snapshots distinguish scheduler job capacity from actual browser worker
  count and report worker queue/completion/fallback counters.

Planned TypeScript-side interfaces:

- Reuse or extend `src/engine/browser/browserWorkerHost.ts` for opaque worker
  envelopes.
- Add a terrain build worker client near the Rust browser runtime adapter. It
  routes Rust-issued build requests to workers and routes completions back to
  Rust without choosing LODs or desired chunks.
- Add a worker module that loads terrain WASM, calls a Rust terrain mesh build
  export, and transfers vertex/index buffers back to the main thread.

Dependencies and constraints:

- Browser workers require the existing COOP/COEP headers from
  `tools/dev-server.mjs`; browser smoke already checks `crossOriginIsolated` and
  `SharedArrayBuffer` availability.
- WebGPU resource creation and upload remain on the main browser thread through
  Rust `wgpu`.
- The worker implementation must not require TypeScript to understand terrain
  materials, noise, scheduler policy, or LOD hierarchy.

## Revision Notes

- 2026-06-07 / Codex: Initial plan created from the user's request to add
  longer-distance LODs, benchmark realistic terrain generation cost, and move
  generation onto separate threads. Included the later clarification that
  performance metrics must average across realistic chunk populations because
  different chunks can have drastically different costs.
