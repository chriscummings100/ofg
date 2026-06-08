# Render Perf UI And Terrain LOD Cost Analysis

Archived note: this completed plan was replaced as the active performance source of truth by `C:\dev\ofg\docs\TERRAIN_SKY_FILL_RATE_OPTIMIZATION_PLAN.md`, which plans the next fill-rate and terrain shader optimization phase.

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `PLANS.md` in this repository. It is the active source of truth for the next performance-debugging phase after the completed shadow-culling work archived at `C:\dev\ofg\docs\archived\SHADOW_CULLING_OPTIMIZATION_PLAN_2026-06-08.md`.

## Purpose / Big Picture

The first render-performance pass removed a major shadow-map bottleneck, but the browser frame rate is still poor. The next objective is to make diagnosis fast and factual: the developer should be able to open the running app, toggle render-debug features without DevTools, turn a live perf overlay on and off, and run repeatable captures that expose terrain LOD draw, vertex, triangle, and render cost.

The working hypothesis is that terrain vertex count is now the next bottleneck, especially for coarser distance LODs that still use too many voxels per node. This plan does not reduce terrain voxel resolution. It adds the UI and measurement tools needed to prove or reject that hypothesis, then records a data-backed recommendation for any follow-up terrain LOD optimization.

## Progress

- [x] (2026-06-08) Created this ExecPlan as the active source of truth for render debug UI, live perf overlay, and terrain LOD cost analysis.
- [x] (2026-06-08) Archived the completed shadow-culling plan at `C:\dev\ofg\docs\archived\SHADOW_CULLING_OPTIMIZATION_PLAN_2026-06-08.md`.
- [x] (2026-06-08) Milestone 1: Added in-browser render-debug controls in `C:\dev\ofg\index.html`, `C:\dev\ofg\src\main.ts`, `C:\dev\ofg\src\app\game.ts`, and `C:\dev\ofg\src\app\renderDebugUi.ts`. Browser smoke verified the UI can set `terrainLodMask: 4`, disable sky/shadows/sampling, force overhead sun, enable white textures, switch to Lambert, and reset to defaults.
- [x] (2026-06-08) Milestone 2: Added a toggleable live perf overlay using `C:\dev\ofg\src\app\perfDebug.ts`. Browser smoke verified overlay text containing browser frame, Rust CPU, GPU, draw, terrain LOD, cascade, and debug-option lines.
- [x] (2026-06-08) Milestone 3: Audited the current Rust-owned perf data and found the existing terrain LOD counters, shadow cascade counters, render counters, and GPU pass timings are sufficient. No Rust counter or WASM contract expansion was needed.
- [x] (2026-06-08) Milestone 4: Extended `C:\dev\ofg\tools\browser-perf-debug-capture.mjs` with scene-only LOD mask experiments and `terrainLodAnalysis` output. Capture artifact: `C:\dev\ofg\artifacts\perf-debug\2026-06-08T21-48-13-910Z\summary.json`.
- [x] (2026-06-08) Milestone 5: Ran the detailed analysis and confirmed terrain vertex count is the next bottleneck. LOD1 and LOD2 contribute `381102 / 550510` submitted scene vertices, or `69.228%`, and LOD1-only with shadows disabled still measured `9.786 ms` GPU total versus `10.300 ms` production.
- [x] (2026-06-08) Milestone 6: Updated `C:\dev\ofg\docs\API_CONTRACTS.md`, ran local milestone review, and completed validation. Commands passed: `npm run test:ts`, `npm run smoke:browser`, `node tools/browser-perf-debug-capture.mjs`, `npm test`, `npm run coverage:rust`, and `git -c safe.directory=C:/dev/ofg diff --check`.

## Surprises & Discoveries

- Observation: The completed shadow pass produced a large improvement, but not enough to make frame rate good.
  Evidence: `C:\dev\ofg\artifacts\perf-debug\2026-06-08T11-11-50-045Z\summary.json` reported production GPU frame time around `10.286 ms` after the shadow fix, down from the archived pre-fix `22.495 ms`.

- Observation: Shadows are no longer the obviously dominant cost in the available capture.
  Evidence: The same capture reported production shadow draws `124`, down from archived pre-fix `1344`, with `1220 / 1344` cascade candidates culled. A low-sun diagnostic produced `shadowStrength: 0` and `shadowDrawAverage: 0`.

- Observation: Terrain LOD masks already suggest geometry cost is a likely next target, but the evidence needs to be made cleaner.
  Evidence: The same capture reported production visible draws `78` and submitted scene vertices `550510`, while the `terrain-lod-3-plus` experiment reported visible draws `14`, submitted scene vertices `73570`, and GPU frame time around `5.757 ms`.

- Observation: One-off GPU deltas are noisy enough that counter evidence must be part of the conclusion.
  Evidence: After shadow culling, the same capture reported production GPU `10.286 ms` and `shadow-pass-off` GPU `10.322 ms`, even though shadow draws dropped from `124` to `0`.

- Observation: The live perf overlay can display all required cost families without new Rust fields.
  Evidence: `C:\dev\ofg\artifacts\browser-smoke\2026-06-08T21-46-59-171Z\report.json` recorded overlay text with `Frame br`, `Render cpu`, `GPU scene`, `Draws`, `Submit`, `LOD`, `Casc`, and `Debug` lines.

- Observation: The remaining production GPU cost is dominated by scene rendering rather than shadows.
  Evidence: `C:\dev\ofg\artifacts\perf-debug\2026-06-08T21-48-13-910Z\summary.json` reports production GPU total average `10.300 ms`, scene average `7.501 ms`, summed average shadow cascade timing about `2.051 ms`, bloom average `0.108 ms`, and post-process average `0.640 ms`.

- Observation: LOD1 is the largest single terrain vertex and triangle contributor in the captured view.
  Evidence: The same artifact reports LOD0 `95838` vertices (`17.409%`), LOD1 `223746` vertices (`40.644%`), LOD2 `157356` vertices (`28.584%`), LOD3 `65472` vertices (`11.893%`), and LOD4 `8094` vertices (`1.470%`).

- Observation: Material texture sampling and material mode are not the primary remaining bottleneck.
  Evidence: The same artifact reports `white-textures` GPU total `10.073 ms` and `lambert-material` GPU total `9.526 ms` versus production `10.300 ms`. These improve less than the LOD mask reductions and do not change the `550510` submitted scene vertices.

- Observation: Shadow culling remains effective after adding the UI and capture changes.
  Evidence: The same artifact reports production `124` shadow draws, `shadow-pass-off` GPU total `9.731 ms`, low-sun shadow strength `0`, and low-sun shadow draws `0`. The production shadow cascade counters cull `1220 / 1344` candidates.

## Decision Log

- Decision: Do not change terrain voxel resolution in this plan.
  Rationale: Reducing voxels per LOD is likely to help if vertex count is dominant, but it should wait until per-LOD draw, vertex, triangle, and timing data makes the expected benefit factual.
  Date/Author: 2026-06-08 / Codex

- Decision: Keep TypeScript ownership limited to UI, command forwarding, and display formatting.
  Rationale: `docs/API_CONTRACTS.md` allows HTML HUD/debug UI and smoke-test hooks, but forbids TypeScript ownership of terrain scheduling, terrain visibility, culling, WebGPU draw submission, and renderer behavior.
  Date/Author: 2026-06-08 / Codex

- Decision: Reuse existing debug command and snapshot lanes wherever possible.
  Rationale: `OFG-API-001` and `OFG-API-003` already define `game.command(...)`, `setRenderDebugOptions(...)`, `resetRenderDebugOptions()`, `getPerfStats()`, `dumpPerfStats()`, `resetPerfStats()`, `debugSnapshot()`, `rustPerfStats`, and Rust-owned `renderDebugOptions`.
  Date/Author: 2026-06-08 / Codex

- Decision: Prefer terrain LOD mask experiments before adding new renderer counters.
  Rationale: Existing Rust-owned counters already expose visible draws, culls, submitted vertices, triangles, terrain LOD counters, shadow cascade counters, and GPU pass timing. Add more counters only if those fields cannot answer which LODs dominate.
  Date/Author: 2026-06-08 / Codex

- Decision: Avoid using alternate shadow LOD geometry as an early optimization.
  Rationale: Rendering shadows from different terrain meshes can introduce subtle bias, contact, acne, and mismatch bugs. The completed shadow plan already made shadow range and culling bounded; this plan should focus on measuring the remaining terrain geometry cost.
  Date/Author: 2026-06-08 / Codex

- Decision: Do not add new Rust perf counters for this plan.
  Rationale: Existing Rust-owned terrain LOD counters, shadow cascade counters, render counters, and GPU timing summaries already identify the dominant terrain LOD costs once the capture script reports them clearly.
  Date/Author: 2026-06-08 / Codex

- Decision: Treat terrain vertex count as confirmed for the next optimization plan.
  Rationale: LOD1 and LOD2 account for `69.228%` of production submitted scene vertices. LOD1-only with shadows disabled still measures `9.786 ms` GPU total and `8.502 ms` scene GPU, nearly production cost despite rendering only `223750` submitted vertices. LOD3+ with shadows disabled measures `5.643 ms` GPU total and `4.397 ms` scene GPU with `73570` submitted vertices.
  Date/Author: 2026-06-08 / Codex

- Decision: Start the follow-up terrain optimization with an intentionally aggressive resolution experiment.
  Rationale: The next plan should test the user's proposed extreme before tuning: keep LOD0 at current detail, reduce LOD1 toward a `16x16x16` node resolution, reduce LOD2 toward `8x8x8`, and reduce LOD3+ to `8x8x8` or lower. A reasonable first target is to reduce production scene submissions from `550510` vertices to at most about `180000` vertices while preserving hole-free coverage.
  Date/Author: 2026-06-08 / Codex

## Outcomes & Retrospective

The plan delivered the requested diagnosis tooling and analysis.

The browser UI now exposes render-debug controls without DevTools. It can select terrain LOD masks, enable or disable sky rendering, enable or disable shadow-map passes, select cascade masks, enable or disable shadow sampling, force shadow sun modes, enable white diagnostic textures, switch material mode between full and Lambert, reset render debug options, and reset perf stats.

The browser UI now exposes a live perf overlay. It shows browser frame latest/average/min/max/p95, Rust frame and render CPU timing, GPU total and scene/shadow timing, visible/cull/shadow draw counts, submitted vertices/indices/triangles, terrain LOD counters, shadow cascade counters, and active render debug options.

The capture tool now writes a `terrainLodAnalysis` section and terminal summary with production LOD breakdown, dominant LODs, LOD mask render costs, and LOD mask scene-only costs with shadows disabled. The latest artifact is `C:\dev\ofg\artifacts\perf-debug\2026-06-08T21-48-13-910Z\summary.json`.

Final performance conclusion: terrain vertex count is confirmed as the next bottleneck. Production submits `550510` scene vertices and `183504` scene triangles. LOD1 is the largest contributor at `223746` vertices and `74582` triangles. LOD2 is second at `157356` vertices and `52452` triangles. Together LOD1 and LOD2 account for `381102` vertices, `127034` triangles, and `69.228%` of submitted scene geometry. LOD1-only with shadows disabled measured `9.786 ms` GPU total and `8.502 ms` scene GPU, nearly production's `10.300 ms` GPU total. The far-terrain `terrain-lod-3-plus-shadow-off` experiment measured `5.643 ms` GPU total with only `73570` submitted vertices.

Recommended follow-up: create a terrain LOD voxel-resolution optimization plan. Keep LOD0 at the current highest resolution initially. Test an aggressive first variant where LOD1 uses roughly `16x16x16`, LOD2 uses roughly `8x8x8`, and LOD3+ uses `8x8x8` or lower. The first measurable target should be production scene submissions at or below roughly `180000` vertices, down from `550510`, then compare visual acceptability, terrain seam behavior, browser smoke, Rust terrain smoke, and the same perf capture scenarios.

Residual risks: browser GPU timings remain noisy and should continue to be interpreted with vertex/draw counters. `C:\dev\ofg\src\app\game.ts` remains above the 600-line split-pressure threshold, but the new UI logic was placed in `C:\dev\ofg\src\app\renderDebugUi.ts` to avoid growing the frame loop substantially. `C:\dev\ofg\tools\browser-smoke.mjs` is also large and continues to carry broad browser integration responsibilities.

## Contract and Quality Baseline

This work must preserve the current Rust-owned engine boundary.

`OFG-API-001: Browser Shell To Rust Browser Game` remains active. Browser app code must go through the TypeScript wrapper and runtime facade, not raw wasm exports. New controls should use the existing `game.command(command)` lane. If a new command is genuinely needed, add it through `GameCommand` and document it in `C:\dev\ofg\docs\API_CONTRACTS.md`.

`OFG-API-003: Debug And Smoke-Test Hooks` remains active. `window.__ofgDebug` is a browser-only debug and test contract, not game simulation ownership. TypeScript may display, dump, and test Rust-owned perf and render debug values, but must not use them to compute terrain visibility, culling, material selection, or renderer behavior.

`OFG-API-004: Terrain Vertex And Material Layout` must remain unchanged unless a later optimization plan explicitly changes terrain mesh layout or shader contracts. This plan can count vertices and triangles, but it should not change the terrain vertex format.

`OFG-API-009: Forbidden TypeScript Ownership` is a hard constraint. Do not reintroduce a TypeScript terrain manager, terrain generator, terrain culler, WebGPU renderer, draw submission owner, material interpreter, or terrain LOD policy owner.

Quality gates from `C:\dev\ofg\PLANS.md` apply. During implementation, each milestone must be followed by the repo-local `milestone-review` skill before the Progress item is marked complete. Required findings must be fixed or explicitly rejected in the Decision Log with rationale. For implementation work, the completion gate includes coverage: run `npm run coverage:rust` and confirm modified implementation files do not appear in the default filtered coverage attention report unless this plan records an explicit exception.

## Context and Orientation

OFG is a browser-native factory game prototype. The current playable path uses TypeScript for browser setup and debug UI, while Rust owns gameplay state, terrain streaming, mesh generation, culling, WebGPU rendering, GPU resource handles, and draw submission through `engine_web.wasm`.

The relevant working directory is `C:\dev\ofg`.

Key files:

`C:\dev\ofg\src\app\game.ts` owns browser app setup, HUD wiring, the frame loop, debug hook installation, and command forwarding. It is the most likely integration point for a debug panel and live perf overlay, but keep it from growing into a monolith. If the UI code becomes substantial, place focused helpers or UI modules under `C:\dev\ofg\src\app\`.

`C:\dev\ofg\src\app\perfDebug.ts` formats and aggregates browser frame timing, Rust perf stats, renderer counters, terrain LOD counters, shadow cascade counters, render debug options, and GPU pass timings for DevTools dumps and capture artifacts.

`C:\dev\ofg\src\app\perfDebug.test.ts` contains focused TypeScript tests for perf debug formatting. Extend these tests when changing display summaries or adding overlay formatting helpers.

`C:\dev\ofg\src\engine\web\browserGameTypes.ts` defines TypeScript types for `GameCommand`, `RenderDebugOptions`, `RenderDebugOptionsUpdate`, debug snapshots, renderer status, and perf stats. Update it only to mirror Rust-owned contracts.

`C:\dev\ofg\crates\engine_web\src\perf.rs` owns Rust frame history, CPU timing spans, render counters, terrain LOD counters, shadow cascade counters, GPU timing samples, and render debug options.

`C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` owns Rust/wgpu render pass execution, render debug option application, terrain LOD masks, shadow range behavior, render counters, and GPU timestamp collection.

`C:\dev\ofg\tools\browser-perf-debug-capture.mjs` runs deterministic browser scenarios and writes JSON artifacts under `C:\dev\ofg\artifacts\perf-debug\`. Extend it to include clearer terrain LOD cost summaries.

`C:\dev\ofg\tools\browser-smoke.mjs` launches a real Chromium browser, verifies WebGPU boot and browser isolation, presses debug keys, checks HUD/debug state, and saves screenshots under `C:\dev\ofg\artifacts\browser-smoke\`. Extend it when the UI or overlay needs browser-level verification.

Terminology:

LOD means level of detail. Terrain LOD0 is the closest, most detailed terrain. Higher-numbered LODs represent farther terrain and should usually be cheaper per area.

Terrain vertex count means the number of mesh vertices submitted to the renderer for visible or shadow-rendered terrain. High vertex count can cost GPU time in vertex shading, rasterization setup, bandwidth, shadow passes, and associated draw overhead.

GPU pass timing means timestamp-query timing collected by the Rust/wgpu renderer for major render passes such as shadow maps, scene rendering, and post-processing. These timings can be noisy in a browser, so they must be interpreted alongside draw and vertex counters.

## Plan of Work

Milestone 1 adds in-browser controls for render debug options. Create a compact debug controls panel that can be opened from the app UI, a keyboard shortcut, or both. It should be usable without opening DevTools and should fit the existing app instead of looking like a marketing page or separate tool. The panel should expose the existing render diagnostics: terrain LOD mask, sky rendering, shadow pass rendering, shadow cascade mask, shadow sampling, shadow sun mode, white textures, material mode, and reset-to-production defaults. The controls must send updates through the existing command/debug lane and read back the current Rust-owned state.

The controls should include these user-visible actions:

- Select terrain LOD rendering mode: production/all, LOD0 only, LOD1 only, LOD2 only, LOD3+ only, and reset/default.
- Enable or disable sky rendering.
- Enable or disable the shadow-map pass.
- Enable all shadow cascades or individual cascades through a cascade mask.
- Enable or disable shadow sampling in the scene shader.
- Select shadow sun mode: production, overhead, angled, and low, matching existing Rust diagnostics.
- Enable or disable white diagnostic textures.
- Select material mode: full material shading or basic Lambert where supported.
- Reset render debug options to production defaults.

Milestone 2 adds a toggleable live perf overlay. The overlay should be independent of the controls panel so the developer can keep metrics visible while changing options. It should show compact, live values for browser frame time, Rust CPU spans, GPU pass timings, visible draws, culls, submitted vertices, submitted triangles, terrain LOD counters, shadow cascade counters, and the active render debug option state. It should update continuously while the game runs, but it should not destabilize layout or obscure the game view incoherently.

Milestone 3 audits and, if needed, extends terrain LOD cost counters. First inspect the current `PerfSnapshot`, `RenderCounterSample`, terrain LOD counters, shadow cascade counters, and capture JSON. If existing data can identify per-LOD scene cost, do not add Rust fields. If it cannot identify which LODs dominate scene or shadow submissions, add narrowly scoped Rust-owned counters. Candidate additions are per-LOD scene submissions, per-LOD shadow submissions, and clearer submitted-versus-visible naming. Any new contract fields must be mirrored in TypeScript types and documented in `C:\dev\ofg\docs\API_CONTRACTS.md`.

Milestone 4 extends the deterministic capture script. `C:\dev\ofg\tools\browser-perf-debug-capture.mjs` should produce a concise console summary and JSON artifact that compare production, shadows off, sky off, shadow sampling off, white textures, Lambert/basic materials, terrain LOD masks, and LOD mask variants with shadows disabled if needed to isolate scene cost. The report should identify the dominant per-LOD draw, vertex, and triangle contributors without requiring manual JSON spelunking.

Milestone 5 runs the analysis and records the conclusion. Use the new UI for sanity checks, then run the capture script and update this ExecPlan with exact artifact paths, scenario names, measurements, and a conclusion. The conclusion must say whether terrain vertex count is confirmed, rejected, or inconclusive as the next bottleneck. If confirmed, it must name specific LOD levels, current counts, and target reductions for a follow-up optimization plan.

Milestone 6 performs review, validation, and documentation cleanup. Run the relevant tests and smoke checks for the actual files changed. Run `milestone-review` after each implementation milestone. Update `C:\dev\ofg\docs\API_CONTRACTS.md` if any debug command, snapshot field, perf counter, or smoke/debug hook contract changes. Keep this plan's living sections current.

## Concrete Steps

Start from a clean understanding of the current tree:

    cd C:\dev\ofg
    git -c safe.directory=C:/dev/ofg status --short --branch
    rg "RenderDebugOptions|setRenderDebugOptions|getPerfStats|terrainLodCounters|shadowCascadeCounters" src crates tools docs

Implement Milestone 1:

    cd C:\dev\ofg
    npm run test:ts
    npm run smoke:browser

Expected result: TypeScript tests pass, browser smoke boots the Rust/wgpu app, and the smoke can verify that the debug controls exist or that the debug UI command path still works.

Implement Milestone 2:

    cd C:\dev\ofg
    npm run test:ts
    npm run smoke:browser

Expected result: the live perf overlay can be toggled on, displays non-empty current metrics after a rendered frame, and can be toggled off.

Implement Milestone 3 if Rust counters or contracts change:

    cd C:\dev\ofg
    cargo test -p engine_web
    cargo check -p engine_web --target wasm32-unknown-unknown
    npm run check:wasm

Expected result: Rust tests pass, the WASM target checks successfully, and TypeScript bindings/contracts still validate.

Implement Milestone 4:

    cd C:\dev\ofg
    node tools/browser-perf-debug-capture.mjs

Expected result: the command writes a new directory under `C:\dev\ofg\artifacts\perf-debug\` with a `summary.json` containing per-scenario timing and terrain LOD cost data. The console output should name the dominant draw, vertex, and triangle contributors.

Complete Milestone 5 and Milestone 6:

    cd C:\dev\ofg
    npm test
    npm run smoke:browser
    node tools/browser-perf-debug-capture.mjs
    npm run coverage:rust
    git -c safe.directory=C:/dev/ofg diff --check

Expected result: full tests pass, browser smoke passes, capture produces the analysis artifact, coverage does not list modified implementation files in the default attention report unless this plan records a justified exception, and the diff has no whitespace errors.

## Milestone Review

After each implementation milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective as needed.
2. Update `C:\dev\ofg\docs\API_CONTRACTS.md` or other active docs if the milestone changed contracts or ownership boundaries.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
4. Apply required review findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands and record the command names, result, and important artifacts.

Completed milestone review record:

- Scope: Milestones 1-2 browser render-debug controls and live perf overlay; Milestones 3-4 capture/reporting; Milestones 5-6 analysis, contracts, validation, and plan updates.
- Reviewers: contract, code quality, legacy ownership, correctness, and validation were reviewed locally. No sub-agents were used because the user did not explicitly request delegated sub-agent review.
- Required findings fixed: none.
- Follow-ups recorded: `C:\dev\ofg\src\app\game.ts` and `C:\dev\ofg\tools\browser-smoke.mjs` remain large enough to watch for split pressure; this plan avoided adding most UI logic to `game.ts` by introducing `C:\dev\ofg\src\app\renderDebugUi.ts`.
- Rejected findings: none.
- Validation rerun: `npm run test:ts`, `npm run smoke:browser`, `node tools/browser-perf-debug-capture.mjs`, `npm test`, `npm run coverage:rust`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: GPU timings are browser-noisy, so the conclusion relies on both GPU timing and stable draw/vertex/triangle counters.

## Validation and Acceptance

This plan is accepted when all of the following are true:

- A developer can enable and disable the render debug features from the browser UI without opening DevTools.
- The UI can reset render debug options back to production defaults.
- A developer can toggle live perf metrics on screen.
- The live overlay displays current browser frame timing, Rust CPU timing, GPU pass timing where available, visible draw count, cull count, submitted vertex count, submitted triangle count, per-LOD terrain counters, and shadow cascade counters.
- `node tools/browser-perf-debug-capture.mjs` produces a report that compares terrain LOD draw, vertex, triangle, and render costs across production and diagnostic scenarios.
- The recorded analysis identifies whether terrain vertex count is confirmed, rejected, or inconclusive as the next bottleneck.
- If terrain vertex count is confirmed, the analysis names the terrain LODs to target first and gives measurable current counts and proposed target reductions for a later optimization plan.
- `npm run test:ts` passes for TypeScript UI/debug changes.
- `cargo test -p engine_web`, `cargo check -p engine_web --target wasm32-unknown-unknown`, and `npm run check:wasm` pass if Rust perf counters or WASM/debug contracts change.
- `npm run smoke:browser` passes after the UI and overlay changes.
- `npm run coverage:rust` passes the coverage completion gate for modified Rust implementation files, or this plan records an explicit exception with rationale.
- `git -c safe.directory=C:/dev/ofg diff --check` reports no whitespace errors.

## Idempotence and Recovery

The debug UI and overlay should be additive. If the UI introduces browser instability, disable the new panel/overlay wiring while leaving the existing DevTools debug hooks intact, then re-run `npm run smoke:browser`.

Render debug options must always support reset to production defaults. If a capture scenario leaves the app in a diagnostic mode, call `resetRenderDebugOptions()` through `window.__ofgDebug` or use the UI reset button before further manual testing.

Perf capture artifacts under `C:\dev\ofg\artifacts\perf-debug\` are generated output and must not be committed. They can be deleted or regenerated safely.

If new Rust counters create contract drift, revert the new fields or update all layers together: Rust snapshot structs, wasm conversion, TypeScript types, formatting tests, capture script, smoke expectations, and `C:\dev\ofg\docs\API_CONTRACTS.md`.

## Artifacts and Notes

Baseline artifact for this plan:

    C:\dev\ofg\artifacts\perf-debug\2026-06-08T11-11-50-045Z\summary.json

Implementation artifacts:

    C:\dev\ofg\artifacts\browser-smoke\2026-06-08T21-46-59-171Z\report.json
    C:\dev\ofg\artifacts\perf-debug\2026-06-08T21-48-13-910Z\summary.json
    C:\dev\ofg\artifacts\browser-smoke\render-perf-ui-live.png

Important baseline values from that artifact:

    archived pre-fix GPU frame: 22.495 ms
    post-shadow-fix production GPU frame: 10.286 ms
    post-shadow-fix production visible draws: 78
    post-shadow-fix production scene vertices: 550510
    post-shadow-fix production shadow draws: 124
    post-shadow-fix production shadow vertices: about 1050460
    terrain-lod-3-plus GPU frame: 5.757 ms
    terrain-lod-3-plus visible draws: 14
    terrain-lod-3-plus scene vertices: 73570

These numbers are starting evidence only. The new analysis must rerun capture after the UI and counter changes and must use the latest artifact paths.

## Interfaces and Dependencies

Existing browser command and debug surfaces that should remain stable:

    game.command({ type: "setRenderDebugOptions", ... })
    game.command({ type: "resetRenderDebugOptions" })
    game.command({ type: "resetPerfStats" })
    game.debugSnapshot()
    window.__ofgDebug.getPerfStats()
    window.__ofgDebug.dumpPerfStats()
    window.__ofgDebug.resetPerfStats()
    window.__ofgDebug.setRenderDebugOptions(...)
    window.__ofgDebug.getRenderDebugOptions()
    window.__ofgDebug.resetRenderDebugOptions()

Expected render debug option fields:

    terrainLodMask
    skyEnabled
    shadowPassEnabled
    shadowCascadeMask
    shadowSamplingEnabled
    shadowSunMode
    whiteTexturesEnabled
    materialMode

Expected render/perf data families:

    browser frame timing summaries
    rustPerfStats timing summaries
    rendererStatus.lastRenderCounters
    rendererStatus.lastGpuPassTimings
    terrain LOD counters
    shadow cascade counters
    rendererStatus.renderDebugOptions

If any of these names differ in the current implementation, use the current repo names and update this plan with the exact names before implementing code against them.
