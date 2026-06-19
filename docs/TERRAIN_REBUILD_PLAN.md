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

The active rebuild deliberately starts smaller than the destination terrain
model. The first browser-visible baseline is a deterministic sine heightfield,
grass material only, and no separate collision mesh, apron, placement, water,
bathymetry, or shore logic. A minimal height query samples generated visible
triangles for player grounding, while richer collision remains future work. It
keeps the important architectural rule that `lod = 0` spans 32x32x32 meters
with 32 cells per axis and 33 shared samples per edge, while coarser LODs
double world cell size per level. Rich density fields, overhangs,
biome/material classification, and water return only after the streaming and
transition model is lean and observable.

Streaming must be hole-free and cheap on the main thread. A child group can
replace its parent only when all eight children are generated or proven empty.
For a target detail level, desired child nodes are derived from a 3x3x3 grid of
parent nodes around the player. Introducing finer LODs proceeds one level at a
time from `lod5` toward `lod0`; the active stream does not skip levels. When a
new LOD replaces an older one, the visual transition should use a dissolve in
which complementary random screen-space or world-space masks discard pixels
from the outgoing and incoming LOD. Nodes participating in a transition cannot
be removed until the dissolve completes.

Success for this reset means the browser-visible terrain renders again from the
new baseline, and the active implementation is small enough to reason about.
Smoke tests should first prove that sky plus sine-grass terrain render
nonblank, a settled stream has no holes, parent/child swaps are one-frame
visible-set flips after generation, dissolve transitions retain both sides until
completion, and terrain generation remains within the target budget of under
30ms per generated node on a worker, with main-thread transition work close to a
render-bit toggle.

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
- [x] (2026-06-15 23:05+01:00) Re-scoped the active rebuild to the requested
  minimum viable terrain: sine heightfield, grass only, no collision, no
  aprons, no placement, and no water.
- [x] (2026-06-15 23:18+01:00) Milestone 2A: replaced active `terrain_core` and
  browser terrain stream code with the lean sine-grass generator,
  one-job-per-node packets, and parent-retained multi-LOD stream scheduler.
- [x] (2026-06-15 23:18+01:00) Milestone 2B: connected the lean stream to the
  browser worker path and Rust/wgpu renderer without TypeScript terrain policy.
- [ ] Milestone 2C: add focused smoke tests for nonblank sine-grass rendering,
  parent/child replacement readiness, and transition retention.
- [ ] Milestone 3: add dissolve shader/state integration for one-level-at-a-time
  LOD transitions.
- [ ] Milestone 4: reintroduce richer terrain generation only after the lean
  stream and transition model is stable.
- [x] (2026-06-15 23:18+01:00) Ran the repo-local `milestone-review` skill
  locally for the reset checkpoint. Required finding: active docs still
  described the retired density/Dual Contouring/water path. Fixed
  `AGENTS.md`, `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and this plan.
- [x] (2026-06-15 23:18+01:00) Validation for the reset checkpoint:
  `cargo test -p terrain_core`, `cargo check -p engine_web`, `npm run build`,
  `npm run check:wasm`, `npm run check:shaders`, and browser screenshot capture
  all passed. Screenshot:
  `artifacts/terrain-rebuild/sine-grass-baseline-after-build.png`.
- [x] (2026-06-16) Added the minimal walkable-terrain slice: generated terrain
  meshes now support triangle-backed height queries, `engine_web` exposes that
  query to the Rust main-thread player tick, first-person/third-person movement
  grounds against visible generated triangles when available, and the stream
  requests one vertical node above and below the player to avoid assuming a
  single fixed Y band.
- [x] (2026-06-16) Validated the height-query slice with
  `cargo test -p terrain_core`, `cargo check -p engine_web`, focused
  `cargo test -p engine_web browser_game_state_ticks_player_with_supplied_mesh_height`,
  focused
  `cargo test -p engine_web browser_terrain_stream_queries_height_from_visible_generated_triangles`,
  `npm run build`, `npm run check:wasm`, `git diff --check`, and a browser
  screenshot at
  `artifacts/terrain-rebuild/height-query-grounding.png`.
- [x] (2026-06-16) Ran the milestone-review workflow locally for the
  height-query slice. Sub-agent review tools were not used because the
  available delegation tool requires an explicit user request for sub-agents.
  Required finding fixed: public mesh height queries now skip malformed
  triangles instead of aborting the whole mesh. Follow-ups recorded below:
  TypeScript fixture tests and smoke harnesses still contain retired preset,
  water, transition, and real-scale span expectations from before the reset.
- [x] (2026-06-19) Added a small third-person camera feature to make walkable
  terrain inspection easier: aim at the player's head, keep a desired chase
  transform driven by the existing third-person look controls, clamp the camera
  goal above sampled ground, lerp the current camera position halfway to the
  goal every frame, reset the camera state when entering/leaving third-person
  mode, and verify with a regenerated 10s third-person walk GIF at
  `artifacts/terrain-rebuild/third-person-camera-walk-after-winding/`.
- [x] (2026-06-19) Validated the third-person camera and terrain winding slice
  with `cargo test -p terrain_core`, `cargo test -p engine_core`, focused
  `cargo test -p engine_web browser_game_state_third_person_draws_character_while_grounding_player`,
  `npm run build`, `npm run check:wasm`, `git diff --check`, and the browser
  50-frame third-person capture. Full `cargo test -p engine_web`,
  `npm run smoke:rust`, and `npm run smoke:browser` were attempted but still
  hit stale post-reset gates: retired preset/water/transition expectations,
  old `ofg_test_harness` terrain APIs, and the pre-reset browser LOD span wait.
- [x] (2026-06-19) Ran the milestone-review workflow locally for the
  camera/winding slice. Sub-agent review tools were not used because the
  milestone review was policy-triggered rather than explicitly requested by the
  user. Required finding fixed: removed a redundant `mesh.rs` purpose comment.
  Follow-up validation debt remains the stale broad `engine_web`, Rust smoke,
  and browser smoke gates listed above.
- [x] (2026-06-19) Removed analytic heightfield fallback from runtime
  player/camera terrain contact. Browser movement now passes generated visible
  mesh samples or `None`, terrain streaming is advanced around the predicted
  player position before collision sampling, and the third-person camera uses a
  separate mesh sample at its chase position. Verified with a new 50-frame
  capture at
  `artifacts/terrain-rebuild/third-person-mesh-collision-raised-camera/`.

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

- Observation: the browser can present frames even when the terrain pipeline is
  broken.
  Evidence: a direct swapchain clear in `BrowserWgpuRenderer::render` presented
  red, proving the WebGPU surface and browser cache-busting path were alive
  while the normal scene/post path remained black.

- Observation: the black-frame regression came from the active renderer still
  depending on the retired water composite targets.
  Evidence: hard-coded pink terrain, cyan sky, no-terrain submission, and a
  red post-process shader all still produced black frames. Rendering the scene
  pass directly into `PostProcessResources` targets and bypassing
  `WaterRendererResources::render` restored visible sky and sine-grass terrain.

- Observation: browser reloads can keep an old `engine_web_bg.wasm` when only
  the WASM binary changes.
  Evidence: appending `ENGINE_WEB_WASM_METADATA.wasmHash` to the dynamic import
  URL made renderer diagnostics and the no-water composite fix appear reliably
  in the browser.

- Observation: the first third-person camera capture still looked like the
  player was walking through sky because the sine terrain mesh triangles were
  wound for their underside.
  Evidence: the rendered terrain became visible under the player after changing
  generated terrain indices from `[a, b, c, c, b, d]` to `[a, c, b, b, c, d]`;
  `baseline_mesh_triangles_face_up_for_culled_rendering` now guards the
  positive-Y winding expected by the culled terrain pipeline.

- Observation: analytic fallback heights made player/camera collision
  debugging ambiguous.
  Evidence: the third-person capture looked like the player and camera were
  disagreeing with the rendered mesh. After removing the fallback, captured
  player Y values still changed every frame and matched the generated mesh
  height within about a millimetre, which isolated the remaining visual issue to
  camera framing/foreground terrain rather than a constant player height.

- Observation: the browser frame order could sample collision from one terrain
  visible set and render another.
  Evidence: before this fix, `RustBrowserGame::tick` sampled terrain height,
  moved the player, then advanced/uploaded the terrain stream before rendering.
  The stream now advances around the predicted player position before mesh
  collision samples are taken.

- Observation: the first walkable baseline can render while several old gates
  remain stale.
  Evidence: a fresh browser capture at
  `artifacts/terrain-rebuild/height-query-grounding.png` shows sky, sine-grass
  terrain, and first-person HUD position `X 0.0 Y -5.5 Z 0.0` for seed `246`.
  `npm run test:ts` still fails at stale fixture compile checks for retired
  presets such as `rollingHills` and removed water packet fields such as
  `waterTexelCount`. `npm run smoke:browser` builds and launches but times out
  in `waitForTerrainLodFrame` because it still requires a 7000m visible span
  plus mixed `lod0` and `lod5` keys. `npm run smoke:rust` fails to compile
  `ofg_test_harness` because it still imports removed terrain surface,
  transition, and rich descriptor APIs.

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

- Decision: the active reset starts from sine heightfield terrain with a single
  grass material and no water, collision, placement, or apron behavior.
  Rationale: the user explicitly asked to stop maintaining a working shape of
  the old terrain system and to rebuild from a smaller, cleaner streaming and
  transition core.
  Date/Author: 2026-06-15 / User and Codex.

- Decision: for the no-water baseline, the scene pass writes directly into
  post-process color/depth targets and does not use the old water composite
  path.
  Rationale: absence of water packets is not enough; the old composite path can
  still black out the final frame. Direct scene-to-post rendering matches the
  current feature set and keeps sky visible when terrain is disabled.
  Date/Author: 2026-06-15 / Codex.

- Decision: the first walkable baseline uses generated visible triangles as the
  authoritative terrain height source when available, with the analytic sine
  sampler only as a temporary fallback while the stream has no visible mesh at
  the queried X/Z.
  Rationale: this restores player grounding without reintroducing a separate
  collision mesh, old density/placement code, or TypeScript terrain sampling.
  Date/Author: 2026-06-16 / Codex.

- Decision: third-person inspection uses a small smoothed chase state rather
  than changing first-person or debug-fly camera behavior.
  Rationale: the current need is fast terrain inspection. Keeping a desired
  third-person camera position, clamping it above sampled ground, aiming at the
  player's head, and lerping by 50% gives a stable view without widening the
  camera system.
  Date/Author: 2026-06-19 / User and Codex.

- Decision: runtime terrain contact is mesh-collision-only.
  Rationale: while the sine field generates the baseline terrain mesh, using it
  as a runtime fallback hides whether player/camera bugs are caused by collision
  mesh coverage, visible-set selection, or camera framing. Missing mesh samples
  now pass `None` so failures are observable.
  Date/Author: 2026-06-19 / User and Codex.

## Outcomes & Retrospective

Milestone 1 is complete. The rebuild first added an additive
`terrain_core::rebuild` model that encoded LOD order, node metrics,
parent/child relationships, 3x3x3 parent-region child selection, and child-group
replacement readiness.

Milestones 2A and 2B are complete for the lean reset checkpoint. Active
`terrain_core` now consists of small sine-heightfield, mesh, variant, node,
stream, facade, and benchmark modules. The browser worker bridge routes opaque
Rust-issued node build requests and returns mesh buffers without TypeScript
terrain policy. Rust/wgpu renders terrain directly into post-process scene
targets with water disabled. The old density, Dual Contouring, placement,
apron, transition-edge mesh, and water-generation systems are out of the active
compiled terrain path and preserved only in the reference snapshot. Milestone
2C still needs focused replacement smoke tests, and Milestone 3 still needs the
dissolve transition shader/state work.

The height-query slice restores a minimal walkable baseline without widening
scope back toward the old terrain system. `terrain_core::MeshData::height_at`
interpolates generated triangle heights at world X/Z, `BrowserTerrainStream`
queries the visible generated mesh cache on the Rust main thread, and
`RustBrowserGame::tick` uses that sample to ground first-person/third-person
player movement. The analytic sine sampler remains only as a fallback when no
visible generated mesh covers the next X/Z yet. The browser screenshot artifact
shows the player grounded at the generated terrain height while sky and terrain
remain visible.

The third-person camera inspection slice is part of the walkable-terrain
debugging baseline, not a full camera system. It should stay inside
`engine_core` player/camera state, leave first-person and debug-fly behavior
unchanged, and be validated by focused engine tests plus a fresh browser capture
from the same frame-dump workflow used for terrain inspection. The corrected
capture lives at
`artifacts/terrain-rebuild/third-person-camera-walk-after-winding/` and keeps
all 50 source frames for per-frame inspection.

The follow-up mesh-only collision pass removed runtime analytic height fallback
from browser movement and player repositioning, samples camera ground at the
camera chase X/Z, and updates the terrain stream before collision sampling so
the rendered visible set and collision source are from the same frame. The
raised-camera capture at
`artifacts/terrain-rebuild/third-person-mesh-collision-raised-camera/` includes
per-frame player positions; Y changes on every captured frame.

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
  together. The active reset emits no water bathymetry packets; water renderer
  code is dormant compatibility until a later water milestone either removes or
  rebuilds it.
- `OFG-API-005`: terrain presets and variant descriptors remain Rust-owned.
  TypeScript may edit flat descriptor values for UI, but cannot sample terrain
  or classify materials. The active reset uses only the `sineGrass` preset.
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

Before this reset, active terrain lived mostly in `crates/terrain_core/src` and
included chunk identity, density sampling, broad shape presets, material
classification, Dual Contouring mesh generation, placement sampling, vertical
band resolution, transition edge meshes, water bathymetry packet generation,
streaming state, a fixture facade, and large test modules. That implementation
now exists only as reference material under
`docs/reference/terrain_legacy_2026_06_15/`. The active path should stay much
smaller until the streaming and transition model is proved.

The rebuild should not preserve this shape just because it exists. The reference
snapshot is a memory aid only. Active code should be reintroduced as small,
named modules with direct tests:

- Terrain identity: LOD order, node coordinates, world spans, parent and child
  relationships, stable debug keys, and negative-coordinate floor division.
- Desired-region resolution: 3x3x3 parent-grid rule, infinite `lod5` grid, and
  vertical range support that does not assume one fixed band.
- Generation: one whole node per job, 33x33 shared edge samples for LOD0,
  sine-wave height sampling, compact mesh packets, empty-node flags, and grass
  material IDs.
- Streaming: job queue, generated/empty/failed states, readiness of all eight
  children before parent replacement, one-level-at-a-time refinement, dissolve
  transition ownership, and stale generation/variant checks.
- Renderer handoff: sub-millisecond per-frame application by toggling active
  draw membership and by refusing to perform heavy generation/upload policy in
  TypeScript.

## Plan of Work

Milestone 0 preserves the previous implementation as reference. Create a
reference-only folder under `docs/reference/terrain_legacy_2026_06_15/` with a
README and copied source files from terrain-owned Rust and browser worker paths.
The folder must not be compiled, imported, or treated as an active source of
truth. The user later approved breaking old tests and deleting active legacy
terrain modules immediately, so the reset may remove compiled legacy code before
all replacement smoke tests exist.

Milestone 1 adds a small rebuilt terrain specification model in
`crates/terrain_core/src/rebuild/`. This module should have top-of-file comments
explaining that it is the new terrain model under construction. It should define
`TerrainLod`, `TerrainNodeCoord`, `TerrainNodeKey`, `TerrainNodeMetrics`,
`TerrainChildGroup`, and desired-region helpers. Tests must prove node spans,
parent/child mapping, negative coordinate floor division, the infinite `lod5`
grid rule, and the 3x3x3 parent-to-child desired set.

Milestone 2A replaces the active terrain implementation with a compact
sine-grass baseline. Define whole-node build request/output packets that include
mesh data, empty state, generation timing, and grass material IDs. Keep
compatibility fields only where the browser facade still requires them. Remove
active separate collision meshes, apron, placement, material classification,
density-field, Dual Contouring, transition-edge mesh, and water generation code
from the compiled terrain path.

Milestone 2B connects the lean stream to active `engine_web` terrain rendering,
browser worker execution, generated WASM artifacts, and debug snapshots. Keep
the current opaque browser worker adapter unless benchmark evidence proves it is
the bottleneck. TypeScript may route opaque build requests and typed arrays, but
must not own desired sets, material choices, visibility, or rendering policy.

Milestone 2C validates the new baseline with focused Rust tests, build/wasm
checks, and immediate browser screenshots. Old smoke tests may be broken while
they still assert removed behavior, but this plan must record which gates ran
and which legacy expectations need replacement.

Milestone 3 adds the rebuilt stream transition state machine and renderer
dissolve. It owns request IDs, generation revisions, desired sets, queue
priority, generated/empty caches, visible set selection, transition states, and
stale completion rejection. Tests must prove a parent remains visible until all
eight children are generated or empty, replacement happens as a single
visible-set flip, transitions retain both incoming and outgoing nodes until
dissolve completion, and LOD refinement does not skip levels.

Milestone 4 reintroduces richer generation only after the lean multi-LOD stream
and transition model is stable. At that point, decide whether density fields,
overhangs, material classification, water, and placement belong in separate
small milestones.

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

Before the full rebuild is considered complete:

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
- First-person/third-person player grounding samples generated visible terrain
  triangles when available, without exposing terrain collision to TypeScript.
- Browser TypeScript routes opaque terrain jobs only; it does not compute
  terrain desired sets, visibility, generation, materials, water, or rendering.
- Rust image smoke captures nonblank multi-LOD terrain frames. Water-depth
  behavior is out of scope for the sine-grass baseline and must be covered by a
  later water milestone.
- Browser smoke passes with Rust-owned runtime sentinel strings, worker/job
  status, reload health, and nonblank frames.
- `npm run bench:terrain:rust` reports generation timings and flags any normal
  node class that regularly exceeds the 30ms target.
- `npm run coverage:rust` does not list modified implementation files below the
  default filtered coverage attention threshold unless this plan records an
  explicit exception.

## Idempotence and Recovery

The reference snapshot can be recreated by deleting only
`docs/reference/terrain_legacy_2026_06_15/` and copying the preserved legacy
terrain files again from Git history or the latest reference source. If a
Rayon/WASM thread-pool experiment destabilizes the build, revert only the
experiment files from that milestone, record the result here, and continue with
the minimal opaque browser worker adapter.

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
- Sine-grass reset screenshot:
  `artifacts/terrain-rebuild/sine-grass-baseline-after-build.png`.

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

Milestones 2A/2B review:

- Scope: active terrain reset across `crates/terrain_core`, browser terrain
  worker routing, `crates/engine_web` stream/render integration, generated WASM
  artifacts, terrain editor preset fields, active docs, and screenshot
  evidence.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally using the repo-local `milestone-review` skill.
  Sub-agents were not spawned because this was the plan-required gate, not an
  explicit user request for delegated reviewers.
- Required findings fixed: active docs still described the retired density,
  Dual Contouring, placement, and water path as current. Updated `AGENTS.md`,
  `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and this ExecPlan to name
  the sine-grass baseline and dormant water compatibility state.
- Follow-ups recorded: replacement smoke tests are still Milestone 2C; dissolve
  shader/state integration is still Milestone 3; the dormant water renderer
  module should be deleted or rebuilt in a later water milestone.
- Rejected findings: none.
- Validation rerun: `cargo test -p terrain_core`, `cargo check -p engine_web`,
  `npm run build`, `npm run check:wasm`, `npm run check:shaders`, browser
  screenshot capture, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: old full smoke suites may still expect retired water,
  placement, or Dual Contouring behavior until Milestone 2C replaces them with
  lean baseline smoke.

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
