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
- [x] Milestone 1: build a trustworthy terrain generation benchmark that
  samples realistic chunk populations and reports cost distributions plus phase
  breakdowns.
- [x] (2026-06-07 22:56+01:00) Captured the pre-change terrain benchmark
  baseline with
  `npm run bench:terrain:rust -- --iterations 12 --mesh-iterations 6 --warmup 2`;
  report:
  `artifacts/terrain-bench/run-1780868582-991/report.json`.
- [x] (2026-06-07 22:56+01:00) Added
  `terrain_core::benchmark::profile_node_mesh_build` and split raw Dual
  Contouring from material expansion so benchmark reports can time density,
  contouring, material expansion, and buffer copy phases separately.
- [x] (2026-06-07 22:56+01:00) Extended
  `crates/ofg_test_harness/src/terrain_bench.rs` with a profiled node
  population sampled from streaming-style LOD bands, movement centers, two seed
  variants, all four presets, and explicit air/solid/surface probes.
- [x] (2026-06-07 22:56+01:00) Ran targeted benchmark validation:
  `cargo test -p terrain_core --features benchmark benchmark --no-fail-fast`
  and `cargo test -p ofg_test_harness terrain_bench --no-fail-fast`.
- [x] (2026-06-07 22:56+01:00) Ran the full improved benchmark with
  `npm run bench:terrain:rust -- --iterations 24 --mesh-iterations 12 --warmup 3`;
  report:
  `artifacts/terrain-bench/run-1780870525-499/report.json`.
- [x] (2026-06-07 22:56+01:00) Fixed local milestone-review code-size finding
  by moving profiled terrain-node benchmark logic into
  `crates/ofg_test_harness/src/terrain_bench_profile.rs`; after the split,
  `terrain_bench.rs` is 935 lines and `terrain_bench_profile.rs` is 864 lines.
- [x] (2026-06-07 23:18+01:00) Milestone 1 review complete. Sub-agent review
  was not used because the user did not explicitly request delegated reviewers;
  local contract, code-quality, legacy, correctness, and validation passes were
  performed. Required findings were fixed before marking the milestone
  complete.
- [x] Milestone 2: add at least one extra terrain LOD and tune default horizon
  bands to reach a multi-kilometer visible span without breaking hierarchical
  streaming.
- [x] (2026-06-07 23:58+01:00) Added default LOD3 and LOD4 far bands. The
  playable stream now uses LOD0 radius 1, LOD1 radius 2, LOD2 radius 3, LOD3
  radius 2, and LOD4 radius 4; near bands use vertical offsets
  `[-2, -1, 0, 1]`, while far bands use `[-1, 0]`.
- [x] (2026-06-07 23:58+01:00) Added visible-span reporting to Rust terrain
  stream status, browser debug JS, TypeScript debug types, Rust smoke reports,
  browser smoke assertions, and the multi-LOD benchmark probe.
- [x] (2026-06-07 23:58+01:00) Updated the profiled-node benchmark population
  to sample the new LOD0 through LOD4 default bands. The population now uses
  one deterministic node per LOD per streaming source, plus explicit class
  probes, for 200 release benchmark samples.
- [x] (2026-06-07 23:58+01:00) Ran Milestone 2 targeted validation:
  `cargo test -p terrain_core stream_scheduler --no-fail-fast`,
  `cargo test -p engine_web browser_terrain_stream --no-fail-fast`,
  `cargo test -p ofg_test_harness terrain_bench --no-fail-fast`,
  `cargo test -p ofg_test_harness multi_lod_scenario_terrain_reports_lod_counts --no-fail-fast`,
  and `npm run test:ts`.
- [x] (2026-06-07 23:58+01:00) Ran render and browser smoke after the LOD4
  horizon change: `npm run smoke:rust` wrote
  `artifacts/rust-smoke/run-1780872366-073/report.json`; `npm run smoke:browser`
  wrote `artifacts/browser-smoke/2026-06-07T22-51-36-514Z/report.json`.
- [x] (2026-06-07 23:58+01:00) Ran the updated release terrain benchmark with
  `npm run bench:terrain:rust -- --iterations 12 --mesh-iterations 6 --warmup 2`;
  report:
  `artifacts/terrain-bench/run-1780872880-562/report.json`.
- [x] (2026-06-08 00:05+01:00) Milestone 2 review complete. Sub-agent review
  was not used because the user did not explicitly request delegated reviewers;
  local contract, code-quality, legacy, correctness, and validation passes were
  performed. Required findings were fixed before marking the milestone
  complete.
- [x] Milestone 3: move node generation off the browser main thread using
  browser workers while keeping Rust scheduler ownership and stale-completion
  validation.
- [x] (2026-06-08 00:45+01:00) Added Rust-owned terrain build request and
  completion packets. `BrowserTerrainStream::tick_for_workers` now queues
  worker jobs instead of calling `build_node_mesh` on the browser frame path;
  Rust still owns request ids, generation tokens, node keys, retries, stale
  completion rejection, empty-node state, mesh cache updates, and visible-node
  synchronization.
- [x] (2026-06-08 00:45+01:00) Added the browser worker bridge:
  `TerrainWorkerClient` routes opaque Rust requests through
  `BrowserWorkerHost`, `terrainBuildWorker.ts` loads `terrain_core.wasm` and
  calls the raw mesh export, and `RustBrowserGameAdapter` drains completions
  before each Rust tick and submits new requests after the tick.
- [x] (2026-06-08 00:45+01:00) Extended debug status and smoke assertions for
  the real worker runtime. Final browser smoke
  `artifacts/browser-smoke/2026-06-07T23-35-19-209Z/report.json` reported
  `terrainWorkerPoolRuntime: "browser-worker"`, 12 workers, 770 completed
  worker builds, 0 failed completions, 0 stale completions,
  `synchronousBuildCount: 0`, max rendered LOD4, 4608 m by 4608 m visible span,
  and 0 missing nodes.
- [x] (2026-06-08 00:45+01:00) Ran Milestone 3 validation:
  `cargo test -p terrain_core stream_scheduler --no-fail-fast`,
  `cargo test -p engine_web worker --no-fail-fast`,
  `npm run test:ts`, `npm test`, `npm run check:wasm`,
  `npm run smoke:browser`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- [x] (2026-06-08 00:45+01:00) Milestone 3 review complete. Sub-agent review
  was not used because the user did not explicitly request delegated reviewers;
  local contract, code-quality, legacy, correctness, and validation passes were
  performed. Required findings were fixed before marking the milestone
  complete.
- [x] Milestone 4: add browser stutter/performance validation that proves worker
  generation is active and records frame-time/upload behavior during movement.
- [x] (2026-06-08 01:00+01:00) Added Rust-owned terrain update diagnostics to
  renderer status: latest update CPU time, upserted/removed mesh counts, and
  uploaded terrain vertex/index counts.
- [x] (2026-06-08 01:00+01:00) Added a browser smoke movement-performance pass.
  It starts from settled terrain, holds run-forward input for 360 animation
  frames, writes full samples to `movement-performance-samples.json`, and
  asserts worker completions, no worker failures/stale completions, no
  synchronous builds, settled missing-node count 0, LOD4 cover, frame-delta
  bounds, and terrain-update/upload bounds.
- [x] (2026-06-08 01:05+01:00) Final browser smoke for this milestone wrote
  `artifacts/browser-smoke/2026-06-08T00-03-01-474Z/report.json`. Movement
  summary: 360 samples, 359 frame advance, mean frame delta 44.335 ms,
  p95 83.425 ms, max 116.770 ms, 223.945 m movement, 272 worker completions,
  0 failed/stale/synchronous builds, max 12 in-flight workers, max completion
  burst 12, max terrain update 14 ms, 130 upserted meshes, 134 removed meshes,
  13,186,608 uploaded vertex floats, 694,032 uploaded indices, and settled
  missing-node count 0.
- [x] (2026-06-08 01:05+01:00) Milestone 4 review complete. Sub-agent review
  was not used because the user did not explicitly request delegated reviewers;
  local contract, code-quality, legacy, correctness, and validation passes were
  performed. Required findings were fixed before marking the milestone
  complete.
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
- Observation: the pre-change benchmark already showed density costs dominate
  cold terrain generation and can spike badly in streaming windows.
  Evidence: `artifacts/terrain-bench/run-1780868582-991/report.json` from
  `npm run bench:terrain:rust -- --iterations 12 --mesh-iterations 6 --warmup 2`
  reported cold mesh median 56.454 ms, prepared mesh median 8.067 ms, and
  retained density-window p95 661.661 ms.
- Observation: the new profiled population covers realistic distribution
  requirements before LOD/threading changes begin.
  Evidence: `artifacts/terrain-bench/run-1780870525-499/report.json` reports
  328 profiled node samples, 2 seeds, 4 presets, 9 sources, 3 LODs, and class
  counts for empty air, solid, surface sparse, surface heavy, and surface
  complex nodes.
- Observation: density generation is the dominant cost across the sampled node
  population.
  Evidence: the corrected full improved benchmark reported profiled cold-node
  median 53.504 ms, p95 78.990 ms, mean 57.636 ms, with mean phase shares of
  about 93.8% density, 5.8% contouring, 0.3% material expansion, and 0.0%
  buffer copy. The prepared-density repeat for the same population reported
  median 1.222 ms, p95 12.877 ms, and mean 4.075 ms.
- Observation: milestone review caught that the first profiler implementation
  only measured cold node builds even though this plan explicitly required
  prepared-density build timing.
  Evidence: `TerrainNodeBuildProfile` now includes `preparedTotalMs` and
  prepared phase timings in the benchmark JSON. The corrected full report is
  `artifacts/terrain-bench/run-1780870525-499/report.json`.
- Observation: the detailed profile report is large enough to need attention if
  it grows further.
  Evidence: after splitting, `crates/ofg_test_harness/src/terrain_bench.rs` is
  935 lines and `crates/ofg_test_harness/src/terrain_bench_profile.rs` is
  864 lines. This is below the hard 1000-line split threshold, but above the
  600-line split-pressure threshold.
- Observation: LOD3 alone is an awkward fit for a 4 km target; LOD4 is the
  practical first horizon band.
  Evidence: a LOD3 node spans 256 meters, so a 4096 meter target would require
  a very wide LOD3 radius. The chosen LOD4 radius 4 band spans 4608 meters in
  X and Z with 512 meter nodes.
- Observation: adding the far horizon makes the synchronous stream visibly
  expensive in tests and smoke.
  Evidence: the targeted engine_web default-band test and the far-view smoke
  scenario each spent about 69 seconds generating the settled stream in the
  current synchronous path, and `npm run smoke:rust` took about 318 seconds.
  The release benchmark stream probe rendered 347 nodes after considering 770
  desired nodes.
- Observation: the LOD4 release benchmark still points at density generation as
  the main cost, but contouring is now large enough to track.
  Evidence:
  `artifacts/terrain-bench/run-1780872880-562/report.json` reports 200 profiled
  node samples with median 58.397 ms, p95 80.049 ms, mean 61.500 ms, and phase
  mean shares of about 88.9% density, 10.3% contouring, 0.7% material
  expansion, and 0.1% copy. The prepared-density repeat reports median
  7.983 ms and p95 13.431 ms.
- Observation: the browser playable path now proves actual worker generation,
  not just a scheduler capacity number.
  Evidence:
  `artifacts/browser-smoke/2026-06-07T23-35-19-209Z/report.json` reports
  `workerPoolRuntime: "browser-worker"`, 12 workers, 770 completed worker
  builds, 0 failed completions, 0 stale completions, and
  `synchronousBuildCount: 0`.
- Observation: worker pool failures must fail all outstanding requests, not
  only requests assigned to the worker that reported the error.
  Evidence: `BrowserWorkerHost::reset` terminates and recreates the whole pool.
  Milestone review caught that only failing one worker slot could orphan
  in-flight requests assigned to other workers. `TerrainWorkerClient` now emits
  failed completions for every outstanding request before resetting the pool.
- Observation: moving generation off-thread does not automatically eliminate
  all main-thread hitches.
  Evidence: Rust still accepts completed typed arrays, updates mesh cache state,
  and uploads/prunes renderer meshes during later frame ticks. Browser smoke
  proves worker generation is active, but it does not yet measure completion
  burst cost or GPU upload cost during movement deltas.
- Observation: browser debug polling can collide with an in-progress
  wasm-bindgen mutable borrow if every debug getter calls Rust directly.
  Evidence: the first Milestone 4 browser smoke attempt hit
  `recursive use of an object detected which would lead to unsafe aliasing in rust`
  while `page.waitForFunction` polled `getRendererStatus`. The browser app now
  caches the latest successful Rust debug snapshot once per frame and debug
  hooks read the cache.
- Observation: `std::time::Instant::now` is not usable in the current browser
  wasm target.
  Evidence: the first terrain update timing implementation panicked with
  `time not implemented on this platform` from
  `library/std/src/sys/pal/wasm/../unsupported/time.rs`. The final
  implementation uses `js_sys::Date::now()` on wasm and `Instant` only for
  native builds.
- Observation: the first movement-performance smoke shows terrain generation is
  off-thread, while remaining frame cost is more likely renderer/update side
  than worker execution.
  Evidence:
  `artifacts/browser-smoke/2026-06-08T00-03-01-474Z/report.json` recorded
  272 worker completions during a 223.945 m run, no failed/stale/synchronous
  builds, max terrain update 14 ms, frame p95 83.425 ms, and max frame delta
  116.770 ms.

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
- Decision: keep Milestone 1 profiling inside native Rust rather than adding a
  browser benchmark.
  Rationale: the first question is terrain generation cost and phase attribution.
  Native Rust gives deterministic low-noise measurements and preserves the
  contract that TypeScript must not become a terrain client. Browser transfer,
  worker latency, and upload timing remain Milestone 3 and 4 work.
  Date/Author: 2026-06-07 / Codex.
- Decision: split profiled-node benchmarking into
  `crates/ofg_test_harness/src/terrain_bench_profile.rs`.
  Rationale: local milestone review flagged the initial 1650-line
  `terrain_bench.rs` as too large. Keeping the profile population/reporting in
  a separate module preserves readability while leaving the command and JSON
  artifact shape unchanged.
  Date/Author: 2026-06-07 / Codex.
- Decision: allow `terrain_bench_profile.rs` to remain at 864 lines for this
  milestone, but treat further growth as split work.
  Rationale: it is below the hard 1000-line threshold and is cohesive: profile
  population selection, profile report conversion, and profile-specific tests.
  Later worker/upload profiling should go in separate modules rather than
  growing this file.
  Date/Author: 2026-06-07 / Codex.
- Decision: use LOD4 as the first multi-kilometer horizon band.
  Rationale: LOD3 nodes are 256 meters wide, which makes a 4096 meter visible
  span require an excessively wide LOD3 radius. LOD4 nodes are 512 meters wide;
  radius 4 gives a measured 4608 meter span while preserving LOD3 as an
  intermediate refinement band.
  Date/Author: 2026-06-07 / Codex.
- Decision: use two far vertical offsets, `[-1, 0]`, for LOD3 and LOD4.
  Rationale: those bands cover 512 meters vertically at LOD3 and 1024 meters at
  LOD4 around the player-height center, while avoiding a third far vertical
  layer before worker generation exists. Near LOD0 through LOD2 keeps the wider
  `[-2, -1, 0, 1]` band for player cover.
  Date/Author: 2026-06-07 / Codex.
- Decision: expose settled visible terrain span as Rust-owned debug and smoke
  data.
  Rationale: tests and browser smoke should assert the player-facing distance
  target directly instead of inferring it from key strings or fragile node
  counts. TypeScript may assert the values, but Rust owns the calculation.
  Date/Author: 2026-06-07 / Codex.
- Decision: reduce profiled stream sampling from three nodes per LOD per source
  to one after adding LOD3 and LOD4.
  Rationale: the new population still samples 200 nodes across two seeds, four
  presets, movement sources, explicit class probes, and five LODs. Keeping the
  old per-LOD sample count made quick benchmark runs too slow while the normal
  path is still synchronous.
  Date/Author: 2026-06-07 / Codex.
- Decision: use `terrain_core.wasm` only inside the dedicated browser terrain
  build worker for Milestone 3.
  Rationale: this gets pure CPU generation off the browser frame path without
  making TypeScript own terrain scheduling, desired sets, LOD visibility, mesh
  validity, renderer uploads, or retry policy. Rust emits opaque build requests
  and validates every completion.
  Date/Author: 2026-06-08 / Codex.
- Decision: a browser worker process error fails all outstanding terrain build
  requests before resetting the worker pool.
  Rationale: the worker host reset terminates every worker, so requests assigned
  to other workers would otherwise be orphaned and remain in-flight forever.
  Rust receives failed completions and can retry through the scheduler.
  Date/Author: 2026-06-08 / Codex.
- Decision: keep worker request ids within the JavaScript safe-integer range.
  Rationale: wasm-bindgen object packets currently cross the boundary as JS
  numbers. Rust wraps generated request ids before `2^53 - 1` and rejects
  completion ids outside that range.
  Date/Author: 2026-06-08 / Codex.
- Decision: defer completion-burst and renderer-upload budgeting to Milestone 4.
  Rationale: Milestone 3 proves generation no longer runs on the frame path.
  Upload and completion costs are still main-thread work and need a movement
  performance smoke with frame deltas, worker queue depth, completion bursts,
  and upload timing.
  Date/Author: 2026-06-08 / Codex.
- Decision: expose terrain update/upload diagnostics through Rust renderer
  status.
  Rationale: browser smoke should record the CPU-side terrain update section
  that still runs on the main thread: removed/upserted mesh counts, uploaded
  vertex/index counts, and elapsed update time. TypeScript records and asserts
  these values but does not derive terrain scheduling decisions from them.
  Date/Author: 2026-06-08 / Codex.
- Decision: cache the Rust debug snapshot once per browser frame for debug
  hooks.
  Rationale: smoke and debug UI may poll multiple getters while a frame is
  being processed. Reading a cached Rust-assembled snapshot avoids transient
  wasm-bindgen recursive borrow failures without moving state ownership into
  TypeScript.
  Date/Author: 2026-06-08 / Codex.

## Outcomes & Retrospective

Milestones 1 through 4 are complete. The overall plan remains active. Current
outcomes:

- Default terrain now uses additional LOD3 and LOD4 far bands and reaches a
  settled visible span of 4608 meters by 4608 meters in the release terrain
  benchmark, Rust smoke, and browser smoke.
- `npm run bench:terrain:rust` now reports realistic average and percentile
  costs for terrain generation across many representative nodes, with phase
  breakdowns that identify where time is spent.
- Browser terrain generation no longer runs `build_node_mesh` on the browser
  frame path. The playable browser path reports `"browser-worker"` with 12
  workers on the validated machine, 770 completed worker builds, 0 failed/stale
  completions, and `synchronousBuildCount: 0` in the final smoke report.
- Browser smoke now records worker generation, stale/failure counters, movement
  frame timing, completion bursts, and terrain update/upload behavior during
  a running movement delta.

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
- Default near vertical offsets are `[-2, -1, 0, 1]`.
- Default far vertical offsets are `[-1, 0]`.
- Default LOD bands are LOD0 radius 1, LOD1 radius 2, LOD2 radius 3, LOD3
  radius 2, and LOD4 radius 4.
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

Milestone 1 review, 2026-06-07 / Codex:

- Scope: benchmark profiling and realistic node population reporting for
  `npm run bench:terrain:rust`; changed Rust benchmark helpers, mesh phase split,
  benchmark harness report schema, and active docs/contracts.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agent review was skipped because delegated reviewers
  were not explicitly requested by the user.
- Required findings fixed: split the oversized 1650-line benchmark runner into
  `terrain_bench.rs` plus `terrain_bench_profile.rs`; added missing
  prepared-density repeat timings to `TerrainNodeBuildProfile` and benchmark
  JSON; corrected cold-total timing so it no longer includes the prepared
  repeat.
- Follow-ups recorded: `terrain_bench_profile.rs` is 864 lines and should not
  absorb later worker/upload profiling; split future profiling modules by
  responsibility.
- Rejected findings: none.
- Validation rerun:
  `cargo test -p terrain_core --features benchmark benchmark --no-fail-fast`,
  `cargo test -p ofg_test_harness terrain_bench --no-fail-fast`,
  `npm run bench:terrain:rust -- --iterations 24 --mesh-iterations 12 --warmup 3`,
  and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: benchmark profile samples are representative of the current
  LOD2 stream and explicit class probes; Milestone 2 must update the sampling
  bands when adding farther LODs.

Milestone 2 review, 2026-06-08 / Codex:

- Scope: default far LOD horizon and span reporting; changed Rust terrain
  stream defaults/status, Rust/wgpu JS debug conversion, TypeScript debug
  schema/tests, Rust smoke and benchmark reports, browser smoke assertions,
  generated WASM artifacts, and active docs/contracts.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agent review was skipped because delegated reviewers
  were not explicitly requested by the user.
- Required findings fixed: removed duplicated visible-span calculation from
  `crates/ofg_test_harness/src/render_smoke/scenarios.rs`, bringing it back
  under the 600-line split-pressure threshold; replaced a hard-coded browser
  smoke span literal with the named `minMultiKmTerrainSpanMeters` constant; and
  marked `docs/TERRAIN_PLAN.md` as completed historical context with current
  follow-up work in this plan.
- Follow-ups recorded: `crates/ofg_test_harness/src/terrain_bench_profile.rs`
  remains 877 lines and must not absorb worker/upload profiling; large existing
  files such as `tools/browser-smoke.mjs`, `crates/engine_web/src/tests.rs`,
  and `crates/engine_web/src/wgpu_renderer.rs` should be split by
  responsibility when future changes add substantial new behavior.
- Rejected findings: none.
- Validation rerun after review fixes:
  `cargo test -p ofg_test_harness multi_lod_scenario_terrain_reports_lod_counts --no-fail-fast`,
  `npm run check:wasm`, `node --check tools/browser-smoke.mjs`,
  `npm run smoke:browser`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: Milestone 2 intentionally increases synchronous terrain work
  before worker generation exists. Correctness and smoke pass, but the roughly
  69-second far-settle tests and 318-second Rust smoke run are evidence that
  Milestone 3 must move generation off the frame path.

Milestone 3 review, 2026-06-08 / Codex:

- Scope: browser-worker terrain generation path; changed Rust terrain stream
  request/completion state, wasm-bindgen worker methods, TypeScript worker
  client and worker module, browser runtime adapter routing, debug snapshot
  schema, browser smoke assertions, generated engine_web WASM artifacts, and
  active docs/contracts.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agent review was skipped because delegated reviewers
  were not explicitly requested by the user.
- Required findings fixed: worker pool errors now fail every outstanding
  request before pool reset instead of orphaning requests assigned to other
  workers; worker request ids are capped to JavaScript safe integers and the
  Rust completion parser rejects out-of-range ids; `RustBrowserGameAdapter`
  now creates terrain workers after wasm game creation and disposes them if
  worker configuration or initial resize fails; stale active docs now describe
  `terrain_core.wasm` as a dedicated worker-build artifact rather than
  fixture-only.
- Follow-ups recorded: Milestone 4 must measure completion-burst and renderer
  upload costs because those remain main-thread work; `crates/engine_web/src/wgpu_renderer.rs`
  is still far over the preferred file size and absorbed more wasm packet glue;
  `tools/browser-smoke.mjs` and `crates/engine_web/src/tests.rs` remain
  oversized existing files and should be split by responsibility before they
  absorb more performance-smoke or worker lifecycle coverage.
- Rejected findings: no separate module-mocking test was added for the
  `RustBrowserGameAdapter.create` disposal guard. The worker routing and reset
  behavior are covered at constructor level, the final browser smoke exercises
  normal static creation, and adding brittle import mocking would not improve
  the worker-streaming contract enough for this milestone.
- Validation rerun after review fixes: `cargo fmt --all --check`,
  `cargo test -p engine_web worker --no-fail-fast`, `npm run test:ts`,
  `npm run check:wasm`, `npm run smoke:browser`, `npm test`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: browser smoke proves worker generation is active and not
  synchronous, but it does not yet measure stutter during running movement,
  completion bursts, transfer/copy overhead, or GPU upload spikes. That is
  Milestone 4.

Milestone 4 review, 2026-06-08 / Codex:

- Scope: browser movement-performance smoke and terrain update diagnostics;
  changed Rust/wgpu renderer status, TypeScript renderer status types and fake
  snapshots, browser debug hook snapshot caching, browser smoke report fields,
  the new movement-performance smoke module, generated engine_web WASM
  artifacts, and active docs/contracts.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agent review was skipped because delegated reviewers
  were not explicitly requested by the user.
- Required findings fixed: `std::time::Instant` was replaced with a
  platform-specific timing helper after it panicked on browser wasm; browser
  debug hooks now read a cached once-per-frame Rust snapshot to avoid
  wasm-bindgen recursive borrow failures during smoke polling; and the
  movement sampler was split into `tools/browser-smoke-movement-performance.mjs`
  after inlining it pushed `tools/browser-smoke.mjs` over 1000 lines.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs` remains far
  over the preferred size and should be split before more renderer/status glue
  lands; the movement smoke now records CPU-side terrain update/upload metrics
  but does not separate typed-array transfer copy, Rust completion parsing, and
  GPU buffer creation timings.
- Rejected findings: none.
- Validation rerun after review fixes: `cargo fmt --all --check`,
  `cargo test -p engine_web worker --no-fail-fast`, `npm run test:ts`,
  `npm test`, `npm run check:wasm`, `node --check tools/browser-smoke.mjs`,
  `node --check tools/browser-smoke-movement-performance.mjs`,
  `npm run smoke:browser`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: the smoke thresholds are intentionally generous to avoid
  machine-specific flakes. The latest report is useful baseline evidence, not a
  final performance budget.

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
