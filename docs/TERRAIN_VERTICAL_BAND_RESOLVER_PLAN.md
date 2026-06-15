# Player-Bounded Vertical Terrain Band Resolver

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

OFG currently streams terrain in a sparse multi-LOD X/Z window, but each LOD uses
fixed vertical chunk offsets relative to the player. That is enough for the
current heightfield-like terrain, but it will limit taller mountains, deep
caves, elevated lakes, and future volumetric terrain features. The purpose of
this plan is to replace fixed vertical offsets with a tested vertical band
resolver.

After this change, the terrain scheduler can decide which `y` nodes to generate
for each `(lod, x, z)` column by combining two facts:

1. a conservative terrain-interest range for that column, meaning the vertical
   range where terrain, water, caves, or other terrain-owned features could
   exist; and
2. a player-centered vertical generation window for that LOD, meaning the amount
   of vertical detail worth generating near the player at that resolution.

The generated range is the intersection of those two ranges, expanded only for
meshing dependencies and parent coverage. This lets a column know that possible
terrain may span, for example, `y = -20..30`, while LOD0 only generates the
slice near the player's current vertical node. Coarser LODs can use wider
windows so distant mountains remain visible without generating the full high
detail vertical column.

The observable result is that high-relief test terrain can have a much wider
estimated vertical envelope, while the default stream still keeps LOD0 node
counts bounded near the player. Debug snapshots, native smoke reports, and Rust
benchmarks should show per-LOD vertical ranges and node counts that change when
the player climbs or descends.

## Progress

- [x] (2026-06-14 11:18+01:00) Drafted this ExecPlan from the user-proposed
  column range intersection model: possible terrain `y` range intersected with
  a per-LOD player vertical window.
- [x] (2026-06-14 11:31+01:00) Milestone 1: added pure vertical range and
  column bounds types in
  `terrain_core`, with exhaustive unit tests for range math and negative
  coordinates. Validation: `cargo test -p terrain_core vertical_band` passed
  with 11 tests; `cargo fmt` ran; `git -c safe.directory=C:/dev/ofg diff
  --check` passed with only Git's existing line-ending normalization warning for
  `crates/terrain_core/src/lib.rs`. Milestone review ran locally across contract,
  code quality, legacy, correctness, and validation passes because sub-agent
  tooling requires an explicit user request for delegation; no required findings
  were found.
- [x] (2026-06-14 11:38+01:00) Milestone 2: added a conservative terrain
  vertical bounds estimator for the current heightfield-like terrain shape.
  Validation: `cargo test -p terrain_core vertical_band` passed with 20 tests;
  `cargo test -p terrain_core terrain_variant` passed with 8 tests;
  `cargo fmt` ran; `git -c safe.directory=C:/dev/ofg diff --check` passed with
  only Git's existing line-ending normalization warning for
  `crates/terrain_core/src/lib.rs`. Milestone review ran locally across
  contract, code quality, legacy, correctness, and validation passes; the only
  required finding was a misleading doc comment on
  `terrain_node_column_xz_bounds`, which was fixed and revalidated.
- [x] (2026-06-14 12:07+01:00) Milestone 3: integrated the resolver into
  `TerrainStreamScheduler` so
  desired nodes are built from X/Z bands plus resolved per-column Y ranges.
  Validation: `cargo test -p terrain_core stream_scheduler` passed with 20
  tests; `cargo test -p terrain_core vertical_band` passed with 20 tests;
  `cargo test -p engine_web tests::browser_terrain_stream_queues_worker_requests_without_sync_building`
  passed; `cargo test -p engine_web
  tests::browser_terrain_stream_generates_unique_mesh_keys_across_lods` passed;
  `cargo test -p engine_web
  tests::browser_terrain_stream_generates_and_prunes_meshes_in_rust` passed;
  `cargo test -p engine_web browser_terrain_stream --no-run` passed; no-run
  harness compiles for `render_smoke` and `terrain_bench_lod` passed. The
  old-default `browser_terrain_stream_default_bands_render_multiple_lods_after_settling`
  test timed out when run alone under the current fixed-offset defaults; this is
  recorded as a Milestone 4 validation target after default bands move to bounded
  policies. Milestone review ran locally; required finding fixed:
  `TerrainLodBoundedVerticalPolicy::new` now rejects negative windows by
  returning `Option<Self>`.
- [x] (2026-06-14 12:23+01:00) Milestone 4: updated `engine_web` default LOD settings, debug snapshots,
  smoke reports, and benchmarks to prove bounded high-detail vertical streaming.
  Default browser LOD bands now use bounded vertical policies, while the LOD0
  fixture helper and standalone facade retain fixed offsets. Debug snapshots,
  Rust smoke reports, and multi-LOD benchmark reports now include per-LOD
  desired min/max node Y. Added a high-relief multi-LOD smoke scenario named
  `vertical-band-high-relief`. Validation: `cargo test -p engine_web
  browser_terrain_stream` passed with 12 tests; `cargo test -p ofg_test_harness
  render_smoke` passed with 24 tests; `cargo test -p ofg_test_harness
  terrain_bench_lod` passed with 3 tests; `npm run test:ts` passed with 115
  tests; `npm run check:wasm` passed after regenerating WASM artifacts with
  `npm run build:wasm`; `git -c safe.directory=C:/dev/ofg diff --check` passed
  with only Git line-ending normalization warnings. A parallel
  `terrain_bench_lod` run initially failed with Windows linker error LNK1104
  because another harness test executable held the target path; rerunning the
  lane by itself passed. Milestone review ran locally; no required code findings
  remained. Follow-up for Milestone 5: active docs still contain fixed-offset
  examples that need replacing with bounded vertical policy text.
- [x] (2026-06-14 13:16+01:00) Milestone 5: ran full validation, coverage,
  active documentation updates, and final milestone review. Validation:
  `npm test` passed; `npm run smoke:rust` passed and wrote
  `artifacts/rust-smoke/run-1781437118-487/report.json`, including
  `vertical-band-high-relief.png`; `npm run smoke:browser` passed and wrote
  `artifacts/browser-smoke/2026-06-14T11-49-06-060Z`; `npm run
  bench:terrain:rust` passed and wrote
  `artifacts/terrain-bench/run-1781437861-041/report.json`; `npm run
  smoke:terrain-seams` passed and wrote
  `artifacts/rust-smoke/run-1781438355-080/report.json`; `npm run
  check:wasm` passed; `npm run coverage:rust` passed. Coverage still reports
  the pre-existing, unmodified `crates/terrain_core/src/surface_query.rs` below
  the default 90% attention threshold, but no modified implementation file is
  listed. `cargo fmt --check` and `git -c safe.directory=C:/dev/ofg diff
  --check` passed; `diff --check` reported only Git line-ending normalization
  warnings. Final milestone review ran locally across contract, code quality,
  legacy, correctness, and validation passes. Required findings fixed:
  `vertical_band` tests were split into `vertical_band_tests.rs` to keep the
  implementation module small, and a stale benchmark helper comment was updated
  to say it intentionally uses fixed-offset profile bands. Revalidation after
  those review fixes: `cargo test -p terrain_core vertical_band` passed with 20
  tests; `cargo fmt --check` and `git diff --check` passed.

## Surprises & Discoveries

- Observation: the current stream already has a 3D node model, but desired node
  selection uses fixed vertical offsets for every X/Z column in a band.
  Evidence: `crates/terrain_core/src/stream.rs` loops over
  `TerrainLodBand.vertical_chunk_offsets` inside `build_desired_nodes`.
- Observation: current runtime defaults use the same near vertical offsets for
  LOD0, LOD1, and LOD2, and a smaller far vertical set for LOD3 and LOD4.
  Evidence: `crates/engine_web/src/terrain_stream.rs` defines
  `DEFAULT_TERRAIN_VERTICAL_OFFSETS = [-2, -1, 0, 1]` and
  `DEFAULT_TERRAIN_FAR_VERTICAL_OFFSETS = [-1, 0]`.
- Observation: the real-scale preset follow-up first extended the bounded
  browser defaults to LOD5 and LOD6, then trimmed the current playable default
  back to LOD5 once fog tuning established a 3 km effective horizon.
  Evidence: `docs/TERRAIN_REAL_SCALE_PRESETS_AND_FAR_LOD_PLAN.md` records the
  LOD6 proof pass and the later LOD5/7000m generated-span target.
- Observation: the current terrain density is still heightfield-biased even
  though the mesher is true 3D Dual Contouring over density chunks.
  Evidence: `crates/terrain_core/src/field.rs` computes density as
  `position.y - macro_sample.base_elevation - detail.value *
  preset.detail_amplitude`.
- Observation: the original compatibility height search was bounded to
  `-96m..96m`, but the real-scale terrain follow-up removed that absolute
  clamp.
  Evidence: `height_at_with_shape` now brackets the surface around
  `sample_macro_terrain` instead of walking between `SURFACE_SEARCH_MIN_Y` and
  `SURFACE_SEARCH_MAX_Y`.
- Observation: each terrain node remains a 32x32x32 cell chunk at its LOD's
  world cell size.
  Evidence: `TERRAIN_CHUNK_CELLS_PER_AXIS = 32` and
  `terrain_node_cell_size(base_cell_size, lod)` doubles cell size by LOD.
- Observation: exact maximum/minimum height over a continuous procedural noise
  footprint is not available from the current noise functions without interval
  arithmetic or a more specialized bound system.
  Evidence: current height helpers sample and refine density at points; no
  module exposes analytic extrema for fractal, ridged, cellular, or warped
  noise over an X/Z rectangle.
- Observation: the first vertical range helpers can stay independent of terrain
  shape, scheduler state, or browser runtime data.
  Evidence: `crates/terrain_core/src/vertical_band.rs` now tests finite
  world-meter ranges, inclusive node ranges, player windows, column keys,
  negative coordinates, LOD-scaled spans, and boundary-touching conversions
  without importing noise, stream, or mesh modules.
- Observation: the current height helper can be reused by the first estimator,
  but its sampled result still needs extra shape padding because the noise
  modules do not expose continuous extrema over an X/Z rectangle.
  Evidence: `estimate_terrain_column_world_y_range` samples
  `height_at_with_shape` over the node column footprint, then expands by shape
  scale padding plus explicit surface and feature margins.
- Observation: once the scheduler stores terrain seed, variant, and base cell
  size in `TerrainStreamConfig`, runtime terrain variant resets must keep that
  scheduler context in sync before bounded browser defaults can ship.
  Evidence: `BrowserTerrainStream::reset_game_with_variant` currently updates
  its own `seed` and `terrain_variant` fields, then calls
  `scheduler.reset(center_coord)`.
- Observation: the existing fixed-offset browser default settling test remains
  expensive enough to time out as an isolated validation run.
  Evidence: `cargo test -p engine_web
  tests::browser_terrain_stream_default_bands_render_multiple_lods_after_settling`
  timed out after 240 seconds while the lighter targeted stream tests passed.
- Observation: after switching browser defaults to bounded vertical policies,
  the previous expensive default-band settling test completes.
  Evidence: `cargo test -p engine_web
  tests::browser_terrain_stream_default_bands_render_multiple_lods_after_settling`
  passed in about 80 seconds, and the full `browser_terrain_stream` filtered
  suite passed in about 105 seconds.
- Observation: Windows linker locks can appear when two `ofg_test_harness` test
  commands link the same executable concurrently.
  Evidence: a parallel `cargo test -p ofg_test_harness terrain_bench_lod` run
  failed with LNK1104, then passed when rerun by itself.
- Observation: custom terrain variant smoke scenarios need to honor their
  requested stream mode, not automatically use the LOD0 fixture stream.
  Evidence: the first final `npm run smoke:rust` run failed because
  `vertical-band-high-relief` was configured as multi-LOD but built through the
  variant LOD0 helper. `create_scenario_stream` now selects LOD0 or bounded
  multi-LOD bands from `ScenarioStreamMode`, and the rerun passed.
- Observation: the high-relief smoke report gives direct evidence that LOD0
  stays bounded while coarser LODs provide broad terrain coverage.
  Evidence: `artifacts/rust-smoke/run-1781437118-487/report.json` shows
  `vertical-band-high-relief` settled with no missing nodes, max rendered LOD 4,
  LOD0 desired node Y `-2..1`, and coarser LOD desired ranges up to `-4..3`.
- Observation: the default Rust coverage attention report still lists
  `crates/terrain_core/src/surface_query.rs` below 90% line coverage.
  Evidence: `npm run coverage:rust` reports `surface_query.rs` at 200/228
  covered lines, 87.7%. That file was not modified by this plan; modified
  implementation files do not appear in the filtered attention report.

## Decision Log

- Decision: resolve vertical generation per `(lod, x, z)` column rather than
  using one global offset list for the entire LOD band.
  Rationale: realistic terrain variation is spatially uneven. A mountain column
  and a flat lowland column should not force the same high-detail vertical stack.
  Date/Author: 2026-06-14 / Codex.
- Decision: model vertical selection as terrain-interest range intersected with
  player vertical window, followed by dependency and parent expansion.
  Rationale: terrain realism and generation budget are separate concerns. The
  terrain system can know a column may contain deep caves or tall peaks while
  still generating only the player-relevant slice at high detail.
  Date/Author: 2026-06-14 / User and Codex.
- Decision: define terrain-interest ranges in world meters first, then convert
  them to inclusive node `y` ranges at each LOD.
  Rationale: future terrain features such as lakes, caves, climate, and geology
  will reason in world-space altitude/depth, while the stream scheduler needs
  integer `TerrainNodeKey.coord.y` values.
  Date/Author: 2026-06-14 / Codex.
- Decision: the first terrain-interest estimator will be conservative and
  sampled, not an exact proof of continuous procedural extrema.
  Rationale: exact extrema for warped fractal noise would be a larger math
  project. A sampled estimate plus shape-based padding is enough to replace the
  current fixed offsets safely and creates a seam for future interval or
  feature-authored bounds.
  Date/Author: 2026-06-14 / Codex.
- Decision: keep parent-before-child coverage as a hard invariant.
  Rationale: the LOD apron and fallback system relies on every generated child
  having its parent ready. Vertical band selection must not create child nodes
  whose parent column is absent.
  Date/Author: 2026-06-14 / Codex.
- Decision: world-to-node conversion includes node coordinates touched exactly
  by the world Y range boundaries.
  Rationale: exact terrain surfaces and future apron or placement queries can
  lie on chunk planes. A conservative conversion may generate one extra node at
  a boundary, which is safer than excluding a surface that belongs to the next
  node by floor-coordinate semantics.
  Date/Author: 2026-06-14 / Codex.
- Decision: the first terrain-interest estimator pads sampled heights by twice
  the one-sided shape extent from height, ridge, cellular, and detail scales,
  in addition to configured surface and future-feature margins.
  Rationale: a missed point inside the column can sit on the opposite side of a
  sampled height. The padding is intentionally conservative until interval
  bounds or feature-authored ranges exist.
  Date/Author: 2026-06-14 / Codex.
- Decision: keep `TerrainLodVerticalPolicy::FixedOffsets` as a migration and
  fixture path while adding `TerrainLodVerticalPolicy::Bounded` for the new
  resolver.
  Rationale: the standalone facade, existing scheduler tests, and some smoke
  fixtures still need stable fixed-offset behavior while the browser runtime
  defaults move over deliberately in Milestone 4.
  Date/Author: 2026-06-14 / Codex.
- Decision: browser default vertical windows are LOD0 below/above `2/1`, LOD1
  `3/2`, LOD2 `4/3`, LOD3 `6/5`, LOD4 `8/7`, and LOD5 `5/4` after the
  fog-trimmed far-LOD follow-up.
  Rationale: LOD0 stays close to the current four-node near band, while coarser
  LODs can cover broader vertical terrain-interest ranges without forcing the
  highest-detail LOD to generate a full column.
  Date/Author: 2026-06-14 / Codex.
- Decision: custom smoke variant scenarios now preserve their requested stream
  mode.
  Rationale: high-relief vertical-band validation must exercise the bounded
  multi-LOD runtime path, while existing variant fixture scenarios can still use
  the LOD0-only path when configured that way.
  Date/Author: 2026-06-14 / Codex.
- Decision: keep `terrain_bench_profile.rs` on fixed-offset LOD bands for the
  profiling comparison path, but correct its comment so it no longer claims to
  mirror current browser defaults.
  Rationale: the multi-LOD benchmark and browser smoke now cover bounded
  defaults. The profile benchmark's stable fixed-offset scenarios remain useful
  for apples-to-apples mesh/profile comparisons.
  Date/Author: 2026-06-14 / Codex.
- Decision: record the existing `wgpu_renderer.rs` size as residual split
  pressure rather than refactoring it in this milestone.
  Rationale: this milestone only adds two debug snapshot fields to an existing
  serializer. Splitting the renderer/JS serialization module is a separate
  renderer refactor with much larger blast radius.
  Date/Author: 2026-06-14 / Codex.

## Outcomes & Retrospective

This plan is complete. The codebase now has Rust-owned, tested primitives for
world Y ranges, inclusive node Y ranges, player-centered vertical windows,
Y-less terrain node columns, and a conservative sampled terrain-interest
estimator. The scheduler can use either legacy fixed offsets or bounded
per-column Y range resolution, and the browser default stream now uses bounded
vertical policies. Debug snapshots, native smoke reports, and multi-LOD
benchmark reports expose per-LOD desired Y ranges.

The final high-relief smoke evidence shows the intended behavior: LOD0 remains
bounded near the player, coarse LODs cover broader vertical ranges, parent/child
coverage still settles, and LOD transition smoke passes without missing-node or
seam smoke failures. Active architecture, API contract, and terrain plan docs
now describe the Rust-owned vertical band resolver and its debug fields.

Remaining risk: the first terrain-interest estimator is deliberately sampled
and conservative, not analytic. That is appropriate for the current
heightfield-biased terrain, but future caves, lakes, geology, and climate
features should eventually provide authored or interval-derived vertical bounds
instead of relying only on sampled height plus padding. Existing large files
such as `crates/engine_web/src/wgpu_renderer.rs` and
`crates/ofg_test_harness/src/terrain_bench_profile.rs` remain split-pressure
areas for future refactors.

## Contract and Quality Baseline

This plan preserves the active ownership rules in `docs/API_CONTRACTS.md` and
`docs/ARCHITECTURE.md`.

`OFG-API-001` is preserved. Browser code continues to interact through
`RustBrowserGame.create`, `resize`, `tick`, `command`, and `debugSnapshot`.
Vertical band selection remains inside Rust.

`OFG-API-003` is preserved. Browser debug hooks may display Rust-authored
vertical band ranges and counts, but must not compute terrain bounds, desired
nodes, or visibility.

`OFG-API-004` is preserved. This plan does not change the terrain vertex layout,
WGSL terrain vertex contract, or render packet layout.

`OFG-API-005` is preserved. Terrain presets and variant descriptors remain
Rust-authored. The vertical bounds estimator may read validated
`TerrainVariantDescriptor` values but must not move terrain generation semantics
into TypeScript.

`OFG-API-006` is preserved. The standalone `terrain_core.wasm` remains a fixture
artifact. If fixture stream configuration exports change, they must be
documented as fixture-only.

`OFG-API-009` is preserved. TypeScript must not regain terrain generation,
density sampling, stream scheduling, LOD visibility, WebGPU terrain ownership,
or render submission.

Quality gates:

- Keep implementation files reasonably sized. If `stream.rs` or
  `terrain_stream.rs` grows past the repository's review thresholds, extract
  pure helper modules instead of piling logic into the stream modules.
- Add tests near the behavior first. Prefer behavior names such as
  `vertical resolver intersects terrain bounds with player window` and
  `scheduler keeps parent y nodes for resolved child bands`.
- After each implementation milestone, run the repo-local `milestone-review`
  skill before marking the milestone complete.
- The plan is not complete until `npm run coverage:rust` shows no modified
  implementation files below the default 90% attention threshold, or the
  Decision Log records an explicit exception with rationale.

## Context and Orientation

Terrain nodes are keyed by `TerrainNodeKey { lod, coord }` in
`crates/terrain_core/src/node.rs`. `lod = 0` is highest detail. Larger `lod`
values are coarser. A node's world cell size is
`terrain_node_cell_size(base_cell_size, lod)`, and each node contains
`TERRAIN_CHUNK_CELLS_PER_AXIS = 32` cells along each axis. Therefore a node's
world vertical span is:

    node_span_m = 32.0 * terrain_node_cell_size(base_cell_size, lod)

For a fixed `(lod, x, z)`, a terrain column is the set of possible nodes with
that same `lod`, `x`, and `z`, varying only by `coord.y`.

The current desired-node scheduler lives in
`crates/terrain_core/src/stream.rs`. `TerrainStreamScheduler::build_desired_nodes`
starts from a center `TerrainChunkCoord`, converts it to each LOD's coordinate
grid, loops over X/Z radius, and inserts one node for each fixed
`vertical_chunk_offsets` entry. It then adds ancestors and closes refined child
groups. This means every X/Z column in a band gets the same vertical offsets,
even if terrain can only exist in part of the column.

The current browser runtime stream lives in
`crates/engine_web/src/terrain_stream.rs`. `default_terrain_lod_bands()` defines
LOD0 through LOD4 horizontal radii and vertical offset lists. The stream creates
`TerrainStreamScheduler`, handles terrain worker request/completion flow, caches
generated node meshes, selects visible cover, builds optional transition apron
meshes, and returns updates to the Rust/wgpu renderer. This plan must keep that
ownership intact.

The current terrain field lives in `crates/terrain_core/src/field.rs`. It is a
3D density function, but the macro shape is heightfield-like. The top surface
height is mostly a function of X/Z plus 3D detail noise:

    density = position.y - macro_base_elevation(x,z) - detail_noise(x,y,z) * detail_amplitude

`height_at_with_shape` now searches for a surface crossing around the macro
terrain estimate rather than a fixed absolute Y band. That height helper is
useful for player grounding and compatibility, but it is still a heightfield
surface query; future deep caves or disconnected overhang surfaces need richer
terrain-interest bounds and/or mesh-backed queries instead of relying only on
one refined height per X/Z point.

The recently added polygonized surface query system in
`crates/terrain_core/src/surface_query.rs` samples exact generated mesh height
inside a generated node. That system is for placement and exact local surface
queries after a node exists. The vertical band resolver is earlier in the
pipeline: it decides which nodes should exist at all.

## Plan of Work

Milestone 1 adds pure vertical range primitives and tests.

Create a new small module such as `crates/terrain_core/src/vertical_band.rs`.
This module should not depend on noise or meshing. It should define inclusive
integer node ranges and world-meter ranges, with safe constructors and helpers
for intersection, expansion, emptiness, and conversion between world Y ranges
and node `coord.y` ranges. It should handle negative Y coordinates using the
same floor-division semantics as `terrain_chunk_coord_containing_position` and
`terrain_node_coord_for_lod`.

The core types should be explicit enough for tests and debug reports. Proposed
names:

    pub struct TerrainWorldYRange {
        pub min_y: f64,
        pub max_y: f64,
    }

    pub struct TerrainNodeYRange {
        pub min_y: i32,
        pub max_y: i32,
    }

    pub struct TerrainLodVerticalWindow {
        pub below_player_nodes: i32,
        pub above_player_nodes: i32,
    }

Add unit tests for:

- finite range validation rejects NaN, infinity, and inverted ranges;
- world-to-node conversion includes every node whose vertical span intersects
  the world range;
- negative world Y values map to the expected negative node coordinates;
- intersection returns empty when ranges do not overlap;
- player windows are asymmetric when configured that way;
- expansion saturates safely and never inverts valid ranges.

Milestone 2 adds a conservative terrain-interest estimator.

Add a Rust-owned estimator that answers: for this `TerrainNodeKey` X/Z footprint
and terrain variant, what world Y interval might contain terrain or terrain
features? For the first implementation, this can live in
`crates/terrain_core/src/vertical_band.rs` or a companion
`vertical_bounds.rs` if the file would become too large.

For the current heightfield-like terrain, estimate the range by:

1. computing the node's X/Z world footprint;
2. sampling macro terrain or refined `height_at_with_shape` at a deterministic
   grid over that footprint, including corners and center;
3. taking min/max sampled height;
4. padding by `detail_amplitude`, configured safety margins, and any feature
   padding reserved for future caves/lakes.

The first estimator should be intentionally conservative. It should be cheap
enough to run during scheduling or cacheable per `(seed, variant, lod, x, z)`.
If it becomes expensive, add a small cache in the scheduler keyed by terrain
variant revision and column key, with tests for invalidation on terrain variant
change.

Add tests for:

- flat or rolling terrain returns a compact Y range near the sampled surface;
- a high-relief synthetic terrain variant returns a larger Y range;
- the returned range includes `height_at_with_shape` for representative sample
  points;
- configured cave/depth padding expands downward without changing current
  density generation;
- invalid terrain variants are rejected at the public boundary, not silently
  clamped;
- estimates are deterministic for the same seed, variant, LOD, and column.

Milestone 3 integrates the resolver into `TerrainStreamScheduler`.

Replace the fixed-offset loop in `TerrainStreamScheduler::build_desired_nodes`
with a per-column resolver. For each configured LOD band and X/Z coordinate:

1. compute the possible terrain-interest world Y range for that column;
2. convert that range to an inclusive node Y range at the band's LOD;
3. compute the player vertical window at the same LOD from the center
   coordinate;
4. intersect possible range and player window;
5. insert the resulting nodes;
6. expand ancestors exactly as today so every generated child has its parent.

The stream configuration needs to evolve from fixed offsets to a per-LOD
vertical policy. A concrete target shape is:

    pub struct TerrainLodBand {
        pub lod: u8,
        pub horizontal_radius: i32,
        pub vertical: TerrainLodVerticalPolicy,
    }

    pub enum TerrainLodVerticalPolicy {
        FixedOffsets(Vec<i32>),
        Bounded(TerrainLodBoundedVerticalPolicy),
    }

    pub struct TerrainLodBoundedVerticalPolicy {
        pub below_player_nodes: i32,
        pub above_player_nodes: i32,
        pub surface_padding_below_m: f64,
        pub surface_padding_above_m: f64,
        pub feature_padding_below_m: f64,
        pub feature_padding_above_m: f64,
    }

`FixedOffsets` is acceptable as a temporary fixture/migration path for tests and
the standalone facade. The default browser runtime should move to `Bounded`.
If implementation shows the enum creates unnecessary complexity, replace fixed
offsets fully and update fixture call sites in the same milestone.

Add scheduler tests for:

- current flat terrain produces a compact vertical set similar to today's
  offsets;
- a column whose possible range is `-20..30` and whose player window is
  `28..32` resolves to `28..30`;
- LOD0 uses a narrower player window than LOD3/LOD4;
- moving the player upward shifts generated LOD0 Y nodes upward;
- far high mountains can appear in coarse LOD without forcing all LOD0 columns
  to generate the mountain's full vertical stack;
- ancestors are inserted for every resolved child Y node;
- empty resolved nodes are recorded and not regenerated every tick;
- negative player and terrain Y coordinates resolve correctly.

Milestone 4 updates runtime defaults, debug output, smoke, and benchmark
observability.

Update `crates/engine_web/src/terrain_stream.rs` default LOD bands to use
bounded vertical policies. Start conservatively:

- LOD0: narrow vertical window near player, enough for current terrain and
  grounding.
- LOD1/LOD2: wider windows for nearby hills and early caves.
- LOD3/LOD4: broad coarse windows so distant tall landforms can render without
  high-detail explosion.

The exact numbers should be chosen from tests and benchmark output. Record them
in the Decision Log with the measured node counts.

Expose Rust-authored debug data sufficient to verify behavior without moving
terrain logic into TypeScript. Useful fields in `BrowserTerrainStreamStatus` or
per-LOD summaries:

- resolved min/max node Y per LOD;
- desired node count per LOD after vertical filtering;
- empty node count per LOD;
- maybe terrain-interest range count diagnostics in native benchmark reports.

Update `crates/ofg_test_harness/src/render_smoke/scenarios.rs` with at least one
high-relief vertical-band scenario. It should use a deterministic terrain
variant with taller vertical bounds than current defaults and assert that:

- the stream settles;
- no missing desired nodes remain;
- at least one coarse LOD covers higher terrain;
- LOD0 desired/rendered counts remain bounded near the player.

Update `crates/ofg_test_harness/src/terrain_bench_lod.rs` so the multi-LOD
benchmark reports vertical generated ranges and node counts. This is where
budget regressions should become visible.

Milestone 5 validates, reviews, and documents the feature.

Update active docs:

- `docs/ARCHITECTURE.md`: describe vertical band resolution as Rust-owned
  terrain stream policy.
- `docs/API_CONTRACTS.md`: update debug snapshot fields if new fields are
  exposed.
- `docs/TERRAIN_PLAN.md`: add a note that this plan replaces fixed vertical
  offsets with player-bounded terrain-interest vertical ranges.

Run the full validation commands listed below. Run the `milestone-review` skill
after each implementation milestone, apply required findings, and record any
rejected findings with rationale in the Decision Log.

## Concrete Steps

All commands run from `C:\dev\ofg`.

Before editing:

    git -c safe.directory=C:/dev/ofg status --short --branch

During Milestone 1:

    cargo test -p terrain_core vertical_band
    cargo fmt

During Milestone 2:

    cargo test -p terrain_core vertical_band
    cargo test -p terrain_core terrain_variant
    cargo fmt

During Milestone 3:

    cargo test -p terrain_core stream_scheduler
    cargo test -p terrain_core vertical_band
    cargo fmt

During Milestone 4:

    cargo test -p engine_web browser_terrain_stream
    cargo test -p ofg_test_harness render_smoke
    cargo test -p ofg_test_harness terrain_bench_lod
    npm run check:wasm
    cargo fmt

Final validation:

    npm test
    npm run smoke:rust
    npm run smoke:browser
    npm run bench:terrain:rust
    npm run coverage:rust
    git -c safe.directory=C:/dev/ofg diff --check

Expected final state:

- Rust tests pass.
- TypeScript tests pass.
- Native smoke writes at least one vertical-band/high-relief scenario report
  under `artifacts/rust-smoke/`.
- Terrain benchmark writes a report under `artifacts/terrain-bench/` with
  vertical range/node count diagnostics.
- Coverage output lists no modified implementation files below the default
  attention threshold.

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

This plan is complete only when these observable behaviors are true:

- The scheduler no longer relies on one fixed vertical offset list for every
  column in the default browser terrain stream.
- For each LOD column, desired Y nodes are derived from terrain-interest bounds
  intersected with a player-centered vertical window.
- LOD0 high-detail vertical generation remains bounded near the player even
  when a column's possible terrain range is large.
- Coarser LODs can cover taller or deeper terrain ranges than LOD0 according to
  their configured player windows.
- Parent nodes are generated before child nodes for all resolved vertical
  ranges.
- Empty vertical nodes are cached and do not churn every tick.
- Debug snapshots or smoke reports expose enough Rust-authored vertical range
  data to prove the resolver is active.
- Existing LOD transition aprons still render without visible holes in LOD
  smoke.
- `npm run coverage:rust` passes the default coverage attention gate for every
  modified implementation file.

Acceptance commands:

    npm test
    npm run smoke:rust
    npm run smoke:browser
    npm run bench:terrain:rust
    npm run coverage:rust
    git -c safe.directory=C:/dev/ofg diff --check

## Idempotence and Recovery

The vertical resolver should be additive and deterministic. Re-running scheduler
sync for the same center, seed, terrain variant, and config must produce the
same desired node set.

If the bounded policy produces too many nodes, reduce only runtime default
windows first; keep the core range math tests. If the terrain-interest estimator
misses visible terrain, increase safety padding and record the reason in the
Decision Log. If the new default stream proves unstable late in the work, keep
the pure resolver and tests but temporarily configure the browser defaults to a
conservative bounded policy that matches today's effective vertical coverage.

Do not revert unrelated terrain surface query or transition apron work. If the
stream configuration shape changes, update all tests and fixture-only facade
helpers coherently rather than keeping duplicate legacy paths.

Generated artifacts from `npm run build:wasm` may be overwritten and regenerated
as part of validation. Do not commit `dist/`, `node_modules/`, or `artifacts/`.

## Artifacts and Notes

Useful existing source paths:

- `crates/terrain_core/src/stream.rs`: desired node selection and parent
  closure.
- `crates/terrain_core/src/stream_types.rs`: public stream config types.
- `crates/terrain_core/src/stream_helpers.rs`: stream config validation and
  priority helpers.
- `crates/terrain_core/src/node.rs`: LOD node coordinate helpers.
- `crates/terrain_core/src/field.rs`: current terrain height/density model.
- `crates/terrain_core/src/constants.rs`: chunk size and current height search
  constants.
- `crates/engine_web/src/terrain_stream.rs`: browser runtime default LOD bands
  and terrain stream status.
- `crates/ofg_test_harness/src/render_smoke/scenarios.rs`: native terrain smoke
  scenarios.
- `crates/ofg_test_harness/src/terrain_bench_lod.rs`: multi-LOD benchmark
  reports.

Expected artifact locations:

- Rust smoke images and reports under `artifacts/rust-smoke/`.
- Terrain benchmark reports under `artifacts/terrain-bench/`.
- Rust coverage reports under `artifacts/coverage/rust/`.

Final validation artifacts from this implementation:

- Browser smoke:
  `artifacts/browser-smoke/2026-06-14T11-49-06-060Z`.
- Rust smoke:
  `artifacts/rust-smoke/run-1781437118-487/report.json`.
- Terrain seam smoke:
  `artifacts/rust-smoke/run-1781438355-080/report.json`.
- Terrain benchmark:
  `artifacts/terrain-bench/run-1781437861-041/report.json`.
- Rust coverage:
  `artifacts/coverage/rust/summary.pretty.json`.

## Interfaces and Dependencies

The final implementation should expose Rust-owned helper functions or types
similar to these names. Exact signatures may change during implementation, but
the concepts must remain clear and tested:

    pub struct TerrainWorldYRange { ... }
    pub struct TerrainNodeYRange { ... }
    pub struct TerrainLodVerticalWindow { ... }
    pub struct TerrainLodBoundedVerticalPolicy { ... }
    pub fn terrain_world_y_range_to_node_y_range(
        range: TerrainWorldYRange,
        lod: u8,
        base_cell_size: f64,
    ) -> Option<TerrainNodeYRange>
    pub fn resolve_column_node_y_range(
        seed: u32,
        descriptor: TerrainVariantDescriptor,
        column: TerrainNodeColumnKey,
        base_cell_size: f64,
        player_center: TerrainChunkCoord,
        policy: TerrainLodBoundedVerticalPolicy,
    ) -> Option<TerrainNodeYRange>

If a `TerrainNodeColumnKey` type is added, it should represent `(lod, x, z)`
without a `y` coordinate. If implementation can stay simple without it, use
`TerrainNodeKey` plus documented conventions instead.

No new third-party libraries are required. All logic should remain in Rust
inside `terrain_core` and `engine_web`.

## Revision Notes

- 2026-06-14: Initial plan drafted to capture player-bounded vertical terrain
  generation before richer terrain realism work such as caves, lakes, climate,
  and vegetation.
- 2026-06-14: Completed implementation, validation, active documentation
  updates, and final milestone review.
