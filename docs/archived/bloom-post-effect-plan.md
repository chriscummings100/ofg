# Add Bloom Post Effect and Temp Buffer Reuse

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

OFG needs a bloom post effect so bright HDR features such as the procedural sun, high-energy sky highlights, emissive factory parts, and future energy systems can glow naturally after lighting but before final display conversion. Bloom should be implemented as a renderer-owned post effect: it reads the completed HDR scene color texture, builds a blurred bloom texture at reduced resolutions, and the tone mapper composites that bloom with the HDR scene before applying exposure, ACES tone mapping, and output encoding.

The first user-visible result should be subtle and controllable. With bloom enabled, the sun disc and other values above the HDR threshold should produce a soft halo without washing out the whole scene. With bloom disabled or intensity set to zero, screenshots should match the tone-mapped sky/shadow renderer apart from ordinary floating-point noise.

This plan also introduces recyclable temporary render targets through a static `TempBuffer` system. Bloom needs several temporary color textures, and future post effects will need more. Rather than each pass owning one-off scratch textures, renderer passes should ask `TempBuffer::get(...)` for a matching temporary buffer. A pass may call `TempBuffer::release(buffer)` as soon as it has encoded the last GPU use of that buffer, and `TempBuffer::end_frame()` automatically returns any remaining frame-scoped buffers. Returned buffers can be reused by later encoded work because WebGPU commands on the same queue preserve ordering and resource synchronization, while actual texture destruction is kept separate and only applies to stale, inactive buffers.

## Progress

- [x] (2026-07-04 06:20Z) Read `C:\dev\ofg\docs\plans\procedural-sky-environment-plan.md` and identified the intended pass order: opaque and sky into HDR scene color, then tone mapping to the platform target.
- [x] (2026-07-04 06:35Z) Researched bloom implementation patterns from production and vendor references: Unreal standard bloom and convolution bloom, Unity bloom settings and filter choices, NVIDIA GPU Gems real-time glow, AMD FidelityFX SPD and Blur, and the Call of Duty Advanced Warfare post-process talk metadata.
- [x] (2026-07-04 06:43Z) Read the current renderer contracts and local worktree state, including HDR `SceneColorTarget`, shared `DepthTarget`, `ToneMapPass`, `RendererCounters`, and the active cascaded shadow map plan.
- [x] (2026-07-04 06:43Z) Drafted this ExecPlan with a bloom pyramid, tone-map composite integration, and recyclable intermediate render targets.
- [x] (2026-07-04 08:05Z) Reviewed this plan with five sub-agents and accepted the required findings around pass ordering, dependencies, exact sampling rules, uniform safety, GPU validation, CMake updates, and performance/memory budgets.
- [x] (2026-07-04 08:09Z) Re-read the completed procedural sky implementation: `Renderer::render_impl` now draws opaque and sky inside one scene-color render pass, ends that pass, then tone maps the `SceneColorTarget`.
- [x] (2026-07-04 08:18Z) Refined `TempBuffer` from delayed frame recycling to immediate ordered reuse with optional early return and stale cleanup.
- [x] (2026-07-04 08:54Z) Implemented Milestone 0: static `TempBuffer` system with ordered reuse, explicit early return, automatic frame-end return, stale cleanup, stats, counters, docs, and doctests.
- [x] (2026-07-04 08:54Z) Completed Milestone 0 review locally: sub-agent tools were available but not used because their contract requires explicit user-requested delegation; required raw-handle prune finding was fixed and validation was rerun.
- [x] (2026-07-04 08:59Z) Implemented Milestone 1: bloom defaults, validation, soft-threshold helper, deterministic pyramid sizing, uniform packing, CMake wiring, docs, and doctests.
- [x] (2026-07-04 08:59Z) Completed Milestone 1 review locally: fixed overflow-prone ceiling division, added non-finite setting tests, corrected ceil-rounded memory notes, and reran validation.
- [x] (2026-07-04 10:04Z) Implemented Milestone 2: `BloomPass`, prefilter/downsample/upsample WGSL, `BloomResult`, diagnostics, tone-map bloom composition, CMake wiring, and deterministic GPU pixel readback coverage.
- [x] (2026-07-04 10:12Z) Completed Milestone 2 review locally: fixed missing `ToneMapBloomInput` tint validation and reran formatting, C++ tests, WASM build, and diff hygiene.
- [x] (2026-07-04 10:48Z) Implemented Milestone 3: six-level bloom quality path, diagnostics, deterministic GPU image validation, temp-buffer reuse checks, and smoke budget assertions.
- [x] (2026-07-04 11:12Z) Implemented Milestone 4: renderer integration, visual smoke, screenshots, runtime/debug diagnostics, docs, coverage refresh, and final validation.

## Surprises & Discoveries

- Observation: The completed sky plan already provides almost all of the post-process boundary bloom needs.
  Evidence: `C:\dev\ofg\docs\archived\procedural-sky-environment-plan.md` specifies `opaque PBR pass -> RGBA16Float scene color -> tone-map pass -> platform color target`, then later extends scene rendering to opaque plus sky before tone mapping.

- Observation: The working tree already contains part of the sky/HDR groundwork.
  Evidence: `C:\dev\ofg\cpp\include\ofg\render\scene_color_target.hpp`, `depth_target.hpp`, `tone_map_pass.hpp`, and their `.cpp` files exist locally; `C:\dev\ofg\cpp\src\render\renderer.cpp` renders opaque content into `SceneColorTarget` and then runs `ToneMapPass`.

- Observation: A single downscaled buffer plus one horizontal/vertical blur is a valid minimal bloom, but a pyramid better matches production game bloom.
  Evidence: NVIDIA GPU Gems describes low-resolution glow sources and separable blur for performance. Unreal documents combining multiple blur sizes, with wide blur work shifted to lower resolutions. Unity exposes bloom downscale, max iterations, scatter/radius, and filter choices such as Gaussian, Dual, and Kawase.

- Observation: The active cascaded shadow map plan creates another durable offscreen target family, but not a recyclable target need.
  Evidence: `C:\dev\ofg\docs\plans\cascaded-shadow-maps-plan.md` specifies a persistent `ShadowMapTarget` depth texture array, whereas bloom requires temporary color textures whose lifetimes are limited to the post-process chain.

- Observation: `C:\dev\ofg\GUIDES.md` is referenced by project instructions but is not present in this checkout.
  Evidence: `Get-Content -Path GUIDES.md -Raw` from `C:\dev\ofg` failed with `Cannot find path 'C:\dev\ofg\GUIDES.md'`. The guide content is available at `C:\dev\ofg\docs\GUIDES.md` and was read during this planning update.

- Observation: The procedural sky plan has been completed and archived.
  Evidence: `C:\dev\ofg\docs\archived\procedural-sky-environment-plan.md` exists, while `C:\dev\ofg\docs\plans\procedural-sky-environment-plan.md` no longer exists.

- Observation: The completed sky renderer creates the exact post-scene boundary bloom needs.
  Evidence: `C:\dev\ofg\cpp\src\render\renderer.cpp` begins an `OFG scene color pass`, calls `OpaquePass::draw`, calls `SkyPass::draw`, ends/releases that render pass, and then calls `ToneMapPass::render` with `m_scene_color_target->view()`.

- Observation: Temporary texture reuse does not need a three-frame cooldown when the same WebGPU texture handle is reused by later encoded work.
  Evidence: The WebGPU specification defines command buffers as GPU work submitted to a queue, and MDN's `GPUQueue.submit` reference notes that resources used by encoded commands must be available and not destroyed at submit. Therefore `TempBuffer` should keep texture handles alive and reuse them according to pass order; only actual stale destruction needs frame-boundary cleanup. References: `https://www.w3.org/TR/webgpu/` and `https://developer.mozilla.org/en-US/docs/Web/API/GPUQueue/submit`.

- Observation: The milestone-review skill asks for `C:\dev\ofg\docs\ARCHITECTURE.md`, but this checkout does not contain that file.
  Evidence: `Test-Path docs\ARCHITECTURE.md` returned false during the Milestone 0 review input pass.

- Observation: The original 1080p bloom memory note had floor-rounded lower levels, while the implementation and algorithm use ceiling division.
  Evidence: `build_bloom_pyramid_plan(1920, 1080, default_bloom_settings())` produces `960x540`, `480x270`, `240x135`, `120x68`, `60x34`, and `30x17`; the Artifacts and Notes estimate now uses those values.

- Observation: The deterministic bloom fixture can validate real bloom pixels when a Vulkan Dawn adapter is available, while preserving null-backend default unit-test behavior.
  Evidence: `C:\dev\ofg\cpp\tests\webgpu_test_utils.hpp` keeps `WGPUBackendType_Null` as the default `TestGpuContext::create(...)` backend, and `C:\dev\ofg\cpp\tests\bloom_pass_test.cpp` only requests `WGPUBackendType_Vulkan` for the real-backend readback case. `npm run test:cpp` passed with the readback test on this machine.

- Observation: The first visual defaults were too conservative for easy browser verification.
  Evidence: The initial defaults were threshold `1.0`, intensity `0.08`, and scatter `0.7`; the user reported little visible bloom in the dev server. Defaults were retuned to threshold `0.6`, soft knee `0.75`, intensity `0.35`, and scatter `0.85` so the procedural sun bloom is visually inspectable during integration.

- Observation: The visible sun wash after bloom integration was primarily sky-source haze rather than bloom itself.
  Evidence: The user supplied a sun-facing browser screenshot where the procedural sun blended into a broad pale haze and clarified that the bloom defaults were acceptable. The sky shader's pre-bloom `sun_radiance` halo used `exp((alignment - 1.0) * 34.0) * 0.28`, which created a wide HDR source before bloom. The shader now uses a tighter halo falloff and lower halo energy while keeping the bloom defaults unchanged.

- Observation: The smoke background reference changed after the sky-side sun and bloom tuning.
  Evidence: Native render smoke first failed with `Background coverage too low: 0.000674` against the previous `[198,216,236,255]` background reference, while the rendered PNG was visually healthy. Sampling the refreshed sky background returned approximately `[223,234,246,255]`; updating `tools/smoke-contract.json` to that value restored native smoke with `backgroundRatio: 0.430462`, `sceneRatio: 0.569538`, and `passed: true`.

- Observation: Runtime smoke diagnostics now prove that the browser and native paths execute the bloom-capable renderer path.
  Evidence: The latest browser smoke report at `C:\dev\ofg\artifacts\browser-smoke\report.json` records `bloomActiveLevelCount: 6`, `bloomEncodedPassCount: 11`, `tempBufferPeakBytes: 3151472`, `tempBufferCreatedCount: 22`, `tempBufferReusedCount: 528`, and `tempBufferDiscardedCount: 11` after resize/warm-up. The latest native smoke report at `C:\dev\ofg\artifacts\render-smoke\report.json` records `bloomActiveLevelCount: 6`, `bloomEncodedPassCount: 11`, `bloomDrawCount: 11`, `bloomEstimatedReadBytes: 5043832`, `bloomEstimatedWriteBytes: 1922832`, `tempBufferPeakBytes: 1922832`, `tempBufferEarlyReleaseCount: 11`, and `passed: true`.

- Observation: C++ coverage for `BloomPass` needed a narrow defensive-line exception after real shader and renderer tests were added.
  Evidence: `npm run coverage` passed with `cpp/src/render/bloom_pass.cpp` at `90.04%` after documenting and excluding twelve WebGPU null-return guard lines in `C:\dev\ofg\tools\cpp-coverage.mjs` and `C:\dev\ofg\COVERAGE.md`. The excluded paths are defensive branches for failed shader/module/pipeline/bind group creation that the current Dawn test harness cannot force deterministically.

- Observation: The original CPU command-encoding delta target is not measured by the final first implementation.
  Evidence: Bloom settings intentionally remain C++ owned and are not exposed through TypeScript, and the renderer has no public enabled/disabled timing toggle. The final evidence instead records deterministic shader tests, active smoke diagnostics, bloom pass-count limits, temp-buffer memory limits, and successful browser/native smoke. A dedicated profiling hook can add enabled-vs-disabled timing once renderer profiling is introduced.

## Decision Log

- Decision: Implement bloom as an HDR post effect before tone mapping, not as part of sky, opaque lighting, or platform output rendering.
  Rationale: Bloom represents camera/display response to bright scene color. It should see all completed HDR contributors, including opaque lighting, shadows, sky, sun disc, and future emissive materials, then feed the tone mapper.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use a bloom pyramid rather than only one downscaled buffer and one 2-pass blur.
  Rationale: A pyramid produces multiple halo radii cheaply. The wide bloom comes from lower-resolution levels, avoiding large full-resolution kernels while giving artists a controllable scatter/radius.
  Date/Author: 2026-07-04 / Codex.

- Decision: Start with render-pass full-screen draws and `textureLoad`-based filtering instead of compute shaders.
  Rationale: OFG already has full-screen tone mapping, and render passes avoid adding compute pipeline ownership before it is needed. `textureLoad` avoids depending on optional float texture filtering behavior and keeps tests close to the current `ToneMapPass`.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use `WGPUTextureFormat_RGBA16Float` for bloom intermediate color targets in the first implementation.
  Rationale: The scene color target is already `RGBA16Float`; keeping bloom in the same linear HDR format avoids precision and format-conversion questions while the effect is being validated. Lower-memory formats such as `R11G11B10Float` can be researched later if WebGPU portability and quality justify them.
  Date/Author: 2026-07-04 / Codex.

- Decision: Add a static `TempBuffer` system before implementing bloom.
  Rationale: Bloom immediately needs multiple scratch render targets, and later post effects will too. A static lifecycle facade, like `Game` and `Resources`, keeps the public interface simple: create it with the GPU context during boot, call `TempBuffer::get(...)` for temporary render targets, and let frame lifecycle calls manage automatic return and cleanup.
  Date/Author: 2026-07-04 / Codex.

- Decision: Keep the first post-effect orchestration explicit in `Renderer`, not through a generic render graph or virtual `PostEffect` interface.
  Rationale: The current renderer has a small set of concrete passes. An explicit `BloomPass` plus `TempBuffer` use is easier to test and can evolve into a graph after there are several post effects with real scheduling pressure.
  Date/Author: 2026-07-04 / Codex.

- Decision: Composite bloom in `ToneMapPass` instead of writing bloom back into `SceneColorTarget`.
  Rationale: The tone mapper is already the final HDR-to-display boundary. Adding bloom there avoids an extra full-resolution HDR combine pass and preserves the original scene color for future effects or debug captures.
  Date/Author: 2026-07-04 / Codex.

- Decision: Make bloom settings C++ owned, with no TypeScript UI in the first version.
  Rationale: Renderer state belongs in C++ under the existing ownership contracts. Browser UI controls can be added later once settings are stable.
  Date/Author: 2026-07-04 / Codex.

- Decision: Treat Milestones 0 and 1 as independent groundwork, but require a stable HDR scene-color/tone-map boundary before GPU bloom integration.
  Rationale: The `TempBuffer` system and CPU-side bloom settings can be built without sky. GPU bloom must sample a completed `SceneColorTarget`, so integration starts only after opaque/sky scene rendering has ended its WebGPU render pass or after a deterministic HDR GPU fixture exists.
  Date/Author: 2026-07-04 / Codex, after plan review.

- Decision: Use `TempBuffer::get(...)` plus optional `TempBuffer::release(buffer)` for temporary render targets.
  Rationale: The clean model is that a pass asks for a temp buffer and gets a temp buffer back. If the pass knows it has encoded the final GPU use of that buffer, it can return it immediately; otherwise `TempBuffer::end_frame()` returns it automatically. This avoids ownership-heavy wording while still letting bloom reuse scratch targets within the same frame.
  Date/Author: 2026-07-04 / Codex, after user refinement.

- Decision: Restrict first-version bloom sizing knobs to deterministic supported values.
  Rationale: Allowing arbitrary downscale, scatter, or tiny target behavior makes tests and GPU kernels ambiguous. The first implementation rejects `initial_downscale` values other than `2` or `4`, rejects `scatter` outside `[0, 1]`, requires positive `min_level_extent`, and skips bloom when the first bloom level would be smaller than the inclusive minimum extent.
  Date/Author: 2026-07-04 / Codex, after plan review.

- Decision: Use one tone-map pipeline shape with a durable black bloom fallback texture.
  Rationale: Always binding a bloom input avoids pipeline/layout switching and keeps disabled bloom on the same shader path. Disabled bloom or zero intensity binds the 1x1 black fallback and writes the same tone-mapped result as the current path.
  Date/Author: 2026-07-04 / Codex, after plan review.

- Decision: Avoid mutating one uniform range between encoded bloom passes.
  Rationale: Queue writes to the same buffer range while encoding multiple passes can be observed by all draws at execution time. Bloom shaders should derive per-pass source dimensions with `textureDimensions` where possible; any required per-pass CPU parameters must be written into preallocated uniform slots selected by dynamic offset or distinct bind groups before the pass is encoded.
  Date/Author: 2026-07-04 / Codex, after plan review.

- Decision: Do not add a three-frame cooldown for temporary texture reuse.
  Rationale: `TempBuffer` reuses existing WebGPU texture handles, not raw memory that is freed and reallocated behind the GPU. Within one queue, later encoded passes observe earlier pass writes after the render pass boundaries and implicit synchronization, so a buffer returned after its final encoded use may be handed out to later work. Actual destruction is separate: stale, inactive buffers are discarded only after they have not been reused for a named cleanup window, initially ten frames.
  Date/Author: 2026-07-04 / Codex, based on user correction and WebGPU resource-ordering review.

- Decision: Temporarily bias the first renderer-integrated bloom defaults toward visual confirmation.
  Rationale: Until there is a live UI/debug toggle, the default effect must be visible enough for browser screenshots and human review. After screenshots prove the path, the settings can be tuned back down if the game scene reads too hazy.
  Date/Author: 2026-07-04 / Codex, after user visual feedback.

- Decision: Leave the current bloom defaults alone and tune the procedural sun source when the sun reads clouded-over.
  Rationale: Bloom should amplify bright HDR sources, not compensate for a sky shader that already paints a broad milky sun aura. Narrowing the sky-side circumsolar halo and warming the sun color gives bloom a cleaner source without reducing the visually approved bloom settings.
  Date/Author: 2026-07-04 / Codex, after user sky-tuning feedback.

- Decision: Make the procedural sun bolder by increasing disc/core size and strength, not by restoring the wide halo.
  Rationale: The user confirmed the less hazy sky looked better but the sun needed more presence. Enlarging the `disc` and `core` thresholds and increasing their HDR multipliers makes the sun read bigger and gives bloom a stronger source while keeping the broad sky-side halo restrained.
  Date/Author: 2026-07-04 / Codex, after user visual feedback.

- Decision: Expose read-only bloom and temp-buffer diagnostics through `RuntimeDebugStatus`, not mutable browser controls.
  Rationale: The first implementation should keep renderer state owned by C++ while still letting tests, browser smoke, native smoke, and reports prove the post-effect path, pass count, memory high-water data, and stale cleanup behavior.
  Date/Author: 2026-07-04 / Codex, during Milestones 3 and 4.

- Decision: Accept pass-count and temp-memory smoke budgets as the first implementation's performance evidence, and defer enabled-vs-disabled CPU command-encoding delta until a renderer profiling hook exists.
  Rationale: Bloom controls are intentionally not exposed to TypeScript, and adding a public toggle only for timing would weaken the ownership contract. Native and browser smoke now enforce the 11-pass default budget and temp-buffer memory ceilings; CPU delta should be measured by a future profiling pass with an explicit renderer-owned setting path.
  Date/Author: 2026-07-04 / Codex, during final validation.

- Decision: Document a targeted C++ coverage exception for twelve `BloomPass` defensive WebGPU null-return lines.
  Rationale: The primary bloom code path is covered by settings tests, direct real-backend bloom readback, renderer integration tests, browser smoke, native smoke, and coverage. The remaining uncovered lines are defensive guards for failed WebGPU object creation that the current Dawn harness cannot trigger reliably without fault-injection support.
  Date/Author: 2026-07-04 / Codex, during final coverage validation.

## Outcomes & Retrospective

Milestone 0 added `C:\dev\ofg\cpp\include\ofg\render\temp_buffer.hpp`, `C:\dev\ofg\cpp\src\render\temp_buffer.cpp`, and `C:\dev\ofg\cpp\tests\temp_buffer_test.cpp`, and wired them into `C:\dev\ofg\cpp\CMakeLists.txt`. It also updated `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` so the renderer-internal `TempBuffer` ownership and lifetime rules are documented.

Milestone review:

- Scope: Milestone 0 static `TempBuffer` system, docs, CMake, and doctests.
- Reviewers: contract, code quality, legacy, correctness, and validation passes were performed locally. Sub-agents were not spawned because the available multi-agent tool requires explicit user-requested delegation.
- Required findings fixed: stale cleanup originally copied raw WebGPU texture/view handles while compacting entries; `prune_stale_entries()` now releases the actual stale entry before erasing it, and tests now cover unsupported usage flags plus double-return safety.
- Follow-ups recorded: none.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check -- cpp\include\ofg\render\temp_buffer.hpp cpp\src\render\temp_buffer.cpp cpp\tests\temp_buffer_test.cpp cpp\CMakeLists.txt docs\API_CONTRACTS.md docs\SYSTEMS.md docs\plans\bloom-post-effect-plan.md`.
- Remaining risk: `TempBuffer` is not yet integrated into `Renderer`; that is intentionally deferred until Milestone 4 after bloom settings and pass work. No screenshots were produced because Milestone 0 has no visual output.

Milestone 1 added `C:\dev\ofg\cpp\include\ofg\render\bloom_settings.hpp`, `C:\dev\ofg\cpp\src\render\bloom_settings.cpp`, and `C:\dev\ofg\cpp\tests\bloom_settings_test.cpp`, and wired them into `C:\dev\ofg\cpp\CMakeLists.txt`. It also updated `C:\dev\ofg\docs\SYSTEMS.md` to name the new CPU-side bloom settings and planning module without claiming bloom is visually integrated yet.

Milestone review:

- Scope: Milestone 1 bloom settings, validation, pyramid planning, uniform packing, docs, CMake, and doctests.
- Reviewers: contract, code quality, legacy, correctness, and validation passes were performed locally. Sub-agents were not spawned because the available multi-agent tool requires explicit user-requested delegation.
- Required findings fixed: `ceil_div()` originally used `value + divisor - 1`, which could overflow for extreme `uint32_t` inputs; it now uses quotient/remainder. Tests now cover non-finite scatter and tint values. The plan's memory estimate now uses the same ceil-rounded dimensions as the implementation.
- Follow-ups recorded: none.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check -- cpp\include\ofg\render\bloom_settings.hpp cpp\src\render\bloom_settings.cpp cpp\tests\bloom_settings_test.cpp cpp\CMakeLists.txt docs\SYSTEMS.md docs\plans\bloom-post-effect-plan.md`.
- Remaining risk: settings are not yet consumed by GPU bloom or renderer integration; that is Milestones 2 through 4. No screenshots were produced because Milestone 1 has no visual output.

Milestone 2 added `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp`, `C:\dev\ofg\cpp\src\render\bloom_pass.cpp`, `C:\dev\ofg\cpp\src\render\shaders\bloom_prefilter_downsample.wgsl.hpp`, `C:\dev\ofg\cpp\src\render\shaders\bloom_upsample.wgsl.hpp`, and `C:\dev\ofg\cpp\tests\bloom_pass_test.cpp`, and wired them into `C:\dev\ofg\cpp\CMakeLists.txt`. It also extended `ToneMapPass` with `ToneMapBloomInput`, a durable 1x1 fallback bloom texture, and bloom composition before exposure and ACES tone mapping. `C:\dev\ofg\cpp\tests\webgpu_test_utils.hpp` now accepts an optional backend type and exposes finite future waiting for the real-backend readback fixture.

Milestone 2 validation before review:

- `npm run format:cpp`
- `npm run format:cpp:check`
- `npm run test:cpp`
- `npm run build:wasm`
- `git -c safe.directory=C:/dev/ofg diff --check -- cpp\include\ofg\render\bloom_pass.hpp cpp\src\render\bloom_pass.cpp cpp\src\render\shaders\bloom_prefilter_downsample.wgsl.hpp cpp\src\render\shaders\bloom_upsample.wgsl.hpp cpp\include\ofg\render\tone_map_pass.hpp cpp\src\render\tone_map_pass.cpp cpp\src\render\shaders\tone_map.wgsl.hpp cpp\tests\bloom_pass_test.cpp cpp\tests\renderer_targets_test.cpp cpp\tests\renderer_test.cpp cpp\tests\webgpu_test_utils.hpp cpp\tests\webgpu_test_utils.cpp cpp\CMakeLists.txt`

Remaining risk before review: `BloomPass` is not yet integrated into `Renderer`, so no browser/native smoke screenshots show the game scene blooming yet. The real-backend fixture proves the WGSL path for threshold, clamp, tint/intensity, and halo formation against a deterministic HDR texture.

Milestone review:

- Scope: Milestone 2 `BloomPass`, bloom WGSL, `ToneMapPass` bloom input/fallback/composite, deterministic readback tests, CMake, and docs.
- Reviewers: contract, code quality, legacy, correctness, and validation passes were performed locally. Sub-agents were not spawned because the available multi-agent tool requires explicit user-requested delegation.
- Required findings fixed: `ToneMapPass::render(...)` accepted public `ToneMapBloomInput` tint values without checking finite/non-negative components. It now rejects invalid tint, and `renderer_targets_test.cpp` covers a NaN tint.
- Follow-ups recorded: renderer integration, game-scene screenshots, warm-up reuse budgets, and smoke/coverage remain Milestones 3 and 4.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `npm run build:wasm`, and `git -c safe.directory=C:/dev/ofg diff --check -- cpp\include\ofg\render\bloom_pass.hpp cpp\src\render\bloom_pass.cpp cpp\src\render\shaders\bloom_prefilter_downsample.wgsl.hpp cpp\src\render\shaders\bloom_upsample.wgsl.hpp cpp\include\ofg\render\tone_map_pass.hpp cpp\src\render\tone_map_pass.cpp cpp\src\render\shaders\tone_map.wgsl.hpp cpp\tests\bloom_pass_test.cpp cpp\tests\renderer_targets_test.cpp cpp\tests\renderer_test.cpp cpp\tests\webgpu_test_utils.hpp cpp\tests\webgpu_test_utils.cpp cpp\CMakeLists.txt docs\API_CONTRACTS.md docs\SYSTEMS.md docs\plans\bloom-post-effect-plan.md`.
- Remaining risk: `BloomPass` is still exercised through a direct fixture rather than the main `Renderer` frame path. No screenshots were produced because game-scene visual integration is Milestone 4.

Milestone 3 completed the quality and diagnostics layer. `BloomPass` now reports active level count, encoded pass count, draw count, estimated read/write bytes, and skipped state. Renderer and runtime diagnostics expose bloom and temp-buffer state through `RuntimeDebugStatus`, and the TypeScript parser plus smoke tooling validate those fields without adding browser-owned bloom controls. Tests now cover small and skipped pyramids, invalid inputs, cache rotation, one-level and multi-level bloom, real-backend threshold/clamp/tint/intensity behavior, tone-map fallback behavior, and temp-buffer reuse/early-release/stale-cleanup behavior.

Milestone review:

- Scope: Milestone 3 bloom diagnostics, quality coverage, memory behavior, runtime status fields, parser/smoke assertions, and budget checks.
- Reviewers: contract, code quality, legacy, correctness, and validation passes were performed locally. Sub-agents were not spawned because delegation was not explicitly requested in this turn.
- Required findings fixed: diagnostics that originally lived only in renderer internals are now surfaced through `RuntimeDebugStatus` and smoke reports; tests now cover skipped/invalid/tiny/one-level paths and bind-group/cache-sensitive behavior; smoke tooling now checks the 11-pass budget and temp-buffer memory/reuse diagnostics.
- Follow-ups recorded: enabled-vs-disabled CPU command-encoding delta still needs a future renderer profiling hook; the first implementation records pass-count, memory, shader, and smoke evidence instead.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:browser:cpp`, `npm run smoke:render`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: no mutable browser bloom controls exist by design, so browser smoke validates the default enabled path and runtime diagnostics rather than direct enabled/disabled timing.

Milestone 4 integrated bloom into the main renderer frame. `Renderer::render_impl` now ends the HDR scene render pass, begins a `TempBuffer` frame, encodes bloom, tone maps with the bloom input, releases the final bloom buffer, and ends the temp-buffer frame. `Renderer::prepare`, `Renderer::release`, and destruction now create and tear down `BloomPass` and `TempBuffer` alongside the existing scene color, depth, opaque, sky, and tone-map systems. Browser/native smoke artifacts and reports were refreshed, docs/contracts were updated, coverage summaries under `C:\dev\ofg\docs\coverage` were refreshed, and the procedural sky source was tuned so the sun is warmer, bolder, and less clouded by pre-bloom haze.

Final milestone review:

- Scope: Milestones 3 and 4 plus final plan completion: renderer integration, runtime diagnostics, smoke tooling, docs, coverage, screenshots, and sky/sun tuning.
- Reviewers: contract, code quality, legacy, correctness, and validation passes were performed locally. Sub-agents were not spawned because delegation was not explicitly requested in this turn. `C:\dev\ofg\docs\ARCHITECTURE.md` was requested by the review skill but is absent in this checkout.
- Required findings fixed: coverage shortfalls were resolved with targeted tests plus a narrow documented defensive-line exception; runtime diagnostics were added to smoke; native smoke report now includes bloom/temp-buffer `debugStatus`; active contracts/docs now describe the C++ owned bloom and `TempBuffer` paths.
- Follow-ups recorded: no required correctness follow-ups. `cpp/src/native/render_smoke.cpp`, `cpp/src/render/bloom_pass.cpp`, and `cpp/src/render/temp_buffer.cpp` are in the 500-1000 line watch band, so future additions should consider splitting harness/helpers if they grow.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:browser:cpp`, `npm run smoke:render`, `npm run smoke`, `npm run coverage`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: same-camera enabled/disabled browser screenshots were not captured because the in-app browser pointer-lock path prevented reliable sun-facing automation, and the first implementation does not measure native CPU command-encoding delta versus disabled bloom. Browser/default and native smoke artifacts validate the bloom-capable path, and tests cover disabled/zero-intensity behavior through direct C++ fixtures.

## Contract and Quality Baseline

`OFG-BOOT-001 TypeScript Host Ownership` must be preserved. TypeScript may run smoke tooling and display screenshots, but it must not own bloom settings, post-effect scheduling, render targets, GPU resources, or draw submission.

`OFG-BOOT-002 C++ Runtime Ownership` changes by adding C++ renderer ownership of the static `TempBuffer` system and bloom pass. `Game` should remain orchestration and status glue; renderer post-effect internals belong behind `Renderer`, `TempBuffer`, `BloomPass`, and `ToneMapPass`.

`OFG-BOOT-004 Renderer Compatibility` changes because browser and native smoke must validate equivalent bloom behavior once it is enabled. The smoke visual contract should remain tone-map-aware and should add bloom-specific assertions only when deterministic bright pixels are available.

`OFG-BOOT-005 WebGPU Baseline` must be preserved. The first bloom implementation must request no optional GPU features and must not manually request higher adapter limits. It should use standard render attachments, sampled texture bindings, uniform buffers, full-screen render passes, and existing platform target formats.

`OFG-BOOT-006 Resource Lifetime` is central to this plan. Bloom pipelines, shader modules, bind group layouts, uniform buffers, reusable temporary textures, texture views, and size-independent bind groups are durable resources. Ordinary steady-state frames may update uniform buffers and ask `TempBuffer::get(...)` for frame-scoped temporary render targets, but they must not recreate durable pass resources or allocate fresh textures after the reusable set has warmed up. Size changes may create new size-dependent textures and views, and stale old-size temporary textures are discarded after the cleanup window.

`OFG-BOOT-009 Coverage` applies. Each modified implementation file must pass the default coverage attention gate, currently about 90% line coverage, unless this plan records an explicit exception with rationale. Browser-only visual confidence must still be backed by `npm run smoke:browser` and `npm run smoke:render`.

## Context and Orientation

The renderer is C++ owned. `Renderer::render_impl` in `C:\dev\ofg\cpp\src\render\renderer.cpp` resolves the current scene camera into `CameraProperties`, builds a transient `DrawList`, builds one directional `LightProperties` item from `Scene::environment().main_directional_light()`, renders opaque geometry and procedural sky into an `RGBA16Float` scene color target inside one WebGPU render pass, ends that scene pass, then tone maps the completed HDR scene color into the platform target. Bloom must be encoded after that scene pass has ended and before `ToneMapPass::render`, because WebGPU cannot sample `SceneColorTarget` while it is still bound as a render attachment.

The relevant current and planned files are:

- `C:\dev\ofg\cpp\include\ofg\render\scene_color_target.hpp` and `C:\dev\ofg\cpp\src\render\scene_color_target.cpp`: renderer-owned full-resolution HDR color target.
- `C:\dev\ofg\cpp\include\ofg\render\depth_target.hpp` and `C:\dev\ofg\cpp\src\render\depth_target.cpp`: renderer-owned full-resolution depth target.
- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp` and `C:\dev\ofg\cpp\src\render\tone_map_pass.cpp`: final HDR-to-platform pass using exposure, ACES-fitted tone mapping, and correct sRGB output encoding.
- `C:\dev\ofg\cpp\include\ofg\render\renderer_counters.hpp`: cumulative counters used to prove resource lifetime behavior.
- `C:\dev\ofg\docs\archived\procedural-sky-environment-plan.md`: completed plan that added HDR scene color, tone mapping, sky, `Environment`, and `Light`.
- `C:\dev\ofg\docs\plans\cascaded-shadow-maps-plan.md`: active plan that adds shadow map targets and shadow passes before opaque scene rendering.

Terms used in this plan:

Bloom means a post-process approximation of optical glow around very bright image regions. It does not cast light into the scene; it only affects the final image.

HDR scene color means linear color values in `RGBA16Float` before tone mapping. Values may exceed `1.0`.

Prefilter means extracting the part of HDR scene color that contributes to bloom. The first implementation should use a soft threshold so pixels near the threshold transition smoothly.

Bloom pyramid means a chain of reduced-resolution textures. Each level is half the width and height of the previous level, down to a small minimum or configured maximum count. Lower levels represent wider blur radii.

Upsample chain means reconstructing a bloom texture from the smallest pyramid level back toward the first bloom level by filtered upsampling and additive or lerped accumulation.

Temp buffer means a renderer-owned, reusable temporary render target texture plus view that is used only inside a frame or pass sequence. The name follows the requested `TempBuffer::get(...)` API. In this plan, "temp buffer" means a temporary WebGPU texture/view used as a render target or sampled texture; it is not a `WGPUBuffer`, not a scene resource, and not directly exposed to TypeScript.

## Research Summary

NVIDIA GPU Gems, Chapter 21, "Real-Time Glow", describes rendering glow sources at lower resolution, blurring them, and compositing back into the image. It also explains why separable blur is important: a two-dimensional convolution with diameter `d` costs about `d * d` samples per pixel, while separable horizontal plus vertical passes cost about `2 * d`.

Unreal Engine bloom documentation distinguishes standard bloom from convolution bloom. Standard bloom is the game-oriented path and combines blurs of different sizes. Convolution bloom models a kernel more physically and can produce richer lens responses, but Epic documents it as suited for high-end or cinematic use with a performance tradeoff.

Unity URP bloom documentation exposes threshold, intensity, scatter/radius, tint, clamp, high-quality filtering, filter type, downscale, and max iterations. This maps well to a first OFG `BloomSettings` type.

AMD FidelityFX Single Pass Downsampler documentation calls mip/downsample generation a common building block for bloom and other post effects. AMD FidelityFX Blur documents Gaussian blur kernels and compares optimized blur to standard separable blur. OFG should not take a FidelityFX dependency for this first pass, but those references support the target structure.

Useful references:

- NVIDIA GPU Gems, Chapter 21, Real-Time Glow: https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-21-real-time-glow
- Unreal Engine Bloom documentation: https://dev.epicgames.com/documentation/unreal-engine/bloom-in-unreal-engine
- Unity URP Bloom documentation: https://docs.unity3d.com/Manual/urp/post-processing-bloom.html
- AMD FidelityFX Single Pass Downsampler: https://gpuopen.com/fidelityfx-spd/
- AMD FidelityFX Blur sample documentation: https://gpuopen.com/manuals/fidelityfx_sdk/samples/blur/
- SIGGRAPH 2014 Advances course, Call of Duty Advanced Warfare post processing: https://advances.realtimerendering.com/s2014/

## Rendering Algorithm

### Final Pass Order

The current post-sky renderer order is:

    build draw list
    resolve camera
    resolve environment and current sun light
    begin scene render pass
    render opaque PBR into HDR scene color
    render procedural sky into remaining HDR scene color pixels
    end scene render pass
    run BloomPass from HDR scene color into temp bloom targets
    run ToneMapPass, adding optional bloom before exposure and ACES
    write platform target

After the cascaded shadow plan lands, shadow rendering slots before the scene render pass:

    build draw list
    resolve camera
    resolve environment and current sun light
    render shadow maps
    begin scene render pass
    render opaque PBR into HDR scene color while sampling shadows
    render procedural sky into remaining HDR scene color pixels
    end scene render pass
    run BloomPass from HDR scene color into temp bloom targets
    run ToneMapPass, adding optional bloom before exposure and ACES
    write platform target

Bloom must not run before sky, because the procedural sun and sky highlights are expected bloom sources. Bloom must not run after tone mapping, because LDR output has already compressed the high-intensity signal that bloom needs. If bloom implementation starts before shadow maps are complete, use the current post-sky graph and keep the shadow step absent. If a future branch temporarily lacks `SkyPass`, the only acceptable GPU fallback is a deterministic HDR render fixture or the reduced graph `opaque -> end scene pass -> bloom -> tone map`; do not reintroduce fake scene content just to show bloom.

### Bloom Settings

Add `BloomSettings` as a small validated value type:

    enabled = true
    threshold = 1.0
    soft_knee = 0.5
    intensity = 0.08
    scatter = 0.7
    clamp = 64.0
    tint = (1.0, 1.0, 1.0)
    initial_downscale = 2
    max_levels = 6
    min_level_extent = 2

Definitions:

- `enabled`: skips `BloomPass` entirely when false.
- `threshold`: brightness value above which pixels contribute fully to bloom.
- `soft_knee`: width of the smooth transition below the threshold. A value of `0.0` is a hard threshold.
- `intensity`: multiplier applied when bloom is composited into tone mapping.
- `scatter`: controls how much lower, wider pyramid levels contribute during upsampling. Higher values produce wider bloom.
- `clamp`: caps source HDR color for bloom extraction only, preventing one hot pixel from dominating the pyramid. It must not clamp the scene color itself.
- `tint`: linear RGB multiplier for the bloom contribution.
- `initial_downscale`: first bloom level size divisor. Supported values are exactly `2` and `4`. Use `2` for half resolution by default; use `4` only for a cheaper quarter-resolution mode.
- `max_levels`: maximum number of bloom pyramid levels.
- `min_level_extent`: inclusive minimum width and height for every bloom level. The default `2` means `2x2` is valid and `1xN` or `Nx1` is not.

Validation must reject non-finite values, negative threshold, negative knee, negative intensity, `scatter` outside `[0, 1]`, negative clamp, unsupported `initial_downscale`, `max_levels == 0`, `min_level_extent == 0`, and any non-finite or negative tint component. Subtractive bloom is out of scope.

Pyramid sizing uses ceiling division. Level 0 dimensions are:

    level0_width = ceil(frame_width / initial_downscale)
    level0_height = ceil(frame_height / initial_downscale)

If either level 0 dimension is smaller than `min_level_extent`, the plan contains zero levels and `Renderer` skips bloom for that frame. Each later level is `ceil(previous / 2)` in each dimension and is added only when both dimensions are at least `min_level_extent`. Odd dimensions round up so no source edge disappears.

### Prefilter

The prefilter pass reads the full-resolution HDR scene color and writes bloom level 0 at half or quarter resolution. It should use `textureLoad` and manually average source pixels rather than relying on filtering. For `initial_downscale = 2`, each destination pixel covers a clamped `2x2` source footprint. For `initial_downscale = 4`, each destination pixel covers a clamped `4x4` source footprint. The shader applies bloom extraction to each source sample first, then averages the extracted samples; this preserves small bright sources better than averaging before thresholding.

The first implementation should use max RGB component brightness:

    brightness = max(hdr.r, max(hdr.g, hdr.b))

Using max component preserves saturated colored bloom sources better than luminance-only extraction. A future setting can offer luminance extraction if art direction needs it.

Use a soft threshold:

    knee = threshold * soft_knee
    soft = clamp(brightness - threshold + knee, 0, 2 * knee)
    soft = soft * soft / max(4 * knee, epsilon)
    contribution = max(brightness - threshold, soft) / max(brightness, epsilon)
    bloom_source = clamp(hdr, vec3(0), vec3(clamp)) * clamp(contribution, 0, 1)

When `soft_knee == 0`, the helper should avoid division by zero and behave like a hard threshold.

### Downsample Pyramid

After prefiltering, each subsequent downsample level reads the previous level and writes the next smaller temp target.

Use a stable weighted kernel. The first kernel is a 13-tap bloom downsample pattern. For destination pixel `p`, compute `source_center = p * 2` in the previous level, then clamp all source coordinates to the previous level bounds. The offsets are in source texels:

    a = (-2, -2), b = ( 0, -2), c = ( 2, -2)
    d = (-2,  0), e = ( 0,  0), f = ( 2,  0)
    g = (-2,  2), h = ( 0,  2), i = ( 2,  2)
    j = (-1, -1), k = ( 1, -1), l = (-1,  1), m = ( 1,  1)

The weights sum to 1:

    e: 0.125
    a, c, g, i: 0.03125 each
    b, d, f, h: 0.0625 each
    j, k, l, m: 0.125 each

If implementation pressure is high, a 5-tap or 9-tap tent kernel can land first, but that downgrade must be recorded in the Decision Log with visual comparison screenshots.

### Upsample Chain

The upsample chain starts from the smallest bloom level and works back to level 0. Each pass reads the current lower-resolution bloom accumulation and the next higher-resolution downsample level, then writes a temporary higher-resolution accumulation target.

Use a 3x3 tent reconstruction kernel. For each destination pixel in the higher-resolution target, compute the nearest lower-resolution source texel from normalized pixel centers, clamp the 3x3 coordinates to the lower target bounds, and apply:

    1 2 1
    2 4 2
    1 2 1

Divide the weighted sum by 16. Use integer-coordinate tent sampling with `textureLoad`; avoid mandatory filterable float sampling in the first version.

Combine as:

    combined = higher_downsample + upsampled_lower * scatter

Clamp only if needed to avoid half-float overflow. Prefer preserving HDR energy and controlling final strength through `intensity`.

The `BloomPass` returns a small `BloomResult` value containing:

- final bloom texture view;
- final bloom width and height;
- settings used for composite;
- whether the result is valid.

`BloomResult` references the final bloom temp buffer view and dimensions. It must remain valid until after `ToneMapPass::render` has encoded the final draw that samples its view, then the renderer can call `TempBuffer::release(result.buffer)` or leave it for `TempBuffer::end_frame()`. Earlier bloom scratch buffers should be released as soon as the pass has encoded their final read or write. The final bloom result is level 0 size, not full platform size. `ToneMapPass` samples it with one nearest/box `textureLoad` per output pixel:

    bloom_pixel.x = clamp((output_pixel.x * bloom_width) / output_width, 0, bloom_width - 1)
    bloom_pixel.y = clamp((output_pixel.y * bloom_height) / output_height, 0, bloom_height - 1)

This keeps the tone-map composite to one scene-color load plus one bloom load per pixel. If nearest/box sampling produces visible blockiness, record evidence and add a dedicated full-resolution final upsample pass as a later quality option rather than quietly adding four bloom loads per tone-map pixel.

### Tone Map Composite

Extend `ToneMapPass` so it can composite optional bloom before exposure and ACES:

    hdr = textureLoad(scene_color, pixel, 0).rgb
    if bloom_enabled:
        bloom = sample_bloom_result_for_output_pixel(pixel)
        hdr += bloom * bloom_intensity * bloom_tint
    exposed = hdr * exposure
    mapped = aces_fitted(exposed)
    output = encode_for_platform(mapped)

`ToneMapPass` should use one pipeline shape that always has a bloom binding. When bloom is disabled, when the pyramid has no levels, or when intensity is zero, it binds a durable 1x1 black fallback texture and sets bloom intensity to zero. The fallback texture belongs to `ToneMapPass` or `Renderer`, is created once during prepare, and keeps disabled bloom on the same shader path while preserving the current output.

### Temp Buffer System

Add `TempBuffer` as a static lifecycle facade backed by one private singleton, following the same broad pattern as `Game`, `Resources`, and `Renderer`. It is not a resource manager for game assets. It only owns temporary WebGPU textures/views used by renderer passes.

The public interface should stay clean:

    class TempBuffer {
    public:
        static void create(GpuContext gpu);
        static void begin_frame();
        static TempBufferRef get(const TempBufferDesc& desc, std::string_view debug_label);
        static void release(TempBufferRef& buffer) noexcept;
        static void end_frame();
        static bool release();
        static void destroy() noexcept;
        static RendererCounters counters() noexcept;
        static TempBufferStats stats() noexcept;
    };

The normal pass-facing API is `TempBuffer::get(...)`: ask for a temporary buffer by descriptor and get a buffer reference back. If a pass knows no later encoded command will read or write that buffer, it may call `TempBuffer::release(buffer)` to return it early. `TempBuffer::end_frame()` returns every remaining buffer handed out during that frame. The no-argument `TempBuffer::release()` remains the lifecycle teardown function matching `Game::release()`, `Resources::release()`, and `Renderer::release()`.

`TempBufferDesc` should include:

    width
    height
    format
    usage
    mip_level_count = 1
    array_layer_count = 1
    sample_count = 1

The first bloom temp buffers should use:

    format = WGPUTextureFormat_RGBA16Float
    usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
    mip_level_count = 1
    array_layer_count = 1
    sample_count = 1

`TempBufferRef` is a small value handle that is valid until it is passed to `TempBuffer::release(buffer)` or until the current frame's `TempBuffer::end_frame()`:

    struct TempBufferRef {
        bool valid() const noexcept;
        RenderTarget render_target() const;
        WGPUTextureView view() const noexcept;
        std::uint32_t width() const noexcept;
        std::uint32_t height() const noexcept;
        WGPUTextureFormat format() const noexcept;
    };

Rules:

- `TempBuffer::create(gpu)` runs during renderer boot/preparation after a valid WebGPU device and queue exist.
- `Renderer` calls `TempBuffer::begin_frame()` once before any renderer pass asks for a temp buffer.
- `TempBuffer::get(desc, label)` returns an exact descriptor match only when a buffer is available for reuse.
- A buffer handed out in frame `N` is active until a pass calls `TempBuffer::release(buffer)` or until `TempBuffer::end_frame()` for frame `N`.
- `TempBuffer::release(buffer)` marks that value handle invalid and makes the underlying temp buffer available for later `TempBuffer::get(...)` calls with a matching descriptor.
- Early return is a pass-order promise: callers may release only after encoding the final GPU use of that buffer in the current command stream. A caller must not release a buffer that a later pass will still sample, such as the final bloom result before tone mapping has been encoded.
- WebGPU command ordering and implicit resource synchronization make this safe for existing texture handles. Do not destroy and recreate the backing texture for early return.
- At `end_frame`, every still-active temp buffer is returned to the reusable set. `end_frame` should not destroy buffers that were active in the just-recorded frame.
- If no reusable matching buffer exists, `TempBuffer::get` creates a new texture/view.
- The same temp buffer must never be handed out twice while it is active. After `TempBuffer::release(buffer)` invalidates the handle, the underlying texture may be handed out again later in the same frame if the descriptor matches.
- Passes should assume every `TempBufferRef` is frame-scoped unless they release it earlier. They must not store it in scene resources, component state, material state, or any object that can outlive the current render.
- If a reusable temp buffer has not been handed out for ten frames since its last use, `TempBuffer` discards it during `begin_frame` cleanup. The exact threshold should be a named constant, initially `10`. Cleanup should run at a frame boundary after the previous command buffer has been submitted, and it must never discard active buffers.
- The no-argument lifecycle `release()` releases all active and reusable WebGPU texture/view handles and leaves the singleton ready for `destroy()`.
- Zero-size descriptors must be rejected with clear `EngineError`s. Validation must also reject `WGPUTextureFormat_Undefined`, zero `mip_level_count`, zero `array_layer_count`, zero `sample_count`, unsupported usage combinations, and non-2D targets. Zero-size platform resize remains a frame-driver/resize condition; `Renderer::render` keeps rejecting zero-size render targets before bloom asks for temp buffers.
- `TempBuffer` tracks active bytes, reusable bytes, peak bytes, created count, reused count, discarded count, active count, reusable count, early-release count, and end-frame-return count.

Do not implement aliasing inside one texture or memory heap in the first version. WebGPU does not expose explicit memory aliasing in the same style as lower-level APIs, and descriptor-level texture reuse through explicit pass ordering is enough for OFG's first post effects.

### Debug and Observability

Renderer counters should include temp-buffer-created textures/views and bloom-created shader modules, pipelines, bind group layouts, bind groups, buffers, and fallback textures. Prefer the existing generic `RendererCounters` fields unless aggregate counters prove insufficient. Tests must avoid assuming exact global counts unless the plan records the count.

Bloom diagnostics should report, at least in native smoke reports and test-only inspection:

- active bloom level count;
- encoded bloom render-pass count;
- full-screen bloom draw count;
- estimated texture read and write bytes;
- temp-buffer active, reusable, and peak bytes;
- temp-buffer created, reused, discarded, active, reusable, early-release, and end-frame-return counts;
- whether the 1x1 fallback bloom texture was used.

Resource categories:

    Permanent pass resources: BloomPass shaders, pipelines, bind group layouts, uniform buffers, samplers if any, and ToneMapPass fallback bloom texture.
    Per-view resources: bind groups that capture scene-color, bloom-result, or intermediate texture views.
    Temp resources: temporary textures and views keyed by exact descriptor, active/reusable state, and last-used frame.
    Per-frame data: BloomSettings packing, active pyramid plan, diagnostics counters, and command encoding.

If useful for smoke and screenshots, add a native-only or test-only bloom debug output mode that writes:

- prefilter level 0;
- smallest pyramid level;
- final bloom accumulation;
- final tone-mapped output.

This debug mode should not expose renderer internals to TypeScript as mutable state.

## Plan of Work

Milestone 0 introduces the static `TempBuffer` system without changing visuals.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\temp_buffer.hpp`
- `C:\dev\ofg\cpp\src\render\temp_buffer.cpp`
- `C:\dev\ofg\cpp\tests\temp_buffer_test.cpp`

Update:

- `C:\dev\ofg\cpp\CMakeLists.txt`

`TempBuffer` should store target objects with exact descriptors, a live texture, a live view, byte size, last-used frame index, last-returned frame index, and a state such as active or reusable. It should create texture/view handles with labels that include the caller label plus dimensions where possible. It should preserve counters across the no-argument lifecycle `release()` in the same style as other static renderer facades until `destroy()`, and it should expose stats for tests and smoke diagnostics. Tests should prove lifecycle errors, descriptor validation, exact-descriptor reuse after early return, exact-descriptor reuse after automatic `end_frame` return, different-descriptor allocation, same-frame uniqueness while a buffer is active, invalidation after early return, double-return safety, ten-frame stale discard, lifecycle release/destroy behavior, and stats/counters.

Milestone 1 adds CPU-side bloom settings, pyramid sizing, and uniform packing.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\bloom_settings.hpp`
- `C:\dev\ofg\cpp\src\render\bloom_settings.cpp`
- `C:\dev\ofg\cpp\tests\bloom_settings_test.cpp`

Update:

- `C:\dev\ofg\cpp\CMakeLists.txt`

Define `BloomSettings`, `BloomPyramidLevel`, `BloomPyramidPlan`, and helper functions such as:

    BloomSettings default_bloom_settings() noexcept;
    void validate_bloom_settings(const BloomSettings& settings);
    BloomPyramidPlan build_bloom_pyramid_plan(std::uint32_t width, std::uint32_t height, const BloomSettings& settings);
    float bloom_prefilter_contribution(float brightness, float threshold, float soft_knee);
    BloomUniformBlock pack_bloom_uniforms(const BloomSettings& settings);

Use fixed-size arrays for pyramid levels and bloom result bookkeeping. `max_levels` is tiny, so per-frame `std::vector` heap churn is unnecessary. Define a packed CPU/WGSL uniform contract with explicit byte size, 16-byte row alignment, field order, and static assertions where possible. Tests should cover default values, invalid values, hard threshold behavior, soft-knee behavior, non-negative HDR clamping, odd viewport dimensions, tiny viewports that produce zero levels, max-level capping, half and quarter initial downscale, deterministic pyramid dimensions, supported downscale rejection, scatter rejection, tint validation, uniform packing, and CPU/WGSL layout drift.

Milestone 2 implements a minimal end-to-end bloom vertical slice.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp`
- `C:\dev\ofg\cpp\src\render\bloom_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\bloom_prefilter_downsample.wgsl.hpp`
- `C:\dev\ofg\cpp\src\render\shaders\bloom_upsample.wgsl.hpp`

Update:

- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp`
- `C:\dev\ofg\cpp\src\render\tone_map_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\tone_map.wgsl.hpp`
- `C:\dev\ofg\cpp\CMakeLists.txt`

`BloomPass::create` should create durable shader modules, pipeline layouts, bind group layouts, render pipelines, uniform buffers or preallocated uniform slots, and any size-independent state. `BloomPass::render` should take a command encoder, source HDR scene-color view, source width/height, and `BloomSettings`. It should build the pyramid plan, ask `TempBuffer::get(...)` for every temporary target it needs, render prefilter into level 0, render downsample levels 1..N, upsample and accumulate back to level 0, and return a small `BloomResult` that references the final temp buffer view for the rest of the current frame.

The minimal vertical slice may start with a small level count such as two or three levels, but it must include prefilter, at least one downsample when dimensions allow, one upsample/composite path, and tone-map composition. Do not add throwaway debug APIs solely because downsample landed before upsample. If per-pass uniform values are required, write them into distinct slots before encoding or bind distinct buffers; do not overwrite the same uniform range between passes encoded into one command buffer. `ToneMapPass` should always bind a bloom texture, using the durable black fallback when the bloom result is invalid.

Add GPU validation that actually creates the bloom shader modules and pipelines. `npm run build:wasm` is not enough because it only compiles the shader strings into C++. Use a native Dawn test path, a focused browser fixture, or a smoke-render fixture that exercises `BloomPass::create` and a tiny deterministic HDR input through at least one render. The validation should read back or compare output enough to prove threshold, clamp, intensity/tint, and halo formation through WGSL, not just counters.

Milestone 3 refines quality, diagnostics, memory behavior, and performance budgets.

Update:

- `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp`
- `C:\dev\ofg\cpp\src\render\bloom_pass.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp`
- `C:\dev\ofg\cpp\src\render\tone_map_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\tone_map.wgsl.hpp`
- `C:\dev\ofg\cpp\CMakeLists.txt` if additional tests or fixtures are added.

Finish the configured six-level default when dimensions allow, implement the exact 13-tap downsample and 3x3 upsample kernels, add per-frame diagnostics, and validate temp-buffer cleanup. Tests should prove disabled bloom matches the old tone-map path, intensity zero matches disabled output, settings update through uniforms without recreating pipelines, bind groups are recreated only when input views change, repeated same-size frames stop creating new temp buffers after the reusable set has warmed up, early-returned bloom scratch buffers can be reused later in the same encoded frame when safe, and old-size temp buffers are discarded after the stale cleanup window.

Add a non-TypeScript way to exercise enabled/disabled/zero-intensity bloom for tests and screenshots. Acceptable choices include a native smoke command option, a renderer-internal test hook compiled only for tests, or a deterministic C++ render fixture. Do not expose mutable bloom settings through the browser TypeScript facade in this plan.

Add performance diagnostics and provisional budgets. At the default 1080p half-resolution, six-level bloom may encode at most 11 bloom render passes: one prefilter, five downsample passes, and five upsample passes. Because `TempBuffer` can reuse returned texture handles within the same ordered frame, the steady warm-up budget should cover one reusable bloom working set plus any targets whose final result must stay alive until tone mapping is encoded. The default path should target no more than 16 MiB of temp bloom color textures at 1080p after warm-up, no more than 64 MiB at 4K after stale cleanup, and a CPU command-encoding delta under 2 ms in native smoke on the configured Dawn machine. Browser smoke should record enabled/disabled frame timing or frame-count deltas when available; if that evidence exceeds a 4 ms frame-time delta, reduce default cost before completion. If these budgets are unrealistic on the available hardware, record measured values and an explicit decision to lower `max_levels`, switch to `initial_downscale = 4`, or temporarily land a cheaper kernel.

Milestone 4 integrates bloom into `Renderer`, docs, smoke, screenshots, and coverage.

Update:

- `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp`
- `C:\dev\ofg\cpp\src\render\renderer.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\renderer_counters.hpp` if new counter fields are needed
- `C:\dev\ofg\cpp\src\render\renderer_counters.cpp` if new counter fields are needed
- `C:\dev\ofg\cpp\CMakeLists.txt`
- `C:\dev\ofg\docs\API_CONTRACTS.md`
- `C:\dev\ofg\docs\SYSTEMS.md`
- `C:\dev\ofg\tools\smoke-contract.json` and smoke tooling only if visual thresholds need bloom-aware changes

`Renderer` should create, begin, end, release, and destroy `TempBuffer` alongside its other renderer systems. It should own `BloomPass` and `BloomSettings`. The first public interface can keep settings internal and defaulted. Do not expose bloom controls to TypeScript. `Renderer::render_impl` should preserve its current nonzero target validation. Bloom should be skipped when settings are disabled, when the pyramid plan has zero levels, or when no valid scene color view exists. Renderer counters should prove that repeated ordinary frames do not create additional bloom temp buffers after the reusable set has been allocated.

Because the sky pass is now complete, visual validation should use the procedural sun and at least one deterministic environment preset. If the active branch temporarily lacks sky during implementation, use the native deterministic HDR fixture and record that limitation in Surprises & Discoveries; do not add fake game content solely to make bloom visible in the final demo. Preserve the current `Renderer::render` nonzero target validation; zero-size resize/no-frame handling happens before render and before bloom acquisition.

## Concrete Steps

Run from `C:\dev\ofg`.

After Milestone 0:

    npm run test:cpp

Expected result: doctests pass for temp-buffer descriptor validation, `TempBuffer::get(...)`, early return, automatic frame-end return, reuse behavior, lifecycle release behavior, stale cleanup, and counters.

After Milestone 1:

    npm run test:cpp

Expected result: doctests pass for bloom settings validation, threshold helpers, and pyramid planning.

After Milestone 2:

    npm run test:cpp
    npm run build:wasm

Expected result: C++ tests pass, the WASM build succeeds, and a native Dawn or browser fixture actually creates bloom pipelines and renders a deterministic tiny HDR bloom case. The output evidence proves the shader path runs; building WGSL string headers alone is not enough.

After Milestone 3:

    npm run test:cpp
    npm run build

Expected result: C++ tests pass, TypeScript app build succeeds, `ToneMapPass` works with bloom disabled and enabled, diagnostics report active levels/pass counts/estimated bytes, and repeated same-size renders keep renderer/temp-buffer creation counters stable after warm-up.

For visual verification during Milestone 4, keep a dev server available:

    npm run dev

Expected result: the command prints a local URL, normally `http://127.0.0.1:5173`, or the next available port. Report the URL in chat when started or restarted.

Run browser and native smoke:

    npm run smoke:browser
    npm run smoke:render

Expected result: browser and native smoke pass and write screenshots/reports under `C:\dev\ofg\artifacts\browser-smoke` and `C:\dev\ofg\artifacts\render-smoke`. The screenshots should show a controlled glow around the procedural sun or deterministic bright source without obvious full-screen haze.

Performance expectation: at 1080p with default settings, smoke diagnostics should report no more than 11 bloom render passes and no more than 16 MiB of temp bloom color textures after warm-up. At 4K, after stale cleanup, temp bloom color textures should stay under 64 MiB. The original target also asked for less than 2 ms native CPU command-encoding delta versus disabled bloom and about 4 ms browser frame-time delta if available; the first implementation records pass-count and memory budgets instead because bloom settings are intentionally not exposed through a runtime toggle yet. A future renderer profiling hook should add that timing evidence without moving ownership into TypeScript.

Run final validation:

    npm run format:cpp
    npm run format:cpp:check
    npm test
    npm run smoke
    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check

Expected result: formatting, C++ tests, TypeScript tests, browser smoke, native render smoke, and coverage pass. Modified implementation files do not appear in the default coverage attention output unless this plan records a justified exception. Refresh `C:\dev\ofg\docs\coverage` after the final coverage run according to `C:\dev\ofg\COVERAGE.md`.

## Milestone Review

After each implementation milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking that milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Functional acceptance:

- Bloom is a post effect that reads completed HDR scene color after opaque, shadow, and sky rendering.
- Bloom samples `SceneColorTarget` only after the scene-color render pass has ended.
- Bloom runs before tone mapping and is composited before exposure and ACES output mapping.
- Bloom can be disabled, and disabled or zero-intensity bloom preserves the existing tone-mapped visual path.
- Bright HDR pixels above the configured threshold produce a soft halo whose strength changes with `intensity`.
- `scatter` changes the apparent bloom radius without changing the original scene lighting.
- Bloom extraction uses a soft threshold and a clamp that affect only the bloom source, not the original scene color.
- Bloom temporary targets are obtained from `TempBuffer::get(...)`, not owned as ad hoc one-off textures by every pass.
- Temporary targets are returned either by explicit `TempBuffer::release(buffer)` after their final encoded use or automatically by `TempBuffer::end_frame()`.
- Renderer counters prove steady-state frames do not recreate bloom pipelines, shader modules, bind group layouts, uniform buffers, or intermediate textures after warm-up.
- Resize creates or reuses only size-dependent targets; repeated same-size frames reuse existing targets.
- Stale cleanup prevents unused old-size temp textures from accumulating indefinitely.
- Bloom diagnostics report active levels, pass count, estimated texture traffic, and temp-buffer memory high-water data.
- Browser and native smoke both render through the same bloom-capable C++ path.
- The first implementation requests no optional WebGPU features and no custom adapter limits.

Test acceptance:

- `npm run test:cpp` passes after each C++ milestone.
- `npm run test:ts` passes if TypeScript smoke/debug code changes.
- `npm test` passes before completion.
- `npm run build` passes before browser visual validation.
- `npm run smoke:browser` passes and stores a screenshot showing the bloom-capable scene.
- `npm run smoke:render` passes and writes a PNG/report showing the bloom-capable scene.
- A deterministic GPU bloom fixture or smoke mode proves threshold, clamp, tint/intensity, scatter, and halo formation through actual WGSL passes.
- `npm run coverage` passes, with changed implementation files above the documented threshold or explicit exceptions recorded here.
- `git -c safe.directory=C:/dev/ofg diff --check` reports no whitespace errors.

Screenshot acceptance:

- Capture and present screenshots after first visible bloom around a deterministic bright source.
- Capture and present screenshots with bloom disabled and enabled from the same camera when practical.
- Capture and present screenshots after final tuning, including browser and native smoke artifacts.
- Store durable comparison screenshots under `C:\dev\ofg\artifacts\bloom\` or the relevant smoke artifact directory.
- In chat, include the artifact path whenever a screenshot is stored in the repo.

Quality acceptance:

- New functions and files follow the project comment/readability requirements. Each new file begins with a maintained purpose comment. Each new function has a purpose comment or doc comment, and functions over 50 lines include internal comments that explain their major stages.
- New C++ uses the repo naming conventions: classes and structs in `CamelCase`, functions in `lowercase_with_underscores`, members as `m_name_with_underscores`, locals as `name_with_underscores`, static variables beginning with `_`, and globals beginning with `g_`.
- New C++ files are formatted with `npm run format:cpp`.

## Idempotence and Recovery

The work should be additive and recoverable. `Renderer::release` must release `BloomPass`, `TempBuffer`, tone-map bloom fallback resources, and any reusable temp textures even if preparation failed after only some resources were created. Repeated `Renderer::prepare` after ready must not create duplicate bloom resources. Repeated render at the same target size must reuse the same temp textures after warm-up. Zero-size resize must not attempt bloom target allocation, and `Renderer::render` must continue rejecting zero-size render targets before bloom code runs.

If `TempBuffer` exposes lifetime bugs, keep it unused by `Renderer` until `get`, early-return, frame-end-return, and stale-cleanup tests pass. Bloom should not land with ad hoc per-pass scratch textures as a workaround unless this ExecPlan is explicitly revised.

If bloom shaders fail validation in browser but native tests pass, disable bloom by default through `BloomSettings` while retaining temp-buffer and settings tests, then isolate the WGSL incompatibility. Do not mark the visual milestone complete until browser and native smoke both pass with the intended shader path.

If bloom output is too strong or hazy, first identify whether the haze is coming from bloom or from the HDR source. Tune `intensity`, `threshold`, and `scatter` when bloom is over-amplifying otherwise clean sources. Tune sky-side sun halo/disc parameters when the pre-bloom procedural sun already reads as a clouded-over wash. The scene lighting should remain physically meaningful; bloom should adapt to it rather than hiding source-content issues.

If the pyramid implementation proves too expensive, first reduce `max_levels` or use `initial_downscale = 4`. If still too expensive, record a decision to switch the first version to a single lower-resolution separable blur and include visual/performance evidence. The trigger for this decision is missing the default budget by more than about 25% after obvious implementation fixes, or browser smoke showing an enabled-bloom frame-time delta above about 4 ms on the review machine.

## Artifacts and Notes

Current local visual artifacts:

- `C:\dev\ofg\artifacts\bloom\bloom-visible-5178.png`: first browser capture after renderer integration and bloom-default retuning.
- `C:\dev\ofg\artifacts\bloom\sky-tuned-default-5178.png`: fresh browser capture after reducing the procedural sky's sky-side sun haze and warming the sun color. Pointer-lock automation was unavailable in the in-app browser, so this artifact verifies the rebuilt scene path rather than a sun-centered camera angle.
- `C:\dev\ofg\artifacts\bloom\sky-sun-bolder-5178.png`: fresh browser capture after making the procedural sun disc/core larger and stronger while preserving the approved bloom defaults.
- `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\browser-smoke\report.json`: browser smoke artifacts after final bloom integration; the report records six active bloom levels, eleven bloom passes, temp-buffer reuse, and stale old-size discard after resize/warm-up.
- `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png` and `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`: focused browser C++ smoke artifacts proving WebGPU startup and bloom-capable runtime diagnostics through the browser fixture.
- `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`: native smoke artifacts after the sky/sun retune and smoke-background reference update; the report records `passed: true`, six active bloom levels, eleven bloom passes, and about 1.83 MiB peak temp-buffer memory.
- `C:\dev\ofg\docs\coverage\latest.md`, `C:\dev\ofg\docs\coverage\cpp-summary.json`, and `C:\dev\ofg\docs\coverage\ts-coverage-summary.json`: committed coverage summaries refreshed after the final `npm run coverage` pass.

Current intended pass graph after sky and before shadows:

    OpaquePass + SkyPass -> SceneColorTarget RGBA16Float
    end scene render pass
    BloomPass -> TempBuffer RGBA16Float pyramid targets
    ToneMapPass(scene_color, optional bloom) -> platform target

Future intended pass graph after shadows:

    ShadowCasterPass -> ShadowMapTarget
    OpaquePass + SkyPass -> SceneColorTarget RGBA16Float
    end scene render pass
    BloomPass -> TempBuffer RGBA16Float pyramid targets
    ToneMapPass(scene_color, optional bloom) -> platform target

Approximate downsample memory estimate for a 1920x1080 viewport with half-resolution bloom level 0 and six `RGBA16Float` levels:

    Level 0: 960 x 540 x 8 bytes  ~= 3.96 MiB
    Level 1: 480 x 270 x 8 bytes  ~= 0.99 MiB
    Level 2: 240 x 135 x 8 bytes  ~= 0.25 MiB
    Level 3: 120 x 68  x 8 bytes  ~= 0.06 MiB
    Level 4: 60  x 34  x 8 bytes  ~= 0.02 MiB
    Level 5: 30  x 17  x 8 bytes  ~= 0.004 MiB

The downsample chain therefore costs about 5.3 MiB for one set of levels at 1080p. The upsample path may temporarily need additional accumulation targets if a higher level cannot be overwritten safely while it is still needed as an input. The temp-buffer memory budget is therefore not exactly the downsample sum; the first default budget is 16 MiB peak temp bloom color textures at 1080p and 64 MiB at 4K after stale cleanup. Implementation must report measured active, reusable, and peak bytes so these estimates can be corrected with evidence.

## Interfaces and Dependencies

Expected new or changed public interfaces by the end:

- `C:\dev\ofg\cpp\include\ofg\render\temp_buffer.hpp`
  - `struct TempBufferDesc`
  - `struct TempBufferRef`
  - `struct TempBufferStats`
  - `class TempBuffer`
  - Descriptor validation helper.
  - `TempBuffer::create(...)`, `begin_frame()`, `get(...)`, `release(TempBufferRef&)`, `end_frame()`, no-argument lifecycle `release()`, `destroy()`, `counters()`, and `stats()`.
  - `TempBufferRef` is a small value handle invalidated by explicit release or frame end.

- `C:\dev\ofg\cpp\include\ofg\render\bloom_settings.hpp`
  - `struct BloomSettings`
  - `struct BloomPyramidLevel`
  - `struct BloomPyramidPlan`
  - `struct BloomUniformBlock`
  - `default_bloom_settings()`
  - `validate_bloom_settings(...)`
  - `build_bloom_pyramid_plan(...)`
  - `bloom_prefilter_contribution(...)`
  - `pack_bloom_uniforms(...)`

- `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp`
  - `class BloomPass`
  - `struct BloomResult`
  - `struct BloomPassDiagnostics`
  - `static std::unique_ptr<BloomPass> BloomPass::create(GpuContext gpu, WGPUTextureFormat bloom_format)`
  - `BloomResult BloomPass::render(WGPUCommandEncoder encoder, WGPUTextureView scene_color_view, std::uint32_t width, std::uint32_t height, const BloomSettings& settings)`
  - `RendererCounters BloomPass::counters() const noexcept`
  - `BloomPassDiagnostics BloomPass::diagnostics() const noexcept` for active levels, encoded pass count, draw count, estimated bytes, and skipped state.

- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp`
  - Add a compact `ToneMapBloomInput` carrying a texture view, width, height, intensity, and tint.
  - Extend `ToneMapPass::render(...)` to accept `ToneMapBloomInput`.
  - Own or receive a durable 1x1 black fallback bloom texture so the tone-map shader always has a bloom binding.
  - Counters must include any new bloom bind groups or fallback texture resources.

- `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp`
  - Renderer initializes and advances the static `TempBuffer` lifecycle.
  - Renderer owns `std::unique_ptr<BloomPass>`.
  - Renderer owns `BloomSettings`.
  - Renderer counters aggregate temp-buffer and bloom pass counters.
  - `Renderer::bloom_diagnostics()` and `Renderer::temp_buffer_stats()` expose read-only renderer diagnostics for tests, runtime status, and smoke reports.

- `C:\dev\ofg\cpp\include\ofg\runtime\runtime_debug_status.hpp`
  - Adds read-only bloom and temp-buffer diagnostic fields to the existing runtime status snapshot.

- `C:\dev\ofg\cpp\include\ofg\game\game.hpp`
  - `Game` copies renderer bloom and temp-buffer diagnostics into `RuntimeDebugStatus` after each render.

- `C:\dev\ofg\src\app\wasmRuntime.ts`
  - Parses the new status fields for smoke/debug reporting without adding mutable bloom controls.

- `C:\dev\ofg\tools\browser-smoke.mjs`, `C:\dev\ofg\tools\browser-smoke-cpp.mjs`, and `C:\dev\ofg\cpp\src\native\render_smoke.cpp`
  - Validate and report the bloom/temp-buffer diagnostic fields in browser and native smoke.

- `C:\dev\ofg\cpp\src\render\shaders\bloom_prefilter_downsample.wgsl.hpp`
  - WGSL source for prefilter and downsample full-screen passes.

- `C:\dev\ofg\cpp\src\render\shaders\bloom_upsample.wgsl.hpp`
  - WGSL source for upsample/accumulate full-screen passes.

- `C:\dev\ofg\cpp\src\render\shaders\tone_map.wgsl.hpp`
  - Updated WGSL source that composites optional bloom before exposure and ACES tone mapping.

- `C:\dev\ofg\cpp\CMakeLists.txt`
  - Add new source and doctest files.

The first implementation must not add a third-party engine, renderer, or runtime dependency.
