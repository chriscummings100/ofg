# Add HDR Post Processing To The Rust/WGPU Renderer

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

This plan follows `PLANS.md` in this repository. After each milestone, run the
repo-local `milestone-review` skill before marking that milestone complete.

## Purpose / Big Picture

Add a Rust-owned post-processing pipeline to OFG rendering with filmic tone
mapping, bloom, and depth of field. After this work, the playable browser scene
renders into an HDR scene target, applies optional debug views and post effects
inside Rust/wgpu, and presents a tone-mapped final image to the browser canvas.

The user-visible result is a richer renderer: bright sky/sun/material highlights
can bloom, the final image has a controllable filmic response instead of direct
surface output, and optional depth of field can be used for cinematic shots or
debug camera views. Debug displays are a first-class part of the work: early
views for scene color, depth, pre/post tone map, bloom levels, and circle of
confusion are critical for building and tuning the effects.

## Progress

- [x] (2026-06-07) Researched WebGPU/wgpu constraints and practical tone
  mapping, bloom, and depth-of-field approaches.
- [x] (2026-06-07) Created this ExecPlan from the research and repo
  architecture review.
- [x] (2026-06-07) Milestone 1: Add HDR scene target,
  sampleable depth/linear-depth output, identity post pass, and debug view
  routing.
- [x] (2026-06-07) Milestone 2: Normalize scene-linear color output and add
  filmic tone mapping.
- [x] (2026-06-07) Milestone 3: Add bloom extraction, half-resolution blur,
  compositing, commands/status, and bloom debug view.
- [x] (2026-06-07) Milestone 4: Add depth-of-field CoC calculation,
  blur/composite, commands/status, and DoF debug views.
- [x] (2026-06-07) Milestone 5: Finish debug controls, smoke coverage,
  documentation, final validation, and archive this completed plan.

## Surprises & Discoveries

- Observation: `crates/engine_web/src/wgpu_renderer.rs` currently renders sky,
  terrain, and models directly into the WebGPU surface format, choosing an sRGB
  surface format when available.
  Evidence: `BrowserWgpuRenderer::new` chooses `capabilities.formats` with
  `format.is_srgb()` and the main render pass writes to the surface view.

- Observation: Depth currently uses `Depth24Plus` with only
  `RENDER_ATTACHMENT` usage, so post effects cannot read it directly.
  Evidence: `create_depth_texture` in `crates/engine_web/src/wgpu_renderer.rs`
  creates a texture with `usage: wgpu::TextureUsages::RENDER_ATTACHMENT`.

- Observation: The shader has mixed color-space behavior today. Terrain returns
  sampled/lit values directly, while model PBR paths call `linearToSrgb` inside
  `fragmentMain`.
  Evidence: `src/engine/render/shaders/uber.wgsl` contains `linearToSrgb` calls
  in `shadeMetallicRoughness` and `shadeSpecularGlossiness`, but not in
  `shadeTerrain`.

- Observation: The active API contracts already list
  `crates/engine_web/src/wgpu_renderer.rs` as oversized and under split
  pressure.
  Evidence: `docs/API_CONTRACTS.md` risk register names the file as over the
  preferred maximum size.

- Observation: The native Rust smoke renderer shares `uber.wgsl`; changing the
  scene fragment output to add linear depth also required a second `R32Float`
  smoke render target.
  Evidence: `npm run smoke:rust` failed risk was avoided by updating
  `crates/ofg_test_harness/src/render_smoke/renderer.rs`; subsequent smoke
  wrote passing PNGs and `report.json` under
  `artifacts/rust-smoke/run-1780839027-491/`.

- Observation: Browser smoke initially timed out on first-run setup but a
  targeted Playwright probe showed the renderer was healthy after the frame
  arrived.
  Evidence: The probe reported `frameDrawCount: 12`,
  `postProcessDebugView: "final"`, WebGPU available, and cross-origin isolation
  true. Increasing the smoke wait to 20 seconds and rerunning
  `npm run smoke:browser` passed.

- Observation: The workspace did not have `node_modules` installed, so
  `npm run test:ts` initially reached the test command but could not find
  `mocha`.
  Evidence: `Test-Path node_modules` returned false; `npm ci` installed the
  locked dependencies, after which `npm run test:ts` passed.

- Observation: The first coverage run listed the new post-process Rust module
  below the default 90% attention threshold.
  Evidence: `npm run coverage:rust` initially reported
  `crates/engine_web/src/post_process.rs` at 20.2% line coverage. Adding a
  native wgpu unit test for post-process resource creation, resize, and debug
  presentation brought the filtered coverage report to `none`.

- Observation: Browser smoke needed to exercise both post-process debug view
  selection and tone-map setting changes to cover Milestone 2's control lane.
  Evidence: The smoke script now selects `linearDepth`, sets exposure to `1.1`,
  selects `postToneMap`, verifies the reported exposure on a later frame, then
  restores final output. `npm run smoke:browser` passed with
  `browser-post-tone-map.png` under
  `artifacts/browser-smoke/2026-06-07T14-01-52-136Z/`.

- Observation: The first bloom implementation can be validated with a lowered
  smoke threshold and stronger intensity without making the default gameplay
  image overly glowy.
  Evidence: Browser smoke now sets bloom to threshold `0.2` and intensity `0.6`,
  selects the `bloom` debug view, verifies Rust-reported bloom settings on a
  later frame, and captures `browser-bloom.png` under
  `artifacts/browser-smoke/2026-06-07T14-13-58-730Z/`. The default remains
  threshold `1.0` and intensity `0.08`.

- Observation: `check:wasm` should not be run in parallel with commands that
  regenerate `engine_web` artifacts.
  Evidence: A parallel `npm run check:wasm` and `npm run test:ts` run reported
  stale WASM artifacts while the TypeScript test lane was rebuilding them. A
  standalone `npm run check:wasm` immediately afterwards passed.

- Observation: Chrome's WebGPU WGSL validation caught non-uniform control flow
  around a DoF `textureSample` path that the native Rust unit test did not
  reject.
  Evidence: The first `npm run smoke:browser` run for Milestone 4 failed with
  `'textureSample' must only be called from uniform control flow` in
  `dofBlurredSceneColor`. Removing the early return and sampling unconditionally
  fixed the browser validation error; the rerun passed with DoF screenshots.

- Observation: DoF debug screenshots are visibly nonblank and varied enough for
  smoke to catch solid-output regressions.
  Evidence: `browser-dof-coc.png` had 21 sampled color buckets and dominant
  bucket ratio `0.565`; `browser-dof-blurred.png` had 131 sampled color buckets
  and dominant bucket ratio `0.270` in
  `artifacts/browser-smoke/2026-06-07T14-26-09-419Z/report.json`.

## Decision Log

- Decision: Implement post processing entirely in Rust/wgpu, with TypeScript
  only forwarding commands and displaying debug/HUD state.
  Rationale: `docs/API_CONTRACTS.md` forbids TypeScript ownership of WebGPU
  devices, render passes, render targets, pipelines, and draw submission.
  Date/Author: 2026-06-07 / Codex

- Decision: Start with an HDR scene target and identity post pass before adding
  individual effects.
  Rationale: Filmic tone mapping, bloom, and depth of field all depend on a
  stable offscreen scene color texture. This also creates the place where debug
  views can be selected.
  Date/Author: 2026-06-07 / Codex

- Decision: Make debug displays part of the early architecture, not a late
  polish task.
  Rationale: Visual post effects are difficult to validate from final pixels
  alone. Views for raw scene color, linear depth, pre/post tone map, bloom mip
  levels, and DoF circle of confusion will make implementation, smoke testing,
  and future tuning much faster.
  Date/Author: 2026-06-07 / Codex

- Decision: Prefer render-pass fullscreen pipelines first; defer compute
  shaders until profiling shows a need.
  Rationale: OFG is still a lightweight prototype. A render-pass approach is
  simpler, portable in WebGPU/wgpu, and avoids extra compute-to-fragment
  synchronization complexity while the effects are being established.
  Date/Author: 2026-06-07 / Codex

- Decision: Store post-process linear depth/distance in a color attachment
  (`R32Float`) rather than trying to sample the existing depth attachment.
  Rationale: The current depth texture is `Depth24Plus` and render-attachment
  only. A linear-depth target gives immediately useful debug output and a stable
  input for later DoF.
  Date/Author: 2026-06-07 / Codex

- Decision: Add browser smoke coverage for the `linearDepth` debug view in
  Milestone 1.
  Rationale: The user explicitly called out debug displays as critical and
  useful early. A screenshot-backed smoke step proves the command/status route
  and catches blank debug outputs.
  Date/Author: 2026-06-07 / Codex

- Decision: Wait for the renderer `frameIndex` to advance after selecting a
  post-process debug view in browser smoke.
  Rationale: The debug status changes immediately after the command, but the
  screenshot must prove the next rendered frame used that view.
  Date/Author: 2026-06-07 / Codex

- Decision: Let the sRGB browser surface perform final display encoding after
  post-process tone mapping.
  Rationale: The browser renderer already prefers an sRGB surface format.
  Applying a manual gamma transform in WGSL would double-encode on that path.
  Date/Author: 2026-06-07 / Codex

- Decision: Ship Milestone 3 bloom as a single half-resolution bright-pass blur
  target, with a later mip pyramid left as a tuning/performance follow-up.
  Rationale: The first bloom slice proves the HDR extraction, Rust command
  surface, debug view, and pre-tone-map composite with much less frame-graph
  complexity. The active acceptance criterion is observable bloom contribution,
  not a particular mip count.
  Date/Author: 2026-06-07 / Codex

- Decision: Ship Milestone 4 DoF as a default-off symmetric focus-distance pass
  with a small fullscreen blur, not the full foreground/background GPU Gems
  multi-pass algorithm.
  Rationale: The first playable DoF slice proves renderer-owned linear-depth
  usage, CoC debugging, command/status plumbing, and final composite behavior.
  Foreground/background separation and larger blur kernels can be added once the
  scene has stronger cinematics or focus controls that need them.
  Date/Author: 2026-06-07 / Codex

## Outcomes & Retrospective

Milestone 1 outcome: the browser renderer now draws sky, terrain, and models
into Rust-owned offscreen scene targets, then presents through a fullscreen post
pass. Debug views currently include final output, HDR scene color, and linear
depth. Browser smoke now captures both final output and linear depth. Filmic
tone mapping, bloom, and DoF are still future milestones.

Milestone 1 validation evidence:

- `npm run check:shaders` passed.
- `npm run check:wasm` passed.
- `npm test` passed.
- `npm run test:rust` passed.
- `npm run test:ts` passed after `npm ci` installed locked dev dependencies.
- `npm run smoke:rust` passed and wrote PNG/report artifacts under
  `artifacts/rust-smoke/run-1780839027-491/`.
- `npm run smoke:browser` passed and wrote final, linear-depth, camera-toggle,
  and reload screenshots under
  `artifacts/browser-smoke/2026-06-07T13-42-55-732Z/`.
- `npm run coverage:rust` passed with no files in the default filtered
  under-threshold report.

Milestone review:

- Scope: Milestone 1 HDR/offscreen post foundation, final/scene-color/depth
  debug routing, shader artifacts, Rust and browser smoke coverage, active docs,
  and generated WASM artifacts.
- Reviewers: contract, code quality, legacy, correctness, and validation were
  performed locally. Sub-agent tooling was available, but not used because the
  current tool contract requires an explicit user request for delegated
  sub-agents.
- Required findings fixed: browser smoke now waits for `frameIndex` to advance
  after selecting a post-process debug view before screenshotting it; native
  post-process GPU coverage was added so `post_process.rs` no longer appears in
  the filtered coverage attention report.
- Follow-ups recorded: none beyond the existing Milestone 2-5 work.
- Rejected findings: none.
- Validation rerun: `npm run smoke:browser`, `npm run check:wasm`,
  `npm run check:shaders`, `npm test`, and `npm run coverage:rust`.
- Remaining risk at Milestone 1 close: color-space normalization was deferred
  to Milestone 2, so that checkpoint's fullscreen pass was identity-style
  rather than a final filmic tone mapper.

Milestone 2 outcome: scene shaders now output scene-linear color. Terrain
albedo is decoded from sRGB to linear, model PBR paths no longer convert lit
color back to display space, and `post.wgsl` owns exposure plus an ACES-style
filmic curve. Debug views now include `postToneMap`, and browser smoke captures
that view after changing exposure through `RustBrowserGame.command(...)`.

Milestone 2 validation evidence:

- `npm run build:shaders` regenerated `uberShader.ts` and `postShader.ts`.
- `npm run check:shaders` passed.
- `npm run check:wasm` passed.
- `npm test` passed.
- `npm run test:rust` passed.
- `npm run test:ts` passed.
- `npm run smoke:rust` passed and wrote PNG/report artifacts under
  `artifacts/rust-smoke/run-1780840683-175/`.
- `npm run smoke:browser` passed and wrote final, linear-depth, post-tone-map,
  camera-toggle, and reload screenshots under
  `artifacts/browser-smoke/2026-06-07T14-01-52-136Z/`.
- `npm run coverage:rust` passed with no files in the default filtered
  under-threshold report.

Milestone 2 review:

- Scope: scene-linear shader output, filmic/exposure post shader, tone-map
  command/status/debug hooks, shader artifacts, active docs, and smoke coverage.
- Reviewers: contract, code quality, legacy, correctness, and validation were
  performed locally. Sub-agent tooling was available, but not used because the
  current tool contract requires an explicit user request for delegated
  sub-agents.
- Required findings fixed: browser smoke now exercises the tone-map command and
  verifies the requested exposure on a later rendered frame before capturing
  `postToneMap`.
- Follow-ups recorded: none beyond the existing bloom/DoF milestones.
- Rejected findings: none.
- Validation rerun: `npm run smoke:browser`; prior Milestone 2 validation also
  included `npm run smoke:rust`, `npm run check:wasm`, `npm run check:shaders`,
  `npm test`, and `npm run coverage:rust`.
- Remaining risk: non-sRGB browser surface fallback would not get explicit
  manual display encoding yet; current browser path prefers sRGB and smoke
  validates that active path.

Milestone 3 outcome: the post-process frame graph now extracts bright HDR scene
energy into a half-resolution `Rgba16Float` bloom target, blurs that
contribution with a small fullscreen filter, composites bloom before tone
mapping, and exposes the `bloom` debug view. Rust commands and renderer status
now cover bloom enabled, threshold, and intensity. Browser smoke captures
`browser-bloom.png` after changing bloom settings and waiting for a rendered
frame with those settings.

Milestone 3 validation evidence:

- `cargo fmt` passed.
- `npm run build:shaders` regenerated `postShader.ts`.
- `npm run check:shaders` passed.
- `npm run check:wasm` passed after rerunning it without a concurrent artifact
  writer.
- `npm run test:rust` passed.
- `npm run test:ts` passed.
- `npm run smoke:rust` passed and wrote PNG/report artifacts under
  `artifacts/rust-smoke/run-1780841587-873/`.
- `npm run smoke:browser` passed and wrote final, linear-depth, bloom,
  post-tone-map, camera-toggle, and reload screenshots under
  `artifacts/browser-smoke/2026-06-07T14-13-58-730Z/`.

Milestone 3 review:

- Scope: bloom settings, Rust/wgpu half-resolution bloom target and fullscreen
  extraction pass, final pre-tone-map composite, `bloom` debug view,
  TypeScript command/status/debug forwarding, active docs, shader artifacts, and
  smoke coverage.
- Reviewers: contract, code quality, legacy, correctness, and validation were
  performed locally. Sub-agent tooling was available, but not used because the
  current tool contract requires an explicit user request for delegated
  sub-agents.
- Required findings fixed: the native post-process GPU test initially exposed a
  bloom pipeline bind-group layout mismatch; the bloom pipeline now binds an
  explicit empty group 0 before its bloom resources, and `npm run test:rust`
  passes.
- Follow-ups recorded: consider replacing the single half-resolution bloom
  target with a mip downsample/upsample pyramid if bloom quality, radius, or
  performance tuning needs it.
- Rejected findings: none.
- Validation rerun: `cargo fmt`, `npm run test:rust`,
  `npm run build:shaders`, `npm run check:shaders`, `npm run test:ts`,
  `npm run check:wasm`, `npm run smoke:rust`, and `npm run smoke:browser`.
- Remaining risk: the bloom pass is visually useful and smoke-covered, but it
  is not yet a full multi-level bloom pyramid.

Milestone 4 outcome: depth of field is now a Rust/wgpu-owned post-process
setting. The post shader derives per-pixel circle of confusion from the
linear-depth target, samples a small HDR scene+bloom blur in the final pass,
keeps default gameplay sharp with DoF disabled, and exposes `dofCoc` and
`dofBlurred` debug views. Rust commands and renderer status now cover DoF
enabled, focus distance, focus range, and maximum blur pixels. Browser smoke
enables a strong DoF setting, captures CoC and blurred-scene screenshots, then
restores defaults before the existing camera-toggle and reload checks.

Milestone 4 validation evidence:

- `cargo fmt` passed.
- `npm run build:shaders` regenerated `postShader.ts`.
- `npm run check:shaders` passed after the WGSL control-flow fix.
- `npm run check:wasm` passed before the browser smoke build; a final
  standalone check remains part of Milestone 5 validation.
- `npm run test:rust` passed after the WGSL control-flow fix.
- `npm run test:ts` passed for the DoF command/status/test updates before the
  final shader control-flow patch; browser smoke then rebuilt and typechecked
  the app with that shader patch.
- `npm run smoke:rust` passed and wrote PNG/report artifacts under
  `artifacts/rust-smoke/run-1780842223-209/`.
- `npm run smoke:browser` passed and wrote final, linear-depth, bloom,
  post-tone-map, DoF CoC, DoF blurred, camera-toggle, and reload screenshots
  under `artifacts/browser-smoke/2026-06-07T14-26-09-419Z/`.

Milestone 4 review:

- Scope: DoF settings, Rust command/status/debug plumbing, post-shader CoC and
  blur/composite logic, `dofCoc` and `dofBlurred` debug views, TypeScript debug
  hooks, active docs, shader artifacts, and browser smoke coverage.
- Reviewers: contract, code quality, legacy, correctness, and validation were
  performed locally. Sub-agent tooling was available, but not used because the
  current tool contract requires an explicit user request for delegated
  sub-agents.
- Required findings fixed: browser smoke exposed a WGSL uniform-control-flow
  violation around `textureSample`; `dofBlurredSceneColor` now samples
  unconditionally and `npm run smoke:browser` passes.
- Follow-ups recorded: consider foreground/background CoC separation,
  downsampled DoF buffers, and stronger native image-smoke coverage for post
  debug outputs if the effect becomes more than a debug/cinematic tool.
- Rejected findings: none.
- Validation rerun: `npm run build:shaders`, `npm run check:shaders`,
  `npm run test:rust`, and `npm run smoke:browser`.
- Remaining risk: the DoF pass is intentionally simple and may blur across
  foreground/background silhouettes; the default is disabled to protect normal
  gameplay.

Milestone 5 outcome: the post-process feature set is complete for this plan.
The active browser renderer now owns HDR scene color, linear-depth debug output,
filmic tone mapping, bloom, and default-off DoF. Browser debug hooks expose
commands and Rust-reported settings for final, scene color, linear depth,
post-tone-map, bloom, DoF CoC, and DoF blurred views. Active contract and
architecture docs now describe the final command/status surface and ownership
rules. The completed plan was moved to `docs/archived/`; future source of truth
is the active docs and implementation.

Milestone 5 validation evidence:

- `cargo fmt` passed.
- `npm run build` passed.
- `npm run check:shaders` passed.
- `npm run check:wasm` passed.
- `npm test` passed.
- `npm run smoke` passed. Rust smoke wrote artifacts under
  `artifacts/rust-smoke/run-1780842613-408/`; browser smoke wrote final,
  linear-depth, bloom, post-tone-map, DoF CoC, DoF blurred, camera-toggle, and
  reload screenshots under
  `artifacts/browser-smoke/2026-06-07T14-30-52-912Z/`.
- `npm run coverage:rust` passed with no files in the default filtered
  under-threshold report.
- `git -c safe.directory=C:/dev/ofg-postprocess diff --check` passed.

Milestone 5 review:

- Scope: final feature integration, active docs, generated artifacts, browser
  smoke screenshots, Rust smoke, coverage, and ExecPlan closeout/archive.
- Reviewers: contract, code quality, legacy, correctness, and validation were
  performed locally. Sub-agent tooling was available, but not used because the
  current tool contract requires an explicit user request for delegated
  sub-agents.
- Required findings fixed: none.
- Follow-ups recorded: bloom is intentionally a single half-resolution target;
  DoF is intentionally a simple symmetric focus blur; stronger native
  post-process image-smoke outputs can be added if these effects become
  tuning-heavy.
- Rejected findings: none.
- Validation rerun: `cargo fmt`, `npm run build`, `npm run check:shaders`,
  `npm run check:wasm`, `npm test`, `npm run smoke`,
  `npm run coverage:rust`, and `git diff --check`.
- Remaining risk: post-process quality is first-pass and smoke-tested rather
  than art-directed; the debug displays make future tuning visible.

## Contract and Quality Baseline

This plan preserves the active runtime ownership rules in `docs/API_CONTRACTS.md`
and `docs/ARCHITECTURE.md`.

`OFG-API-001: Browser shell to Rust browser game` remains active. New controls
must use `RustBrowserGame.command(...)` variants and new state must flow through
`debugSnapshot()`. Do not add separate wasm methods for individual effects.

`OFG-API-003: Debug and smoke-test hooks` is extended only as a browser test and
inspection surface. Debug hooks may expose selected post-process view names,
enabled flags, render-target sizes, and sampled status, but must not compute
rendering state in TypeScript.

`OFG-API-004: Terrain vertex and material layout` must remain intact. The terrain
vertex layout, object uniform layout, and shader locations are not part of this
feature unless a milestone explicitly updates their tests and contracts.

`OFG-API-009: Forbidden TypeScript ownership` is critical. TypeScript must not
create WebGPU textures, pipelines, bind groups, render passes, scene graphs, or
post-process resources. It may display controls and send opaque commands.

Quality gates:

- Keep implementation files human readable and avoid growing
  `crates/engine_web/src/wgpu_renderer.rs` further when focused modules are
  reasonable.
- Add comments at the top of new files describing what they own.
- Add function comments for new Rust functions, especially renderer helpers.
- Run `milestone-review` after each milestone and address required findings
  before marking it complete.
- Keep generated shader artifacts deterministic through `tools/build-shaders.mjs`.
- For implementation work, run coverage and confirm changed implementation files
  do not appear in the default filtered coverage output unless an exception is
  recorded with rationale.

## Context and Orientation

The repository root is `C:/dev/ofg-postprocess`.

The playable browser renderer lives in
`C:/dev/ofg-postprocess/crates/engine_web/src/wgpu_renderer.rs`. It creates the
WebGPU surface, device, queue, depth texture, camera/object bind groups, terrain
pipeline, model pipeline, and sky pipeline. Today `BrowserWgpuRenderer::render`
gets the surface texture, starts one render pass, draws the sky fullscreen
triangle, then draws terrain and model meshes directly to the surface.

The shared WGSL shader lives in
`C:/dev/ofg-postprocess/src/engine/render/shaders/uber.wgsl`. Rust includes this
source directly. TypeScript shader tests use the generated artifact at
`C:/dev/ofg-postprocess/src/generated/render/uberShader.ts`, which is produced by
`C:/dev/ofg-postprocess/tools/build-shaders.mjs`.

The native offscreen smoke renderer lives in
`C:/dev/ofg-postprocess/crates/ofg_test_harness/src/render_smoke/renderer.rs`.
It mirrors the browser shader/pipeline path and writes PNGs/reports under
`artifacts/rust-smoke/`. Any post-process pipeline used by the browser should be
covered here as soon as practical, because Rust image smoke is the repo's
renderer visual-regression lane.

Relevant terms:

- HDR scene color: an offscreen floating-point color texture, probably
  `wgpu::TextureFormat::Rgba16Float`, that can store values greater than `1.0`
  before tone mapping.
- Tone mapping: mapping scene-linear HDR color into displayable color. Filmic
  curves add a toe for shadows and shoulder for highlights.
- Bloom: blurred bright HDR energy composited back over the scene before final
  tone mapping.
- Depth of field: blur based on camera depth. The blur radius is driven by a
  circle of confusion, abbreviated CoC.
- Debug view: a selectable fullscreen output that shows an intermediate texture
  or scalar field instead of the final composite.

## Web Research Notes

WebGPU/wgpu resource constraints:

- `wgpu::TextureFormat::Rgba16Float` is a 16-bit float RGBA format suitable for
  HDR scene color. `TextureUsages::RENDER_ATTACHMENT` allows a texture to be a
  render-pass output, and `TextureUsages::TEXTURE_BINDING` allows it to be
  sampled in later passes.
- `wgpu::SurfaceConfiguration` describes the presentable surface. In wgpu 0.20,
  the only supported swapchain usage is `RENDER_ATTACHMENT`, so post processing
  should render to offscreen textures first and then render the final pass into
  the surface.
- Sources: `https://docs.rs/wgpu/0.20.1/wgpu/enum.TextureFormat.html`,
  `https://docs.rs/wgpu/0.20.1/wgpu/struct.TextureUsages.html`,
  `https://docs.rs/wgpu/0.20.1/wgpu/type.SurfaceConfiguration.html`.

Tone mapping:

- ACES describes output transforms as converting scene-referred image data into a
  rendered/display state, with a rendering transform followed by display
  encoding.
- Unreal documents ACES-style filmic tone mapping as the default physically based
  post-process path and notes that emissive colors bloom more physically under
  that model.
- John Hable's filmic work describes practical toe, shoulder, white point,
  exposure, and gamma handling for real-time rendering.
- Sources: `https://docs.acescentral.com/system-components/output-transforms/`,
  `https://dev.epicgames.com/documentation/unreal-engine/post-process-effects?application_version=4.27`,
  `https://filmicworlds.com/blog/filmic-tonemapping-with-piecewise-power-curves/`.

Bloom:

- Unity's bloom documentation describes bloom as bright light spilling from image
  borders and recommends using HDR thresholding around values above LDR range.
- Khronos' Vulkan samples describe a plausible frame pipeline as rendering HDR
  image, running HDR+bloom, then tone mapping into the swapchain pass.
- Sources:
  `https://docs.unity.cn/2017.2/Documentation/Manual/PostProcessing-Bloom.html`,
  `https://github.khronos.org/Vulkan-Site/samples/latest/samples/performance/async_compute/README.html`.

Depth of field:

- GPU Gems 3 Chapter 28 describes a first-person-game-friendly post-process DoF
  approach using z-buffer/depth information. Its complete algorithm downsamples
  foreground CoC, blurs near CoC, calculates the actual foreground CoC, then
  applies foreground/background CoC in a final variable-width blur pass.
- It explicitly warns that the technique assumes usable depth information is
  already generated by the engine; OFG should make this a renderer-owned output
  rather than trying to recover it in TypeScript.
- Source:
  `https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-28-practical-post-process-depth-field`.

## Plan of Work

Milestone 1 establishes the offscreen post-processing frame graph. Add a focused
Rust module such as
`C:/dev/ofg-postprocess/crates/engine_web/src/post_process.rs` for reusable
post-process resource descriptors, settings, debug-view enums, and fullscreen
pipeline helpers. Add a new WGSL shader file such as
`C:/dev/ofg-postprocess/src/engine/render/shaders/post.wgsl`. Extend
`tools/build-shaders.mjs` and shader tests so the post shader is generated and
checked like `uber.wgsl`.

In `BrowserWgpuRenderer`, add resize-managed post-process resources:
`scene_color`, a sampleable linear depth or distance texture, and a small set of
temporary color textures. The first version should draw sky/terrain/models into
`scene_color`, write linear depth/distance for geometry, then run a fullscreen
identity pass into the surface. Add debug output modes immediately: final,
scene color, linear depth, and possibly raw depth if both are available. The
debug view command can be wired through Rust first and exposed to TypeScript in
a later milestone.

Milestone 2 clarifies color-space ownership and adds filmic tone mapping.
Geometry shaders should output scene-linear color to the HDR target. The final
post pass applies exposure, a filmic curve, and display encoding. Remove
premature per-material display conversion from `uber.wgsl` once the final pass
owns it. Add debug views for pre-tone-map scene color and post-tone-map output.
Default settings should be mild enough to preserve the current scene readability.

Milestone 3 adds bloom. Extract bright HDR energy from the scene color with
threshold and soft knee, build a half-resolution or quarter-resolution
downsample chain, blur/upsample with a tent or separable filter, and composite
the result into the HDR scene before tone mapping. Add debug views for bright
pass, individual bloom levels, and final bloom contribution. Use fixed small mip
counts at first and clamp by viewport dimensions.

Milestone 4 adds depth of field. Use linear camera distance and camera near/far
metadata to calculate CoC. Start with artist-friendly parameters: focus distance,
near start/end, far start/end, maximum near blur, maximum far blur, and enabled
flag. Generate a downsampled CoC/debug texture, blur it, then composite sharp and
blurred scene color. Add debug views for linear depth, CoC, blurred scene, and
DoF composite. Keep default DoF disabled or extremely subtle to avoid hurting
normal gameplay.

Milestone 5 completes controls, smoke tests, docs, and cleanup. Add
`GameCommand` variants in `src/engine/web/browserGameTypes.ts` and Rust command
handling for post settings and debug view selection. TypeScript may add HUD/debug
buttons or keyboard/debug-hook controls, but it must only send commands and
display Rust-reported state. Extend browser smoke to toggle one debug view and
verify the canvas remains nonblank. Extend Rust image smoke to render final,
depth, and bloom/DoF debug outputs when effects are enabled. Update
`docs/API_CONTRACTS.md` if the debug snapshot shape changes.

## Concrete Steps

Use `C:/dev/ofg-postprocess` as the working directory for all commands.

Before implementation:

    git -c safe.directory=C:/dev/ofg-postprocess status --short
    npm test

For each shader milestone:

    npm run build:shaders
    npm run check:shaders
    npm run test:ts

For Rust renderer milestones:

    npm run test:rust
    npm run smoke:rust

For browser command, HUD, WASM loading, or canvas integration changes:

    npm run build:wasm
    npm run smoke:browser

For integrated validation:

    npm test
    npm run build
    npm run smoke
    npm run coverage:rust

Expected successful evidence:

- `npm run check:shaders` reports no stale generated shader artifacts.
- `npm test` passes Rust and TypeScript logic lanes.
- `npm run smoke:rust` writes nonblank PNGs and a passing `report.json` under
  `artifacts/rust-smoke/`.
- `npm run smoke:browser` verifies WebGPU canvas rendering, reload health,
  browser isolation, Rust runtime sentinels, and the existing camera toggle.
- If debug views are added to browser smoke, screenshots under
  `artifacts/browser-smoke/` show nonblank final and debug outputs.
- `npm run coverage:rust` does not list modified implementation files in the
  default filtered under-threshold report, unless an explicit exception is
  recorded in this plan.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective sections.
2. Update `docs/API_CONTRACTS.md` or `docs/ARCHITECTURE.md` if ownership,
   commands, debug snapshot fields, shader contracts, or smoke responsibilities
   changed.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this plan.
4. Apply required review findings, or record a rejected finding with rationale
   in the Decision Log.
5. Re-run relevant validation commands.
6. Only then mark the milestone complete in Progress.

## Validation and Acceptance

The implementation is accepted when:

- The playable browser path renders through Rust-owned HDR scene color and a
  fullscreen post pass, with no TypeScript WebGPU ownership introduced.
- Debug views can show at least final output, pre-tone-map scene color, linear
  depth, bloom contribution or level, and DoF CoC once the corresponding effect
  exists.
- Filmic tone mapping is applied in the final post pass and can be toggled or
  configured through Rust commands/debug settings.
- Bloom can be enabled and produces visible glow from HDR highlights while
  preserving a nonblank, non-solid frame in smoke tests.
- Depth of field can be enabled, uses depth/CoC data from Rust/wgpu, and has
  debug views that make focus and blur behavior inspectable.
- Browser smoke and Rust smoke both still pass.
- `npm run build`, `npm test`, `npm run smoke`, and `npm run coverage:rust`
  complete successfully, or any coverage exception is explicitly recorded with
  rationale.

## Idempotence and Recovery

Post-process resources should be recreated on resize using the same dimensions
as the configured surface. Re-running resize or reset commands must destroy or
replace old texture views safely and leave no stale handles in renderer state.

Keep the first pass identity-capable. If a later effect regresses rendering,
debug settings should allow the renderer to bypass bloom and DoF and present the
tone-mapped or identity scene output. If the post shader artifact becomes stale,
run `npm run build:shaders` and verify with `npm run check:shaders`.

If a milestone introduces browser failure, preserve the Rust-only post-process
logic and use `npm run smoke:rust` debug images to isolate whether the issue is
shader/resource logic or browser surface integration.

## Artifacts and Notes

Initial renderer state reviewed on 2026-06-07:

- `crates/engine_web/src/wgpu_renderer.rs` owns the browser WebGPU surface,
  depth texture, pipelines, mesh/resource stores, and render submission.
- `src/engine/render/shaders/uber.wgsl` contains the current sky, terrain, and
  model shader entry points.
- `tools/build-shaders.mjs` currently generates only `uberShader.ts`; it must be
  extended if `post.wgsl` is added.
- `crates/ofg_test_harness/src/render_smoke/renderer.rs` mirrors the current
  browser render path and should be updated to exercise post effects offscreen.

Debug displays are critical, especially early:

- Scene color debug view: shows HDR scene rendering before post effects.
- Linear depth debug view: verifies depth/distance output and exposes near/far
  precision problems.
- Pre/post tone-map debug views: isolate color-space and exposure issues.
- Bloom bright-pass and bloom-level debug views: show whether threshold, soft
  knee, blur radius, and mip selection are working.
- CoC debug view: shows depth-of-field focus ranges and foreground/background
  blur radii.
- Final composite debug view: confirms the normal presented output after all
  enabled effects.

## Interfaces and Dependencies

Planned Rust types and interfaces:

- `PostProcessSettings`: enabled flags and parameters for tone mapping, bloom,
  and DoF.
- `PostProcessDebugView`: enum for `Final`, `SceneColor`, `LinearDepth`,
  `PreToneMap`, `PostToneMap`, `BloomBrightPass`, `BloomLevel(u32)`,
  `BloomComposite`, `DofCoc`, `DofBlurred`, and `DofComposite`, trimmed if some
  views are not yet implemented.
- `PostProcessResources`: resize-owned textures, texture views, bind groups,
  and fullscreen pipelines.
- `BrowserWgpuRenderer::set_post_process_settings(...)` or equivalent internal
  method called only through `RustBrowserGame.command(...)`.

Planned TypeScript command surface:

- Extend `GameCommand` in
  `C:/dev/ofg-postprocess/src/engine/web/browserGameTypes.ts` with a
  post-process settings/debug-view command. The exact shape should stay
  object-based and narrow, following current command patterns.

Planned shader artifacts:

- `C:/dev/ofg-postprocess/src/engine/render/shaders/post.wgsl`
- `C:/dev/ofg-postprocess/src/generated/render/postShader.ts`
- Tests in `C:/dev/ofg-postprocess/src/engine/render/shaders/` confirming entry
  points, generated hash, texture bindings, debug-view symbols, and tone-map
  function presence.
