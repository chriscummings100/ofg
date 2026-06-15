# Terrain Rebuild From Reference

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

The terrain system has accumulated useful behavior and useful mistakes. This
plan preserves the current implementation as reference material, then rebuilds
the active terrain model as a lean Rust-owned system around the exact rules the
project has narrowed down:

Terrain is an infinite grid of chunked octree nodes. `lod = 0` is the highest
detail level. Larger LOD numbers are coarser. A node's parent is the next
coarser node at `lod + 1`, and each parent covers a 2x2x2 group of children at
`lod - 1`. The current coarsest playable level is `lod = 5`, which forms an
infinite world grid instead of a single root. Every generated node is a whole
chunk job; jobs are never split below one node.

Each node is sampled from a signed density field. The highest-detail `lod = 0`
node spans 32x32x32 meters, contains 32 cells per axis, and samples 33x33x33
vertices so neighbor nodes share boundary samples. Coarser LODs keep 32 cells
per axis and double world cell size per LOD. The first rebuilt generator keeps
the current no-overhang rule: each XZ column finds a highest solid Y and treats
all lower samples as solid. Broad shape comes from large-feature simplex-style
noise, ridge noise, domain warp noise, and cell noise; local surface variation
comes from 3D detail noise. Material choice remains small and terrain-owned,
based on altitude and gradient until a later biome/material layer exists.

Streaming must be hole-free and cheap on the main thread. A child group can
replace its parent only when all eight children are generated or proven empty.
For a target detail level, desired child nodes are derived from a 3x3x3 grid of
parent nodes around the player. Introducing finer LODs proceeds one level at a
time from `lod5` toward `lod0`; the active stream does not skip levels. When a
new LOD replaces an older one, the visual transition should use a dissolve in
which complementary random screen-space or world-space masks discard pixels
from the outgoing and incoming LOD. Nodes participating in a transition cannot
be removed until the dissolve completes.

Success means the browser-visible terrain still works, but its implementation is
smaller, more explicit, and easier to test. Smoke tests should prove a settled
stream has no holes, parent/child swaps are one-frame visible-set flips after
generation, dissolve transitions retain both sides until completion, and terrain
generation remains within the target budget of under 30ms per generated node on
a worker, with main-thread transition work close to a render-bit toggle.

## Progress

- [x] (2026-06-15 21:53+01:00) Read `PLANS.md`,
  `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and
  `docs/TERRAIN_PLAN.md` before drafting this rebuild plan.
- [x] (2026-06-15 21:53+01:00) Researched Rust/WASM job library options and
  recorded the first decision point for `rayon` plus `wasm-bindgen-rayon`.
- [x] (2026-06-15 21:58+01:00) Preserved the current terrain implementation in
  `docs/reference/terrain_legacy_2026_06_15/`, added a README explaining that
  it is reference-only, committed the full dirty worktree as baseline
  `7aaf3cf`, and pushed it to `origin/main`.
- [x] (2026-06-15 22:04+01:00) Milestone 1: added a small tested Rust terrain
  specification model in `crates/terrain_core/src/rebuild/mod.rs` for LOD
  identity, parent/child relationships, node sizing, desired child sets, and
  hole-free replacement readiness.
- [x] (2026-06-15 22:04+01:00) Ran the repo-local `milestone-review` skill for
  Milestone 1 locally, fixed the required `TerrainLod` vocabulary alignment
  finding, and reran validation.
- [ ] Milestone 2: introduce the rebuilt generator contract and terrain node
  output packet, still callable synchronously for Rust tests.
- [ ] Milestone 3: introduce the rebuilt stream state machine with one-job-per
  node scheduling, parent-retained fallback cover, and dissolve transition
  ownership.
- [ ] Milestone 4: connect the rebuilt stream to browser worker execution or
  the chosen Rust/WASM thread-pool path without giving TypeScript terrain
  ownership.
- [ ] Milestone 5: replace the active renderer/debug integration, retire the
  legacy active modules, and keep only the reference snapshot.
- [ ] Milestone 6: add and pass Rust image smoke, browser smoke, benchmark, and
  coverage gates for the rebuilt terrain path.

## Surprises & Discoveries

- Observation: the worktree already contains many modified terrain, renderer,
  generated artifact, app, and docs files.
  Evidence: `git -c safe.directory=C:/dev/ofg status --short` listed modified
  files under `crates/terrain_core`, `crates/engine_web`, `src/app`,
  `src/engine/web`, `src/generated`, `assets/wasm`, and multiple docs. This
  plan must preserve those edits rather than overwrite them casually.

- Observation: `wasm-bindgen-rayon` is the best-matching current crate for
  using a Rust thread-pool API while executing with browser workers and
  `SharedArrayBuffer`.
  Evidence: docs.rs describes `wasm-bindgen-rayon` as a Rayon adapter for the
  Web using `wasm-bindgen`, Web Workers, and `SharedArrayBuffer`, and exposes an
  async `initThreadPool` function after wasm-bindgen generation.

- Observation: the Rayon-on-WASM path is not a free drop-in replacement for the
  current browser worker adapter.
  Evidence: the wasm-bindgen threading guide says threaded Rust WebAssembly
  requires atomics-related target features and rebuilding the standard library
  with nightly `-Z build-std`; it also warns that the browser main thread cannot
  block. The terrain stream still needs an async completion model even if node
  execution uses Rayon internally.

- Observation: the first rebuild slice can be validated without touching the
  active runtime path.
  Evidence: `cargo test -p terrain_core rebuild` passed 7 focused tests, and
  `npm run test:rust` passed the Rust workspace after adding
  `terrain_core::rebuild`.

## Decision Log

- Decision: preserve the current terrain implementation by copying it into a
  reference-only folder before deleting or replacing active modules.
  Rationale: the user asked to shift existing terrain code aside for reference,
  and the dirty worktree means a non-destructive reference snapshot is safer
  than immediately moving user-modified files out from under the build.
  Date/Author: 2026-06-15 / Codex.

- Decision: make `lod = 0` the highest detail level and increasing LOD numbers
  coarser, with `lod = 5` as the first rebuilt coarsest playable grid.
  Rationale: this matches the user's clarified terminology and the existing
  debug language, while avoiding a rooted octree that would not fit an infinite
  world.
  Date/Author: 2026-06-15 / User and Codex.

- Decision: every generation job builds one complete terrain node at one LOD.
  Rationale: smaller job fragments complicate cancellation, completion
  validation, transition readiness, and renderer toggles without matching the
  desired streaming model.
  Date/Author: 2026-06-15 / User and Codex.

- Decision: desired nodes for a finer LOD are derived from the 3x3x3 parent LOD
  region around the player.
  Rationale: a parent can be visually replaced only by a complete or empty
  child group, so the child desired set must be parent-region based instead of
  independently radius based.
  Date/Author: 2026-06-15 / User and Codex.

- Decision: the rebuilt visible set must introduce finer LODs one level at a
  time and must not skip from `lod5` directly to `lod3` or lower.
  Rationale: one-level transitions keep fallback, readiness, dissolve, and
  removal rules tractable.
  Date/Author: 2026-06-15 / User and Codex.

- Decision: evaluate `rayon` plus `wasm-bindgen-rayon` as the preferred
  library path only if benchmarks prove the current opaque browser worker path
  has significant performance issues.
  Rationale: it is the strongest match for Rust-owned jobs on browser workers,
  but it likely requires atomics, nightly wasm standard-library builds, and a
  different wasm-bindgen output path. The user is happy to keep the current
  worker system if it does not show significant performance problems, so the
  rebuild should not take on Rayon/WASM atomics complexity speculatively.
  Date/Author: 2026-06-15 / User and Codex.

## Outcomes & Retrospective

Milestone 1 is complete. The rebuild now has an additive
`terrain_core::rebuild` model that encodes LOD order, node metrics,
parent/child relationships, 3x3x3 parent-region child selection, and child-group
replacement readiness. It does not yet generate terrain, schedule jobs, dissolve
transitions, or connect to the active renderer; those remain Milestones 2
through 5.

## Contract and Quality Baseline

This plan preserves the active OFG contracts:

- `OFG-API-001`: the browser shell continues to use `RustBrowserGame.create`,
  `resize`, `tick`, `command`, and `debugSnapshot`. Terrain scheduler,
  visibility, worker request IDs, stale completion checks, and renderer updates
  remain Rust-owned.
- `OFG-API-003`: debug hooks may report Rust-assembled terrain state, stream
  timings, transition counts, and worker status. Browser code must not compute
  desired terrain sets, LOD selection, terrain visibility, material selection,
  or renderer state.
- `OFG-API-004`: terrain mesh vertices keep the current renderer contract unless
  a milestone updates all Rust, WGSL, generated shader artifacts, and tests
  together. Water bathymetry packets remain terrain-job outputs consumed by the
  Rust/wgpu renderer.
- `OFG-API-005`: terrain presets and variant descriptors remain Rust-owned.
  TypeScript may edit flat descriptor values for UI, but cannot sample terrain
  or classify materials.
- `OFG-API-006`: the standalone `terrain_core.wasm` artifact remains a fixture
  and worker-build artifact, not a TypeScript terrain runtime.
- `OFG-API-009`: TypeScript must not regain terrain generation, density
  sampling, stream scheduling, mesh generation, WebGPU resource ownership, water
  generation, material manifest interpretation, or draw submission.

Every implementation milestone must keep modified implementation files above
the repository coverage attention threshold. Before this plan is complete, run
`npm run coverage:rust` and confirm the default filtered output does not list
modified implementation files below the threshold, or record an explicit
exception here with rationale.

## Context and Orientation

Current terrain lives mostly in `crates/terrain_core/src`. It includes chunk and
node identity, density sampling, broad shape presets, material classification,
Dual Contouring mesh generation, placement sampling, vertical band resolution,
transition edge meshes, water bathymetry packet generation, streaming state, a
fixture facade, and large test modules. `crates/engine_web/src/terrain_stream.rs`
owns the playable browser stream facade, emits opaque browser worker requests,
validates completions, uploads/removes meshes through the Rust/wgpu renderer,
and assembles terrain debug status. `src/engine/web/terrainWorkerClient.ts` and
`src/engine/web/terrainBuildWorker.ts` currently route Rust-issued build
requests to browser workers and call raw `terrain_core.wasm` exports.

The rebuild should not preserve this shape just because it exists. The reference
snapshot is a memory aid only. Active code should be reintroduced as small,
named modules with direct tests:

- Terrain identity: LOD order, node coordinates, world spans, parent and child
  relationships, stable debug keys, and negative-coordinate floor division.
- Desired-region resolution: 3x3x3 parent-grid rule, infinite `lod5` grid, and
  vertical range support that does not assume one fixed band.
- Generation: one whole node per job, 33x33x33 sample lattice, no-overhang
  column solidification, broad shape noise, detail noise, compact mesh packet,
  optional water-depth packet, and material labels.
- Streaming: job queue, generated/empty/failed states, readiness of all eight
  children before parent replacement, one-level-at-a-time refinement, dissolve
  transition ownership, and stale generation/variant checks.
- Renderer handoff: sub-millisecond per-frame application by toggling active
  draw membership and by refusing to perform heavy generation/upload policy in
  TypeScript.

## Plan of Work

Milestone 0 preserves the current implementation as reference. Create a
reference-only folder under `docs/reference/terrain_legacy_2026_06_15/` with a
README and copied source files from terrain-owned Rust and browser worker paths.
The folder must not be compiled, imported, or treated as an active source of
truth. Because the worktree is dirty, use a copy snapshot first; active deletion
comes only after the rebuilt path passes smoke.

Milestone 1 adds a small rebuilt terrain specification model in
`crates/terrain_core/src/rebuild/`. This module should have top-of-file comments
explaining that it is the new terrain model under construction. It should define
`TerrainLod`, `TerrainNodeCoord`, `TerrainNodeKey`, `TerrainNodeMetrics`,
`TerrainChildGroup`, and desired-region helpers. Tests must prove node spans,
parent/child mapping, negative coordinate floor division, the infinite `lod5`
grid rule, and the 3x3x3 parent-to-child desired set.

Milestone 2 adds generation contracts without replacing rendering yet. Define
node build request and output packets that include mesh data, empty state,
generation timing, material IDs, and optional water bathymetry data. Implement a
first synchronous no-overhang density path using the rebuilt identity and
descriptor shape values. Add tests for deterministic density, no-overhang column
solidification, material classification by altitude/gradient, empty-node output,
and water packet presence when sea level crosses a node.

Milestone 3 adds the rebuilt stream state machine. It owns request IDs,
generation revisions, desired sets, queue priority, generated/empty caches,
visible set selection, transition states, and stale completion rejection. Tests
must prove a parent remains visible until all eight children are generated or
empty, replacement happens as a single visible-set flip, transitions retain both
incoming and outgoing nodes until dissolve completion, and LOD refinement does
not skip levels.

Milestone 4 decides the job execution strategy. First, build an executor
interface that supports native synchronous tests and browser async completions.
Then run a small branch experiment with `rayon` plus `wasm-bindgen-rayon`. Adopt
it only if `npm run build:wasm` and `npm run smoke:browser` can run with the
required wasm atomics, `SharedArrayBuffer`, and async thread-pool initialization
without blocking the browser main thread. If that proof fails, keep a minimal
opaque browser worker adapter and record why in the Decision Log.

Milestone 5 connects the rebuilt stream to active `engine_web` terrain rendering
and debug snapshots. Retire legacy active modules only after equivalent behavior
exists in the rebuilt path. Maintain compatibility fields only where browser
HUD, smoke, or generated WASM contracts still need them. Keep the renderer API
node-keyed and Rust-owned.

Milestone 6 validates the rebuild with Rust unit tests, Rust image smoke,
browser smoke, benchmarks, shader/wasm checks if touched, and coverage. Add or
update smoke scenarios that prove hole-free replacement, visible dissolve
transitions, water depth packets near shorelines, and nonblank multi-LOD
terrain frames. `npm run bench:terrain:rust` must report per-node generation
timing distributions with attention to the under-30ms target.

## Concrete Steps

Run commands from `C:\dev\ofg`.

Initial safe setup:

    git -c safe.directory=C:/dev/ofg status --short
    New-Item -ItemType Directory -Force docs/reference/terrain_legacy_2026_06_15

Milestone 1 focused validation:

    cargo test -p terrain_core rebuild
    npm run test:rust

After each milestone:

    git -c safe.directory=C:/dev/ofg diff --check
    npm run test:rust

Before plan completion:

    npm test
    npm run smoke:rust
    npm run smoke:browser
    npm run bench:terrain:rust
    npm run coverage:rust

Run `npm run check:shaders` if any WGSL, shader metadata, scene target, water,
or terrain vertex layout changes. Run `npm run check:wasm` after wasm export or
generated binding changes.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
3. Apply required findings before marking that milestone complete, or record a
   rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in
   Progress or Outcomes & Retrospective.

## Validation and Acceptance

The rebuilt terrain path is accepted only when these behaviors are observable:

- The current terrain implementation exists only as reference material in
  `docs/reference/terrain_legacy_2026_06_15/` or another documented
  reference-only folder.
- Active terrain identity uses `lod0` as highest detail, `lod5` as the current
  coarsest infinite grid, and parent/child relationships with floor division for
  negative coordinates.
- LOD0 nodes span 32x32x32 meters with 32 cells and 33 samples per axis.
  Coarser nodes double world cell size per LOD while keeping 32 cells per axis.
- Desired child sets are derived from a 3x3x3 parent grid around the player.
- A parent is replaced only when all eight children are generated or proven
  empty.
- A visible parent/child replacement can be applied as one visible-set change
  after readiness is satisfied.
- Dissolve transitions keep both outgoing and incoming LOD nodes alive until
  their transition completes.
- Terrain generation jobs are one whole node per job.
- Browser TypeScript routes opaque terrain jobs only; it does not compute
  terrain desired sets, visibility, generation, materials, water, or rendering.
- Rust image smoke captures nonblank multi-LOD terrain frames and water-depth
  behavior where sea level intersects terrain.
- Browser smoke passes with Rust-owned runtime sentinel strings, worker/job
  status, reload health, and nonblank frames.
- `npm run bench:terrain:rust` reports generation timings and flags any normal
  node class that regularly exceeds the 30ms target.
- `npm run coverage:rust` does not list modified implementation files below the
  default filtered coverage attention threshold unless this plan records an
  explicit exception.

## Idempotence and Recovery

The reference snapshot can be recreated by deleting only
`docs/reference/terrain_legacy_2026_06_15/` and copying the current terrain
files again. Do not delete active terrain modules until the rebuilt replacement
has passing tests and smoke. If a Rayon/WASM thread-pool experiment destabilizes
the build, revert only the experiment files from that milestone, record the
result here, and continue with the minimal opaque browser worker adapter.

Because the worktree starts dirty, every milestone should inspect `git status`
before broad moves or deletions. Never use `git reset --hard` or `git checkout
--` for recovery unless the user explicitly asks for it.

## Artifacts and Notes

Reference snapshot target:

    docs/reference/terrain_legacy_2026_06_15/

Expected generated validation artifacts:

- Rust terrain benchmark reports under `artifacts/terrain-bench/`.
- Rust image smoke screenshots and reports under `artifacts/rust-smoke/`.
- Browser smoke screenshots and reports under `artifacts/browser-smoke/`.
- Rust coverage summaries under `artifacts/coverage/rust/`.

Thread-pool research notes:

- `wasm-bindgen-rayon` 1.3.0 documents a Rayon adapter for browser WebAssembly
  using Web Workers and `SharedArrayBuffer`, with async `initThreadPool` setup.
- The wasm-bindgen threading guide documents atomics-related target features,
  nightly `-Z build-std`, and main-thread blocking caveats for threaded WASM.
- OFG already serves COOP/COEP and smoke-tests `crossOriginIsolated` plus
  `SharedArrayBuffer`, which removes one browser prerequisite but not the Rust
  build-pipeline work.
- The current opaque browser worker system remains the preferred path unless
  benchmark evidence shows significant terrain generation or completion-routing
  performance issues.

Milestone 1 review:

- Scope: additive rebuild model in `crates/terrain_core/src/rebuild/mod.rs`,
  public module export in `crates/terrain_core/src/lib.rs`, and this ExecPlan.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally using the repo-local `milestone-review` skill.
  Sub-agents were not spawned because this was the plan-required gate, not an
  explicit user request for delegated reviewers.
- Required findings fixed: added the documented `TerrainLod` alias and used it
  in public rebuild model fields/functions so the implementation matches the
  Milestone 1 plan vocabulary.
- Follow-ups recorded: coverage remains the plan completion gate; no coverage
  run was needed for this additive model slice because focused and workspace
  Rust tests passed and the plan still requires `npm run coverage:rust` before
  completion.
- Rejected findings: none.
- Validation rerun: `cargo fmt -p terrain_core`, `cargo test -p terrain_core
  rebuild`, `npm run test:rust`, and
  `git -c safe.directory=C:/dev/ofg diff --check` all passed.
- Remaining risk: the rebuild model is not yet the active runtime terrain path.
  Generator, stream state machine, dissolve transitions, worker execution, and
  renderer integration are still future milestones.

## Interfaces and Dependencies

The first rebuilt model module should expose names close to these. Exact Rust
types may change during implementation if the Decision Log records why.

    pub const MAX_PLAYABLE_LOD: u8 = 5;
    pub const TERRAIN_NODE_CELLS_PER_AXIS: u32 = 32;
    pub const LOD0_NODE_SIZE_METERS: f64 = 32.0;
    pub const TERRAIN_NODE_SAMPLES_PER_AXIS: u32 = 33;

    pub struct TerrainNodeCoord {
        pub x: i32,
        pub y: i32,
        pub z: i32,
    }

    pub struct TerrainNodeKey {
        pub lod: u8,
        pub coord: TerrainNodeCoord,
    }

    pub fn terrain_node_size_meters(lod: u8) -> f64;
    pub fn terrain_node_cell_size_meters(lod: u8) -> f64;
    pub fn terrain_node_parent(key: TerrainNodeKey) -> Option<TerrainNodeKey>;
    pub fn terrain_node_children(parent: TerrainNodeKey) -> Option<[TerrainNodeKey; 8]>;

    pub struct TerrainParentRegion {
        pub lod: u8,
        pub center: TerrainNodeCoord,
        pub radius: i32,
    }

    pub fn desired_children_for_parent_region(region: TerrainParentRegion) -> Vec<TerrainNodeKey>;

    pub enum TerrainNodeReadiness {
        Missing,
        Generated,
        Empty,
    }

    pub fn child_group_can_replace_parent(children: [TerrainNodeReadiness; 8]) -> bool;

Future stream interfaces should keep request, completion, and transition state
Rust-owned. TypeScript-facing packets may contain opaque IDs, numeric node keys,
typed arrays, timing, and failure messages, but no terrain policy.
