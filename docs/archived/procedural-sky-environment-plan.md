# Add Procedural Sky and Environment Rendering

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

OFG needs a procedural sky that immediately gives the world a real atmosphere: sun, moon, stars, sunrise, sunset, a day/night cycle, and non-volumetric weather hints such as clear, partly cloudy, and stormy skies. The first implementation should be a real renderer feature, not a painted background. It should fit the current C++/WebGPU render path and leave room for later aerial perspective, distance fade, and volumetric clouds. Before the sky is added, the renderer should gain an HDR linear scene-color target and a final tone-mapping pass so bright sun/sky values can be represented without clipping in the main lighting passes.

After this plan is implemented, running the browser or native smoke scene should show the existing PBR-lit ground/cubes/player against a procedural sky. Opaque meshes and sky render into a half-precision linear buffer, then the final pass maps that HDR color to sRGB output. The sky should change over time, include a visible sun in daytime, a moon and stars at night, and a cheap procedural cloud layer whose coverage and darkness can be driven by weather parameters. Volumetric clouds are intentionally out of scope for this first pass.

## Progress

- [x] (2026-07-04) Completed online research pass for modern sky, atmosphere, cloud, sun/moon/star, and weather-control techniques.
- [x] (2026-07-04) Read the current renderer, opaque pass, camera, scene, game, smoke-contract, and API-contract code to place the design in OFG's architecture.
- [x] (2026-07-04) Revised ownership so `Environment` is a direct `Scene` member rather than a `Game` member.
- [x] (2026-07-04) Added Milestone 0 for HDR linear scene color and final tone mapping before sky rendering.
- [x] (2026-07-04) Reviewed this plan with five sub-agents and accepted the major correctness/completeness/performance findings.
- [x] (2026-07-04) Revised lighting design so direct lights are entity-owned `Light` components and ambient lighting belongs to `Environment`.
- [x] (2026-07-04) Added `Environment` ownership of the sun light through a safe `Ptr<Light>`, with environment-owned first-directional fallback scanning.
- [x] (2026-07-04) Implemented Milestone 0: HDR scene-color/depth targets, tone-map pass, output encoding, renderer observability, and tone-mapped smoke contract.
- [x] (2026-07-04) Implemented Milestone 1: `Light` component, scene-owned `Environment`, renderer `LightProperties` extraction, demo sun light, docs, tests, and review fix.
- [x] (2026-07-04) Implemented Milestone 2: `SkyPass`, clear analytic sky, sun disc/halo, shared scene render pass, sky-aware smoke background reference, docs, tests, screenshot, and review fix.
- [x] (2026-07-04) Implemented Milestone 3: procedural clouds, moon, stars, deterministic environment presets, active docs, shader quality constants, screenshot/report, and review fix.
- [x] (2026-07-04) Implemented Milestone 4: smoke schema cleanup, browser/native/full smoke, coverage gate expansion, refreshed coverage summaries, screenshots, and dev-server review URL.

## Surprises & Discoveries

- Observation: `OpaquePass` currently owns the depth texture privately.
  Evidence: `C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp` stores `m_depth_texture` and `m_depth_view`; `Renderer` only owns `std::vector<std::unique_ptr<OpaquePass>>`.

- Observation: A sky pass after opaque rendering is feasible with the current depth convention if it draws a full-screen triangle at depth 1.0 and uses depth compare `Equal`.
  Evidence: `OpaquePass` clears depth to `1.0F` and opaque pipelines use `WGPUCompareFunction_Less`, so untouched background pixels remain exactly at the clear depth while visible opaque pixels store values less than 1.0.

- Observation: The current smoke contract treats the old dark clear color as the background.
  Evidence: `C:\dev\ofg\tools\smoke-contract.json` includes `clearColorRgba8` and background classification thresholds; `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` describe the visual contract as a dark blue-gray background.

- Observation: The current renderer writes directly into the platform target format, usually `Rgba8Unorm` in native smoke and a browser canvas format in WebGPU.
  Evidence: `Renderer::create` receives the platform color format, `OpaquePass::create` currently uses that same format for its render pipeline, and native smoke uses `WGPUTextureFormat_RGBA8Unorm`.

- Observation: ACES plus manual sRGB output changes the visible clear/background bytes from `[27, 37, 50, 255]` to `[103, 125, 147, 255]` for the current native `Rgba8Unorm` smoke target.
  Evidence: the first Milestone 0 native smoke render wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` but failed the old background classifier with background coverage `0.0`; sampling the top-left background pixel returned `103,125,147,255`, and updating `C:\dev\ofg\tools\smoke-contract.json` made `npm run smoke:render` pass.

- Observation: A live `Ptr<Light>` can still point at a light from a different scene; safe pointer liveness alone is not enough to prove scene membership.
  Evidence: the Milestone 1 review found `Environment::update` only scanned when `m_main_directional_light` was null. `C:\dev\ofg\cpp\src\scene\environment.cpp` now drops a non-null sun pointer that is not present in `scene.light_count()` / `scene.get_light(index)`, then adopts the first local directional light.

- Observation: Once the sky pass writes background pixels, the smoke classifier's previous `clearColorRgba8` field can no longer mean both "renderer clear color" and "expected background color."
  Evidence: the first Milestone 2 `npm run smoke:render` wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` with visible sky but failed with `Background coverage too low: 0.000000` against the old `[103,125,147,255]` tone-mapped clear. Top sky pixels sampled around `[198,216,236,255]`. `tools/smoke-contract.json` now keeps `clearColorRgba8` as `[27,37,50,255]` and adds `backgroundReferenceRgba8` as `[198,216,236,255]`; smoke wrappers classify with the background reference.

- Observation: The C++ coverage wrapper's render-file allowlist did not include the new render target and pass source files.
  Evidence: after Milestone 4 coverage initially passed, a manual coverage-summary audit showed `cpp/src/render/depth_target.cpp`, `lighting.cpp`, `renderer_counters.cpp`, `scene_color_target.cpp`, `sky_pass.cpp`, and `tone_map_pass.cpp` were not checked by `tools/cpp-coverage.mjs`. The wrapper now gates every `cpp/src/render/*.cpp` file, and targeted tests plus documented defensive-line exclusions make the stricter gate pass.

## Decision Log

- Decision: Add Milestone 0 for a half-precision linear scene-color buffer and a final tone-map pass.
  Rationale: The sky needs HDR headroom for sun discs, halos, sunset scattering, cloud highlights, and later exposure work. Putting this first makes the rest of the sky work operate in the correct color space instead of baking LDR compromises into the shader.
  Date/Author: 2026-07-04 / Codex, based on user direction.

- Decision: Use `WGPUTextureFormat_RGBA16Float` as the initial HDR scene-color format.
  Rationale: It is the practical WebGPU baseline format for a renderable half-precision linear color target and provides enough range for early sky/PBR lighting without introducing optional GPU features.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use an ACES-fitted tone-mapping curve for the first final pass.
  Rationale: ACES fitted tone mapping is compact, stable, handles bright skies gracefully, and can be replaced later by AgX, exposure adaptation, or a more authored filmic curve behind the same `ToneMapPass` interface.
  Date/Author: 2026-07-04 / Codex.

- Decision: Add a new `Environment` object owned directly by `Scene`, not by `Game` and not as an entity/component.
  Rationale: The environment is part of world state and should travel with the scene that owns cameras, entities, ambient/world lighting, sky state, and weather. This avoids an extra snapshot handoff and avoids over-rigid ECS modeling for global systems.
  Date/Author: 2026-07-04 / Codex, based on user direction.

- Decision: Add a scene `Light` component for direct lights, with only `LightType::Directional` in the first version.
  Rationale: Direct lighting should be normal scene state attached to entities rather than hidden inside `Environment`. The owning entity's world `+Z` forward direction defines the light direction. Ambient lighting remains global/world state owned by `Environment`.
  Date/Author: 2026-07-04 / Codex, based on user direction.

- Decision: Let `Environment` store the current sun directional light as a safe `Ptr<Light>`.
  Rationale: The visible sun, sky scattering direction, and primary directional light should stay coordinated while the light remains an ordinary scene component. If the environment pointer is null or its target is destroyed, `Environment::update` scans the scene and adopts the first directional `Light` component in creation order.
  Date/Author: 2026-07-04 / Codex, based on user direction.

- Decision: Keep the renderer dumb about environment simulation and feed it render-state light data.
  Rationale: `Environment` may control the current sun light entity's orientation, and possibly its cosmetic position, but renderer code should only consume a list of `LightProperties`, analogous to how it consumes `CameraProperties`. The first list contains at most one directional light property, translated from the environment's current sun `Light` component: direction from entity world `+Z`, color, and intensity. Future day-time-driven shadow softness belongs on `LightProperties`, `Light`, or a shadow subsystem once shadows exist.
  Date/Author: 2026-07-04 / Codex, based on user clarification.

- Decision: Treat `SkyPass` as a logical renderer pass, but encode opaque draws and sky draws inside one WebGPU scene render pass when possible.
  Rationale: The sky still runs after opaque draws and uses the opaque depth buffer, but a single WebGPU render pass avoids an extra `RGBA16Float` color load/store and avoids storing depth twice.
  Date/Author: 2026-07-04 / Codex.

- Decision: Start with an analytic, shader-only sky model rather than Hillaire/Bruneton LUT precomputation.
  Rationale: OFG needs visible sky/weather progress now, while full atmospheric LUTs are a larger renderer milestone. The pass and environment interfaces should be shaped so a future physically based LUT implementation can replace `clear_sky_radiance()` without changing game/environment ownership.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use procedural stars first, not a real star catalog.
  Rationale: A procedural hash starfield is deterministic, asset-free, and good enough for a plausible first night sky. A later real catalog can be added as a GPU buffer or generated texture behind the same sky pass.
  Date/Author: 2026-07-04 / Codex.

- Decision: Factor depth ownership out of `OpaquePass` into a renderer-owned depth target.
  Rationale: Both opaque and sky passes need the same depth attachment. Future distance fade will also need shared access to scene depth, so depth should belong to the frame renderer rather than one pass.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use narrow line-level coverage exclusions for untestable WebGPU defensive branches in the new pass/target files.
  Rationale: `SceneColorTarget`, `SkyPass`, and `ToneMapPass` contain guard code for WebGPU resource creation returning null, impossible tangent overflow, and partial-creation cleanup. Normal resize, move, validation, pass creation, uniform packing, draw/render, and bind-group reuse behavior is covered directly by doctests and by browser/native smoke, so the coverage wrapper excludes only those defensive lines while still gating the files.
  Date/Author: 2026-07-04 / Codex.

## Outcomes & Retrospective

Milestone 0 is implemented. The renderer now owns `SceneColorTarget` (`RGBA16Float`), `DepthTarget`, `OpaquePass`, and `ToneMapPass`. `OpaquePass` no longer owns depth and now renders into the HDR scene-color target; `ToneMapPass` uses a fullscreen triangle, `textureLoad`, ACES-fitted tone mapping, and explicit output encoding (`ManualSrgb` for non-sRGB targets, `LinearOutput` for `*Srgb` targets). Renderer counters now include durable texture, texture-view, bind-group-layout, bind-group, shader-module, pipeline, and buffer creation counts.

Milestone review:

- Scope: Milestone 0 HDR scene color, shared depth, final tone mapping, renderer observability, smoke contract update, and active docs updates.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agent tools were available, but this automatic plan gate did not have an explicit user request for delegated/sub-agent work, so no sub-agents were spawned.
- Required findings fixed: `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` still described direct-to-target/byte-identical clear rendering; both now describe HDR scene color plus final tone mapping. `tools/smoke-contract.json` still used the pre-tone-map clear bytes; it now uses `[103, 125, 147, 255]`.
- Follow-ups recorded: `docs/ARCHITECTURE.md` is absent, so the review used `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `PLANS.md`, `AGENTS.md`, and the active ExecPlan as the authoritative architecture context.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check`, `npm run test:cpp`, and `npm run smoke:render` passed.
- Artifacts: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; the report shows `passed: true`, `backgroundRatio: 0.430462`, `sceneRatio: 0.569538`, and `textureFormat: Rgba8Unorm`.
- Remaining risk: browser smoke and full coverage are still pending for later milestones; Milestone 1 has not yet replaced scene direct-light storage with `Light` and `Environment`.

Milestone 1 is implemented. `Scene` now owns `Light` components and one `Environment`; `Scene::main_light`, `Scene::set_main_light`, `Scene::ambient_light`, and `Scene::set_ambient_light` are retired from the active C++ API. `Environment` owns ambient/weather/day-night state and a safe pointer to the current sun light, adopts the first local directional light during update when needed, rotates the sun entity so world `+Z` is the directional light travel direction, and updates the selected light color/intensity. The renderer builds a transient one-item `LightProperties` list from `scene.environment().main_directional_light()` and passes that plus `scene.environment().ambient_light()` into `OpaquePass`. The demo scene now creates a directional sun `Light` entity. `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` now describe scene-owned `Light`, scene-owned `Environment`, and renderer-facing `LightProperties`.

Milestone review:

- Scope: Milestone 1 `Light` component, `Environment`, scene storage/update order, renderer extraction, opaque pass light inputs, demo sun setup, tests, and docs.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agent tools were available, but this automatic plan gate did not have an explicit user request for delegated/sub-agent work, so no sub-agents were spawned.
- Required findings fixed: `Environment::update` accepted a live foreign-scene sun `Light` pointer without rescanning because the safe pointer was non-null. It now verifies the selected light belongs to the updating scene, clears foreign selections, and then adopts the first local directional light. `cpp/tests/scene_test.cpp` covers the foreign-light recovery path.
- Follow-ups recorded: `docs/ARCHITECTURE.md` is absent, so the review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and this ExecPlan as the authoritative architecture context. `cpp/tests/scene_test.cpp` is now 866 lines and should be watched for split pressure, though it remains below the repository's hard concern threshold.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check`, `npm run test:cpp`, and `npm run smoke:render` passed after the review fix.
- Artifacts: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; the report shows `passed: true`, `backgroundRatio: 0.430462`, `sceneRatio: 0.569538`, and `textureFormat: Rgba8Unorm`.
- Remaining risk: browser smoke and full coverage remain pending for later milestones; the sky pass itself has not yet been added.

Milestone 2 is implemented. `Renderer` now owns `SkyPass` and owns the scene render pass boundary: it clears the HDR scene-color target and shared depth, asks `OpaquePass` to draw opaque geometry, asks `SkyPass` to draw a fullscreen triangle at depth `1.0` with depth compare `Equal`, and then runs `ToneMapPass`. `SkyPass` owns a durable shader module, bind group layout, pipeline layout, render pipeline, uniform buffer, bind group, and counters. `build_sky_pass_uniforms` packs camera basis vectors, field-of-view scale, environment sun/moon directions, weather hints, and the first directional light's sun color/intensity for both tests and the WGSL shader. The first sky shader renders an HDR analytic clear sky gradient, horizon/sunset warmth, sun disc, and sun halo. `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` now describe procedural sky as part of the active visual contract.

Milestone review:

- Scope: Milestone 2 `SkyPass`, shared scene render pass boundary, `OpaquePass::draw`, sky WGSL, renderer counters/tests, smoke contract, smoke wrappers, active docs, and native screenshot/report.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agent tools were available, but this automatic plan gate did not have an explicit user request for delegated/sub-agent work, so no sub-agents were spawned.
- Required findings fixed: `tools/smoke-contract.json` was briefly using `clearColorRgba8` as the sky background reference, which would have broken the bootstrap clear-color contract and left stale smoke terminology. It now keeps `clearColorRgba8` for the renderer clear baseline and adds `backgroundReferenceRgba8` for sky/background classification. `tools/browser-smoke.mjs`, `tools/browser-smoke-cpp.mjs`, and `tools/smoke-render-cpp.mjs` prefer `backgroundReferenceRgba8` with fallback to the old field. Native smoke comments and bootstrap clear-color test wording were updated to avoid implying the sky reference is a clear color.
- Follow-ups recorded: the native render-smoke CLI/report still use the legacy `--clear-color-rgba8` flag and `clearColor` report field as compatibility names while receiving the background reference; Milestone 4 should finish the schema rename across smoke tooling and reports.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check -- <Milestone 2 paths>`, and `npm run smoke:render` passed after the review fix.
- Artifacts: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; the report shows `passed: true`, `backgroundRatio: 0.430462`, `sceneRatio: 0.569538`, `clearColor: [198,216,236,255]` as the current compatibility-named background reference, and `textureFormat: Rgba8Unorm`.
- Remaining risk: browser smoke and full coverage remain pending for later milestones; clouds, moon, stars, and deterministic presets have not yet been added.

Milestone 3 is implemented. `Environment` now exposes deterministic `EnvironmentPreset` values for daylight, sunset, night, and storm states. Each preset configures a stable day phase, cloud/weather controls, moon phase, and procedural star seed so tests and smoke tools can author known sky states without relying on wall-clock time. `SkyPass` now packs expanded sky uniforms for moon direction/phase, day/twilight factors, haze, time, cloud coverage, storm intensity, cloud opacity, precipitation hint, wind, cloud scale/height/sharpness, and star seed. The WGSL sky shader keeps the analytic clear-sky/sun path and adds a cheap non-volumetric cloud layer, moon disc/halo, and deterministic procedural stars. `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` now describe clouds, moon, stars, presets, weather controls, and the procedural sky pass as active contracts.

Milestone review:

- Scope: Milestone 3 `EnvironmentPreset`, `SkyWeather` packing, procedural cloud/moon/star WGSL, sky uniform tests, active docs, shader quality limits, native screenshot/report, and validation evidence.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agent tools were available, but this automatic plan gate did not have an explicit user request for delegated/sub-agent work, so no sub-agents were spawned.
- Required findings fixed: active docs described the sky pass generically but did not yet name the M3 cloud/moon/star/preset/weather contract; `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` now do. The WGSL shader had the agreed quality limits as literals; it now names the FBM octave count plus clear-cloud, horizon, storm-detail, and star-night early-outs.
- Follow-ups recorded: the native render-smoke CLI/report still use `--clear-color-rgba8` and `clearColor` as compatibility names for the sky background reference; Milestone 4 remains responsible for finishing that smoke schema rename and browser/full coverage validation.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run smoke:render` passed after the review fixes.
- Artifacts: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; the report shows `passed: true`, `backgroundRatio: 0.430462`, `sceneRatio: 0.569538`, `clearColor: [198,216,236,255]` as the current compatibility-named background reference, and `textureFormat: Rgba8Unorm`.
- Remaining risk: browser smoke, full smoke, and full coverage remain pending for Milestone 4.

Milestone 4 is implemented. Native smoke now accepts `--background-reference-rgba8`, `SmokeContract` stores `m_background_reference_rgba8`, and the native report writes `backgroundReferenceRgba8` instead of the stale `clearColor` compatibility name. `clearColorRgba8` remains in `tools/smoke-contract.json` as the bootstrap clear baseline, while browser and native classifiers use `backgroundReferenceRgba8` for sky-background classification. Browser smoke, native smoke, full smoke, `npm test`, and `npm run coverage` all pass. Coverage summaries were copied from `artifacts/coverage` to `docs/coverage`, and `docs/coverage/latest.md` records the July 4 run. The local dev server is running for review at `http://127.0.0.1:5173`.

Final validation:

- `npm run build` passed.
- `npm run smoke:browser` passed and wrote `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` plus `C:\dev\ofg\artifacts\browser-smoke\report.json`; report pixels show `backgroundRatio: 0.4186267166042447`, `sceneRatio: 0.5813732833957553`, and `nonBackgroundColorBuckets: 15`.
- `npm run smoke:render` passed and wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json`; report pixels show `backgroundRatio: 0.430462`, `sceneRatio: 0.569538`, `backgroundReferenceRgba8: [198,216,236,255]`, and `textureFormat: Rgba8Unorm`.
- `npm test` passed: native C++ doctests and 28 TypeScript Mocha tests.
- `npm run smoke` passed after the native smoke schema rename and coverage-test additions.
- `npm run coverage` passed with every checked C++ file above the 90% gate after the render gate was expanded to all `cpp/src/render/*.cpp`; TypeScript checked files also passed. The new direct tests cover target move/release, tone-map pass validation/render/bind-group reuse, sky pass validation/draw, light extraction edge cases, and environment validation edges.
- `git -c safe.directory=C:/dev/ofg diff --check` passed with only Git line-ending warnings.

Final artifacts:

- Browser screenshot: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`
- Browser report: `C:\dev\ofg\artifacts\browser-smoke\report.json`
- Native screenshot: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`
- Native report: `C:\dev\ofg\artifacts\render-smoke\report.json`
- Dev-server log: `C:\dev\ofg\artifacts\dev-server\procedural-sky-dev-server.out.log`
- Coverage summaries: `C:\dev\ofg\docs\coverage\cpp-summary.json`, `C:\dev\ofg\docs\coverage\ts-coverage-summary.json`, and `C:\dev\ofg\docs\coverage\latest.md`

Remaining risk: the first sky model is intentionally non-volumetric and daylight clouds are subtle in the default smoke view. Future distance fade/aerial perspective, volumetric clouds, shadow softness, exposure controls, and real star catalogs remain future work.

## Contract and Quality Baseline

`OFG-BOOT-001 TypeScript Host Ownership` must be preserved. TypeScript must not own sky simulation, weather state, render passes, GPU resources, or draw submission.

`OFG-BOOT-002 C++ Runtime Ownership` changes by adding a C++ `Environment` object owned by `Scene` and a scene-owned `Light` component. The contract must be updated after Milestone 1.

`OFG-BOOT-004 Renderer Compatibility` changes because browser and native smoke must validate the same HDR-to-sRGB tone-mapped renderer path. Later milestones also change the visual background from the dark blue-gray clear color to procedural sky; background classification should become sky-aware.

`OFG-BOOT-005 WebGPU Baseline` changes because the renderer will contain a render-to-texture HDR scene target, a final tone-map pass, and later at least three render passes: opaque PBR, procedural sky, and tone mapping. The renderer must still request no optional GPU features or custom adapter limits.

`OFG-BOOT-006 Resource Lifetime` must be preserved. Scene-color/depth textures are resized only when the target size changes. Tone-map and sky buffers, bind group layouts, shader modules, and pipelines are durable pass resources. Bind groups that capture size-dependent texture views may be recreated only when those views change. Per-frame work may update uniform buffers but must not recreate pipelines, layouts, shader modules, or size-independent bind groups during steady-state frames.

`OFG-BOOT-009 Coverage` applies. Each modified implementation file must pass the default coverage attention gate, currently about 90% line coverage unless this plan records an explicit exception with rationale.

## Context and Orientation

The current render path is entirely C++ owned. `Game::render` in `C:\dev\ofg\cpp\src\game\game.cpp` validates the target and calls `Renderer::render`. `Renderer::render_impl` in `C:\dev\ofg\cpp\src\render\renderer.cpp` resolves the scene camera into `CameraProperties`, builds a transient `DrawList` from visible `MeshRenderer` components, and then calls the current opaque pass.

`OpaquePass` in `C:\dev\ofg\cpp\src\render\opaque_pass.cpp` currently owns the frame uniform buffer, draw uniform buffer, depth texture, depth view, and `PipelineCache`. It clears the color target and depth target, draws opaque meshes, and stores both color and depth. Its pipeline uses `WGPUCompareFunction_Less` and writes depth. Today the color target is the platform surface/offscreen target; Milestone 0 changes that so opaque draws write to a renderer-owned `RGBA16Float` scene-color texture instead.

`CameraProperties` in `C:\dev\ofg\cpp\include\ofg\render\camera_properties.hpp` uses OFG's left-handed coordinate system: `+X` is right, `+Y` is up, and camera-local `+Z` is forward. The sky shader should use `world_from_camera` columns directly so it reconstructs rays consistently with the existing camera.

`Scene` in `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp` currently stores one main directional light and one ambient light. Replace that direct-light storage with scene-owned `Light` components and a scene-owned `Environment`. `Environment` owns ambient light, sky state, time of day, weather, and a safe pointer to the current sun light. Environment update logic may rotate the current sun light entity, and may later move that entity for cosmetic sun-disc placement, but the renderer should not know or care why the light is oriented that way. During its own update, `Environment` is responsible for detecting a null or destroyed sun pointer and scanning the scene for the first directional `Light`. The renderer converts the resulting renderable lights into a transient `LightProperties` list, the same kind of entity-to-render-state translation that `CameraProperties` provides for cameras. The first renderer version emits at most one directional `LightProperties` item. The renderer resolves ambient light from `scene.environment()`.

`Game` in `C:\dev\ofg\cpp\include\ofg\game\game.hpp` currently owns `FrameState`, `DemoScene`, `ControlInput`, and the current `Scene`. It should not own environment simulation. `Game::update_impl` should continue to build `SceneUpdateContext` and call `m_current_scene->update(context)`; `Scene::update` should tick its own environment before gameplay/render-dependent components observe the frame.

The texture resource path already distinguishes base-color sRGB textures from linear data. `TextureColorSpace::Srgb` creates `WGPUTextureFormat_RGBA8UnormSrgb`, so sampled base-color textures are decoded to linear values before the PBR shader combines them with lighting. The HDR scene buffer should therefore store linear shader outputs.

The smoke contract in `C:\dev\ofg\tools\smoke-contract.json` and docs in `C:\dev\ofg\docs\API_CONTRACTS.md` / `C:\dev\ofg\docs\SYSTEMS.md` must be updated twice: first to describe HDR scene color plus final tone mapping, and later because the background visual changes from clear color to procedural sky.

## Rendering Algorithm

### HDR Scene Color and Tone Mapping

Milestone 0 changes the renderer from direct-to-platform rendering to a small render graph:

    opaque PBR pass -> RGBA16Float scene color
    tone-map pass   -> platform color target

Later sky work extends the graph:

    scene render pass:
        opaque PBR draws -> RGBA16Float scene color + depth
        sky full-screen draw -> remaining far-depth pixels in same scene color
    tone-map pass   -> platform color target

Add a renderer-owned scene color target with:

- color format: `WGPUTextureFormat_RGBA16Float`;
- usage: `WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding`;
- size: current render target width and height;
- one mip level, one array layer, one sample;
- a default 2D texture view.

The scene color target is recreated only on nonzero resize changes and released on zero-size resize or renderer release. `OpaquePass` should be created with the scene color format, not the platform color format. `Renderer::render_impl` should render opaque content into `RenderTarget{scene_color_view, WGPUTextureFormat_RGBA16Float, width, height}`, then run `ToneMapPass` into the caller-provided platform target.

Add `ToneMapPass` with a full-screen triangle and no depth attachment. It samples the HDR scene color with `textureLoad`, not a filtering sampler, so the first pass does not depend on optional filterability behavior for float textures. The fragment shader uses pixel coordinates from `@builtin(position)`:

    let pixel = vec2<i32>(floor(input.position.xy));
    let hdr_color = max(textureLoad(scene_color, pixel, 0).rgb, vec3<f32>(0.0));

Apply exposure and ACES-fitted tone mapping:

    let exposed = hdr_color * exposure;
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    let mapped = clamp((exposed * (a * exposed + b)) /
                       (exposed * (c * exposed + d) + e),
                       vec3<f32>(0.0),
                       vec3<f32>(1.0));

For non-sRGB `RGBA8Unorm` / `BGRA8Unorm` style platform targets, manually encode the tone-mapped linear color to sRGB:

    linear_to_srgb(channel) =
        channel * 12.92, if channel <= 0.0031308
        1.055 * pow(channel, 1.0 / 2.4) - 0.055, otherwise

Milestone 0 must implement the output encoding decision from the actual platform target format, because the browser format selection can fall back to `*Srgb` formats. Add a small `ToneMapOutputEncoding` mode:

- `ManualSrgb`: used for non-sRGB `RGBA8Unorm` / `BGRA8Unorm` targets; the shader applies `linear_to_srgb`.
- `LinearOutput`: used for `RGBA8UnormSrgb` / `BGRA8UnormSrgb` targets; the shader writes linear tone-mapped color and lets the render target encode.

Tests must cover all four current output formats so the renderer cannot double-encode.

The initial tone-map uniform can be one 16-byte row:

    vec4 tone_map; // exposure, output_encoding_mode, unused, unused

Default exposure should be `1.0`. The pass should expose C++ helpers for later tests and tuning, but no user-facing UI is required in this plan.

### Procedural Sky Pass

The first sky renderer is a full-screen procedural draw. It does not render a cube mesh and does not use a cubemap. It computes a world-space view ray for each pixel and evaluates sky, celestial, and cloud functions in the fragment shader. Sky color is HDR linear radiance-like RGB written into the `RGBA16Float` scene color target; it is not clamped to display range inside the sky draw.

The opaque draw sequence runs first and clears depth to 1.0. It writes normal opaque geometry depth values less than 1.0. The sky draw runs after opaque draws inside the same WebGPU scene render pass whenever the current pass abstraction allows it:

- color attachment: the HDR scene color target, clear once before opaque, store once after sky;
- depth attachment: renderer-owned depth view, clear once before opaque, store once after the scene pass only if a later pass needs depth;
- sky pipeline depth state: `depthWriteEnabled = false`, `depthCompare = Equal`;
- sky vertex output position: full-screen triangle with clip-space `z = 1.0` and `w = 1.0`.

If implementation pressure keeps sky as a separate WebGPU render pass temporarily, record that in the Decision Log with a bandwidth estimate and add a native smoke timing comparison before marking the milestone complete.

This means the sky shader only shades pixels where no opaque geometry was drawn. If the scene contains no mesh draws, every pixel still has depth 1.0 and the sky fills the target.

The full-screen vertex shader uses vertex index only:

    positions[0] = vec2(-1, -1)
    positions[1] = vec2( 3, -1)
    positions[2] = vec2(-1,  3)
    clip_position = vec4(position.xy, 1.0, 1.0)

The fragment shader reconstructs a ray from interpolated clip-space xy:

    camera_ray = normalize(vec3(
        ndc.x * aspect * tan(vertical_fov / 2),
        ndc.y * tan(vertical_fov / 2),
        1.0))

    world_ray = normalize(
        camera_right   * camera_ray.x +
        camera_up      * camera_ray.y +
        camera_forward * camera_ray.z)

The CPU supplies `camera_right`, `camera_up`, and `camera_forward` from `CameraProperties::world_from_camera` columns 0, 1, and 2. This avoids matrix inversion in WGSL and keeps the shader aligned with OFG's `+Z` camera convention.

### Light Component and Sun Selection

Add `C:\dev\ofg\cpp\include\ofg\scene\light.hpp` and `C:\dev\ofg\cpp\src\scene\light.cpp`.

`Light` is a scene component, not an environment field. Extend `ComponentType` and `Scene::create_component` in the same style as the existing `MeshRenderer`, `Camera`, `Player`, and `AnimationPlayer` components. The first implementation supports only:

    enum class LightType {
        Directional,
    };

Each `Light` stores:

- `LightType m_type`, initially always `Directional`;
- linear `math::Vec3 m_color`;
- non-negative `float m_intensity`;
- an enabled/visible flag if that matches existing component style.

For a directional light, the owning entity's world `+Z` forward direction is the direction the light travels. This matches the current PBR shader convention where the shader uses `-main_light_direction` as the vector from surface to light.

`Scene` should own `Light` components the same way it owns `MeshRenderer`, `Camera`, `Player`, and `AnimationPlayer` components:

- `std::size_t light_count() const noexcept;`
- `Light* get_light(std::size_t index) noexcept;`
- `const Light* get_light(std::size_t index) const noexcept;`

`Environment` stores a safe `Ptr<Light>` for the current main directional light, also called the sun light. The pointer may be null. During `Environment::update`, the environment resolves its own effective sun light as:

1. `Environment::main_directional_light()` when it references a live directional `Light`;
2. otherwise scan `Scene` light components in creation order and store the first directional `Light`;
3. otherwise no direct sun light, with direct PBR contribution set to zero while ambient/sky state remains valid.

The demo scene must create a directional light entity. In the default procedural day/night mode, `Environment::update` computes the desired observer-to-sun direction and rotates the effective sun light entity so the entity's world `+Z` points in the opposite direction, the direction light travels. Environment may also set the light entity position later for visual sun/moon helpers, but that position has no lighting meaning for a directional light. The renderer still derives the final direct-light direction only from the light entity transform; environment only drives scene state for the default simulation.

### Environment Simulation

Add `C:\dev\ofg\cpp\include\ofg\scene\environment.hpp` and `C:\dev\ofg\cpp\src\scene\environment.cpp`.

`Environment` owns time-of-day and weather simulation inputs:

- `m_day_cycle_seconds`: default first value should be long enough to inspect, for example 600 seconds.
- `m_day_phase_offset`: normalized offset in `[0, 1)`, chosen so the demo starts in readable daylight.
- `m_sun_azimuth_radians`: horizontal direction of the sun path in OFG world space.
- `m_main_directional_light`: safe pointer to the explicitly selected or environment-discovered sun `Light`; null means the next `Environment::update` scans scene light components for the first directional-light fallback.
- `m_weather`: cloud coverage, storm intensity, haze, precipitation hint, wind direction, wind speed, cloud scale, cloud height, cloud opacity, cloud sharpness.
- `m_moon_phase`: simple normalized phase in `[0, 1]`; first default can be a mostly full moon.
- `m_star_seed`: deterministic seed for procedural stars.

Weather ranges and units are part of the contract. `cloud_coverage`, `storm_intensity`, `haze`, `precipitation_hint`, `cloud_opacity`, and `cloud_sharpness` are normalized `[0, 1]`. Wind direction is a normalized XZ vector or zero. Wind speed is world units per second. Cloud height is world units. Cloud scale is inverse world units.

Each `Environment::update(Scene& scene, double time_ms, float delta_seconds)` updates the live environment state. At the start of the update, if `m_main_directional_light` is null, the environment scans `scene.light_count()` / `scene.get_light(index)` and stores the first live directional light. If no directional light exists, the pointer remains null and direct lighting is absent for that frame. The first weather implementation should be static and deterministic at mild-cloud defaults; later weather animation can be added deliberately. The day/night simulation is deterministic and time-driven.

The day/night solar path is:

    seconds = time_ms * 0.001
    day_phase = fract(seconds / day_cycle_seconds + day_phase_offset)
    theta = day_phase * 2*pi - pi/2
    horizontal = cos(theta)
    sun_direction = normalize(vec3(
        horizontal * sin(sun_azimuth_radians),
        sin(theta),
        horizontal * cos(sun_azimuth_radians)))

`sun_direction` is the direction from the observer/world toward the sun. The environment's current sun light entity's world `+Z` should point in the opposite direction, the direction sunlight travels:

    sun_light_world_forward = -sun_direction

The moon direction starts opposite the sun with a small fixed tilt so it is not mechanically identical:

    moon_direction = normalize(-sun_direction + vec3(0.08, 0.03, -0.04))

Ambient values are derived from the same environment state:

    day_factor = smoothstep(-0.06, 0.08, sun_direction.y)
    twilight_factor = smoothstep(-0.22, 0.02, sun_direction.y) *
                      (1.0 - smoothstep(0.02, 0.20, sun_direction.y))
    storm_dimming = mix(1.0, 0.45, storm_intensity)
    ambient_intensity = mix(0.025, 0.22, day_factor) * mix(1.0, 0.55, cloud_coverage)
    ambient_color = mix(vec3(0.08, 0.10, 0.18), vec3(0.46, 0.52, 0.62), day_factor)

When the environment has a live sun light after its internal fallback scan, the default environment simulation may rotate that light's owning entity to match `sun_light_world_forward`, and may update the light's color/intensity from the same day factor and storm dimming. Those direct-light properties still live on the `Light` component. Ambient light properties live on `Environment`.

### Scene Environment Contract

`Scene` should store the live `Environment` next to its component containers:

- `Environment& environment() noexcept;`
- `const Environment& environment() const noexcept;`

`Scene::update` should validate controls first, then call `m_environment.update(*this, context.m_time_ms, context.m_delta_seconds)` before players, animation players, CPU skinning, or cameras update. `Scene` should not resolve or pass in the sun light; it only supplies the scene that `Environment` can scan when its current sun pointer is null. If the scene is rendered before its first update, `Renderer` should produce an empty light-properties list rather than crash, while ambient fallback remains valid.

Existing `Scene::set_main_light` and `Scene::set_ambient_light` should be removed or retired from the main renderer path once `Light` and `Environment` land. Tests that need direct lighting should create a `Light` component; tests that need ambient should configure `scene.environment()`.


### Sky Uniform Layout

`SkyPass` writes one uniform buffer per frame. Use a packed float array or an explicitly tested standard-layout struct with 16-byte rows:

    vec4 camera_right_tan_half_fov;  // xyz camera right, w tan(vertical_fov/2)
    vec4 camera_up_aspect;           // xyz camera up, w aspect
    vec4 camera_forward_time;        // xyz camera forward, w environment time seconds
    vec4 camera_position_day;        // xyz camera position, w day factor
    vec4 sun_direction_intensity;    // xyz observer-to-sun from Environment, w directional light intensity
    vec4 moon_direction_phase;       // xyz observer-to-moon, w moon phase
    vec4 sky_factors;                // solar elevation, twilight factor, haze, exposure
    vec4 weather;                    // cloud coverage, storm intensity, precipitation, star intensity
    vec4 cloud_motion;               // wind x, wind z, cloud scale, cloud speed
    vec4 cloud_shape;                // cloud height, cloud opacity, cloud sharpness, cloud layer blend

The first implementation can use a single bind group layout with one durable uniform buffer visible to vertex and fragment stages. `SkyPass` updates the buffer contents once per rendered frame with `wgpuQueueWriteBuffer`; it must not allocate a new buffer per frame. No material resources are needed.

### Clear Sky Function

The first implementation uses an analytic shader approximation inspired by Rayleigh/Mie sky behavior. It is not a physically calibrated spectral model and does not need atmospheric LUTs yet.

For each pixel:

    mu = dot(world_ray, sun_direction)
    up = saturate(world_ray.y)
    horizon = pow(1.0 - saturate(abs(world_ray.y)), 2.0)

    day_zenith = mix(vec3(0.08, 0.24, 0.70), vec3(0.15, 0.42, 0.95), 1.0 - haze)
    day_horizon = mix(vec3(0.86, 0.76, 0.58), vec3(0.70, 0.84, 1.00), day_factor)
    night_zenith = vec3(0.002, 0.004, 0.014)
    night_horizon = vec3(0.010, 0.012, 0.026)

    zenith_color = mix(night_zenith, day_zenith, day_factor)
    horizon_color = mix(night_horizon, day_horizon, day_factor)
    sky = mix(horizon_color, zenith_color, pow(up, 0.45))

Add directional scattering:

    rayleigh_phase = 0.75 * (1.0 + mu * mu)
    g = mix(0.70, 0.86, haze)
    mie_phase = (1.0 - g*g) / pow(max(1.0 + g*g - 2.0*g*mu, 0.001), 1.5)
    sunset = twilight_factor * horizon * pow(saturate(mu), 2.0)

    sky += day_factor * vec3(0.05, 0.09, 0.16) * rayleigh_phase
    sky += sunset * vec3(1.00, 0.32, 0.08) * mix(0.8, 1.8, haze)
    sky += day_factor * mie_phase * vec3(1.0, 0.88, 0.66) * 0.015

Apply storm/haze dimming:

    sky *= mix(1.0, 0.62, storm_intensity)
    sky = mix(sky, vec3(dot(sky, vec3(0.2126, 0.7152, 0.0722))), storm_intensity * 0.20)

Do not clamp this value for display inside the sky shader. It remains HDR linear scene color and is compressed by `ToneMapPass`.

### Sun Disc

Before evaluating the sun, moon, and stars, compute the non-volumetric cloud noise and `cloud_alpha` described below. Use `cloud_alpha` to attenuate celestial bodies, then composite `cloud_color` over the final clear-sky/celestial result once at the end. This avoids using `cloud_alpha` before it exists and keeps cloud compositing in one place.

The sun disc is evaluated in the sky shader:

    sun_radius = radians(0.265)
    sun_softness = radians(0.04)
    sun_cos = dot(world_ray, sun_direction)
    sun_disc = smoothstep(cos(sun_radius + sun_softness), cos(sun_radius), sun_cos)
    sun_halo = pow(saturate(sun_cos), mix(90.0, 18.0, haze))
    cloud_occlusion = 1.0 - cloud_alpha * mix(0.45, 0.90, storm_intensity)
    sky += (sun_disc * 3.0 + sun_halo * 0.25) * sun_color * day_factor * cloud_occlusion

Because Milestone 0 provides an HDR scene target, the sun can intentionally exceed 1.0 in linear scene color. `ToneMapPass` handles the visible compression to display output.

### Moon Disc

The moon is also procedural. No texture is required for the first version.

    moon_radius = radians(0.30)
    moon_softness = radians(0.04)
    moon_cos = dot(world_ray, moon_direction)
    moon_disc = smoothstep(cos(moon_radius + moon_softness), cos(moon_radius), moon_cos)

Build a stable basis from moon direction and world up:

    moon_reference_up = select(vec3(0, 1, 0), vec3(1, 0, 0), abs(moon_direction.y) > 0.98)
    moon_right = normalize(cross(moon_reference_up, moon_direction))
    moon_up = cross(moon_direction, moon_right)
    moon_x = dot(world_ray, moon_right) / sin(moon_radius)

Approximate phase:

    phase_offset = moon_phase * 2.0 - 1.0
    moon_lit = smoothstep(-0.08, 0.08, moon_x + phase_offset)
    moon_visibility = smoothstep(0.05, -0.08, sun_direction.y)
    sky += moon_disc * moon_lit * moon_visibility *
           vec3(0.64, 0.70, 0.84) * mix(0.35, 0.10, storm_intensity)

This gives a readable moon and a crude terminator. A later real moon texture can replace only this function.

### Stars

Use procedural deterministic stars. Do not load a real catalog in the first implementation.

Map `world_ray` to equirectangular coordinates:

    u = atan2(world_ray.x, world_ray.z) / (2*pi) + 0.5
    v = asin(clamp(world_ray.y, -1.0, 1.0)) / pi + 0.5

Use a fixed grid and hash per cell:

    grid = vec2(1024.0, 512.0)
    cell = floor(vec2(u, v) * grid)
    random = hash2(cell + star_seed)
    center = (cell + random.xy) / grid
    d = (vec2(u, v) - center) * vec2(grid.x * cos((v - 0.5) * pi), grid.y)
    star_exists = step(0.9975, random.z)
    star_size = mix(0.35, 1.15, pow(random.w, 8.0))
    star = star_exists * smoothstep(star_size, 0.0, length(d))
    star_color = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.86, 0.62), random.y)

Stars fade out during daylight and behind clouds:

    night_visibility = smoothstep(0.02, -0.10, sun_direction.y)
    star_visibility = night_visibility * (1.0 - cloud_alpha) * (1.0 - storm_intensity * 0.85)
    sky += star * star_color * star_visibility * star_intensity

This produces a plausible sky without asset management. If a real catalog becomes desirable, add a preprocessing step that turns Hipparcos/Tycho entries into a compact generated asset or GPU buffer; keep the `Environment` and `SkyPass` API unchanged.

### Non-Volumetric Cloud Weather Layer

Clouds are a 2D procedural layer projected onto a horizontal plane above the camera. This is intentionally not volumetric.

For upward or near-horizon rays:

    horizon_fade = smoothstep(-0.03, 0.12, world_ray.y)
    t = cloud_height / max(world_ray.y, 0.05)
    cloud_world_xz = camera_position.xz + world_ray.xz * t
    wind_direction = select(vec2(0.0), normalize(weather.wind_xz), dot(weather.wind_xz, weather.wind_xz) > 0.0001)
    wind = wind_direction * cloud_speed * time_seconds
    uv = cloud_world_xz * cloud_scale + wind

Evaluate value-noise FBM in WGSL:

    large = fbm(uv * 0.19)
    detail = fbm(uv + large * 1.7)
    storm_detail = fbm(uv * 2.7 + large)
    shaped = mix(detail, detail * 0.65 + storm_detail * 0.35, storm_intensity)

Coverage controls the threshold:

    effective_coverage = clamp(cloud_coverage + storm_intensity * 0.20, 0.0, 1.0)
    threshold = mix(1.05, 0.30, effective_coverage)
    cloud_alpha = smoothstep(threshold, threshold + max(cloud_sharpness, 0.02), shaped)
    cloud_alpha *= cloud_opacity * horizon_fade

Cloud color:

    sun_wrap = saturate(dot(world_ray, sun_direction) * 0.5 + 0.5)
    lit_cloud = mix(vec3(0.74, 0.78, 0.82), vec3(1.0, 0.92, 0.78), sun_wrap)
    storm_cloud = vec3(0.12, 0.14, 0.17)
    night_cloud = vec3(0.025, 0.030, 0.045)
    cloud_color = mix(night_cloud, lit_cloud, day_factor)
    cloud_color = mix(cloud_color, storm_cloud, storm_intensity)

Composite:

    sky = mix(sky, cloud_color, cloud_alpha)

This gives clear, partly cloudy, overcast, and stormy silhouettes without raymarching. The first implementation should keep weather static at deterministic mild-cloud defaults; later weather animation can be added deliberately through environment presets or simulation.

## Plan of Work

Milestone 0 adds HDR scene color, shared depth ownership, final tone mapping, and renderer observability.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\scene_color_target.hpp`
- `C:\dev\ofg\cpp\src\render\scene_color_target.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\depth_target.hpp`
- `C:\dev\ofg\cpp\src\render\depth_target.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp`
- `C:\dev\ofg\cpp\src\render\tone_map_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\tone_map.wgsl.hpp`

`SceneColorTarget` owns the `RGBA16Float` texture and view. `DepthTarget` owns the depth texture and view currently hidden inside `OpaquePass`. Both targets use the same resize/release contract: nonzero size creates or reuses, zero size releases and leaves views null, repeated same-size resize is a no-op, and resize-storm tests prove pipelines, shader modules, uniform buffers, layouts, and size-independent bind groups stay stable while only size-dependent textures/views change. `ToneMapPass` owns a shader module, pipeline, uniform buffer, bind group layout, and counters. Its scene-color texture-view bind group is recreated only when the scene-color view generation changes. `Renderer::prepare_impl` creates `OpaquePass` for `WGPUTextureFormat_RGBA16Float` and creates `ToneMapPass` for the platform format. `Renderer::render_impl` resizes scene color/depth, renders opaque into scene color/depth, then tone-maps scene color into the caller-provided platform target. Add focused tests that repeated renders do not recreate tone-map resources, that zero resize releases targets safely, that resize storms recreate only size-dependent texture/view resources, and that invalid target/scene-color state throws clear `EngineError`s. Update the smoke visual contract if the old clear-color/background byte expectations shift after ACES plus sRGB output encoding. If the existing PBR demo appears too bright or too muted through tone mapping, prefer adjusting the first `ToneMapPass` exposure default over changing material albedo values.

Milestone 1 adds the `Light` component and the scene-owned `Environment` ambient/weather/sun-selection contract.

Create `C:\dev\ofg\cpp\include\ofg\scene\light.hpp`, `C:\dev\ofg\cpp\src\scene\light.cpp`, `C:\dev\ofg\cpp\include\ofg\scene\environment.hpp`, and `C:\dev\ofg\cpp\src\scene\environment.cpp`. Add scene-owned light storage, `Entity::light()` accessors, `ComponentType::Light`, and renderer extraction from light entity world `+Z` into `LightProperties`. Add `Environment` and `SkyWeather` data with validation helpers, ambient light values, a `Ptr<Light>` explicit-or-discovered main directional light selection, and deterministic day/night state. Add `Environment m_environment;` to `Scene`. `Scene::clear` should reset environment to defaults; scene move construction/assignment should move environment and preserve safe pointer semantics; rendering before the first update should still use a valid ambient default and an empty-or-default light-properties list safely. The demo scene should create a directional sun light entity; explicit binding through `scene.environment().set_main_directional_light(...)` is allowed but the environment's first-directional fallback scan must be sufficient.

Milestone 2 adds the first procedural sky slice: clear sky plus sun.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\sky_pass.hpp`
- `C:\dev\ofg\cpp\src\render\sky_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\procedural_sky.wgsl.hpp`

`SkyPass` owns a shader module, pipeline, bind group layout, durable uniform buffer, bind group, and counters. It creates durable state in `SkyPass::create`. It records a full-screen sky draw after opaque draws, preferably inside the same WebGPU scene render pass, with depth compare `Equal` and no depth writes. In this milestone, implement only clear sky gradient, sunrise/sunset color, sun disc, and sun halo. The sky sun direction comes from the environment's current sun direction, while sun color/intensity can come from the first directional `LightProperties` item when present. Tests must cover sky uniform packing, environment sun fallback scanning, light-properties extraction, and steady-state resource reuse.

`Renderer::prepare_impl` should create `SkyPass` in addition to the existing `OpaquePass` and Milestone 0 `ToneMapPass`. `Renderer::render_impl` should run:

    resolve camera
    build draw list
    resolve environment ambient
    build light properties list
    scene_color_target.resize(target size)
    depth_target.resize(target size)
    begin scene render pass with scene color + depth
    opaque_pass.draw(... render_pass, camera, light_properties, environment.ambient_light(), draw_list ...)
    sky_pass.draw(... render_pass, camera, scene.environment(), light_properties ...)
    end scene render pass
    tone_map_pass.render(... scene_color_target.view(), target ...)

Milestone 3 adds clouds, moon, stars, and deterministic environment presets.

Extend `SkyPass` with the non-volumetric cloud layer, moon disc/phase, procedural stars, and weather controls. Add deterministic presets for daylight, sunset, night/moon/stars, and storm clouds. These presets should be callable from tests and smoke tools so visual validation is not tied to wall-clock or frame time. Define shader quality constants before implementation: maximum FBM octaves, daytime star early-out, clear-cloud early-out, storm-detail early-out, target smoke viewport, and acceptable native render timing delta.

Milestone 4 integrates the visual contract and validation.

Update `DemoScene` or `Environment` defaults so the first loaded view starts in daylight with visible sky and mild clouds. Add or adjust smoke expectations so background is classified as tone-mapped sky rather than clear color. Update smoke tooling and schemas explicitly, including `tools/browser-smoke.mjs`, `tools/browser-smoke-cpp.mjs`, `tools/smoke-render-cpp.mjs`, `cpp/include/ofg/native/render_smoke.hpp`, and `cpp/src/native/render_smoke.cpp`. Update `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` to describe HDR scene color, tone mapping, `Light`, `Environment`, the sky draw, and the new visual contract. Run unit tests, browser smoke, native render smoke, coverage, refresh committed coverage summaries under `docs/coverage`, and take screenshots. Store durable screenshots under `C:\dev\ofg\artifacts\...` and present them in chat.

## Concrete Steps

Run from `C:\dev\ofg`.

1. Add HDR scene color, shared depth target, tone-map pass, and observability.

    npm run test:cpp

Expected: C++ doctests pass, including new scene-color target, depth target, tone-map pass, output encoding, resize-storm, bind-group-generation, and renderer observability tests. Existing rendered content should still appear through the tone-mapped path.

2. Add Light component, environment data, and tests.

    npm run test:cpp

Expected: C++ doctests pass, including new `Light` component tests, entity `+Z` directional-light extraction tests, environment ambient/weather tests, default sun rotation/color tests, environment sun `Ptr<Light>` fallback-scan tests, scene move/clear tests, and demo sun-light setup tests.

3. Add clear sky and sun sky draw.

    npm run test:cpp

Expected: renderer tests pass; sky uniform packing, environment current-sun-light, and `LightProperties` tests pass; renderer counters include the new tone-map and sky durable resources; counters do not grow on repeated ordinary frames.

4. Add clouds, moon, stars, and deterministic presets.

    npm run test:cpp

Expected: environment preset tests pass for daylight, sunset, night/moon/stars, and storm clouds; shader quality constants and early-out paths are covered by CPU-side packing/config tests where possible.

5. Build browser/WASM output.

    npm run build

Expected: app and WASM build succeeds.

6. Run browser smoke.

    npm run smoke:browser

Expected: browser starts, WebGPU initializes, the scene renders with procedural sky instead of dark clear background, and smoke pixel classification passes.

7. Run native render smoke.

    npm run smoke:render

Expected: native Dawn render writes a PNG and JSON report under `C:\dev\ofg\artifacts\render-smoke`; PNG shows sky, sun/daylight or configured test phase, ground, cubes, and player.

8. Run full smoke and coverage gates before completion.

    npm run smoke
    npm run coverage

Expected: smoke passes; coverage summaries do not list changed implementation files under the default attention threshold unless this plan records a justified exception.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking that milestone complete, or record a rejected finding with rationale.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Functional acceptance:

- Opaque PBR content renders into an `RGBA16Float` linear scene-color target, not directly into the platform output target.
- A final tone-map pass maps HDR linear scene color to sRGB output with the ACES-fitted curve and default exposure `1.0`.
- The tone-map pass reads scene color with `textureLoad` and does not require float texture filtering or optional GPU features.
- Output encoding is correct for both non-sRGB and `*Srgb` platform target formats.
- Directional direct lighting comes from a `Light` component, and the owning entity's world `+Z` direction defines the direction light travels.
- `Environment` owns ambient light properties and current sun-light state through a safe `Ptr<Light>`; when that pointer is null, `Environment` scans scene lights and adopts the first directional light.
- Renderer passes consume a transient `LightProperties` list, initially containing at most one directional light property translated from the environment's current sun light.
- The browser app shows procedural sky behind the existing scene instead of a flat dark clear color.
- The sky includes sun, sunrise/sunset coloring, moon, stars, and a day/night cycle.
- Deterministic environment presets can produce daylight, sunset, night/moon/stars, and storm-darkened cloud cover without volumetric clouds.
- Scene PBR direct lighting changes with the `LightProperties` generated from the environment's current sun light, and ambient lighting changes with the environment state.
- The sky draw runs after opaque drawing and does not overwrite opaque geometry.
- Renderer resource counters or test-only observability prove steady-state frames update uniforms but do not recreate tone-map or sky pipelines, buffers, bind group layouts, shader modules, size-independent bind groups, or textures.
- The implementation leaves a clear route for future distance fade/aerial perspective through the renderer-owned depth target.

Test acceptance:

- `npm run test:cpp` passes.
- `npm run test:ts` passes if TypeScript smoke/ownership contracts are touched.
- `npm test` passes.
- `npm run smoke:browser` passes and produces a screenshot/report showing the procedural sky.
- `npm run smoke:render` passes and writes `artifacts/render-smoke/opaque-demo.png` or its renamed successor with the procedural sky visible.
- `npm run coverage` passes, with changed implementation files above the documented threshold or explicit exceptions recorded here, and committed coverage summaries under `docs/coverage` are refreshed as required by `COVERAGE.md`.

Screenshot acceptance:

- During implementation, take screenshots after the first tone-mapped opaque render, after the first visible sky, after adding clouds, after adding moon/stars, and before finalization.
- Present screenshots in chat for human review.
- Store useful comparison screenshots under `C:\dev\ofg\artifacts\procedural-sky\` or the smoke artifact directory.

## Idempotence and Recovery

The work should be additive and recoverable. `Renderer::release` must release `SceneColorTarget`, `DepthTarget`, `ToneMapPass`, `SkyPass`, and pass resources even if preparation failed after one pass was created. Repeated `Renderer::prepare` after ready must not create duplicate tone-map or sky resources. Repeated `Renderer::resize` with the same dimensions must not recreate scene-color or depth resources. Zero-size resize must release scene-color/depth GPU resources, leave their views null, and preserve the existing zero-size render validation behavior.

If Milestone 0 tone mapping fails, the local recovery fallback must recreate/reprepare `OpaquePass` for the platform output format before sending the caller-provided target directly to opaque rendering; an `OpaquePass` prepared for `RGBA16Float` cannot be used with the platform target. If sky visuals fail but tone-mapped opaque rendering still works, leave `Environment` and `Light` in place and temporarily skip `SkyPass::draw` from `Renderer::render_impl`; the opaque pass will continue to clear the HDR scene color until the sky shader is fixed, then `ToneMapPass` will present that fallback. These fallbacks are local recovery steps only; do not mark the milestone complete until the intended renderer path is restored or this ExecPlan is explicitly revised in the Decision Log.

If the depth-equal approach proves unreliable on a target backend, switch the first implementation to render sky before opaque while keeping the renderer-owned depth target. Record that decision here, update render ordering docs, and keep the future distance-fade path as a later sampled-depth composite pass.

## Artifacts and Notes

Research references used for this plan:

- Hillaire / Unreal-style sky atmosphere: physically based atmosphere with time-of-day, sun/moon directional lights, and LUT-scaled rendering.
- Bruneton/Neyret atmospheric scattering: precomputed scattering remains a good future target for physically based sky/aerial perspective.
- Horizon/Nubis and Frostbite cloud work: weather-controlled clouds use coverage/type/noise controls; OFG's first layer borrows the weather-control idea but not volumetric raymarching yet.
- NREL SPA and Hipparcos/Tycho: useful later if OFG needs real-world sun or star positions; not needed for this first game-oriented procedural implementation.

## Interfaces and Dependencies

New or changed public interfaces expected by the end:

- `C:\dev\ofg\cpp\include\ofg\render\scene_color_target.hpp`
  - `class SceneColorTarget`
  - `static constexpr WGPUTextureFormat SceneColorTarget::format() noexcept` or equivalent constant returning `WGPUTextureFormat_RGBA16Float`
  - `void SceneColorTarget::resize(std::uint32_t width, std::uint32_t height)`
  - `RenderTarget SceneColorTarget::render_target() const`
  - `WGPUTextureView SceneColorTarget::view() const noexcept`

- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp`
  - `class ToneMapPass`
  - `enum class ToneMapOutputEncoding { LinearOutput, ManualSrgb }`
  - `static std::unique_ptr<ToneMapPass> ToneMapPass::create(GpuContext gpu, WGPUTextureFormat output_format, ToneMapOutputEncoding encoding)`
  - `void ToneMapPass::render(WGPUCommandEncoder encoder, WGPUTextureView scene_color_view, RenderTarget output_target)`
  - `RendererCounters ToneMapPass::counters() const noexcept`
  - Internally cache one bind group per scene-color view generation, not per frame.

- `C:\dev\ofg\cpp\include\ofg\render\depth_target.hpp`
  - `class DepthTarget`
  - `static constexpr WGPUTextureFormat DepthTarget::format() noexcept`, initially `WGPUTextureFormat_Depth24Plus`
  - `void DepthTarget::resize(std::uint32_t width, std::uint32_t height)`
  - `WGPUTextureView DepthTarget::view() const noexcept`
  - Width/height/generation accessors used by renderer tests and bind-group reuse checks.

- `C:\dev\ofg\cpp\include\ofg\render\lighting.hpp`
  - `struct AmbientLight`
    - linear `math::Vec3 m_color`
    - non-negative `float m_intensity`
  - `enum class LightPropertiesType { Directional }`
  - `struct LightProperties`
    - `LightPropertiesType m_type`
    - normalized world direction the light travels, derived from the light entity's world `+Z` for directional lights
    - linear `math::Vec3 m_color`
    - non-negative `float m_intensity`
  - `std::size_t build_light_properties(const Scene& scene, std::span<LightProperties> output)` or an equivalent renderer-owned helper.
  - The first implementation should emit at most one `LightProperties` item, using `scene.environment().main_directional_light()` when it is live. It should not scan for the sun fallback; `Environment::update` owns that scan.
  - These are renderer input value types, analogous to `CameraProperties`. Direct-light authoring lives on `Light`; ambient-light authoring lives on `Environment`.

- `C:\dev\ofg\cpp\include\ofg\scene\light.hpp`
  - `enum class LightType { Directional }`
  - `class Light : public Component`
  - `LightType Light::light_type() const noexcept`
  - color/intensity accessors and setters that validate finite, non-negative linear values
  - enabled/visible accessors if included in the first implementation
  - Directional light direction is not stored on `Light`; it is resolved from the owning entity's world transform.

- `C:\dev\ofg\cpp\include\ofg\scene\environment.hpp`
  - `struct SkyWeather`
  - `class Environment`
  - forward declarations for `Scene` and `Light` to avoid making scene headers heavier than needed
  - `void Environment::update(Scene& scene, double time_ms, float delta_seconds)`
  - `const SkyWeather& Environment::weather() const noexcept`
  - `void Environment::set_main_directional_light(Light* light)`
  - `Light* Environment::main_directional_light() noexcept`
  - `const Light* Environment::main_directional_light() const noexcept`
  - ambient color/intensity setters and `AmbientLight Environment::ambient_light() const noexcept`
  - accessors for sun direction, moon direction, day factor, twilight factor, moon phase, and time seconds
  - `Environment::update` scans scene light components when `m_main_directional_light` is null, adopts the first directional `Light`, and then applies the default day/night simulation to that effective sun `Light` by rotating its owning entity so world `+Z` equals `-sun_direction`.

- `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp`
  - Add `Light` to `ComponentType`.
  - `Environment& Scene::environment() noexcept`
  - `const Environment& Scene::environment() const noexcept`
  - `std::size_t Scene::light_count() const noexcept`
  - `Light* Scene::get_light(std::size_t index) noexcept`
  - `const Light* Scene::get_light(std::size_t index) const noexcept`
  - `Scene::clear` resets light components and environment selection safely; scene moves preserve valid `Ptr<Light>` observer semantics.

- `C:\dev\ofg\cpp\include\ofg\render\sky_pass.hpp`
  - `static std::unique_ptr<SkyPass> SkyPass::create(GpuContext gpu, WGPUTextureFormat color_format, WGPUTextureFormat depth_format)`
  - `void SkyPass::draw(WGPURenderPassEncoder pass, const CameraProperties& camera, const Environment& environment, std::span<const LightProperties> lights)`
  - `RendererCounters SkyPass::counters() const noexcept`

- `C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp`
  - `OpaquePass::create` should receive the scene color format, initially `WGPUTextureFormat_RGBA16Float`, rather than the platform output format.
  - `void OpaquePass::draw(WGPURenderPassEncoder pass, const CameraProperties& camera, std::span<const LightProperties> lights, AmbientLight ambient_light, const DrawList& draw_list)`
  - `OpaquePass` should no longer own depth texture/view state after Milestone 0.
  - `OpaquePass` should no longer begin or end the scene render pass after the shared render-pass refactor; `Renderer` owns that pass boundary so sky can draw after opaque before the HDR scene color is stored.

- `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp`
  - `Renderer` should own explicit opaque, tone-map, and sky passes plus scene-color and shared depth targets, rather than a vector that assumes every pass is an `OpaquePass`.
  - `Renderer` resolves `AmbientLight` from `scene.environment()` and builds a transient `LightProperties` list, then passes those value types to draw passes.
  - `Renderer` must not mutate the sun light orientation, weather, ambient values, or environment time; that simulation work belongs to `Environment` during `Scene::update`.
  - Renderer counters should report durable texture, texture-view, shader-module, bind-group-layout, bind-group, pipeline, and buffer creation counts needed by the new tests.

- `C:\dev\ofg\cpp\CMakeLists.txt`
  - Add new `.cpp` implementation files and new doctest files.

The initial implementation must not add a third-party engine or runtime dependency.
