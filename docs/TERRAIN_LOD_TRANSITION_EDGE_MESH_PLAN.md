# LOD Transition Edge Mesh Aprons

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

OFG's Rust terrain stream can now render mixed terrain LODs, but same-LOD seam
ownership does not by itself solve cracks between a refined child node and a
coarser parent-level surface. This plan adds separately generated LOD transition
edge meshes, also called aprons in this plan, that can be rendered only when a
fine node is visible beside coarser coverage.

The important user-visible outcome is that moving through multi-LOD terrain
should show fewer holes or cracks along LOD boundaries without forcing child
terrain meshes to be regenerated when visibility changes. The canonical child
mesh remains independent and cacheable. Aprons are derived render meshes built
from already-generated child and parent mesh data, so toggling them on or off
does not rerun density sampling, Dual Contouring, material classification, or
terrain worker jobs.

The first slice should prove the idea on X and Z side faces. A child node on the
outer boundary of its parent copies a boundary edge or narrow boundary band from
its generated mesh, asks the already-generated parent mesh for matching
polygonized surface positions, and emits a separate terrain-layout mesh that
connects the fine surface to the parent surface. Later slices can add corner
patches, smoother morph weights, or a fuller seam-space Dual Contouring system
if caves and more arbitrary topology require it.

## Progress

- [x] (2026-06-13 13:48+01:00) Drafted this ExecPlan from the selected
  mesh-space transition edge mesh approach.
- [x] (2026-06-13 13:58+01:00) Milestone 1 complete: added Rust-only
  transition mesh building in `terrain_core`, boundary-inclusive surface
  queries, and synthetic tests.
- [x] (2026-06-13 14:06+01:00) Milestone 2 complete: added Rust runtime
  transition face detection, separate transition mesh caching, and Rust status
  counters in `engine_web`.
- [x] (2026-06-13 14:28+01:00) Milestone 3 complete: transition meshes now
  upload, render, clear, and expose browser debug counters as optional terrain
  drawables distinct from canonical terrain node meshes.
- [x] (2026-06-13 15:05+01:00) Milestone 4 complete: LOD smoke renders
  transition meshes, the terrain benchmark reports transition counts/timings,
  coverage passed, and final validation is complete.
- [x] (2026-06-13 19:40+01:00) Post-validation seam correction complete:
  pink-sky diagnostics proved the visible white lines included real gaps, so the
  transition builder was changed from a vertical-query seam lattice to exact
  child/parent boundary vertex-strip zipping with one-cell corner overscan.

## Surprises & Discoveries

- Observation: `TerrainStreamScheduler` already inserts parent nodes into the
  desired hierarchy and refuses to submit child node builds until their desired
  parent has generated.
  Evidence: `crates/terrain_core/src/stream.rs` adds ancestors in
  `compute_desired_nodes`, and `should_submit_node` calls `parent_generated`.
- Observation: `BrowserTerrainStream` selects a non-overlapping visible cover:
  when every child of a desired parent is generated, the children replace the
  parent in the visible set.
  Evidence: `select_visible_node` in `crates/engine_web/src/terrain_stream.rs`
  recurses into children only when all children are desired and generated.
- Observation: browser worker terrain jobs currently return mesh buffers and
  water packets, not parent mesh context.
  Evidence: `src/engine/web/terrainBuildWorker.ts` copies `vertices`, `indices`,
  and optional water bathymetry data from `terrain_core.wasm`.
- Observation: the new `TerrainSurfaceIndex` uses half-open node ownership,
  which is correct for placement, but transition mesh generation needs explicit
  boundary-inclusive queries at parent and child edges.
  Evidence: `TerrainSurfaceIndex::owns_xz` rejects `x == node_origin_x +
  node_span` and `z == node_origin_z + node_span`.
- Observation: the first terrain-core implementation uses a deterministic
  one-cell seam lattice instead of extracting arbitrary Dual Contouring boundary
  triangles.
  Evidence: `build_parent_lod_transition_edge_mesh` samples already-generated
  fine and parent `MeshData` through `TerrainSurfaceIndex` and never calls
  density sampling or mesh generation helpers.
- Observation: transition mesh cache lifecycle is large enough that keeping it
  in `terrain_stream.rs` pushes that module over the repo's 1000-line review
  threshold.
  Evidence: Milestone 3 review initially measured `terrain_stream.rs` at 1086
  lines; extracting `TerrainTransitionMeshCache` into
  `crates/engine_web/src/terrain_transitions.rs` reduced it to 997 lines.
- Observation: native `engine_web` tests do not compile the wasm-only
  `wgpu_renderer.rs` import surface.
  Evidence: after moving `BrowserTerrainTransitionMeshUpdate` to
  `terrain_transitions.rs`, `cargo test -p engine_web` passed, but
  `npm run check:wasm` caught the stale wasm renderer import.
- Observation: the full Rust offscreen smoke now takes longer than five minutes
  on this machine.
  Evidence: an initial `npm run smoke:rust` invocation timed out at the tool
  wrapper after 304 seconds while the child process continued and wrote a
  passing report; rerunning with a longer timeout completed successfully in
  355 seconds.
- Observation: direct benchmark transition mesh timing should distinguish
  attempted faces from successful bridge meshes.
  Evidence: a four-face parent/child timing sample for the rolling hills preset
  produced two successful transition meshes. The benchmark report now records
  `attemptedCount`, `buildCount`, vertex/index totals, and timing statistics.
- Observation: hard-coded white transition vertices made early screenshots
  ambiguous.
  Evidence: forcing the sky shader to bright pink showed the visible seam lines
  turned pink, proving those pixels were real background gaps rather than only
  white apron geometry.
- Observation: a denser vertical-query seam lattice still left holes.
  Evidence: the pink-sky `lod-boundary-oblique` capture continued to show
  cracks after increasing sample density, while switching to exact boundary
  vertex profiles removed the long gaps.
- Observation: same-LOD seam correctness is a stronger transition invariant
  than arbitrary vertical ray samples.
  Evidence: same-LOD neighbor meshes already share matching one-cell apron
  vertex strips; using the hidden parent mesh's matching strip lets a child
  transition target the same positions a visible neighboring parent would use.

## Decision Log

- Decision: generate separate optional transition edge meshes instead of
  conforming or mutating the main child mesh.
  Rationale: the main child mesh must stay canonical and cacheable. Visibility
  changes or neighbor regeneration should toggle derived edge meshes, not force
  child terrain jobs to rerun.
  Date/Author: 2026-06-13 / Codex.
- Decision: derive aprons only from existing child and parent `MeshData` plus
  mesh-backed surface queries.
  Rationale: the feature should not resample density, rerun Dual Contouring, or
  build voxel transition fields. Parent and child meshes already contain the
  geometry the renderer sees; the apron should be a lightweight mesh-space
  bridge between those surfaces.
  Date/Author: 2026-06-13 / Codex.
- Decision: use the fine node's own parent mesh as the first coarse reference,
  not an arbitrary currently visible neighbor.
  Rationale: the stream guarantees the parent is generated before the child, and
  same-LOD parent meshes already have deterministic seams. If a refined parent
  region borders a visible neighboring parent, matching the hidden parent
  boundary should also match the neighboring parent boundary.
  Date/Author: 2026-06-13 / Codex.
- Decision: begin with X and Z side faces; defer Y faces, corner patches, and
  cave-grade volumetric transition cells.
  Rationale: the current terrain renderer and smoke scenarios most visibly need
  horizontal LOD boundaries. X/Z faces are enough to prove the cache and render
  model before adding corner and fully 3D topology complexity.
  Date/Author: 2026-06-13 / Codex.
- Decision: add a transition-specific boundary query path rather than weakening
  placement query ownership.
  Rationale: placement should keep half-open node ownership so a candidate on a
  shared boundary is not claimed twice. Apron generation is a different use case:
  it must be able to sample the exact parent/child boundary.
  Date/Author: 2026-06-13 / Codex.
- Decision: implement Milestone 1 with a deterministic one-cell seam lattice
  instead of direct boundary-triangle extraction.
  Rationale: `MeshData` does not retain half-edge topology, and arbitrary
  triangle boundary ordering would add fragility before runtime visibility and
  rendering are proven. The lattice still satisfies the user constraint because
  it queries only already-generated fine and parent meshes, never terrain
  density or Dual Contouring.
  Date/Author: 2026-06-13 / Codex.
- Decision: keep transition cache/delta ownership in
  `crates/engine_web/src/terrain_transitions.rs`, while `BrowserTerrainStream`
  remains responsible for visible cover selection and canonical node cache
  ownership.
  Rationale: this keeps transition lifecycle logic near transition face
  detection, avoids growing `terrain_stream.rs` beyond 1000 lines, and still
  keeps all apron decisions Rust-owned.
  Date/Author: 2026-06-13 / Codex.
- Decision: render a transition mesh under the fine node's terrain LOD debug
  mask and key renderer objects as
  `terrain-transition:<fine>:<parent>:<face>`.
  Rationale: the apron is owned by the fine boundary, should toggle predictably
  with that LOD, and must not collide with canonical `lodN:x,y,z` terrain node
  object IDs.
  Date/Author: 2026-06-13 / Codex.
- Decision: native smoke reports keep canonical `renderedNodeCount` as the node
  count, append transition meshes to the rendered mesh list, and expose
  transition counts separately.
  Rationale: this proves transition meshes are actually drawn without confusing
  terrain node coverage, parent-child overlap checks, or legacy LOD reports.
  Date/Author: 2026-06-13 / Codex.
- Decision: benchmark transition mesh construction with explicit attempted and
  successful build counts.
  Rationale: a parent/child boundary may legitimately lack a sampled bridge for
  a particular face and preset, but timing and buffer reports still need to show
  how many builds were attempted and how many produced geometry.
  Date/Author: 2026-06-13 / Codex.
- Decision: supersede the vertical-query seam lattice with boundary vertex-strip
  zipping.
  Rationale: LOD aprons must link actual child boundary vertices to actual
  parent boundary vertices, because same-LOD seam closure is guaranteed by
  matching boundary vertex strips. Regular vertical samples can miss the
  polygonized contour and leave holes.
  Date/Author: 2026-06-13 / Codex.
- Decision: allow one-cell profile overscan along the seam axis for transition
  face meshes.
  Rationale: adjacent X/Z face aprons need a small overlap at corners; otherwise
  two individually valid side meshes can meet at a zero-width line and leave
  pinholes in rasterized LOD boundary views.
  Date/Author: 2026-06-13 / Codex.

## Outcomes & Retrospective

Milestone 1 is implemented. `terrain_core` now exposes
`TerrainTransitionFace`, `TerrainTransitionMeshKey`,
`TerrainTransitionMeshConfig`, `TerrainTransitionMeshInput`, and
`build_parent_lod_transition_edge_mesh`. The builder creates a separate
terrain-layout bridge mesh from existing fine and parent `MeshData`, using
boundary-inclusive polygonized surface queries and a one-cell child-side seam
lattice. It does not mutate source meshes and does not call density sampling or
terrain meshing.

Remaining work is runtime transition-face detection, optional transition mesh
caching, renderer upload/draw support, smoke/benchmark coverage, and final
coverage validation.

Milestone 2 is implemented. `engine_web` now has
`crates/engine_web/src/terrain_transitions.rs` for parent-region face detection
and transition counter aggregation. `BrowserTerrainStream` now keeps
`transition_mesh_cache` and `visible_transition_meshes` separate from canonical
`mesh_cache`, builds transition meshes from cached fine/parent meshes after
visible cover selection, clears stale transitions when referenced node meshes
change, and reports Rust-side transition face/mesh/vertex/index counters.

Remaining work is renderer upload/draw support, browser debug snapshot typing
for transition counters, smoke/benchmark coverage, and final coverage
validation.

Milestone 3 is implemented. `BrowserTerrainStreamUpdate` now carries transition
mesh upserts/removals separately from canonical terrain node upserts/removals.
`RustBrowserGame` keeps separate transition mesh GPU handles and pending queues,
uploads transition meshes with the terrain vertex layout, draws them through the
existing terrain material pipeline, clears them during terrain resets, and
filters them by the fine node's LOD debug mask. Browser debug snapshots now
serialize `transitionFaceCount`, `transitionMeshCount`,
`transitionVertexFloatCount`, and `transitionIndexCount`, with matching
TypeScript types, fixtures, docs, and regenerated `engine_web` wasm artifacts.

Milestone 3 validation:

- `cargo test -p terrain_core transition_mesh`: passed. Five transition mesh
  tests passed.
- `cargo test -p engine_web terrain_transition`: passed. Four transition helper
  tests passed.
- `cargo test -p engine_web browser_terrain_stream`: passed. Twelve focused
  stream tests passed, including transition mesh upsert stability.
- `cargo test -p engine_web`: passed. One hundred sixty-nine engine_web tests
  passed.
- `npm run check:wasm`: passed after regenerating stale `engine_web` wasm
  artifacts.
- `npm run test:ts`: passed. One hundred fifteen TypeScript tests passed.
- `git -c safe.directory=C:/dev/ofg diff --check -- ...`: passed for Milestone
  3 paths with Windows line-ending warnings only.

Milestone 3 review:

- Scope: transition mesh update packets, transition cache extraction, renderer
  upload/removal/draw lifecycle, browser debug snapshot counters, TypeScript
  status contracts, docs, and regenerated `engine_web` wasm artifacts.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because their tool
  contract requires explicit user authorization for delegation.
- Required findings fixed: extracted transition cache lifecycle out of
  `terrain_stream.rs` after the file crossed 1000 lines; fixed stale wasm
  renderer import after `BrowserTerrainTransitionMeshUpdate` moved modules; and
  regenerated stale wasm artifacts.
- Follow-ups recorded: Milestone 4 must add visual seam/LOD smoke and benchmark
  reporting; `wgpu_renderer.rs` remains oversized and should be split before
  further substantial renderer growth.
- Rejected findings: no rejected findings.
- Remaining risk: transition meshes are now uploaded and drawn, but targeted
  smoke images have not yet proven that an active mixed-LOD boundary visually
  hides cracks.

Milestone 4 is implemented and the plan acceptance gates are satisfied. The
native Rust smoke harness now tracks transition mesh upserts/removals from
`BrowserTerrainStreamUpdate`, appends transition meshes to the terrain meshes
passed to the offscreen renderer, and reports transition face/mesh/vertex/index
counts in each scenario debug block. Multi-LOD smoke readiness now requires at
least one active transition mesh. The terrain benchmark report now includes
settled stream transition counts and a focused transition mesh build timing
summary with attempted/successful build counts, vertex/index totals, median,
p95, and mean times.

Milestone 4 validation:

- `cargo test -p ofg_test_harness render_smoke`: passed. Twenty-four focused
  smoke harness tests passed.
- `cargo test -p ofg_test_harness terrain_bench_lod`: passed. Three focused
  multi-LOD benchmark tests passed.
- `cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario lods`:
  passed. Report `artifacts/rust-smoke/run-1781358136-119/report.json`
  recorded active transition meshes in all LOD scenarios.
- `npm run smoke:rust`: passed with a longer timeout. Report
  `artifacts/rust-smoke/run-1781359151-173/report.json` recorded:
  `far-view-multi-lod` with 60 transition meshes,
  `lod-boundary-oblique` with 53 transition meshes, and
  `running-stream-delta` with 66 transition meshes. All three reached max LOD4
  and nonblank pixel diversity.
- `npm run bench:terrain:rust`: passed. Report
  `artifacts/terrain-bench/run-1781358519-194/report.json` recorded 54 active
  transition meshes in the multi-LOD stream and a focused transition build
  timing summary of two successful builds from four attempts, with median
  0.8611 ms and p95 0.9605 ms on this run.
- `npm test`: passed. The Rust workspace tests and TypeScript tests passed.
- `npm run check:wasm`: passed.
- `npm run coverage:rust`: passed. The default coverage attention output listed
  no implementation files below the 90% line coverage threshold.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows
  line-ending warnings only.

Milestone 4 review:

- Scope: Rust smoke report/schema and scenario rendering of transition meshes,
  terrain benchmark transition counters/timings, final validation artifacts,
  and this ExecPlan closeout.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because their tool
  contract requires explicit user authorization for delegation.
- Required findings fixed: no required findings after Milestone 4 edits.
- Follow-ups recorded: `wgpu_renderer.rs` remains oversized and should be split
  before further substantial renderer growth; `terrain_stream.rs` is now below
  1000 lines but close enough that future stream features should continue moving
  focused lifecycle code into smaller modules.
- Rejected findings: no rejected findings.
- Remaining risk at initial closeout: the first transition builder was an X/Z
  side-face lattice and did not yet cover corners, Y faces, or cave-grade
  arbitrary topology. Some representative benchmark faces could legitimately
  produce no transition mesh.

Post-validation seam correction:

- The transition builder now extracts exact child and parent boundary vertex
  profiles from existing `MeshData` and zips the double-resolution child profile
  to the parent profile. It no longer uses vertical surface queries for apron
  topology.
- Transition vertices preserve source mesh colors/material payloads by copying
  the original 19-float terrain vertex records.
- Face profile extraction includes a one-cell overscan along the seam axis so
  adjacent X/Z face aprons overlap at parent-region corners.
- A pink-sky diagnostic render of `lod-boundary-oblique` with the old lattice
  showed real sky gaps. The same diagnostic after vertex-profile zipping and
  corner overscan showed no visible terrain gaps in that view:
  `artifacts/rust-smoke/run-1781373197-374/lod-boundary-oblique.png`.
- A normal-sky targeted capture also passed:
  `artifacts/rust-smoke/run-1781373454-939/lod-boundary-oblique.png`.

Post-validation correction commands:

- `cargo test -p terrain_core transition_mesh`: passed. Seven transition mesh
  tests passed, including exact parent boundary vertex reuse and
  double-resolution child-to-parent zipper coverage.
- `cargo test -p terrain_core surface_query`: passed. Nine surface-query and
  placement tests passed.
- `cargo test -p engine_web transition`: passed. Five transition/runtime tests
  passed.
- `cargo test -p ofg_test_harness parse_args`: passed. Two smoke CLI filter
  tests passed.
- `cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario lods --case lod-boundary-oblique`:
  passed for the final normal-sky targeted capture.
- `npm run build:wasm`: passed and regenerated Rust wasm artifacts.
- `npm test`: passed after the correction, including the Rust workspace tests,
  TypeScript tests, shader artifact checks, and wasm artifact checks.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows
  line-ending warnings only.

## Contract and Quality Baseline

This plan must preserve the active ownership contracts in
`docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md`.

`OFG-API-001` is preserved. Browser code continues to drive the game through
`RustBrowserGame.tick`, commands, worker request routing, and Rust-assembled
debug snapshots. Transition mesh decisions and visibility are Rust-owned.

`OFG-API-003` is preserved. Debug hooks may expose Rust-authored transition mesh
counts, active transition face counts, or timing counters, but must not compute
terrain boundaries or decide apron visibility in TypeScript.

`OFG-API-004` is preserved. Transition edge meshes use the same terrain vertex
layout as normal terrain meshes: 19 `f32` values per vertex with position,
color, normal, UV, material layer indices, and material weights. Any change to
the terrain vertex layout is out of scope for this plan.

`OFG-API-006` is preserved. The standalone `terrain_core.wasm` worker artifact
may continue building canonical node meshes. The first implementation should
not require TypeScript workers to understand parent mesh context or transition
semantics. If a later optimization moves apron generation into worker jobs, Rust
must still author the opaque request payload and TypeScript must only route
bytes.

`OFG-API-009` is preserved. TypeScript must not generate, classify, schedule, or
render terrain semantically. It may receive new Rust-produced debug fields only.

Quality gates:

- Keep new modules focused and documented with top-of-file comments.
- Add behavior-focused Rust tests near the implementation.
- Run `milestone-review` after each implementation milestone before marking it
  complete.
- Before final completion, run `npm test`, relevant smoke commands, benchmark
  commands if timing-sensitive code changed, and `npm run coverage:rust`. The
  default coverage attention output must not list modified implementation files
  below the current 90% line threshold unless this plan records an explicit
  exception with rationale.

## Context and Orientation

A terrain node is identified by `TerrainNodeKey { lod, coord }`. LOD0 uses the
finest current node resolution. A parent node is one LOD coarser and covers the
eight child nodes returned by `terrain_node_children(parent)`. A child node's
parent is returned by `terrain_node_parent(child)`.

`crates/terrain_core/src/mesh.rs` emits each terrain node as `MeshData`, a flat
terrain vertex buffer and index buffer. The current vertex layout is the
renderer-facing layout. No separate editable mesh or half-edge topology is kept.

`crates/terrain_core/src/surface_query.rs` can build `TerrainSurfaceIndex` from
one generated `MeshData`. It answers vertical queries against the polygonized
mesh, not the analytic density field. The existing API intentionally uses
half-open XZ ownership for placement; transition generation will need either a
new boundary-inclusive method or a separate helper that can query exact node
edges.

`crates/terrain_core/src/stream.rs` owns desired node scheduling. It includes
ancestors of desired nodes and ensures child builds wait for parent generation.
This gives apron generation a useful invariant: if a child mesh exists, its
parent mesh should either already exist in the runtime mesh cache or be known as
generated empty.

`crates/engine_web/src/terrain_stream.rs` owns the browser terrain stream inside
Rust. It caches generated node meshes in `mesh_cache`, chooses a visible cover
with no parent-child overlap, uploads visible terrain meshes through
`BrowserTerrainStreamUpdate`, and prunes old meshes. This is the right runtime
location to decide when optional transition meshes are needed.

A transition edge mesh in this plan is a separate renderable terrain mesh that
bridges one fine node face to its parent-level surface. It is not the child's
main mesh and it is not a new terrain node. It should be keyed by the fine node,
the parent node, the face, and the active terrain variant revision.

## Research Summary

Common terrain LOD seam techniques split into four families.

Skirts add curtain geometry around tile borders. They are cheap and can hide
holes, but they do not make surfaces agree and can be visible in shadows, water,
and grazing views. They remain a fallback, not the preferred OFG solution.

Heightfield clipmaps and geomorphing blend between nested grid levels. The idea
of a transition band is useful for OFG, but OFG should blend mesh surfaces, not
height textures, because the source terrain is Dual Contouring over a 3D density
field.

Transvoxel-style transition cells and Dual Contouring seam-space generation are
more complete voxel LOD solutions. They are strong long-term references for
caves, arches, overhangs, and arbitrary topology, but they require much more
topology work than the first OFG apron slice.

The selected first step is mesh-space transition edge meshes: copy or sample the
already-generated fine boundary, query the already-generated parent mesh, and
emit a small independent bridge mesh. This preserves OFG's Rust ownership,
avoids complex terrain regeneration, and can be toggled or eventually faded by
the renderer.

Reference links:

- Transvoxel: https://transvoxel.org/
- Geometry clipmaps: https://hhoppe.com/geomclipmap.pdf
- GPU Gems terrain clipmap chapter:
  https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry
- Dual Contouring chunked terrain seam discussion:
  https://ngildea.blogspot.com/2014/09/dual-contouring-chunked-terrain.html
- ProcWorld seam-space note:
  https://procworld.blogspot.com/2013/07/emancipation-from-skirt.html

## Milestone 1 Evidence

- Added `crates/terrain_core/src/transition_mesh.rs` and
  `crates/terrain_core/src/transition_mesh_tests.rs`.
- Extended `TerrainSurfaceIndex` with `vertical_hits_including_boundary` and
  `highest_vertical_hit_including_boundary`, preserving the existing half-open
  `vertical_hits` behavior for placement.
- Exported transition mesh types and builder from `crates/terrain_core/src/lib.rs`.
- `cargo test -p terrain_core transition_mesh`: passed. Five transition tests
  cover non-empty bridge construction, 19-float terrain stride and valid
  indices, immutable source meshes, exact max-boundary parent queries,
  invalid/missing-hit rejection, and deterministic negative-coordinate ordering.
- `cargo test -p terrain_core surface_query`: passed. Nine filtered tests
  passed, including existing placement half-open ownership coverage.
- `cargo test -p terrain_core`: passed. Eighty-eight terrain-core tests passed.
- `cargo test -p terrain_core --features benchmark`: passed. Ninety-three
  feature-unified terrain-core tests passed.

Milestone 1 review:

- Scope: Rust-only transition mesh construction API, boundary-inclusive surface
  query helpers, transition tests, public exports, and this ExecPlan update.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because their tool
  contract requires explicit user authorization for delegation.
- Required findings fixed: no required findings.
- Follow-ups recorded: runtime detection and rendering must keep transition
  meshes separate from canonical node meshes and must not put transition meshes
  into `mesh_cache`.
- Rejected findings: no rejected findings.
- Remaining risk: the first builder samples a one-cell lattice rather than
  copying arbitrary boundary triangles. This is intentional for the first slice
  but should be revisited after runtime visual smoke evidence.

## Milestone 2 Evidence

- Added `crates/engine_web/src/terrain_transitions.rs`; it detects outer
  parent-region transition faces, skips faces with visible same-LOD neighbors,
  and aggregates active transition mesh counters.
- Extended `BrowserTerrainStream` with `transition_mesh_cache` and
  `visible_transition_meshes`, both separate from canonical `mesh_cache`.
- Runtime transition meshes are derived from cached fine and parent `MeshData`
  through `build_parent_lod_transition_edge_mesh`; no browser worker protocol or
  TypeScript terrain semantics changed in this milestone.
- Added Rust-side status counters: `transition_face_count`,
  `transition_mesh_count`, `transition_vertex_float_count`, and
  `transition_index_count`.
- `cargo test -p engine_web terrain_transition`: passed. Three helper tests
  passed.
- `cargo test -p engine_web browser_terrain_stream`: passed. Twelve focused
  stream tests passed, including a new cache test proving transition meshes can
  remain active across a tick without rebuilding or re-upserting canonical child
  meshes.
- `cargo test -p engine_web`: passed. One hundred sixty-eight engine_web tests
  passed.
- `git -c safe.directory=C:/dev/ofg diff --check -- ...`: passed for the
  milestone paths with Windows line-ending warnings only.

Milestone 2 review:

- Scope: Rust runtime transition face detection, transition mesh cache lifecycle,
  stream status counters, focused stream/helper tests, and this ExecPlan update.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because their tool
  contract requires explicit user authorization for delegation.
- Required findings fixed: no required findings.
- Follow-ups recorded: Milestone 3 must serialize the Rust transition counters
  through the browser debug snapshot and upload/draw transition meshes without
  putting them in canonical terrain node `mesh_cache`.
- Rejected findings: no rejected findings.
- Remaining risk: transition meshes are built and counted but not yet uploaded
  or drawn, so visual seam coverage is not proven until Milestone 3 and
  Milestone 4 smoke.

## Plan of Work

Milestone 1 adds a pure Rust transition mesh builder in `terrain_core`.

Create `crates/terrain_core/src/transition_mesh.rs` with a top comment
explaining that it derives optional LOD transition meshes from existing
`MeshData`. Add public types similar to:

    pub enum TerrainTransitionFace {
        NegX,
        PosX,
        NegZ,
        PosZ,
    }

    pub struct TerrainTransitionMeshKey {
        pub fine_key: TerrainNodeKey,
        pub parent_key: TerrainNodeKey,
        pub face: TerrainTransitionFace,
    }

    pub struct TerrainTransitionMeshConfig {
        pub max_vertical_search_meters: f64,
        pub min_normal_y: f64,
    }

    pub struct TerrainTransitionMeshInput<'a> {
        pub fine_key: TerrainNodeKey,
        pub parent_key: TerrainNodeKey,
        pub face: TerrainTransitionFace,
        pub fine_node_cell_size: f64,
        pub parent_node_cell_size: f64,
        pub fine_mesh: &'a MeshData,
        pub parent_mesh: &'a MeshData,
        pub config: TerrainTransitionMeshConfig,
    }

    pub fn build_parent_lod_transition_edge_mesh(
        input: TerrainTransitionMeshInput<'_>,
    ) -> Option<MeshData>;

The implementation should not call density sampling or terrain meshing. It
should read only the provided mesh buffers and surface query indices built from
those buffers.

The initial builder should focus on side-face stitch strips. For the requested
face, derive an ordered set of seam samples from the fine mesh boundary. A
simple first version can collect fine mesh vertices or triangle intersections
near the target face, sort them along the face's horizontal axis, deduplicate by
axis coordinate and height, and query the parent surface at the same boundary
XZ. For each neighboring pair of samples, emit two triangles connecting the
fine-side sample curve to the parent-side sample curve. The emitted mesh uses
the normal terrain vertex layout and copies or interpolates material indices and
weights from the fine side and parent side.

If synthetic tests show that arbitrary triangle boundary extraction is too noisy
for the first slice, use a deterministic seam lattice instead: sample child and
parent surfaces at fixed fine-cell intervals along the face using
boundary-inclusive surface queries. This still satisfies the main constraint
because it queries already-generated meshes, not terrain density or Dual
Contouring. Record that adjustment in the Decision Log.

Add a transition-specific query helper in `surface_query.rs` only if needed. It
should be named to make boundary ownership explicit, for example
`vertical_hits_including_boundary` or a private helper used by
`transition_mesh.rs`. Do not change placement behavior.

Add `crates/terrain_core/src/transition_mesh_tests.rs`. Synthetic tests should
cover:

- a child boundary and parent boundary at different heights produce a non-empty
  bridge mesh;
- the emitted mesh keeps the 19-float terrain vertex stride and valid indices;
- the child `MeshData` and parent `MeshData` are not mutated;
- exact parent/child boundary XZ queries succeed even where placement-style
  half-open ownership would reject the edge;
- invalid meshes, missing parent hits, or non-finite config values return no
  transition mesh;
- face ordering is deterministic for negative coordinates.

Milestone 2 integrates transition detection and caching in `engine_web` without
rendering it yet.

Add a small module such as `crates/engine_web/src/terrain_transitions.rs`. It
should own runtime-facing transition key helpers, face detection, cache counter
aggregation, and tests. Keep `terrain_stream.rs` from growing further.

Detect required side transitions from the Rust visible cover. For each visible
fine node, inspect X/Z faces. A face can require a transition when:

- the fine node has a generated parent;
- the face is an outer face of that parent region, based on child coordinate
  parity;
- the visible cover across that face is coarser than the fine node, or no
  same-LOD generated neighbor is visible;
- the parent mesh exists and is renderable, not generated empty.

Build transition meshes from `mesh_cache` entries after normal mesh completions
are accepted. Cache them separately from canonical node meshes:

    transition_mesh_cache:
        BTreeMap<TerrainTransitionMeshKey, Arc<MeshData>>

Do not put transition meshes into `mesh_cache`, because they are not terrain
nodes. Add debug status counters such as `transitionMeshCount`,
`transitionFaceCount`, `transitionVertexFloatCount`, and
`transitionIndexCount` only if they help tests and smoke diagnostics.

Tests in `crates/engine_web/src/tests.rs` or a split module should cover:

- transition detection identifies only outer parent-region faces;
- transition cache entries appear when a fine visible node borders coarser
  coverage;
- transition cache entries are removed when same-LOD neighbor coverage replaces
  the coarse boundary;
- toggling transition visibility does not rebuild or replace the canonical child
  mesh cache entry;
- stale terrain variant revisions clear transition meshes with the normal mesh
  cache.

Milestone 3 renders transition meshes as optional terrain drawables.

Extend `BrowserTerrainStreamUpdate` with transition mesh upserts/removals, or
add a parallel update packet if that keeps the code cleaner. `wgpu_renderer.rs`
should upload transition meshes using the same terrain vertex layout and terrain
material pipeline as normal terrain. Because `wgpu_renderer.rs` is already
oversized, prefer extracting terrain mesh update/status helpers before adding
more serialization or resource bookkeeping there.

Transition meshes should have stable renderer object keys distinct from terrain
node keys, for example:

    transition:lod0:0,0,0:parent=lod1:0,0,0:posX

They should participate in render debug LOD masks in a predictable way. The
first choice should be to draw a transition mesh when its fine node's LOD is
enabled, because the transition is owned by the fine boundary.

Add native Rust renderer or stream tests that prove transition meshes can be
uploaded, retained, and removed without disturbing normal terrain mesh handles.
Add debug snapshot typing only for counters, not for mesh details.

Milestone 4 adds smoke, benchmark, documentation, and final validation.

Extend Rust offscreen seam or LOD smoke scenarios so at least one camera sees a
mixed-LOD boundary where transition meshes are active. The report should include
transition mesh counts and enough rendered-node metadata to diagnose whether a
failure was "no transition generated" or "transition generated but not drawn."

Extend the terrain benchmark or a focused Rust benchmark path to include
transition mesh construction cost for realistic visible covers. Report build
count, vertex/index counts, and timing distributions. This should remain a Rust
benchmark, not a TypeScript terrain benchmark.

Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` if debug status fields
or renderer update packet shapes change. Document that transition meshes are
Rust-owned derived render geometry and that TypeScript does not decide apron
visibility.

## Concrete Steps

From `C:\dev\ofg`:

1. Add `crates/terrain_core/src/transition_mesh.rs`, export the intended public
   types from `crates/terrain_core/src/lib.rs`, and add
   `transition_mesh_tests.rs`.

2. Run:

       cargo test -p terrain_core transition_mesh
       cargo test -p terrain_core surface_query

3. Run `milestone-review` for Milestone 1, apply required findings, and update
   this plan.

4. Add `crates/engine_web/src/terrain_transitions.rs` and wire transition cache
   bookkeeping into `BrowserTerrainStream` without rendering.

5. Run:

       cargo test -p engine_web terrain_transition
       cargo test -p engine_web browser_terrain_stream

6. Run `milestone-review` for Milestone 2, apply required findings, and update
   this plan.

7. Add renderer upload/removal/draw support for transition meshes. Prefer
   extracting focused terrain renderer update helpers before adding more logic
   directly to `crates/engine_web/src/wgpu_renderer.rs`.

8. Run:

       cargo test -p engine_web
       npm run check:wasm
       npm run test:ts

9. Run `milestone-review` for Milestone 3, apply required findings, and update
   this plan.

10. Add or extend Rust smoke and benchmark coverage.

11. Run:

       npm run smoke:terrain-seams
       npm run smoke:rust
       npm run bench:terrain:rust

12. Before final completion, run:

       npm test
       npm run coverage:rust

   The filtered coverage output must not list modified implementation files
   below the 90% attention threshold unless this plan records an explicit
   exception with rationale.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in
   Progress or Outcomes & Retrospective.

## Validation and Acceptance

This plan is complete when the following observable criteria are true:

- A Rust API can derive a transition edge mesh from existing child and parent
  `MeshData` without calling terrain density sampling or node meshing.
- The canonical child terrain mesh is not mutated or regenerated when transition
  mesh visibility changes.
- Transition meshes use the standard 19-float terrain vertex layout and render
  through Rust/wgpu terrain resource ownership.
- Runtime transition meshes are optional, node-keyed derived geometry that can
  be toggled on when fine/coarse LOD boundaries are visible and removed when
  same-LOD coverage replaces them.
- Boundary-inclusive transition queries do not weaken half-open placement
  ownership.
- TypeScript does not calculate terrain boundaries, inspect transition mesh
  semantics, decide apron visibility, or create terrain render resources.
- Rust tests cover mesh construction, invalid input, deterministic face
  ordering, runtime cache toggling, and renderer upload/removal.
- Rust smoke includes a mixed-LOD seam scenario with transition meshes active.
- `npm test`, `npm run coverage:rust`, and relevant smoke/benchmark commands
  pass with no modified implementation files below the coverage attention
  threshold.

## Idempotence and Recovery

Transition meshes are derived from cached terrain node meshes. If generation
fails for one face, the runtime can skip that transition mesh and continue
rendering the normal visible cover. This should not invalidate the child mesh,
parent mesh, scheduler state, or terrain worker queues.

If renderer integration causes instability, disable transition mesh upload and
keep the Rust-only `terrain_core` construction tests. Do not revert to mutating
canonical child meshes or reintroducing TypeScript terrain ownership.

If the side-face strip approach cannot handle common topology, keep the runtime
cache/update shape but replace the builder with a fuller mesh-space band
replacement or seam-space Dual Contouring implementation. Record that change in
the Decision Log and keep the "no density remeshing for aprons" constraint
unless the user explicitly approves a larger terrain LOD architecture change.

## Artifacts and Notes

Expected artifact locations during implementation:

- Rust smoke images and reports under `artifacts/rust-smoke/`.
- Terrain benchmark reports under `artifacts/terrain-bench/`.
- Coverage summaries under `artifacts/coverage/rust/`.

Useful source files:

- `crates/terrain_core/src/mesh.rs`: canonical terrain `MeshData` emission.
- `crates/terrain_core/src/surface_query.rs`: polygonized vertical surface
  query index.
- `crates/terrain_core/src/stream.rs`: parent-before-child scheduling.
- `crates/engine_web/src/terrain_stream.rs`: runtime mesh cache, visible cover,
  and terrain update packets.
- `crates/engine_web/src/terrain_placement.rs`: example of building a derived
  mesh-backed packet from accepted terrain meshes.
- `src/engine/web/terrainBuildWorker.ts`: current browser worker boundary; this
  should remain unaware of transition semantics in the first implementation.

## Interfaces and Dependencies

The exact names can change during implementation, but the final design should
preserve these interfaces unless the Decision Log records a better reason.

`crates/terrain_core/src/transition_mesh.rs`:

    pub enum TerrainTransitionFace {
        NegX,
        PosX,
        NegZ,
        PosZ,
    }

    pub struct TerrainTransitionMeshKey {
        pub fine_key: TerrainNodeKey,
        pub parent_key: TerrainNodeKey,
        pub face: TerrainTransitionFace,
    }

    pub struct TerrainTransitionMeshConfig {
        pub max_vertical_search_meters: f64,
        pub min_normal_y: f64,
    }

    pub struct TerrainTransitionMeshInput<'a> {
        pub fine_key: TerrainNodeKey,
        pub parent_key: TerrainNodeKey,
        pub face: TerrainTransitionFace,
        pub fine_node_cell_size: f64,
        pub parent_node_cell_size: f64,
        pub fine_mesh: &'a MeshData,
        pub parent_mesh: &'a MeshData,
        pub config: TerrainTransitionMeshConfig,
    }

    pub fn build_parent_lod_transition_edge_mesh(
        input: TerrainTransitionMeshInput<'_>,
    ) -> Option<MeshData>;

`crates/engine_web/src/terrain_transitions.rs`:

    pub(crate) struct BrowserTerrainTransitionMeshUpdate {
        pub key: TerrainTransitionMeshKey,
        pub mesh: Arc<MeshData>,
    }

    pub(crate) fn required_transition_faces(
        visible_nodes: &BTreeSet<TerrainNodeKey>,
        generated_nodes: impl Fn(TerrainNodeKey) -> bool,
    ) -> Vec<TerrainTransitionMeshKey>;

Runtime debug status may include transition counts, but should not expose raw
transition mesh data to TypeScript.

## Revision Notes

- (2026-06-13 13:48+01:00) Initial draft. The plan intentionally adopts
  separate optional edge meshes derived from parent/child mesh buffers, replacing
  the earlier idea of modifying child meshes to conform to their parents.
