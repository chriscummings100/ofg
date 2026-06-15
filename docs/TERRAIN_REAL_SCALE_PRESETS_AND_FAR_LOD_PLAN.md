# Real-Scale Terrain Presets And Far LOD Span

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

The current terrain presets use frequencies that make "large" features appear
at roughly sub-kilometer to two-kilometer scale. That is useful for a prototype,
but it makes hills and mountains feel like game bumps rather than landscape
forms. The purpose of this plan is to replace the built-in shape preset numbers
with real-scale wavelengths and then extend the default terrain stream far
enough for those wavelengths to be visible.

After this change, the default browser terrain stream should settle with coarse
far terrain over an approximately 7 km generated span, not just the previous
4.6 km. An earlier proof pass validated LOD6 and an 18 km span, but live fog
tuning showed the practical playable horizon is much shorter: fog starts at
200 m, reaches full skybox-matched blend at 3000 m, and the shared camera far
plane sits at 3500 m. The preset catalog should still expose the same stable
preset IDs, but the shape values should represent lowland plain, rolling hills,
mountain valley, and rocky highland at kilometer-scale wavelengths. Smoke and
benchmark reports should prove the stream reaches LOD5, keeps terrain generated
behind the fogged horizon, and that preset screenshots still render.

## Progress

- [x] (2026-06-14 13:43+01:00) Drafted this ExecPlan to pair real-scale preset
  values with far LOD5/LOD6 coverage, rather than changing preset wavelengths
  without enough draw distance to see them.
- [x] (2026-06-14 13:49+01:00) Milestone 1: updated built-in terrain preset
  values to kilometer-scale macro wavelengths and regenerated
  `src/generated/world/terrainPresets.ts`. Preset code 0 now displays as
  Lowland Plain while keeping the stable `seed` ID. Validation: `npm run
  build:terrain-presets` regenerated metadata; `cargo test -p terrain_core
  terrain_preset` passed with 3 tests; `cargo fmt` ran.
- [x] (2026-06-14 13:59+01:00) Milestone 2: extended the default runtime and
  smoke multi-LOD bands to LOD5 and LOD6. The tapered far ladder now uses LOD4
  radius 3, LOD5 radius 2 with a `5/4` below/above vertical window, and LOD6
  radius 4 with a `4/3` window. Updated
  stream, smoke, and benchmark assertions from the old 4096m target to a
  16000m minimum settled span. Validation: `cargo test -p engine_web
  tests::browser_terrain_stream_default_bands_render_multiple_lods_after_settling`
  passed; `cargo test -p ofg_test_harness
  multi_lod_scenario_terrain_reports_lod_counts` passed; `cargo test -p
  ofg_test_harness terrain_bench_lod` passed with 3 tests; `cargo fmt` ran.
- [x] (2026-06-14 21:35+01:00) Milestone 3: updated active docs, regenerated
  WASM and generated preset metadata, validated the real-scale stream, and ran
  the repo-local milestone review skill locally. Final validation included
  `npm run test:ts`, `npm run check:wasm`, `cargo test -p engine_core camera`,
  `npm run smoke:terrain-presets`, `npm run smoke:rust`, `npm run
  smoke:browser`, `npm run bench:terrain:rust`, `cargo fmt --check`, and
  `git -c safe.directory=C:/dev/ofg diff --check`. Browser smoke report
  `artifacts/browser-smoke/2026-06-14T20-13-46-901Z/report.json` settled at
  `maxRenderedLod: 6`, `visibleWorldSpanXMeters: 18432`, and
  `visibleWorldSpanZMeters: 18432` with no worker failures, stale completions,
  or synchronous builds. The latest movement sample recorded
  `maxTerrainUpdateTotalMs: 192`.
- [x] (2026-06-14 22:06+01:00) Milestone 4: added a Rust-owned atmospheric
  fog pass in post-process. Fog settings are packed by
  `PostProcessSettings`, applied in `post.wgsl` from linear depth before tone
  mapping, exposed through Rust renderer commands/status and browser debug UI,
  and validated with browser fog-off/fog-on/fog-factor screenshots. Rust smoke
  `far-view-multi-lod` now reports post-process fog status for the 18.4 km
  span. Validation: `npm run test:rust`, `npm run test:ts`, `npm run
  smoke:browser`, `npm run smoke:rust`, `cargo test -p engine_web
  post_process`, `npm run check:shaders`, and `npm run coverage:rust` passed.
- [x] (2026-06-15 05:45+01:00) Follow-up tuning guardrails: added live fog
  range sliders for debug tuning, changed fog to converge toward the procedural
  skybox with a tint multiplier, lifted terrain variant validation/editor
  ranges above the old 256m-scale ceiling, made the compatibility height query
  search around macro terrain elevation, and documented that regular 500ms-class
  frame or terrain update spikes are not a finished state for the 60fps target.
- [x] (2026-06-15 07:30+01:00) Follow-up horizon trim: live fog tuning settled
  on a 200 m start and 3000 m end. The default stream now drops LOD6, widens
  LOD5 to radius 3, targets a 7000 m settled generated span, and uses a 3500 m
  shared Rust camera far plane.

## Surprises & Discoveries

- Observation: existing "large" preset feature wavelengths are mostly between
  about 0.6 km and 1.8 km.
  Evidence: `crates/terrain_core/src/presets.rs` uses frequencies such as
  `0.0016`, `0.00105`, `0.00095`, and `0.00055`, where wavelength is
  `1 / frequency`.
- Observation: the current height/density model is still heightfield-biased.
  Evidence: `crates/terrain_core/src/field.rs` computes density as world `y`
  minus macro base elevation minus 3D detail displacement.
- Observation: the old compatibility surface search was bounded to `-96m..96m`,
  which blocked high-base terrain even after vertical stream bands were added.
  Evidence: the follow-up removed `SURFACE_SEARCH_MIN_Y` and
  `SURFACE_SEARCH_MAX_Y`; `height_at_with_shape` now searches around the macro
  terrain sample from `sample_macro_terrain`.
- Observation: the browser water smoke camera was coupled to the old default
  terrain shape.
  Evidence: `npm run smoke:browser` initially failed because the retuned
  `rollingHills` preset remained sensible but the fixed water camera saw only
  a narrow sea strip, about 3.6% water-like pixels. The preset should not be
  lowered just to satisfy smoke; the smoke view needs to be adjusted or given a
  dedicated smoke terrain setup.
- Observation: the terrain stream could report LOD6 and an 18.4 km span while
  the camera still clipped at 500 m.
  Evidence: `crates/engine_core/src/render_packet.rs`,
  `crates/engine_core/src/scene_components.rs`, and the native smoke camera in
  `crates/ofg_test_harness/src/render_smoke/renderer.rs` all used
  `far_plane: 500.0` before this follow-up.
- Observation: the 24 km far plane was useful as proof that LOD6 was genuinely
  visible, but it is larger than the current design target.
  Evidence: browser smoke and the manual mountain-valley proof capture showed
  terrain rendering across the horizon; the design target then shifted to an
  8 km-class visible distance with post-process fog rather than maximum exposed span.
- Observation: once fog was tuned to a 200 m start and 3000 m end, LOD6 no
  longer carried terrain the player should inspect in normal play.
  Evidence: the default far plane is now 3500 m and the stream smoke/benchmark
  targets wait for LOD5 and a 7000 m generated span instead of LOD6 and 16000 m.
- Observation: before Milestone 4 there was no renderer fog hook to tune.
  Evidence: the first source search for `fog` across `crates`, `src`, and
  `tools` found no active runtime fog implementation, which is why the follow-up
  implemented fog in the Rust-owned post-process path rather than embedding a
  partial terrain-specific fade.
- Observation: the post-process shader already has the data needed for a first
  distance-fog pass.
  Evidence: `src/engine/render/shaders/post.wgsl` binds
  `linearDepthTexture`, and terrain, model, and water scene shaders write
  linear depth for downstream post processing. Sky pixels write zero linear
  depth, which gives the fog pass a simple way to leave the sky untouched.
- Observation: the original long-horizon fog ramp was visually subtle in
  whole-image averages because most pixels in the long-horizon smoke camera
  were near terrain or sky.
  Evidence: browser smoke initially saw only about a `0.19` RGB mean delta
  between the historical 7 km/11.5 km fog-off and fog-on captures. Current
  defaults use the stronger user-tuned 200 m/3000 m ramp directly.
- Observation: native Rust terrain smoke does not execute the browser
  post-process frame graph.
  Evidence: `crates/ofg_test_harness/src/render_smoke/renderer.rs` renders the
  terrain/water path into offscreen color/depth textures without constructing
  `PostProcessResources`. Milestone 4 therefore uses the engine_web native
  post-process GPU test for shader execution, browser smoke for visual fog
  screenshots, and a native smoke `postProcess` report block for far-view fog
  status.
- Observation: fixed-color fog is not enough for a convincing horizon because
  it creates a separate band from the procedural sky.
  Evidence: `post.wgsl` now binds the camera uniform, reconstructs the sky ray
  for each fogged pixel, and blends distant opaque pixels toward
  `skyColorAtUv(uv)` multiplied by the debug RGB tint.

## Decision Log

- Decision: keep preset IDs stable while changing the preset numbers and the
  display name for preset code 0.
  Rationale: TypeScript URLs and generated preset metadata use string IDs. The
  user asked for better existing presets, not an API/catalog rename. Code 0 can
  display as Lowland Plain while keeping the existing `seed` ID until a larger
  preset taxonomy change is desired.
  Date/Author: 2026-06-14 / Codex.
- Decision: add LOD5 and LOD6 instead of widening LOD3 for long view distances.
  Rationale: LOD3 nodes are only 256 meters wide. LOD6 nodes are 2048 meters
  wide, so a modest radius can show real-scale landforms without exploding
  node counts at mid-detail.
  Date/Author: 2026-06-14 / Codex.
- Decision: use a tapered far ladder with LOD4 radius 3, LOD5 radius 2, and
  LOD6 radius 4 for the first real-scale horizon pass.
  Rationale: this creates a theoretical LOD6 span of about 18.4 km while
  keeping intermediate bands from duplicating too much mid/far work during
  movement. Browser movement smoke exposed that wider LOD4/LOD5 overlap could
  create excessive upload churn even though LOD6 was carrying the horizon.
  Date/Author: 2026-06-14 / Codex.
- Decision: tune presets to plausible relief under the current validation and
  surface-search constraints, not to full alpine extremes yet.
  Rationale: the new vertical band resolver makes taller terrain streamable,
  but the current terrain function, height search, water logic, and camera
  assumptions are not ready for kilometer-high mountains everywhere.
  Date/Author: 2026-06-14 / Codex.
- Decision: raise the default camera far plane from 500 m to 12000 m.
  Rationale: the original 500 m plane clipped almost all useful far terrain.
  A temporary 24000 m proof value confirmed the LOD6 terrain was really
  renderable, but the practical target is now an 8 km-class visible horizon
  with fog later hiding the final clip. A named `engine_core` constant keeps
  browser and native smoke cameras aligned.
  Date/Author: 2026-06-14 / Codex.
- Decision: temporarily raise the browser movement smoke terrain-update ceiling
  from 500 ms to 600 ms only as a diagnostic during the real-scale far-view
  migration.
  Rationale: after the 24000 m far plane exposed the LOD6 workload, repeated
  browser smoke runs settled at about 514-515 ms maximum terrain update while
  retaining LOD6, no synchronous terrain builds, no worker failures, and a
  settled 18.4 km span. This was useful to keep gathering evidence, but it is
  not an acceptable completed baseline for the playable game.
  Date/Author: 2026-06-14 / Codex.
- Decision: make regular large frame or terrain-update spikes a blocking
  completion issue.
  Rationale: the target is a 60fps game. A change that creates regular
  500ms-class spikes can be profiled behind a diagnostic path, but it is not
  finished until the default playable path avoids those spikes or has bounded
  work scheduling that keeps normal play responsive.
  Date/Author: 2026-06-15 / User and Codex.
- Decision: keep LOD6 for the 8 km-class horizon instead of replacing it with
  wider LOD5 coverage.
  Rationale: the then-current LOD6 radius 4 gave a measured 18432 m generated span,
  or about 9.2 km from the player to a cardinal edge. Dropping LOD6 would need
  many more 1 km LOD5 nodes to cover the same horizon. Keeping LOD6 while
  reducing the far plane to 12 km is the cheaper near-term trim.
  Date/Author: 2026-06-14 / Codex.
- Decision: supersede the LOD6 horizon with an LOD5-only default horizon.
  Rationale: live fog tuning established a practical default ramp of 200 m to
  3000 m, with a 3500 m camera far plane. At that visibility distance, LOD6's
  18 km generated span is unnecessary default work. Widening LOD5 to radius 3
  gives roughly 7 km of generated terrain, enough to sit just behind the fogged
  horizon without paying for LOD6.
  Date/Author: 2026-06-15 / User and Codex.
- Decision: implement horizon fog in the Rust-owned post-process path, not in
  terrain generation or terrain mesh shaders.
  Rationale: fog is a camera/rendering effect over all opaque scene pixels,
  water, models, and terrain. The post-process path already owns linear depth,
  tone mapping, bloom composition, and browser debug-view integration, so it
  can fade distant pixels without changing mesh generation, terrain ownership,
  or LOD selection.
  Date/Author: 2026-06-14 / Codex.
- Decision: keep production fog defaults at 200 m start, 3000 m end, full
  density, skybox-matched fog with a neutral `[1, 1, 1]` tint, and a 1.35 curve.
  Rationale: these are the current user-tuned values. The ramp is strong enough
  that a 3500 m far plane can be hidden cleanly, and smoke can use the same
  values instead of a separate long-range proof ramp.
  Date/Author: 2026-06-15 / User and Codex.
- Decision: extend Rust smoke reports with post-process fog status instead of
  duplicating the browser post-process frame graph inside the native terrain
  smoke harness.
  Rationale: the native harness is currently a terrain/water offscreen renderer
  and does not construct the browser post-process resources. The engine_web
  native GPU test verifies the post-process shader path, browser smoke verifies
  screenshots, and the Rust smoke report proves the far-view scenario carries
  the active fog defaults.
  Date/Author: 2026-06-14 / Codex.

## Outcomes & Retrospective

Milestones 1-4 are complete. The built-in catalog now represents Lowland
Plain, Rolling Hills, Mountain Valley, and Rocky Highland with kilometer-scale
macro wavelengths while preserving stable preset IDs. The default Rust-owned
browser stream now uses bounded vertical windows through LOD5 and targets a
7000 m settled generated span in browser smoke, Rust smoke, and the terrain
benchmark. The camera far plane is no longer the old 500 m blocker and is now
3500 m, behind the default 200 m to 3000 m skybox-matched fog ramp. The
Rust-owned post-process pass fades distant opaque pixels into that skybox fog
before tone mapping, leaving sky pixels untouched because they carry zero
linear depth. Browser smoke captures fog-off, fog-on, and fog-factor images
from a deterministic long-horizon debug camera.

The main design lesson is that "view distance" and "generated span" should
remain separate knobs. LOD5 is now the default horizon carrier because the
fogged playable horizon is about 3 km; LOD6 remains proven history, not the
default. The preset values and fog defaults are a plausible technical
baseline only; a human terrain-art pass is still expected before these numbers
should be treated as authored world style.

## Contract and Quality Baseline

`OFG-API-005` remains active. Rust owns preset metadata and terrain variant
descriptors. The generated TypeScript preset metadata must be rebuilt from the
Rust catalog rather than edited by hand.

`OFG-API-003` remains active. Browser debug snapshots may report larger far
spans and max rendered LOD, but TypeScript must not own terrain generation or
stream scheduling.

`OFG-API-009` remains active. TypeScript must not regain terrain generation,
terrain stream scheduling, LOD visibility, or world simulation.

`OFG-API-004` remains active. The fog pass must not change terrain vertex
layout; it should consume existing scene color and linear depth.

Quality gates:

- Keep preset values inside `TerrainShapeParameters::validate`.
- Keep generated metadata and WASM artifacts fresh if Rust exports or generated
  artifacts change.
- Run terrain preset smoke and multi-LOD smoke/benchmark validation.
- For the fog pass, run shader checks, post-process Rust tests, browser smoke,
  and inspect/record screenshots showing fog on and off. Fog must fade distant
  opaque terrain into the procedural skybox, with RGB controls acting only as a
  tint; a fixed fog color that separates from the sky is not done.
- For terrain/view-distance work, capture movement/perf evidence. Regular
  500ms-class frame or terrain-update spikes fail the 60fps completion bar even
  if visual smoke screenshots pass.
- Run the repo-local `milestone-review` skill before marking the final
  milestone complete.

## Context and Orientation

Built-in preset values live in `crates/terrain_core/src/presets.rs`. Preset
metadata lives in `crates/terrain_core/src/variant.rs` and is generated into
`src/generated/world/terrainPresets.ts` by
`tools/build-terrain-preset-metadata.mjs`.

The browser default terrain stream lives in
`crates/engine_web/src/terrain_stream.rs`. Default multi-LOD bands currently
run from LOD0 through LOD5. A terrain node spans `32 * base_cell_size * 2^lod`
meters on each axis, so LOD4 is 512 meters wide and LOD5 is 1024 meters wide at
the current default `base_cell_size = 1`.

The duplicate smoke multi-LOD bands live in
`crates/ofg_test_harness/src/render_smoke/scenarios.rs`. The multi-LOD
benchmark uses the browser default stream through
`crates/ofg_test_harness/src/terrain_bench_lod.rs`.

The post-process renderer lives in `crates/engine_web/src/post_process.rs` and
`src/engine/render/shaders/post.wgsl`. The shader already samples scene color,
linear depth, and bloom, then applies optional DoF and tone mapping. Fog should
extend this same Rust-owned post-process settings/uniform path and expose only
command/status/debug wrappers to TypeScript.

## Plan of Work

Milestone 1 replaces `TERRAIN_PRESETS` with real-scale values. The target
wavelengths are about 4-10 km for large landforms, 1-3 km for ridges where
present, and 100-250 m for density detail. Tests in `presets.rs` should assert
these stronger wavelength expectations.

Milestone 2 first extended default runtime bands to LOD5 and LOD6. The current
playable default supersedes that proof by keeping LOD0 tight near the player,
dropping LOD6, and using LOD5 radius 3 as the far carrier. Smoke and benchmark
assertions now target a generated span around 7000 m. The camera far plane
remains a separate renderer knob, currently 3500 m behind the 200-3000 m fog
ramp.

Milestone 3 updates active docs, regenerates generated metadata/WASM artifacts
where required, runs validation, and records review outcomes.

Milestone 4 adds an atmospheric distance-fog pass. Extend
`PostProcessSettings` and the post-process uniform block with fog enablement,
start distance, end distance, curve/density, and sky tint. Initial defaults
now target the current design direction: fog starts at 200 m, reaches full
blend at 3000 m, and leaves the 3500 m far plane hidden. The shader
should compute fog from linear depth, ignore sky pixels with zero/nonpositive
linear depth, blend scene color toward the procedural skybox color before tone
mapping, and keep the effect renderer-wide rather than terrain-specific.

Milestone 4 also adds debug and validation surface. Add a Rust renderer command
for fog settings, expose fog fields in renderer status and browser debug types,
and add a post-process debug view or smoke-only capture path that makes fog
factor visible. Browser smoke should capture fog disabled and fog enabled from
a long-distance debug camera, sample enough pixels to prove distant terrain is
faded while near terrain remains visible, and keep the settled LOD5/7000m
stream assertions. Rust smoke should include a deterministic far-view fog
scenario or extend `far-view-multi-lod` with fog status in the report.

## Concrete Steps

All commands run from `C:\dev\ofg`.

    npm run build:terrain-presets
    cargo test -p terrain_core terrain_preset
    cargo test -p engine_web browser_terrain_stream
    cargo test -p ofg_test_harness render_smoke
    cargo test -p ofg_test_harness terrain_bench_lod
    npm run smoke:terrain-presets
    npm run smoke:rust
    npm run smoke:browser
    npm run bench:terrain:rust
    cargo test -p engine_web post_process
    npm run check:shaders
    npm run test:ts
    npm run check:terrain-presets
    npm run check:wasm
    git -c safe.directory=C:/dev/ofg diff --check

## Milestone Review

Milestones 1-3 review:

- Scope: real-scale preset replacement, bounded vertical far-LOD stream bands,
  browser/Rust smoke and benchmark assertions, camera far-plane adjustment,
  coordinate HUD, generated artifacts, and active docs.
- Reviewers: contract, code quality, legacy, correctness, and validation were
  run locally. No sub-agents were used because the user did not request a
  delegated review.
- Required findings fixed: active doc drift in
  `docs/TERRAIN_VARIANT_EDITOR_PLAN.md` still described the previous
  sub-kilometer preset tuning; it now points to the current kilometer-scale
  preset baseline. Ad-hoc browser perf capture helpers that still waited for
  old LOD3/LOD4 and 4096m assumptions were updated during the proof pass, and
  the later horizon trim now waits for LOD5 and 7000m generated span.
- Follow-ups recorded: renderer fog/atmospheric fade should be the visual
  follow-up for the 8 km-class horizon.
- Rejected findings: none.
- Validation rerun: `npm run check:wasm`, `npm run smoke:browser`, `cargo
  fmt --check`, and final diff hygiene were rerun after the 12000m far-plane
  trim and review fixes.
- Remaining risk: the preset numbers are technical defaults, not human-authored
  terrain art direction.

Milestone 4 review:

- Scope: Rust-owned post-process fog settings/uniforms/shader, renderer
  command/status/debug wrappers, browser debug UI, browser smoke fog
  screenshots, Rust smoke fog status report, generated shader/WASM artifacts,
  and active docs.
- Reviewers: contract, code quality, legacy, correctness, and validation were
  run locally. No sub-agents were used because the user did not request a
  delegated review.
- Required findings fixed: active docs still contained future-fog wording in
  `docs/TERRAIN_PLAN.md`; this now describes the implemented post-process fog
  pass. This ExecPlan still had the Milestone 3 review note saying fog was not
  implemented; it now records the Milestone 4 review. The ExecPlan coverage
  gate had not been run; `npm run coverage:rust` now ran and reported only
  unmodified `crates/terrain_core/src/surface_query.rs` below the default 90%
  attention threshold.
- Follow-ups recorded: native Rust smoke records post-process fog status in
  report JSON but does not apply the browser post-process chain; visual fog
  proof remains owned by browser smoke and the engine_web native post-process
  GPU test.
- Rejected findings: none.
- Validation rerun: `npm run test:rust`, `npm run test:ts`, `npm run
  smoke:browser`, `npm run smoke:rust`, `npm run coverage:rust`, `cargo test
  -p engine_web post_process`, `npm run check:shaders`, `npm run
  check:terrain-presets`, `npm run check:wasm`, `cargo fmt --check`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: fog defaults are technical first-pass values. Human tuning
  should revisit fog color/curve/start/end alongside terrain art direction.

## Validation and Acceptance

This plan is complete when:

- Built-in presets use kilometer-scale macro wavelengths and remain valid.
- The default browser stream includes LOD5 and does not require LOD6.
- Multi-LOD smoke and benchmark reports show max rendered LOD at least 5.
- Settled generated terrain span is at least 7000 meters in X and Z.
- The shared Rust camera far plane is 3500 meters, behind the 3000 meter fog
  end distance.
- Distant terrain fades into the sky before the 3500 m far plane is visually
  obvious, while nearby terrain and water remain readable.
- Fog settings are Rust-owned, exposed through renderer status/debug wrappers,
  and covered by post-process shader/Rust tests.
- Browser movement/perf evidence shows the default playable path does not
  produce regular 500ms-class frame or terrain-update spikes.
- Preset smoke screenshots render for all built-in presets.
- Generated preset metadata and WASM checks pass.

## Idempotence and Recovery

If the LOD5 horizon is too expensive, reduce LOD5 radius or upload budgets
before reducing macro terrain scale. If preset heights exceed current surface
search assumptions too often, reduce height/ridge scales first and record the
compromise. Generated artifacts from `npm run build:terrain-presets` and
`npm run build:wasm` may be regenerated. If fog tuning looks wrong, keep the
renderer settings adjustable and disable fog by setting its debug/runtime
enable flag rather than reintroducing LOD6 as a default.

## Artifacts and Notes

Expected artifact locations:

- Rust smoke reports under `artifacts/rust-smoke/`.
- Terrain benchmark reports under `artifacts/terrain-bench/`.
- Latest browser smoke report:
  `artifacts/browser-smoke/2026-06-14T20-51-28-675Z/report.json`.
- Manual far-plane proof capture:
  `artifacts/browser-smoke/far-plane-proof/far-plane-mountain-valley.png`.
- Latest terrain benchmark report:
  `artifacts/terrain-bench/run-1781467106-552/report.json`.
- Latest full Rust smoke report:
  `artifacts/rust-smoke/run-1781470513-567/report.json`.
- Fog browser smoke screenshots:
  `artifacts/browser-smoke/2026-06-14T20-51-28-675Z/browser-fog-off.png`,
  `artifacts/browser-smoke/2026-06-14T20-51-28-675Z/browser-fog-on.png`,
  and
  `artifacts/browser-smoke/2026-06-14T20-51-28-675Z/browser-fog-factor.png`.
- Rust far-view fog status:
  `artifacts/rust-smoke/run-1781470513-567/report.json`,
  image `far-view-multi-lod`, field `postProcess`.

## Interfaces and Dependencies

No new third-party libraries are required. This work changes Rust-owned terrain
presets, Rust-owned default stream bands, generated TypeScript preset metadata,
active docs, and generated WASM artifacts if validation requires it. Milestone
4 also changed Rust-owned post-process settings/status, WGSL post-process
shader code, generated shader artifacts, browser debug TypeScript types, and
smoke automation.

## Revision Notes

- 2026-06-14: Initial plan drafted.
- 2026-06-14: Extended with Milestone 4 for a Rust-owned post-process fog pass.
- 2026-06-14: Completed Milestone 4 and recorded fog validation artifacts.
