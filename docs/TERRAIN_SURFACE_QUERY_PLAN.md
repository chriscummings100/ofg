# Polygonized Terrain Surface Queries

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

Future foliage, rocks, placed props, gameplay collision probes, and eventually
mesh-aware player grounding need to ask a simple question: "where is the actual
polygonized terrain surface at this world XZ point?" The current Rust
`height_at_for_variant` query answers the analytic density surface, not the
rendered Dual Contouring triangle mesh. That mismatch is fine for today's player
grounding, but it is not good enough for foliage because trees and rocks must sit
on the same triangles the player sees.

After this plan is implemented, a terrain node build in Rust can generate its
mesh, build a compact query index over that mesh, and make repeated vertical
surface queries against the exact generated triangles. A later vegetation
generator can use the returned hit position, normal, material weights, biome
signals, and deterministic ownership rules to place instances without
re-sampling terrain in TypeScript or inventing a separate heightfield.

The first observable win is internal but concrete: Rust tests and benchmarks can
build a terrain node, query exact triangle hits from the generated mesh, and
prove placement samples lie on that mesh. When a first foliage or prop slice
arrives, it can consume the same Rust API inside terrain generation and expose
only Rust-owned placement counts or opaque instance packets to the browser.

## Progress

- [x] (2026-06-11 23:08+01:00) Researched the current OFG terrain ownership,
  mesh generation, worker build path, vegetation note, and external surface
  query references.
- [x] (2026-06-11 23:08+01:00) Drafted this implementation plan.
- [x] (2026-06-11 23:08+01:00) Added explicit surface-query test coverage for
  triangle interiors and exact vertex-XZ boundary hits.
- [x] (2026-06-12 00:12+01:00) Milestone 1 complete: added an exact vertical
  triangle surface query index in
  `terrain_core`.
- [x] (2026-06-12 00:20+01:00) Milestone 2 complete: integrated the index with
  node mesh build helpers and Rust terrain benchmark metrics.
- [x] (2026-06-12 00:28+01:00) Milestone 3 complete: added a deterministic
  Rust placement sampler that queries generated mesh surface indices.
- [x] (2026-06-12 00:52+01:00) Milestone 4 complete: wired Rust-owned placement
  sample packets into terrain stream status counters without giving TypeScript
  terrain ownership.
- [x] (2026-06-12 01:30+01:00) Final validation, coverage, docs, and
  retrospective complete.

## Surprises & Discoveries

- Observation: `crates/engine_web/src/game_state.rs` grounds the player through
  `height_at_for_variant`, which searches the analytic density function rather
  than the emitted triangles.
  Evidence: `terrain_height_at` calls `height_at_for_variant` at
  `crates/engine_web/src/game_state.rs`.
- Observation: `MeshData` in `crates/terrain_core/src/mesh.rs` is currently only
  render buffers: flat 19-float vertices plus u32 indices. There is no retained
  CPU-side spatial query structure.
  Evidence: `MeshData { vertices: Vec<f32>, indices: Vec<u32> }`.
- Observation: triangle material expansion duplicates vertices per triangle,
  which makes the first query index easier. Each indexed triangle can carry the
  same position, normal, and material payload that the renderer sees.
  Evidence: `expand_terrain_mesh_for_triangle_material_palettes` rewrites the
  mesh so each triangle owns its three expanded vertices.
- Observation: browser terrain workers currently return only mesh buffers and
  water bathymetry. Adding a reusable surface query structure to worker
  completions would waste bandwidth; the index should be built and consumed
  inside Rust terrain generation, then discarded unless a compact placement
  packet needs to be returned.
  Evidence: `src/engine/web/terrainBuildWorker.ts` copies `vertices`, `indices`,
  and optional `waterDepths` from `terrain_core.wasm`.
- Observation: the current sea-level water bathymetry still samples analytic
  terrain height. This plan does not have to change water, but the new surface
  query can become a later replacement if water needs to match polygonized
  overhangs or edited terrain.
  Evidence: `crates/terrain_core/src/water.rs` calls height/density helpers
  directly.
- Observation: the first surface-query implementation was close to the 600-line
  split-pressure threshold before review.
  Evidence: local milestone review measured `surface_query.rs` at 585 lines
  before extracting triangle extraction and barycentric/bin helpers to
  `crates/terrain_core/src/surface_query_geometry.rs`; the final files are 264,
  358, and 246 lines for API, geometry helpers, and tests.
- Observation: feature-unified workspace testing exposed shared-density-store
  interference in generated-node query tests.
  Evidence: the first final `npm test` run failed
  `benchmark_density_window_prepares_and_reuses_store_entries` because
  generated surface/placement tests mutated the same process-global density
  store concurrently; adding `test_lock()` to those generated-node tests made
  `cargo test -p terrain_core --features benchmark` and `npm test` pass.
- Observation: the first final coverage run timed out before returning and left
  a previous coverage target executable locked during the next cleanup attempt.
  Evidence: rerunning `npm run coverage:rust` with a longer timeout exited 0 and
  reported no filtered files below the 90% attention threshold; a process check
  immediately afterward showed no lingering Rust, cargo, LLVM, or OFG test
  processes.

## Decision Log

- Decision: start with a vertical-query index over mesh triangles, not a general
  arbitrary-ray BVH.
  Rationale: foliage placement will issue many local XZ-to-surface queries
  during one node or vegetation-cell build. A 2D XZ bin grid over the node's
  triangle projections has low build cost, simple deterministic behavior, and
  excellent locality. A general BVH remains a later extension for collision,
  bullets, picking, or arbitrary rays.
  Date/Author: 2026-06-11 / Codex.
- Decision: query the final expanded `MeshData`, not the density function and
  not a parallel heightfield.
  Rationale: the requirement is exact height of the polygonized terrain. The
  expanded mesh is the renderer-facing triangle soup, including current material
  palette expansion and Dual Contouring vertex placement.
  Date/Author: 2026-06-11 / Codex.
- Decision: return all vertical hits plus a convenience "highest acceptable
  ground hit" query.
  Rationale: today's terrain is mostly single-valued in XZ, but caves, arches,
  cliffs, and overhangs are natural future outcomes of a 3D density field. A
  multi-hit API avoids painting the system into a heightfield corner while still
  giving foliage the common top-ground query it wants.
  Date/Author: 2026-06-11 / Codex.
- Decision: include public position/normal arrays, material indices, material
  weights, node key, and triangle id in each hit rather than exposing
  `terrain_core`'s crate-private `Vec3` type.
  Rationale: foliage filters need slope and surface type, and debugging needs a
  way to trace a placement decision back to a concrete triangle. Keeping the
  hit payload explicit avoids turning the internal math module into a new public
  API surface as a side effect.
  Date/Author: 2026-06-11 / Codex.
- Decision: do not expose arbitrary terrain surface queries through playable
  TypeScript.
  Rationale: `OFG-API-009` forbids TypeScript terrain generation and sampling
  ownership. Runtime TypeScript may route opaque worker packets and display
  Rust-produced debug counts, but it must not query terrain surfaces or decide
  placement.
  Date/Author: 2026-06-11 / Codex.
- Decision: keep `TerrainSurfaceIndex` as a public Rust API but keep low-level
  triangle extraction, barycentric math, and bin helpers private to
  `terrain_core`.
  Rationale: placement and future gameplay Rust code need a stable exact query
  surface, while the implementation details should remain easy to change before
  broader runtime use.
  Date/Author: 2026-06-12 / Codex.

## Outcomes & Retrospective

Milestone 1 is implemented. `terrain_core` now exposes `TerrainSurfaceIndex`,
`TerrainVerticalQuery`, and `TerrainSurfaceHit` for exact vertical queries over
generated `MeshData`. The implementation builds a 32x32 XZ bin index, supports
multiple vertical hits sorted high-to-low, deduplicates shared-vertex hits at the
same height, returns material and normal payloads for placement filters, and
keeps all query ownership inside Rust.

Milestone 2 is implemented. `terrain_core` now has
`build_node_mesh_and_surface_for_variant`, which returns the generated mesh and
an optional `TerrainSurfaceIndex` over that exact mesh. The Rust terrain
benchmark report now records surface index build time, triangle count, total bin
references, max bin occupancy, deterministic query sample count, hit count,
mean query time, and p95 query time per measured LOD.

Milestone 3 is implemented. `terrain_core` now exposes
`TerrainPlacementSample`, `TerrainPlacementSamplingConfig`,
`TerrainPlacementSamplePacket`,
`terrain_placement_candidates_for_node`,
`sample_terrain_placements_from_candidates`, and
`build_node_surface_placement_samples_for_variant`. The sampler builds on the
polygonized surface index, rejects missed, underwater, steep, and vertically
degenerate candidates, preserves node half-open ownership through the surface
query, and produces stable coordinate-derived IDs for accepted samples.

Milestone 4 is implemented. `engine_web` now builds Rust-owned placement sample
packets from accepted terrain meshes on both synchronous and browser-worker
stream paths, caches the packets alongside mesh nodes, and reports aggregate
candidate/sample/reject counts through `terrainStreamStatus`. TypeScript only
types and displays Rust-provided counts; it does not query surfaces or decide
placement.

Final validation is complete. The new surface query, placement sampler, runtime
status counters, generated WASM artifacts, API contracts, architecture notes, and
vegetation research notes are all updated in one vertical slice. The final
coverage run reports no modified implementation files below the Rust coverage
attention threshold. The only remaining follow-up is the recorded
`wgpu_renderer.rs` extraction risk for future debug/status serialization
expansion; it is not required for this feature.

## Contract and Quality Baseline

This plan preserves the current API ownership rules:

- `OFG-API-001`: browser code continues to use `RustBrowserGame.create`,
  `resize`, `tick`, `command`, and `debugSnapshot`. Any future debug counts for
  placement queries must be Rust-assembled snapshot data.
- `OFG-API-003`: debug hooks may show query/placement counts, rejected-surface
  counts, or sample diagnostics, but may not compute terrain hits in
  TypeScript.
- `OFG-API-004`: the terrain vertex layout remains 19 `f32` values per vertex.
  The query code reads that layout; it must not change stride, offsets, or WGSL
  shader contracts unless a separate milestone updates every owner and test.
- `OFG-API-006`: the standalone `terrain_core.wasm` artifact remains a fixture
  and worker-build artifact. If this plan adds raw exports, they are only for
  worker-internal opaque terrain jobs or fixture tests, and `docs/API_CONTRACTS.md`
  must be updated in the same milestone.
- `OFG-API-009`: TypeScript must not regain terrain generation, density
  sampling, mesh generation, surface querying, foliage placement, stream
  scheduling, or WebGPU terrain ownership.

Every implementation milestone must run the repo-local `milestone-review` skill
before it is marked complete. Every implementation milestone must also satisfy
the Rust coverage attention gate for modified implementation files by running
`npm run coverage:rust` before final completion, unless this plan records an
explicit exception with rationale.

## Context and Orientation

Current terrain nodes are generated in Rust. `crates/terrain_core/src/mesh.rs`
builds a 32x32x32-cell Dual Contouring mesh for a node, using neighbor density
chunks so same-LOD boundaries are deterministic. `build_node_mesh_for_variant`
derives the node's effective cell size from `TerrainNodeKey.lod` and then calls
the chunk mesher.

Renderable terrain vertices have 19 floats. In order, the fields are position
XYZ, color RGB, normal XYZ, UV, four material layer indices, and four material
weights. The current constants are private to `terrain_core`:
`FLOATS_PER_VERTEX`, `MATERIAL_INDICES_VERTEX_OFFSET`, and
`MATERIAL_WEIGHTS_VERTEX_OFFSET`.

`crates/engine_web/src/terrain_stream.rs` owns browser terrain streaming inside
Rust. On the synchronous path, `complete_node_job` builds the mesh and water
packet directly. On the browser worker path, Rust issues
`BrowserTerrainBuildRequest`, TypeScript routes the opaque request into
`terrain_core.wasm`, and Rust validates `BrowserTerrainBuildCompletion` before
caching the mesh and submitting renderer updates.

`docs/VEGETATION_RESEARCH.md` already says vegetation placement should query
terrain surface height, normal, material, biome, wetness, and future exclusion
masks in Rust. This plan fills the missing prerequisite: an exact surface query
against the polygonized mesh, not a scalar terrain height helper.

Important definitions:

- A terrain node is one generated or empty terrain chunk at a specific LOD and
  3D coordinate, represented by `TerrainNodeKey`.
- Polygonized terrain means the final triangle mesh produced by Dual Contouring
  and sent to the renderer.
- A vertical surface query casts along the world Y axis at fixed world XZ.
- A surface hit is one triangle intersection with interpolated placement data.
- A placement sampler is deterministic Rust generation code that asks the
  surface query for candidate positions and returns accepted samples or future
  instance records.

## Research Summary

Dual Contouring is the right mesh source to query because it extracts a surface
from a signed grid using Hermite edge intersections and normals. The OFG mesher
already follows this shape, so querying the emitted triangles is the most direct
way to match the rendered surface. Source:
https://www.cs.wustl.edu/~taoju/research/dualContour.pdf

Moller-Trumbore ray/triangle intersection is a standard low-storage way to get
ray distance plus barycentric coordinates. For vertical terrain placement, a
specialized XZ-projection barycentric test can compute the same hit position
more cheaply, while preserving barycentric interpolation for normals and
materials. Source:
https://cadxfem.org/inf/Fast%20MinimumStorage%20RayTriangle%20Intersection.pdf

BVHs and AABB trees are the right general reference for arbitrary ray and
distance queries against triangle sets. PBRT describes BVHs as primitive
subdivision where each node stores a bounding box and missed node bounds skip
whole subtrees. CGAL's AABB tree shows the same static-triangle use case for
intersection and distance queries. These are excellent future references, but
the first OFG placement query is narrower than a general ray tracer. Sources:
https://www.pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies
and https://doc.cgal.org/latest/AABB_tree/index.html

Production vegetation systems reinforce that placement needs more than height.
O3DE surface data exposes surface signals and tags for vegetation alignment and
inclusion/exclusion, while Unreal's foliage mode explicitly paints foliage on
surfaces of geometry. OFG should return enough hit data to support those masks
later, but keep the source and interpretation in Rust. Sources:
https://docs.o3de.org/docs/user-guide/gems/reference/environment/surface-data/
and https://dev.epicgames.com/documentation/unreal-engine/foliage-mode-in-unreal-engine

## Plan of Work

Milestone 1 adds the core query structure in `terrain_core`. Create
`crates/terrain_core/src/surface_query.rs` with a top comment explaining that it
builds CPU-side surface query data from rendered terrain meshes. Add public
types similar to:

    pub struct TerrainSurfaceIndex { ... }

    pub struct TerrainVerticalQuery {
        pub x: f64,
        pub z: f64,
        pub min_y: f64,
        pub max_y: f64,
        pub min_normal_y: f64,
    }

    pub struct TerrainSurfaceHit {
        pub node_key: TerrainNodeKey,
        pub triangle_index: u32,
        pub position: [f64; 3],
        pub geometric_normal: [f32; 3],
        pub shading_normal: [f32; 3],
        pub material_indices: [u8; 4],
        pub material_weights: [f32; 4],
    }

`TerrainSurfaceIndex::from_mesh(key, node_cell_size, &mesh)` computes the node
XZ bounds, extracts triangles from the same `MeshData` sent to the renderer,
skips malformed or degenerate triangles, and bins triangle ids into a fixed 2D
grid over XZ. Start with 32x32 bins for LOD0-sized nodes because this matches
the current terrain cell count and keeps bin lookup simple. For coarser LODs,
keep the same bin count per node; the bins simply cover larger world-space
areas.

The query algorithm maps world XZ to a bin, tests only the triangle ids in that
bin, solves the vertical ray/triangle hit with projected barycentric
coordinates, rejects hits outside `min_y..=max_y`, rejects ground queries below
`min_normal_y`, and returns hits sorted from highest Y to lowest Y. It should
also provide `highest_vertical_hit(query)` as a convenience wrapper. Boundary
checks should use half-open node XZ bounds, with a small epsilon only for
floating-point robustness, so adjacent nodes do not both claim a candidate on a
shared edge.

Unit tests should use tiny handcrafted meshes first, then generated terrain
meshes. The handcrafted cases must include rays through polygon interiors and
boundary cases such as a vertical ray whose XZ position exactly matches an
existing triangle vertex. Required behavior-focused tests:

- `surface_index_returns_exact_height_on_sloped_triangle`
- `surface_index_hits_triangle_vertex_xz_without_duplicate_or_missed_hit`
- `surface_index_sorts_multiple_vertical_hits_from_high_to_low`
- `surface_index_rejects_degenerate_vertical_projections`
- `surface_index_interpolates_normals_and_material_weights`
- `surface_index_uses_half_open_node_bounds_for_edge_ownership`
- `generated_node_surface_hits_lie_on_indexed_triangles`

Milestone 2 integrates the index with node build helpers without changing
browser behavior. Add a Rust helper such as:

    pub struct TerrainNodeBuildSurface {
        pub mesh: MeshData,
        pub surface: Option<TerrainSurfaceIndex>,
    }

    pub fn build_node_mesh_and_surface_for_variant(
        seed: u32,
        descriptor: TerrainVariantDescriptor,
        key: TerrainNodeKey,
        base_cell_size: f64,
    ) -> TerrainNodeBuildSurface

Keep existing `build_node_mesh_for_variant` available for current callers. Use
the new helper in targeted Rust tests and benchmarks first, not in
`BrowserTerrainStream` unless the milestone needs runtime timings. Extend the
Rust terrain benchmark to record index build time, triangle count, total bin
references, max bin occupancy, average query time, and p95 query time for a
fixed deterministic set of candidate XZ points.

Milestone 3 adds the first placement-adjacent consumer inside Rust generation.
This is not full foliage rendering. Add a deterministic placement sampler that
takes a generated node mesh/index plus candidate XZ points and returns compact
surface samples:

    pub struct TerrainPlacementSample {
        pub position: [f32; 3],
        pub normal: [f32; 3],
        pub material_indices: [u8; 4],
        pub material_weights: [f32; 4],
        pub stable_id: u64,
    }

The sampler should generate or accept deterministic candidates, query the
surface index, reject samples below water or above a slope threshold, and keep
only samples owned by the node's half-open XZ bounds. Tests must prove same
seed/key gives identical samples, neighboring nodes do not duplicate boundary
samples, steep/vertical triangles are rejected, and accepted samples exactly
match the mesh query hit they came from.

Milestone 4 wires the first runtime-facing placement/debug path, still without
rendering full foliage unless a separate foliage plan has reached that point.
The smallest useful runtime proof is a Rust-owned placement packet or debug
counter generated in the same terrain worker job that builds the mesh. If this
touches the browser worker, TypeScript should only copy opaque typed arrays or
counts, the way it currently copies mesh and water buffers. Rust should validate
request id, generation, node key, and variant revision exactly as it does for
mesh completions.

Possible runtime additions:

- `TerrainNodeGenerationPacket` in Rust with `mesh`, `water`, and
  `surface_samples` or future `placement_instances`.
- Optional raw `terrain_core.wasm` fixture exports for worker-internal sample
  buffers, documented under `OFG-API-006`.
- `BrowserTerrainStreamStatus` counters such as `placementSampleCount`,
  `surfaceQueryTriangleCount`, or `surfaceQueryRejectedCount`, exposed through
  Rust `debugSnapshot()` only if they are useful for smoke/perf reports.

Update `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and
`docs/VEGETATION_RESEARCH.md` in the milestone that adds any runtime-facing
packet or debug field.

## Concrete Steps

From `C:\dev\ofg`:

1. Add `crates/terrain_core/src/surface_query.rs` and export only the intended
   public surface query types from `crates/terrain_core/src/lib.rs`.
2. Add focused unit tests in `crates/terrain_core/src/tests.rs` or split them
   into a new `surface_query_tests.rs` if file size pressure increases.
3. Run:

       cargo test -p terrain_core surface_query
       npm run test:rust

4. Run `milestone-review` for Milestone 1, apply required findings, and record
   the evidence in this plan.
5. Add `build_node_mesh_and_surface_for_variant` and benchmark metrics.
6. Run:

       cargo test -p terrain_core
       cargo test -p ofg_test_harness terrain_bench
       npm run bench:terrain:rust

7. Run `milestone-review` for Milestone 2 and record report paths from
   `artifacts/terrain-bench/`.
8. Add the placement sampler and tests.
9. Run:

       cargo test -p terrain_core placement
       npm run test:rust

10. Run `milestone-review` for Milestone 3.
11. If runtime packets/debug fields are added, update contracts/docs and run:

       npm run test:ts
       npm run check:wasm
       npm run smoke:browser

12. Before final completion, run:

       npm test
       npm run coverage:rust

   The filtered coverage output must not list modified implementation files
   below the attention threshold, currently about 90% line coverage, unless this
   plan records an explicit exception with rationale.

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

- Rust can build a `TerrainSurfaceIndex` from generated terrain `MeshData`.
- Vertical queries return exact positions on the polygonized triangles, not the
  analytic `height_at_for_variant` surface.
- Query hits include enough placement data for foliage filters: geometric
  normal, interpolated normal, material indices, material weights, node key, and
  triangle id.
- Multiple vertical hits are supported and sorted, so future caves and overhangs
  do not require replacing the API.
- Boundary ownership is deterministic across neighboring terrain nodes.
- A Rust placement sampler can call the query index during node generation and
  produce deterministic mesh-backed placement samples.
- Browser TypeScript does not query terrain, classify terrain, or decide
  placement. Any worker changes move only Rust-authored opaque packets.
- Benchmarks report index build time and query timing for realistic generated
  nodes.
- `npm test`, relevant smoke tests for runtime packet changes, and
  `npm run coverage:rust` pass with no modified implementation files below the
  coverage attention threshold.

## Idempotence and Recovery

The first milestones are additive. If the query index has correctness or
performance problems, remove use of `build_node_mesh_and_surface_for_variant`
from runtime callers and keep the existing `build_node_mesh_for_variant` path.
If worker packet changes cause browser instability, revert only the packet
extension and keep the Rust-only query/index tests. Do not restore TypeScript
terrain sampling or a browser-owned terrain query helper as a fallback.

The query index is derived from `MeshData`, so it never needs to be serialized
or retained across resets to preserve terrain correctness. Runtime caches can
drop it at any time and rebuild it from the deterministic mesh generation path.

## Artifacts and Notes

Expected artifact locations during implementation:

- Rust terrain benchmark reports under `artifacts/terrain-bench/`.
- Browser smoke screenshots and reports under `artifacts/browser-smoke/` if
  runtime packets/debug fields are added.
- Rust coverage summaries under `artifacts/coverage/rust/`.

Source links used while drafting this plan:

- Dual Contouring of Hermite Data:
  https://www.cs.wustl.edu/~taoju/research/dualContour.pdf
- Fast Minimum Storage Ray/Triangle Intersection:
  https://cadxfem.org/inf/Fast%20MinimumStorage%20RayTriangle%20Intersection.pdf
- PBRT Bounding Volume Hierarchies:
  https://www.pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies
- CGAL AABB Tree:
  https://doc.cgal.org/latest/AABB_tree/index.html
- O3DE Surface Data:
  https://docs.o3de.org/docs/user-guide/gems/reference/environment/surface-data/
- Unreal Foliage Mode:
  https://dev.epicgames.com/documentation/unreal-engine/foliage-mode-in-unreal-engine

When milestones complete, paste concise evidence here: command names, pass/fail
summary, relevant report paths, and any important timing notes.

Milestone 1 evidence:

- Added `crates/terrain_core/src/surface_query.rs`,
  `crates/terrain_core/src/surface_query_geometry.rs`, and
  `crates/terrain_core/src/surface_query_tests.rs`; exported only
  `TerrainSurfaceIndex`, `TerrainVerticalQuery`, and `TerrainSurfaceHit` from
  `crates/terrain_core/src/lib.rs`.
- `cargo test -p terrain_core surface_query`: passed. Seven tests cover polygon
  interior height, exact vertex-XZ hit deduplication, multiple vertical hits,
  degenerate XZ projections, normal/material interpolation, half-open node
  ownership, and generated terrain mesh centroid queries.
- `npm run test:rust`: passed after final code shape; Rust workspace tests pass.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows
  line-ending warnings only.

Milestone 1 review:

- Scope: Rust-only exact vertical surface query index for generated terrain
  meshes in `terrain_core`, public Rust exports, focused tests, and this
  ExecPlan update.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because this was the
  plan-required review rather than an explicit user request for delegated
  reviewers.
- Required findings fixed: split the initial 585-line `surface_query.rs` into a
  264-line API module and a 358-line private geometry helper module; added
  explicit docs to the public Rust query API and helper functions; retained
  half-open node ownership with epsilon only inside triangle projection bounds.
- Follow-ups recorded: no follow-ups beyond the remaining planned milestones.
- Rejected findings: no rejected findings.
- Remaining risk: this milestone does not yet attach the surface index to the
  terrain node build helpers, benchmarks, placement sampler, or runtime worker
  packets.

Milestone 2 evidence:

- Added `crates/terrain_core/src/mesh_surface.rs`; exported
  `TerrainNodeBuildSurface` and `build_node_mesh_and_surface_for_variant` from
  `crates/terrain_core/src/lib.rs`.
- Extended `crates/ofg_test_harness/src/terrain_bench_lod.rs` so the
  multi-LOD benchmark JSON includes surface index build time, triangle count,
  bin reference count, max bin occupancy, and deterministic vertical query
  timing fields.
- `cargo test -p terrain_core build_node_mesh_and_surface`: passed.
- `cargo test -p terrain_core`: passed. Seventy-two terrain core tests passed.
- `cargo test -p ofg_test_harness terrain_bench`: passed. Eleven benchmark
  harness tests passed; pre-existing `engine_web` dead-code warnings remained.
- `npm run bench:terrain:rust`: passed and wrote
  `artifacts/terrain-bench/run-1781219923-206/report.json`. In that run,
  reported per-LOD surface index build times ranged from 0.5304ms to 0.8898ms,
  with p95 vertical query timings from 0.0019ms to 0.0231ms.
- `cargo test -p terrain_core surface_query`: passed after the milestone review
  header fix.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows
  line-ending warnings only.

Milestone 2 review:

- Scope: Rust-only mesh-plus-surface helper, terrain benchmark surface metrics,
  focused tests, benchmark artifact inspection, and this ExecPlan update.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because this was the
  plan-required review rather than an explicit user request for delegated
  reviewers.
- Required findings fixed: added a top-of-file purpose comment to
  `surface_query_tests.rs`; removed an unnecessary `Debug` derive from
  `TerrainNodeBuildSurface` instead of expanding `MeshData` traits.
- Follow-ups recorded: no follow-ups beyond the remaining planned milestones.
- Rejected findings: no rejected findings.
- Remaining risk: benchmark metrics build and query the surface index in the
  harness, but runtime jobs still do not retain or report placement/sample
  packets until Milestone 3 and Milestone 4.

Milestone 3 evidence:

- Added `crates/terrain_core/src/placement.rs`; exported placement sample,
  config, packet, candidate generation, candidate-fed sampling, and
  node-generation sampling APIs from `crates/terrain_core/src/lib.rs`.
- Added `crates/terrain_core/src/placement_tests.rs`. Eleven tests cover
  deterministic sampling for the same seed/key, order-independent stable IDs,
  exact boundary ownership between neighboring nodes, steep-surface rejection,
  vertically degenerate projection handling, below-water rejection, exact hit
  payload preservation, invalid config diagnostics, deterministic sampling from
  a generated node, the default public builder, no-surface builder fallback, and
  invalid candidate grid/node-bound guards.
- `cargo test -p terrain_core placement`: passed. Eleven placement tests passed.
- `cargo test -p terrain_core`: passed. Eighty terrain core tests passed.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows
  line-ending warnings only.

Milestone 3 review:

- Scope: Rust-only placement sampling consumer over `TerrainSurfaceIndex`,
  placement public exports, focused placement tests, and this ExecPlan update.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because this was the
  plan-required review rather than an explicit user request for delegated
  reviewers.
- Required findings fixed: removed the separate node-key parameter from
  `sample_terrain_placements_from_candidates` by adding
  `TerrainSurfaceIndex::node_key()`; changed stable sample IDs to derive from
  candidate coordinates rather than candidate array order; preserved candidate
  counts when rejecting invalid sampling configs.
- Follow-ups recorded: no follow-ups beyond the remaining planned milestones.
- Rejected findings: no rejected findings.
- Remaining risk: the sampler is still Rust-only and does not yet travel through
  browser worker completions or debug snapshots until Milestone 4.

Milestone 4 evidence:

- Added `crates/engine_web/src/terrain_placement.rs` and
  `crates/engine_web/src/terrain_removal.rs`; kept
  `crates/engine_web/src/terrain_stream.rs` below the 1000-line concern
  threshold after review by extracting the existing deferred-removal helper.
- `BrowserTerrainStream` now caches `TerrainPlacementSamplePacket` values for
  accepted meshes and aggregates `placementCandidateCount`,
  `placementSampleCount`, `placementMissedSurfaceCount`,
  `placementRejectedBelowWaterCount`, and `placementRejectedSlopeCount` in
  `terrainStreamStatus`.
- Updated `src/engine/web/browserGameTypes.ts` and TypeScript debug snapshot
  fixtures for the Rust-owned placement counters. No TypeScript surface query,
  terrain classification, or placement decision code was added.
- Updated `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and
  `docs/VEGETATION_RESEARCH.md` for the new debug counters and ownership.
- `cargo test -p engine_web browser_terrain_stream`: passed after final module
  extraction. Eleven focused terrain stream tests passed.
- `cargo test -p engine_web terrain_removal`: passed. Two extracted removal
  helper tests passed.
- `cargo test -p engine_web`: passed before the final extraction; 164 tests
  passed. The final extraction was then covered by the focused stream/removal
  tests and will be covered again in final `npm test`.
- `npm run test:ts`: passed before the final extraction. TypeScript source
  shape did not change afterward beyond regenerated WASM metadata; final
  `npm test` will cover it again.
- `npm run check:wasm`: passed after regenerating `engine_web` WASM artifacts.
- `npm run smoke:browser`: passed after final WASM regeneration. Browser smoke
  artifacts: `artifacts/browser-smoke/2026-06-11T23-50-23-583Z/`.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows
  line-ending warnings only.

Milestone 4 review:

- Scope: Rust-owned placement packet caching/counting in `engine_web`, debug
  status serialization, TypeScript status typing/fixtures, generated WASM
  artifacts, ownership docs, and this ExecPlan update.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent tools were not used because this was the
  plan-required review rather than an explicit user request for delegated
  reviewers.
- Required findings fixed: extracted placement diagnostics into
  `terrain_placement.rs`; extracted existing terrain removal helpers/tests into
  `terrain_removal.rs`; kept `terrain_stream.rs` at 969 lines after review;
  regenerated stale `engine_web` WASM artifacts after the extraction.
- Follow-ups recorded: `docs/API_CONTRACTS.md` now says the next expansion of
  debug/status serialization should extract terrain/renderer JS status builders
  from oversized `wgpu_renderer.rs` instead of adding fields directly there.
- Rejected findings: no rejected findings.
- Remaining risk: placement counters are currently diagnostics only; no foliage
  instance packet, renderer path, or gameplay interaction consumes them yet.

Final validation evidence:

- `cargo test -p terrain_core placement`: passed. Eleven placement tests passed.
- `cargo test -p terrain_core surface_query`: passed. Nine filtered tests passed,
  including polygon-interior, exact vertex-XZ, generated mesh, and placement
  payload cases.
- `cargo test -p terrain_core --features benchmark`: passed. Eighty-eight tests
  passed, including the benchmark feature-unified lane that previously exposed
  shared density store interference.
- `npm run coverage:rust`: passed on rerun with a longer timeout. The first
  timed-out run left a cleanup lock warning on the next run, but the successful
  coverage output reported `files below 90% line coverage ... none`, with
  `artifacts/coverage/rust/summary.pretty.json` showing `reportedFileCount: 0`.
- `npm test`: passed on the current tree. Rust workspace tests passed, the TS
  lane rebuilt WASM artifacts, and Mocha reported 115 passing TypeScript tests.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows
  line-ending warnings only.

## Interfaces and Dependencies

The exact names can change during implementation, but the final design should
preserve these interfaces unless the Decision Log records a better reason.

`crates/terrain_core/src/surface_query.rs`:

    pub struct TerrainSurfaceIndex {
        key: TerrainNodeKey,
        node_origin_x: f64,
        node_origin_z: f64,
        node_span: f64,
        bins_per_axis: u16,
        bin_offsets: Vec<u32>,
        bin_triangle_indices: Vec<u32>,
        triangles: Vec<TerrainSurfaceTriangle>,
    }

    pub struct TerrainVerticalQuery {
        pub x: f64,
        pub z: f64,
        pub min_y: f64,
        pub max_y: f64,
        pub min_normal_y: f64,
    }

    pub struct TerrainSurfaceHit {
        pub node_key: TerrainNodeKey,
        pub triangle_index: u32,
        pub position: [f64; 3],
        pub geometric_normal: [f32; 3],
        pub shading_normal: [f32; 3],
        pub material_indices: [u8; 4],
        pub material_weights: [f32; 4],
    }

    impl TerrainSurfaceIndex {
        pub fn from_mesh(
            key: TerrainNodeKey,
            node_cell_size: f64,
            mesh: &MeshData,
        ) -> Option<Self>;

        pub fn vertical_hits(&self, query: TerrainVerticalQuery) -> Vec<TerrainSurfaceHit>;

        pub fn highest_vertical_hit(
            &self,
            query: TerrainVerticalQuery,
        ) -> Option<TerrainSurfaceHit>;
    }

`crates/terrain_core/src/mesh.rs` or a new build-output module:

    pub struct TerrainNodeBuildSurface {
        pub mesh: MeshData,
        pub surface: Option<TerrainSurfaceIndex>,
    }

    pub fn build_node_mesh_and_surface_for_variant(
        seed: u32,
        descriptor: TerrainVariantDescriptor,
        key: TerrainNodeKey,
        base_cell_size: f64,
    ) -> TerrainNodeBuildSurface;

Future placement consumer:

    pub struct TerrainPlacementSample {
        pub stable_id: u64,
        pub position: [f32; 3],
        pub normal: [f32; 3],
        pub material_indices: [u8; 4],
        pub material_weights: [f32; 4],
    }

    pub fn build_node_surface_placement_samples_for_variant(
        seed: u32,
        descriptor: TerrainVariantDescriptor,
        key: TerrainNodeKey,
        base_cell_size: f64,
    ) -> TerrainPlacementSamplePacket;

These types should remain Rust-owned. If JavaScript worker glue needs to copy a
future packet, it copies typed arrays by request id and node key without
interpreting placement semantics.
