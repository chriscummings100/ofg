# Implement Cascading Shadow Maps

Completion note: this ExecPlan was completed on 2026-06-07. The active source
of truth for the finished behavior is now `docs/API_CONTRACTS.md`,
`docs/ARCHITECTURE.md`, the Rust/wgpu renderer code, shader tests, and smoke
tests. This plan is kept only as an archived implementation record.

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

This document follows the repository ExecPlan standard in `PLANS.md`.

## Purpose / Big Picture

OFG should render directional sunlight shadows across the playable terrain and
scene meshes using cascading shadow maps, or CSM. CSM splits the camera frustum
into several near-to-far ranges, renders one depth map from the directional
light for each range, and samples the matching depth map while shading the main
color pass.

The user-visible result is terrain and imported models casting and receiving
sun shadows in the Rust-owned WebGPU renderer. The first complete version should
show stable opaque shadows with modest PCF softening for terrain and current
GLTF scene meshes in browser smoke and native Rust image smoke. It does not need
polished cascade blending, contact-hardening PCSS shadows, alpha-tested shadow
casters, GPU culling, or high-end quality presets. It should include early
shadow debug outputs because shadow-map work is otherwise difficult to inspect:
native tests should be able to write shadow-map visualizations to disk, and the
browser renderer should support simple debug views for cascade index and sampled
shadow visibility.

## Progress

- [x] (2026-06-07) Drafted this ExecPlan from repository orientation and
  preliminary CSM research.
- [x] (2026-06-07 14:05+01:00) Merged updated `origin/main` into the
  `shadow-maps` branch and kept the active CSM work on top of it.
- [x] (2026-06-07 14:05+01:00) Added shared render math, AABB extraction,
  transform, frustum extraction/intersection tests, renderer-side post-extraction
  frustum culling, and a visible draw-count debug/status field.
- [x] (2026-06-07 14:25+01:00) Ran Milestone 1 `milestone-review` locally,
  fixed required findings, reran validation, and marked the render-math/culling
  foundation complete.
- [x] (2026-06-07 15:00+01:00) Added CSM cascade split math, camera frustum
  slice corners, stable directional-light fitting, texel snapping, and focused
  Rust tests in `crates/engine_web/src/render_math_tests.rs`.
- [x] (2026-06-07 15:00+01:00) Ran Milestone 2 `milestone-review` locally,
  split new math/shadow tests out of the large catch-all test file, reran Rust
  validation, and marked the cascade-math milestone complete.
- [x] (2026-06-07 14:55+01:00) Added shadow uniform packing, persistent WebGPU
  shadow-map resources, comparison sampler and bind group, Rust-owned shadow
  renderer status fields, TypeScript status typing, and browser smoke status
  assertions. Shadows are still not rendered or sampled.
- [x] (2026-06-07 14:55+01:00) Ran Milestone 3 `milestone-review` locally,
  found no required fixes after the `shadow_renderer.rs` helper split, reran
  validation, and marked shadow resources/status complete.
- [x] (2026-06-07 15:10+01:00) Added depth-only terrain/model shadow pipelines,
  per-frame cascade construction, four per-cascade shadow render passes, positive
  shadow draw-count browser smoke assertions, and wasm artifact regeneration.
- [x] (2026-06-07 15:10+01:00) Ran Milestone 4 `milestone-review` locally,
  recorded the no-culling first-pass choice and WebGPU depth-bind-group split,
  reran validation including coverage, and marked the depth-pass milestone
  complete.
- [x] (2026-06-07 15:36+01:00) Added native shadow-map cascade dumps, a shadow
  atlas, shadow debug statistics in Rust smoke reports, Rust-owned browser
  shadow debug commands/snapshot state, and browser smoke debug-mode checks.
- [x] (2026-06-07 15:36+01:00) Ran Milestone 5 `milestone-review` locally,
  recorded file-size and approximate-cascade-selection follow-ups, inspected
  shadow/browser artifacts, reran validation including coverage, and marked
  the debug tooling milestone complete.
- [x] (2026-06-07 15:50+01:00) Added WGSL main-pass shadow sampling for
  terrain and GLTF materials, including a fixed 3x3 PCF kernel, direct-light
  shadowing, and refreshed shader/wasm artifacts.
- [x] (2026-06-07 15:50+01:00) Ran Milestone 6 `milestone-review` locally,
  fixed the shader source header finding, inspected normal/debug smoke
  screenshots, reran validation including coverage, and marked main-pass
  shadow sampling complete.
- [x] (2026-06-07 15:54+01:00) Added generated shader metadata for
  `shadowVertexMain` and `shadowModelVertexMain`, strengthened shader contract
  tests for group-2 shadow bindings/debug helpers, and regenerated
  `src/generated/render/uberShader.ts`.
- [x] (2026-06-07 15:54+01:00) Ran Milestone 7 `milestone-review` locally,
  fixed missing purpose headers in touched TypeScript/tool files, reran
  TypeScript/shader/wasm validation, and marked shader metadata/docs complete.
- [x] (2026-06-07 16:00+01:00) Strengthened browser smoke to capture
  `browser-shadow-visibility.png` in addition to the cascade-index debug view.
- [x] (2026-06-07 16:00+01:00) Ran Milestone 8 `milestone-review` locally,
  inspected final browser/native smoke artifacts, reran final validation
  including coverage, and completed the CSM ExecPlan.

## Surprises & Discoveries

- Observation: There is no general render-time frustum-culling system yet.
  Evidence: `rg -n "frustum|cull|aabb|bounds"` found terrain chunk bounds in
  `crates/terrain_core/src/chunk.rs`, but no renderer-visible AABB/frustum
  abstraction in `engine_web`.

- Observation: Browser smoke can already avoid port conflicts.
  Evidence: `tools/browser-smoke.mjs` reads `OFG_SMOKE_PORT` and scans forward
  for an available port.

- Observation: Manual `npm run dev` currently uses only `PORT`.
  Evidence: `tools/dev-server.mjs` reads `process.env.PORT ?? "5173"` and does
  not scan forward.

- Observation: `origin/main` updated the ExecPlan standard with a coverage
  completion gate.
  Evidence: `PLANS.md` now says an implementation plan is not complete until
  each modified implementation file clears the default Rust coverage attention
  gate or the plan records an explicit exception.

- Observation: Local TypeScript tests needed dependencies installed in this
  worktree after the main merge.
  Evidence: the first `npm run test:ts` failed because `mocha` was not found;
  after `npm ci`, `npm run test:ts` passed.

- Observation: The updated Rust coverage gate caught an under-tested modified
  implementation file before the milestone closed.
  Evidence: the first `npm run coverage:rust` reported
  `crates/engine_web/src/render_packets.rs` at 81.1% line coverage because its
  `RenderPacketError` display arms were not covered. A targeted diagnostic test
  brought the default attention list to `none`.

- Observation: `crates/engine_web/src/wgpu_renderer.rs` is already above the
  repository's preferred file-size envelope.
  Evidence: `Measure-Object -Line` reported 2250 lines after Milestone 1. This
  plan now routes shadow-specific GPU helpers into a separate renderer-side
  helper module before the main shadow pass work grows.

- Observation: The catch-all `crates/engine_web/src/tests.rs` is also already
  above the preferred file-size envelope.
  Evidence: Milestone 2 initially pushed it to 1998 lines. The milestone review
  moved render-packet, render-math, and shadow-cascade tests into the focused
  `crates/engine_web/src/render_math_tests.rs` module, leaving `tests.rs` at
  1628 lines and the focused module at 404 lines.

- Observation: Shadow resource/status changes require checked-in wasm artifact
  regeneration.
  Evidence: `npm run check:wasm` compiled successfully but reported stale
  `assets/wasm/engine_web/engine_web.js`,
  `assets/wasm/engine_web/engine_web_bg.wasm`, and
  `src/generated/web/engineWebWasm.ts`. Running `npm run build:wasm` refreshed
  them, after which `npm run check:wasm` passed.

- Observation: A shadow-map texture cannot be bound for sampling in the same
  WebGPU synchronization scope where a cascade layer is the writable depth
  attachment.
  Evidence: the first browser smoke after adding the depth pass failed with
  `usage (TextureBinding|RenderAttachment) includes writable usage and another
  usage in the same synchronization scope` for `shadow map texture array`, then
  produced an invalid command buffer and a solid screenshot. Splitting the
  depth pass onto a uniform-only shadow bind group removed the conflict, and
  browser smoke passed.

- Observation: Once the shared WGSL fragment references shadow debug bindings,
  every color pipeline using `fragmentMain` must provide group 2 even when debug
  mode is off.
  Evidence: the first Rust image smoke after adding browser debug branches
  failed while creating `smoke terrain pipeline` because shader group 2 binding
  0 was missing from the native pipeline layout. The offscreen color path now
  binds an inert shadow uniform, texture array, and comparison sampler.

- Observation: Native shadow-map dumps are useful immediately, but writing them
  for every smoke scenario would create a noisy and large artifact set.
  Evidence: one boot scenario already writes four 1024x1024 cascade PNGs plus a
  2048x2048 atlas with useful non-black/variation statistics. The plan now keeps
  those dumps on the boot scenario and leaves regular preset/seam images
  unchanged.

- Observation: The first main-pass sampling milestone can be validated with
  existing debug views, but it still lacks an automated visual-shadow assertion.
  Evidence: browser smoke with active sampling wrote nonblank normal frames and
  a cascade-index screenshot under
  `artifacts/browser-smoke/2026-06-07T14-41-48-968Z`, and manual inspection
  showed terrain shadow variation. The smoke report asserts debug-mode state
  transitions and positive shadow draw counts, but it does not yet compare a
  shadow-on image against a shadow-off image.

## Decision Log

- Decision: Implement CSM in Rust-owned renderer modules, not TypeScript.
  Rationale: `docs/ARCHITECTURE.md` and `docs/API_CONTRACTS.md` state that Rust
  owns WebGPU resources, draw submission, terrain render data, and scene mesh
  render extraction. TypeScript remains browser shell and generic asset loading.
  Date/Author: 2026-06-07 / Codex

- Decision: Start with four cascades and a 1024x1024 depth texture array.
  Rationale: Four cascades are a standard baseline for large outdoor scenes, and
  1024 keeps the first browser implementation conservative. The values should be
  constants in `crates/engine_web/src/config.rs` so later quality tuning is
  explicit.
  Date/Author: 2026-06-07 / Codex

- Decision: Use stable, padded cascade fitting with texel snapping before
  pursuing tighter fit-to-cascade quality.
  Rationale: The first implementation should avoid obvious shimmering during
  player movement. Tighter fitting can improve resolution later, but it makes
  stability more fragile.
  Date/Author: 2026-06-07 / Codex

- Decision: Sample shadows with a WebGPU depth texture array and comparison
  sampler.
  Rationale: WGSL supports `texture_depth_2d_array` with
  `textureSampleCompare` and `textureSampleCompareLevel`. This matches WebGPU's
  native shadow path and avoids custom depth encodings for the first version.
  Date/Author: 2026-06-07 / Codex

- Decision: Use `textureSampleCompareLevel` for the first shader version.
  Rationale: It avoids derivative and uniform-control-flow requirements while
  the CSM selection logic is still simple, and still allows a small fixed PCF
  kernel through constant texel offsets.
  Date/Author: 2026-06-07 / Codex

- Decision: Include small PCF softening in the first complete CSM version.
  Rationale: CSM provides distance coverage and resolution, but not soft edges
  by itself. A fixed 3x3 PCF kernel is the smallest useful soft-shadow step and
  can be tuned later into distance-scaled PCF or PCSS.
  Date/Author: 2026-06-07 / Codex

- Decision: Build shadow inspection tools as soon as shadow maps exist.
  Rationale: Depth-only shadow passes are hard to debug from the final color
  frame alone. Disk dumps of shadow-map layers and browser debug views for
  cascade index and sampled visibility make matrix, culling, bias, and sampling
  errors observable early.
  Date/Author: 2026-06-07 / Codex

- Decision: Use explicit non-default ports for manual browser verification in
  this branch.
  Rationale: Other worktrees are active. Use `PORT=5183` or another chosen free
  port for `npm run dev`, and `OFG_SMOKE_PORT=5184` or another chosen free port
  for `npm run smoke:browser`.
  Date/Author: 2026-06-07 / Codex

- Decision: Expose both total render-item candidates and camera-frustum-visible
  mesh draws in renderer status.
  Rationale: Browser smoke should not pass a bad culling implementation just
  because the sky pass rendered. The status remains Rust-owned debug state
  copied through the existing debug snapshot contract.
  Date/Author: 2026-06-07 / Codex

- Decision: Interpret the existing sun direction as a vector from the shaded
  point toward the sun when building shadow cascades.
  Rationale: `src/engine/render/shaders/uber.wgsl` uses
  `camera.sunDirectionAndIntensity.xyz` directly for `dot(normal,
  lightDirection)` and for the sky sun disk. Therefore the shadow light camera
  is placed at `cascade_center + sun_direction * distance` and looks back toward
  the cascade center, matching the current shader convention.
  Date/Author: 2026-06-07 / Codex

- Decision: Keep Milestone 2 as internal Rust implementation without an API
  contract update.
  Rationale: It exposes Rust crate helpers and constants for tests and future
  renderer code, but it does not add TypeScript commands, debug snapshot fields,
  wasm-bindgen public methods, or browser-facing ownership.
  Date/Author: 2026-06-07 / Codex

- Decision: Expose shadow resource readiness through renderer status before
  rendering shadow passes.
  Rationale: Early status fields make the new depth texture array and shadow
  pass counter visible to browser smoke while the pass still draws zero items.
  This remains Rust-owned debug state copied through the existing debug snapshot
  lane.
  Date/Author: 2026-06-07 / Codex

- Decision: Put WebGPU shadow texture, view, sampler, and bind-group creation
  in `crates/engine_web/src/shadow_renderer.rs`.
  Rationale: `wgpu_renderer.rs` is already large. Keeping shadow-specific setup
  in a helper module satisfies the split plan while allowing
  `BrowserWgpuRenderer` to own the persistent resources.
  Date/Author: 2026-06-07 / Codex

- Decision: Render every prepared terrain/model item into every cascade for the
  first depth-pass milestone.
  Rationale: This avoids missed off-camera casters before shadow-map dump and
  cascade debug views exist. It is more expensive than per-cascade caster
  culling, but it makes the first observable depth output conservative and easy
  to validate through `frameShadowDrawCount`.
  Date/Author: 2026-06-07 / Codex

- Decision: Use a uniform-only shadow bind group layout for depth rendering and
  keep the full texture/sampler shadow bind group for later main-pass sampling.
  Rationale: WebGPU rejects a command encoder that writes a shadow cascade layer
  while a bind group in the same scope also exposes the shadow texture for
  sampling. The depth pass only needs the active light-view-projection matrix,
  while the full bind group remains the intended color-pass sampling interface.
  Date/Author: 2026-06-07 / Codex

- Decision: Write native shadow cascade dumps only for the boot smoke scenario.
  Rationale: The boot scenario is enough to prove that all four cascade layers
  receive nonblank depth output and to give developers an atlas to inspect.
  Emitting shadow PNGs for every preset and seam scenario would add a lot of
  artifact volume without improving the first debug signal.
  Date/Author: 2026-06-07 / Codex

- Decision: Expose shadow debug modes through `setShadowDebugView` and
  `debugSnapshot().shadowDebugView`, not through new wasm methods or TypeScript
  renderer state.
  Rationale: This keeps the feature inside OFG-API-003's browser debug hook lane
  and preserves the rule that TypeScript may forward debug commands and copy
  Rust state, but must not compute cascades, shadow depths, or visibility.
  Date/Author: 2026-06-07 / Codex

- Decision: Use approximate camera distance for early browser cascade debug
  selection until main-pass shadow sampling adds a stronger view-depth packet.
  Rationale: It is sufficient for an inspectable cascade-index view and avoids
  destabilizing frame uniform packing in the debug milestone. Milestone 6
  revisited this choice and kept it for the first sampled-shadow version.
  Date/Author: 2026-06-07 / Codex

- Decision: Keep approximate camera-distance cascade selection for the first
  sampled-shadow version.
  Rationale: The existing distance-based helper gives readable cascade debug
  bands and working PCF shadows without changing the frame uniform layout late
  in the milestone. Exact camera-forward view depth, cascade blending, and split
  fade zones remain follow-up quality work.
  Date/Author: 2026-06-07 / Codex

- Decision: Apply shadow visibility to direct sunlight only and leave ambient
  lighting unshadowed.
  Rationale: This keeps the first CSM result readable and avoids turning fully
  shadowed terrain black. The fixed 3x3 PCF kernel softens hard compare edges
  while preserving the existing sky/ambient contribution.
  Date/Author: 2026-06-07 / Codex

- Decision: Treat shadow shader entry-point names as generated shader metadata,
  but not as a new runtime API contract.
  Rationale: The metadata helps tests verify the Rust renderer and WGSL source
  stay aligned. Browser code still reaches rendering through the Rust-owned
  `RustBrowserGame` facade and does not consume shader metadata at runtime, so
  `docs/API_CONTRACTS.md` did not need a new supported API entry.
  Date/Author: 2026-06-07 / Codex

## Outcomes & Retrospective

Milestone 1 foundation is complete. It introduces shared render math for later
CSM work, computes local/world AABBs for registered meshes, culls main-pass
terrain/model draws against the camera frustum, and reports
`frameVisibleDrawCount` through the existing Rust-owned debug snapshot.

Milestone review:

- Scope: render math, render-packet helper reuse, main-pass camera-frustum
  culling, visible draw-count debug/status plumbing, API contract docs, wasm
  metadata, and browser smoke assertions.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agents were available but not used because the tool contract only
  permits delegation when the user explicitly asks for sub-agents.
- Required findings fixed: added public render-math function descriptions;
  added invalid-input and orthographic projection tests; added
  `RenderPacketError` diagnostic coverage after coverage flagged
  `render_packets.rs`; refreshed the plan with a `shadow_renderer.rs` split path
  before adding large shadow GPU helper blocks.
- Follow-ups recorded: keep shadow-specific GPU resource, pipeline, and debug
  visualization helpers outside `wgpu_renderer.rs` unless a change is very
  small.
- Rejected findings: none.
- Remaining risk: browser smoke proves the current view has visible post-cull
  draws, but it does not yet contain a dedicated off-camera culling fixture.
  Later shadow milestones should add stronger render/debug scenarios when shadow
  maps can be inspected.

Validation run for this milestone:

    cargo test -p engine_web
    npm run check:wasm
    npm run check:shaders
    npm test
    npm run coverage:rust
    git diff --check
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser

Results:

    cargo test -p engine_web: 107 passed
    npm run check:wasm: passed
    npm run check:shaders: passed
    npm test: passed, including Rust workspace tests and 62 TypeScript tests
    npm run coverage:rust: files below 90% line coverage: none
    git diff --check: passed, with only Git line-ending warnings
    npm run smoke:browser: passed on http://127.0.0.1:5184/

Browser smoke wrote fresh artifacts under
`artifacts/browser-smoke/2026-06-07T13-18-40-274Z`. The report had
`frameDrawCount: 1` and `frameVisibleDrawCount: 1` in first person, then
`frameDrawCount: 15` and `frameVisibleDrawCount: 13` after the camera toggle.
The screenshots were nonblank and varied.

Earlier validation while developing the milestone also ran:

    npm run build:wasm
    npm run test:ts

Milestone 2 cascade math is complete. It adds `crates/engine_web/src/shadows.rs`
with four-cascade split computation, camera-slice corner generation, padded
directional-light fitting, conservative caster depth margin, texel-snapped
orthographic bounds, and finite-matrix validation. It also adds shadow constants
to `crates/engine_web/src/config.rs` and exports the pure Rust helpers from
`crates/engine_web/src/lib.rs`.

Milestone 2 review:

- Scope: `crates/engine_web/src/shadows.rs`, shadow constants in
  `config.rs`, public Rust exports in `lib.rs`, and focused tests in
  `render_math_tests.rs`.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agents were not used because the tool contract only permits
  delegation when the user explicitly asks for sub-agents.
- Required findings fixed: moved the new render-math and shadow-cascade tests
  out of the already-large `tests.rs` into `render_math_tests.rs`.
- Follow-ups recorded: continue adding focused test modules for future renderer
  subsystems instead of growing `tests.rs`; keep shadow GPU helper code outside
  `wgpu_renderer.rs`.
- Rejected findings: none.
- Remaining risk: cascade fitting is validated numerically, but no shadow depth
  texture exists yet, so visual stability and caster coverage still need the
  later shadow-map dump/debug milestones.

Validation run for Milestone 2:

    cargo test -p engine_web
    npm run test:rust
    npm run coverage:rust
    git diff --check

Results:

    cargo test -p engine_web: 113 passed
    npm run test:rust: passed across the Rust workspace
    npm run coverage:rust: files below 90% line coverage: none
    git diff --check: passed, with only Git line-ending warnings

Milestone 3 shadow resources and uniforms are complete, with shadows not yet
rendered or sampled. It adds `build_shadow_uniform_values` in
`crates/engine_web/src/render_uniforms.rs`, focused uniform tests in
`crates/engine_web/src/render_uniform_tests.rs`, and wasm-only WebGPU resource
creation in `crates/engine_web/src/shadow_renderer.rs`. The browser renderer now
allocates a 4-layer 1024x1024 `Depth32Float` shadow texture array, per-layer
views, an array view, a comparison sampler, a shadow uniform buffer, and a
shadow bind group. Renderer status now exposes `frameShadowDrawCount`,
`shadowCascadeCount`, and `shadowMapSize`; the draw count remains `0` until the
depth pass lands.

Milestone 3 review:

- Scope: shadow uniform packing, wasm shadow resource creation, renderer status
  fields, TypeScript status typing, browser smoke assertions, generated wasm
  artifacts, and API contract docs.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agents were not used because the tool contract only permits
  delegation when the user explicitly asks for sub-agents.
- Required findings fixed: none after the `shadow_renderer.rs` helper split was
  already in place.
- Follow-ups recorded: render and debug shadow passes still need real depth
  output and image dump tooling before sampling.
- Rejected findings: none.
- Remaining risk: WebGPU shadow resources are allocated and reported, but no
  pass writes the depth texture yet, so visual/debug validation remains for the
  next milestones.

Validation run for Milestone 3:

    cargo test -p engine_web
    npm run build:wasm
    npm run check:wasm
    npm run test:ts
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser
    npm run test:rust
    npm run coverage:rust
    git diff --check

Results:

    cargo test -p engine_web: 117 passed
    npm run build:wasm: regenerated engine_web wasm artifacts
    npm run check:wasm: passed after regeneration
    npm run test:ts: 62 passing
    npm run smoke:browser: passed on http://127.0.0.1:5184/
    npm run test:rust: passed across the Rust workspace
    npm run coverage:rust: files below 90% line coverage: none
    git diff --check: passed, with only Git line-ending warnings

Browser smoke wrote artifacts under
`artifacts/browser-smoke/2026-06-07T13-51-15-361Z`. The report showed
`frameShadowDrawCount: 0`, `shadowCascadeCount: 4`, and `shadowMapSize: 1024`
for first-person, toggled-camera, and reloaded frames.

Milestone 4 depth-only shadow rendering is complete. It adds shadow-only WGSL
vertex entry points for terrain and model meshes, creates depth-only shadow
pipelines in `crates/engine_web/src/shadow_renderer.rs`, builds CSM cascade
matrices from the Rust engine render snapshot each frame, writes per-cascade
shadow uniforms, renders four shadow-map layers before the color pass, and
reports positive `frameShadowDrawCount` values through renderer status. The
shadow maps are not sampled by the color pass yet.

Milestone 4 review:

- Scope: WGSL depth-only shadow entries, `shadow_renderer.rs` depth pipelines
  and uniform-only cascade bind groups, `wgpu_renderer.rs` cascade construction
  and shadow pass orchestration, browser smoke shadow draw assertions, generated
  shader/wasm artifacts, and validation evidence.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agents were not used because the tool contract only permits
  delegation when the user explicitly asks for sub-agents.
- Required findings fixed: split the depth-pass shadow bind group from the full
  shadow texture/sampler bind group after browser smoke exposed a WebGPU
  writable-resource conflict.
- Follow-ups recorded: add shadow-map dumps and browser debug views next, then
  add sampling/PCF; revisit per-cascade caster culling after the depth maps can
  be inspected.
- Rejected findings: none.
- Remaining risk: the pass renders all prepared items into every cascade and
  does not yet prove depth contents visually. Milestone 5 must make the shadow
  maps inspectable before final sampling and bias tuning.

Validation run for Milestone 4:

    cargo test -p engine_web
    npm run check:shaders
    npm run build:wasm
    npm run check:wasm
    npm run test:ts
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser
    npm run test:rust
    npm run coverage:rust
    git diff --check

Results:

    cargo test -p engine_web: 117 passed
    npm run check:shaders: passed
    npm run build:wasm: regenerated engine_web wasm artifacts
    npm run check:wasm: passed
    npm run test:ts: 62 passing
    npm run smoke:browser: passed on http://127.0.0.1:5184/
    npm run test:rust: passed across the Rust workspace
    npm run coverage:rust: files below 90% line coverage: none
    git diff --check: passed, with only Git line-ending warnings

Browser smoke wrote artifacts under
`artifacts/browser-smoke/2026-06-07T14-06-00-250Z`. The report showed
`frameShadowDrawCount: 4` in first person, `frameShadowDrawCount: 60` after the
camera toggle, and `frameShadowDrawCount: 4` after reload, with
`shadowCascadeCount: 4`, `shadowMapSize: 1024`, nonblank screenshots, and varied
pixel buckets.

Milestone 5 shadow debug tooling is complete. The native Rust smoke harness now
writes `shadow-cascade-0.png` through `shadow-cascade-3.png` plus
`shadow-atlas.png` for the boot scenario, and records `shadowImages` with luma
statistics in `report.json`. The browser debug hook now exposes
`getShadowDebugView()` and `setShadowDebugView(...)`; Rust stores the debug mode,
reports it through `debugSnapshot().shadowDebugView`, and packs it into the
shadow uniform spare slot. WGSL can show cascade index colors, one-tap shadow
visibility, or projected depth for a selected cascade. Normal lighting still
does not sample shadows by default.

Milestone 5 review:

- Scope: native shadow-map visualization in `crates/ofg_test_harness`, browser
  `setShadowDebugView` command plumbing, `shadowDebugView` debug snapshot field,
  WGSL debug branches, browser smoke debug-mode checks, shader contract tests,
  API contract docs, and generated shader/wasm artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agents were not used because the tool contract only permits
  delegation when the user explicitly asks for sub-agents.
- Required findings fixed: added a disabled shadow bind group to the native
  color smoke pipeline after WGSL debug bindings made group 2 required for
  `fragmentMain`; extended browser smoke to exercise `cascadeIndex`,
  `shadowVisibility`, and `shadowDepthCascade0`.
- Follow-ups recorded: `crates/ofg_test_harness/src/render_smoke/renderer.rs`
  and `shadow_debug.rs` are now split-pressure files, though both remain below
  1000 lines; keep future harness additions in smaller modules. Revisit cascade
  selection depth when Milestone 6 adds main-pass shadow sampling. Continue
  shrinking `wgpu_renderer.rs` where practical.
- Rejected findings: none.
- Remaining risk: browser depth/visibility debug modes are smoke-tested for
  successful frames and snapshot state, but only the cascade-index view has an
  asserted screenshot. Final sampling and PCF still need visual/bias validation.

Validation run for Milestone 5:

    npm run check:shaders
    npm run check:wasm
    cargo test -p engine_web
    cargo test -p ofg_test_harness
    npm run test:ts
    npm run smoke:rust
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser
    npm run test:rust
    npm run coverage:rust
    git diff --check

Results:

    npm run check:shaders: passed
    npm run check:wasm: passed
    cargo test -p engine_web: 117 passed
    cargo test -p ofg_test_harness: 28 passed
    npm run test:ts: 63 passing
    npm run smoke:rust: passed
    npm run smoke:browser: passed on http://127.0.0.1:5184/
    npm run test:rust: passed across the Rust workspace
    npm run coverage:rust: files below 90% line coverage: none
    git diff --check: passed, with only Git line-ending warnings

Rust smoke wrote artifacts under
`artifacts/rust-smoke/run-1780842598-421`. The shadow report included
`shadow-cascade-0.png` through `shadow-cascade-3.png` and `shadow-atlas.png`;
the atlas had `nonBlackPixels: 27228`, `uniqueLumaBuckets: 14`, and a
non-solid `dominantLumaRatio: 0.8961334`.

Browser smoke wrote artifacts under
`artifacts/browser-smoke/2026-06-07T14-36-58-245Z`. The report showed
`shadowDebugView` transitions through `off`, `cascadeIndex`,
`shadowVisibility`, `shadowDepthCascade0`, and back to `off`. The cascade-index
screenshot was `browser-shadow-cascade-index.png`, with 120 unique color
buckets and `dominantColorRatio: 0.5764902998236332`.

Milestone 6 main-pass shadow sampling is complete. `src/engine/render/shaders/uber.wgsl`
now samples the shadow texture array from terrain and GLTF material shading,
uses a fixed 3x3 `textureSampleCompareLevel` PCF kernel, applies visibility to
direct sunlight, and keeps ambient light unshadowed. The TypeScript shader
contract test now checks for compare sampling and the PCF average, and
`src/generated/render/uberShader.ts` was regenerated from the WGSL source.

Milestone review:

- Scope: WGSL shadow compare sampling and PCF, terrain/model material lighting
  integration, generated shader artifact freshness, browser/native smoke
  evidence, and the updated ExecPlan coverage gate.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agents were not used because this was the required internal
  milestone gate rather than an explicit delegated review request.
- Required findings fixed: added a top-of-file purpose comment to
  `src/engine/render/shaders/uber.wgsl` and regenerated the shader artifact.
- Follow-ups recorded: replace radial camera-distance cascade selection with
  exact view-depth selection, add cascade blending/fade zones, tune bias and PCF
  scale, and consider a stronger browser smoke assertion that compares
  shadow-on and shadow-off imagery.
- Rejected findings: none.
- Remaining risk: the sampled shadows are functional but still first-pass
  quality. The renderer still renders every prepared caster into every cascade,
  uses a fixed constant bias, has no cascade blending, and relies on manual
  screenshot inspection rather than a semantic visual-shadow diff.

Validation run for Milestone 6:

    npm run build:shaders
    npm run check:shaders
    npm run test:ts
    cargo test -p engine_web
    npm run check:wasm
    npm run smoke:rust
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser
    npm run test:rust
    npm run coverage:rust
    git diff --check

Results:

    npm run build:shaders: regenerated src/generated/render/uberShader.ts
    npm run check:shaders: passed
    npm run test:ts: 63 passing, including shader and wasm metadata contracts
    cargo test -p engine_web: 117 passed
    npm run check:wasm: passed after TypeScript tests rebuilt wasm artifacts
    npm run smoke:rust: passed
    npm run smoke:browser: passed on http://127.0.0.1:5184/
    npm run test:rust: passed across the Rust workspace
    npm run coverage:rust: files below 90% line coverage: none
    git diff --check: passed, with only Git line-ending warnings

Rust smoke wrote artifacts under
`artifacts/rust-smoke/run-1780843491-459`. The shadow atlas again had
`nonBlackPixels: 27228`, `uniqueLumaBuckets: 14`, and
`dominantLumaRatio: 0.8961334`.

Browser smoke wrote artifacts under
`artifacts/browser-smoke/2026-06-07T14-41-48-968Z`. The normal first-person
image had 137 unique color buckets, the cascade-index screenshot had 120 unique
color buckets, and the toggled third-person image had 158 unique color buckets.
The debug snapshot reported `frameShadowDrawCount: 4` in first person,
`frameShadowDrawCount: 48` in cascade/visibility/depth debug frames, and
`frameShadowDrawCount: 60` after the camera toggle.

Milestone 7 shader metadata, docs, and validation is complete.
`tools/build-shaders.mjs` now emits `shadowVertexEntryPoint` and
`shadowModelVertexEntryPoint` in `UBER_SHADER_METADATA`, and
`src/engine/render/shaders/UberShader.test.ts` asserts those names plus the
group-2 shadow uniform, depth texture, comparison sampler, compare sampling,
PCF averaging, cascade-index debug, and shadow-visibility debug contracts. No
new API contract entry was required because the metadata is an internal
build/test contract rather than a runtime browser API.

Milestone review:

- Scope: shader generator metadata, generated shader artifact, shader contract
  tests, API contract impact, and TypeScript validation.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes.
- Required findings fixed: added purpose headers to
  `tools/build-shaders.mjs` and
  `src/engine/render/shaders/UberShader.test.ts`.
- Follow-ups recorded: none beyond the existing shadow-quality and smoke
  coverage follow-ups.
- Rejected findings: none.
- Remaining risk: shader metadata checks entry-point names and binding strings,
  but final render quality is still covered by smoke screenshots rather than a
  full visual diff.

Validation run for Milestone 7:

    npm run build:shaders
    npm run check:shaders
    npm run test:ts
    npm run check:wasm
    git diff --check

Results:

    npm run build:shaders: regenerated src/generated/render/uberShader.ts
    npm run check:shaders: passed
    npm run test:ts: 63 passing
    npm run check:wasm: passed
    git diff --check: passed, with only Git line-ending warnings

Milestone 8 smoke coverage and final acceptance is complete. Browser smoke now
captures both `browser-shadow-cascade-index.png` and
`browser-shadow-visibility.png`, so the report includes a per-pixel sampled
shadow visibility artifact as well as the cascade-selection artifact. Native
Rust smoke continues to write four cascade visualizations plus
`shadow-atlas.png` for disk inspection.

Milestone review:

- Scope: final smoke coverage, browser visibility screenshot capture, final
  validation commands, coverage gate, generated artifact freshness, and
  ExecPlan closure.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes.
- Required findings fixed: added the missing browser smoke visibility screenshot
  and report entry.
- Follow-ups recorded: exact view-depth cascade selection, cascade blending,
  per-cascade caster culling, bias tuning, larger/distance-scaled PCF or PCSS,
  and stronger visual-diff smoke once the scene has deterministic test geometry.
- Rejected findings: none.
- Remaining risk: final smoke proves that CSM resources render, shadow maps are
  nonblank, sampled visibility is visible in debug, and normal frames are
  varied. It does not yet prove every desired artistic shadow case or guard
  against all acne/peter-panning/cascade-transition artifacts.

Final validation:

    npm test
    npm run smoke:rust
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser
    npm run coverage:rust
    git diff --check

Results:

    npm test: passed, including Rust workspace tests and 63 TypeScript tests
    npm run smoke:rust: passed
    npm run smoke:browser: passed on http://127.0.0.1:5184/
    npm run coverage:rust: files below 90% line coverage: none
    git diff --check: passed, with only Git line-ending warnings

Final Rust smoke artifacts are under
`artifacts/rust-smoke/run-1780844244-424`. The final shadow atlas had
`nonBlackPixels: 27228`, `uniqueLumaBuckets: 14`, and
`dominantLumaRatio: 0.8961334`.

Final browser smoke artifacts are under
`artifacts/browser-smoke/2026-06-07T14-58-25-598Z`. The report includes:

- `browser-first-person.png`: 139 unique color buckets and nonblank frame.
- `browser-shadow-cascade-index.png`: 122 unique color buckets.
- `browser-shadow-visibility.png`: 130 unique color buckets and visible
  grayscale shadow visibility bands.
- `browser-camera-toggle.png`: 158 unique color buckets after toggling to
  third person.
- `frameShadowDrawCount`: 4 in first person, 48 in debug views, 60 after the
  camera toggle, and 4 after reload.

The complete plan landed a first CSM implementation in the Rust-owned WebGPU
renderer: shared render math and AABBs, main-pass frustum culling, four stable
directional-light cascades, persistent shadow resources, depth-only shadow
passes, disk/debug visualization tools, shader metadata, browser debug hooks,
and main-pass direct-light shadow sampling with fixed 3x3 PCF. The intended
follow-up work is quality-focused rather than foundational: exact view-depth
cascade selection, blend zones near splits, caster culling per cascade,
receiver/slope bias tuning, richer debug overlays, quality presets, and larger
or contact-hardening soft-shadow kernels.

## Contract and Quality Baseline

This plan preserves these active contracts:

- OFG-API-001, Browser Shell To Rust Browser Game: no new TypeScript runtime
  renderer ownership or public scalar wasm methods. If debug/status fields are
  needed, expose them through Rust `debugSnapshot()` and existing TypeScript
  copying. If `setShadowDebugView` is added as a `GameCommand`, update
  `docs/API_CONTRACTS.md` and TypeScript command types in the same milestone.
- OFG-API-003, Debug And Smoke-Test Hooks: any new shadow status fields are
  browser test affordances only. TypeScript must not compute shadow cascades,
  terrain visibility, shadow-map depth, or renderer state.
- OFG-API-004, Terrain Vertex And Material Layout: terrain vertex stride and
  shader locations must remain synchronized across `terrain_core`,
  `engine_web`, and shader tests. Shadow vertex entry points must reuse the
  existing terrain/model vertex layouts.
- OFG-API-009, Forbidden TypeScript Ownership: do not recreate a TypeScript
  scene graph, render world, terrain manager, terrain renderer, or shadow
  manager.
- OFG-API-010, GLTF Model, Animation, And Skinning Loading: shadow rendering may
  reuse already prepared CPU-skinned model vertex buffers, but must not move
  GLTF ownership into TypeScript.

Quality rules:

- Keep files under control. If `wgpu_renderer.rs` grows too much, move pure
  math and shadow packet logic into separate modules instead of adding more
  large private helper blocks.
- Keep tests behavior-focused. Good names include
  `culls_aabb_outside_camera_frustum`,
  `snaps_cascade_projection_to_shadow_texels`, and
  `packs_shadow_uniforms_for_four_cascades`.
- Do not introduce a new shader language. Keep WGSL under
  `src/engine/render/shaders/uber.wgsl` and generated metadata behind
  `tools/build-shaders.mjs`.
- Before this ExecPlan is marked complete, run the default Rust coverage
  attention gate. Any modified implementation file that appears in the default
  filtered coverage output must either gain coverage until it clears the
  documented threshold or have an explicit exception recorded in the Decision
  Log with rationale.
- Run `milestone-review` after each milestone before marking it complete.

## Context and Orientation

Current renderer ownership:

- `crates/engine_web/src/wgpu_renderer.rs` owns the browser WebGPU device,
  surface, depth texture, camera/object bind groups, texture arrays, terrain and
  model pipelines, sky pipeline, GPU mesh handles, GPU texture handles, object
  uniforms, and draw submission.
- `crates/engine_web/src/render_packets.rs` builds the frame packet from
  `engine_core::RenderSnapshot`. It currently contains private camera matrix
  helpers: perspective, look-at, and matrix multiply.
- `crates/engine_web/src/render_uniforms.rs` packs the frame and object uniform
  arrays consumed by WGSL.
- `src/engine/render/shaders/uber.wgsl` is included by the Rust renderer and
  generated into TypeScript shader metadata for tests.
- `crates/ofg_test_harness/src/render_smoke/renderer.rs` is a native `wgpu`
  offscreen renderer for Rust image smoke. It duplicates the browser shader and
  bind group shapes enough to render terrain PNGs without a browser.

Current scene data flow:

- `engine_core` extracts visible scene mesh items with logical mesh/material IDs
  and world matrices.
- `engine_web::BrowserGameState` resolves scene mesh labels and material labels.
- `RustBrowserGame::render_frame` combines terrain chunk meshes and scene mesh
  items into parallel arrays of mesh handles, object handles, texture handles,
  world matrices, and material packets.
- `BrowserWgpuRenderer::render_engine_frame` builds the frame packet and calls
  `BrowserWgpuRenderer::render`.
- `BrowserWgpuRenderer::render` updates object uniforms and draws every render
  item. There is no general frustum culling yet.

Relevant current constants:

- `DEPTH_FORMAT` in `wgpu_renderer.rs` is `Depth24Plus` for the main camera
  depth buffer.
- Terrain vertices use `TERRAIN_VERTEX_FLOATS` floats per vertex with position
  at offset 0.
- Model vertices use `MODEL_VERTEX_FLOATS` floats per vertex with position at
  offset 0.

Terminology:

- AABB means axis-aligned bounding box. A local AABB encloses mesh vertices in
  mesh-local coordinates. A world AABB encloses that local AABB after applying a
  world transform.
- Frustum means the clipped volume of a camera or light projection. It is
  represented as six planes. An AABB is visible if it is not fully outside any
  frustum plane.
- Cascade means one camera-depth interval and its matching shadow-map layer.
- Light view-projection matrix means the directional-light view matrix composed
  with an orthographic projection that encloses one camera cascade.
- Texel snapping means moving the light-space orthographic bounds in whole
  shadow-map texel increments to reduce shadow swimming during camera movement.

Research anchors:

- Microsoft CSM notes: `https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps`
- NVIDIA CSM PDF: `https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf`
- WGSL texture comparison: `https://www.w3.org/TR/WGSL/#texturesamplecompare`

## Plan of Work

Milestone 1: Render math and culling foundations.

Add a pure Rust module `crates/engine_web/src/render_math.rs`. Move or expose the
matrix helpers currently private in `render_packets.rs` so the frame packet,
cascade builder, culling code, and tests share one implementation.

Implement these types and functions:

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct RenderVec3 { pub x: f32, pub y: f32, pub z: f32 }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct Aabb { pub min: RenderVec3, pub max: RenderVec3 }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct Plane { pub normal: RenderVec3, pub distance: f32 }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct Frustum { pub planes: [Plane; 6] }

    pub fn aabb_from_vertex_positions(
        vertices: &[f32],
        floats_per_vertex: u32,
        position_offset: usize,
    ) -> Option<Aabb>

    pub fn transform_aabb(aabb: Aabb, world_matrix: &[f32; 16]) -> Aabb

    pub fn frustum_from_view_projection(matrix: &[f32; 16]) -> Option<Frustum>

    pub fn frustum_intersects_aabb(frustum: Frustum, aabb: Aabb) -> bool

    pub fn perspective_mat4(
        fov_y_radians: f32,
        aspect: f32,
        near: f32,
        far: f32,
    ) -> Option<[f32; 16]>

    pub fn look_at_mat4(
        eye: RenderVec3,
        target: RenderVec3,
        up: RenderVec3,
    ) -> Option<[f32; 16]>

    pub fn orthographic_mat4(
        left: f32,
        right: f32,
        bottom: f32,
        top: f32,
        near: f32,
        far: f32,
    ) -> Option<[f32; 16]>

    pub fn multiply_mat4(a: &[f32; 16], b: &[f32; 16]) -> [f32; 16]

Add tests in `crates/engine_web/src/tests.rs`:

- `aabb_from_vertex_positions_reads_terrain_and_model_layouts`
- `transform_aabb_tracks_translation_and_scale`
- `frustum_intersects_aabb_accepts_intersecting_bounds`
- `frustum_intersects_aabb_rejects_fully_outside_bounds`
- `render_packet_builder_uses_shared_matrix_helpers`

Then extend `GpuMesh` in `wgpu_renderer.rs` with:

    local_bounds: Aabb

In `BrowserWgpuRenderer::register_mesh`, compute local bounds from vertex
positions. Reject invalid meshes if bounds cannot be computed.

Add a frame-internal prepared item structure:

    struct PreparedRenderItem {
        mesh_handle: ResourceHandle,
        object_handle: ResourceHandle,
        world_bounds: Aabb,
        vertex_layout: MeshVertexLayout,
    }

During `BrowserWgpuRenderer::render`, build prepared items after object uniforms
are updated. Compute the camera frustum from `frame_packet[0..16]`. Draw only
items whose world bounds intersect the camera frustum. Record both submitted
item count and visible draw count. For the first culling milestone, keep terrain
streaming/pruning unchanged; culling is render submission only.

Update status structs and debug snapshot only if needed:

- Add `frame_visible_draw_count` to `RustBrowserGameStatus`.
- Add a JS debug property such as `frameVisibleDrawCount`.
- Mirror the optional field in `src/engine/web/engineWebWasm.ts` only as a
  renderer status field, not as TypeScript-derived state.

Milestone 2: Cascade split and stable light matrices.

Add `crates/engine_web/src/shadows.rs` with pure CPU-side CSM calculations. The
first version should not allocate GPU resources.

Add constants in `crates/engine_web/src/config.rs`:

    pub const SHADOW_CASCADE_COUNT: usize = 4;
    pub const SHADOW_MAP_SIZE: u32 = 1024;
    pub const SHADOW_MAX_DISTANCE: f32 = 220.0;
    pub const SHADOW_SPLIT_LAMBDA: f32 = 0.65;

Implement:

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct ShadowCascade {
        pub near_depth: f32,
        pub far_depth: f32,
        pub light_view_projection: [f32; 16],
        pub light_view: [f32; 16],
        pub light_projection: [f32; 16],
        pub world_bounds: Aabb,
    }

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct ShadowCascadeSet {
        pub cascades: [ShadowCascade; SHADOW_CASCADE_COUNT],
        pub split_depths: [f32; SHADOW_CASCADE_COUNT],
    }

    pub fn compute_cascade_splits(
        near: f32,
        far: f32,
        max_shadow_distance: f32,
        lambda: f32,
    ) -> Option<[f32; SHADOW_CASCADE_COUNT]>

    pub fn camera_frustum_corners_world(
        eye: RenderVec3,
        target: RenderVec3,
        fov_y_radians: f32,
        aspect: f32,
        near: f32,
        far: f32,
    ) -> Option<[RenderVec3; 8]>

    pub fn build_shadow_cascades(
        eye: RenderVec3,
        target: RenderVec3,
        fov_y_radians: f32,
        aspect: f32,
        camera_near: f32,
        camera_far: f32,
        light_direction: RenderVec3,
    ) -> Option<ShadowCascadeSet>

Fit strategy:

- Clamp the shadow far distance to `min(camera_far, SHADOW_MAX_DISTANCE)`.
- Use the practical split scheme:
  `split = lambda * logarithmic + (1 - lambda) * linear`.
- For each cascade, compute the eight camera frustum slice corners in world
  space.
- Compute the slice center and a bounding sphere radius from those corners.
- Build a light view looking from `center - light_direction * radius` toward
  `center`, using a fallback up vector when the light is close to world up.
- Transform corners into light space.
- Use padded sphere or max extent for stable width/height.
- Set near/far to enclose cascade receivers plus a conservative caster margin.
  Start with a constant margin such as 80m, and record it as a tuning constant.
- Snap light-space min x/y to whole shadow texel increments, where
  `texel_size = extent / SHADOW_MAP_SIZE`.

Add tests:

- `cascade_splits_are_monotonic_and_clamped_to_shadow_distance`
- `cascade_corners_fit_inside_light_projection`
- `cascade_projection_snaps_to_shadow_texels`
- `cascade_builder_rejects_invalid_camera_or_light`
- `cascade_matrices_remain_finite_for_sun_near_up_vector`

Milestone 3: Shadow uniforms and GPU resources.

Extend `crates/engine_web/src/render_uniforms.rs`:

    pub const SHADOW_UNIFORM_FLOATS: usize = 76;

Layout target:

- 64 floats for four `mat4x4<f32>` light view-projection matrices.
- 4 floats for cascade split depths.
- 4 floats for shadow options: enabled, constant bias, normal bias, texel size.
- 4 spare floats for future blend distance or debug settings.

Implement:

    pub fn build_shadow_uniform_values(
        cascades: &ShadowCascadeSet,
        enabled: bool,
        constant_bias: f32,
        normal_bias: f32,
        texel_size: f32,
    ) -> Result<[f32; SHADOW_UNIFORM_FLOATS], RenderUniformError>

Add tests:

- `shadow_uniforms_pack_four_cascade_matrices_and_splits`
- `shadow_uniforms_disable_cleanly_when_no_shadow_pass_runs`

In `BrowserWgpuRenderer`, add fields:

    shadow_texture: wgpu::Texture,
    shadow_layer_views: Vec<wgpu::TextureView>,
    shadow_array_view: wgpu::TextureView,
    shadow_uniform_buffer: wgpu::Buffer,
    shadow_bind_group_layout: wgpu::BindGroupLayout,
    shadow_bind_group: wgpu::BindGroup,
    shadow_sampler: wgpu::Sampler,
    terrain_shadow_pipeline: wgpu::RenderPipeline,
    model_shadow_pipeline: wgpu::RenderPipeline,
    frame_visible_draw_count: u32,
    frame_shadow_draw_count: u32,

`wgpu_renderer.rs` is already large, so do not continue growing it with large
shadow-only helper blocks. Add a renderer-side helper module such as
`crates/engine_web/src/shadow_renderer.rs` for shadow texture/view creation,
shadow bind-group layout creation, shadow pipeline descriptors, and any
depth-debug copy/visualization helpers. `BrowserWgpuRenderer` may own the fields
and call the helper functions, but shadow-specific construction and pass setup
should live outside the main renderer file unless a very small inline change is
clearly simpler.

Create shadow resources in `BrowserWgpuRenderer::new`:

- Texture format: `wgpu::TextureFormat::Depth32Float`.
- Texture size: `SHADOW_MAP_SIZE x SHADOW_MAP_SIZE`.
- Layers: `SHADOW_CASCADE_COUNT`.
- Usage: `wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING`.
- Per-layer render views use `base_array_layer`.
- Array sample view uses `TextureViewDimension::D2Array`.
- Sampler uses `compare: Some(wgpu::CompareFunction::LessEqual)`,
  `AddressMode::ClampToEdge`, and linear filters if supported by WebGPU
  comparison sampling.

Add a shadow bind group layout at group 2:

- Binding 0: shadow uniform buffer visible to vertex and fragment.
- Binding 1: `texture_depth_2d_array` visible to fragment.
- Binding 2: comparison sampler visible to fragment.

Update the main terrain/model pipeline layout to include group 2. Update the sky
pipeline layout only if WGSL group declarations force it; otherwise keep sky
bound only to camera.

Update `crates/engine_web/src/renderer.rs` lightweight state if needed:

- Track `shadow_texture_count` only if the logical tests need it.
- Track `frame_shadow_draw_count` and `frame_visible_draw_count` as pure state.

Milestone 4: Depth-only shadow pipelines and shadow pass.

Update `src/engine/render/shaders/uber.wgsl` with:

    struct Shadows {
      lightViewProjection0: mat4x4<f32>,
      lightViewProjection1: mat4x4<f32>,
      lightViewProjection2: mat4x4<f32>,
      lightViewProjection3: mat4x4<f32>,
      cascadeSplits: vec4<f32>,
      options: vec4<f32>,
      spare: vec4<f32>,
    };

    @group(2) @binding(0) var<uniform> shadows: Shadows;
    @group(2) @binding(1) var shadowTexture: texture_depth_2d_array;
    @group(2) @binding(2) var shadowSampler: sampler_comparison;

Add vertex entries:

    @vertex
    fn shadowVertexMain(input: VertexInput) -> @builtin(position) vec4<f32>

    @vertex
    fn shadowModelVertexMain(input: ModelVertexInput) -> @builtin(position) vec4<f32>

The entries transform local position by `object.world`, then by the active
shadow matrix. Because WGSL cannot dynamically index separate matrix fields
without more machinery, prefer one of these two designs:

- Simpler first design: during each cascade pass, write the active cascade matrix
  into a dedicated first matrix field and have depth-only entries use
  `shadows.lightViewProjection0`.
- Later design: store matrices in `array<mat4x4<f32>, 4>` if WGSL alignment and
  host packing are clear and tests cover it.

For the first version, choose the simpler active-matrix design for the depth
pass, while still packing all four matrices for main-pass sampling.

Add `create_terrain_shadow_pipeline` and `create_model_shadow_pipeline` in
`wgpu_renderer.rs`:

- Same vertex buffer layouts as current terrain/model pipelines.
- Vertex entry point: `shadowVertexMain` or `shadowModelVertexMain`.
- `fragment: None`.
- Depth format: `Depth32Float`.
- `depth_write_enabled: true`.
- `depth_compare: LessEqual` or `Less`.
- Add depth bias in pipeline state:
  `constant: 2`, `slope_scale: 2.0`, `clamp: 0.0` as first conservative values.

In `BrowserWgpuRenderer::render`, sequence:

1. Build frame packet and frame uniforms.
2. Build shadow cascades from engine snapshot camera and light.
3. Build and update object uniforms and prepared render items once.
4. Render shadow passes into all cascade layers.
5. Render main color pass using camera, object, and shadow bind groups.

For each cascade shadow pass:

- Use the cascade's receiver frustum or light-space world bounds to filter
  `PreparedRenderItem`.
- Begin a render pass with only depth attachment pointing at that cascade's
  layer view.
- Clear depth to 1.0.
- Set group 2 shadow bind group and group 1 object bind group per item.
- Switch between terrain/model shadow pipelines based on `MeshVertexLayout`.
- Draw indexed.
- Count shadow draws.

Potential first-pass culling approach:

- Main pass: camera frustum vs item world AABB.
- Shadow pass: cascade camera slice frustum vs item world AABB, plus optional
  light-space bounds. This captures receivers and nearby casters but can miss
  off-camera casters. If missed caster shadows are visible, expand the cascade
  bounds by a caster margin or render all visible stream terrain chunks into
  each cascade for the first version and optimize later.

Record any choice in the Decision Log before marking the milestone complete.

Milestone 5: Shadow-map dumps and browser debug views.

Add inspection tooling immediately after the depth-only shadow pass works, before
tuning final lighting. The goal is to make incorrect light matrices, empty
shadow maps, bad culling, cascade selection errors, and bias problems visible.

Native shadow-map dumps:

- Extend `crates/ofg_test_harness/src/render_smoke/renderer.rs`, or add a helper
  module such as `crates/ofg_test_harness/src/render_smoke/shadow_debug.rs`, to
  render shadow depth layers into CPU-readable RGBA images.
- Prefer a debug visualization pass over direct depth copies. The pass samples
  `texture_depth_2d_array` with `textureLoad` or equivalent depth sampling,
  converts depth to grayscale, writes to an `Rgba8Unorm` texture, and reuses the
  existing padded texture readback path.
- Write one PNG per cascade and, if useful, one atlas PNG:

      artifacts/rust-smoke/<run-id>/shadow-cascade-0.png
      artifacts/rust-smoke/<run-id>/shadow-cascade-1.png
      artifacts/rust-smoke/<run-id>/shadow-cascade-2.png
      artifacts/rust-smoke/<run-id>/shadow-cascade-3.png
      artifacts/rust-smoke/<run-id>/shadow-atlas.png

- Include shadow debug artifact paths and simple pixel statistics in
  `report.json`. At minimum, fail if every shadow layer visualization is blank,
  solid white, or solid black after a scene that should contain casters.
- Add a targeted harness scenario with simple known geometry if the procedural
  terrain scene is too noisy for deterministic assertions.

Browser debug views:

- Add a small Rust-owned debug enum, for example:

      enum ShadowDebugView {
          Off,
          CascadeIndex,
          ShadowVisibility,
          ShadowDepthCascade0,
          ShadowDepthCascade1,
          ShadowDepthCascade2,
          ShadowDepthCascade3,
      }

- Add a `GameCommand` variant through the existing command lane:

      { type: "setShadowDebugView", view:
        "off" | "cascadeIndex" | "shadowVisibility" |
        "shadowDepthCascade0" | "shadowDepthCascade1" |
        "shadowDepthCascade2" | "shadowDepthCascade3" }

- TypeScript may expose this command and copy the current debug-view string in
  `debugSnapshot()`, but must not compute cascades, shadow depths, or visibility.
- Pack the active debug mode into the shadow uniform spare/options field.
- In `uber.wgsl`, when debug mode is active:
  - `CascadeIndex`: output a distinct flat color for the selected cascade.
  - `ShadowVisibility`: output grayscale sampled visibility, where black means
    fully shadowed and white means fully lit.
  - `ShadowDepthCascadeN`: output grayscale projected depth or loaded
    shadow-map depth for the selected cascade. If a full-screen shadow-map view
    is cumbersome in the browser path, first implement per-scene-pixel sampled
    shadow depth and record a follow-up for a fullscreen atlas view.
- Add browser smoke or debug-hook checks only where stable. The main acceptance
  is that the debug commands do not crash and screenshots can be captured for
  manual/AI inspection during development.

Suggested tests:

- `shadow_debug_mode_round_trips_through_game_command`
- `shadow_debug_snapshot_reports_current_view`
- `shadow_depth_visualization_rejects_blank_layers`
- `shadow_cascade_index_debug_uses_distinct_colors`

Milestone 6: Main-pass shadow sampling.

In `uber.wgsl`, extend `VertexOutput` if needed with camera view depth. A simple
first version can compute cascade selection in the fragment from world position:

- Add camera forward vector to frame uniform if needed, or derive view depth from
  the existing view-projection is not straightforward. Prefer adding a packed
  `cameraForwardAndShadowDistance: vec4<f32>` to the camera uniform only if it
  does not destabilize too much existing packing.
- Alternative: compare approximate distance from `camera.eyeWorld.xyz` to
  `input.worldPosition` against cascade splits. This is less exact than camera
  view-space z but acceptable for a first prototype and easier to pack.

Recommended first implementation:

- Add `cameraForwardAndShadowDistance` to `Camera`, increasing
  `FRAME_UNIFORM_FLOATS`.
- Pack camera forward from snapshot eye and target in
  `build_frame_packet_from_engine_snapshot` or `build_frame_uniform_values`.
- Use `dot(input.worldPosition - camera.eyeWorld.xyz, cameraForward.xyz)` for
  cascade depth.

Add WGSL helpers:

    fn shadowCascadeIndex(viewDepth: f32) -> i32

    fn shadowMatrixForCascade(cascadeIndex: i32) -> mat4x4<f32>

    fn sampleShadowVisibility(worldPosition: vec3<f32>, normal: vec3<f32>) -> f32

Sampling steps:

- If shadows disabled, return 1.0.
- Choose cascade by view depth and `shadows.cascadeSplits`.
- Transform world position by selected light view-projection.
- Divide by w.
- Convert clip xy to UV.
- Convert clip z to depth reference.
- Reject outside texture coordinates by returning 1.0.
- Apply constant bias first. Add normal bias later if the first version acnes.
- Use `textureSampleCompareLevel(shadowTexture, shadowSampler, uv, cascadeIndex, depth_ref)`.
- Average a fixed 3x3 PCF kernel using `SHADOW_MAP_SIZE` to compute texel
  offsets. Keep the radius small for the first version so browser cost remains
  predictable.

Apply visibility:

- Terrain: multiply the direct diffuse and direct specular terms by shadow
  visibility. Keep ambient unchanged.
- Metallic-roughness/specular-glossiness models: update `pbrDirectLight` or add
  a `shadowVisibility` parameter so ambient remains unshadowed and direct light
  is shadowed.

Avoid cascade blending and contact-hardening PCSS in the first complete version.
Add a visual TODO or plan follow-up only after the baseline CSM plus small PCF
passes smoke.

Milestone 7: Shader metadata, docs, and validation.

Update `tools/build-shaders.mjs` metadata:

- Add `shadowVertexEntryPoint`.
- Add `shadowModelVertexEntryPoint`.

Regenerate `src/generated/render/uberShader.ts` with:

    npm run build:shaders

Update `src/engine/render/shaders/UberShader.test.ts`:

- Assert shadow entry point names.
- Assert `texture_depth_2d_array`.
- Assert `sampler_comparison`.
- Assert `textureSampleCompareLevel`.
- Assert group 2 bindings.
- Assert debug-view branches or helper names for cascade index and shadow
  visibility.

Update `docs/API_CONTRACTS.md` only if new debug fields or shader contracts
become part of an active supported API. If changes are internal only, record in
this ExecPlan that no contract update was needed.

Milestone 8: Smoke coverage.

Browser smoke:

- Run with a non-default port because other worktrees are active:
  `OFG_SMOKE_PORT=5184 npm run smoke:browser` on shells that support inline env,
  or in PowerShell:
  `$env:OFG_SMOKE_PORT="5184"; npm run smoke:browser`.
- Existing browser smoke checks nonblank frames, reload, HUD camera toggle, and
  renderer status. Add shadow renderer-status assertions if shadow status fields
  are exposed.
- If shadow debug commands are exposed through `window.__ofgDebug`, capture at
  least one cascade-index or shadow-visibility screenshot when stable enough to
  avoid flaky smoke. Save it under `artifacts/browser-smoke/`.

Native Rust smoke:

- Extend `crates/ofg_test_harness/src/render_smoke/renderer.rs` to create the
  same shadow bind group shape and shadow pass, or add a smaller dedicated
  shadow smoke path. Reuse the shadow-map dump helpers from Milestone 5.
- Preferred first test scene: a simple opaque box or terrain ridge casting onto
  a flat receiving surface. If existing terrain scenarios are not deterministic
  enough to prove shadowing, add a synthetic mesh scenario inside the harness.
- Pixel acceptance should compare two regions or sample a known receiver area
  and assert that the shadowed region is darker while the frame remains varied.
- Save PNGs and `report.json` under `artifacts/rust-smoke/` as existing smoke
  already does. Include shadow cascade PNGs or an atlas in the same report.

## Concrete Steps

All commands run from `C:\dev\ofg-shadow-maps`.

Initial orientation:

    git status --short
    rg -n "shadow|frustum|cull|aabb|Depth32Float|texture_depth" crates src tools

After Milestone 1:

    npm run test:rust

Expected: Rust tests pass, including new culling/math tests.

After Milestone 2:

    npm run test:rust

Expected: Rust tests pass, including cascade split and texel-snapping tests.

After Milestone 3:

    npm run test:rust

Expected: Rust tests pass. Browser-specific `wgpu_renderer.rs` code compiles
during wasm build in later steps.

After Milestone 4:

    npm run check:shaders
    npm run build:wasm
    npm run test:rust

Expected: depth-only shadow shader entries compile, wasm-bindgen output builds,
and Rust tests pass.

After Milestone 5:

    npm run test:rust
    npm run smoke:rust

Expected: debug commands and/or debug state tests pass, Rust smoke writes
shadow cascade visualizations or a shadow atlas under `artifacts/rust-smoke/`,
and the debug images are not blank or solid for a scene with casters.

After Milestone 6:

    npm run check:shaders
    npm run build:wasm
    npm run test:rust

Expected: main-pass shadow sampling and PCF shader code compile, wasm-bindgen
output builds, and Rust tests pass.

After Milestone 7:

    npm run test:ts

Expected: TypeScript shader metadata tests and browser shell tests pass.

Final validation:

    npm test
    npm run smoke:rust
    $env:OFG_SMOKE_PORT="5184"; npm run smoke:browser
    npm run coverage:rust

Manual dev-server verification, if needed:

    $env:PORT="5183"; npm run dev

Then open `http://127.0.0.1:5183/`. If that port is occupied, choose another
explicit free port such as 5185 or 5190. Do not assume 5173 or 5174 in this
branch because other worktrees may be active.

Optional improvement to make manual port handling safer:

- Update `tools/dev-server.mjs` to scan forward from `PORT`, using the same
  local-port probing pattern as `tools/browser-smoke.mjs`.
- If this is done, add a small TypeScript or script-level test only if practical;
  otherwise record the change and manually verify server startup output.

## Milestone Review

After each implementation milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective as needed.
2. Update `docs/API_CONTRACTS.md` if a supported boundary changed.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands.
6. Record commands, artifact paths, smoke screenshots or reports, and remaining
   risks here.

## Validation and Acceptance

Internal acceptance:

- Render math has unit tests for AABB extraction, transformed bounds, frustum
  intersection, and invalid inputs.
- Cascade math has unit tests for split monotonicity, fit correctness,
  texel-snapping stability, finite matrices, and invalid inputs.
- Shadow uniforms have unit tests for layout and disabled state.
- Shader tests assert the new shadow entry points and group 2 bindings.
- Shadow debug commands or state have tests proving they round-trip through the
  Rust-owned command/debug path without TypeScript computing renderer state.
- Native debug image output can write one shadow-layer PNG per cascade or one
  atlas PNG and reject blank/solid layers in targeted scenes.
- No TypeScript terrain/render ownership is introduced.
- `wgpu_renderer.rs` remains readable. Pure math and packet logic live in
  modules instead of a single oversized renderer file.

Visual acceptance:

- Browser smoke passes on a non-default port and screenshots remain nonblank and
  varied.
- Rust smoke writes PNGs and `report.json` under `artifacts/rust-smoke/`.
- Rust smoke writes shadow-map visualization PNGs or an atlas that can be loaded
  in this view and inspected manually or by scripts.
- Browser debug views can render cascade index and sampled shadow visibility
  without crashing, and are available through existing debug command plumbing.
- At least one smoke or targeted image test proves a shadowed receiver region is
  darker than an adjacent lit region.
- Shadow edges are visibly softened by a small PCF kernel rather than being
  single-sample hard edges.
- Camera movement does not produce gross shadow swimming in ordinary movement.
  Fine shimmering can be noted as follow-up if texel snapping is present.

Command acceptance:

    npm run check:shaders
    npm test
    npm run smoke:rust
    $env:OFG_SMOKE_PORT="5184"; npm run smoke:browser
    npm run coverage:rust

All must pass before this plan is complete. If a command cannot run because of
local environment limitations, record the error, explain whether it is
environmental or code-related, and run the closest narrower validation.

Coverage acceptance:

- Run `npm run coverage:rust` before marking the full ExecPlan complete.
- Inspect the default filtered coverage output and
  `artifacts/coverage/rust/summary.pretty.json`.
- Modified implementation files such as `crates/engine_web/src/render_math.rs`,
  `crates/engine_web/src/render_packets.rs`, `crates/engine_web/src/render_uniforms.rs`,
  `crates/engine_web/src/wgpu_renderer.rs`, and future shadow modules must not
  appear in the default attention list unless the Decision Log records an
  intentional exception with rationale.

## Idempotence and Recovery

- Pure Rust math and shader packing changes are additive and can be rerun with
  `npm run test:rust`.
- `npm run build:shaders` deterministically rewrites generated shader metadata.
  If generated output changes unexpectedly, inspect `src/generated/render/uberShader.ts`
  before committing.
- `npm run build:wasm` deterministically rewrites `assets/wasm/engine_web/*` and
  generated WASM metadata. Do not manually edit generated WASM artifacts.
- If browser smoke leaves a server process running, stop the shell process and
  rerun with a different `OFG_SMOKE_PORT`.
- If manual `npm run dev` reports an occupied port, stop that process or rerun
  with another explicit `PORT`.
- Do not use `git reset --hard` or revert unrelated worktree changes. Inspect
  `git status --short` before and after each milestone.

## Artifacts and Notes

Expected final smoke artifacts:

    artifacts/rust-smoke/<run-id>/report.json
    artifacts/rust-smoke/<run-id>/*.png
    artifacts/rust-smoke/<run-id>/shadow-cascade-0.png
    artifacts/rust-smoke/<run-id>/shadow-cascade-1.png
    artifacts/rust-smoke/<run-id>/shadow-cascade-2.png
    artifacts/rust-smoke/<run-id>/shadow-cascade-3.png
    artifacts/rust-smoke/<run-id>/shadow-atlas.png
    artifacts/browser-smoke/report.json
    artifacts/browser-smoke/*.png

Expected useful status additions, if exposed:

    rendererStatus.shadowCascadeCount == 4
    rendererStatus.shadowMapSize == 1024
    debugSnapshot.shadowDebugView == "off"
    rendererStatus.frameVisibleDrawCount >= 1
    rendererStatus.frameShadowDrawCount >= 1

If these fields are added to debug status, update
`src/engine/web/engineWebWasm.ts`, `src/engine/web/browserGameTypes.ts`, and any
smoke checks in the same milestone.

## Interfaces and Dependencies

New internal Rust modules:

- `crates/engine_web/src/render_math.rs`
- `crates/engine_web/src/shadows.rs`

Updated Rust modules:

- `crates/engine_web/src/lib.rs`: `mod render_math; mod shadows;` and test-only
  or public exports needed by unit tests and the smoke harness.
- `crates/engine_web/src/config.rs`: shadow constants.
- `crates/engine_web/src/render_packets.rs`: use shared matrix helpers and
  optionally pack camera forward data.
- `crates/engine_web/src/render_uniforms.rs`: shadow uniform packing.
- `crates/engine_web/src/renderer.rs`: optional logical counters.
- `crates/engine_web/src/wgpu_renderer.rs`: GPU shadow resources, depth-only
  pipelines, shadow pass, culling integration, renderer status.
- `crates/engine_web/src/tests.rs`: math, cascade, uniform, and status tests.
- `crates/ofg_test_harness/src/render_smoke/renderer.rs`: native shadow smoke
  and shadow visualization render/readback.
- `crates/ofg_test_harness/src/render_smoke/shadow_debug.rs`: optional helper
  module for shadow-layer PNG or atlas generation if that keeps the smoke
  renderer readable.

Updated shader and generated artifacts:

- `src/engine/render/shaders/uber.wgsl`
- `tools/build-shaders.mjs`
- `src/generated/render/uberShader.ts`
- `src/engine/render/shaders/UberShader.test.ts`

Optional TypeScript debug shape updates:

- `src/engine/web/engineWebWasm.ts`
- `src/engine/web/browserGameTypes.ts`
- `src/engine/web/rustBrowserGameRuntime.ts`
- `tools/browser-smoke.mjs`

WebGPU dependency assumptions:

- `wgpu = "0.20.1"` is already used by `engine_web` and the test harness.
- Required WebGPU features should remain empty for the first CSM version. If a
  feature becomes necessary, update renderer initialization, browser smoke
  expectations, and this Decision Log before relying on it.

Follow-up work after this plan:

- Cascade blending near split boundaries.
- Larger or distance-scaled PCF kernels with better slope or receiver-plane bias.
- Contact-hardening PCSS shadows.
- Shadow debug overlays for cascade layers and splits.
- Quality presets for cascade count and resolution.
- Better caster selection for off-camera casters.
- GPU culling or indirect draws when scene counts grow.
