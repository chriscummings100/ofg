# Multi-Resolution Terrain View Distance

This completed terrain view-distance plan records the LOD0 through LOD2
migration history. Follow-up distance, benchmarking, and worker-thread work is
complete and archived in
`docs/archived/TERRAIN_PERFORMANCE_THREADING_PLAN_2026-06-08.md`.

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

The goal of this terrain phase is to make the world feel much larger by replacing
the current small same-detail terrain window with a sparse, rootless,
multi-resolution grid. Near the player, terrain should render at the same
resolution the game has today. Farther away, terrain should render as coarser
levels of detail, so distant mountains and landforms remain visible without
requiring thousands of highest-detail chunks.

Success means a player can move through the world and see a long view distance.
Terrain should stream in the right order, never leave holes in the intended
visible cover, and render the correct level of detail for each distance band.
The coarsest loaded level is not a single root node; it is an infinite grid of
coarse nodes around the player. Every level is a grid, and each coarser node can
represent up to eight generated or ungenerated children at the next finer level.

For this plan, `lod = 0` is the current highest-detail terrain. Larger `lod`
values are coarser. A `lod = 1` node covers a 2x2x2 group of `lod = 0` child
nodes, `lod = 2` covers a 2x2x2 group of `lod = 1` child nodes, and so on.
Every generated fine node must have its coarser parent ready first. That
parent-before-child invariant gives the renderer a fallback surface while finer
detail is generated and is the basis for smooth streaming transitions.

## Progress

- [x] (2026-06-07 14:08+01:00) Archived the historical terrain plan to
  `docs/archived/TERRAIN_PLAN_2026-06-07.md` and created this focused
  view-distance ExecPlan.
- [x] (2026-06-07 14:20+01:00) Added concrete API change examples for
  node-keyed LOD identity, scheduling, mesh generation, runtime stream updates,
  renderer IDs, debug snapshots, and architecture verification tests.
- [x] (2026-06-07 14:44+01:00) Milestone 1 complete: introduced
  `TerrainNodeKey`, LOD band stream config, node-keyed density/mesh jobs,
  parent-before-child scheduler ordering, per-LOD status summaries, and LOD0
  compatibility adapters for the fixture facade and current browser stream.
- [x] (2026-06-07 15:12+01:00) Milestone 2 complete: added node mesh generation,
  converted browser stream mesh updates/removals and Rust/wgpu terrain handles
  to `TerrainNodeKey`, added node/LOD debug snapshot fields, preserved LOD0
  chunk compatibility fields, and proved mixed LOD0/LOD1 runtime mesh keys in
  tests.
- [x] (2026-06-07 15:27+01:00) Milestone 3 complete: enabled conservative LOD0,
  LOD1, and LOD2 default bands; split generated mesh caching from renderer
  visibility; selected hole-free parent fallback cover until child groups are
  ready; exposed node keys through the browser debug hook; and made browser
  smoke assert multiple rendered terrain LODs.
- [x] (2026-06-07 16:02+01:00) Milestone 4 complete: added far-view and
  LOD-boundary Rust image smoke scenarios, extended smoke and benchmark reports
  with multi-LOD node counts, verified browser smoke/benchmark/coverage gates,
  and recorded final review evidence.
- [x] (2026-06-08 01:20+01:00) Follow-up distance, benchmarking, and threaded
  generation work completed and moved to
  `docs/archived/TERRAIN_PERFORMANCE_THREADING_PLAN_2026-06-08.md`: the default
  playable stream now includes LOD3/LOD4 far bands, reports a settled visible
  span above 4 km in X and Z, benchmarks realistic terrain-node generation
  populations, and uses browser workers for terrain node builds.

## Surprises & Discoveries

- Observation: the previous `docs/TERRAIN_PLAN.md` had become a combined
  research memo, migration history, progress log, and active plan.
  Evidence: it is archived as `docs/archived/TERRAIN_PLAN_2026-06-07.md`.
- Observation: the current terrain mesh entry point already accepts `cell_size`,
  which gives the first coarse-LOD implementation a simple path: keep 32x32x32
  voxel cells per node, but increase world-space cell size for coarser LODs.
  Evidence: `crates/terrain_core/src/mesh.rs` exposes
  `build_chunk_mesh(seed, preset, coord, cell_size)`.
- Observation: the runtime stream and renderer can process coarser nodes and
  now use them by default through LOD0 through LOD4 bands.
  Evidence:
  `browser_terrain_stream_generates_unique_mesh_keys_across_lods` creates LOD0
  and LOD1 bands, settles the stream, and asserts rendered node keys include
  both LOD0 and LOD1 keys; `browser_terrain_stream_default_bands_render_multiple_lods_after_settling`
  asserts the default stream reaches at least LOD3 and spans at least 4096m in
  X and Z.
- Observation: Milestone 1 made `stream.rs` exceed the review skill's 600-line
  split-pressure threshold.
  Evidence: local milestone review measured `crates/terrain_core/src/stream.rs`
  above 600 lines before extracting public stream types to
  `crates/terrain_core/src/stream_types.rs`; after the split, `stream.rs` is
  533 lines.
- Observation: the standalone fixture facade can remain LOD0-compatible while
  the Rust library scheduler becomes node-keyed.
  Evidence: `ofg_stream_configure` still builds a single LOD0 band, while
  `TerrainStreamJob` and scheduler completion APIs now use `TerrainNodeKey`.
- Observation: Milestone 2 initially left the native Rust smoke harness on the
  old stream update field names.
  Evidence: `npm run test:rust` failed in
  `crates/ofg_test_harness/src/render_smoke/scenarios.rs` on
  `removed_coords` and `mesh_update.coord`; the harness now translates LOD0
  `removed_nodes` and node-keyed upserts for its current chunk coverage checks.
- Observation: Milestone 2 review again put pressure on scheduler file size.
  Evidence: after runtime work, `crates/terrain_core/src/stream.rs` measured
  615 lines; moving pure config validation and priority helpers to
  `crates/terrain_core/src/stream_helpers.rs` reduced it to 570 lines.
- Observation: parent fallback needs cached mesh data separate from renderer
  visibility.
  Evidence: `BrowserTerrainStream` now stores generated non-empty node meshes in
  `mesh_cache` and tracks only renderer-submitted nodes in `visible_nodes`, so a
  parent can be hidden and restored without regenerating the terrain mesh.
- Observation: the native smoke harness can prove multi-LOD visibility without
  weakening the existing boot, preset, and seam coverage checks.
  Evidence: `ScenarioStreamMode::Lod0` keeps legacy coverage scenarios on exact
  LOD0 chunk maps, while the new `ScenarioFilter::Lods` group uses the default
  multi-LOD stream and reports rendered node counts per LOD.
- Observation: the default LOD0/LOD1/LOD2 bands already give a much larger
  visible world span than the old LOD0 window without adding LOD3 yet.
  Evidence: `npm run bench:terrain:rust` wrote
  `artifacts/terrain-bench/run-1780843414-534/report.json`, where the
  `multiLod` probe rendered 104 nodes, reached max rendered LOD 2, and reported
  a 1152m by 1152m visible world span.
- Observation: coverage initially flagged the new stream config/error surface.
  Evidence: the first `npm run coverage:rust` run listed
  `crates/terrain_core/src/stream_types.rs` below the filtered 90% attention
  threshold; `terrain_stream_config_helpers_and_errors_are_stable` now covers
  the config helper and stable diagnostic messages, and the rerun listed no
  files below threshold.

## Decision Log

- Decision: keep the active terrain plan at `docs/TERRAIN_PLAN.md` and archive
  the old long-form plan under `docs/archived/`.
  Rationale: existing repo instructions point terrain work at
  `docs/TERRAIN_PLAN.md`, while the old file remains valuable historical
  context.
  Date/Author: 2026-06-07 / Codex.
- Decision: use `lod = 0` for the current highest-detail terrain and increasing
  integers for coarser terrain.
  Rationale: the existing scheduler, tests, and mesh packet store already use
  LOD0 language. Keeping that convention minimizes churn.
  Date/Author: 2026-06-07 / Codex.
- Decision: model the terrain as a sparse grid at every LOD, not as a rooted
  octree.
  Rationale: the player needs a moving local world window, not a fixed world
  root. A rootless grid preserves infinite-world behavior while still giving
  parent/child relationships.
  Date/Author: 2026-06-07 / Codex.
- Decision: require coarser parent nodes to be ready before scheduling finer
  child mesh generation.
  Rationale: when a child is not ready, the parent can still render the same
  broad terrain volume. This prevents holes during movement and reset.
  Date/Author: 2026-06-07 / Codex.
- Decision: the first visual transition target is hole-free, stable cover with
  bounded popping; aesthetic geomorph fading is future work unless this phase's
  smoke captures prove it is necessary.
  Rationale: cross-LOD Dual Contouring transition meshes are substantial. The
  next useful terrain win is long view distance with reliable coverage, followed
  by transition polish if needed.
  Date/Author: 2026-06-07 / Codex.
- Decision: make the API change reviewable through code-shaped examples in this
  plan before implementation starts.
  Rationale: this phase changes architecture, not just constants. Concrete
  proposed Rust and TypeScript shapes let reviewers verify ownership boundaries,
  naming, and compatibility before code lands.
  Date/Author: 2026-06-07 / User and Codex.
- Decision: keep the default browser terrain LOD bands at LOD0 during Milestone
  2, even though the stream can generate and render coarser nodes under an
  explicit test configuration.
  Rationale: enabling long-distance bands before parent fallback and cross-LOD
  transition policy would make visual behavior harder to reason about. Milestone
  3 owns the default view-distance bands and no-hole selection behavior.
  Date/Author: 2026-06-07 / Codex.
- Decision: Milestone 3 default view distance uses LOD0 radius 1 with vertical
  offsets `[-2, -1, 0, 1]`, LOD1 radius 2 with vertical offsets `[-1, 0, 1]`,
  and LOD2 radius 4 with vertical offset `[0]`.
  Rationale: this expands visible terrain far beyond the old one-chunk LOD0
  radius while keeping synchronous per-frame work and smoke-test wait time
  bounded. Milestone 4 benchmarks can tune these radii or add LOD3 once the
  report includes multi-LOD timing.
  Date/Author: 2026-06-07 / Codex.
- Decision: the first cross-LOD transition strategy is conservative parent
  fallback, not geomorphing or transition meshes.
  Rationale: a parent remains visible until all eight desired children are
  generated or proven empty, so the renderer never removes broad cover before a
  replacement exists. This avoids holes and duplicate overlap in full child
  groups; visual transition polish remains future work if smoke captures show
  unacceptable popping or cracks.
  Date/Author: 2026-06-07 / Codex.
- Decision: keep the default view-distance bands at LOD0/LOD1/LOD2 for this
  phase rather than adding LOD3 immediately.
  Rationale: the benchmark already proves a 1152m by 1152m visible span with
  mixed rendered LODs and no missing nodes in the settled debug summary. LOD3
  and distant-mountain composition can be a follow-up once transition polish and
  streaming budgets are tuned from this baseline.
  Date/Author: 2026-06-07 / Codex.
- Decision: add a dedicated native smoke `Lods` scenario group instead of
  changing the legacy boot, preset, and seam scenarios to multi-LOD mode.
  Rationale: the old scenarios still need precise LOD0 chunk coverage for seam
  and preset regressions. A separate group makes far-view assertions explicit
  and keeps the existing checks stable.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

Milestone 1 landed the terrain-core architecture foundation only. The playable
browser terrain is intentionally still configured as LOD0, so user-visible view
distance has not changed yet. Remaining gaps are Milestone 2 runtime node-keyed
mesh upload/rendering, Milestone 3 visible-set and transition behavior, and
Milestone 4 smoke/benchmark/coverage acceptance.

Milestone 2 landed the runtime node-keyed mesh path. `terrain_core` now exposes
`build_node_mesh`, `engine_web` streams mesh upserts/removals by
`TerrainNodeKey`, Rust/wgpu mesh and object IDs use stable node strings, and
debug snapshots expose node keys plus per-LOD summaries while preserving legacy
LOD0 chunk fields. At that point, user-visible view distance was still unchanged
because default bands and parent fallback belonged to Milestone 3.

Milestone 3 made view distance user-visible in the default browser runtime. The
stream now generates LOD0/LOD1/LOD2 bands, caches generated meshes, submits only
the selected visible cover to the renderer, and keeps parent cover visible until
desired children are ready or empty. Browser smoke now waits for and asserts a
Rust-reported multi-LOD terrain frame.

Milestone 4 completed the validation layer for this view-distance slice. Native
Rust image smoke now captures far-view and LOD-boundary scenes with rendered
LOD0/LOD1/LOD2 node counts in the report, the Rust benchmark records multi-LOD
stream counts, mesh timings by LOD, and visible world span, browser smoke still
passes against the Rust-owned multi-LOD debug snapshot, and Rust coverage no
longer lists modified implementation files below the filtered attention
threshold. Remaining work is future terrain polish, not required acceptance for
this ExecPlan: add LOD3+ distant mountain composition when budgeted, and replace
bounded parent/child popping with geomorphing, skirts, or transition meshes if
visual review demands it.

## Contract and Quality Baseline

This plan preserves these active contracts:

- `OFG-API-001`: browser code continues to use `RustBrowserGame.create`,
  `resize`, `tick`, `command`, and `debugSnapshot`. New terrain debug data must
  be exposed through `debugSnapshot()` rather than new TypeScript terrain owners.
- `OFG-API-003`: browser debug hooks may expose Rust-assembled terrain LOD
  status, but must not compute desired terrain sets, visibility, generation, or
  renderer state in TypeScript.
- `OFG-API-004`: terrain vertex layout remains 19 `f32` values per vertex unless
  a milestone explicitly updates every Rust and WGSL layout site plus shader
  tests.
- `OFG-API-006`: the standalone `terrain_core.wasm` artifact remains fixture
  only. Runtime terrain scheduling, density, meshing, and rendering stay inside
  Rust and `engine_web`.
- `OFG-API-009`: TypeScript must not regain terrain generation, density
  sampling, stream scheduling, mesh generation, WebGPU resource ownership, or
  terrain render submission.

If a milestone changes public debug fields, update `docs/API_CONTRACTS.md` in
the same milestone. If a milestone changes the terrain architecture, update
`docs/ARCHITECTURE.md` before marking the milestone complete.

Every implementation milestone must satisfy the default Rust coverage attention
gate for modified implementation files. Run `npm run coverage:rust` before the
plan is complete and confirm changed implementation files do not appear in the
default filtered output, or record an explicit exception here with rationale.

## Context and Orientation

Current terrain is Rust-owned and same-LOD. `crates/terrain_core/src/stream.rs`
contains `TerrainStreamScheduler`, which currently builds one desired LOD0 render
set plus a wider density dependency set. It submits density jobs first, then
LOD0 jobs only after the 2x2x2 positive-apron density dependencies are ready.

`crates/engine_web/src/terrain_stream.rs` owns the playable browser stream inside
Rust. It creates the scheduler with a one-chunk horizontal radius and vertical
offsets `[-2, -1, 0, 1]`, executes jobs synchronously in `tick`, calls
`build_chunk_mesh` for LOD0 meshes, and returns mesh upserts/removals to the
Rust/wgpu renderer.

`crates/terrain_core/src/chunk.rs` defines `TerrainChunkCoord`,
`terrain_chunk_origin`, `terrain_chunk_coord_containing_position`, and
`terrain_chunk_key`. These are currently LOD0-oriented names, but the coordinate
math already accepts a `cell_size`, so the same coordinate type can be reused
inside an explicit `TerrainNodeKey`.

`crates/terrain_core/src/mesh.rs` builds a 32x32x32-cell Dual Contouring mesh
with a 2x2x2 neighbor density apron and same-LOD seam ownership. It takes
`cell_size`, so `lod = n` can initially use
`cell_size = base_cell_size * 2^n` while keeping the same sample count per node.

`crates/ofg_test_harness/src/render_smoke/scenarios.rs` builds deterministic
terrain meshes with `BrowserTerrainStream` for native Rust image smoke. This is
the right place to add long-view and LOD-boundary image scenarios because terrain
visual verification belongs in Rust smoke rather than browser-side terrain
clients.

Important definitions for this plan:

- A terrain node is one generated or empty chunk at a specific LOD and 3D grid
  coordinate.
- A parent node is the next coarser node, with `lod + 1` and coordinate
  `floor_div(child_coord, 2)` on each axis.
- A child group is the up-to-eight finer nodes covered by one parent.
- The loaded set is every node whose density or mesh state is retained.
- The visible set is the subset of ready non-empty nodes submitted to the
  renderer this frame.
- A missing node is desired but not generated.
- An empty node has generated density/mesh work and proven it has no renderable
  surface.
- A transition boundary is any face where adjacent visible terrain is at
  different LODs.

## Plan of Work

Milestone 1 introduces the multi-resolution node model without trying to solve
all rendering at once. Add `TerrainNodeKey { lod, coord }` in
`crates/terrain_core`, plus helpers for node key strings, cell size per LOD,
world bounds, parent keys, child keys, and floor division for negative
coordinates. Replace scheduler internals that assume `desired_lod0` with
node-keyed desired mesh sets. The scheduler must build a rootless set of desired
nodes from configurable LOD bands, schedule coarser parents before finer
children, preserve density-before-mesh ordering per node, reject stale
completions after reset, and expose per-LOD status counts. Existing LOD0 tests
must keep passing, and new tests must prove parent/child mapping, no-root desired
sets, parent-before-child scheduling, empty parent handling, and pruning.

Milestone 2 makes the runtime render real coarser nodes. Add a mesh entry point
that takes `TerrainNodeKey` and base cell size, derives the effective cell size
from `lod`, and reuses the current 32x32x32 density and Dual Contouring path.
Update `BrowserTerrainStream` so mesh state, removal, upsert, status, and debug
keys are node-keyed rather than chunk-only. Keep existing `terrainChunkKeys`
debug output compatible for LOD0, and add explicit node-level fields such as
`loadedTerrainNodeKeys`, `terrainNodeKeys`, and `terrainLodSummary`. Update the
Rust/wgpu terrain mesh handle map to use stable node IDs, for example
`terrain:lod:x,y,z`, so LOD0 and coarser nodes cannot collide. Native tests must
prove the stream can upload and prune both LOD0 and coarser meshes.

Milestone 3 implements the actual view-distance behavior. Define conservative
LOD bands in `engine_web`, such as near LOD0, mid LOD1, far LOD2, and distant
LOD3 or LOD4, with hysteresis so walking across a chunk boundary does not churn
the whole visible set. The scheduler should keep parent cover rendered until a
child group is ready enough to replace it, and should fall back to the parent if
children are missing, empty, or pruned. Add cross-LOD transition safety at
visible boundaries. The first acceptable implementation is a Rust-owned,
tested no-hole strategy: keep parent cover alive while children arrive, avoid
rendering duplicate overlapping regions where possible, and add transition
closure geometry or skirts where mixed-LOD screenshots show cracks. Record the
chosen seam strategy in the Decision Log after the implementation proves what is
needed.

Milestone 4 validates and tunes. Extend Rust image smoke with a far-view
scenario and an LOD-boundary scenario that records rendered LOD counts, total
node counts, vertex/index counts, and screenshot paths. Extend the Rust terrain
benchmark so it reports multi-LOD desired node counts, prepared density counts,
mesh timings by LOD, and total visible world span. Browser smoke should continue
to validate startup, reload, input, WebGPU rendering, and Rust debug snapshots,
and should assert that the terrain debug snapshot reports at least two LODs
after the stream settles. Update active docs and run `milestone-review` before
marking each milestone complete.

## Concrete Steps

Work from `C:\dev\ofg-terrain`.

1. Before implementation, inspect the current diff and tests:

       git -c safe.directory=C:/dev/ofg-terrain status --short
       npm run test:rust

2. For Milestone 1, edit:

       crates/terrain_core/src/chunk.rs
       crates/terrain_core/src/stream.rs
       crates/terrain_core/src/facade.rs, only if fixture stream buffers need node LOD fields
       crates/terrain_core/src/tests.rs

   Expected new behavior: scheduler tests show desired nodes across multiple
   LODs, parent nodes are scheduled before child nodes, and reset/prune behavior
   still rejects stale completions.

3. For Milestone 2, edit:

       crates/terrain_core/src/mesh.rs
       crates/terrain_core/src/density.rs, only if density-store keys need node-aware helpers
       crates/engine_web/src/terrain_stream.rs
       crates/engine_web/src/wgpu_renderer.rs
       crates/engine_web/src/tests.rs

   Expected new behavior: `BrowserTerrainStream` can produce at least one LOD0
   mesh and one coarser mesh in a deterministic test, and renderer mesh handles
   remain stable and unique by LOD node key.

4. For Milestone 3, edit:

       crates/engine_web/src/terrain_stream.rs
       crates/terrain_core/src/stream.rs
       crates/terrain_core/src/mesh.rs, if transition geometry is needed
       crates/terrain_core/src/tests.rs
       crates/engine_web/src/tests.rs

   Expected new behavior: moving the stream center keeps a complete visible
   cover, parent nodes remain available until child nodes are ready, and mixed
   LOD boundaries do not create visible holes in targeted Rust smoke captures.

5. For Milestone 4, edit:

       crates/ofg_test_harness/src/render_smoke/scenarios.rs
       crates/ofg_test_harness/src/render_smoke/report.rs
       crates/ofg_test_harness/src/terrain_bench.rs
       tools/browser-smoke.mjs, only for debug snapshot assertions
       docs/API_CONTRACTS.md, if debug contract fields change
       docs/ARCHITECTURE.md
       docs/TERRAIN_PLAN.md

   Expected new behavior: reports include multi-LOD node counts, the Rust smoke
   screenshots show far terrain, browser smoke observes multiple Rust-owned LODs,
   and this ExecPlan records final outcomes.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective.
2. Update changed active docs or API contracts.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands.
6. Record commands, artifact paths, and remaining risks in this plan.

## Validation and Acceptance

The plan is accepted only when these observable behaviors are true:

- The terrain stream debug snapshot reports multiple LODs loaded and rendered
  after settling near the default spawn.
- The visible terrain cover extends far beyond the old one-chunk horizontal
  LOD0 radius while keeping near terrain at current detail.
- Moving the player across multiple chunk centers does not produce holes in the
  intended terrain cover.
- Coarser nodes are generated before finer child nodes, and tests prove stale
  child completions after reset are rejected.
- Empty nodes are tracked and do not repeatedly regenerate every tick.
- Renderer mesh/object keys are unique across LODs and stale nodes are pruned.
- Rust image smoke includes a far-view capture and an LOD-boundary capture with
  nonblank terrain pixels and multiple rendered LOD counts in the report.
- Browser smoke still passes, including reload, input forwarding, WebGPU canvas
  rendering, Rust runtime sentinels, and terrain debug status.

Run these commands before completing the plan:

    npm run test:rust
    npm run test:ts
    npm test
    npm run bench:terrain:rust
    npm run smoke:rust
    npm run smoke:terrain-seams
    npm run smoke:browser
    npm run coverage:rust
    git diff --check

If shader code or terrain vertex layout changes, also run:

    npm run check:shaders

If WASM fixture exports, generated bindings, or build artifacts change, also
run:

    npm run check:wasm
    npm run build:wasm

For coverage, the default filtered `npm run coverage:rust` output must not list
modified implementation files. If a file remains below the attention threshold,
record the exception and rationale in Outcomes & Retrospective before stopping.

## Idempotence and Recovery

The implementation should remain restartable. Stream resets must bump
generations so stale density and mesh work is ignored. Changing LOD band
settings should clear or reconcile loaded node sets without reusing incompatible
mesh handles. Generated terrain remains deterministic from seed, preset, node
key, and cell size, so failed jobs can be retried.

If a milestone destabilizes rendering, set the runtime LOD config back to a
single LOD0 band while keeping the tested node-key helpers. That rollback path
should preserve the current playable behavior while allowing the LOD scheduler
work to be debugged separately.

Do not delete the archived historical plan. Do not restore TypeScript terrain
workers, terrain schedulers, mesh builders, or WebGPU terrain ownership as a
fallback.

## Artifacts and Notes

Historical terrain context is archived at
`docs/archived/TERRAIN_PLAN_2026-06-07.md`.

Expected artifact locations during implementation:

- Rust terrain benchmark reports under `artifacts/terrain-bench/`.
- Rust image smoke screenshots and reports under `artifacts/rust-smoke/`.
- Browser smoke screenshots and reports under `artifacts/browser-smoke/`.
- Rust coverage summaries under `artifacts/coverage/rust/`.

When milestones complete, paste concise evidence here: command names, pass/fail
summary, relevant report paths, and any important timing or screenshot notes.

Milestone 1 evidence:

- `cargo test -p terrain_core`: passed before widening validation.
- `cargo test -p engine_web`: passed before widening validation.
- `npm run test:rust`: passed after the node-keyed scheduler, type split,
  architecture doc update, and warning cleanup.
- `git -c safe.directory=C:/dev/ofg-terrain diff --check`: passed.

Milestone review:

- Scope: Milestone 1 node-keyed terrain stream scheduler in `terrain_core`,
  LOD0 compatibility adapters in `terrain_core` facade and `engine_web`
  stream setup, active architecture doc update, and this ExecPlan.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were discoverable, but the tool rules
  allow spawning only when the user explicitly requests delegated/parallel
  agents, so no sub-agents were spawned.
- Required findings fixed: split `crates/terrain_core/src/stream.rs` by moving
  public stream types to `crates/terrain_core/src/stream_types.rs`; updated
  `docs/ARCHITECTURE.md` so active docs no longer describe the scheduler as
  LOD0-only; hardened and documented `crates/terrain_core/src/node.rs` helpers;
  removed an unused re-export warning and reran validation.
- Follow-ups recorded: none for Milestone 1.
- Rejected findings: no rejected findings.
- Remaining risk at the time: the runtime still rendered only LOD0 because
  mesh updates, renderer keys, and debug snapshots had not yet been converted
  to `TerrainNodeKey`. Milestones 2 and 3 have since addressed this.

Milestone 2 evidence:

- `cargo test -p terrain_core`: passed after adding `build_node_mesh` and the
  coarse-cell-size equivalence test.
- `cargo test -p engine_web`: passed after converting browser stream updates to
  `TerrainNodeKey` and adding
  `browser_terrain_stream_generates_unique_mesh_keys_across_lods`.
- `npm run test:ts`: passed after installing locked npm dependencies with
  `npm ci`; this command rebuilt wasm artifacts, compiled app/test TypeScript,
  and ran 62 mocha tests.
- `npm run test:rust`: passed after updating the Rust smoke harness to consume
  node-keyed stream updates for its current LOD0 coverage map.
- `npm run check:wasm`: passed after generated wasm artifacts were refreshed.
- `git -c safe.directory=C:/dev/ofg-terrain diff --check`: passed.

Milestone 2 review:

- Scope: node mesh generation in `terrain_core`; node-keyed
  `BrowserTerrainStream` updates/removals/status; Rust/wgpu node-keyed terrain
  mesh handles, object IDs, and debug snapshot fields; TypeScript debug types
  and adapter fixture updates; Rust smoke harness compatibility; active API and
  architecture docs; generated wasm artifacts.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because this milestone
  review was plan-required rather than an explicit user request for delegated
  reviewers.
- Required findings fixed: updated `docs/API_CONTRACTS.md` for
  `loadedTerrainNodeKeys`, `terrainNodeKeys`, and `terrainLodSummary`; updated
  `docs/ARCHITECTURE.md` so runtime mesh identity is no longer described as
  chunk-keyed; updated the Rust smoke harness off the removed `removed_coords`
  and `mesh_update.coord` fields; split pure stream helpers into
  `crates/terrain_core/src/stream_helpers.rs`, reducing `stream.rs` from 615 to
  570 lines; reran validation.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs` remains a
  pre-existing oversized wasm facade at 2439 lines. Milestone 2 kept renderer
  edits scoped to terrain node IDs and debug conversion; a future renderer
  decomposition should be planned before broadening that file again.
- Rejected findings: no rejected findings.
- Remaining risk at the time: default gameplay still rendered only LOD0.
  Milestone 3 has since enabled distance bands and parent fallback cover.

Milestone 3 evidence:

- `cargo test -p terrain_core`: passed after adding the scheduler
  `mesh_generated` cover query.
- `cargo test -p engine_web`: passed after adding
  `browser_terrain_stream_keeps_parent_visible_until_children_are_ready` and
  `browser_terrain_stream_default_bands_render_multiple_lods_after_settling`.
- `npm run test:ts`: passed after exposing terrain node-key debug hooks in
  `src/app/game.ts` and rebuilding wasm artifacts.
- `npm run smoke:browser`: passed and wrote screenshots/report under
  `artifacts/browser-smoke/2026-06-07T14-23-52-877Z`; browser smoke now waits
  for `terrainStreamStatus.maxRenderedLod >= 1` and mixed `terrainNodeKeys`.
- `npm run smoke:rust`: passed and wrote screenshots/report under
  `artifacts/rust-smoke/run-1780842288-292`; current native smoke scenarios use
  explicit LOD0 bands until Milestone 4 adds far-view and LOD-boundary image
  scenarios.
- `npm run test:rust`: passed after all Milestone 3 edits.
- `npm run check:wasm`: passed after wasm artifacts were refreshed.
- `git -c safe.directory=C:/dev/ofg-terrain diff --check`: passed.

Milestone 3 review:

- Scope: default multi-LOD bands in `BrowserTerrainStream`; generated mesh cache
  plus visible renderer set; parent fallback selection; browser debug hook node
  keys; browser smoke multi-LOD assertions; active architecture and this
  ExecPlan.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because this milestone
  review was plan-required rather than an explicit user request for delegated
  reviewers.
- Required findings fixed: updated `docs/ARCHITECTURE.md` so it no longer says
  the default playable stream remains LOD0-only; changed
  `Option::is_none_or` to an older-stable-compatible `match`; kept
  `crates/engine_web/src/terrain_stream.rs`, `tools/browser-smoke.mjs`, and
  `crates/terrain_core/src/stream.rs` under the 600-line split-pressure
  threshold; reran validation.
- Follow-ups recorded: the Rust smoke harness still uses explicit LOD0 stream
  bands for its existing boot/preset/seam scenarios. Milestone 4 must add
  dedicated far-view and LOD-boundary native smoke scenarios instead of
  overloading those legacy coverage checks.
- Rejected findings: no rejected findings.
- Remaining risk: parent fallback is hole-safe but visually conservative. It
  can pop when a full child group replaces a parent, and it does not yet add
  geomorphing, skirts, or transition meshes for mixed-LOD boundaries.

Milestone 4 evidence:

- `cargo test -p ofg_test_harness render_smoke`: passed after adding the
  `Lods` scenario group and splitting smoke scenario tests to
  `crates/ofg_test_harness/src/render_smoke/scenarios_tests.rs`.
- `cargo test -p ofg_test_harness terrain_bench`: passed after adding
  `crates/ofg_test_harness/src/terrain_bench_lod.rs` and the multi-LOD report
  fields.
- `npm run smoke:rust`: passed and wrote screenshots/report under
  `artifacts/rust-smoke/run-1780843124-199`. `far-view-multi-lod` rendered
  103 nodes with max LOD 2 and LOD counts `LOD0=4, LOD1=15, LOD2=84`;
  `lod-boundary-oblique` rendered 101 nodes with max LOD 2 and LOD counts
  `LOD0=4, LOD1=15, LOD2=82`. Both images had nonblank pixel diversity
  (`uniqueColorBuckets` 57 and 80 respectively).
- `npm run bench:terrain:rust`: passed and wrote
  `artifacts/terrain-bench/run-1780843414-534/report.json`. The `multiLod`
  probe settled in 107 stream ticks, rendered 104 nodes, reached max LOD 2,
  and reported a 1152m by 1152m visible world span.
- `npm run smoke:terrain-seams`: passed and wrote screenshots/report under
  `artifacts/rust-smoke/run-1780843478-661`.
- `npm run smoke:terrain-presets`: passed and wrote screenshots/report under
  `artifacts/rust-smoke/run-1780843478-783`.
- `cargo test -p terrain_core`: passed after adding coverage for
  `TerrainStreamConfig` helpers and `TerrainStreamError` messages.
- `npm run coverage:rust`: passed after the coverage fix; the filtered output
  listed no implementation files below the 90% attention threshold and wrote
  summaries under `artifacts/coverage/rust/`.
- `npm test`: passed after all Milestone 4 edits; this ran `npm run test:rust`
  and `npm run test:ts`, rebuilt generated shader/WASM artifacts, and ran the
  62 TypeScript tests.
- `npm run smoke:browser`: passed at the end of validation and wrote
  screenshots/report under
  `artifacts/browser-smoke/2026-06-07T14-59-42-538Z`.
- `npm run check:wasm`: passed after final browser smoke rebuilt the WASM
  artifacts.
- `git -c safe.directory=C:/dev/ofg-terrain diff --check`: passed; Git emitted
  Windows line-ending warnings only.

Milestone 4 review:

- Scope: native Rust far-view and LOD-boundary smoke scenarios; smoke report
  multi-LOD debug fields; Rust terrain benchmark multi-LOD probe/report fields;
  coverage for new stream types; browser smoke final validation; active docs
  and this ExecPlan.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally using the repo-local `milestone-review` instructions.
  Sub-agent tools were not used because this review was plan-required rather
  than an explicit user request for delegated reviewers.
- Required findings fixed: split smoke scenario tests out of
  `crates/ofg_test_harness/src/render_smoke/scenarios.rs`, keeping the scenario
  implementation under the 600-line split-pressure threshold; extracted the
  multi-LOD benchmark probe to `crates/ofg_test_harness/src/terrain_bench_lod.rs`
  instead of growing `terrain_bench.rs` past 1000 lines; added
  `terrain_stream_config_helpers_and_errors_are_stable` after coverage initially
  flagged `stream_types.rs`; reran validation.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs`,
  `crates/engine_web/src/tests.rs`, `crates/terrain_core/src/tests.rs`, and
  `crates/terrain_core/src/facade.rs` remain pre-existing oversized files. Do
  not broaden those files further without a split plan; future benchmark growth
  should also continue moving focused probes out of
  `crates/ofg_test_harness/src/terrain_bench.rs`.
- Rejected findings: no rejected findings.
- Remaining risk: the current transition behavior is hole-free but can still
  pop when a complete child group replaces a parent. The default far view uses
  LOD0/LOD1/LOD2; LOD3+ distant mountain composition and visual transition
  polish remain future terrain phases.

## Interfaces and Dependencies

This phase should make one architectural shift visible in the API: terrain is
addressed as LOD nodes, not plain chunks. The current chunk is simply `lod = 0`.
The concrete names may adjust during implementation, but new code should keep
this shape unless the Decision Log records a better reason.

Core node identity should live in `crates/terrain_core`, either in
`src/chunk.rs` or a focused `src/node.rs` module:

    #[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
    pub struct TerrainNodeKey {
        pub lod: u8,
        pub coord: TerrainChunkCoord,
    }

    pub fn terrain_node_key(key: TerrainNodeKey) -> String {
        format!(
            "lod{}:{},{},{}",
            key.lod, key.coord.x, key.coord.y, key.coord.z
        )
    }

    pub fn terrain_node_cell_size(base_cell_size: f64, lod: u8) -> f64 {
        base_cell_size * 2_f64.powi(i32::from(lod))
    }

    pub fn terrain_node_parent(key: TerrainNodeKey) -> Option<TerrainNodeKey> {
        if key.lod == u8::MAX {
            return None;
        }

        Some(TerrainNodeKey {
            lod: key.lod + 1,
            coord: TerrainChunkCoord {
                x: key.coord.x.div_euclid(2),
                y: key.coord.y.div_euclid(2),
                z: key.coord.z.div_euclid(2),
            },
        })
    }

    pub fn terrain_node_children(parent: TerrainNodeKey) -> Option<[TerrainNodeKey; 8]> {
        if parent.lod == 0 {
            return None;
        }

        let lod = parent.lod - 1;
        let base_x = parent.coord.x * 2;
        let base_y = parent.coord.y * 2;
        let base_z = parent.coord.z * 2;

        Some([
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x,     y: base_y,     z: base_z } },
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x + 1, y: base_y,     z: base_z } },
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x,     y: base_y + 1, z: base_z } },
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x + 1, y: base_y + 1, z: base_z } },
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x,     y: base_y,     z: base_z + 1 } },
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x + 1, y: base_y,     z: base_z + 1 } },
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x,     y: base_y + 1, z: base_z + 1 } },
            TerrainNodeKey { lod, coord: TerrainChunkCoord { x: base_x + 1, y: base_y + 1, z: base_z + 1 } },
        ])
    }

The stream configuration should become band-based instead of one
horizontal-radius setting:

    #[derive(Clone, Debug, Eq, PartialEq)]
    pub struct TerrainLodBand {
        pub lod: u8,
        pub horizontal_radius: i32,
        pub vertical_chunk_offsets: Vec<i32>,
    }

    #[derive(Clone, Debug, Eq, PartialEq)]
    pub struct TerrainStreamConfig {
        pub lod_bands: Vec<TerrainLodBand>,
        pub max_in_flight_jobs: usize,
    }

Milestone 3 should enable conservative default bands near
`crates/engine_web/src/terrain_stream.rs`. This is illustrative; tune the exact
radii after benchmark and smoke evidence. Milestone 2 intentionally keeps the
default runtime at a single LOD0 band while tests exercise mixed LODs:

    fn default_terrain_lod_bands() -> Vec<TerrainLodBand> {
        vec![
            TerrainLodBand {
                lod: 0,
                horizontal_radius: 1,
                vertical_chunk_offsets: vec![-2, -1, 0, 1],
            },
            TerrainLodBand {
                lod: 1,
                horizontal_radius: 3,
                vertical_chunk_offsets: vec![-1, 0, 1],
            },
            TerrainLodBand {
                lod: 2,
                horizontal_radius: 6,
                vertical_chunk_offsets: vec![0],
            },
            TerrainLodBand {
                lod: 3,
                horizontal_radius: 10,
                vertical_chunk_offsets: vec![0],
            },
        ]
    }

Scheduler jobs should become node-keyed:

    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    pub enum TerrainStreamJob {
        Density {
            generation: u64,
            key: TerrainNodeKey,
        },
        Mesh {
            generation: u64,
            key: TerrainNodeKey,
        },
    }

    impl TerrainStreamScheduler {
        pub fn complete_density(&mut self, generation: u64, key: TerrainNodeKey) -> bool;
        pub fn fail_density(&mut self, generation: u64, key: TerrainNodeKey) -> bool;

        pub fn complete_mesh(
            &mut self,
            generation: u64,
            key: TerrainNodeKey,
            empty: bool,
        ) -> bool;

        pub fn visible_nodes(&self) -> Vec<TerrainNodeKey>;
    }

The first coarse-mesh API should reuse the current chunk mesher by deriving
effective cell size from LOD:

    pub fn build_node_mesh(
        seed: u32,
        preset: u32,
        key: TerrainNodeKey,
        base_cell_size: f64,
    ) -> MeshData {
        build_chunk_mesh(
            seed,
            preset,
            key.coord,
            terrain_node_cell_size(base_cell_size, key.lod),
        )
    }

`BrowserTerrainStream` should pass node-keyed runtime updates to the Rust/wgpu
renderer:

    pub struct BrowserTerrainMeshUpdate {
        pub key: TerrainNodeKey,
        pub mesh: MeshData,
    }

    #[derive(Default)]
    pub struct BrowserTerrainStreamUpdate {
        pub removed_nodes: Vec<TerrainNodeKey>,
        pub upserted_meshes: Vec<BrowserTerrainMeshUpdate>,
    }

Renderer object IDs and mesh-handle maps must include the LOD so coarser and
finer nodes at the same coordinate cannot collide:

    fn terrain_node_object_id(key: TerrainNodeKey) -> String {
        terrain_node_key(key)
    }

`BrowserTerrainStreamStatus` should keep existing LOD0-compatible counts where
browser smoke relies on them, and add node/LOD-specific status instead of
changing TypeScript into a terrain client. The renderer should receive only
Rust-owned mesh updates and removals keyed by stable terrain node IDs.

The TypeScript debug type should expand only as a Rust-assembled snapshot. It
must not compute desired terrain sets, visibility, LOD selection, density
dependencies, or renderer state:

    export type TerrainNodeKey = string;

    export type TerrainLodSummary = {
      readonly lod: number;
      readonly desiredNodeCount: number;
      readonly densityReadyNodeCount: number;
      readonly renderedNodeCount: number;
      readonly emptyNodeCount: number;
      readonly missingNodeCount: number;
    };

    export type TerrainStreamStatus = {
      readonly generation: number;
      readonly pending: boolean;

      // Existing compatibility fields may stay for HUD and smoke tests.
      readonly loadedChunkCount: number;
      readonly desiredRenderChunkCount: number;
      readonly renderedChunkCount: number;

      // New architecture counts and summaries.
      readonly loadedNodeCount: number;
      readonly desiredRenderNodeCount: number;
      readonly renderedNodeCount: number;
      readonly emptyNodeCount: number;
      readonly missingNodeCount: number;
      readonly maxRenderedLod: number;
      readonly terrainLodSummary: readonly TerrainLodSummary[];
      readonly workerPoolRuntime: "rust";
    };

    export type RustBrowserGameDebugSnapshot = {
      readonly loadedTerrainChunkKeys: readonly string[];
      readonly loadedTerrainNodeKeys: readonly string[];
      readonly terrainChunkKeys: readonly string[];
      readonly terrainNodeKeys: readonly string[];
      readonly terrainStreamStatus: TerrainStreamStatus;
    };

Architecture tests should include these behavior-focused cases before or with
the implementation:

    #[test]
    fn terrain_node_parent_maps_negative_coords_with_floor_division() {}

    #[test]
    fn stream_scheduler_builds_rootless_lod_bands_without_a_root_node() {}

    #[test]
    fn stream_scheduler_schedules_parent_mesh_before_child_mesh() {}

    #[test]
    fn browser_terrain_stream_generates_unique_mesh_keys_across_lods() {}

    #[test]
    fn browser_terrain_stream_keeps_parent_visible_until_children_are_ready() {}
