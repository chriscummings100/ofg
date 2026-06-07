# Terrain Streaming Hierarchy Fix

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

Recent merges made the playable terrain visibly broken. The user supplied a
photo showing a broad horizontal sky band between foreground and distant terrain,
which is a genuine streaming bug, not the expected thin cross-LOD seam that will
later need aprons. The goal of this plan is to rebuild terrain streaming around
the standard hierarchical invariants so the player always has valid terrain
cover while the stream settles and while moving at running speed.

After this work, a player should be able to start the browser game, wait for
terrain to settle, run through the world, and not see large holes, missing floor
patches, wrong visible LOD swaps, or broad sky bands caused by unloading before
replacement meshes are ready. Thin cracks between different LOD resolutions are
explicitly a later apron task unless they indicate a missing mesh or invalid
visible cover.

For this plan, `lod = 0` is highest-detail terrain. Larger `lod` values are
coarser. A `lod = 1` node covers eight `lod = 0` children, a `lod = 2` node
covers eight `lod = 1` children, and so on. "Generated" means the node has been
classified as either a renderable mesh or empty. "Visible" means its mesh is
currently submitted to the renderer. Empty nodes can satisfy hierarchy rules,
but they are not rendered.

The universal rules are:

- A node must be generated before any of its children are generated.
- A node cannot be discarded before all of its children are discarded.
- A child group replaces its parent only when all eight siblings are generated
  or identified as empty. The parent is hidden but retained.
- When a child group is no longer desired, the children are hidden/discarded
  first and the retained parent can be made visible again in one frame.

## Progress

- [x] (2026-06-07 19:30+01:00) Read `AGENTS.md`, `README.md`,
  `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, `docs/TERRAIN_PLAN.md`, and
  `PLANS.md` for terrain ownership, test expectations, and ExecPlan rules.
- [x] (2026-06-07 19:34+01:00) Created the initial terrain hole regression plan.
- [x] (2026-06-07 21:10+01:00) Reframed this plan from a narrow smoke
  regression investigation into a hierarchical terrain streaming fix.
- [x] (2026-06-07 21:10+01:00) Audited the current scheduler shape enough to
  identify the main architectural mismatch: `terrain_core` splits density and
  mesh stages, while `engine_web` layers cache/visibility retention on top of a
  recomputed desired set.
- [x] (2026-06-07 21:52+01:00) Milestone 1 complete: audited the current
  density/mesh split scheduler and recorded the v2 generated-node decision in
  this plan, `docs/API_CONTRACTS.md`, and `docs/ARCHITECTURE.md`.
- [x] (2026-06-07 21:52+01:00) Milestone 2 complete: added/updated Rust tests
  for parent-before-child builds, complete sibling closure, movement-time
  visible cover, parent/child overlap rejection, same-LOD boundary vertex reuse,
  settled stream status, and smoke lower-center sky-hole detection.
- [x] (2026-06-07 21:52+01:00) Milestone 3 complete: replaced the active
  scheduler lifecycle with single `BuildNode` jobs that complete as generated
  mesh or empty nodes, while keeping fixture-only density-named compatibility
  exports/status fields.
- [x] (2026-06-07 21:52+01:00) Milestone 4 complete: added the
  `running-stream-delta` Rust smoke scenario, made multi-LOD smoke wait for
  settled status, added a lower-center sky-hole failure gate, and slowed the
  day-night cycle to keep smoke inspectable.
- [x] (2026-06-07 21:52+01:00) Milestone 5 complete: ran validation, coverage,
  benchmark, browser smoke, generated-WASM check, diff check, and local
  milestone review. Evidence is recorded below.
- [x] (2026-06-07 21:52+01:00) Local milestone review complete. Sub-agent tools
  were available, but the tool policy only permits spawning when the user
  explicitly asks for sub-agents/delegation, so the contract, code-quality,
  legacy, correctness, and validation passes were done locally.

<!-- Completed milestone checklist retained for audit:
- [x] Milestone 1: finish the current-code audit, record a precise v1-versus-v2
  decision, and leave failing or pending tests that demonstrate the broken
  lifecycle.
- [x] Milestone 2: add hierarchy and movement-delta tests that distinguish real
  missing-cover bugs from known cross-LOD apron seams.
- [x] Milestone 3: implement the streaming fix, preferring a v2 scheduler with a
  single node-build lifecycle unless Milestone 1 proves a smaller patch has the
  same clarity and safety.
- [x] Milestone 4: extend Rust and browser smoke so a broad sky/floor hole like
  the photo cannot pass after the stream reports settled.
- [x] Milestone 5: run validation, coverage, docs updates, and the repo-local
  `milestone-review` skill before marking the plan complete.
-->

## Surprises & Discoveries

- Observation: the completed `docs/TERRAIN_PLAN.md` claims hole-free parent
  fallback cover, but local smoke artifacts and the user photo show large
  missing-cover failures.
  Evidence: the photo attached under `.codex-remote-attachments/` shows a broad
  sky band; earlier local artifacts include
  `artifacts/rust-smoke/run-1780857093-665/far-view-multi-lod.png` and
  `artifacts/browser-smoke/2026-06-07T18-39-49-990Z/browser-first-person.png`.
- Observation: current Rust smoke can pass even when multi-LOD captures show
  obvious holes.
  Evidence: earlier `npm run smoke:rust` runs reported success while the
  generated far-view and LOD-boundary PNGs had visible broad gaps. The smoke
  assertions checked nonblank diversity and some LOD counts, but did not prove
  settled visible cover during movement.
- Observation: the active browser terrain path is Rust-owned and currently
  synchronous from `engine_web`; the old TypeScript terrain worker bridge is not
  the playable path.
  Evidence: `docs/API_CONTRACTS.md` forbids TypeScript terrain scheduling, and
  `docs/ARCHITECTURE.md` says `engine_web` advances terrain streaming and uploads
  meshes inside Rust. `BrowserWorkerHost` remains generic browser substrate.
- Observation: the current scheduler does not own a tree lifecycle.
  Evidence: `crates/terrain_core/src/stream.rs` recomputes `desired_mesh` and
  `desired_density` around the current center, then `prune_outside_desired_sets`
  removes node records not in either desired set. That is a window-retention
  policy, not the rule "children retire before parent."
- Observation: the current stream lifecycle is split into density and mesh jobs,
  which makes "generated" a derived state rather than the atomic scheduler state.
  Evidence: `TerrainStreamJob` has separate `Density` and `Mesh` variants, and
  `TerrainNodeRecord` tracks independent `DensityStage` and `MeshStage`.
- Observation: current browser-side fixes try to recover from lifecycle issues
  after scheduling.
  Evidence: `crates/engine_web/src/terrain_stream.rs` has `mesh_cache`,
  `visible_nodes`, delayed stale-removal while pending, and recursive
  `select_visible_node` logic. These are useful renderer concerns, but the
  parent/child lifetime rules should be guaranteed before this layer.
- Observation: day-night made visual debugging unreliable and has already been
  slowed in local WIP.
  Evidence: `crates/engine_core/src/sky.rs` now uses a much longer day length,
  and `crates/engine_core/src/tests.rs` was updated accordingly.
- Observation: replacing the split density/mesh scheduler with one generated
  node lifecycle reduced the active stream state while keeping the fixture
  exports available.
  Evidence: `TerrainStreamJob` now has one active variant,
  `BuildNode { generation, key }`; `TerrainStreamScheduler` stores a single
  `NodeStage` per `TerrainNodeKey`; `npm test` and `npm run check:wasm` passed.
- Observation: the old pending-retention workaround could render a parent and a
  child together after the scheduler became node-based.
  Evidence: `browser_terrain_stream_keeps_current_position_covered_while_running`
  initially failed with visible `lod1:0,0,0` and `lod0:0,0,0` overlap. The
  runtime adapter now removes hierarchy-conflicting visible nodes even while it
  retains non-conflicting stale cover during pending updates.
- Observation: a deliberately wider default Y band is feasible for the current
  smoke/test budget, but it increases desired node counts and needs future
  optimization.
  Evidence: Rust smoke passed with multi-LOD `desiredRenderNodeCount` 404 for
  `far-view-multi-lod` and 508 for `running-stream-delta`; the terrain benchmark
  reported the multi-LOD probe rendering 135 nodes over an 896m by 896m span.
- Observation: the new movement smoke scenario directly exercises the failure
  mode the user called out: streaming after a clean initial load.
  Evidence: `npm run smoke:rust` wrote
  `artifacts/rust-smoke/run-1780864614-889/running-stream-delta.png`; its report
  shows `streamPending: false`, `missingNodeCount: 0`, `maxRenderedLod: 2`, and
  `lowerCenterSkyLikeRatio: 0.00028153154`.

## Decision Log

- Decision: expand the initial regression plan in place instead of creating a
  second active plan.
  Rationale: the original plan was too narrow, but its reproduction context is
  still relevant. Keeping one active plan avoids contradictory terrain fix docs.
  Date/Author: 2026-06-07 / Codex.
- Decision: treat broad holes, missing floor, wrong visible LODs, and missing
  generated meshes as correctness bugs; treat thin mixed-LOD cracks as a future
  apron task.
  Rationale: aprons solve geometric mismatch at valid LOD boundaries. They do
  not excuse invisible parent/child cover, missing sibling groups, or unloaded
  terrain over the player path.
  Date/Author: 2026-06-07 / Codex.
- Decision: prefer a v2 hierarchical scheduler over more v1 patches unless the
  audit finds a very small change that makes the invariants structural.
  Rationale: the current v1 has split density/mesh jobs, recomputed desired sets,
  and pruning that is not ordered by tree lifecycle. The invariant should live in
  one scheduler data model, not as several compensating checks across
  `terrain_core` and `engine_web`.
  Date/Author: 2026-06-07 / Codex.
- Decision: collapse the active runtime conceptually to a single "build node"
  job that produces `GeneratedMesh` or `GeneratedEmpty`.
  Rationale: there is no active gameplay scenario where the runtime wants a
  density chunk without the corresponding mesh classification. Density can
  remain an internal meshing detail or cache optimization, but scheduling should
  request generated nodes.
  Date/Author: 2026-06-07 / Codex.
- Decision: start with a deliberately wide vertical band and optimize empty
  elimination later.
  Rationale: Y-band mistakes produce catastrophic holes. A wider band is easier
  to prove correct while we restore the hierarchy; performance can be recovered
  with faster empty detection and terrain-envelope heuristics.
  Date/Author: 2026-06-07 / Codex.
- Decision: do not restore TypeScript terrain workers or TypeScript terrain
  scheduling while investigating "async terrain gen."
  Rationale: active architecture says Rust owns streaming and mesh generation.
  If async returns, it should be a Rust-owned job/worker boundary behind the same
  hierarchical scheduler contract.
  Date/Author: 2026-06-07 / Codex.
- Decision: keep legacy density-named status fields and fixture exports as
  compatibility aliases rather than deleting them in this milestone.
  Rationale: browser HUD/smoke and standalone `terrain_core.wasm` export checks
  already know these names. The active scheduler no longer submits density jobs,
  and active docs now state that density-named fields are opaque compatibility
  status, not a browser density pipeline.
  Date/Author: 2026-06-07 / Codex.
- Decision: use the same broad vertical offsets `[-2, -1, 0, 1]` for default
  LOD0, LOD1, and LOD2 bands.
  Rationale: vertical under-coverage produces catastrophic holes. This milestone
  favors correctness and visual stability over optimizing empty-node dismissal;
  optimization is a later terrain-generation task.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

Completed. The active terrain stream now schedules generated nodes, not separate
density and mesh phases. Parent-before-child is enforced by the scheduler, child
groups only replace parents after all siblings are generated or empty, and the
browser runtime rejects parent/child visible overlap while still retaining
non-conflicting cover during pending movement deltas.

Rust smoke now includes `running-stream-delta`, which settles the stream, moves
across chunk centers at running-speed-sized steps, checks cover during deltas,
settles again, renders the final frame, and fails if multi-LOD screenshots have
a broad lower-center sky-colored gap. Browser smoke waits for settled mixed-LOD
status and default daylight stays inspectable because the day length is now
86,400 seconds.

Remaining gaps are intentional: thin cross-LOD cracks still need apron work,
and the broader vertical bands need future empty-node optimization. The
benchmark evidence records current cost rather than treating it as solved.

## Contract and Quality Baseline

This work must preserve the current Rust-owned terrain architecture:

- `OFG-API-001`: browser code continues to use `RustBrowserGame.create`,
  `resize`, `tick`, `command`, and `debugSnapshot`.
- `OFG-API-003`: smoke/debug hooks may read Rust-assembled terrain status but
  must not compute terrain cover, desired sets, or LOD selection in TypeScript.
- `OFG-API-004`: terrain vertex and material layout remains 19 `f32` values
  unless a later explicitly recorded fix updates every layout site and shader
  test.
- `OFG-API-006`: standalone `terrain_core.wasm` remains limited to export
  checks and the dedicated browser terrain build worker. Terrain behavior tests
  should run through native Rust or `engine_web`.
- `OFG-API-009`: TypeScript must not regain terrain generation, stream
  scheduling, mesh generation, worker payload ownership, or render submission.

Implementation completion requires `npm run coverage:rust`. Modified
implementation files must not appear in the default filtered coverage attention
output unless this plan records an explicit exception with rationale.

## Context and Orientation

Runtime terrain is Rust-owned. `crates/terrain_core` owns height and density
sampling, node identity, density filling, Dual Contouring mesh generation, and
the scheduler. `crates/engine_web/src/terrain_stream.rs` owns the browser runtime
stream adapter: it asks `terrain_core` for jobs, builds terrain node meshes,
caches mesh data, selects visible nodes, produces renderer updates, and reports
debug status. `crates/engine_web/src/wgpu_renderer.rs` owns Rust/wgpu terrain
mesh handles and draw submission. `crates/ofg_test_harness/src/render_smoke`
creates native offscreen terrain screenshots and reports.

The current v1 scheduler in `crates/terrain_core/src/stream.rs` has these
important properties:

- `TerrainStreamScheduler::sync_center` rebuilds desired mesh and desired
  density sets from configured LOD bands around the player.
- `build_desired_mesh_nodes` inserts configured LOD band nodes and their
  coarser ancestors. Local WIP also closes sibling groups when a parent and a
  child are desired.
- `TerrainStreamJob` separates `Density` and `Mesh`.
- `TerrainNodeRecord` separates `DensityStage` and `MeshStage`.
- `tick` schedules jobs based on missing density or mesh dependencies.
- `prune_outside_desired_sets` removes node records if they are no longer in the
  recomputed desired sets.

The current browser stream in `crates/engine_web/src/terrain_stream.rs` then
adds:

- `mesh_cache` for generated non-empty meshes.
- `visible_nodes` for renderer-submitted meshes.
- `select_visible_node`, which renders children only when all eight children
  are desired and generated, otherwise falls back to the parent if it has mesh.
- Local WIP that keeps old visible meshes while the stream is pending.

That browser layer is not enough by itself. The scheduler should own node
lifetime and generated state so the renderer layer only chooses visibility from
a valid generated tree.

## Plan of Work

Milestone 1 audits and pins the design. Read the current scheduler, runtime
stream, smoke harness, and tests. Record the exact ways v1 can violate or obscure
the hierarchy: density-only state, desired-set pruning, movement deltas,
partial sibling groups, stale completions, and visible-cache retention. Produce
or preserve at least one failing test or artifact that demonstrates a real
missing-cover bug. The expected decision is to implement a v2 hierarchical
scheduler, but this milestone must record the final choice and rationale.

Milestone 2 adds tests before the main rewrite. Tests should cover:

- Parent is generated before any child build job can be submitted.
- A child group cannot become visible until all eight siblings are generated or
  empty.
- A parent remains retained while any generated child exists.
- Retiring a desired window discards/hides children before the parent can be
  discarded.
- Moving at running speed after an initial settle never leaves the current
  player position without visible cover.
- Same-LOD neighboring meshes share identical boundary vertices, preserving the
  existing seam contract.
- Smoke/debug status can report settled state only when missing generated nodes
  are zero for the desired hierarchy.

Milestone 3 implements the scheduler fix. The preferred v2 shape is:

- Replace public active scheduling with `BuildNode { generation, key }` jobs
  that complete as `GeneratedMesh` or `GeneratedEmpty`.
- Keep density sampling and any density cache as an internal implementation
  detail of node generation, not a separate requested state.
- Store nodes in one hierarchy-aware record map keyed by `TerrainNodeKey`.
- Track desired target nodes from LOD bands, then expand to required ancestors
  and complete sibling groups.
- Enforce build eligibility in one place: parent generated first, then children,
  nearest/lowest LOD priority where appropriate.
- Enforce retention eligibility in one place: a node can be removed only after
  all generated descendants are gone.
- Select visibility from generated tree state: recurse to children only when the
  complete child group is generated or empty; otherwise render the retained
  parent if non-empty.
- Preserve current debug fields where practical, but rename or map legacy
  density counts carefully so browser smoke remains black-box and TypeScript
  does not become a terrain client.

If removing the legacy density/mesh facade in one step is too disruptive, keep
fixture-only compatibility functions in `crates/terrain_core/src/facade.rs` but
make the active Rust library/runtime path use the single generated-node
lifecycle. Any temporary compatibility must be recorded here and tested as
fixture-only.

Milestone 4 hardens smoke. Rust smoke should include at least one photo-like
forward view and one movement-delta scenario: settle, move the camera/player at
running speed across multiple chunk centers, tick terrain normally, and
periodically assert no broad missing-cover holes. Image checks should separate
large lower-center sky/floor gaps from legitimate thin cross-LOD cracks.
Browser smoke should keep day lighting inspectable, wait for a truly settled
Rust terrain status, and assert Rust-owned streaming status rather than compute
terrain in TypeScript.

Milestone 5 validates and reviews. Run narrow tests during iteration, then broad
Rust tests, smoke tests, coverage, and `milestone-review`. Update
`docs/ARCHITECTURE.md`, `docs/API_CONTRACTS.md`, or `docs/TERRAIN_PLAN.md` only
where the actual final contracts changed. Record exact artifact paths and
commands here.

## Concrete Steps

Work from `C:\dev\ofg`.

Audit current terrain stream code:

    rg -n "TerrainStreamScheduler|TerrainStreamJob|DensityStage|MeshStage|sync_center|prune|select_visible" crates/terrain_core/src crates/engine_web/src -g "*.rs"
    cargo test -p terrain_core stream_scheduler
    cargo test -p engine_web browser_terrain_stream

Run current visual reproductions:

    npm run smoke:rust
    npm run smoke:terrain-seams
    npm run smoke:browser

Run focused tests while iterating:

    cargo test -p terrain_core stream_scheduler
    cargo test -p engine_web browser_terrain_stream
    cargo test -p ofg_test_harness render_smoke

Run full validation for logic changes:

    npm run test:rust
    npm test
    npm run smoke:rust
    npm run smoke:browser
    npm run coverage:rust

If generated WASM artifacts change, run:

    npm run check:wasm

## Milestone Review

After each implementation milestone:

1. Update changed docs or contracts.
2. Run the repo-local `milestone-review` skill against the diff and this plan.
3. Apply required findings or record a rejected finding with rationale.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks here.

Milestone review, 2026-06-07:

- Scope: hierarchical terrain streaming fix across `terrain_core`,
  `engine_web`, Rust smoke harness, browser smoke, sky timing, generated WASM
  artifacts, and active architecture/API docs.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were present but not used because tool
  policy requires the user to explicitly ask for sub-agents/delegation.
- Required findings fixed: active docs did not yet say density-named
  terrain-stream fields are compatibility aliases after the generated-node
  scheduler rewrite; fixed in `docs/API_CONTRACTS.md` and
  `docs/ARCHITECTURE.md`.
- Follow-ups recorded: optimize empty-node dismissal and vertical-band cost in a
  later terrain-generation/performance task; implement cross-LOD aprons later.
- Rejected findings: no required code changes were rejected.
- Validation rerun after required doc fixes: `npm run check:wasm`, `git -c
  safe.directory=C:/dev/ofg diff --check`, and `npm run bench:terrain:rust`.
  The main code validations had already passed after the implementation and
  before doc-only fixes.
- Remaining risk: smoke now catches broad holes and movement-delta cover loss,
  but it is not a pixel-perfect visual diff and does not solve future apron
  cracks.

## Validation and Acceptance

Acceptance requires:

- The plan records whether v1 was patched or v2 replaced it, with rationale
  tied to concrete code.
- Tests prove the two universal rules: parent generated before child, parent not
  discarded before children.
- Tests prove complete sibling-group swaps: a parent is hidden only when all
  eight children are generated or empty.
- Movement-delta tests reproduce the class of bug where streaming breaks after a
  clean initial load.
- Rust smoke fails on broad terrain holes or photo-like sky bands once the stream
  reports settled.
- Same-LOD seam tests still pass and are not confused with future cross-LOD
  apron work.
- The default day-night cycle is slow or paused enough for default smoke images
  to remain inspectable.
- `npm run test:rust`, `npm run smoke:rust`, `npm run smoke:browser`, and
  `npm run coverage:rust` pass.
- `milestone-review` has no unhandled required findings.

Validation evidence, 2026-06-07:

- `cargo test -p terrain_core stream_scheduler --no-fail-fast`: passed, 13
  scheduler tests.
- `cargo test -p engine_web browser_terrain_stream --no-fail-fast`: passed, 5
  browser terrain stream tests.
- `cargo test -p ofg_test_harness render_smoke --no-fail-fast`: passed, 23
  render-smoke tests before the wider validation run.
- `npm run smoke:rust`: passed. Artifacts:
  `artifacts/rust-smoke/run-1780864614-889/`; report:
  `artifacts/rust-smoke/run-1780864614-889/report.json`.
- `npm run test:rust`: passed across the Rust workspace.
- `npm run smoke:browser`: passed. Artifacts:
  `artifacts/browser-smoke/2026-06-07T20-42-37-526Z/`; screenshot:
  `browser-first-person.png`.
- `npm run coverage:rust`: passed. Filtered coverage output listed no
  implementation files below the default 90% attention threshold.
- `npm test`: passed, including 90 TypeScript tests.
- `npm run check:wasm`: passed after generated WASM rebuilds.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed; Git only printed
  Windows line-ending warnings.
- `npm run bench:terrain:rust`: passed. Report:
  `artifacts/terrain-bench/run-1780865460-775/report.json`; multi-LOD probe:
  62 stream ticks, 404 loaded nodes, 135 rendered nodes, max LOD 2, 896m by
  896m visible span.

## Idempotence and Recovery

Smoke commands write under `artifacts/` and can be rerun. Code changes should be
small enough to review in normal git diff output. Do not reset or discard user
changes.

If the v2 rewrite destabilizes rendering, temporarily configure the runtime to a
single conservative LOD0 band while keeping the scheduler tests. That rollback
preserves playability and gives a smaller surface for finishing the hierarchy.
Do not restore TypeScript terrain generation or TypeScript terrain workers.

## Artifacts and Notes

Known relevant artifacts and references:

- User-provided photo attachment under
  `.codex-remote-attachments/019ea357-e527-7050-8386-3baf81112414/4d9088de-325e-42c8-ba3f-becba0b5d5a6/1-Photo-1.jpg`
  shows a broad horizontal sky band that should fail future smoke.
- `artifacts/rust-smoke/run-1780857093-665/far-view-multi-lod.png` and
  `artifacts/rust-smoke/run-1780857093-665/lod-boundary-oblique.png` were
  examples where smoke passed despite visible multi-LOD holes.
- `artifacts/browser-smoke/2026-06-07T18-39-49-990Z/browser-first-person.png`
  showed browser-path terrain holes while browser smoke still passed.

Current WIP before the v2 decision includes day-night slowdown, extra smoke
debug metrics, same-LOD seam tests, pending-state smoke assertions, and local
patches that retain visible meshes while streaming. These are useful evidence
but should not be mistaken for the final scheduler architecture.

## Interfaces and Dependencies

The final active runtime should expose a Rust-owned terrain stream contract from
`terrain_core` to `engine_web` that is centered on generated terrain nodes:

- `TerrainNodeKey { lod, coord }` remains the stable terrain node identity.
- The active scheduler submits generated-node build work, ideally as a single
  `BuildNode` job.
- Completion records generated non-empty mesh data or generated empty state.
- `engine_web` receives enough Rust-owned status to expose debug/smoke fields,
  but TypeScript only displays/asserts those fields.
- Renderer mesh IDs remain node-keyed so different LODs cannot collide.

Future apron work should build on the hierarchy: because a finer child is only
visible while its coarser parent exists, an interior apron can connect the
child edge to the parent surface. The first apron can be a simple wall if that
is enough to cover transition cracks.
