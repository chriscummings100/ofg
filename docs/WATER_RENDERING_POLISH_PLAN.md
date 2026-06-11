# Improve Sea Water Visual Quality

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `PLANS.md`.

## Purpose / Big Picture

The current sea-level water is functional but not yet convincing. It has three visible problems to address:

1. Planar reflections show odd behavior at screen edges and should be disabled for normal play until the reflection math is fixed.
2. The water is too transparent near shallow edges, so the shoreline does not read as dense water.
3. The surface needs visible small animated ripples and shoreline foam. "Moving waves" in this plan means small shader ripples and foam motion, not large ocean swells or water-geometry displacement.

After this change, the default view should show denser, livelier sea water without broken planar reflection artifacts. Reflections remain a later repair target, not removed from the codebase.

## Progress

- [x] (2026-06-11) Recorded follow-on water rendering polish scope after packet-driven bathymetry milestone.
- [x] (2026-06-11) Disable planar reflections by default and update debug/smoke expectations.
- [x] (2026-06-11) Tune water density and near-shore opacity so shallow edges are less glassy.
- [x] (2026-06-11) Add small animated ripples to the water shader without moving water geometry.
- [x] (2026-06-11) Add procedural shoreline foam driven by bathymetry/depth and animated noise.
- [x] (2026-06-11) Analyse and document the planar reflection screen-edge failure mode before re-enabling reflections.
- [x] (2026-06-11) Replace nearest-neighbor bathymetry atlas reads with tile-safe manual bilinear filtering and tighten foam to avoid broad blocky shelves.
- [x] (2026-06-11) Run milestone review, tests, shader/wasm checks, browser smoke, screenshot inspection, and coverage gate.

## Surprises & Discoveries

- Observation: The current shader already has a time-dependent `waveNormal`, but the effect is a simple sine/cosine perturbation and does not read as convincing small ripples.
  Evidence: `src/engine/render/shaders/water.wgsl` computes `waveNormal(worldXZ, timeSeconds)` from three smooth wave terms and only affects normals/reflection distortion.

- Observation: The implemented polish keeps water geometry fixed.
  Evidence: `waterPatchVertexMain` still places patch vertices on `water.settings.z` and the new ripple work stays in fragment normal/detail functions (`waveNormal`, `rippleSlope`, `foamAmount`).

- Observation: The close shoreline view exposed blocky shallow-water/foam transitions.
  Evidence: the bathymetry atlas is `R32Float` and bound as non-filterable, and the shader was using `textureLoad(floor(atlasPixel))`, so a 32x32 packet could show as visible texel-sized steps. `loadBathymetryDepth` now performs manual bilinear interpolation between four `textureLoad` samples and clamps interpolation inside the tile to avoid neighboring atlas bleed.

- Observation: The first foam pass made broad shallow shelves too pale.
  Evidence: close capture `artifacts/water-polish/close-shoreline-bilinear.png` showed large pale foam slabs. The foam mask now requires a thin-water band plus local bathymetry gradient and a higher animated-noise threshold; close capture `artifacts/water-polish/close-shoreline-bilinear-tight-foam.png` is less slabby.

- Observation: Reflection edge artifacts are likely caused by reflection UV validity rather than bathymetry.
  Evidence: `reflectionUv` projects the water-world position through `reflectionViewProjection`, divides by clip `w`, then clamps UVs into `0.001..0.999`. When projected coordinates fall outside the reflection render, clamping can smear edge pixels across the water instead of fading to non-reflective water.

- Observation: The reflection shader samples reflection color only; it does not use reflection linear depth to reject invalid reflected geometry or fade missing reflected content.
  Evidence: `water.wgsl` binds `reflectionColorTexture` but not the reflection linear-depth target. `crates/engine_web/src/water_renderer.rs` creates `reflection_linear_depth`, but the composite bind group exposes only the color texture.

- Observation: The reflection render path does not currently mention an oblique clip plane at the water plane.
  Evidence: `crates/engine_web/src/wgpu_renderer.rs` builds a mirrored frame packet with `build_reflection_frame_packet_from_engine_snapshot` and renders terrain/model pipelines into reflection targets. A future fix should verify that geometry on the wrong side of the water plane cannot leak into the reflection.

## Decision Log

- Decision: Disable reflections for default play before polishing water.
  Rationale: Broken screen-edge reflection artifacts make the water look less stable than no reflections. Default water should favor a clean dense surface while reflection repair remains tracked.
  Date/Author: 2026-06-11 / Codex

- Decision: Keep reflection code and debug plumbing present, but treat it as experimental until the reflection fix milestone.
  Rationale: The existing render path, status fields, debug view, and smoke hooks are useful for diagnosis. Removing them would make the later repair harder and add churn.
  Date/Author: 2026-06-11 / Codex

- Decision: Improve shallow-water density with shader tuning, not bathymetry regeneration.
  Rationale: The user-visible issue is optical: shallow edges are too transparent. The bathymetry packet already supplies bottom depth and the opaque depth target supplies eye-ray path length, so the first fix belongs in water color/absorption curves.
  Date/Author: 2026-06-11 / Codex

- Decision: Implement small animated ripples as shader normal/detail terms, not vertex displacement.
  Rationale: The requested motion is small surface texture/ripple movement. Geometry displacement would require patch tessellation, water-edge stability work, and collision/render-depth decisions that are larger than this polish slice.
  Date/Author: 2026-06-11 / Codex

- Decision: Implement foam procedurally from bottom depth, bathymetry gradient, and animated noise.
  Rationale: Terrain-job bathymetry gives a cheap shoreline signal. A procedural mask avoids new asset loading and can be validated in shader tests and screenshots.
  Date/Author: 2026-06-11 / Codex

## Outcomes & Retrospective

Completed. Defaults now keep planar reflections off while preserving the debug toggle path. Water defaults are denser (`reflection_enabled = false`, stronger absorption, shallower/deeper style thresholds of `1.25m`/`18m`, and stronger small-ripple settings). The shader now uses tile-safe manual bilinear bathymetry sampling, bathymetry gradient sampling, a shallow-water density floor, multi-frequency small ripple normals, and procedural shoreline foam. Browser smoke passed and the final water screenshots show denser, less glassy shorelines with no default planar reflection artifacts. A close shoreline capture verified that manual bilinear filtering and tighter foam reduce the blocky/pale shelf artifact, though future water work should consider higher-resolution or apron-backed bathymetry if patch-edge discontinuities remain visible in very low close views.

## Contract and Quality Baseline

`OFG-API-004` owns terrain vertex layout, render targets, and water shaders. This plan changes water defaults, water shader behavior, generated shader artifacts, Rust/wgpu water resource code, browser debug expectations, and smoke screenshots in one milestone set.

`OFG-API-009` forbids TypeScript ownership of water generation, bathymetry filling, optical path-length calculation, reflection-camera construction, and draw/composite behavior. This plan preserves that. TypeScript may expose controls and assert debug status; Rust and WGSL own water behavior.

Shader changes must update `src/engine/render/shaders/water.wgsl`, generated shader artifacts, `src/engine/render/shaders/WaterShader.test.ts`, and Rust/wgpu pipeline code if bindings or uniform layout change.

Quality gates: run `cargo test -p engine_web water`, targeted renderer tests affected by defaults, `npm run check:shaders`, `npm run check:wasm` if WASM/debug contracts change, `npm run smoke:browser`, and `npm run coverage:rust`. If water screenshots or smoke expectations change, inspect `browser-water-final.png`, `browser-water-bottom-depth.png`, and any new foam/ripple debug captures before marking the milestone complete.

## Context and Orientation

`crates/engine_web/src/water.rs` owns `WaterSettings`, defaults, status, and command validation. It now defaults `reflection_enabled` to `false`, uses `shallow_depth_meters = 1.25`, `deep_depth_meters = 18.0`, absorption `[0.18, 0.075, 0.030]`, `wave_scale = 0.11`, and `wave_strength = 0.34`.

`crates/engine_web/src/water_renderer.rs` owns WebGPU water resources. It creates opaque scene targets, reflection targets, a bathymetry atlas, water uniforms, and a water patch instance buffer. Its bind group currently exposes opaque color, opaque linear depth, bathymetry, reflection color, sampler, and uniforms.

`src/engine/render/shaders/water.wgsl` copies opaque scene targets, draws water patch instances, computes bottom depth with manual bilinear filtering inside each bathymetry atlas tile, computes local bathymetry gradient from that filtered depth, computes optical path length from the opaque linear-depth target, applies denser absorption/tint/specular, adds small animated ripple normals, adds procedural shoreline foam, and optionally samples planar reflection color when the experimental reflection setting is enabled.

`crates/engine_web/src/wgpu_renderer.rs` owns reflection frame construction, reflection pass execution, water settings commands, water packet upload/removal, and debug status serialization.

`src/app/renderDebugUi.ts`, `src/engine/web/browserGameTypes.ts`, fixtures, and `tools/browser-smoke.mjs` expose and validate water debug controls and status. They must follow Rust-owned state rather than inventing water behavior.

## Plan of Work

First, disable planar reflections for normal play. Change `WaterSettings::new` so `reflection_enabled` defaults to `false`, update Rust tests, debug fixtures, browser smoke default expectations, and any UI reset behavior that currently assumes reflection is on. Keep the debug toggle and reflection debug view available for analysis unless a direct bug forces a temporary hard-disable.

Second, tune water density. Adjust absorption and color/opacity curves in `water.wgsl` and defaults in `water.rs` so shallow shoreline water gets a minimum visible water tint instead of becoming nearly transparent. Prefer a physically motivated curve: retain optical path-length absorption, but add a bottom-depth contribution or shallow-edge density floor so water at the edge reads as water. Keep bottom-depth and path-length debug views honest.

Third, add small animated ripples. Replace or extend `waveNormal` with a multi-frequency, low-amplitude ripple normal built from stable procedural waves/noise. The ripples should move over time, affect specular highlights and subtle color/reflection distortion, and avoid moving patch geometry or making the sea look like large rolling waves.

Fourth, add foam. Compute a foam mask from shallow bottom depth, local bathymetry gradient, and animated procedural noise. Blend a restrained off-white/blue foam color near shorelines and possibly on strong ripple crests. The foam should be irregular, moving, and thin; it should not blanket entire shallow water patches.

Fifth, analyse planar reflection repair. Add notes or tests that reproduce the edge artifact, then plan the fix: reject or fade reflection samples whose projected UV falls outside the reflection target, avoid clamped-edge smearing, consider binding reflection linear depth for validity/fade, and verify whether the mirrored camera needs an oblique water-plane clip.

## Concrete Steps

Run from `C:\dev\ofg`:

    cargo test -p engine_web water
    npm run check:shaders
    npm run check:wasm
    npm run smoke:browser
    npm run coverage:rust

For shader-only iterations, run the faster checks first:

    npm run check:shaders
    cargo test -p engine_web water

For final visual acceptance, inspect browser smoke artifacts:

    artifacts/browser-smoke/<timestamp>/browser-water-final.png
    artifacts/browser-smoke/<timestamp>/browser-water-bottom-depth.png

If reflection diagnostics add a new debug screenshot, record its path in this plan.

## Milestone Review

After each implementation milestone, run the repo-local `milestone-review` skill against this ExecPlan and the relevant diff. Apply required findings before marking the milestone complete, or record rejected findings here with rationale.

Milestone 1 review target: reflection disabled by default, docs/tests/smoke expectations updated, no TypeScript water ownership added.

Milestone 2 review target: density/ripple/foam shader and Rust setting changes, generated shader artifacts fresh, screenshots inspected.

Milestone 3 review target: reflection edge-artifact analysis captured with concrete evidence and a future repair design.

Milestone review:

- Scope: default water settings, water shader density/ripple/foam/filtering, generated shader/WASM artifacts, smoke expectations, active API/architecture docs, and visual artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes using the repo-local `milestone-review` skill. Sub-agent reviewers were not used because this milestone was not explicitly requested as a delegated review.
- Required findings fixed: stale default reflection expectations were updated in `tools/browser-smoke.mjs`, `tests/fixtures/debugSnapshotFixtures.ts`, and `src/app/perfDebug.test.ts`; active docs now state that planar reflections are default-off and experimental; bathymetry atlas reads now use manual bilinear filtering instead of nearest texel loads; foam was tightened after close-view inspection showed broad pale slabs.
- Follow-ups recorded: split or shrink `crates/engine_web/src/water_renderer.rs` before the next substantial water-renderer growth because it is now 1037 lines; future reflection repair should add UV validity/fade, consider reflection linear-depth validation, and verify an oblique water-plane clip; if close low-angle patch seams remain objectionable, evaluate higher-resolution bathymetry, apron/gutter texels, or neighbor-aware filtering.
- Rejected findings: the remaining `waterReflectionEnabled: true` in `tools/browser-smoke.mjs` is intentional because it verifies the debug UI can still opt into the experimental reflection path.
- Validation rerun: `cargo test -p engine_web water`, `npm run check:shaders`, `npm run check:wasm`, `npm run test:ts`, `npm run smoke:browser`, `npm run coverage:rust`, close shoreline Playwright capture, screenshot inspection, and `git diff --check`.
- Motion check: same-camera Playwright captures `artifacts/water-polish/ripple-motion-a.png` and `artifacts/water-polish/ripple-motion-b.png` were 98 renderer frames apart with measurable water-band pixel changes while reflections stayed disabled.
- Remaining risk: small close-range discontinuities can still appear at water patch or terrain edges because packets are node-local and do not yet carry neighbor/apron bathymetry.

## Validation and Acceptance

Acceptance criteria:

- Default water status reports `waterReflectionEnabled: false`, and default browser screenshots do not show planar reflection edge artifacts.
- The reflection debug path remains available for future diagnosis, or any temporary hard-disable is explicitly documented with rationale.
- Final water screenshot shows a denser shoreline: shallow water has visible tint/opacity and does not look like clear glass over terrain.
- Small animated ripples are visible over time in normal play without large water-plane displacement.
- Foam appears near shorelines and shallow/breaking-looking edges, moves subtly over time, and does not cover entire water bodies.
- Browser smoke still passes and includes meaningful water-like pixels in final water and grayscale coverage in bottom-depth debug.
- Rust coverage default attention output reports no modified implementation files below the 90% line threshold, or this plan records an explicit exception.

Validation evidence:

    cargo test -p engine_web water
    # 10 passed

    npm run check:shaders
    # passed

    npm run check:wasm
    # passed

    npm run test:ts
    # 115 passing

    npm run coverage:rust
    # files below 90% line coverage: none

    npm run smoke:browser
    # passed; artifacts/browser-smoke/2026-06-11T11-28-53-931Z
    # waterReflectionEnabled: false
    # waterLikePixels: 11255 / 56700
    # bottomDepthDebugPixels: 9774 / 56700
    # workerFailedDelta: 0, workerStaleCompletionDelta: 0, synchronousBuildDelta: 0

    close shoreline Playwright capture against http://127.0.0.1:5173
    # artifacts/water-polish/close-shoreline-bilinear-tight-foam.png

    same-camera ripple motion capture against http://127.0.0.1:5173
    # artifacts/water-polish/ripple-motion-a.png
    # artifacts/water-polish/ripple-motion-b.png
    # frameA: 94, frameB: 192, changedRatio: 0.0077 in the sampled water band

    git diff --check -- <water polish files>
    # no whitespace errors; CRLF conversion warnings only

## Idempotence and Recovery

All shader artifacts can be regenerated with `npm run build:shaders`. WASM/debug contract artifacts can be regenerated with `npm run build:wasm`. If the foam/ripple tuning becomes too noisy, keep reflection disabled and density tuning in place, then revert only the foam/ripple shader section and retry with smaller procedural terms.

If reflection analysis uncovers a larger render architecture problem, do not re-enable reflections as part of this polish milestone. Record the finding and make a dedicated reflection repair plan.

## Artifacts and Notes

Starting visual reference from the completed bathymetry milestone:

    artifacts/browser-smoke/2026-06-11T09-40-37-758Z/browser-water-final.png
    artifacts/browser-smoke/2026-06-11T09-40-37-758Z/browser-water-bottom-depth.png

Observed user feedback to preserve:

    The water is not bad, but planar reflections show odd screen-edge behavior.
    Disable planar reflections for now and fix later.
    Water is too transparent at the edge.
    Add moving waves, meaning small ripples, plus foam.

## Interfaces and Dependencies

Expected Rust settings after Milestone 1:

    WaterSettings {
      enabled: true,
      reflection_enabled: false,
      sea_level_meters: 0.0,
      shallow_depth_meters: tuned value,
      deep_depth_meters: tuned value,
      absorption_rgb: tuned value,
      wave_scale: tuned ripple scale,
      wave_strength: tuned ripple strength,
      debug_view: WaterDebugView::Final
    }

Expected shader behavior:

    waterPatchFragmentMain
      samples bathymetry for vertical bottom depth
      samples opaque linear depth for eye-ray path length
      applies denser shallow-water tint/absorption
      applies small animated ripple normals
      applies procedural foam near shorelines
      skips planar reflection contribution by default

Potential future reflection repair interfaces:

    reflectionUvValidity(waterWorld, normal) -> validity/fade
    optional reflection linear-depth binding for validating reflected content
    optional reflected camera oblique clip plane at sea_level_meters
