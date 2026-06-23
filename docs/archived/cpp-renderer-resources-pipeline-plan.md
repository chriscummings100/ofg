# Build C++ mutable render resources and an opaque draw-list renderer

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

Replace the current bootstrap triangle renderer with the first reusable C++ rendering slice for OFG. After this work, `npm run dev`, `npm run smoke:browser`, and `npm run smoke:render` should show and verify a simple 3D scene: a large ground plane plus several cubes at different depths, with browser frames animating cube rotation and vertical motion over time. The scene should be submitted through a draw list rather than through hard-coded bootstrap draw calls.

The renderer should use ordinary C++ resource ownership. C++ owns mutable Texture, Shader, Material, and Mesh objects in stable lifetime containers owned by `Game` or a demo/resource bundle. Materials, meshes, and draw commands may refer to those resources through non-owning pointers or references whose owner outlives the draw list. These resource types are high-level game/render assets, not thin wrappers around every WebGPU handle; renderer internals may use raw `WGPUTextureView`, `WGPUBindGroup`, `WGPURenderPipeline`, and similar WebGPU handles directly when that is clearer.

The first resource types are:

Texture: pixel data created from generated or caller-provided RGBA8 pixels, with linear or sRGB formats and explicit mip-map policy. Textures can update their pixels after creation and keep their WebGPU texture, view, and sampler in sync for the single active device. Mip maps are not optional for the completed plan: the implementation may start with level-zero texture data, but by plan completion `GenerateCpuFullChain` must create and upload the full mip chain. General image decoding can wait for a later asset-loading plan unless it stays small, pinned, and fully covered in this plan.

Shader: WGSL source plus explicit parameter and pipeline schemas. Shaders own their shader module for the active device and can be replaced during development.

Material: a shader reference plus a property bag containing named shader parameters, including texture pointers. Materials own material uniform and bind-group state when GPU-ready.

Mesh: vertices, indices, and submesh ranges, where each submesh has a default material pointer. Meshes can update vertices or indices after creation and keep their vertex and index buffers in sync.

PropertyBag: a shared named-value structure used by materials and draw commands for non-hot shader parameters. A draw command has an explicit `model` matrix field so transform upload does not depend on a per-draw named lookup, while the property bag leaves room for object id, instance tint, animation phase, or later render parameters.

The renderer also needs a minimal C++ math slice before the first 3D scene can be built. This should look familiar to anyone writing WGSL or GLSL without becoming a broad engine math package: vectors use shader-style components such as `x`, `y`, `z`, and `w`; matrices pack in the column-major order expected by WGSL `mat4x4<f32>` uniforms; and helpers such as `vec3(...)`, `mat4_identity()`, `mul(...)`, `normalize(...)`, `perspective_rh(...)`, and `look_at_rh(...)` keep demo-scene code close to shader-language notation.

This is not intended to become a full resource database yet. The goal is a concrete, mutable, tested resource interface that feels like a game engine: resources have explicit owners, callers pass references or pointers, and the renderer consumes a draw list of already-resolved mesh/material references plus property bags.

## Progress

- [x] (2026-06-20 18:05Z) Re-read `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `README.md`, `AGENTS.md`, `cpp/CMakeLists.txt`, `package.json`, and the archived pre-migration renderer/resource plan.
- [x] (2026-06-20 18:05Z) Confirmed the active runtime is C++/WASM with TypeScript as a narrow host, browser WebGPU through Emdawnwebgpu, native offscreen rendering through pinned Dawn, C++ tests through doctest/CTest, and coverage through Clang/LLVM.
- [x] (2026-06-20 18:05Z) Drafted this C++-first replacement plan at `C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md`.
- [x] (2026-06-20 18:15Z) Reviewed this plan through local correctness, completeness, clarity, efficiency, performance, contract, code-quality, legacy, and validation passes during Milestone 6 of the C++/WASM migration.
- [x] (2026-06-21 21:56Z) Aligned the future renderer entry-point sketch with the shared `Game::render` boundary; resource creation or GPU preparation receives `GpuContext`.
- [x] (2026-06-22 19:35Z) Refined this plan before implementation to add a minimal shader-language-like C++ math layer for vectors, matrices, camera transforms, and WGSL uniform packing.
- [x] (2026-06-22 19:38Z) Applied pre-implementation review updates: high-level resources are not WebGPU wrappers, resources may store their creator `GpuContext`, mips are required before completion, draw commands carry explicit model matrices, and the renderer shape now leaves room for future passes.
- [x] (2026-06-22 19:47Z) Replaced the typed-handle/store resource model with explicit C++ ownership through stable resource containers and non-owning resource pointers/references.
- [x] (2026-06-22 20:50Z) Implemented Milestone 1 CPU-side `ofg::math` vectors/matrices/transforms and resource types: `ResourceArena`, `Texture`, `Shader`, `Material`, `Mesh`, `SubMesh`, `PropertyBag`, `PropertyValue`, and typed resource-error helpers.
- [x] (2026-06-22 20:50Z) Added focused doctest coverage for math behavior, deterministic CPU mip generation, stable direct resource ownership, non-owning pointer/reference assumptions, validation failures, revision changes, move-only resource semantics, and uniform packing.
- [x] (2026-06-22 20:50Z) Expanded `npm run coverage:cpp` so `cpp/src/math` and `cpp/src/resources` are checked by the 90% line-coverage gate.
- [x] (2026-06-22 21:44Z) Completed Milestone 1 local milestone review. Required finding fixed: odd-sized texture mip chains now use ceil-halving and have regression coverage.
- [x] (2026-06-22 22:46Z) Implemented Milestone 2 single-device WebGPU resource state: textures create/upload mipmapped textures, views, and samplers; shaders create/replace modules; materials create uniform buffers, bind group layouts, and bind groups; meshes create/replace vertex and index buffers.
- [x] (2026-06-22 22:46Z) Added Dawn-null-backed resource GPU doctests plus a test-only `TestGpuContext` fixture for native resource lifecycle coverage.
- [x] (2026-06-22 22:46Z) Completed Milestone 2 local milestone review. Required findings fixed: stale Milestone 1 header comments were updated to current GPU-ready behavior and an unused test include was removed.
- [x] (2026-06-23 01:45Z) Implemented Milestone 3 reusable render interfaces: `DrawList`, `DrawCommand`, `RenderView`, `PipelineCache`, `OpaquePass`, `Renderer`, `RendererCounters`, and the first opaque WGSL shader path.
- [x] (2026-06-23 01:45Z) Replaced `BootstrapRenderer` ownership in `Game` with direct C++ resources held by `ResourceArena`, a resolved draw list, an identity render view, and draw-list submission through the opaque renderer.
- [x] (2026-06-23 01:45Z) Added native smoke WebGPU validation error-scope diagnostics and fixed the first real shader/layout issue they found: material uniforms are fragment-only, so material tinting now happens in `fs_main`.
- [x] (2026-06-23 01:45Z) Updated `docs/API_CONTRACTS.md` so current renderer ownership, compatibility, WebGPU baseline, and resource lifetime contracts describe resources plus the draw-list opaque pass.
- [x] (2026-06-23 01:45Z) Completed Milestone 3 local milestone review. Required findings fixed: active API contracts and stale ExecPlan current-state text were aligned with the removed `BootstrapRenderer` implementation.
- [x] (2026-06-23 02:20Z) Implemented Milestone 4 `DemoScene`: generated mipmapped checker and white textures, always-textured opaque shader layout, ground and cube meshes, perspective camera, per-frame draw-list rebuilds, and deterministic native smoke time.
- [x] (2026-06-23 02:20Z) Updated browser/native smoke from triangle ratios to scene, ground, colored-cube, lower-half scene, background, and color-bucket validation; smoke artifacts now use `opaque-demo.png` / `scene.png` names.
- [x] (2026-06-23 02:20Z) Completed Milestone 4 local milestone review. Required findings fixed: `Game::dispose` clears non-owning demo-scene pointers, long demo-scene functions now have internal comments, and demo-scene coverage was raised from 87.50% to 90.00%.
- [x] (2026-06-23 03:05Z) Completed Milestone 5 documentation and packaging consistency: `README.md`, `AGENTS.md`, `docs/SYSTEMS.md`, `docs/API_CONTRACTS.md`, smoke contracts, and CMake comments now describe the draw-list demo scene and direct C++ resource ownership.
- [x] (2026-06-23 03:05Z) Completed Milestone 5 local milestone review. Required finding fixed: `AGENTS.md` still described focused C++ browser smoke as bootstrap-triangle pixel validation, and now names demo-scene pixels.
- [x] (2026-06-23 03:05Z) Final validation passed: `npm run format:cpp:check`, `npm test`, `npm run smoke`, `npm run coverage`, `npm run build:cloudflare`, and `git -c safe.directory=C:/dev/ofg diff --check`. Dev-server review is available at `http://127.0.0.1:5174` with screenshot `C:\dev\ofg\artifacts\browser-smoke\dev-server-review.png`.
- [x] Milestone 1: add CPU-side C++ resource objects, stable ownership containers, property bags, validation, and doctest coverage.
- [x] Milestone 2: add single-device WebGPU state and mutation/update methods to Texture, Shader, Material, and Mesh.
- [x] Milestone 3: replace the BootstrapRenderer path with an opaque-pass renderer that consumes DrawList and resource references.
- [x] Milestone 4: build the animated plane-and-cubes demo scene in C++ and integrate it into browser and native smoke.
- [x] Milestone 5: update docs, API contracts, visual smoke contracts, screenshots, coverage records, and any package/deployment assumptions.

## TODO

Deferred post-migration code-review suggestions from 2026-06-20. These are recorded here for planning only; do not treat them as implemented until a later milestone explicitly takes them on.

- [ ] Fix device-lost and uncaptured-error callback userdata lifetime in `cpp/src/web/browser_game.cpp`. The current device callback userdata is owned by `BrowserGame` and can be reset during teardown; move it to storage that cannot be freed while callbacks may still fire, or add a safe inactive/cancellation path.
- [ ] Finish surface-loss recovery in `BrowserGame::render_frame_if_ready`. `Timeout` and `Outdated` now stay recoverable while preserving durable `Game` resources; surface-loss-style states still need a clearer recreate path.
- [ ] Preserve WebGPU subsystem errors until real recovery. `Game::tick` and valid `resize` calls should not erase adapter/device/render failures before TypeScript or smoke diagnostics can observe them.
- [ ] Decide and align the device-pixel-ratio reconfiguration contract. Either include DPR in the runtime/browser surface configuration key and tests, or update `docs/API_CONTRACTS.md` and smoke expectations to say only backing-size changes reconfigure the surface.
- [ ] Escape or replace existing canvas ids before building the Emdawn CSS selector. Valid DOM ids such as `game.canvas` should not break `BrowserGame::create`.
- [ ] Require pinned desktop LLVM for `npm run test:cpp`. Remove the Emscripten/PATH Clang fallback from `tools/test-cpp.mjs` so native tests prove the same compiler-family contract as coverage and native smoke.
- [ ] Reduce per-frame TypeScript debug/status overhead in `src/app/main.ts`. Keep `window.__ofgDebugStatus` available for smoke and diagnostics, but throttle or debug-gate JSON status serialization/parsing and DOM text updates.
- [ ] Replace per-frame canvas layout polling with a lower-overhead resize path before the browser UI grows. Consider `ResizeObserver`, DPR-change handling, and dirty-size tracking so `getBoundingClientRect()` is not read every animation frame.
- [ ] Add failure-path WebGPU coverage for adapter/device request failures, device loss, uncaptured errors, surface acquisition failure, encoder/finish failure, and dispose-before-callback behavior.
- [ ] Add PNG artifact validation for native smoke output, either through a small doctest around `write_rgba_png` or by having `tools/smoke-render-cpp.mjs` decode the generated PNG with `pngjs`.
- [ ] Add direct tests for WebGPU helper functions such as enum/status string names, `failure_message`, and surface format selection/fallback behavior.
- [ ] Deduplicate shared WebGPU string/format helpers between `cpp/src/render/webgpu_common.cpp` and `cpp/src/web/webgpu_utils.cpp` so browser/native labels cannot drift.
- [ ] Extract shared native C++ toolchain helpers from `tools/test-cpp.mjs`, `tools/cpp-coverage.mjs`, and `tools/smoke-render-cpp.mjs` before the next build-tooling expansion.
- [ ] Harden native smoke argument parsing in `cpp/src/native/render_smoke.cpp`: require full-string numeric consumption, reject signed unsigned values consistently, and validate finite/sensible threshold ranges.
- [ ] Split `cpp/src/native/render_smoke.cpp` before the next substantial native-smoke feature. It is now 783 lines after Milestone 4, still below the 1000-line break-up threshold but in the review pressure band.
- [ ] Deduplicate shared JS smoke pixel-classification helpers between `tools/browser-smoke.mjs` and `tools/browser-smoke-cpp.mjs` once another smoke mode appears or the visual classifier changes again.

## Surprises & Discoveries

- Observation: The useful design from the archived renderer/resource plan is the resource taxonomy and draw-list renderer, not its handle implementation path.
  Evidence: The archived plan defines mutable Texture/Shader/Material/Mesh records, PropertyBag, DrawList, OpaqueRenderer, and an animated plane-and-cubes acceptance scene. Its typed-handle/generic-store model was useful in the previous runtime shape, but the active C++ project can use direct object references under explicit owner lifetimes.

- Observation: The current C++ renderer shares the plane-and-cubes demo scene through the resource and draw-list path.
  Evidence: `cpp/include/ofg/render/demo_scene.hpp`, `cpp/src/render/demo_scene.cpp`, `cpp/src/game/game.cpp`, `cpp/src/render/opaque_pass.cpp`, browser runtime code, and the native Dawn smoke are all built by `cpp/CMakeLists.txt` and validated by `npm run smoke:browser`, `npm run smoke:browser:cpp`, and `npm run smoke:render`. `BootstrapRenderer` has been removed; `bootstrap_scene` remains only for legacy triangle layout tests plus shared clear color.

- Observation: The active public contracts now describe the plane-and-cubes visual contract.
  Evidence: Milestone 4 updated `docs/API_CONTRACTS.md` OFG-BOOT-002, OFG-BOOT-004, OFG-BOOT-005, and OFG-BOOT-006 to describe demo-scene ownership, textured checker ground, colored cubes, durable renderer-resource counters, and per-frame draw-list/model-matrix animation.

- Observation: Full mip generation must ceil-halve odd texture dimensions.
  Evidence: Milestone 1 review found the first CPU mip implementation used floor division, so a `3x1` texture skipped the required `2x1` level. `cpp/src/resources/texture.cpp` now uses `(dimension + 1) / 2`, and `cpp/tests/texture_resource_test.cpp` pins the `3x1 -> 2x1 -> 1x1` chain.

- Observation: Native smoke needed to report WebGPU validation errors before pixel analysis.
  Evidence: The first Milestone 3 native smoke rendered an all-zero PNG. Adding a WebGPU validation error scope to `cpp/src/native/render_smoke.cpp` reported that the vertex stage read a material uniform whose bind group layout was fragment-only. Moving material tinting from `vs_main` to `fs_main` fixed the validation error and restored native/browser bootstrap pixels.

- Observation: The first demo scene produces stable native and browser visual ratios with comfortable smoke margins.
  Evidence: `artifacts/render-smoke/report.json` and `artifacts/browser-smoke/report.json` both record `sceneRatio` about 0.569, `backgroundRatio` about 0.431, `groundRatio` about 0.547, `coloredRatio` about 0.0225, `lowerHalfSceneRatio` about 0.996, and 6 non-background color buckets.

- Observation: Coverage found useful untested demo-scene validation branches.
  Evidence: The first Milestone 4 `npm run coverage:cpp` run failed with `cpp/src/render/demo_scene.cpp` at 87.50%. Adding `cpp/tests/demo_scene_test.cpp` coverage for incomplete scene resources raised `demo_scene.cpp` to 90.00% and the full C++ coverage gate passed.

- Observation: Final documentation review caught stale bootstrap-era wording outside the API contracts.
  Evidence: Milestone 5 updated `README.md`, `AGENTS.md`, `docs/SYSTEMS.md`, and a native-smoke CMake comment. The final stale-term search returned no active matches for the old bootstrap-triangle smoke descriptions outside historical plan text and legacy layout-test comments.

## Decision Log

- Decision: Build the renderer/resource pipeline in C++20 under the existing `cpp/` tree.
  Rationale: The active runtime, build, test, coverage, browser WebGPU, and native Dawn smoke paths are now C++-first. Creating a separate language or runtime boundary would reintroduce the migration problem this project just removed.
  Date/Author: 2026-06-20 / Codex

- Decision: Use explicit C++ ownership and non-owning pointers/references instead of game-asset handles.
  Rationale: The active runtime is C++, so caller code can keep resource objects alive directly and pass references or pointers into materials, meshes, and draw commands. A stable owner such as `ResourceArena` or demo-scene resource bundle gives deterministic lifetimes without a typed handle/store layer or a large manager API. Resource removal is out of scope for this renderer slice.
  Date/Author: 2026-06-20, revised 2026-06-22 / Codex and User

- Decision: Assume one active WebGPU device for this milestone and allow resources to hold GPU state for that device.
  Rationale: OFG currently creates one browser device or one native smoke device. Storing prepared GPU state on the resource is simpler than a multi-device prepared-resource cache. Future device-loss or multi-device work can split CPU resource data from prepared GPU resources later.
  Date/Author: 2026-06-20 / Codex

- Decision: Do not make the device global.
  Rationale: Browser runtime and native smoke own the active `WGPUDevice` and `WGPUQueue`. Device-bound resources may store the borrowed `GpuContext` that created or prepared them so later mutation methods do not need a repeated context argument, but there should still be no process-wide global device.
  Date/Author: 2026-06-20, revised 2026-06-22 / Codex and User

- Decision: Resource classes represent high-level game/render assets, not one-to-one WebGPU wrappers.
  Rationale: OFG is exclusively WebGPU, so wrapping every WebGPU type adds ceremony without portability value. Texture, Shader, Material, and Mesh should be user-facing constructs with lifetime, validation, and stable ownership behavior; renderer internals may use raw WebGPU views, bind groups, pipelines, passes, and encoders directly.
  Date/Author: 2026-06-22 / User and Codex

- Decision: Mip-map support is required before this plan is complete.
  Rationale: The renderer should prove texture resources can represent production-shaped image assets, not just level-zero debug textures. The implementation can bring up level-zero texture upload first, but `MipMapPolicy::GenerateCpuFullChain` must generate deterministic lower levels and upload all mip levels before final acceptance.
  Date/Author: 2026-06-22 / User and Codex

- Decision: Start the first opaque material path with one always-textured bind layout.
  Rationale: Binding a generated white texture for colored materials and a generated checker texture for the ground keeps the first scene simple, avoids early shader variant churn, and still exercises texture/sampler/material plumbing. Shader variants can be added later when a real material difference requires them.
  Date/Author: 2026-06-22 / Codex

- Decision: Treat `Game::render` as the top-level browser/native render boundary for this renderer-resource plan.
  Rationale: Browser and native frame drivers should acquire targets, create command encoders, append platform work, finish command buffers, and submit; shared game/renderer code should record render commands through a plain `render` method without receiving a per-frame `GpuContext`.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Keep pipeline caching in the renderer, not in generic resource owners.
  Rationale: Render pipelines depend on shader revision, variant, target color/depth formats, vertex layout, primitive state, depth state, sample count, and bind group layouts. They are render-state combinations rather than standalone resources.
  Date/Author: 2026-06-20 / Codex

- Decision: Define shader parameter schemas explicitly beside each Shader instead of parsing WGSL reflection in this milestone.
  Rationale: WebGPU and `webgpu.h` do not provide a stable WGSL reflection API. Explicit schemas give deterministic name-to-offset behavior and are easier to cover with doctest cases.
  Date/Author: 2026-06-20 / Codex

- Decision: Match the current C++ error-reporting style for public renderer/resource factories unless a tiny local expected-style helper earns its keep.
  Rationale: Existing C++ renderer code returns `std::unique_ptr` or `bool` and fills a caller-provided `std::string& error`. The resource pipeline should not introduce a broad result framework by accident. If implementation creates a small `Expected` helper, it must be documented, covered by doctest, and used consistently enough to remove real complexity.
  Date/Author: 2026-06-20 / Codex

- Decision: Use small local math types for the initial camera, transform, and vector data unless implementation proves that a dependency is justified.
  Rationale: The first scene only needs matrices, vectors, perspective projection, and basic transforms. A local, tested math slice is enough for Milestone 4 and avoids adding a dependency before OFG knows the shape of its scene/physics math.
  Date/Author: 2026-06-20 / Codex

- Decision: Make the local C++ math API feel like shader code while keeping it minimal.
  Rationale: Renderer, shader-schema, and demo-scene code will be easier to audit when CPU-side math resembles WGSL values and uniform packing. Type names stay C++-style as `Vec2`, `Vec3`, `Vec4`, and `Mat4`, but these plain value structs intentionally expose shader-style vector fields `x`, `y`, `z`, and `w` as a narrow exception to the usual member-prefix rule. Other renderer/resource classes still use `m_` members.
  Date/Author: 2026-06-22 / User and Codex

- Decision: Require purpose comments or doc comments for every function and detailed file headers for new files in this plan.
  Rationale: `AGENTS.md` requires function comments and top-of-file purpose comments. Renderer/resource code will introduce enough types and ownership rules that comments are part of correctness, not polish.
  Date/Author: 2026-06-20 / User and Codex

- Decision: Defer renderer sorting and keep stable insertion order for the first opaque draw-list slice.
  Rationale: Milestone 3 still draws a single bootstrap triangle, so stable insertion order is sufficient and directly covered. Milestone 4's plane-and-cubes scene can decide whether front-to-back sorting is required once the first multi-object scene exists.
  Date/Author: 2026-06-23 / Codex

- Decision: Keep a native smoke WebGPU validation error scope around render/readback work.
  Rationale: A validation error can otherwise produce a black or zeroed PNG and fail only through pixel ratios. Reporting the Dawn validation message makes renderer and shader layout failures actionable before pixel classification.
  Date/Author: 2026-06-23 / Codex

- Decision: Use double-sided culling for the first plane-and-cubes opaque scene.
  Rationale: Milestone 4 is proving resources, texture sampling, draw-list submission, depth, camera transforms, and visual smoke classification. Disabling back-face culling avoids making the first scene fragile to mesh winding while still rendering through ordinary indexed triangle geometry. A later mesh/import pass can re-enable culling once winding conventions are formalized.
  Date/Author: 2026-06-23 / Codex

- Decision: Browser/native smoke should require initialized durable renderer counters but not exactly one pipeline.
  Rationale: The current pipeline key includes each material bind-group-layout handle, so the demo scene naturally reports five pipelines for five materials even though they share a shader shape. Exact one-pipeline assertions belonged to the bootstrap triangle and would fail a legitimate multi-material scene.
  Date/Author: 2026-06-23 / Codex

## Outcomes & Retrospective

Milestone 1 is complete. Direct C++ resource ownership was sufficient for the CPU-side slice: `ResourceArena` gives stable owning storage, while `Material`, `Mesh`, and `PropertyBag` use explicit non-owning pointers/references instead of handles. The local milestone review fixed odd-sized mip-chain generation before completion.

Milestone 2 is complete. The same ownership model now carries live WebGPU state for one active device: resource creation eagerly prepares device-backed state when given a ready `GpuContext`, and mutation APIs refresh durable GPU state only when explicit resource data changes. Later milestones still need to consume these resources through a draw-list renderer, prove material bind group and pipeline layout compatibility in real draws, and replace the bootstrap visual contract with the plane-and-cubes demo scene.

Milestone 3 is complete. `Game` now builds visible content as ordinary C++ resources, stores them in `ResourceArena`, submits a resolved `DrawList`, and renders through `Renderer` and `OpaquePass` instead of `BootstrapRenderer`. The Milestone 3 visible triangle contract was intentionally preserved only for that milestone while the internal path gained pass-owned frame/draw uniforms, depth state, pipeline caching, material bind groups, and native/browser smoke diagnostics.

Milestone 4 is complete. The visible renderer output is now a C++ demo scene with a mipmapped checker ground plane, four animated colored cubes, perspective camera, depth buffering, and an always-textured opaque WGSL material path. Browser and native smokes validate scene, ground, cube-color, background, lower-half, and color-bucket ratios from shared thresholds. Remaining final work is mostly a consistency and packaging pass: keep docs/contracts/current-state text aligned, run the broad test/smoke/coverage gates, keep the dev server available for review, and record final artifacts.

Milestone 5 is complete. The resource/renderer plan now delivers direct C++ resource ownership without asset handles, GPU-ready Texture/Shader/Material/Mesh resources that may retain their creator `GpuContext`, required CPU-generated mip chains, a shader-language-like math slice, an opaque draw-list renderer, and a visible C++ plane-and-cubes demo scene. Active docs and smoke contracts describe that current state, Cloudflare packaging still includes the generated C++ WASM runtime, coverage passes, and the completed plan is ready to archive.

## Contract and Quality Baseline

This plan must preserve or intentionally update the active contracts in `C:\dev\ofg\docs\API_CONTRACTS.md`.

OFG-BOOT-001 TypeScript Host Ownership is preserved. TypeScript may keep creating the canvas, resizing it, loading WASM, and displaying errors. It must not own renderer resource objects, mesh/material/texture/shader mutation, GPU pipeline setup, scene data, or draw submission.

OFG-BOOT-002 C++ Runtime Ownership is preserved and expanded. C++ continues to own frame state, scene data, WebGPU resources, renderer setup, browser runtime behavior, native Dawn offscreen rendering, and platform queue submission. Shared `Game` owns renderer resources, stable resource owners, draw-list construction, and render command recording; browser and native frame drivers own target acquisition, command-buffer finish, and submit.

OFG-BOOT-003 WASM Facade is preserved. The browser facade should still expose create, resize, frame, debug status, and dispose. The existing `frame(time_ms)` input supplies time to C++; this plan does not require a new TypeScript render API.

OFG-BOOT-004 Renderer Compatibility has been rewritten for the draw-list renderer and demo scene. Browser and native smoke must use the same C++ resource layer, shader source, draw-list renderer, clear color, generated checker/white textures, ground/cube visual categories, and visual smoke expectations. Allowed differences remain the final output target and adapter/surface format.

OFG-BOOT-005 WebGPU Baseline now describes the current plane-and-cubes draw-list visual. The baseline still requests no optional GPU features and no manual limits above adapter defaults. The current visual uses an always-textured opaque material path, generated mipmapped textures, perspective camera, depth buffering, a ground plane, and four animated cubes. Smoke/debug expectations require initialized durable renderer counters but must not assume exactly one pipeline.

OFG-BOOT-006 Resource Lifetime remains important. Shaders, buffers, textures, samplers, bind groups, and pipelines must not be recreated on ordinary frames unless a caller explicitly mutates a resource or the render target is resized/reconfigured. Device-bound resources may keep their creator `GpuContext` for the owning `Game` device lifetime, but they do not own or release the platform device or queue. Ordinary frames may update frame and draw uniform buffer contents and submit draw calls.

OFG-BOOT-007 Generated Artifacts is preserved. New screenshots and local smoke output should live under `C:\dev\ofg\artifacts`, not in source-controlled generated directories.

OFG-BOOT-008 Deployment is preserved unless the generated C++ WASM/JS paths change. This plan should not change Cloudflare packaging unless it deliberately adds runtime asset files that must be copied.

OFG-BOOT-009 Coverage is preserved. Modified implementation files should meet the coverage gate unless this plan records a specific exception with rationale. Browser-only WebGPU code continues to be validated by WASM builds, TypeScript adapter tests, and browser smoke.

Quality constraints from `C:\dev\ofg\AGENTS.md` apply: every function written should have a doc string or purpose comment, functions over 50 lines should contain internal comments explaining their workings, files should have maintained top comments, and files in the 500-1000 line band should be considered for splitting before they grow further.

## Context and Orientation

The repository root is `C:\dev\ofg`. It is now a C++/WASM runtime with a TypeScript browser host.

`C:\dev\ofg\cpp\CMakeLists.txt` builds one shared WebGPU-capable OFG C++ library, the doctest executable, browser Emscripten module, and native Dawn render-smoke executable. C++ browser builds use Emscripten, Embind, and Emdawnwebgpu. Native render smoke uses an installed Dawn checkout supplied through `OFG_DAWN_SOURCE_DIR` and `tools/smoke-render-cpp.mjs`.

`C:\dev\ofg\cpp\include\ofg\render\bootstrap_renderer.hpp` and `C:\dev\ofg\cpp\src\render\bootstrap_renderer.cpp` were the previous hard-coded bootstrap renderer and are removed by Milestone 3. The current path builds `DemoScene` resources in `C:\dev\ofg\cpp\src\render\demo_scene.cpp`, stores them in `ResourceArena`, rebuilds `DrawCommand` values from frame time in `C:\dev\ofg\cpp\src\game\game.cpp`, then renders through `Renderer` and `OpaquePass`.

`C:\dev\ofg\cpp\include\ofg\render\bootstrap_scene.hpp` and `C:\dev\ofg\cpp\src\render\bootstrap_scene.cpp` remain as legacy deterministic triangle layout regression data plus the shared clear-color helper. These are native-checkable and covered by doctest, but they are no longer the active visual renderer path.

`C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp` and `C:\dev\ofg\cpp\src\web\browser_game.cpp` own browser WebGPU setup, surface configuration, resize behavior, frame submission, and lifecycle.

`C:\dev\ofg\cpp\include\ofg\native\render_smoke.hpp`, `C:\dev\ofg\cpp\src\native\render_smoke.cpp`, and `C:\dev\ofg\cpp\src\native\render_smoke_main.cpp` own browser-free native rendering through Dawn. `render_smoke.cpp` is already in the 500-1000 line concern band and should be split before this plan adds more native smoke behavior.

Definitions used in this plan:

Resource: a mutable game/render object such as Texture, Shader, Material, or Mesh.

Resource owner: a C++ object that owns resources for a `Game` or demo scene lifetime. It can be as simple as vectors of `std::unique_ptr<Texture>`, `std::unique_ptr<Shader>`, `std::unique_ptr<Material>`, and `std::unique_ptr<Mesh>`, or an equivalent stable-address container. Its job is lifetime, not broad resource-management policy.

Resource reference: a non-owning `Texture*`, `Shader*`, `Material*`, or `Mesh*` used by another resource or a draw command. These pointers are valid because the resource owner outlives all materials, meshes, and draw lists built from it.

GpuContext: a simple borrowed context containing the active `WGPUDevice` and `WGPUQueue`, or equivalent method arguments. It is not global. Device-bound resources may store the context that created or prepared them, and that context is only valid for the owning `Game` device lifetime.

Math slice: a small `ofg::math` module with shader-like vector and matrix values for renderer data. It provides `Vec2`, `Vec3`, `Vec4`, `Mat4`, arithmetic helpers, transform builders, camera projection/view helpers, and column-major uniform packing. It is not a general-purpose physics, geometry, or SIMD math library.

PropertyBag: a named collection of shader parameter values. Materials and draw commands both use it.

Draw list: an ordered or sortable collection of draw commands passed to the renderer for one frame.

Draw command: one mesh instance to render, with a mesh pointer, explicit world transform, optional material override pointers, optional extra properties, and sort metadata.

Opaque pass: the first render pass for non-transparent geometry. It should clear color and depth, sort front-to-back or use an explicitly documented stable-order policy, bind each command's shader, material, mesh, and draw uniforms, then issue indexed draw calls.

Renderer: the shared C++ object owned by `Game` that consumes `RenderView` and `DrawList`, then records one or more passes into the caller-owned command encoder. This plan only implements an opaque pass, but the public shape should leave room for later depth prepass, shadow, transparent, postprocess, and UI passes.

Opaque shader: the first WGSL shader path. Milestone 3 uses vertex color plus a material `base_color_factor` uniform to preserve the bootstrap triangle while proving frame, draw, and material bind groups. Milestone 4 should extend this path with one always-textured material bind layout: colored materials bind a generated white texture with a `base_color_factor`, and the checker ground material binds the generated checker texture. Shader variants can wait until a real second layout is needed.

## Plan of Work

Milestone 1 adds the CPU-side resource model and the minimal math layer. Add math headers under `C:\dev\ofg\cpp\include\ofg\math\` and sources under `C:\dev\ofg\cpp\src\math\` for `Vec2`, `Vec3`, `Vec4`, `Mat4`, arithmetic, camera, transform, and uniform-packing helpers. Add resource headers under `C:\dev\ofg\cpp\include\ofg\resources\` and source files under `C:\dev\ofg\cpp\src\resources\`. Implement `ResourceArena` or an equivalently small stable owner, `Texture`, `Shader`, `Material`, `Mesh`, `SubMesh`, `PropertyBag`, `PropertyValue`, and validation errors. Texture CPU data should include mip policy, expected mip counts, and room for generated mip levels, but the full lower-level mip generation may land in Milestone 2 with GPU upload. In this milestone, resources can be constructed and mutated as CPU data; GPU state can be absent or represented by explicit empty state types that are filled in Milestone 2. This milestone should not require a GPU adapter to test.

Milestone 2 adds single-device GPU state to the resource types using `webgpu.h`. Texture creation/update methods upload or rewrite texture data, and `MipMapPolicy::GenerateCpuFullChain` must generate deterministic CPU mip levels and upload each level into a WebGPU texture with the matching mip count. Shader creation/replacement creates a shader module. Material creation/update validates its property bag against its shader and creates or refreshes material uniform and bind-group state. Mesh creation/update creates or refreshes vertex and index buffers. Device-bound resources store the `GpuContext` that created or prepared them so later mutation methods can refresh GPU state without requiring every caller to pass device and queue again. Resource mutation is eager once a resource is GPU-prepared: when a method changes pixels, vertices, shader source, or material properties, it updates the related GPU state in the same call.

Milestone 3 introduces reusable renderer interfaces under `C:\dev\ofg\cpp\include\ofg\render\` and `C:\dev\ofg\cpp\src\render\`. Add draw-list, camera/render-view, renderer, opaque-pass, pipeline-cache, shader-layout, and demo-scene modules or similarly named files. Replace BootstrapRenderer rather than preserving it as a public compatibility interface. The renderer should own pass-level resources: frame uniform buffer, dynamic draw uniform arena, depth texture, pipeline cache, and an initial opaque pass. The draw list should contain resolved mesh/material pointers from the owning resource bundle. Per-draw uniforms must use dynamic offsets, a per-frame uniform arena, storage-buffer indexing, or another explicit strategy that prevents all draws from seeing the last model matrix.

Milestone 4 replaces the visible bootstrap triangle with the animated demo scene. Build a ground plane mesh, a cube mesh, a generated mipmapped checker texture, a generated white texture for colored materials, a basic opaque shader using one always-textured material layout, and several draw commands whose explicit model transforms are derived from frame time. Use deterministic demo constants for camera pose, field of view, near/far planes, ground size, cube sizes, cube positions, cube colors, and the native smoke frame time.

Milestone 5 updates remaining docs, smoke expectations, coverage artifacts, and screenshots. Update `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` in the milestone where each contract changes, then do a final consistency pass here for the resource model, renderer, opaque pass, and DemoScene. Update `C:\dev\ofg\tools\smoke-contract.json` and smoke pixel classification so a blank frame, missing depth, missing ground, missing cube colors, broken mip generation, or per-frame durable-resource churn fail clearly. Renderer diagnostics should prove durable resource counts stay stable across ordinary frames and resizes; they should not keep bootstrap-era assertions such as exactly one pipeline and one buffer.

After each milestone, run the repo-local `milestone-review` skill before marking that milestone complete. Apply required findings or record a rejected finding with rationale in this plan's Decision Log.

## Concrete Steps

From `C:\dev\ofg`, create the C++ math, resource, and renderer module files and wire them into `cpp/CMakeLists.txt`:

    cpp/include/ofg/math/vec.hpp
    cpp/include/ofg/math/mat.hpp
    cpp/include/ofg/math/transform.hpp
    cpp/src/math/mat.cpp
    cpp/src/math/transform.cpp
    cpp/include/ofg/resources/resource_arena.hpp
    cpp/include/ofg/resources/texture.hpp
    cpp/include/ofg/resources/shader.hpp
    cpp/include/ofg/resources/material.hpp
    cpp/include/ofg/resources/mesh.hpp
    cpp/include/ofg/resources/property_bag.hpp
    cpp/include/ofg/resources/resource_error.hpp
    cpp/src/resources/resource_arena.cpp
    cpp/src/resources/texture.cpp
    cpp/src/resources/shader.cpp
    cpp/src/resources/material.cpp
    cpp/src/resources/mesh.cpp
    cpp/src/resources/property_bag.cpp
    cpp/src/resources/resource_error.cpp
    cpp/include/ofg/render/draw_list.hpp
    cpp/include/ofg/render/camera.hpp
    cpp/include/ofg/render/renderer.hpp
    cpp/include/ofg/render/opaque_pass.hpp
    cpp/include/ofg/render/pipeline_cache.hpp
    cpp/include/ofg/render/demo_scene.hpp
    cpp/src/render/draw_list.cpp
    cpp/src/render/camera.cpp
    cpp/src/render/renderer.cpp
    cpp/src/render/opaque_pass.cpp
    cpp/src/render/pipeline_cache.cpp
    cpp/src/render/demo_scene.cpp
    cpp/src/render/shaders/opaque_uber.wgsl.hpp

Add doctest files and register them in the existing `ofg_cpp_tests` target:

    cpp/tests/math_test.cpp
    cpp/tests/resource_arena_test.cpp
    cpp/tests/property_bag_test.cpp
    cpp/tests/texture_resource_test.cpp
    cpp/tests/shader_resource_test.cpp
    cpp/tests/material_resource_test.cpp
    cpp/tests/mesh_resource_test.cpp
    cpp/tests/resource_error_test.cpp
    cpp/tests/renderer_test.cpp
    cpp/tests/opaque_pass_test.cpp
    cpp/tests/demo_scene_test.cpp

Milestone 1 validation:

    npm run test:cpp
    npm run coverage:cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 2 validation:

    npm run test:cpp
    npm run build:wasm
    npm run smoke:render
    npm run coverage:cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 3 validation:

    npm run test:cpp
    npm run smoke:render
    npm run smoke:browser
    npm run coverage:cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 4 validation:

    npm test
    npm run smoke
    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 5 final validation:

    npm test
    npm run smoke
    npm run coverage
    npm run build:cloudflare
    git -c safe.directory=C:/dev/ofg diff --check

For browser or visual work, keep a dev server available for human review:

    npm run dev

Report the URL printed by the server. If port 5173 is busy, use the alternate URL printed by the tool. Take and share screenshots after the first 3D render appears, after animation/material changes, and before finalizing.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, screenshots, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Milestone 1 is accepted when C++ exposes documented CPU-side `ofg::math` vector/matrix helpers plus `ResourceArena` or an equivalent stable owner, `Texture`, `Shader`, `Material`, `Mesh`, `PropertyBag`, and `PropertyValue` types. Doctest coverage must cover vector construction and arithmetic, dot/cross/normalize behavior, matrix identity/multiplication/transform composition, perspective and look-at matrix behavior, column-major WGSL uniform packing, stable resource ownership, pointer/reference lifetime assumptions, texture format mapping, sRGB versus linear selection, pixel-array validation, mip policy and mip-count validation, generated pixel texture construction, shader parameter lookup, property value type validation, material property validation, draw-scope property validation, and mesh submesh range validation. If general image-byte decoding is added, its success and failure paths must be covered too.

Milestone 2 is accepted when resource methods can eagerly create and update GPU state for the single active device while retaining their creator `GpuContext`. Native or browser-backed tests and smokes should cover texture upload/update, `GenerateCpuFullChain` mip generation and all-level upload, shader module creation/replacement, material uniform/bind-group creation, mesh buffer creation/update, and that explicit resource mutation changes GPU resources while ordinary read-only frames do not recreate durable resources.

Milestone 3 is accepted when C++ exposes DrawList, DrawCommand, Camera or RenderView, Renderer, OpaquePass, PipelineCache, and RendererCounters or equivalent diagnostics. Tests must cover front-to-back sort order or an explicitly deferred stable-order policy, material override resolution, draw-list command validation, draw PropertyBag validation, explicit model-matrix packing, dynamic uniform-buffer offsets or the chosen equivalent per-draw data strategy, pipeline cache key separation, depth state, and cleanup of old BootstrapRenderer exports/imports. Native smoke should render through DrawList.

Milestone 4 is accepted when browser and native smoke render a large ground plane plus multiple cubes at different depths. The browser frame loop should animate cube rotation and vertical sine-wave motion. Native smoke should render a deterministic time sample, such as 1250 ms. Browser screenshots and native PNGs should visibly show the plane and cubes with depth testing.

Milestone 5 is accepted when docs and contracts describe the direct C++ resource ownership model, stored resource `GpuContext` ownership, mip-map policy, and renderer/pass ownership; smoke expectations are updated; Cloudflare packaging still contains the right generated C++ runtime assets; and coverage passes. The coverage command must confirm changed implementation files do not appear in the default filtered coverage attention report unless this plan records an explicit exception.

Visual acceptance:

The first viewport should show the actual running render surface, not a marketing or explanatory page. The rendered image should not be blank. The ground plane should be visibly large relative to the cubes. At least three cubes should appear at distinct depths, with perspective and depth ordering visible. Browser screenshots should be stored under `C:\dev\ofg\artifacts\browser-smoke` or a clearly named subdirectory under `C:\dev\ofg\artifacts`.

Comment/readability acceptance:

Every new or changed C++ header, C++ source file, TypeScript file, or tool script should have a maintained top-of-file purpose comment unless the file's established local style clearly uses an equivalent header. Every new or changed function should have a purpose comment or doc comment. Any function over 50 lines should have internal comments that explain its phases. Milestone reviews must check this explicitly before marking a milestone complete.

## Idempotence and Recovery

Source edits may replace the bootstrap renderer path outright once the new opaque renderer is ready for the same milestone's tests. There is no requirement to keep BootstrapRenderer, bootstrap scene helpers, or bootstrap shader source as public compatibility interfaces after the new renderer is wired in and the contracts are updated.

Generated directories `C:\dev\ofg\dist`, `C:\dev\ofg\dist-test`, `C:\dev\ofg\.deploy`, `C:\dev\ofg\artifacts`, and `C:\dev\ofg\assets\wasm\ofg_cpp` can be regenerated by the existing npm scripts. Do not manually preserve generated files as source of truth.

If a GPU smoke command fails because no adapter is available, record the adapter/environment error in Surprises & Discoveries and continue with CPU tests only if the user agrees or the environment limitation is clear. Do not weaken smoke expectations for real rendering failures.

If shader variants become too complex for simple explicit variant keys, fall back to a minimal deterministic variant source builder that only substitutes declared boolean or numeric constants at known markers. Record that decision here before implementing it.

If mip generation slips while level-zero textures already render, do not mark this plan complete. Either finish `GenerateCpuFullChain` support before Milestone 5 acceptance or explicitly split the remaining mip work into a new active plan approved by the user.

If the single-device assumption stops holding, revise this plan before coding the affected feature. That future revision can split resource CPU data from prepared GPU resources, but that complexity is intentionally out of scope for this milestone.

## Artifacts and Notes

Expected durable implementation artifacts:

    C:\dev\ofg\cpp\include\ofg\math\vec.hpp
    C:\dev\ofg\cpp\include\ofg\math\mat.hpp
    C:\dev\ofg\cpp\include\ofg\math\transform.hpp
    C:\dev\ofg\cpp\src\math\mat.cpp
    C:\dev\ofg\cpp\src\math\transform.cpp
    C:\dev\ofg\cpp\include\ofg\resources\resource_arena.hpp
    C:\dev\ofg\cpp\include\ofg\resources\texture.hpp
    C:\dev\ofg\cpp\include\ofg\resources\shader.hpp
    C:\dev\ofg\cpp\include\ofg\resources\material.hpp
    C:\dev\ofg\cpp\include\ofg\resources\mesh.hpp
    C:\dev\ofg\cpp\include\ofg\resources\property_bag.hpp
    C:\dev\ofg\cpp\include\ofg\resources\resource_error.hpp
    C:\dev\ofg\cpp\src\resources\resource_arena.cpp
    C:\dev\ofg\cpp\src\resources\texture.cpp
    C:\dev\ofg\cpp\src\resources\shader.cpp
    C:\dev\ofg\cpp\src\resources\material.cpp
    C:\dev\ofg\cpp\src\resources\mesh.cpp
    C:\dev\ofg\cpp\src\resources\property_bag.cpp
    C:\dev\ofg\cpp\src\resources\resource_error.cpp
    C:\dev\ofg\cpp\include\ofg\render\draw_list.hpp
    C:\dev\ofg\cpp\include\ofg\render\camera.hpp
    C:\dev\ofg\cpp\include\ofg\render\renderer.hpp
    C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp
    C:\dev\ofg\cpp\include\ofg\render\pipeline_cache.hpp
    C:\dev\ofg\cpp\include\ofg\render\demo_scene.hpp
    C:\dev\ofg\cpp\src\render\draw_list.cpp
    C:\dev\ofg\cpp\src\render\camera.cpp
    C:\dev\ofg\cpp\src\render\renderer.cpp
    C:\dev\ofg\cpp\src\render\opaque_pass.cpp
    C:\dev\ofg\cpp\src\render\pipeline_cache.cpp
    C:\dev\ofg\cpp\src\render\demo_scene.cpp
    C:\dev\ofg\cpp\src\render\shaders\opaque_uber.wgsl.hpp

Expected visual artifacts:

    C:\dev\ofg\artifacts\render-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    C:\dev\ofg\artifacts\browser-smoke\*.png

Record final command transcripts here in concise form as milestones complete.

Initial plan review during migration Milestone 6:

    Required fixes applied: removed stale implementation-path wording, aligned interface sketches with the current C++ error-reporting style, narrowed texture scope to generated/caller-provided pixels unless image decoding remains small and covered, and added explicit decisions for local math types plus comment/readability acceptance.
    Validation: stale retired-language/tooling search against this plan found no active implementation-path drift; git diff --check passed with only existing LF-to-CRLF warnings elsewhere in the worktree.
    Remaining risks: per-draw uniform packing, durable WebGPU resource lifetime, native smoke file split, and keeping browser/native visual expectations aligned remain implementation risks for this plan's later milestones.

Milestone 1 implementation validation on 2026-06-22:

    Implemented artifacts: math headers/sources, CPU-side resource headers/sources, resource doctests, and CMake wiring.
    Coverage gate update: `tools/cpp-coverage.mjs` now checks `cpp/src/math` and `cpp/src/resources` in addition to the earlier native-checkable C++ sources.
    Milestone review:
      Scope: Milestone 1 CPU-side math/resource implementation, CMake wiring, coverage gate update, and this ExecPlan.
      Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this thread did not explicitly request delegated sub-agent review.
      Required findings fixed: odd-sized texture mip chains now use ceil-halving and regression tests cover a `3x1 -> 2x1 -> 1x1` chain.
      Follow-ups recorded: none new beyond the existing later-milestone WebGPU/upload/visual risks.
      Rejected findings: none.
      Note: `docs/ARCHITECTURE.md` is not present in this checkout; review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, this ExecPlan, and touched source/tests.
    Validation:
      `npm run format:cpp:check` passed.
      `npm run test:cpp` passed with 53 doctest cases.
      `npm run coverage:cpp` passed; checked files included `cpp/src/math/*` and `cpp/src/resources/*`, with changed resource coverage at or above 90%: `material.cpp` 100.00%, `mesh.cpp` 97.54%, `property_bag.cpp` 94.87%, `resource_arena.cpp` 100.00%, `resource_error.cpp` 93.75%, `shader.cpp` 98.35%, and `texture.cpp` 100.00%.
      `git diff --check` passed with only the existing LF-to-CRLF warning for `cpp/CMakeLists.txt`.
    Visual artifacts: none expected for this CPU-only milestone.
    Remaining risks: WebGPU upload/release paths, stored `GpuContext` mutation behavior, bind-group/pipeline layout design, draw-list integration, and visual smoke expectations remain for later milestones.

Milestone 2 implementation validation on 2026-06-22:

    Implemented artifacts: GPU state and mutation paths in `Texture`, `Shader`, `Material`, and `Mesh`; `gpu_context_is_empty`/`gpu_context_is_ready`; Dawn-null resource GPU tests; and native test CMake wiring for `webgpu_test_utils`.
    Milestone review:
      Scope: Milestone 2 WebGPU-backed resource implementation, CMake/test fixture changes, coverage gate behavior for changed resource files, and this ExecPlan.
      Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this thread did not explicitly request delegated sub-agent review.
      Required findings fixed: stale Milestone 1 header/source comments were updated to current GPU-ready behavior, and an unused `<stdexcept>` include was removed from the test WebGPU helper.
      Follow-ups recorded: none new beyond the existing Milestone 3 need to prove bind-group-layout and pipeline-layout compatibility in real draw submission.
      Rejected findings: none.
      Note: `docs/ARCHITECTURE.md` is not present in this checkout; review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, this ExecPlan, and touched source/tests.
    Validation:
      `npm run format:cpp:check` passed.
      `npm run test:cpp` passed with 60 doctest cases.
      `npm run coverage:cpp` passed; changed resource coverage stayed at or above 90%: `material.cpp` 93.31%, `mesh.cpp` 91.59%, `property_bag.cpp` 94.87%, `resource_arena.cpp` 100.00%, `resource_error.cpp` 93.75%, `shader.cpp` 90.96%, and `texture.cpp` 90.78%.
      `npm run build:wasm` passed and regenerated `assets\wasm\ofg_cpp\ofg_cpp.js` plus `assets\wasm\ofg_cpp\ofg_cpp.wasm`.
      `npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json`.
      `git -c safe.directory=C:/dev/ofg diff --check` passed with only existing LF-to-CRLF warnings for `cpp/CMakeLists.txt` and `cpp/include/ofg/game/gpu_context.hpp`.
    Visual artifacts: native bootstrap smoke image at `C:\dev\ofg\artifacts\render-smoke\bootstrap.png`; no browser screenshot expected because Milestone 2 does not change visible browser output.
    Remaining risks: real renderer pass integration, material/pipeline layout compatibility, per-draw uniform strategy, shader WGSL contracts, depth state, and browser/native visual acceptance remain for Milestones 3 and 4.

Milestone 3 implementation validation on 2026-06-23:

    Implemented artifacts: `DrawList`, `DrawCommand`, `RenderView`, `PipelineCache`, `OpaquePass`, `Renderer`, `RendererCounters`, `opaque_uber_wgsl`, `Game` resource/draw-list integration, native smoke WebGPU validation error-scope diagnostics, renderer tests, and updated active API contracts.
    Milestone review:
      Scope: Milestone 3 draw-list renderer implementation, `BootstrapRenderer` removal, `Game` integration, renderer tests, coverage gate changes, native/browser smoke artifacts, `docs/API_CONTRACTS.md`, and this ExecPlan.
      Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this thread did not explicitly request delegated sub-agent review.
      Required findings fixed: active API contracts were updated from the removed bootstrap-renderer implementation to resource/draw-list renderer ownership, compatibility, WebGPU baseline, and resource lifetime; stale current-state wording in this ExecPlan was updated after `BootstrapRenderer` removal.
      Follow-ups recorded: no new follow-up beyond existing later-milestone work. `render_smoke.cpp` remains in the 500-1000 line concern band and should be split before substantial native-smoke expansion; Milestone 4 or 5 must revise renderer diagnostics and smoke expectations once the scene has multiple objects, textures, or additional pipelines.
      Rejected findings: none.
      Note: `docs/ARCHITECTURE.md` is not present in this checkout; review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, this ExecPlan, and touched source/tests.
    Validation:
      `npm run format:cpp:check` passed.
      `npm run test:cpp` passed with 70 doctest cases.
      `npm run build:wasm` passed and regenerated `assets\wasm\ofg_cpp\ofg_cpp.js` plus `assets\wasm\ofg_cpp\ofg_cpp.wasm`.
      `npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json`; the report recorded `triangleRatio` 0.230112, `backgroundRatio` 0.769888, and 28 non-background color buckets.
      `npm run smoke:browser` passed and wrote `C:\dev\ofg\artifacts\browser-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\browser-smoke\report.json`; the report showed WebGPU available, cross-origin isolation enabled, pipeline count 1, buffer count 1, and no runtime error.
      `npm run coverage:cpp` passed; checked changed render/resource implementation coverage stayed at or above 90%: `draw_list.cpp` 92.86%, `opaque_pass.cpp` 90.46%, `pipeline_cache.cpp` 93.70%, `renderer.cpp` 100.00%, resource files at or above 90%, and math files at 100.00%.
      `git -c safe.directory=C:/dev/ofg diff --check` passed with only existing LF-to-CRLF warnings for edited C++ files.
      `rg -n "BootstrapRenderer|bootstrap_renderer" cpp` returned no matches.
    Visual artifacts: native bootstrap smoke image at `C:\dev\ofg\artifacts\render-smoke\bootstrap.png`; browser bootstrap screenshot at `C:\dev\ofg\artifacts\browser-smoke\bootstrap.png`.
    Remaining risks: Milestone 4 still needs the real animated plane-and-cubes scene, texture sampling in the opaque shader, generated checker/white materials, perspective camera, multi-draw depth behavior, updated smoke visual thresholds, and renderer diagnostics that no longer assume exactly one pipeline and one compatibility buffer.

Milestone 4 implementation validation on 2026-06-23:

    Implemented artifacts: `DemoScene`, always-textured opaque WGSL sampling, generated mipmapped checker and white textures, ground/cube meshes, animated draw-list update, perspective camera, native deterministic demo time, updated browser/native smoke classifiers, updated smoke thresholds, focused demo-scene doctests, and active API contract updates.
    Milestone review:
      Scope: Milestone 4 demo-scene implementation, texture-sampling shader path, `Game` per-frame draw-list integration, browser/native smoke contract update, focused C++ WebGPU smoke counter update, `docs/API_CONTRACTS.md`, and this ExecPlan.
      Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this thread did not explicitly request delegated sub-agent review.
      Required findings fixed: `Game::dispose` now clears non-owning `DemoScene` pointers after clearing `ResourceArena`; long demo-scene builder/updater functions now include internal comments; coverage failure for `demo_scene.cpp` at 87.50% was fixed by adding invalid-scene updater tests, raising it to 90.00%.
      Follow-ups recorded: split `cpp/src/native/render_smoke.cpp` before the next substantial native-smoke feature; it is now 783 lines, still below the hard 1000-line threshold but in the review pressure band. Shared JS smoke pixel-classification helpers are duplicated between `tools/browser-smoke.mjs` and `tools/browser-smoke-cpp.mjs`; acceptable for this milestone but worth deduplicating once another smoke mode appears.
      Rejected findings: none.
      Note: `docs/ARCHITECTURE.md` is not present in this checkout; review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, this ExecPlan, and touched source/tests/tools.
    Validation:
      `npm run format:cpp:check` passed.
      `npm run test:cpp` passed with 72 doctest cases.
      `npm run build:wasm` passed and regenerated `assets\wasm\ofg_cpp\ofg_cpp.js` plus `assets\wasm\ofg_cpp\ofg_cpp.wasm`.
      `npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json`; the report recorded `sceneRatio` 0.569488, `backgroundRatio` 0.430512, `groundRatio` 0.546966, `coloredRatio` 0.0225218, `lowerHalfSceneRatio` 0.996105, and 6 non-background color buckets.
      `npm run smoke:browser` passed and wrote `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` plus `C:\dev\ofg\artifacts\browser-smoke\report.json`; the report showed WebGPU available, cross-origin isolation enabled, pipeline count 5, buffer count 1, and no runtime error.
      `npm run smoke:browser:cpp` passed and wrote `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png` plus `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`; resize, zero-size recovery, and renderer counters stayed valid.
      `npm run coverage:cpp` passed; checked changed render/resource implementation coverage stayed at or above 90%: `demo_scene.cpp` 90.00%, `draw_list.cpp` 92.86%, `opaque_pass.cpp` 90.46%, `pipeline_cache.cpp` 93.70%, `renderer.cpp` 100.00%, resource files at or above 90%, and math files at 100.00%.
    Visual artifacts: native demo smoke image at `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`; browser demo screenshot at `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`; focused C++ browser fixture screenshot at `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`.
    Remaining risks: final Milestone 5 still needs broad `npm test`, `npm run smoke`, `npm run coverage`, `git diff --check`, dev-server review URL reporting, and final docs/package consistency checks.

Milestone 5 implementation validation on 2026-06-23:

    Implemented artifacts: final active-doc consistency updates in `README.md`, `AGENTS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `tools/smoke-contract.json`, smoke scripts, CMake comments, Cloudflare packaging output, and this ExecPlan.
    Milestone review:
      Scope: Milestone 5 final docs, smoke-contract, packaging, visual artifacts, coverage evidence, and ExecPlan completion state.
      Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this thread did not explicitly request delegated sub-agent review.
      Required findings fixed: `AGENTS.md` still described focused C++ browser smoke as bootstrap-triangle pixel validation; it now describes demo-scene pixel validation.
      Follow-ups recorded: split `cpp/src/native/render_smoke.cpp` before the next substantial native-smoke feature, and deduplicate duplicated JS smoke pixel-classification helpers once another smoke mode appears or the classifier changes again.
      Rejected findings: none.
      Note: `docs/ARCHITECTURE.md` is not present in this checkout; review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, this ExecPlan, and touched source/tests/tools/docs.
    Validation:
      `npm run format:cpp:check` passed after final documentation/CMake edits.
      `npm test` passed with 72 C++ doctest cases and 19 TypeScript tests.
      `npm run smoke` passed, including browser demo smoke and native Dawn render smoke.
      `npm run coverage` passed; changed C++ implementation files met the 90% coverage attention gate and TypeScript coverage passed.
      `npm run build:cloudflare` passed, rebuilt the C++ WASM package, packaged `.deploy`, and reported `ofg_cpp.wasm` size 244623 bytes.
      `git -c safe.directory=C:/dev/ofg diff --check` passed with only LF-to-CRLF warnings for edited files.
      Stale active bootstrap-renderer wording search passed for `AGENTS.md`, `README.md`, `docs/SYSTEMS.md`, `docs/API_CONTRACTS.md`, `cpp/CMakeLists.txt`, `tools`, and `src`.
    Visual artifacts: native demo smoke image at `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`; browser demo screenshot at `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`; focused C++ browser fixture screenshot at `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`; live dev-server screenshot at `C:\dev\ofg\artifacts\browser-smoke\dev-server-review.png`.
    Dev server: `npm run dev` is available for human review at `http://127.0.0.1:5174`.
    Remaining risks: no required milestone blockers remain. Known follow-ups are recorded in TODO and are not needed for this renderer-resource slice to be complete.

## Interfaces and Dependencies

The exact names may adjust during implementation, but the public shape should remain close to these interfaces until a later milestone deliberately revises it. The important constraint is that resources are mutable under explicit C++ owners, and device-bound resources are tied to one explicit `GpuContext` rather than a global device. These sketches use the current project style of returning nullable objects or `bool` and filling a caller-provided error string.

Minimal math layer:

    namespace ofg::math {

    struct Vec2 {
      float x = 0.0F;
      float y = 0.0F;
    };

    struct Vec3 {
      float x = 0.0F;
      float y = 0.0F;
      float z = 0.0F;
    };

    struct Vec4 {
      float x = 0.0F;
      float y = 0.0F;
      float z = 0.0F;
      float w = 0.0F;
    };

    class Mat4 {
     public:
      Vec4& operator[](std::size_t column) noexcept;
      const Vec4& operator[](std::size_t column) const noexcept;
      const float* data() const noexcept;

     private:
      std::array<Vec4, 4> m_columns{};
    };

    Vec2 vec2(float x, float y) noexcept;
    Vec3 vec3(float x, float y, float z) noexcept;
    Vec4 vec4(float x, float y, float z, float w) noexcept;
    float dot(Vec3 a, Vec3 b) noexcept;
    Vec3 cross(Vec3 a, Vec3 b) noexcept;
    std::optional<Vec3> normalize(Vec3 value, std::string& error);
    Mat4 mat4_identity() noexcept;
    Mat4 mat4_translation(Vec3 translation) noexcept;
    Mat4 mat4_scale(Vec3 scale) noexcept;
    Mat4 mat4_rotation_y(float radians) noexcept;
    std::optional<Mat4> perspective_rh(float fovy_radians, float aspect, float near_z, float far_z, std::string& error);
    std::optional<Mat4> look_at_rh(Vec3 eye, Vec3 target, Vec3 up, std::string& error);
    Mat4 mul(Mat4 a, Mat4 b) noexcept;
    Vec4 mul(Mat4 matrix, Vec4 vector) noexcept;
    std::array<float, 16> pack_mat4(Mat4 matrix) noexcept;

    } // namespace ofg::math

`Mat4` stores columns, so `matrix[3]` is the translation column for ordinary transform matrices, and `pack_mat4` writes the same 16-float order expected by a WGSL `mat4x4<f32>` uniform. Math helpers should stay deterministic and finite; invalid inputs such as zero-length normalization or bad projection planes report through the caller-provided error string instead of returning NaNs silently.

Stable resource owner:

    class ResourceArena {
     public:
      Texture& add_texture(Texture texture);
      Shader& add_shader(Shader shader);
      Material& add_material(Material material);
      Mesh& add_mesh(Mesh mesh);
      void clear();

      std::span<const std::unique_ptr<Texture>> textures() const noexcept;
      std::span<const std::unique_ptr<Shader>> shaders() const noexcept;
      std::span<const std::unique_ptr<Material>> materials() const noexcept;
      std::span<const std::unique_ptr<Mesh>> meshes() const noexcept;

     private:
      std::vector<std::unique_ptr<Texture>> m_textures;
      std::vector<std::unique_ptr<Shader>> m_shaders;
      std::vector<std::unique_ptr<Material>> m_materials;
      std::vector<std::unique_ptr<Mesh>> m_meshes;
    };

`ResourceArena` is intentionally small. It provides stable ownership and bulk teardown for the first renderer slice; it does not provide lookup by name, hot-reload routing, dependency tracking, async loading, or removal while draw lists may still point at resources.

GPU context:

    struct GpuContext {
      WGPUDevice m_device = nullptr;
      WGPUQueue m_queue = nullptr;
    };

Device-bound resources store this borrowed context after creation or preparation. They do not own or release the device or queue, and they become invalid when the owning `Game` device lifetime ends.

Shared property data:

    using PropertyValue =
        std::variant<float, math::Vec2, math::Vec3, math::Vec4, math::Mat4, Texture*>;

    class PropertyBag {
     public:
      void set(std::string name, PropertyValue value);
      const PropertyValue* get(std::string_view name) const;
      bool validate_for_scope(const Shader& shader, ShaderParameterScope scope, std::string& error) const;
      std::optional<std::vector<std::byte>> pack_uniforms_for_scope(
          const Shader& shader,
          ShaderParameterScope scope,
          std::string& error) const;
    };

Texture resource:

    enum class TextureColorSpace { Srgb, Linear };
    enum class TexturePixelFormat { Rgba8, Rgba8Srgb };
    enum class MipMapPolicy { None, GenerateCpuFullChain };

    class Texture {
     public:
      static std::optional<Texture> from_rgba8_pixels(
          GpuContext gpu,
          std::string label,
          uint32_t width,
          uint32_t height,
          TextureColorSpace color_space,
          std::vector<std::byte> pixels,
          MipMapPolicy mip_map_policy,
          std::string& error);

      bool update_pixels(std::vector<std::byte> pixels, std::string& error);
      uint32_t mip_level_count() const noexcept;
      WGPUTextureView view() const;
      WGPUSampler sampler() const;
    };

Shader and material interfaces:

    class Shader {
     public:
      static std::optional<Shader> create(
          GpuContext gpu,
          std::string label,
          std::string wgsl_source,
          ShaderParameterLayout parameter_layout,
          std::vector<PipelineDefinition> pipelines,
          std::string& error);

      bool replace_source(std::string wgsl_source, std::string& error);
      const ShaderParameter* parameter(std::string_view name) const;
      WGPUShaderModule module() const;
      uint64_t revision() const;
    };

    class Material {
     public:
      static std::optional<Material> create(
          GpuContext gpu,
          std::string label,
          Shader& shader,
          PropertyBag properties,
          std::string& error);

      bool set_property(
          std::string name,
          PropertyValue value,
          std::string& error);

      const Shader& shader() const;
      WGPUBindGroup bind_group() const;
      uint64_t revision() const;
    };

Mesh interface:

    struct MeshVertex {
      float position[3];
      float normal[3];
      float uv[2];
    };

    struct SubMesh {
      std::string label;
      uint32_t index_start = 0;
      uint32_t index_count = 0;
      Material* default_material = nullptr;
    };

    class Mesh {
     public:
      static std::optional<Mesh> create(
          GpuContext gpu,
          std::string label,
          std::vector<MeshVertex> vertices,
          std::vector<uint32_t> indices,
          std::vector<SubMesh> submeshes,
          std::string& error);

      bool replace_vertices(std::vector<MeshVertex> vertices, std::string& error);
      bool replace_indices(
          std::vector<uint32_t> indices,
          std::vector<SubMesh> submeshes,
          std::string& error);
      WGPUBuffer vertex_buffer() const;
      WGPUBuffer index_buffer() const;
      std::span<const SubMesh> submeshes() const;
      uint64_t revision() const;
    };

Renderer interface:

    struct MaterialOverride {
      uint32_t submesh_index = 0;
      Material* material = nullptr;
    };

    struct DrawCommand {
      Mesh* mesh = nullptr;
      math::Mat4 model;
      PropertyBag properties;
      std::vector<MaterialOverride> material_overrides;
      math::Vec3 sort_origin;
    };

    class Renderer {
     public:
      static std::unique_ptr<Renderer> create(
          GpuContext gpu,
          WGPUTextureFormat color_format,
          std::string& error);
      bool resize(uint32_t width, uint32_t height, std::string& error);
      bool render(
          WGPUCommandEncoder encoder,
          WGPUTextureView target,
          const RenderView& view,
          const DrawList& draw_list,
          std::string& error);
      RendererCounters counters() const;
    };

Initial shader parameters:

Frame scope, bind group 0:

    view_projection: mat4x4<f32>

Draw scope, bind group 1:

    model: mat4x4<f32>

CPU-side `math::Mat4` values must pack directly into these WGSL matrix uniforms without a row/column transpose step.

Material scope, bind group 2:

    base_color_factor: vec4<f32>
    base_color_texture: texture_2d<f32>
    base_color_sampler: sampler derived from base_color_texture

Initial shader material path:

    All opaque materials bind base_color_factor, base_color_texture, and base_color_sampler.
    Colored cube materials use a generated 1x1 white texture plus their base_color_factor.
    The generated checker ground material uses a mipmapped checker texture plus a white base_color_factor.

The first renderer slice should avoid shader variants unless implementation proves the single always-textured path is more complex than two variants. If variants are introduced, their keys must include shader revision, material layout, target format, depth format, vertex layout, primitive state, sample count, and any compile-time shader constants.
