# Move Water Bathymetry Into Terrain Chunk Jobs

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `PLANS.md`.

## Purpose / Big Picture

Sea-level water should no longer ask the renderer to sample terrain height into a camera-centered bathymetry texture. Each terrain node build already knows the seed, terrain variant, node key, and cell size, so that job should also decide whether the node has water and emit a small node-local bathymetry texture. The browser renderer should only upload water packets produced by terrain jobs and draw water over the corresponding node coverage.

After this change, moving through terrain should not trigger renderer-side bathymetry generation. The visible behavior remains sea-level water with debug views, but the ownership is cleaner: terrain jobs produce terrain mesh plus optional water depth data, and `engine_web` owns GPU upload and drawing.

## Progress

- [x] (2026-06-11) Read `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, terrain worker bridge, terrain stream, and water renderer flow.
- [x] (2026-06-11) Add terrain-core water packet generation and standalone WASM buffer exports.
- [x] (2026-06-11) Route water packet fields through the generic browser worker bridge without TypeScript interpreting bathymetry.
- [x] (2026-06-11) Store node water packets in `engine_web` terrain stream updates and upload them in the Rust renderer.
- [x] (2026-06-11) Replace the renderer-side global camera bathymetry build path.
- [x] (2026-06-11) Run milestone review, tests, wasm checks, browser smoke, and coverage gate.

## Surprises & Discoveries

- Observation: The current worker bridge already forwards `TerrainVariantDescriptor` as flat numbers and copies mesh typed arrays from `terrain_core.wasm`; it can carry a bathymetry `Float32Array` the same way without TypeScript gaining terrain semantics.
  Evidence: `src/engine/web/terrainBuildWorker.ts` calls `ofg_build_chunk_mesh_for_variant` and copies exported buffers.

- Observation: The first packet implementation incorrectly used `height_at_for_variant` for every bathymetry texel. That public helper performs a vertical density search and was the wrong tool for terrain-job bathymetry.
  Evidence: `height_at_for_variant` delegates to `height_at_with_shape`, which now brackets a heightfield-like surface around the macro terrain estimate rather than using the terrain job's local bounded water-depth search.

- Observation: Sea-level ownership should stay half-open and local to the node that contains the plane; with independent sampling, sea level `0` belongs to LOD0 node `y = 0`.
  Evidence: the rejected density-chunk approach forced ownership toward the chunk below the plane only because it reused pre-sampled vertical columns. Direct bounded sampling has no such dependency.

- Observation: Reusing the pre-sampled density chunk for bathymetry creates a bad dependency shape between terrain mesh generation/cache details and water packet generation.
  Evidence: the density-chunk attempt made water ownership depend on the chunk below the sea plane and on the density store, even though the water packet can be generated independently from seed, variant, node key, and cell size.

- Observation: Bathymetry only needs the render-relevant depth range.
  Evidence: once the shader reaches its deepest water style, deeper true terrain depth does not change the visual result.

- Observation: The Rust coverage gate initially flagged `crates/engine_web/src/water.rs` below the 90% attention threshold.
  Evidence: `npm run coverage:rust` reported `crates/engine_web/src/water.rs: lines 223/253 (88.1%)`; focused water status and error tests raised the default filtered report to `none`.

## Decision Log

- Decision: Generate a 32x32 per-node bathymetry grid inside terrain generation jobs and omit it when all sampled depths are zero.
  Rationale: 32x32 is tiny enough to transfer and upload, but detailed enough for near-shore color/depth transitions. Empty-water omission prevents creating water resources for dry nodes.
  Date/Author: 2026-06-11 / Codex

- Decision: Derive node bathymetry from bounded direct terrain-equation sampling, not from pre-sampled density chunks and not from the public unbounded height probe.
  Rationale: The packet job remains independent: seed, variant, node key, and cell size are enough. The sampler first tests sea level and `sea_level - WATER_NODE_MAX_RELEVANT_DEPTH_METERS`; it stores `0`, the cap, or a bounded bisection result inside that vertical interval.
  Date/Author: 2026-06-11 / Codex

- Decision: Use `WATER_NODE_MAX_RELEVANT_DEPTH_METERS = 64.0` as the current packet-generation cap.
  Rationale: The water style saturates after a finite vertical bottom depth, so terrain jobs do not need to find terrain farther below the sea plane than the deepest useful visual response. This also bounds the number of density evaluations per texel.
  Date/Author: 2026-06-11 / Codex

- Decision: Keep TypeScript as an opaque packet router.
  Rationale: `OFG-API-009` forbids TypeScript water generation, bathymetry filling, terrain visibility, and WebGPU ownership. TypeScript may copy Rust-authored typed arrays from worker WASM to `engine_web.wasm`.
  Date/Author: 2026-06-11 / Codex

- Decision: Record renderer-driven cap invalidation as follow-up work instead of expanding this milestone.
  Rationale: The browser worker currently passes the same default cap as Rust (`64`) into the standalone fixture WASM. The right long-term source is the active water style's deepest meaningful bottom-depth threshold, but changing water settings should also invalidate/regenerate cached water packets. That is a separate lifecycle slice.
  Date/Author: 2026-06-11 / Codex

## Outcomes & Retrospective

Packet-driven browser water is working. Browser smoke produced visible final water and bottom-depth debug screenshots under `artifacts/browser-smoke/2026-06-11T09-40-37-758Z`. The final water debug snapshot reports `waterRuntime: rust-wgpu`, `waterBathymetryRuntime: rust-heightfield`, 32x32 bathymetry coverage, reflection enabled, `workerPoolRuntime: browser-worker`, 12 workers, and `synchronousBuildCount: 0`. Movement smoke reported `sampleCount: 360`, `workerCompletedDelta: 96`, `workerFailedDelta: 0`, `workerStaleCompletionDelta: 0`, `synchronousBuildDelta: 0`, `maxTerrainUpdateTotalMs: 12`, `maxWorkerInFlightCount: 12`, `maxCompletedBurst: 6`, and `settledMissingNodeCount: 0`.

The full `npm run smoke:rust` lane timed out at 15 minutes during the all-scenario run while the packet implementation was still being corrected. After replacing density-chunk reuse with bounded direct sampling, a focused Rust boot smoke passed and wrote `artifacts/rust-smoke/run-1781170348-449/report.json`. Full all-scenario smoke remains expensive and should be investigated separately from this water packet milestone.

The implementation now matches the corrected ownership model: terrain jobs generate optional node-local bathymetry packets by independent bounded density sampling; `engine_web` caches and uploads visible water packets; TypeScript copies raw buffers only; the renderer no longer builds a camera-centered bathymetry field.

## Contract and Quality Baseline

`OFG-API-001` permits TypeScript to route opaque Rust terrain worker packets and requires Rust to own scheduling and completion validation. This plan preserves that by adding bathymetry bytes to the existing worker completion packet.

`OFG-API-004` previously described a renderer-owned bathymetry texture CPU-filled inside `engine_web`. This milestone updates that contract: bathymetry remains Rust-authored, but the source is terrain job output rather than renderer-side terrain probes.

`OFG-API-009` forbids TypeScript ownership of terrain generation, water generation, bathymetry texture filling, water visibility, and WebGPU resources. This plan preserves that rule. TypeScript only copies flat arrays and forwards completions.

Quality gates: run targeted Rust tests, TypeScript tests for the worker bridge, `npm run check:wasm`, `npm run smoke:browser`, a focused native Rust boot smoke, and `npm run coverage:rust`. Modified implementation files should not appear in the default coverage attention output unless this plan records an explicit exception.

## Context and Orientation

`crates/terrain_core/src/water.rs` exposes `sea_depth_at_for_variant`, a compatibility vertical depth query over the active terrain height surface. Terrain-job bathymetry uses `build_water_node_packet_for_variant`, which performs independent bounded density sampling instead of calling that unbounded compatibility height helper.

`src/engine/web/terrainBuildWorker.ts` is the browser worker entry point. It calls `terrain_core.wasm`, copies mesh vertex/index buffers, and posts a `TerrainBuildCompletion` back to the generic worker host.

`crates/engine_web/src/terrain_stream.rs` owns scheduling, request ids, completion validation, mesh cache, and visible terrain selection. It must receive optional water packets alongside accepted mesh completions.

`crates/engine_web/src/water_renderer.rs` previously owned a single camera-centered bathymetry texture and an incremental renderer-side build. This plan replaces that with a terrain-job packet atlas and water-plane instances.

`src/engine/render/shaders/water.wgsl` now has a copy pass plus water patch entry points. The water patch path samples renderer-uploaded terrain-job bathymetry data, not a renderer-generated heightfield.

## Plan of Work

First, extend `terrain_core` with a `WaterNodePacket` data structure and a `build_water_node_packet_for_variant` function. The function samples the node XZ footprint at sea level into a 32x32 `Vec<f32>`, derives vertical bottom depth from bounded direct terrain-equation sampling, tracks whether any sample is wet, clamps depth to the render-relevant cap, and records origin/span metadata. Add standalone WASM exports for the latest water packet buffer, size, origin, span, maximum generated depth, and presence flag.

Second, update the browser terrain worker types and worker entry point so a worker completion includes optional water metadata and a `Float32Array`. TypeScript must not compute or inspect the values beyond copying them.

Third, update `engine_web` terrain stream completion structs, caches, and update packets so accepted worker completions store water packets by node key and visible-node transitions upsert/remove water alongside terrain mesh data.

Fourth, update `water_renderer` and `wgpu_renderer` to consume node water packets. The implemented GPU shape is a bounded R32Float bathymetry atlas plus per-patch water-plane instances carrying world bounds, atlas tile coordinates, sea level, and max generated depth. The water shader samples terrain-job bathymetry packets for vertical bottom depth.

Finally, delete the global renderer-side bathymetry build path once packet-driven bathymetry is verified.

## Concrete Steps

Run from `C:\dev\ofg`:

    cargo test -p terrain_core water
    cargo test -p engine_web water
    cargo test -p engine_web browser_terrain_stream_emits_water_for_generated_empty_sea_level_nodes
    npm run test:ts
    npm run check:shaders
    npm run check:wasm
    npm run smoke:browser
    npm run coverage:rust

Use `npm run smoke:rust` if native smoke water resource behavior changes.

Also run a focused Rust image smoke while full all-scenario smoke remains too expensive:

    cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario boot

## Milestone Review

Milestone review:

- Scope: packet-driven sea-level bathymetry generation, worker routing, Rust terrain-stream water cache/visibility, renderer bathymetry atlas upload, shader packet sampling, docs/contracts, and validation artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes using the repo-local `milestone-review` skill. Sub-agent reviewers were not used because this milestone was not explicitly requested as a delegated review.
- Required findings fixed: stale docs/artifacts updated; the rejected `height_at_for_variant` and pre-sampled-density approaches are recorded as rejected; max relevant depth is an explicit packet input; `crates/engine_web/src/water.rs` coverage was raised above the default attention threshold with focused tests.
- Follow-ups recorded: drive `max_depth_meters` from the active water style cap and invalidate/regenerate water packets when that cap changes; investigate the full all-scenario `npm run smoke:rust` timeout separately.
- Rejected findings: none.
- Validation rerun: targeted Rust water tests, TypeScript tests, shader/wasm checks, browser smoke, focused Rust boot smoke, `npm run coverage:rust`, and `git diff --check`.
- Remaining risk: the browser worker currently duplicates the Rust default max relevant depth constant (`64`) until water-style-driven regeneration exists.

## Validation and Acceptance

Acceptance criteria:

- Terrain worker completions contain optional Rust-authored bathymetry packets for water-bearing terrain nodes.
- Renderer-side water debug screenshots still show final water and bottom-depth views.
- No renderer-side camera-centered bathymetry sampling remains in `render_frame`.
- Browser movement performance reports `synchronousBuildDelta: 0`, worker runtime `"browser-worker"`, and bounded terrain update timing.
- Coverage gate passes with no files below the default 90% filtered attention threshold.

Validation evidence:

    cargo test -p terrain_core water
    # 7 passed

    cargo test -p engine_web water
    # 10 passed

    cargo test -p engine_web browser_terrain_stream_emits_water_for_generated_empty_sea_level_nodes
    # passed

    npm run test:ts
    # 114 passing

    npm run check:shaders
    # passed

    npm run check:wasm
    # passed

    npm run smoke:browser
    # passed; artifacts/browser-smoke/2026-06-11T09-40-37-758Z

    cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario boot
    # passed; artifacts/rust-smoke/run-1781170348-449/report.json

    npm run coverage:rust
    # files below 90% line coverage: none

    git -c safe.directory=C:/dev/ofg diff --check
    # no whitespace errors; line-ending warnings only

## Idempotence and Recovery

All generated WASM and shader artifacts can be regenerated with `npm run build:wasm` and `npm run build:shaders`. If the renderer conversion becomes too large, keep packet generation and worker routing behind tests while leaving the existing renderer fallback active, then resume from this plan.

## Artifacts and Notes

Previous stall fix artifacts:

    artifacts/terrain-stream-cpu/2026-06-11T07-14-13-983Z
    artifacts/terrain-stream-cpu/2026-06-11T07-19-17-250Z
    artifacts/browser-smoke/2026-06-11T07-20-28-494Z

## Interfaces and Dependencies

Expected new Rust-facing types:

    WaterNodePacket {
      texel_count: u32,
      origin_x: f32,
      origin_z: f32,
      world_span_x: f32,
      world_span_z: f32,
      sea_level_meters: f32,
      max_depth_meters: f32,
      depths_meters: Vec<f32>
    }

Expected Rust packet builder:

    build_water_node_packet_for_variant(
      seed: u32,
      descriptor: TerrainVariantDescriptor,
      key: TerrainNodeKey,
      cell_size: f64,
      sea_level: f64,
      max_depth_meters: f64
    ) -> Result<Option<WaterNodePacket>, TerrainVariantValidationError>

The standalone `terrain_core.wasm` fixture export mirrors that final `max_depth_meters` parameter as `maxDepthMeters`.

Expected worker completion fields:

    waterTexelCount: number
    waterOriginX: number
    waterOriginZ: number
    waterWorldSpanX: number
    waterWorldSpanZ: number
    waterSeaLevelMeters: number
    waterMaxDepthMeters: number
    waterDepths?: Float32Array
