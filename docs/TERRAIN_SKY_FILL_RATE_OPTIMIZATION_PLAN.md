# Terrain And Sky Fill-Rate Optimization

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `C:\dev\ofg\PLANS.md`. It replaces the completed render-debug analysis plan archived at `C:\dev\ofg\docs\archived\RENDER_PERF_UI_AND_TERRAIN_LOD_ANALYSIS_PLAN_2026-06-08.md`.

## Purpose / Big Picture

The game now has enough instrumentation to show that frame rate is still poor after shadow culling improved. The next goal is to reduce and measure likely fill-rate and terrain pixel-shader costs without guessing. After this work, a developer should be able to run the browser build, open the render-debug panel, toggle expensive terrain and sky shader features by LOD or globally, run repeatable captures, and see whether mipmaps, terrain material sample limits, roughness sampling, and procedural cloud noise materially improve GPU time.

The user-visible outcome is not just faster rendering. It is a factual diagnosis loop: the app will expose production defaults, aggressive debug presets, and live/captured metrics so we can prove whether the terrain shader, sky cloud noise, missing mipmaps, roughness sampling, or some other pass is the current bottleneck.

## Progress

- [x] (2026-06-08 23:47Z) Created this ExecPlan as the active source of truth for terrain shader, sky cloud-noise, mipmap, and fill-rate optimization.
- [x] (2026-06-08 23:47Z) Archived the completed render-debug analysis plan at `C:\dev\ofg\docs\archived\RENDER_PERF_UI_AND_TERRAIN_LOD_ANALYSIS_PLAN_2026-06-08.md`.
- [ ] Milestone 1: Re-run and record a clean baseline capture with the current debug controls before changing shader cost.
- [ ] Milestone 2: Add Rust-owned render-debug controls and status for terrain material sample count by LOD and terrain roughness-map sampling by LOD.
- [x] (2026-06-09 06:42Z) Milestone 3: Added Rust-owned `skyCloudNoiseEnabled`, forced scene-pass sky cloud coverage to zero when disabled, and made `cloudLayer(...)` return before fBm noise at zero coverage.
- [ ] Milestone 4: Generate and sample mipmaps for terrain texture arrays.
- [ ] Milestone 5: Extend browser UI, live overlay, smoke tests, and capture scenarios for the new fill-rate controls. Cloud-noise UI, overlay text, smoke coverage, and capture scenario are complete; terrain material sample and roughness controls remain.
- [ ] Milestone 6: Run post-change captures, compare against baseline, and record a data-backed conclusion.
- [ ] Milestone 7: Run milestone review, coverage, smoke, and final documentation cleanup.

## Surprises & Discoveries

- Observation: Terrain material candidates are already sorted by descending weight before they are written into the four material slots.
  Evidence: `C:\dev\ofg\crates\terrain_core\src\material.rs` uses `pack_material_weights(...)`, sorts positive candidates by descending weight, truncates to four, and normalizes the surviving weights.

- Observation: Per-triangle terrain palettes are also ranked by aggregate material weight before expansion.
  Evidence: `triangle_material_palette(...)` in `C:\dev\ofg\crates\terrain_core\src\material.rs` sums layer weights across the triangle, sorts layers by descending weight, and writes the top four palette entries.

- Observation: The terrain shader currently samples albedo and roughness using four material weights and three triplanar axes.
  Evidence: `C:\dev\ofg\src\engine\render\shaders\uber.wgsl` calls `sampleTriplanarTerrainAlbedoLayer(...)` four times and `sampleTriplanarTerrainRoughnessLayer(...)` four times for terrain, and each triplanar helper samples x/y/z projections.

- Observation: Terrain normal maps are loaded as texture arrays but are not currently applied in terrain lighting.
  Evidence: `normalTexture` is bound in `uber.wgsl`, but the terrain path does not call `textureSample(normalTexture, ...)`; `C:\dev\ofg\docs\ARCHITECTURE.md` also states that terrain normal maps are loaded but not yet applied.

- Observation: The sky shader computes procedural cloud noise every sky pixel, and the sky pass currently draws a full-screen triangle before terrain.
  Evidence: `skyFragmentMain(...)` in `C:\dev\ofg\src\engine\render\shaders\uber.wgsl` calls `cloudLayer(...)`; `cloudLayer(...)` performs two `skyFbm2(...)` calls, each with five noise octaves. `C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` draws the sky before terrain with the sky pipeline depth compare set to `Always`.

- Observation: Terrain texture arrays currently have no mipmaps.
  Evidence: `create_texture(...)` in `C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` creates renderer texture arrays with `mip_level_count: 1`, and `C:\dev\ofg\src\engine\browser\textureAssetLoader.ts` returns only mip-0 RGBA bytes.

- Observation: Disabling procedural cloud noise produced a larger total GPU reduction than disabling the whole sky in one 120-frame capture, but this should be repeated before changing production defaults.
  Evidence: `C:\dev\ofg\artifacts\perf-debug\2026-06-09T05-29-47-678Z\summary.json` reports baseline `gpuTotalAverageMs=9.869`, `sky-off=9.284` (`-0.585ms`), and `cloud-noise-off=8.752` (`-1.117ms`) with the same visible draw count and submitted vertex count.

- Observation: The sky currently renders as a full-screen triangle before terrain, so looking at the floor still pays the sky shader for the whole render target when `skyEnabled` is true.
  Evidence: `C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` begins the scene render pass, binds `sky_pipeline`, draws `0..3`, then draws terrain/model items; `C:\dev\ofg\src\engine\render\shaders\uber.wgsl` runs `skyFragmentMain(...)` for that sky draw.

- Observation: The current capture summary labels `gpuTotalAverageMs` as total measured GPU time, but that total is the sum of all timed GPU passes, including shadow cascades. It is not only scene plus post/resolve overhead.
  Evidence: `GpuPassTimings::from_timestamp_pairs(...)` in `C:\dev\ofg\crates\engine_web\src\perf.rs` adds every `GpuTimedPass`, including `ShadowCascade(index)`, to `total_measured_ms`.

- Observation: Manual local testing reported two unresolved numbers that need dedicated frame-graph instrumentation: an all-features-off black-screen view still costs about 4ms GPU, and a direct-down terrain-only view costs about 5ms scene GPU even when only part of the screen contains LOD0 terrain.
  Evidence: User reported these measurements on 2026-06-09 after testing the debug controls. The current capture script does not yet have null-frame, clear-only, sky-after-terrain, or terrain-depth/area probe scenarios to isolate this floor.

- Observation: Switching the post-process debug view from `final` to `sceneColor` made the missing milliseconds disappear, and the shader showed that `final` was computing the DoF blurred scene unconditionally even when DoF was disabled.
  Evidence: User reported the `sceneColor` result on 2026-06-09. `postFragmentMain(...)` in `C:\dev\ofg\src\engine\render\shaders\post.wgsl` returned immediately for `POST_DEBUG_SCENE_COLOR`, but the `final` path previously computed `dofBlurredSceneColor(...)` before checking whether DoF was enabled.

## Decision Log

- Decision: Keep all new render behavior Rust-owned and use TypeScript only for controls, status display, generic browser asset decoding, and smoke/capture automation.
  Rationale: `OFG-API-001`, `OFG-API-002`, `OFG-API-003`, `OFG-API-004`, and `OFG-API-009` forbid TypeScript terrain material ownership, WebGPU ownership, renderer behavior, texture semantics, and terrain LOD policy ownership.
  Date/Author: 2026-06-08 / Codex

- Decision: Use per-LOD arrays for terrain material sample count and roughness-map enablement rather than single global booleans.
  Rationale: The optimization question is specifically whether far terrain can become cheaper while nearby terrain keeps higher-quality material blending. Per-LOD arrays allow captures like production `[4,4,4,4,4]`, conservative `[4,3,2,1,1]`, and aggressive `[4,2,1,1,1]` without changing terrain streaming or mesh generation.
  Date/Author: 2026-06-08 / Codex

- Decision: Do not add a visible terrain normal-map performance toggle unless this plan also implements real terrain normal-map sampling.
  Rationale: Terrain normal maps are currently not sampled, so a normal-map toggle would be a no-op and would corrupt the measurement story. This plan should record that current normal-map cost is zero. If terrain normal-map shading is added in this plan or a later one, it must be gated by the same per-LOD debug pattern before it becomes default.
  Date/Author: 2026-06-08 / Codex

- Decision: Implement cloud-noise disabling by making `cloudLayer(...)` return the analytic sky color before any fBm work when cloud coverage is zero, then have the Rust renderer force coverage to zero when the debug option is disabled.
  Rationale: This avoids expanding the camera uniform layout for the first diagnostic pass and makes the no-cloud sky path a real shader-cost reduction rather than a visual-only opacity change.
  Date/Author: 2026-06-08 / Codex

- Decision: Prefer GPU-side mip generation for terrain texture arrays if feasible; fall back to a tested CPU mip-chain builder only if the GPU path is blocked by WebGPU/wgpu constraints.
  Rationale: WebGPU does not automatically generate mipmaps. GPU generation avoids storing or transferring a larger CPU-side mip chain across the browser/Rust boundary. A fallback CPU path is acceptable only if it is deterministic, tested, and measured for startup impact.
  Date/Author: 2026-06-08 / Codex

- Decision: Keep production defaults visually conservative.
  Rationale: These are optimization controls. Defaults should preserve current behavior except for mipmaps, which should be production-on once verified because they improve distant sampling quality and likely performance. Material sample reductions and cloud disabling should remain debug-tunable until captures justify production changes.
  Date/Author: 2026-06-08 / Codex

## Outcomes & Retrospective

Milestone 3 shipped a real cloud-noise toggle without changing production defaults. `skyCloudNoiseEnabled` defaults true, resets with `resetRenderDebugOptions`, appears in Rust renderer status and TypeScript debug types, is visible in the browser render-debug panel as `Cloud noise`, and appears in the live perf overlay as `cloud=on/off`. When disabled, Rust writes zero cloud coverage into the scene camera uniform and WGSL returns from `cloudLayer(...)` before running procedural fBm cloud noise.

On 2026-06-09, the final post-process shader was also tightened after debug-view testing showed that `sceneColor` removed the unexplained overhead. The `final` shader path now computes the 9-tap DoF blur only when DoF is enabled or the selected debug view is `dofBlurred`. A follow-up capture at `C:\dev\ofg\artifacts\perf-debug\2026-06-09T05-50-08-634Z\summary.json` reported baseline `gpuTotalAverageMs=8.305`, `dof-on=8.828` (`+0.523ms`), and `tone-map-off=8.282`, which is consistent with DoF blur no longer being a hidden always-on cost.

Milestone review:
- Scope: Rust render debug option, scene camera-uniform override, WGSL early return, browser UI/debug hooks, smoke/capture automation, shader/WASM artifacts, and API contract docs for `skyCloudNoiseEnabled`.
- Reviewers: contract, code quality, legacy, correctness, and validation were run locally. Sub-agent tools were available, but their tool contract only permits spawning when the user explicitly asks for sub-agents or delegation.
- Required findings fixed: moved the sky cloud coverage uniform offset into `render_uniforms` and exported it so the renderer does not own a hidden layout magic number; reran the focused Rust test and confirmed the warning was gone.
- Follow-ups recorded: add null-frame / clear-only / scene-without-sky / post-disabled capture scenarios and consider rendering sky after opaque terrain or using depth to avoid paying sky over floor pixels.
- Rejected findings: none.
- Validation rerun: `cargo test -p engine_web perf_tests`, `npm run check:shaders`, `npm run test:ts`, `npm run smoke:browser`, `node tools/browser-perf-debug-capture.mjs`, `npm run coverage:rust`, `npm run check:wasm`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm test` all passed.
- Remaining risk: `cloud-noise-off` improved the measured capture, but the user-observed 4ms black-frame floor and 5ms terrain-only scene cost are not explained by this milestone and need more granular frame-graph toggles/timers before production optimization choices.

## Contract and Quality Baseline

This work must preserve the Rust-owned runtime architecture.

`OFG-API-001: Browser Shell To Rust Browser Game` remains active. New controls must go through `game.command(...)` and `debugSnapshot()`. Do not add direct wasm scalar methods for material samples, mips, or cloud settings.

`OFG-API-002: Rust Game To Browser Asset Loader` remains active. TypeScript may continue returning generic RGBA texture array bytes, but Rust owns terrain texture array roles, validation, mip generation, sampler creation, and GPU texture installation. TypeScript must not interpret the Poly Haven manifest or generate terrain-material semantics.

`OFG-API-003: Debug And Smoke-Test Hooks` must be updated if this plan adds new debug option fields, renderer status fields, capture scenarios, or UI controls. `window.__ofgDebug` can expose these controls only as command/status wrappers around Rust state.

`OFG-API-004: Terrain Vertex And Material Layout` must remain stable unless explicitly changed. The intended implementation does not change the 19-float terrain vertex layout. It uses existing ordered material slots and existing object-uniform spare texture option fields to control how many ordered slots the shader samples.

`OFG-API-009: Forbidden TypeScript Ownership` is a hard constraint. Do not add a TypeScript terrain material selector, terrain LOD policy, WebGPU texture generator, renderer, culler, or scene graph.

Quality gates from `C:\dev\ofg\PLANS.md` apply. After every implementation milestone, run the repo-local `milestone-review` skill before marking the milestone complete. Apply required findings or record a rejection with rationale in the Decision Log. For implementation work, run `npm run coverage:rust` before completion and confirm modified Rust implementation files do not appear in the default filtered coverage attention report unless this plan records an explicit exception.

## Context and Orientation

The working directory is `C:\dev\ofg`.

OFG renders through Rust/wgpu in `C:\dev\ofg\crates\engine_web`. Browser TypeScript in `C:\dev\ofg\src\app` owns DOM UI, input, debug hooks, and command forwarding. Terrain generation and material packing live in Rust `terrain_core`; shader source lives in checked-in WGSL under `C:\dev\ofg\src\engine\render\shaders`.

Relevant files and current responsibilities:

`C:\dev\ofg\crates\terrain_core\src\material.rs` packs material layer indices and weights into terrain vertices. It currently keeps the top four positive material candidates sorted by descending weight. This file should get tests that lock down the priority behavior because the shader will rely on slot order when sampling fewer than four materials.

`C:\dev\ofg\src\engine\render\shaders\uber.wgsl` contains the terrain, model, shadow-debug, and sky scene shader. Terrain currently does triplanar albedo and triplanar roughness over up to four material slots. Sky currently computes analytic sky, sun, procedural cloud noise, night sky, stars, and moon glow.

`C:\dev\ofg\crates\engine_web\src\wgpu_renderer.rs` owns WebGPU texture creation, sampler creation, render-pass submission, render debug option application, sky drawing, terrain drawing, object uniforms, renderer status, and command parsing.

`C:\dev\ofg\crates\engine_web\src\perf.rs` defines `RenderDebugOptions`, `RenderDebugOptionsUpdate`, render counters, perf history, and debug-option validation.

`C:\dev\ofg\crates\engine_web\src\render_uniforms.rs` packs object uniforms. `ObjectUniforms.textureOptions` is already a `vec4<f32>` in WGSL. Its x component is material workflow and y component is texture scale. The z and w components are currently unused by terrain and can carry terrain-only debug shader settings without changing the uniform size.

`C:\dev\ofg\src\engine\web\browserGameTypes.ts` mirrors command and debug snapshot types for TypeScript. Update it when Rust-owned command/status fields change.

`C:\dev\ofg\src\app\renderDebugUi.ts`, `C:\dev\ofg\src\app\game.ts`, `C:\dev\ofg\src\main.ts`, and `C:\dev\ofg\index.html` own the visible debug panel wiring. They must only forward user selections to Rust commands and read Rust-owned status.

`C:\dev\ofg\tools\browser-smoke.mjs` runs real-browser integration checks. Extend it to exercise the new controls and verify reset/default state.

`C:\dev\ofg\tools\browser-perf-debug-capture.mjs` runs deterministic performance captures and writes JSON under `C:\dev\ofg\artifacts\perf-debug\`. Extend it with fill-rate scenarios and record exact artifact paths in this plan.

Definitions:

LOD means terrain level of detail. LOD0 is the near, highest-detail terrain; larger LOD numbers are farther/coarser terrain.

Material sample count means the number of ordered terrain material slots the WGSL terrain shader samples per pixel. Today it samples all four slots for albedo and roughness. A sample count of one means only slot 0 is used and remaining material weights are ignored in the shader.

Triplanar sampling means sampling the same texture layer along x, y, and z projections and blending by surface normal. One material slot costs three texture samples per map. Four material slots cost twelve samples per map.

Mipmaps are prefiltered smaller versions of a texture. Without mipmaps, distant terrain samples full-resolution textures at high frequency, which can waste bandwidth/cache and create shimmer.

Cloud noise means the procedural fBm noise in the sky shader. This is not a texture; it is math run per sky pixel. Long-term, cloud detail may be baked into cubemaps or other lookup textures, but this plan only adds a real off switch and measures the effect.

## Plan of Work

Milestone 1 records a clean baseline. Run the current `node tools/browser-perf-debug-capture.mjs` before changing shader or texture behavior. Record production GPU totals, scene pass time, sky-off delta, white-textures delta, Lambert delta, post-process deltas, terrain submitted vertices/triangles, and LOD breakdown in this plan. This gives every later measurement a stable comparison point.

Milestone 2 makes terrain material sample count and roughness sampling controllable by LOD. In Rust, extend `RenderDebugOptions` and `RenderDebugOptionsUpdate` with two validated per-LOD settings:

    terrainMaterialSampleCountsByLod: [u32; 5], valid values 1..=4
    terrainRoughnessEnabledByLod: [bool; 5]

Use five entries for current LOD0 through LOD4. If the runtime later renders LODs above 4, clamp to the final entry. Defaults are material samples `[4,4,4,4,4]` and roughness enabled `[true,true,true,true,true]`.

In `wgpu_renderer.rs`, when preparing each terrain draw, derive the current item's LOD from `PreparedRenderItem.terrain_lod` and write two terrain-only shader controls into the existing object uniform:

    object.textureOptions.z = material sample count as f32
    object.textureOptions.w = terrain map flags as f32, with bit 0 for roughness enabled

Do this only for terrain workflow objects. Model rendering must preserve its existing material semantics.

In `uber.wgsl`, add helper functions to read `textureOptions.z` and `textureOptions.w`. Update `sampleTerrainAlbedo(...)` so it samples slot 0 always, slot 1 only when sample count is at least 2, slot 2 only when at least 3, and slot 3 only when 4. Renormalize the sampled weights in the shader so a one-sample mode uses slot 0 at full weight. Update `sampleTerrainRoughness(...)` so disabled roughness returns a stable constant roughness instead of sampling the roughness texture array. Do not add terrain normal-map sampling in this milestone; record explicitly that normal-map runtime cost is currently zero.

Add tests in `terrain_core` proving material slot priority order is descending by weight with deterministic tie-breakers. Add Rust tests in `engine_web` proving the new debug options validate arrays, default to production behavior, serialize to JS, and reach renderer status. Add shader contract tests proving the sample-count and roughness flag helpers exist.

Milestone 3 makes procedural cloud noise optional. Extend `RenderDebugOptions` and status with a Rust-owned `skyCloudNoiseEnabled` boolean, default true. In `wgpu_renderer.rs`, when building frame uniforms for the scene pass, force sky cloud coverage to zero if this option is false. In `uber.wgsl`, modify `cloudLayer(...)` so it immediately returns `skyColor` before computing wind, UV, fBm, broad noise, or detail noise when coverage is effectively zero. The `skyEnabled` option should still disable the whole sky draw; the new cloud option keeps sky enabled but removes procedural cloud work and cloud visuals.

Add tests that the render debug option updates/reset include `skyCloudNoiseEnabled`, that renderer status exposes it, and that the shader contract contains the early return before fBm work. Extend capture scenarios with `cloud-noise-off` separate from `sky-off`.

Milestone 4 adds mipmaps for terrain texture arrays. Implement this in Rust/wgpu, not in TypeScript terrain logic. The preferred path is:

1. Compute mip count as `floor(log2(max(width,height))) + 1` for terrain albedo, normal, and material arrays.
2. Create terrain texture arrays with `mip_level_count` set to that value and usage including `TEXTURE_BINDING`, `COPY_DST`, and whatever GPU mip generation needs, likely `RENDER_ATTACHMENT`.
3. Upload mip 0 as today.
4. Generate each subsequent mip level for each array layer using a small Rust-owned GPU blit/downsample pipeline. If this path is blocked, implement a deterministic CPU mip-chain builder in Rust and upload every mip level, but record the fallback and startup cost in Surprises & Discoveries.
5. Create texture views that include all mip levels.
6. Update the terrain sampler to use a real mip filter, likely `mipmap_filter: wgpu::FilterMode::Linear`, while preserving linear min/mag filters and repeat wrap modes.

Add tests for mip-count calculation, texture metadata where testable, and any CPU fallback mip builder. Add smoke or native render coverage that checks nonblank terrain still renders with mipped texture arrays. Update `OFG-API-002` if any texture asset contract changes. The preferred GPU path should not change the TypeScript `RgbaTextureArrayAsset` shape because the browser still returns only mip 0.

Milestone 5 extends the browser UI, live overlay, smoke, and capture automation. Add controls to the existing render-debug panel for:

    terrain material sample preset: production, balanced, aggressive, custom
    terrain roughness sampling preset: all LODs, near only, off, custom
    sky cloud noise enabled

If custom per-LOD numeric controls fit cleanly in the panel, add them. Otherwise implement preset buttons/selects first and expose custom per-LOD arrays through `window.__ofgDebug.setRenderDebugOptions(...)` and the capture script. Do not add a normal-map toggle unless real terrain normal-map sampling is implemented. Instead, the overlay/capture notes should report that terrain normal maps are currently not sampled.

Update `buildPerfOverlayLines(...)` in `C:\dev\ofg\src\app\perfDebug.ts` to show material sample counts, roughness mask, cloud noise state, and mip status if exposed by renderer status. Extend `tools/browser-smoke.mjs` to exercise the new UI controls, wait for Rust status to change, reset to defaults, and verify screenshots remain nonblank. Extend `tools/browser-perf-debug-capture.mjs` with scenarios:

    production
    sky-off
    cloud-noise-off
    white-textures
    lambert-material
    material-samples-balanced, for example [4,3,2,1,1]
    material-samples-aggressive, for example [4,2,1,1,1]
    material-samples-one, [1,1,1,1,1]
    roughness-near-only, for example [true,true,false,false,false]
    roughness-off
    combined-aggressive-fill-rate

Capture reports must include active debug settings, mip status, scene GPU time, total GPU time, sky draw state, terrain LOD counters, submitted vertices/triangles, and notes about normal map sampling being absent.

Milestone 6 runs the post-change analysis. Run the capture script several times if needed to separate browser timing noise from real deltas. Record the artifact path and a concise table in this plan. The conclusion must answer:

- Did mipmaps reduce distant terrain scene cost or mainly improve quality/stability?
- Did limiting material samples by LOD reduce scene GPU time?
- Did disabling roughness sampling reduce scene GPU time?
- Did cloud-noise-off approach the sky-off delta, or is the sky cost elsewhere?
- Is terrain far-LOD shader cost a meaningful percentage of frame time after mipmaps?
- Should any debug setting become a production default, or should the next optimization be terrain geometry/LOD resolution, sky pass ordering, sky cubemap baking, or something else?

Milestone 7 completes review and validation. Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` if contracts, debug options, mipmap ownership, or shader behavior changed. Run milestone review after each implementation milestone. Run the full validation commands in this plan and record results. Commit and push regularly.

## Concrete Steps

Start from the repo root:

    cd C:\dev\ofg
    git -c safe.directory=C:/dev/ofg status --short --branch
    rg "RenderDebugOptions|textureOptions|sampleTerrainAlbedo|sampleTerrainRoughness|cloudLayer|mip_level_count|terrain texture sampler" crates src tools docs

Milestone 1 baseline:

    cd C:\dev\ofg
    node tools/browser-perf-debug-capture.mjs

Expected result: a new directory under `C:\dev\ofg\artifacts\perf-debug\` with `summary.json`. Record the artifact path and key numbers in Surprises & Discoveries.

Milestone 2 terrain shader controls:

    cd C:\dev\ofg
    cargo test -p terrain_core material
    cargo test -p engine_web perf_tests
    npm run build:shaders
    npm run test:ts

Expected result: material priority tests pass, render-debug validation tests pass, generated shader artifacts are fresh, and TypeScript tests still pass.

Milestone 3 cloud-noise toggle:

    cd C:\dev\ofg
    cargo test -p engine_web perf_tests
    npm run check:shaders
    npm run test:ts

Expected result: Rust debug-option tests pass, shader metadata is current, and TypeScript command/status types are valid.

Milestone 4 mipmaps:

    cd C:\dev\ofg
    cargo test -p engine_web
    npm run check:wasm
    npm run smoke:rust
    npm run smoke:browser

Expected result: Rust tests pass, wasm binding checks pass, native render smoke produces nonblank terrain/sky images, and browser smoke passes with mipped terrain textures.

Milestone 5 UI and capture:

    cd C:\dev\ofg
    npm run test:ts
    npm run smoke:browser
    node tools/browser-perf-debug-capture.mjs

Expected result: browser UI controls are verified by smoke, capture scenarios run successfully, and JSON includes the new active debug settings.

Milestone 6 and final validation:

    cd C:\dev\ofg
    npm test
    npm run smoke
    node tools/browser-perf-debug-capture.mjs
    npm run coverage:rust
    git -c safe.directory=C:/dev/ofg diff --check

Expected result: full tests pass, Rust and browser smoke pass, capture produces the final analysis artifact, coverage does not list modified Rust implementation files in the default attention report unless a justified exception is recorded, and the diff has no whitespace errors.

## Milestone Review

After each implementation milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective.
2. Update `C:\dev\ofg\docs\API_CONTRACTS.md` or `C:\dev\ofg\docs\ARCHITECTURE.md` if the milestone changed contracts, ownership, shader behavior, or debug surfaces.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands and record command names, results, artifacts, and remaining risks.

## Validation and Acceptance

This plan is accepted when all of the following are true:

- Terrain material slot order is tested and documented as priority order.
- The terrain shader can sample fewer than four ordered material slots per pixel according to Rust-owned per-LOD debug settings.
- The terrain shader can skip roughness-map texture sampling by LOD according to Rust-owned debug settings.
- The plan records that current terrain normal maps have no terrain shader cost, or implements real terrain normal-map sampling with a per-LOD debug gate before exposing a normal-map toggle.
- Procedural cloud noise can be disabled while keeping the analytic sky visible.
- Terrain texture arrays use mipmaps and a sampler with mip filtering in the Rust/wgpu renderer.
- Browser UI or debug hooks can set and reset the new controls without TypeScript computing rendering behavior.
- Live overlay and capture artifacts report the new active settings clearly enough to interpret captures.
- Browser smoke verifies the controls, reset behavior, and nonblank rendering.
- Capture analysis records before/after measurements and says which hypotheses were confirmed, rejected, or inconclusive.
- `npm test` passes.
- `npm run smoke` passes.
- `node tools/browser-perf-debug-capture.mjs` produces the final analysis artifact.
- `npm run coverage:rust` satisfies the coverage gate for modified Rust implementation files, or this plan records an explicit exception with rationale.
- `git -c safe.directory=C:/dev/ofg diff --check` reports no whitespace errors.

## Idempotence and Recovery

All render-debug controls must reset to production defaults through `resetRenderDebugOptions`. If a capture scenario leaves the app in an aggressive diagnostic state, reset through the UI, `window.__ofgDebug.resetRenderDebugOptions()`, or a browser reload before manual inspection.

Mip generation must be deterministic and rerunnable. If GPU mip generation fails on a browser adapter, keep the mip generation path behind a narrow Rust helper and fall back to mip level 0 only as a temporary diagnostic with a clear renderer status field such as `terrainMipmapsEnabled: false` and `terrainMipmapReason`.

Generated shader artifacts under `C:\dev\ofg\src\generated\render\` may be regenerated with `npm run build:shaders`. Generated build output under `dist`, `dist-test`, and `artifacts` must not be committed.

If a new debug field causes contract drift, update all layers together: Rust `RenderDebugOptions`, JS command parsing, renderer status conversion, TypeScript types, browser UI, perf overlay, smoke tests, capture script, and `docs/API_CONTRACTS.md`.

If a visual artifact appears after mipmaps or material sample limiting, first reset debug options to production defaults. Then isolate with capture scenarios: production mips on, mips off if a temporary debug path exists, roughness all on/off, material sample counts all four/one, cloud-noise on/off, and sky off.

## Artifacts and Notes

Pre-plan evidence:

    C:\dev\ofg\artifacts\perf-debug\2026-06-08T21-48-13-910Z\summary.json
    Production submitted 550510 scene vertices and 183504 scene triangles.
    LOD1 and LOD2 together accounted for 381102 submitted vertices, or 69.228%.

Post-effect control capture evidence:

    C:\dev\ofg\artifacts\perf-debug\2026-06-08T22-25-37-086Z\summary.json
    First post-effect scenarios did not show clean post-process savings.
    Sky-off, white-textures, Lambert, and LOD masks remained more plausible next targets.

Current shader-cost facts to verify again during Milestone 1:

    terrain albedo cost at full quality: 4 material slots * 3 triplanar axes = 12 albedo texture samples per terrain pixel
    terrain roughness cost at full quality: 4 material slots * 3 triplanar axes = 12 roughness texture samples per terrain pixel
    terrain normal map cost today: 0 terrain texture samples because normal maps are not applied in terrain shading
    shadow sampling cost when enabled: up to 9 shadow compare samples per terrain/model pixel inside a shadow cascade
    sky cloud noise cost: two 5-octave fBm calls per sky pixel when clouds are active

## Interfaces and Dependencies

Expected new or changed Rust debug fields:

    RenderDebugOptions.terrain_material_sample_counts_by_lod: [u32; 5]
    RenderDebugOptions.terrain_roughness_enabled_by_lod: [bool; 5]
    RenderDebugOptions.sky_cloud_noise_enabled: bool

Expected TypeScript mirrors:

    RenderDebugOptions.terrainMaterialSampleCountsByLod: readonly [number, number, number, number, number]
    RenderDebugOptions.terrainRoughnessEnabledByLod: readonly [boolean, boolean, boolean, boolean, boolean]
    RenderDebugOptions.skyCloudNoiseEnabled: boolean

Expected production defaults:

    terrainMaterialSampleCountsByLod = [4, 4, 4, 4, 4]
    terrainRoughnessEnabledByLod = [true, true, true, true, true]
    skyCloudNoiseEnabled = true
    terrain mipmaps enabled when supported

Expected shader use:

    object.textureOptions.z = terrain material sample count for terrain draws
    object.textureOptions.w bit 0 = terrain roughness sampling enabled
    cloudLayer(...) returns skyColor before fBm when effective cloud coverage is zero

Expected renderer status additions:

    rendererStatus.renderDebugOptions contains the new fields
    rendererStatus.terrainMipmapsEnabled: boolean
    rendererStatus.terrainMipmapLevelCount: number
    rendererStatus.terrainMipmapReason?: string, only if disabled or unavailable

Expected capture presets:

    production
    sky-off
    cloud-noise-off
    white-textures
    lambert-material
    material-samples-balanced
    material-samples-aggressive
    material-samples-one
    roughness-near-only
    roughness-off
    combined-aggressive-fill-rate

Any names that differ during implementation must be updated here before code relying on them is marked complete.
