# Shadow Cascade Culling And Range Optimization

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

Archived on 2026-06-08 after completion. The active source of truth for the next
performance debugging, UI, and terrain LOD cost analysis work moved to
`docs/RENDER_PERF_UI_AND_TERRAIN_LOD_ANALYSIS_PLAN.md`.

This document follows `PLANS.md` in this repository. It was the active source of truth for reducing the measured shadow-map cost discovered by the completed diagnostic plan archived at `docs/archived/PERF_DEBUGGING_PLAN_2026-06-08.md`.

## Purpose / Big Picture

OFG currently spends most measured GPU frame time rendering cascaded shadow maps. The latest perf capture showed a 22.495 ms measured GPU baseline and a 7.408 ms GPU frame when shadow-map passes were disabled. Main-camera culling rejected 258 of 336 render candidates, but every shadow cascade rendered all 336 candidates. Across four cascades that produced 1344 shadow draws and about 7.7 million shadow-submitted vertices per frame.

The goal of this plan is to make shadow rendering correct and bounded before attempting terrain LOD or material optimizations. After this work, shadow cascades should draw only objects whose shadows can affect the cascade receiver region, shadow distance should be intentionally limited, and shadows should fade or disable as the sun gets too low to avoid unbounded far-distance casters. A developer should be able to run the perf capture script and see fewer shadow draws, fewer shadow-submitted vertices, nonzero cascade cull counts, and lower measured GPU time without changing terrain generation or mesh resolution.

This plan explicitly does not reduce terrain voxel resolution, switch shadow rendering to coarser terrain LODs, or hand-optimize shader material cost. Those may be later plans, but they should wait until shadow culling and range limits are working and measured.

## Progress

- [x] (2026-06-08) Created this ExecPlan from the shadow optimization discussion and archived the completed perf debugging plan.
- [x] (2026-06-08) Milestone 1: Added Rust-owned `shadowSunMode` diagnostics (`production`, `overhead`, `angled`, `low`), exposed active state through debug/status contracts, and added overhead/angled/low capture experiments.
- [x] (2026-06-08) Milestone 2: Implemented CPU-side light-space cascade caster culling and per-cascade cull counters, with focused Rust tests for overhead and angled sun behavior.
- [x] (2026-06-08) Milestone 3: Reduced production shadow receiver distance to 100 meters and added low-sun shadow fade/disable plus clamped cascade light direction.
- [x] (2026-06-08) Milestone 4: Re-ran production, overhead, angled, low-sun, cascade, material, and LOD capture scenarios. Artifact: `C:\dev\ofg\artifacts\perf-debug\2026-06-08T11-11-50-045Z\summary.json`.
- [x] (2026-06-08) Milestone 5: Ran validation and updated outcomes. Commands passed: `cargo test -p engine_web shadow --lib`, `cargo check -p engine_web --target wasm32-unknown-unknown`, `npm run check:shaders`, `npm run check:wasm`, `cargo test -p engine_web`, `npm run test:ts`, `node tools/browser-perf-debug-capture.mjs`, `npm run smoke:browser`, `npm test`, `npm run coverage:rust`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- [x] (2026-06-08) Local milestone review for Milestones 1-3 completed. No sub-agents were used because the user did not explicitly request delegated sub-agent review. Required finding fixed: removed an unnecessary new `#[allow(clippy::too_many_arguments)]` from `crates/engine_web/src/shadows.rs`. Validation rerun: `cargo test -p engine_web shadow --lib`.
- [x] (2026-06-08) Local milestone review for Milestones 4-5 completed. Reviewers covered contract, code quality, legacy ownership, correctness, and validation locally. No required findings. Residual risk: GPU timing in the browser capture is noisy enough that counter/vertex evidence is more reliable than one-run `shadow-pass-off` delta after the optimization.

## Surprises & Discoveries

- Observation: Main-camera culling already works, but shadow-cascade culling does not.
  Evidence: `artifacts/perf-debug/2026-06-08T07-32-04-418Z/summary.json` reports `frameDrawCount: 336`, `frameVisibleDrawCount: 78`, `frameCulledDrawCount: 258`, and each shadow cascade reports `candidateCount: 336`, `visibleCount: 336`, `culledCount: 0`.

- Observation: The current configured shadow distance is 220 meters.
  Evidence: `crates/engine_web/src/config.rs` defines `SHADOW_MAX_DISTANCE: f32 = 220.0`.

- Observation: The cascade builder already stores a world-space receiver AABB per cascade.
  Evidence: `crates/engine_web/src/shadows.rs` defines `ShadowCascade.world_bounds` and sets it from the camera frustum slice corners in `build_shadow_cascade`.

- Observation: The shadow draw loop currently tests no bounds before drawing.
  Evidence: `crates/engine_web/src/wgpu_renderer.rs`, `BrowserWgpuRenderer::render_shadow_passes`, loops all `render_items` for each enabled cascade and calls `pass.draw_indexed(...)` without a cascade intersection test.

- Observation: A cascade's existing light view-projection matrix is enough for conservative caster submission culling.
  Evidence: `crates/engine_web/src/shadows.rs` now exposes `shadow_caster_intersects_cascade(...)`, which builds a frustum from `ShadowCascade.light_view_projection` and tests each render item AABB before the shadow draw call.

- Observation: Production shadow submission is now bounded and culling-heavy.
  Evidence: `artifacts/perf-debug/2026-06-08T11-11-50-045Z/summary.json` reports baseline shadow draws `124` instead of the archived `1344`, cascade culls `1220 / 1344`, and shadow-submitted vertices `1,050,460` instead of about `7,706,776`.

- Observation: The forced overhead sun probe is tight relative to production, while the angled sun includes more casters as expected.
  Evidence: The same capture reports overhead shadow draws `105`, production shadow draws `124`, and angled shadow draws `135`.

- Observation: Low sun no longer creates an unbounded shadow workload.
  Evidence: Forced low sun reports `shadowStrength: 0`, `shadowDrawAverage: 0`, all cascades disabled, and clamped effective sun direction `y = 0.18`.

- Observation: GPU timestamp deltas are now noisy after shadow culling.
  Evidence: The same capture reports production GPU `10.286 ms` and `shadow-pass-off` GPU `10.322 ms`, even though shadow draws drop from `124` to `0`. The archived pre-fix comparison was `22.495 ms` production versus `7.408 ms` with shadow passes off. After this work, the shadow-map pass is no longer the obviously dominant GPU cost in this scene; counter and vertex evidence are the reliable diagnosis.

## Decision Log

- Decision: Fix shadow culling and bounds before terrain LOD resolution changes.
  Rationale: The measured first-order cost is shadow-map generation. Reducing terrain voxel resolution could help both main and shadow rendering, but it would hide whether shadows are rendering the right objects.
  Date/Author: 2026-06-08 / Codex

- Decision: Use a deterministic overhead-sun test as the first correctness probe.
  Rationale: With the sun directly overhead, off-screen terrain should not cast long horizontal shadows into the camera view. The total shadow draws across cascades should be close to the set of camera-relevant/cascade-relevant draws, apart from split overlap and receiver/caster padding.
  Date/Author: 2026-06-08 / Codex

- Decision: Avoid using different terrain LOD meshes for shadow rendering in this plan.
  Rationale: Rendering shadows from different terrain geometry risks subtle bias, peter-panning, acne, and contact mismatch bugs. First make the existing geometry submit correctly.
  Date/Author: 2026-06-08 / Codex

- Decision: Treat low sun angles as a bounded/faded shadow problem, not an infinite caster search problem.
  Rationale: Near-horizon directional shadows can require arbitrarily distant casters. The renderer should clamp or fade shadows at low sun elevations rather than trying to render the world.
  Date/Author: 2026-06-08 / Codex

- Decision: Use the cascade light-space clip volume for the first caster culling implementation.
  Rationale: The renderer already computes stable light view-projection matrices with receiver extent and caster margin. Testing render-item AABBs against that frustum is conservative, Rust-owned, and directly matches the shadow map volume being rendered.
  Date/Author: 2026-06-08 / Codex

- Decision: Set production shadow receiver distance to 100 meters while keeping the existing caster margin for this pass.
  Rationale: The user's concrete target was a far cascade around 100 meters. Keeping the caster margin unchanged avoids mixing the primary culling/range fix with a more bias-sensitive quality change.
  Date/Author: 2026-06-08 / Codex

- Decision: Add a shader shadow-strength lane in `shadows.spare.w`.
  Rationale: Low-sun behavior should fade sampled shadows and skip shadow-map passes when fully faded, without adding another uniform buffer or changing vertex/material layouts.
  Date/Author: 2026-06-08 / Codex

## Outcomes & Retrospective

Implemented Rust-owned shadow diagnostics, cascade caster culling, a 100 meter production shadow receiver range, and low-sun fade/disable behavior. The browser debug/capture path can force production, overhead, angled, and low sun through `setRenderDebugOptions({ shadowSunMode })`, while TypeScript remains a command/debug shell and does not compute sun direction, culling, or terrain visibility.

The measured result is a large and factual reduction in shadow work:

    Archived baseline: shadowDrawAverage 1344, shadow vertices about 7.7M, measured GPU 22.495 ms.
    New production capture: shadowDrawAverage 124, shadow vertices 1.05M, measured GPU 10.286 ms.
    New production culls: 1220 of 1344 cascade candidates, or about 90.8%.
    Forced overhead: shadowDrawAverage 105.
    Forced angled: shadowDrawAverage 135.
    Forced low: shadowStrength 0 and shadowDrawAverage 0.

The direct `shadow-pass-off` delta is no longer useful as a first-order diagnosis in the new capture because disabling the already-small shadow passes measured slightly slower in one run (`10.322 ms` versus `10.286 ms` production). Treat that as GPU timing variance and a sign that the original shadow submission problem has been addressed. The next render optimization plan should use the new counters to study remaining scene/material/terrain LOD costs. The LOD-only capture still suggests terrain geometry is important: `terrain-lod-3-plus` measured `5.757 ms` GPU with only 4 shadow draws, while production measured `10.286 ms`.

## Contract and Quality Baseline

This plan preserves `OFG-API-001: Browser Shell To Rust Browser Game`. Any new debug controls must go through `GameCommand` in `src/engine/web/browserGameTypes.ts`, be forwarded by `src/engine/web/rustBrowserGameRuntime.ts` and `src/engine/web/rustBrowserGameAdapter.ts`, and be parsed by `RustBrowserGame::command(...)` in `crates/engine_web/src/wgpu_renderer.rs`. Do not add scalar wasm-bindgen methods.

This plan extends `OFG-API-003: Debug And Smoke-Test Hooks` only for diagnostics needed to force a sun direction or shadow state during capture. TypeScript may expose buttons or `window.__ofgDebug` helpers that forward commands, but it must not compute sun direction, shadow cascades, terrain visibility, or renderer culling.

This plan preserves `OFG-API-004: Terrain Vertex And Material Layout`. Shadow culling must use existing world-space bounds and GPU mesh metadata. Do not change terrain vertex layout, material packing, or shader vertex inputs.

This plan preserves `OFG-API-009: Forbidden TypeScript Ownership`. TypeScript must not regain terrain manager, render packet, culling, scene graph, or WebGPU ownership. Culling decisions remain Rust-owned.

Quality gates:

- Add focused Rust tests for shadow-caster culling math, overhead sun behavior, angled sun expansion, low-sun clamp/fade, and invalid inputs.
- Add TypeScript tests for any new command/debug hook typing and forwarding.
- Update `docs/API_CONTRACTS.md` if new debug commands or snapshot fields are added.
- Run `npm test`, `npm run smoke:browser`, `node tools/browser-perf-debug-capture.mjs`, and `npm run coverage:rust` before completion.
- Run the repo-local `milestone-review` skill after each milestone before marking it complete. Apply required findings or record a rejected finding with rationale.

## Context and Orientation

Shadow rendering is Rust-owned and browser WebGPU-backed. The most relevant files are:

- `crates/engine_web/src/config.rs`: shadow constants such as `SHADOW_CASCADE_COUNT`, `SHADOW_MAP_SIZE`, `SHADOW_MAX_DISTANCE`, `SHADOW_SPLIT_LAMBDA`, and `SHADOW_CASTER_MARGIN`.
- `crates/engine_web/src/shadows.rs`: CPU-side cascaded shadow map math. It computes split distances, camera frustum slice corners, directional-light view/projection matrices, and a `world_bounds` AABB for each cascade receiver slice.
- `crates/engine_web/src/render_math.rs`: AABB, frustum, matrix, and vector helpers. Existing main-camera culling uses `frustum_intersects_aabb(...)`.
- `crates/engine_web/src/wgpu_renderer.rs`: browser WebGPU renderer. `BrowserWgpuRenderer::render(...)` builds `PreparedRenderItem` values with `world_bounds`; the main pass culls with the camera frustum; `render_shadow_passes(...)` currently draws every prepared item into every enabled cascade.
- `crates/engine_web/src/perf.rs`: frame-history and render counter types. `ShadowCascadeCounter` already records per-cascade `candidateCount`, `visibleCount`, `culledCount`, draw count, vertices, indices, and triangles.
- `tools/browser-perf-debug-capture.mjs`: deterministic capture script that records baseline and render-debug toggles under `artifacts/perf-debug/`.

Definitions:

- A cascade receiver slice is the portion of the camera frustum covered by one shadow cascade.
- A caster is an object that can cast a shadow onto that receiver slice.
- Overhead sun means light direction is nearly vertical. In this case, off-screen horizontal displacement is minimal, so culling should be tight.
- Low sun means light direction is near the horizon. In this case, potential caster distance grows very large, so the renderer must clamp/fade instead of searching indefinitely.
- Shadow distance is the farthest camera depth receiving cascaded shadows. The current value is 220 meters.

## Plan of Work

Milestone 1 adds deterministic diagnostic controls. Add a Rust-owned shadow/sun diagnostic override so captures can force an overhead sun and a few angled sun cases without changing normal production time-of-day behavior. Prefer extending the existing render debug command path with fields such as `shadowSunMode: "production" | "overhead" | "angled" | "low"` or a small separate `setShadowDebugOptions` command if the command becomes clearer. The active debug state must be visible in `debugSnapshot()` or `rendererStatus`. Extend `tools/browser-perf-debug-capture.mjs` with overhead-sun and angled-sun capture scenarios. The overhead-sun capture is a correctness probe, not merely a benchmark.

Milestone 2 implements cascade caster culling. Add CPU-side helpers in Rust that decide whether a `PreparedRenderItem.world_bounds` can cast into a cascade. The first version should be conservative and correct:

- Start from each cascade receiver `world_bounds`.
- Expand the receiver bounds in the opposite light direction by a finite caster distance derived from shadow distance, cascade depth, terrain/object height bounds, and `SHADOW_CASTER_MARGIN`.
- Test each render item AABB against that expanded caster region or against a light-space cascade frustum if that proves more exact.
- Record per-cascade candidate, visible, and culled counts using existing `ShadowCascadeCounter`.
- Keep all tests in Rust. For overhead sun, the sum of shadow draws across cascades should be close to camera/cascade-relevant visible draws, with documented tolerance for split overlap and padding. For angled sun, off-camera casters should increase but still be bounded.

Do not alter terrain stream desired sets, mesh generation, resident mesh caches, or render packet ownership. This is draw submission culling only.

Milestone 3 bounds the shadow problem. Revisit `SHADOW_MAX_DISTANCE` and make it a clearly named production constant, likely shorter than the current 220 meters after measurement. Add low-sun behavior:

- Define a minimum sun elevation for full-strength shadow rendering.
- Define a lower elevation at which shadow rendering fades to zero or disables.
- Clamp the effective shadow light direction used for cascade construction if needed so near-horizon shadows do not demand infinitely distant casters.
- Report the active shadow fade factor or effective shadow state through debug status.
- Ensure the shader and shadow uniforms treat fully faded/disabled shadows consistently with `shadowSamplingEnabled: false` or disabled shadow pass behavior.

Milestone 4 measures the result. Run the capture script for production sun, forced overhead sun, at least one angled sun, and low sun. Compare against the archived baseline:

- Baseline before this plan: `shadowDrawAverage: 1344`, measured GPU `22.495 ms`, shadow vertices about `7.7M`.
- Success should show nonzero cascade `culledCount`, lower `shadowDrawAverage`, lower shadow vertex/index counts, and lower measured GPU time in production and overhead captures.
- Overhead-sun capture should be especially tight. If it still draws many off-screen objects, stop and fix culling before tuning distances.

Milestone 5 finalizes docs, tests, and review. Update `docs/API_CONTRACTS.md` for any new command/snapshot fields. Run full validation, coverage, browser smoke, capture, and milestone review. Update this plan's living sections with exact numbers and recommendations.

## Concrete Steps

All commands run from `C:\dev\ofg`.

1. Inspect current shadow and perf code:

    Get-Content crates/engine_web/src/shadows.rs
    Get-Content crates/engine_web/src/render_math.rs
    Get-Content crates/engine_web/src/wgpu_renderer.rs
    Get-Content crates/engine_web/src/perf.rs
    Get-Content tools/browser-perf-debug-capture.mjs

2. Add or adjust Rust debug state and command typing:

    Edit crates/engine_web/src/perf.rs or a new shadow debug module for validated options.
    Edit crates/engine_web/src/wgpu_renderer.rs to parse commands and serialize active state.
    Edit src/engine/web/browserGameTypes.ts and src/engine/web/engineWebWasm.ts for TypeScript types.
    Edit src/app/game.ts only to expose browser debug hooks that forward Rust commands.

3. Add shadow culling helpers and tests:

    Edit crates/engine_web/src/shadows.rs or a new focused module such as crates/engine_web/src/shadow_culling.rs.
    Add tests near the implementation, or in a sibling test file if implementation files approach 1000 lines.
    Run:

    cargo test -p engine_web shadow --lib

4. Integrate culling into the WebGPU shadow pass:

    Edit BrowserWgpuRenderer::render_shadow_passes in crates/engine_web/src/wgpu_renderer.rs.
    Ensure counters report candidates, visible draws, and culled items per cascade.
    Run:

    cargo check -p engine_web --target wasm32-unknown-unknown
    cargo test -p engine_web

5. Regenerate and validate browser contracts if WASM or shaders changed:

    npm run build:engine-web-wasm
    npm run check:wasm
    npm run check:shaders
    npm run test:ts

6. Run capture and smoke:

    npm run smoke:browser
    node tools/browser-perf-debug-capture.mjs

7. Run full validation and coverage:

    npm test
    npm run coverage:rust
    git -c safe.directory=C:/dev/ofg diff --check

8. Update this plan:

    Record capture artifact paths.
    Record before/after shadow draws, culled counts, vertices, GPU timings, and low-sun behavior.
    Record milestone review findings and final recommended follow-up work.

## Milestone Review

After each milestone:

1. Update changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Behavioral acceptance:

- `window.__ofgDebug.dumpPerfStats()` still works and reports per-cascade shadow counters.
- The capture script can force overhead sun and record a settled frame.
- With overhead sun, per-cascade culling rejects off-cascade/off-camera objects, and total shadow draws are close to the camera/cascade-relevant object set rather than four times all candidates.
- With angled sun, off-camera casters may be included, but counts remain bounded by configured shadow distance and caster margin.
- With low sun below the chosen threshold, shadows fade or disable rather than expanding to unbounded caster distances.
- Production capture shows lower `shadowDrawAverage`, lower shadow-submitted vertices/indices, and lower measured GPU time than the archived baseline.
- Terrain stream status, terrain LOD scheduling, mesh generation, texture ownership, and TypeScript ownership boundaries remain unchanged.

Command acceptance:

- `cargo test -p engine_web shadow --lib` passes.
- `cargo check -p engine_web --target wasm32-unknown-unknown` passes.
- `cargo test -p engine_web` passes.
- `npm run test:ts` passes.
- `npm test` passes.
- `npm run smoke:browser` passes and verifies any new debug hook shape.
- `node tools/browser-perf-debug-capture.mjs` completes and writes `summary.json`.
- `npm run coverage:rust` reports no modified implementation file below the default filtered 90% line-coverage threshold, or this plan records a specific exception with rationale.

Expected measured result:

    Before: shadowDrawAverage 1344, shadow vertices about 7.7M, measured GPU 22.495 ms.
    After: shadowDrawAverage materially lower, nonzero cascade culled counts, measured GPU lower.

Do not mark this plan complete if shadow draw counts fall only because shadows are globally disabled in normal daylight production rendering.

## Idempotence and Recovery

All debug sun/shadow controls must reset to production defaults on reload and through a reset debug command. Running the capture script repeatedly should create a new `artifacts/perf-debug/<run-id>/` directory and must not mutate committed source or runtime settings.

If a culling change causes missing shadows, revert or disable only the new culling path through a Rust-owned debug option while preserving the tests and capture evidence. If GPU timestamp queries are unavailable on a machine, acceptance may use CPU and counter evidence but must record that GPU timing was unavailable.

If low-sun fade causes visual popping, widen the fade band before changing culling math. If overhead-sun culling is not tight, stop and fix the culling model before adjusting `SHADOW_MAX_DISTANCE`.

## Artifacts and Notes

The main baseline artifact is:

    C:\dev\ofg\artifacts\perf-debug\2026-06-08T07-32-04-418Z\summary.json

Important baseline facts from that artifact:

    gpuTotalAverageMs: 22.495
    shadow-pass-off gpuTotalAverageMs: 7.408
    frameDrawCount: 336
    frameVisibleDrawCount: 78
    frameCulledDrawCount: 258
    shadowDrawAverage: 1344
    shadowCascadeCounters[*].candidateCount: 336
    shadowCascadeCounters[*].visibleCount: 336
    shadowCascadeCounters[*].culledCount: 0

Do not commit generated `artifacts/`, screenshots, `dist/`, `dist-test/`, or `node_modules/`.

## Interfaces and Dependencies

Potential Rust interfaces:

- `crates/engine_web/src/shadow_culling.rs`
  - `ShadowCasterCullingOptions`
  - `ShadowCasterCullResult`
  - `shadow_caster_intersects_cascade(...)`
  - `expanded_shadow_caster_bounds(...)`

- `crates/engine_web/src/shadows.rs`
  - May expose cascade receiver bounds or derived light-space caster bounds if the culling helper belongs here.
  - May accept an effective shadow max distance or sun clamp/fade options instead of using only `SHADOW_MAX_DISTANCE`.

- `crates/engine_web/src/perf.rs`
  - May add validated shadow/sun diagnostic options if they belong beside `RenderDebugOptions`.

- `crates/engine_web/src/wgpu_renderer.rs`
  - `render_shadow_passes(...)` must call the culling helper before `draw_indexed`.
  - Must update `ShadowCascadeCounter.visibleCount` and `culledCount` accurately.

Potential TypeScript interfaces:

- `src/engine/web/browserGameTypes.ts`
  - Add command/debug types only if new debug options are needed for deterministic sun captures.

- `src/app/game.ts`
  - Add only browser debug hooks that forward commands and read Rust state.

Tool updates:

- `tools/browser-perf-debug-capture.mjs`
  - Add overhead, angled, and low-sun capture experiments.
  - Include shadow fade/effective sun state in `summary.json` if available.

No new rendering framework, ECS, terrain manager, or TypeScript WebGPU owner should be introduced.
