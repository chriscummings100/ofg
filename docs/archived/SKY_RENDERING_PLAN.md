# Improve Sky Rendering With Analytic Atmosphere, Clouds, And Day/Night

Archived note, 2026-06-07 / Codex: this ExecPlan is complete. The active source
of truth for sky ownership and contracts is now the implementation plus
`docs/ARCHITECTURE.md` and `docs/API_CONTRACTS.md`.

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it. This plan follows `PLANS.md` in this repository.

## Purpose / Big Picture

OFG should stop looking like it has a flat blue backdrop and start looking like a
world with believable atmosphere. A user flying or walking through the browser
build should see a richer clear sky, a sun whose color and intensity feel
coherent with the terrain lighting, procedural clouds that drift over time, and
an optional day/night cycle that can show sunrise, daytime, sunset, twilight, and
night without changing environment-map files.

The central rendering direction is a Hosek/Wilkie-style analytic sky. In this
plan, "analytic sky" means the shader computes sky color from a view direction,
sun direction, haze/turbidity, and ground-albedo parameters instead of sampling a
static sky texture. Hosek/Wilkie is the clear-sky foundation. It does not itself
generate clouds, stars, moonlight, or night-time sky; those are planned as
separate compositing layers that use the same Rust-owned sun/time state.

The first successful implementation should be modest but visible: the current
gradient sky in `src/engine/render/shaders/uber.wgsl` is replaced by a more
physically plausible clear sky with circumsolar glow, horizon behavior, and
low-sun warmth. Later milestones add Rust-owned time of day, procedural clouds,
and night/twilight features. True HDRI environment-map lighting is intentionally
deferred because the current renderer has no HDR/EXR environment texture path.

## Progress

- [x] (2026-06-07 12:38Z) Read `PLANS.md` before drafting this ExecPlan.
- [x] (2026-06-07 12:38Z) Refreshed renderer/API ownership context from
  `docs/ARCHITECTURE.md` and `docs/API_CONTRACTS.md`.
- [x] (2026-06-07 12:38Z) Researched procedural sky approaches, including
  Preetham, Hosek/Wilkie, Bruneton, Hillaire, SkyGAN, and the 2021 fitted
  atmosphere model.
- [x] (2026-06-07 12:38Z) Researched CC0 environment-map sources, especially
  Poly Haven and ambientCG, and recorded that they are useful references but not
  the first runtime path.
- [x] (2026-06-07 12:38Z) Inspected the current WGSL sky pass, Rust/wgpu sky
  pipeline, frame uniform packing, `engine_core` render light packet, and
  `engine_web` per-frame tick path.
- [x] (2026-06-07 13:56Z) Merged updated `origin/main` into `sky` and reread
  the updated `PLANS.md`. The plan now includes the Rust coverage attention gate
  before final completion.
- [x] (2026-06-07 13:48Z) Captured baseline Rust smoke artifacts at
  `artifacts/rust-smoke/run-1780836682-635`.
- [x] (2026-06-07 14:18Z) Chose a compact Hosek/Wilkie-inspired shader-side fit
  rather than vendoring exact coefficient tables. License risk is avoided
  because no third-party implementation or dataset is copied into the repo.
- [x] (2026-06-07 14:38Z) Implemented Rust-owned sky cycle state in
  `crates/engine_core/src/sky.rs`, including deterministic day phase, sun
  direction, light color, intensity, ambient, cloud settings, star intensity,
  moon intensity, and night blend.
- [x] (2026-06-07 14:38Z) Expanded Rust render snapshot, frame packet, and WGSL
  camera uniform packing to carry 12 sky floats. Current sizes are
  `RENDER_SNAPSHOT_FLOAT_COUNT = 31`, `FRAME_PACKET_FLOATS = 55`, and
  `FRAME_UNIFORM_FLOATS = 56`.
- [x] (2026-06-07 14:38Z) Replaced the WGSL gradient sky with named analytic
  sky, sun-radiance, cloud, star, and night helpers in
  `src/engine/render/shaders/uber.wgsl`.
- [x] (2026-06-07 14:38Z) Added Rust-owned sky debug values through
  `debugSnapshot()` and browser debug hooks, and extended browser smoke to
  validate the sky runtime sentinel and numeric ranges.
- [x] (2026-06-07 14:38Z) Updated focused Rust and TypeScript tests for sky
  state, packet packing, shader helper contracts, and debug types.
- [x] (2026-06-07 13:48Z) Ran final validation: `npm run check:shaders`,
  `$env:OFG_SMOKE_PORT="5184"; npm run smoke:browser`, `npm run smoke:rust`,
  `npm test`, and `npm run coverage:rust`.
- [x] (2026-06-07 13:48Z) Browser smoke passed on port 5184 and recorded
  `skyRuntime = "rust"`, sky day phase advancing from `0.1608342826` to
  `0.1643767506`, and screenshots under
  `artifacts/browser-smoke/2026-06-07T13-31-08-711Z`.
- [x] (2026-06-07 13:48Z) Final Rust smoke passed with artifacts under
  `artifacts/rust-smoke/run-1780839797-809`; the boot screenshot was visually
  checked for nonblank terrain and the new analytic sky.
- [x] (2026-06-07 13:48Z) Final `npm run coverage:rust` reported `files below
  90% line coverage ... none`; changed Rust implementation files are absent
  from the default attention report.
- [x] (2026-06-07 13:48Z) Ran the repo-local `milestone-review` skill for the
  implemented sky milestones. Required finding fixed: the native Rust smoke
  harness now derives its main light and sky packet from the same Rust sky
  state.
- [x] (2026-06-07 13:48Z) Completed final docs, screenshot notes, coverage
  evidence, and Outcomes & Retrospective.

## Surprises & Discoveries

- Observation: The current sky is already a dedicated full-screen WGSL pass.
  Evidence: `crates/engine_web/src/wgpu_renderer.rs` binds the camera group,
  selects `sky_pipeline`, and draws three vertices before terrain and model
  items. `src/engine/render/shaders/uber.wgsl` reconstructs a world ray in
  `skyFragmentMain`.

- Observation: The frame packet now carries Rust-owned sky/time parameters.
  Evidence: `crates/engine_web/src/render_packets.rs` emits 55 floats:
  view-projection matrix, inverse view-projection matrix, eye position, sun
  direction, sun color, sun intensity, ambient, and 12 sky values.
  `render_uniforms.rs` expands these to the 56-float WGSL camera uniform.

- Observation: At plan start, the sun was static and hard-coded in
  `engine_core`.
  Evidence: `crates/engine_core/src/render_packet.rs` creates
  `RenderLightPacket` with direction `(0.89, 0.25, 0.38).normalize()`, color
  `(1.0, 0.96, 0.88)`, intensity `1.0`, and ambient `0.34`.

- Observation: This static sun has been replaced by Rust-owned sky cycle state.
  Evidence: `crates/engine_core/src/sky.rs` maps elapsed seconds/day phase to
  `RenderLightPacket` and `SkyRenderPacket`; `Engine::render_snapshot()` uses
  elapsed engine time.

- Observation: Rust already has a clean home for day/night time advancement.
  Evidence: `crates/engine_web/src/game_state.rs` receives `BrowserGameInput`
  with `delta_seconds` each tick, and `crates/engine_core/src/engine.rs` already
  tracks elapsed engine time during `Engine::update`.

- Observation: CC0 HDRIs are easy to find, but true HDRI runtime sky/IBL would
  be a larger renderer feature.
  Evidence: `src/engine/browser/textureAssetLoader.ts` decodes browser images
  into 8-bit RGBA texture arrays or fetches opaque bytes. It does not decode HDR
  or EXR files, create cubemaps, prefilter environment lighting, or bind a sky
  environment texture.

- Observation: Port-safe browser testing is already partially supported.
  Evidence: `tools/dev-server.mjs` reads `PORT`, and `tools/browser-smoke.mjs`
  reads `OFG_SMOKE_PORT` and then searches for an available port starting there.

- Observation: The updated ExecPlan standard now requires coverage before
  finishing implementation plans.
  Evidence: `PLANS.md` says modified implementation files must not appear in the
  default `npm run coverage:rust` attention output unless an explicit exception
  is recorded with rationale.

- Observation: Browser smoke initially caught an incomplete TypeScript debug
  bridge.
  Evidence: `tools/browser-smoke.mjs` saw `getSkyRuntime` on `window.__ofgDebug`
  but read `skyRuntime = "missing"` until
  `src/engine/web/rustBrowserGameAdapter.ts` copied the new sky fields from the
  raw wasm debug snapshot.

- Observation: The coverage gate caught the untested new sky module before
  completion.
  Evidence: the first `npm run coverage:rust` listed
  `crates/engine_core/src/sky.rs` at 81.1% line coverage. Focused tests for
  packet defaults, write order, non-finite elapsed fallback, and
  `Engine::sky_render_state()` removed it from the final attention report.

- Observation: Milestone review found the native smoke harness was using
  inconsistent light and sky state.
  Evidence: `crates/ofg_test_harness/src/render_smoke/renderer.rs` paired the
  old fixed `RenderLightPacket` with `SkyRenderPacket::default_day()`. It now
  calls `sky_state_at_elapsed_seconds(0.0)` and writes both packets from that
  single state.

## Decision Log

- Decision: Keep sky rendering Rust/WGSL-owned, not TypeScript-owned.
  Rationale: `docs/API_CONTRACTS.md` forbids TypeScript WebGPU and scene/render
  ownership. The current architecture already has a Rust/wgpu sky pass, so sky
  changes should stay in Rust renderer state and WGSL shader code.
  Date/Author: 2026-06-07 / Codex

- Decision: Treat Hosek/Wilkie as the clear-sky layer, not the whole weather
  system.
  Rationale: Hosek/Wilkie models clear daytime sky radiance. Moving clouds,
  twilight, stars, and moonlight require additional procedural layers and
  blending rules.
  Date/Author: 2026-06-07 / Codex

- Decision: Do not add HDRI runtime support in the first implementation pass.
  Rationale: CC0 HDRIs are valuable visual references, but importing HDR/EXR
  environment maps would require new image decoding, texture format, binding,
  and possibly image-based-lighting infrastructure. The existing shader pass can
  get a large visual improvement with procedural sky math first.
  Date/Author: 2026-06-07 / Codex

- Decision: Prototype the Hosek/Wilkie data path before committing to exact
  coefficient tables in WGSL.
  Rationale: Full Hosek/Wilkie uses fitted coefficient datasets. We need to
  choose between uploading compact per-frame sky coefficients from Rust or
  embedding a smaller shader-side approximation. The choice should be based on
  shader size, code clarity, validation, and visual quality.
  Date/Author: 2026-06-07 / Codex

- Decision: Preserve the existing browser frame input contract for day/night.
  Rationale: Time-of-day can advance from Rust-owned elapsed time and
  `delta_seconds`; no TypeScript timer or new scalar wasm methods are needed.
  If debug controls are added, they must use the existing `GameCommand` lane and
  be documented in `docs/API_CONTRACTS.md`.
  Date/Author: 2026-06-07 / Codex

- Decision: Use Poly Haven and ambientCG sky HDRIs as reference material and
  optional future assets.
  Rationale: Both provide CC0 sky HDRIs. They are useful for matching clear,
  partly cloudy, overcast, and sunset palettes even if the runtime path remains
  procedural.
  Date/Author: 2026-06-07 / Codex

- Decision: Treat the Rust coverage attention report as a final completion gate
  for this plan.
  Rationale: Updated `PLANS.md` requires implementation plans to prove changed
  implementation files meet the documented coverage attention threshold, which
  is currently about 90% line coverage, or record an explicit exception.
  Date/Author: 2026-06-07 / Codex

- Decision: Implement a compact Hosek/Wilkie-inspired shader fit rather than
  exact Hosek/Wilkie coefficient tables.
  Rationale: The compact fit avoids vendoring coefficient data or third-party
  code, keeps the WGSL readable, and is sufficient for the current renderer's
  direct-light terrain model. Exact coefficients or LUT-based atmosphere remain
  future work if the renderer later needs higher physical accuracy.
  Date/Author: 2026-06-07 / Codex

- Decision: Keep the Rust smoke harness on the same sky state as the runtime
  renderer.
  Rationale: Native smoke should not validate a mixed state that the browser
  runtime never produces. Building both `RenderLightPacket` and
  `SkyRenderPacket` from `sky_state_at_elapsed_seconds(0.0)` keeps image smoke
  evidence aligned with the Rust-owned browser path.
  Date/Author: 2026-06-07 / Codex

## Outcomes & Retrospective

This plan delivered a Rust-owned procedural sky path: `engine_core` now advances
time of day, derives sun direction/color/intensity/ambient, and emits sky
parameters for haze, clouds, stars, moon glow, and night blending. The Rust
render snapshot and `engine_web` frame uniform now carry 12 sky floats into
WGSL. The old two-color sky pass has been replaced by a compact
Hosek/Wilkie-inspired analytic sky with procedural moving clouds, sun glow,
star field, and moon glow. Browser TypeScript remains a facade: it exposes
Rust-provided debug values but does not compute sky, cloud, lighting, or time
state.

The user-visible browser path is validated by
`artifacts/browser-smoke/2026-06-07T13-31-08-711Z/browser-first-person.png`,
which shows the improved sky and cloud layer over terrain. The native Rust
offscreen path is validated by
`artifacts/rust-smoke/run-1780839797-809/boot-frame.png`, which shows nonblank
terrain with the analytic sky. Browser smoke also proved that Rust-owned day
phase advances over time: `0.1608342826` to `0.1643767506` after the smoke wait.

Final validation passed: `npm run check:shaders`,
`$env:OFG_SMOKE_PORT="5184"; npm run smoke:browser`, `npm run smoke:rust`,
`npm test`, and `npm run coverage:rust`. The final coverage attention report
lists no files below the threshold, so there is no coverage exception for this
plan.

Remaining gaps are intentionally scoped as future renderer work. The sky is a
compact fitted approximation, not an exact Hosek/Wilkie coefficient
implementation. Clouds are procedural shader noise rather than volumetric
weather, and there are not yet user-facing controls for time-of-day or weather
tuning. Milestone review also noted existing split pressure in large renderer
files such as `crates/engine_web/src/wgpu_renderer.rs` and
`crates/engine_web/src/tests.rs`; future renderer features should plan a split
before adding much more code there.

## Contract and Quality Baseline

This plan must preserve the active ownership rules in `docs/API_CONTRACTS.md`
and `docs/ARCHITECTURE.md`.

`OFG-API-001: Browser Shell To Rust Browser Game` remains active. The browser
app must keep using `RustBrowserGame.create(canvas, assetLoader)`,
`game.resize(viewport)`, `game.tick(frame)`, `game.command(command)`, and
`game.debugSnapshot()`. Time-of-day and sky tuning must not add scalar
wasm-bindgen frame methods. If user-facing debug commands are needed, add them
through `GameCommand` and update `src/engine/web/browserGameTypes.ts`,
`crates/engine_web/src/wgpu_renderer.rs`, and `docs/API_CONTRACTS.md`.

`OFG-API-002: Rust Game To Browser Asset Loader` remains active. The first sky
implementation should not use this contract. If a later milestone adds sky
texture or reference-map loading, Rust must own asset meaning, requested URLs,
validation, texture creation, and renderer binding. TypeScript may only decode
generic RGBA texture-array requests or fetch opaque bytes.

`OFG-API-003: Debug And Smoke-Test Hooks` remains active. Browser smoke may use
debug snapshots to assert runtime ownership, time-of-day values, cloud settings,
or camera mode. Debug hooks must not compute sky, clouds, terrain, player, or
renderer state in TypeScript.

`OFG-API-004: Terrain Vertex And Material Layout` must be preserved. Sky shader
changes share `uber.wgsl` with terrain and model shading, so shader edits must
not alter terrain vertex locations, terrain material index/weight layout, or
object uniform packing unless a milestone explicitly updates all contract sites.

`OFG-API-009: Forbidden TypeScript Ownership` is binding. This plan must not
create TypeScript scene graph, renderer, sky renderer, terrain renderer, terrain
generator, simulation owner, or WebGPU resource owner. Allowed TypeScript work
is limited to debug/HUD display, existing browser app setup, and generic browser
asset loading.

The relevant validation gates are:

    cd C:\dev\ofg-sky
    npm run check:shaders
    npm run test:rust
    npm run test:ts
    npm test
    npm run smoke:rust
    $env:OFG_SMOKE_PORT="5184"; npm run smoke:browser
    npm run coverage:rust

Before this plan is complete, inspect the default filtered coverage output from
`npm run coverage:rust`. Every modified Rust implementation file must be absent
from `artifacts/coverage/rust/summary.json` and
`artifacts/coverage/rust/summary.pretty.json`, or this plan must record a
specific exception with rationale in the Decision Log and Outcomes &
Retrospective. The current attention threshold is about 90% line coverage.

Use `PORT` for manual dev server runs when other worktrees occupy the default:

    cd C:\dev\ofg-sky
    $env:PORT="5183"; npm run dev

After each implementation milestone, run the repo-local `milestone-review`
skill before marking that milestone complete. Required findings must be fixed,
or rejected findings must be recorded in the Decision Log with rationale.

## Context and Orientation

The repository root for this worktree is `C:\dev\ofg-sky`.

The current shader source is `src/engine/render/shaders/uber.wgsl`. It includes
terrain/model material shading and the sky pass. `tools/build-shaders.mjs`
generates `src/generated/render/uberShader.ts`, and
`src/engine/render/shaders/UberShader.test.ts` validates generated metadata and
shader contract snippets.

The current sky pass is procedural. `skyVertexMain` draws a full-screen
triangle. `skyFragmentMain` reconstructs a world ray from
`camera.inverseViewProjection`, evaluates `analyticSkyColor`, `sunRadiance`,
`cloudLayer`, and `nightSkyColor`, then applies an exposure-style sky tone map.

The Rust browser renderer is `crates/engine_web/src/wgpu_renderer.rs`. It owns
the WebGPU device, pipelines, bind groups, texture handles, terrain mesh
handles, model resources, and draw submission. It creates the sky pipeline using
`skyVertexMain` and `skyFragmentMain`, then draws the sky before mesh items.

The camera/light/sky uniform is built in
`crates/engine_web/src/render_uniforms.rs`. `FRAME_PACKET_FLOATS` is currently
55 and `FRAME_UNIFORM_FLOATS` is currently 56. The WGSL `Camera` struct is:

    viewProjection: mat4x4<f32>
    inverseViewProjection: mat4x4<f32>
    eyeWorld: vec4<f32>
    sunDirectionAndIntensity: vec4<f32>
    sunColorAndAmbient: vec4<f32>
    skyTimeAndLight: vec4<f32>
    skyAtmosphereAndCloud: vec4<f32>
    skyCloudAndNight: vec4<f32>

The Rust render snapshot starts in `crates/engine_core/src/render_packet.rs`.
`RenderSnapshot::from_player_view_at_time` creates a camera packet,
time-derived `RenderLightPacket`, and `SkyRenderPacket`.
`crates/engine_core/src/engine.rs` exposes `Engine::render_snapshot`, which
`engine_web` uses to build frame packets.

The per-frame Rust browser game tick is in
`crates/engine_web/src/game_state.rs`. `BrowserGameState::tick` receives
`BrowserGameInput`, updates player movement, advances engine elapsed time, and
syncs render-facing scene items.

Definitions used by this plan:

`view ray` means the normalized world-space direction from the camera through a
screen pixel.

`sun direction` means the normalized world-space direction toward the main light.
The current renderer treats this as the direction used for direct sunlight and
the sky sun disk.

`turbidity` means a haze amount. Lower values produce clearer deep-blue skies;
higher values produce paler, hazier skies and stronger horizon whitening.

`ground albedo` means approximate average ground reflectance that influences sky
color near the horizon in analytic sky models.

`circumsolar` means the bright region around the sun, distinct from the hard sun
disk.

`aerial perspective` means atmospheric brightening, desaturation, and color shift
of distant terrain. This plan may prepare sky data for it, but terrain aerial
perspective is optional future work unless explicitly pulled into a milestone.

## Plan of Work

Milestone 1 establishes a baseline and chooses the Hosek/Wilkie implementation
strategy. Capture current screenshots through `npm run smoke:rust` and browser
smoke on a non-default port. Inspect the generated sky images, record where they
are too flat, and create a small prototype that can evaluate clear-sky color for
several sun elevations and view directions. The prototype can be a Rust unit
module, a temporary shader function, or a small local script, but it must be
removed or promoted by the end of the milestone. The milestone decision is
whether to use exact Hosek/Wilkie-style per-frame coefficients uploaded from
Rust, or a compact WGSL function fitted to Hosek/Wilkie-like behavior. Record
the choice and license implications in this plan.

Milestone 2 implements the analytic clear sky. Update
`src/engine/render/shaders/uber.wgsl` so `skyFragmentMain` calls named helper
functions rather than hand-coded color constants. The helper functions should
compute sun/view angles safely, avoid `NaN` around the horizon, render a brighter
but controlled circumsolar region, whiten and brighten the horizon based on
turbidity, warm the sun and horizon as sun elevation decreases, and tone-map the
result into stable display color. If exact coefficients are uploaded from Rust,
add the necessary Rust uniform packing and WGSL struct fields in the same
milestone. Regenerate `src/generated/render/uberShader.ts`, update shader tests,
and verify terrain/model shader contracts still pass.

Milestone 3 adds Rust-owned time of day. Add an engine-side or browser-game-side
sky/time state that advances from `delta_seconds` and derives sun direction,
sun color, sun intensity, and ambient. The time state should be deterministic
and testable. A first cycle can be stylized rather than astronomical: sun rises
from one horizon, arcs overhead, sets on the opposite horizon, and spends a
configurable fraction of the cycle below the horizon. Terrain lighting should
dim at night and warm near sunrise/sunset. If browser debug controls are added,
they must go through `GameCommand` and debug snapshots rather than a new browser
simulation path.

Milestone 4 adds moving procedural clouds. Extend the camera or sky uniform with
time and cloud parameters, preferably appended to the existing camera uniform or
split into a small sky-specific uniform bind group if that keeps packing clearer.
In WGSL, project the view ray onto a high-altitude layer or dome and sample
procedural value/fBm noise with wind offset. Clouds should have coverage,
softness, sunlit edge, shadow tint, and horizon fade controls. They should move
smoothly over time, remain deterministic across reloads for the same seed/time
settings, and stay cheap enough for browser smoke.

Milestone 5 adds twilight and night treatment. Because classic Hosek/Wilkie is a
daytime clear-sky model, fade to a separate twilight/night path when the sun is
near or below the horizon. Add procedural stars that fade in when the sun is low,
an optional moon disk using a simple direction and phase parameter, and ambient
lighting curves that do not make terrain black or over-bright. Validate that the
sky remains stable through sunrise and sunset transitions.

Milestone 6 polishes the feature. Update `docs/ARCHITECTURE.md` to describe the
new sky ownership and rendering path. Update `docs/API_CONTRACTS.md` only if new
commands, debug snapshot fields, or uniform contract risks are introduced. Add
or update README testing guidance for non-default ports if useful. Keep all
visual reference links in this plan or a small active note, not scattered through
code comments.

## Concrete Steps

Start every milestone from the current worktree root:

    cd C:\dev\ofg-sky

Before implementation changes, inspect local status and protect unrelated work:

    git status --short --branch

For baseline screenshots:

    npm run smoke:rust
    $env:OFG_SMOKE_PORT="5184"; npm run smoke:browser

Inspect `artifacts/rust-smoke/<run-id>/report.json`,
`artifacts/rust-smoke/<run-id>/*.png`, and `artifacts/browser-smoke/report.json`
after each smoke run.

After shader source edits:

    npm run build:shaders
    npm run check:shaders
    npm run test:ts

After Rust light, time, renderer, or uniform edits:

    npm run test:rust
    npm test

After visual renderer changes:

    npm run smoke:rust
    $env:OFG_SMOKE_PORT="5184"; npm run smoke:browser

For manual browser inspection while other worktrees are running:

    $env:PORT="5183"; npm run dev

Expected successful smoke evidence:

    Browser smoke report shows a nonblank WebGPU frame, Rust runtime sentinel
    strings, camera toggle still works, and screenshot pixel samples are not
    black/blank/solid.

    Rust smoke report shows nonblank terrain/sky PNGs under artifacts/rust-smoke
    and no blank or solid frame failures.

## Milestone Review

After each milestone:

1. Update this plan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective.
2. Update `docs/ARCHITECTURE.md` or `docs/API_CONTRACTS.md` if the milestone
   changed ownership, command/debug contracts, shader uniform contracts, or test
   expectations.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands and record the results, artifacts, and
   remaining risks in this plan.

Completed milestone review, 2026-06-07 / Codex:

- Scope: Rust-owned sky cycle, expanded render/frame uniforms, WGSL analytic
  sky/cloud/night shader, browser debug hooks, smoke updates, docs/contracts,
  generated shader and wasm artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agent tools were available, but not used because the available
  sub-agent tool policy requires explicit user-requested delegation.
- Required findings fixed: native Rust smoke paired the old fixed light packet
  with the new default sky packet. The harness now builds both from
  `sky_state_at_elapsed_seconds(0.0)`.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs` and
  `crates/engine_web/src/tests.rs` are already above 1000 lines, and
  `crates/engine_web/src/game_state.rs` is above 600 lines. Future renderer
  growth should include a split plan.
- Rejected findings: none.
- Validation rerun: `cargo test -p ofg_test_harness`, `npm run smoke:rust`,
  `npm test`, and `npm run coverage:rust` after the required finding was fixed.
- Remaining risk: the sky is Hosek/Wilkie-inspired rather than exact, and cloud
  movement is verified through shader/time paths and smoke screenshots rather
  than a visual-diff test.

## Validation and Acceptance

The clear-sky milestone is accepted when:

- The visible sky is no longer a two-color gradient; it has plausible horizon
  whitening, zenith depth, sun-adjacent glow, and warmer low-sun behavior.
- `src/engine/render/shaders/UberShader.test.ts` verifies the named analytic sky
  helpers and sky entry points.
- `npm run check:shaders`, `npm run test:ts`, `npm run smoke:rust`, and
  `npm run smoke:browser` pass.
- Terrain/model rendering still works with the shared `Camera` uniform.

The time-of-day milestone is accepted when:

- Sun direction, light color, intensity, and ambient change deterministically
  over time from Rust-owned state.
- Unit tests cover noon, sunrise/sunset, and night light values.
- Browser debug or smoke evidence can observe time/light state without
  TypeScript computing it.
- Terrain lighting visibly changes with the sky without flicker or blank frames.

The cloud milestone is accepted when:

- Clouds move over time in the sky pass without texture assets.
- Cloud coverage and motion are deterministic and parameterized.
- Clouds are lit by the Rust-owned sun direction and fade near the horizon or
  night in a controlled way.
- Browser and Rust smoke remain nonblank and stable.

The night milestone is accepted when:

- The sky transitions safely through sunset, twilight, night, and sunrise.
- Stars fade in at night and out during day.
- Optional moon rendering, if included, is controlled by Rust-owned state and
  does not create false suns or lighting discontinuities.
- Terrain remains readable at night without violating the direct-light model.

The full plan is accepted when:

- `npm test` passes.
- `npm run smoke:rust` passes.
- `$env:OFG_SMOKE_PORT="5184"; npm run smoke:browser` passes.
- `npm run coverage:rust` passes and the default filtered coverage report does
  not list modified Rust implementation files, unless this plan records an
  explicit exception with rationale.
- Manual inspection on a non-default dev-server port shows improved sky, moving
  clouds, and day/night behavior.
- Active docs and contracts describe the final ownership and limitations.

## Idempotence and Recovery

Shader generation is deterministic. If a shader edit leaves generated output
stale, rerun:

    npm run build:shaders

If a browser smoke port is busy, set `OFG_SMOKE_PORT` to another nearby value.
The smoke script scans upward from the requested port.

If the dev server port is busy, set `PORT` before `npm run dev`.

If an exact Hosek/Wilkie coefficient implementation becomes too large or
license-unclear, do not partially vendor it. Remove the prototype, record the
decision, and continue with a compact fitted clear-sky implementation named as
"Hosek/Wilkie-inspired" rather than claiming exact conformance.

If clouds introduce performance or stability problems, keep the clear-sky and
day/night milestones intact and disable cloud composition behind a Rust-owned
parameter defaulting off until the next milestone fixes it.

If a uniform layout change breaks terrain or model rendering, revert only the
sky-uniform change from the current milestone, keep unrelated user changes, and
return to the last passing generated shader artifact.

## Artifacts and Notes

Useful research references:

- Hosek/Wilkie sky research and sample implementation:
  `https://cgg.mff.cuni.cz/projects/SkylightModelling/`
- Hosek/Wilkie solar radiance extension:
  `https://pubmed.ncbi.nlm.nih.gov/24807990/`
- 2021 fitted atmosphere model with post-sunset support:
  `https://cgg.mff.cuni.cz/publications/skymodel-2021/`
- Preetham daylight model:
  `https://courses.cs.duke.edu/cps124/fall01/resources/p91-preetham.pdf`
- Bruneton precomputed atmospheric scattering implementation:
  `https://ebruneton.github.io/precomputed_atmospheric_scattering/`
- Hillaire production-ready atmosphere paper:
  `https://onlinelibrary.wiley.com/doi/10.1111/cgf.14050`
- Unreal Sky Atmosphere documentation:
  `https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-in-unreal-engine?lang=en-US`
- Poly Haven CC0 license:
  `https://polyhaven.com/license`
- Poly Haven HDRI FAQ:
  `https://docs.polyhaven.com/en/faq`
- ambientCG CC0 license:
  `https://docs.ambientcg.com/license/`

Useful CC0 visual references:

- Poly Haven Kloppenheim 05 Pure Sky:
  `https://polyhaven.com/a/kloppenheim_05_puresky`
- Poly Haven Kloofendal 48d Partly Cloudy Pure Sky:
  `https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky`
- Poly Haven Syferfontein 18d Clear Pure Sky:
  `https://polyhaven.com/a/syferfontein_18d_clear_puresky`
- Poly Haven Industrial Sunset Pure Sky:
  `https://polyhaven.com/a/industrial_sunset_puresky`
- ambientCG Day Sky HDRI examples:
  `https://ambientcg.com/view?id=DaySkyHDRI035A`

Expected artifact locations:

    artifacts/rust-smoke/<run-id>/report.json
    artifacts/rust-smoke/<run-id>/*.png
    artifacts/browser-smoke/report.json
    artifacts/browser-smoke/*.png

## Interfaces and Dependencies

Final code should expose stable, Rust-owned sky state. The exact names may be
chosen during implementation, but the end state should have these concepts:

- A Rust data type for sky/time settings or state, probably in
  `crates/engine_core` if it is engine logic, or in `crates/engine_web` if it is
  renderer-only presentation state.
- A deterministic function that maps time of day to sun direction, sun color,
  sun intensity, and ambient.
- A renderer uniform path for any sky-specific values needed by WGSL, such as
  elapsed sky time, turbidity, cloud coverage, cloud speed, star fade, or moon
  direction.
- WGSL helper functions in `src/engine/render/shaders/uber.wgsl` for analytic
  sky color, sun disk/glow, cloud coverage, night/star color, and safe angle
  math.
- Shader tests in `src/engine/render/shaders/UberShader.test.ts` that guard the
  sky entry points, shared camera uniform contract, and expected helper names.
- Rust tests for time/light curves and frame uniform packing if those paths
  change.
- Browser smoke checks only for black-box integration signals and screenshots,
  not TypeScript-owned sky calculations.

This plan intentionally does not require adding a third-party runtime dependency
in the first milestone. If a Rust crate or vendored implementation is adopted
for Hosek/Wilkie coefficients, its license and maintenance cost must be recorded
in the Decision Log before it is committed.

## Revision Notes

- 2026-06-07 / Codex: Initial ExecPlan drafted from the sky-rendering research
  discussion and aligned with the current Rust-owned renderer architecture.
- 2026-06-07 / Codex: Updated after merging `origin/main` to include the new
  `PLANS.md` coverage completion gate.
- 2026-06-07 / Codex: Updated after the first sky implementation slice landed:
  Rust sky cycle, expanded uniforms, analytic WGSL sky, clouds, stars, moon
  glow, and debug snapshot fields.
- 2026-06-07 / Codex: Completed final validation, coverage gate, screenshot
  inspection, and milestone review. Recorded the fixed native smoke harness
  consistency issue and remaining large-file split pressure.
