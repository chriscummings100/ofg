# Add Cascaded Sun Shadows

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes &
Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input
that cannot be safely inferred, or when the plan is complete.

This document follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

OFG needs world shadows from the current sun so the open-world factory scene reads as grounded, large, and spatially
coherent. The first production target is three cascaded shadow maps for the current sun directional light, with
configurable transition distances, cascade seam blending, soft shadow filtering, and shadow intensity. Cascaded shadow
maps split the camera view into near/mid/far depth ranges, render a sun-depth map for each range, then sample the
appropriate map while shading opaque geometry.

After this plan is implemented, the browser and native smoke scene should show existing opaque PBR content receiving
shadows from the same sun direction used by the sky/environment system. Near-camera shadows should be sharper and more
stable than far shadows. Moving the camera should not cause obvious shadow swimming. Shadow strength should be tunable
without changing light color or ambient light.

Before culling or shadows land, OFG needs a larger deterministic default demo scene. The current plane-and-few-cubes
scene is too small to prove culling and cascade transitions. Replace or evolve the default demo into a broad scene
containing many cubes with varied sizes, heights, rotations, spacing, and intentional intersections with each other and
the ground so the renderer has enough near, mid, far, partly hidden, and off-camera objects to exercise the systems. The
large scene should be the normal baseline because it will remain useful for renderer validation, player navigation,
culling, shadows, and later LOD/streaming work.

This plan builds on the completed procedural sky/environment work archived at
`C:\dev\ofg\docs\archived\procedural-sky-environment-plan.md` and on the active contracts in
`C:\dev\ofg\docs\API_CONTRACTS.md`. The current renderer already consumes `Environment`, scene-owned `Light`
components, and `LightProperties`. Shadow rendering must use that current sun state rather than reintroducing
`Scene::main_light()` or a parallel sun concept.

This plan also includes a standard render-culling prerequisite. OFG currently skips manually hidden renderers, but it
does not have mesh bounds, camera frustum culling, or shadow-caster culling. Add those foundations before CSM so opaque
rendering and shadow rendering can each generate a draw list from a pass-specific set of culling planes.

## Progress

- [x] (2026-07-04) Completed initial research pass for cascaded shadow maps, soft shadow filtering, cascade blending,
  shadow-map biasing, WebGPU depth comparison sampling, and browser ray tracing pipeline availability.
- [x] (2026-07-04) Read the current renderer, opaque pass, scene lighting, camera math, archived sky ExecPlan, API
  contracts, and guide docs to place the shadow plan in OFG's architecture.
- [x] (2026-07-04) Confirmed current renderer has no standard culling beyond `MeshRenderer::visible()`.
- [x] (2026-07-04) Reviewed the plan with correctness, completeness, clarity, efficiency, and performance sub-agents.
- [x] (2026-07-04) Incorporated review and user feedback: default large scene, explicit culling API, current sun/light
  baseline, low-sun shadow fade/clamp, cascade overlap, and shadow diagnostics.
- [x] (2026-07-04 12:16 +01:00) Implemented Milestone 0: the default demo is now a deterministic
  large culling/shadow validation scene with 184 reused cube-mesh box instances, browser/native smoke diagnostics, and
  reviewed validation evidence.
- [x] (2026-07-04 12:45 +01:00) Implemented Milestone 1: render bounds, extracted render objects, camera
  frustum culling, runtime culling diagnostics, browser/native smoke reporting, docs, coverage, and milestone review.
- [x] (2026-07-04 13:05 +01:00) Implemented Milestone 2: shadow settings, orthographic projection, CPU-side
  cascade split/matrix construction, stable texel snapping, low-sun fade/clamp helpers, cascade culling planes, docs,
  coverage, and milestone review.
- [x] (2026-07-04 13:44 +01:00) Implemented Milestone 3: shadow-map array target, depth-only shadow caster pass,
  per-cascade caster culling from the full extracted render-object list, renderer-owned shadow diagnostics, docs,
  coverage, smoke validation, and milestone review.
- [x] (2026-07-04 14:12 +01:00) Implemented Milestone 4: opaque PBR shadow sampling now uses the
  current sun shadow maps with intensity, receiver/normal bias, hard/five-tap/nine-tap PCF modes, cascade blending,
  visible browser/native smoke screenshots, docs, coverage, and milestone review.
- [ ] Implement Milestone 5: diagnostics, smoke/debug visuals, screenshots, docs, and coverage.
- [x] (2026-07-04) Debugged cascade shadow-map generation: per-cascade shadow caster passes now use distinct
  frame/model uniform bindings so command-buffer submission cannot observe the last cascade's matrix/model writes, and
  cascade bounds now use tight light-space receiver side planes with bounded caster-depth padding instead of a
  sphere-sized square fit.

## Surprises & Discoveries

- Observation: Soft shadow edges and cascade blending solve different artifacts.
  Evidence: Microsoft documents cascade blending as a seam fix between cascade layers, while PCF/VSM-style filtering is
  the mechanism for soft or blurred shadow edges.

- Observation: WebGPU/WGSL has the primitives needed for shadow maps, including depth textures, comparison samplers, and
  `texture_depth_2d_array` comparison sampling.
  Evidence: WGSL defines `sampler_comparison` for depth textures and `textureSampleCompare` overloads for
  `texture_depth_2d_array`.

- Observation: Browser graphics APIs do not expose hardware ray tracing pipelines today.
  Evidence: WebGL 2.0 is specified as closely matching OpenGL ES 3.0; hardware ray tracing APIs such as Vulkan RT and
  DXR require acceleration structures and ray tracing pipeline/shader stages that WebGL does not expose. The GPUWeb ray
  tracing issue for WebGPU remains open and marked future/milestone 4+.

- Observation: The sky/HDR/bloom groundwork has landed in the current local code.
  Evidence: `C:\dev\ofg\cpp\include\ofg\scene\environment.hpp`,
  `C:\dev\ofg\cpp\include\ofg\render\lighting.hpp`, `C:\dev\ofg\cpp\include\ofg\render\sky_pass.hpp`, and
  `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp` exist; `Renderer::render_impl` builds `LightProperties`, renders
  opaque and sky into HDR scene color, runs bloom, then tone maps.

- Observation: `Environment::sun_direction()` and `LightProperties::m_direction` use opposite conventions.
  Evidence: `Environment::sun_direction()` is observer-to-sun. `LightProperties::m_direction` is the direction light
  travels, derived from the selected light entity's world `+Z`. The opaque shader uses `-main_light_direction` as the
  surface-to-sun vector.

- Observation: Before Milestone 1, render extraction had no frustum, distance, occlusion, or shadow-caster culling.
  Evidence: `Renderer::build_draw_list_from_scene` iterates every `scene.mesh_renderer_count()` entry and only skips
  renderers where `MeshRenderer::visible()` is false. `DrawList` does not store world bounds, and `Mesh` does not store
  local bounds.

- Observation: The larger default scene filled more of the smoke camera than the old four-cube scene.
  Evidence: native smoke initially failed `minBackgroundRatio` with `backgroundRatio = 0.176504`; raising the default
  camera target to `y = 1.9` preserved the large field while producing passing native/browser background ratios.

- Observation: The milestone review caught that only native smoke reported the large-scene object counts.
  Evidence: the first implementation wrote `demoScene` only in `C:\dev\ofg\cpp\src\native\render_smoke.cpp`; the fix
  publishes the same `demoScene` block through `RuntimeDebugStatus` and has browser smoke assert/report it.

- Observation: C++ coverage initially rejected the new culling math because defensive validation branches were not
  tested.
  Evidence: `npm run coverage` first failed `cpp\src\render\bounds.cpp` at 83.78% and
  `cpp\src\render\frustum.cpp` at 85.71%; focused invalid-geometry and invalid-plane tests raised both files to
  100.00%.

- Observation: Camera culling is active in real smoke frames without changing the expected visible scene.
  Evidence: native smoke reported `renderCulling = { extractedObjectCount: 186, cameraVisibleObjectCount: 118,
  cameraCulledObjectCount: 68 }`; browser smoke reported `renderCulling = { extractedObjectCount: 188,
  cameraVisibleObjectCount: 105, cameraCulledObjectCount: 83 }` after the player model loaded.

- Observation: `docs\ARCHITECTURE.md` is not present in this repo, though the milestone-review skill lists it as a
  desired input.
  Evidence: `Get-Content docs\ARCHITECTURE.md` failed during the Milestone 1 review; the review used
  `docs\API_CONTRACTS.md`, `docs\SYSTEMS.md`, and this ExecPlan for current architecture contracts.

- Observation: Milestone 2 could be validated without a visual delta because the new shadow work is CPU-only.
  Evidence: `shadow_settings.hpp`, `shadow_cascade.hpp`, and `math::orthographic_lh` are compiled and covered by doctests,
  while native/browser smoke still report the same unshadowed large-scene culling counts until Milestone 3/4 wire GPU
  rendering and shader sampling.

- Observation: The new shadow files met the per-file coverage gate without exclusions.
  Evidence: `npm run coverage` reported `cpp\src\math\transform.cpp line coverage 100.00%`,
  `cpp\src\render\shadow_cascade.cpp line coverage 91.44%`, and `cpp\src\render\shadow_settings.cpp line coverage 95.24%`.

- Observation: Milestone 3 can encode shadow maps without a visual delta.
  Evidence: `ShadowMapTarget` and `ShadowCasterPass` render three hidden depth passes before opaque rendering when a
  current sun exists, while native and browser smoke screenshots remain intentionally unshadowed until Milestone 4 wires
  opaque shader sampling. Native smoke reported `pipelineCreateCount = 12` and browser smoke reported
  `pipelineCreateCount = 15` after the hidden pass landed.

- Observation: WebGPU backend null-return and partial-allocation cleanup branches are not deterministic native test
  fixtures.
  Evidence: focused tests covered invalid public inputs, move/release/reuse behavior, CPU-only mesh rejection, disabled
  shadows, fully faded low-sun shadows, and renderer integration, but `npm run coverage:cpp` still required explicit
  defensive exclusions for `cpp\src\render\shadow_caster_pass.cpp` and
  `cpp\src\render\shadow_map_target.cpp` WebGPU null-return/cleanup lines. Normal behavior remains gated above 94%.

- Observation: The Milestone 3 review found a real generation-token bug in the new shadow target.
  Evidence: `ShadowMapTarget::view_generation()` claimed to change whenever views changed, but `resize(0)` released the
  texture and views without incrementing the token. The fix made texture/view release return whether handles changed,
  increments the generation on zero-size release and public `release()`, and added doctest assertions for the sequence.

- Observation: WGSL depth comparison sampling in cascade-dependent control flow needs the explicit-level form.
  Evidence: the first Milestone 4 native smoke failed WebGPU validation because `textureSampleCompare` must be called
  only from uniform control flow; switching opaque shadow lookups to `textureSampleCompareLevel` passed native and browser
  smoke with the same cascade selection branches.

- Observation: Rewriting one shadow-caster frame uniform buffer inside the cascade loop can make every recorded shadow
  pass observe the last cascade's matrix at submit time.
  Evidence: the shadow-map debug overlay showed different per-cascade caster membership but identical object scale.
  The fix gives each cascade distinct frame and draw uniform buffers/bind groups; `npm run test:cpp`,
  `npm run smoke:render`, and `npm run smoke:browser` passed afterward.

- Observation: The initial sphere-derived square cascade fit wasted a large amount of shadow-map area and admitted too
  many behind/around-camera casters.
  Evidence: after replacing the sphere fit with tight light-space X/Y receiver bounds plus bounded light-depth padding,
  native smoke accepted casters dropped from near/mid/far `17/155/186` to `12/111/139`, and browser smoke reported
  `24/113/112` for its final camera state while still passing.

## Decision Log

- Decision: Implement CSM before any ray-traced shadow path.
  Rationale: WebGL cannot use ray tracing pipelines, standard browser WebGPU does not expose hardware ray tracing, and
  CSM is the established real-time sun-shadow technique for large outdoor scenes.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use three cascades for the first version.
  Rationale: Three cascades match the user request and are a practical outdoor default: one high-resolution near cascade,
  one mid cascade, and one far cascade. More cascades increase pass count and shader sampling complexity.
  Date/Author: 2026-07-04 / Codex.

- Decision: Treat cascade blending as a transition feature, not as the main soft-shadow feature.
  Rationale: Blending two cascades hides resolution discontinuities at split boundaries. Soft shadow edges should come
  from PCF first, with PCSS or VSM/EVSM left as later quality upgrades.
  Date/Author: 2026-07-04 / Codex.

- Decision: Start with comparison-sampler PCF over a depth texture array.
  Rationale: It fits WebGPU, needs no optional features, avoids extra blur passes, and keeps the first implementation
  close to standard shadow mapping. VSM/EVSM can produce wider soft shadows but adds moment textures, blur passes, and
  light-leaking controls.
  Date/Author: 2026-07-04 / Codex.

- Decision: Apply shadow intensity to direct sun lighting only.
  Rationale: A shadow should reduce direct sun contribution while ambient/sky lighting still remains. This avoids
  crushed black shadows and composes naturally with future environment lighting.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use stable cascades with texel snapping in the first implementation, even if the first smoke scene is small.
  Rationale: OFG is open-world and camera movement is central. Unstable fit-to-cascade projections can shimmer badly as
  the camera moves.
  Date/Author: 2026-07-04 / Codex.

- Decision: Prefer `WGPUTextureFormat_Depth32Float` for shadow maps unless implementation testing exposes a browser or
  native compatibility problem.
  Rationale: The renderer can keep `Depth24Plus` for the main scene depth target, but shadow maps benefit from stable
  precision and predictable sampling. WebGPU comparison sampling works with depth textures.
  Date/Author: 2026-07-04 / Codex.

- Decision: Add standard bounds-based render culling before CSM GPU work.
  Rationale: CSM multiplies draw submission by three shadow cascades, so culling becomes a correctness and performance
  prerequisite. Camera frustum culling alone is not enough because off-camera objects can cast shadows into the area the
  player can see. The first culling milestone should build a generic "bounded objects plus culling planes produce a draw
  list" framework. Opaque rendering uses the camera frustum planes. Shadow rendering later derives different culling
  planes from the camera-visible receiver volume and current sun direction.
  Date/Author: 2026-07-04 / Codex.

- Decision: Make the large validation scene the default demo scene rather than an optional preset.
  Rationale: A dense deterministic scene will stay useful for renderer validation, culling, shadows, player navigation,
  smoke screenshots, and later open-world scalability work. Keeping the old tiny scene as the default would reduce the
  chance of catching culling and cascade mistakes during ordinary development.
  Date/Author: 2026-07-04 / Codex, based on user direction.

- Decision: Keep the culling API deliberately small: bounded render objects plus an explicit set of inward-facing
  culling planes produce a `DrawList`.
  Rationale: Culling correctness is foundational and easy to break if it is hidden behind camera-specific logic. A
  plane-set API lets camera, shadow, and future portal/zone/LOD systems share the same conservative object test while
  still generating pass-specific draw lists.
  Date/Author: 2026-07-04 / Codex, based on user direction.

- Decision: Fade out sun shadows at low sun elevation and clamp the effective shadow light angle before building
  cascades.
  Rationale: Near-horizon directional lights create extremely long shadows and very large caster volumes. Fading the
  shadow intensity to zero as the sun approaches the horizon hides artifacts, while clamping the effective shadow angle
  prevents practically infinite shadow projections during the fade band.
  Date/Author: 2026-07-04 / Codex, based on user direction.

- Decision: Publish default demo-scene identity and counts through `RuntimeDebugStatus`.
  Rationale: Browser and native smoke can now report the actual C++ scene diagnostics from one source of truth, and
  browser smoke can fail if the default scene regresses to a tiny or sparse setup.
  Date/Author: 2026-07-04 / Codex.

- Decision: Raise the default demo camera target from `y = 0.55` to `y = 1.9`.
  Rationale: The large scene should remain readable but not consume so much of the frame that the shared sky/background
  smoke contract fails. The revised target keeps the near/mid/far box field visible and leaves stable background headroom
  in browser and native smoke.
  Date/Author: 2026-07-04 / Codex.

- Decision: Store mesh local bounds on `Mesh` and recompute them whenever CPU vertex data changes.
  Rationale: Camera culling and future shadow-caster culling need conservative bounds for static meshes, replacement
  vertex data, and dynamic CPU-skinned mesh updates. Keeping the bounds beside the CPU vertices makes extraction cheap
  and keeps ownership inside the C++ resource layer.
  Date/Author: 2026-07-04 / Codex.

- Decision: Publish camera culling counts through `RuntimeDebugStatus.renderCulling`.
  Rationale: Browser and native smoke need a shared observable contract that culling ran and balanced
  extracted/visible/culled counts without relying on visual differences, because correct camera culling should not alter
  visible pixels.
  Date/Author: 2026-07-04 / Codex.

- Decision: Start the CPU shadow settings with fixed defaults of three cascades ending at 12, 32, and 80 world units,
  blend widths of 2, 4, and 8 world units, a 1024 shadow-map size, five-tap PCF, 0.75 intensity, 10-to-1 degree low-sun
  fade, 5 degree minimum shadow elevation, and 80 world units of caster depth padding.
  Rationale: These values give Milestone 3/4 a concrete, testable renderer contract while keeping all quality knobs in
  one validated C++ settings type.
  Date/Author: 2026-07-04 / Codex.

- Decision: Treat `LightProperties::m_direction` as the light travel direction for all cascade helpers, and compute sun
  elevation from the negated surface-to-sun vector.
  Rationale: This matches the existing opaque shader convention and avoids creating a parallel sun direction convention
  inside the shadow subsystem.
  Date/Author: 2026-07-04 / Codex.

- Decision: Build finite cascade matrices even when shadows are disabled or the sun has faded out.
  Rationale: Downstream renderer code can keep a simple always-present data path while using `m_effective_intensity == 0`
  to skip or neutralize shadowing when disabled or below the low-sun fade band.
  Date/Author: 2026-07-04 / Codex.

- Decision: Encode Milestone 3 shadow maps as a material-independent depth-only pass.
  Rationale: The first GPU milestone only needs caster depth for opaque geometry. Drawing positions with model and
  cascade matrices avoids touching the opaque material/shader layout before Milestone 4, while still proving
  per-cascade caster culling, durable resource ownership, and current-sun integration.
  Date/Author: 2026-07-04 / Codex.

- Decision: Keep the shadow comparison sampler alive across zero-size shadow texture releases.
  Rationale: The sampler is size-independent durable renderer state. `ShadowMapTarget::resize(0)` releases texture and
  views but keeps the sampler, while public `release()` drops both texture/view state and the sampler for full teardown.
  Date/Author: 2026-07-04 / Codex.

- Decision: Record defensive coverage exclusions for shadow WebGPU allocation-failure cleanup paths.
  Rationale: The Dawn test backend validates normal allocation, rendering, and public error paths, but it does not expose
  stable fixtures that make `wgpuDeviceCreate*` or `wgpuTextureCreateView` return null after descriptor validation. The
  excluded lines are narrow null-return and partial-allocation cleanup branches; normal target/pass behavior remains
  covered and smoke-tested.
  Date/Author: 2026-07-04 / Codex.

- Decision: Keep the opaque shadow bind group always present with durable fallback resources.
  Rationale: One opaque pipeline shape avoids disabled-shadow material variants and lets no-sun/disabled frames bind a
  valid `texture_depth_2d_array` plus comparison sampler while the shadow uniform disables sampling.
  Date/Author: 2026-07-04 / Codex.

- Decision: Use `textureSampleCompareLevel` for opaque shadow-map lookups.
  Rationale: Cascade selection, early-outs, and blend-band logic are intentionally pixel-dependent. The explicit-level
  comparison path is valid in that non-uniform control flow and keeps the shader within the WebGPU/WGSL baseline.
  Date/Author: 2026-07-04 / Codex.

## Outcomes & Retrospective

Milestone 0 is complete. The default app/native demo now uses a large deterministic validation scene: 184 box instances
share the existing cube mesh and four cube materials, cover near/mid/far distance buckets, include overlapping clusters,
partly below-ground boxes, and off-camera candidates, and preserve the player/camera smoke workflow. Browser and native
reports include `demoScene` counts from the C++ runtime status.

Milestone 1 is complete. Meshes now expose local CPU-derived bounds, render extraction produces transient bounded
`RenderObject` values from authored-visible `MeshRenderer` components, camera frustum culling filters those objects into
the opaque draw list, and runtime/browser/native reports expose `renderCulling` counts. The generic culling API is a
small plane-set contract that future shadow passes can reuse with different culling planes. Remaining gaps are the
planned CPU cascade/shadow math, shadow-map target/pass, opaque shader sampling, and shadow diagnostics. Non-blocking
follow-up: `src/app/wasmRuntime.ts` is now 584 lines; future debug-status growth should split parser helpers before the
file crosses the repo-local 600-line review threshold.

Milestone 2 is complete. The renderer now has CPU-side shadow settings and cascade math: `ShadowSettings` validates the
three-cascade public contract, `math::orthographic_lh` provides WebGPU-depth orthographic projection, and
`build_shadow_cascades` produces stable texel-snapped light matrices, split intervals, blend bands, low-sun
fade/clamp state, effective intensity, receiver bounds, and owned culling planes for each cascade. The work intentionally
does not create WebGPU shadow textures or change opaque draw submission yet; that remains Milestone 3 and Milestone 4.

Milestone 3 is complete. The renderer now owns a durable three-layer `Depth32Float` `ShadowMapTarget`, a depth-only
`ShadowCasterPass`, and `ShadowPassDiagnostics`. When a current sun directional light exists, `Renderer::render` builds
the three CPU cascades, culls shadow casters against each cascade's light-space plane set from the full extracted
render-object list, and encodes one depth render pass per cascade before opaque rendering. Opaque shading still does not
sample these maps, so browser/native screenshots remain visually unshadowed until Milestone 4. The milestone review
fixed `ShadowMapTarget::view_generation()` so releasing texture/views to zero also updates the generation token future
bind-group code can trust.

Milestone 4 is complete. `ShadowFrameState` now packs the three cascade matrices, split distances, blend widths,
texel/bias controls, effective intensity, and PCF mode into the opaque group-3 uniform. `OpaquePass` owns an
always-present shadow bind-group layout, uniform buffer, durable fallback depth array, fallback comparison sampler, and a
generation-aware live shadow bind group. `PipelineCache` now keys the shadow layout into the four-group opaque pipeline
layout, and `Renderer::render` passes live current-sun shadow maps into opaque drawing after the shadow caster pass. The
opaque WGSL shader samples the current sun depth array, applies receiver and normal bias, supports hard/five-tap/nine-tap
comparison filtering, blends adjacent cascades inside transition bands, and only attenuates direct sun lighting by the
effective shadow intensity. Browser and native smoke now show visible sun-cast shadows. Public runtime/browser shadow
diagnostics and debug overlays remain Milestone 5 work.

## Contract and Quality Baseline

`OFG-BOOT-001 TypeScript Host Ownership` must be preserved. TypeScript may display debug data or smoke artifacts, but it
must not own shadow cascade calculation, shadow GPU resources, draw submission, or light selection.

`OFG-BOOT-002 C++ Runtime Ownership` changes by adding renderer-owned culling and shadow resources to the existing C++
renderer path. Shadows consume the already-current `Environment`, scene-owned `Light` components, and renderer-built
`LightProperties`; the plan must not reintroduce `Scene::main_light()` or any TypeScript-owned lighting state.

`OFG-BOOT-004 Renderer Compatibility` changes because browser and native smoke must validate the same shadowed renderer
path. The smoke visual contract should add shadow-aware assertions rather than rely only on lit color categories.

`OFG-BOOT-005 WebGPU Baseline` must be preserved. The first CSM implementation must request no optional GPU features and
must not manually request higher adapter limits. Shadow maps should use standard render attachments, texture bindings,
depth comparison samplers, and ordinary render passes.

`OFG-BOOT-006 Resource Lifetime` must be preserved. Shadow map textures/views, comparison samplers, bind group layouts,
shader modules, pipelines, and uniform buffers are durable renderer resources. Ordinary frames may update shadow uniforms
and render depth, but must not recreate pipelines or size-independent bind groups. Intensity, fade/clamp values, receiver
bias, PCF mode/radius, blend widths, and cascade distances update uniforms and CPU-side cascade data only. Shadow map
size or format changes recreate shadow textures/views only when the value actually changes. Static WebGPU pipeline
depth-bias values are part of shadow-caster pipeline state; if this plan later makes them user-tunable, changing them
must mark the shadow caster pipeline dirty and recreate it deliberately.

`OFG-BOOT-009 Coverage` applies. Each modified implementation file must pass the default coverage attention gate,
currently about 90% line coverage unless this plan records an explicit exception with rationale.

## Context and Orientation

The current renderer is C++ owned. `Renderer::render_impl` in `C:\dev\ofg\cpp\src\render\renderer.cpp` resolves the main
camera into `CameraProperties`, extracts authored-visible mesh renderers into bounded transient render objects,
camera-frustum culls those objects into an opaque `DrawList`, builds a transient `LightProperties` list from
`scene.environment().main_directional_light()`, renders opaque content and sky into the HDR scene color target, runs
bloom, then tone maps the HDR result to the platform target. Shadow rendering should slot in before the opaque scene
pass:

    extract visible-by-flag render objects with world bounds
    resolve camera
    resolve current sun light
    camera-cull render objects into the opaque draw list
    compute three sun-shadow cascades
    per-cascade cull shadow casters into shadow draw lists
    render shadow draw lists into shadow-map depth layers
    render opaque PBR into HDR scene color while sampling shadows
    render sky
    run bloom
    tone map to platform output

The shadow system consumes the same current sun directional light used by opaque and sky rendering. CSM must use
`LightProperties::m_direction` as the light travel direction. If a future helper reads celestial state directly from
`Environment::sun_direction()`, it must negate that observer-to-sun vector before building shadow matrices. Add a focused
test that a simple cube shadow falls opposite the surface-to-sun vector.

`CameraProperties` in `C:\dev\ofg\cpp\include\ofg\render\camera_properties.hpp` provides left-handed camera matrices
with camera-local `+Z` forward and WebGPU depth range `[0, 1]`. Shadow cascade math should use this camera snapshot,
not scene camera pointers.

`DrawList` already carries mesh pointers, model matrices, material overrides, and submesh ranges. The first shadow
caster pass can reuse this draw list and draw indexed geometry with a depth-only shader. Because CPU skinning updates
dynamic mesh vertices before rendering, skinned player geometry can cast shadows through the same vertex buffers.

As of Milestone 1, `DrawList` is no longer built directly from scene mesh renderers. Renderer extraction creates bounded
render objects from all authored-visible `MeshRenderer` components, and each pass filters those objects into its own draw
list. Opaque rendering uses camera frustum culling. Shadow rendering will use each cascade's light-space caster volume,
expanded enough that off-camera casters can still cast into the visible cascade.

The culling API should be plane-set based, not camera-specific. Camera culling is the first user of the framework because
it turns the camera frustum planes into an opaque draw list. Shadow culling will use different planes: it starts from the
camera-visible receiver region for each cascade, then extends or bounds that region along the reverse sun-light direction
to find objects that can cast shadows into what the player can see. The API should stay simple and testable:

    struct CullingPlane { math::Vec3 m_normal; float m_distance; };
    struct CullingPlaneSet { std::span<const CullingPlane> m_planes; };
    bool intersects_culling_planes(Bounds3 world_bounds, CullingPlaneSet planes);
    void append_culled_draws(std::span<const RenderObject> objects, CullingPlaneSet planes, DrawList& output, CullingStats& stats);

Plane normals point inward. A bounded object is accepted when it intersects every half-space; objects touching a plane
remain visible to avoid pop-in. The first implementation should use conservative AABB or bounding-sphere tests and cull
whole render objects, not individual triangles or submeshes. `DrawList` should remain unchanged unless diagnostics prove
it needs a render-object index later.

The math library currently has `perspective_lh` and `look_at_lh`, but no orthographic projection helper. CSM work should
add a tested `orthographic_lh` helper for the sun projection.

## Research Summary

Canonical CSM/PSSM sources describe the same four-step shape: split the camera frustum, compute a light
view-projection matrix per split, render one shadow map per split, then choose/sample the matching map while shading.
GPU Gems 3 documents the practical split scheme that blends logarithmic and uniform split positions with a lambda
parameter. Microsoft documents interval-based cascade selection, map-based selection, cascade blending, PCF filtering,
and depth bias caveats.

For OFG, use interval-based selection first because it is simple and maps directly to explicit transition distances.
Map-based selection can be revisited if stable cascade fitting wastes too much resolution or if cascade overlap becomes
important.

Soft shadows should not be designed as cascade blending. Cascade blending only hides the seam where two cascades have
different texel density. The first soft-shadow implementation should be PCF: sample several nearby depth comparisons and
average them. WGSL comparison samplers already return filtered comparison results when bilinear filtering is enabled, so
manual PCF can be built by offsetting UVs and averaging multiple compare-sampler calls. PCSS is a later upgrade for
contact-hardening shadows: search blockers, estimate penumbra radius, then run PCF with that radius. VSM/EVSM are also
later candidates if wide blur and prefiltering become more important than light-leak risk.

Depth bias is a core feature, not polish. The first implementation should include constant bias, slope-scaled depth bias
in the shadow caster pipeline, and a small receiver normal offset or shader-side depth bias if acne remains visible.
Bias must be cascade-aware because each cascade has a different world-units-per-texel scale.

Ray tracing pipelines are not viable for OFG's browser target now. WebGL does not expose Vulkan/DXR-style acceleration
structures, ray-generation/hit/miss shader stages, shader tables, or trace commands. WebGPU has compute shaders, so a
software ray query/path tracer could be written with storage buffers and a custom BVH, but that is not hardware ray
tracing and would be a separate renderer research project. It should not block CSM.

References used:

- Microsoft, Cascaded Shadow Maps: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps
- Microsoft, Common Techniques to Improve Shadow Depth Maps:
  https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
- NVIDIA GPU Gems 3, Chapter 10, Parallel-Split Shadow Maps:
  https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
- NVIDIA, Percentage-Closer Soft Shadows:
  https://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf
- W3C WGSL specification: https://www.w3.org/TR/WGSL/
- Khronos WebGL 2.0 specification: https://registry.khronos.org/webgl/specs/latest/2.0/
- GPUWeb WebGPU ray tracing issue: https://github.com/gpuweb/gpuweb/issues/535
- Khronos, Ray Tracing in Vulkan: https://www.khronos.org/blog/ray-tracing-in-vulkan
- Microsoft, DirectX Raytracing functional spec: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html

## Rendering Algorithm

### Cascade Splits

The first settings type should expose three cascade end distances in camera view space, plus a helper for generated
defaults:

    cascade_count = 3
    cascade_end_distances = [12.0, 32.0, 80.0]
    max_shadow_distance = cascade_end_distances[2]
    split_lambda = 0.5
    shadow_map_size = 1024
    pcf_mode = hard shadows first, then 5-tap PCF, then 9-tap PCF only if budgets pass
    shadow_intensity = 0.85
    shadow_fade_start_sun_elevation_degrees = 12.0
    shadow_fade_end_sun_elevation_degrees = 3.0
    shadow_matrix_min_sun_elevation_degrees = 5.0

Explicit distances satisfy the requested "transition at certain distances" behavior. A `practical_split_distances`
helper should also exist for tests and tuning:

    i is 1..cascade_count
    uniform_i = near_z + (shadow_far - near_z) * i / cascade_count
    logarithmic_i = near_z * pow(shadow_far / near_z, i / cascade_count)
    split_i = mix(uniform_i, logarithmic_i, split_lambda)

Clamp the final shadow distance to `camera.far_z` until OFG supports receiving shadows beyond the camera far plane.
When the current camera has `far_z = 80.0`, the default `[12, 32, 80]` maps cleanly to the existing render distance.
Validation must force the final end distance to `shadow_far`, require strictly increasing cascade ends, and require each
blend width to be smaller than the adjacent cascade interval it overlaps.

Each cascade has a primary range `[near_z, end0]`, `[end0, end1]`, and `[end1, end2]`. Cascade matrices must include
blend overlap so the next cascade is valid when sampled in the previous cascade's blend band. The simplest first rule is:

    cascade0 render range = [near_z, end0]
    cascade1 render range = [end0 - blend_width0, end1]
    cascade2 render range = [end1 - blend_width1, end2]

The shader selects by camera view-space depth:

    cascade_index = 0 when depth <= end0
    cascade_index = 1 when depth <= end1
    cascade_index = 2 otherwise, while depth <= end2

Pixels beyond `max_shadow_distance` receive full direct light visibility.

### Cascade Matrices

For each cascade:

1. Compute the eight world-space corners of the camera frustum slice from `CameraProperties`.
2. Compute a stable cascade center and radius. Start with a bounding sphere around the frustum slice corners.
3. Build a sun view matrix using the effective shadow light travel direction. The light "looks" along the direction
   light travels. This effective direction is the real sun direction except in the low-sun fade band, where its elevation
   is clamped to `shadow_matrix_min_sun_elevation_degrees` while the resulting shadow intensity fades toward zero.
4. Snap the light-space cascade center to shadow texel increments:

       world_units_per_texel = (2 * radius) / shadow_map_size
       snapped_center_xy = floor(center_xy / world_units_per_texel) * world_units_per_texel

5. Build a left-handed orthographic projection covering `[-radius, radius]` in X/Y and a configurable caster depth
   range in Z.
6. Store `shadow_clip_from_world[cascade]` plus texel size and bias values in a shadow uniform buffer.

Do not accept an unbounded low-sun caster range. For the first implementation, define a conservative but bounded caster
distance from settings, for example `max_caster_distance = 120.0` for near/mid cascades and `180.0` for the far cascade,
then record tested/accepted caster counts in diagnostics. Low-sun and far-cascade tests must prove that the caster plane
set remains finite.

### Shadow Caster Culling Volume

For each cascade, build shadow-caster culling planes from the camera-visible receiver region, not from the already
camera-culled opaque draw list:

1. Start with that cascade's overlapped receiver slice corners in world space.
2. Transform those corners into effective-light space using the clamped shadow light travel direction.
3. Compute light-space X/Y bounds from receiver corners plus a small texel margin.
4. Extend light-space Z toward incoming light by the configured `max_caster_distance`; keep the far side tight enough to
   include the receiver region.
5. Convert the resulting light-space orthographic box back into six inward-facing world-space culling planes.
6. Test every extracted render object against those planes to build the per-cascade shadow-caster draw list.

Tests must include an object outside the camera frustum whose shadow can reach a visible receiver, and an object outside
both the camera and caster plane set that is rejected.

### Shadow Map Rendering

Add `ShadowMapTarget`:

- texture format: `WGPUTextureFormat_Depth32Float`;
- dimensions: `shadow_map_size x shadow_map_size x 3 array layers`;
- usage: `WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding`;
- one `texture_depth_2d_array` view for sampling;
- one 2D depth view per cascade layer for render attachment use;
- comparison sampler with clamp-to-edge addressing, `compare = LessEqual`, and linear min/mag filters when supported by
  the standard depth comparison path.

Add `ShadowCasterPass`:

- depth-only render pipeline;
- vertex input: `MeshVertex` position at shader location 0;
- uniforms: one cascade view-projection matrix and per-draw model matrix;
- no fragment shader unless WebGPU validation requires a trivial one;
- depth write enabled, compare `Less`;
- fixed constant and slope-scaled depth bias configured in the pipeline;
- per-frame receiver/normal bias configured in uniforms during opaque sampling;
- draw the per-cascade shadow-caster draw list generated from extracted render objects, filtered to shadow-casting
  opaque geometry.

Render three passes per frame at first: one pass per cascade layer. This is simple and explicit. A later optimization can
use render bundles, multiview, or instanced/layered rendering if WebGPU support and OFG abstractions make that worthwhile.

### Opaque Shader Sampling

Extend the opaque PBR frame/shadow bind groups with:

- a new always-present group 3 shadow bind group;
- `ShadowFrameUniforms` at group 3 binding 0 containing three `shadow_clip_from_world` matrices, cascade end depths,
  blend widths, texel sizes, receiver/normal bias controls, effective shadow intensity, PCF mode, and enabled flags;
- `texture_depth_2d_array` shadow map at group 3 binding 1;
- `sampler_comparison` shadow sampler at group 3 binding 2.

The opaque pipeline layout must always include the shadow bind group so disabled shadows do not create a second material
pipeline family. When shadows are disabled or unavailable, bind a durable fallback depth texture array and uniforms with
`enabled = 0`. Update `PipelineCache` creation and keys so the shadow layout participates in the pipeline layout
contract and invalid reuse cannot happen.

The fragment shader computes shadow visibility only for the current sun direct light:

    shadow_visibility = sample_csm_shadow(world_position, normal, camera_view_depth)
    direct_shadow_multiplier = 1.0 - shadow_intensity * (1.0 - shadow_visibility)
    direct = direct_lighting * direct_shadow_multiplier
    color = ambient + direct

`shadow_intensity = 0.0` disables darkening. `shadow_intensity = 1.0` fully removes direct sun where the shadow map says
occluded, while ambient/sky light remains.

The first visible shadow milestone should implement hard shadows. Then add a measurable five-tap cross PCF mode. Only
promote nine-tap 3x3 PCF to the default if browser and native smoke budgets pass. Use a per-cascade texel radius so near
shadows can be crisp and far shadows can be slightly softer:

    near cascade radius: 1.0 texels
    mid cascade radius: 1.5 texels
    far cascade radius: 2.0 texels

The shader must early-out before sampling shadows when shadows are disabled, when the pixel is beyond
`max_shadow_distance`, when `n_dot_l <= 0`, or when projected shadow coordinates are outside valid UV/depth range.

### Cascade Blending

For transition smoothing, add a blend band at the end of cascade 0 and cascade 1. In the blend band, sample both the
current and next cascade and linearly interpolate:

    band_start = cascade_end - blend_width
    blend_t = saturate((view_depth - band_start) / blend_width)
    visibility = mix(visibility_current, visibility_next, blend_t)

Default blend widths should be explicit and conservative:

    blend_widths = [2.0, 5.0, 0.0]

This avoids a hard seam without pretending to be the source of soft shadow edges.

### Sun Integration

`Renderer` should build shadow cascades from the first current directional `LightProperties` item produced by
`build_light_properties(scene, output)`. If no live current sun exists, shadow rendering is skipped and opaque shading
uses direct visibility `1.0`.

The shadow subsystem should not mutate `Environment`, light entity transforms, time of day, or weather. It consumes the
resolved sun state exactly like `SkyPass` and `OpaquePass`.

Sun elevation is required behavior, not a later enhancement. Compute elevation from the surface-to-sun vector:

    surface_to_sun = normalize(-light_properties.m_direction)
    sun_elevation_degrees = degrees(asin(clamp(surface_to_sun.y, -1.0, 1.0)))

When the sun is below `shadow_fade_end_sun_elevation_degrees`, disable sun shadows. Between fade end and fade start,
multiply `shadow_intensity` by a smooth fade factor from 0 to 1. When building cascade matrices and caster planes, clamp
the effective surface-to-sun elevation to at least `shadow_matrix_min_sun_elevation_degrees` while preserving azimuth.
Because intensity is fading in this band, the clamp prevents infinite shadows without making low-angle artifacts
prominent. Storm or cloud settings may further reduce direct light and shadow intensity through `Environment`.

### Ray Tracing Answer

WebGL is not capable of using hardware ray tracing pipelines. WebGL 1/2 expose OpenGL ES style rasterization APIs, not
DXR/Vulkan RT style acceleration structures, ray-generation shaders, hit/miss shaders, shader tables, or trace commands.

Browser WebGPU also does not currently expose standard hardware ray tracing pipelines. The GPUWeb ray tracing extension
issue is still open. WebGPU compute can run a custom software ray tracer or shadow ray query over an engine-owned BVH,
but that would be a large custom renderer subsystem and not the same thing as using hardware ray tracing pipelines.

For OFG's browser target, CSM is the right first implementation. Keep the public shadow abstraction narrow enough that a
future ray-query backend could provide the same "sun visibility" value if browser standards eventually expose it.

## Plan of Work

Milestone 0 makes the default demo scene large and deterministic before culling or shadows are implemented.

Replace or evolve the current default demo scene with a scene builder that can be used by the app, browser smoke, native
smoke, and focused tests without relying on wall-clock randomness. The scene should keep the existing player/camera
controls usable, but it should add enough geometry to inspect culling and cascades:

- a ground plane large enough to cover the full camera far distance;
- at least 150 cubes or box-like mesh instances, reusing existing mesh/material resources rather than creating unique GPU
  resources per cube;
- varied dimensions, including low slabs, tall pillars, small crates, and wide blocks;
- varied transforms across near, mid, and far ranges relative to the default camera;
- deliberate intersections: boxes partially below the ground, boxes crossing each other, and clustered stacks;
- objects outside the starting camera frustum but near enough to become visible after a small camera turn;
- objects outside the camera frustum that should still become shadow casters once CSM lands;
- deterministic material/color categories so smoke screenshots can classify that the large scene loaded.

This should be the default app scene. If a tiny scene remains useful for isolated tests, keep it as a named test fixture,
not as the default. Browser/native smoke reports should include the default scene's object counts so accidental fallback
to the old small scene is visible.

Milestone 1 adds standard render bounds and a generic plane-set culling path, with camera frustum culling as the first
consumer.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\bounds.hpp`
- `C:\dev\ofg\cpp\src\render\bounds.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\frustum.hpp`
- `C:\dev\ofg\cpp\src\render\frustum.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\render_object.hpp`
- `C:\dev\ofg\cpp\src\render\render_object.cpp`

Add local mesh bounds to `Mesh`, recomputed when immutable vertices are initialized/replaced and when dynamic vertices
are updated in place. Add world-space bounds to extracted render objects. Replace direct scene-to-`DrawList` extraction
with a renderer-owned render object list, then create draw lists by testing render-object bounds against a supplied set
of culling planes. The first supplied plane set is the camera frustum. Keep `MeshRenderer::visible()` as an
authoring/logic flag that excludes objects before culling. Add culling counters for extracted, camera-visible, and
camera-culled objects.

The culling API must remain simple and explicit. It should expose inward-facing plane sets, a conservative
bounds-vs-plane-set test, and one function that appends accepted render objects to a `DrawList`. Tests must cover plane
normal orientation, objects fully inside, fully outside, intersecting/touching planes, non-uniform world scale, and an
empty plane set that accepts every bounded object.

This milestone should not add shadow-specific culling, occlusion culling, portals, terrain chunking, GPU-driven culling,
or LOD selection. Those are later systems. The goal is the basic CPU bounds/plane-set layer that every pass can share.

Milestone 2 adds shadow settings and CPU-side cascade math without touching GPU rendering.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\shadow_settings.hpp`
- `C:\dev\ofg\cpp\src\render\shadow_settings.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\shadow_cascade.hpp`
- `C:\dev\ofg\cpp\src\render\shadow_cascade.cpp`

Add `math::orthographic_lh` to `C:\dev\ofg\cpp\include\ofg\math\transform.hpp` and
`C:\dev\ofg\cpp\src\math\transform.cpp`. Add doctests covering split generation, explicit distance validation,
overlapped cascade ranges for blending, frustum-corner reconstruction, sun-view matrix creation, texel snapping, matrix
finite validation, low-sun intensity fade, low-sun effective angle clamp, disabled/no-sun behavior, and default settings.

Milestone 3 adds GPU shadow target ownership and the shadow caster pass.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\shadow_map_target.hpp`
- `C:\dev\ofg\cpp\src\render\shadow_map_target.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\shadow_caster_pass.hpp`
- `C:\dev\ofg\cpp\src\render\shadow_caster_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\shadow_caster.wgsl.hpp`

`Renderer` owns one `ShadowMapTarget` and one `ShadowCasterPass`. Renderer counters include shadow texture/view,
layout, bind group, buffer, and pipeline creation counts using existing shared counter categories where possible. Local
shadow-pass tests may also inspect sampler or shader-module creation if useful. Tests verify resize/reuse/release
behavior and that steady-state frames do not recreate durable resources. Shadow rendering must filter the extracted
render objects per cascade rather than using the already camera-culled opaque draw list, because off-camera casters can
still cast shadows into visible pixels.

Milestone 4 wires shadow sampling into opaque PBR.

Update:

- `C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp`
- `C:\dev\ofg\cpp\src\render\opaque_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\opaque_uber.wgsl.hpp`
- `C:\dev\ofg\cpp\src\render\renderer.cpp`

Add the always-present group 3 shadow bind group and update the opaque pipeline layout/cache contract. Stage the shader
work in three checkpoints: disabled/fallback plus hard shadows, five-tap PCF, then nine-tap PCF and cascade blending if
budgets pass. The shader applies shadow visibility only to direct sun lighting. Tests cover uniform packing, disabled
shadows, cascade selection boundaries, overlapped blend-band math through CPU helper functions, shadow intensity and
low-sun fade math, early-out conditions, and resource lifetime counters.

Milestone 5 integrates debug/status plumbing, performance diagnostics, smoke contracts, and visual validation.

Update docs and smoke contracts. Add `ShadowPassDiagnostics` beside the existing bloom diagnostics path, route it through
`RuntimeDebugStatus`, the embind/browser status surface, browser smoke, and native smoke JSON. Diagnostics should include
per-cascade tested/accepted caster counts, draw/submesh/index counts, encoded shadow pass count, map size, estimated
shadow depth bytes, selected PCF mode, effective sun elevation, effective shadow intensity, and whether the low-sun angle
clamp is active. Add screenshots after the large default scene lands, after first visible hard shadows, after PCF
softness, after cascade blend debugging, and before finalization. Store durable screenshots under
`C:\dev\ofg\artifacts\cascaded-shadows\`.

Add a debug mode if useful for smoke and human review:

- cascade color overlay;
- shadow map layer preview in native smoke artifacts or browser debug screenshot;
- counters for cascade count, map size, and shadow pass draw counts.

## Concrete Steps

Run from `C:\dev\ofg`.

1. Implement the large deterministic default scene.

    npm run test:cpp
    npm run build
    npm run smoke:render

Expected: scene/demo tests prove the default scene creates the expected number and spread of cubes without creating
unique mesh or material resources per instance; build succeeds; native smoke renders the large default scene and writes a
PNG/report with object-count fields proving it did not fall back to the old small scene.

2. Implement render bounds and camera frustum culling.

    npm run test:cpp

Expected: doctests pass for mesh local bounds, world bounds, plane normal orientation, frustum plane extraction,
sphere/AABB culling, inside/outside/intersecting plane cases, empty plane-set behavior, extracted render object counts,
invisible renderer exclusion, and camera-culled opaque draw-list counts.

3. Implement CPU shadow settings and cascade math.

    npm run test:cpp

Expected: doctests pass for shadow settings, split distances, overlapped blend ranges, orthographic projection, cascade
matrices, texel snapping, finite caster plane sets, sun-direction convention, low-sun fade, and low-sun angle clamp.

4. Implement shadow map target and shadow caster pass.

    npm run test:cpp

Expected: doctests pass for shadow target resize/release/reuse, per-cascade caster culling including off-camera casters,
low-sun/far-cascade caster-count diagnostics, and renderer counters. No browser visual change is required yet if opaque
sampling is still disabled.

5. Integrate opaque shadow sampling.

    npm run test:cpp
    npm run build

Expected: C++ tests pass and the browser/WASM build succeeds with the new WGSL shaders. Tests cover hard-shadow
sampling first, then PCF modes and cascade blending after the hard-shadow path is stable.

6. Run browser and native smoke with screenshots.

    npm run smoke:browser
    npm run smoke:render

Expected: smoke passes; screenshots or PNG artifacts show sun-cast shadows on the ground/player/cubes with no obvious
cascade seam in the default camera view. Smoke reports include shadow diagnostics: pass count, caster counts, draw/index
counts, map bytes, PCF mode, effective sun elevation, and effective intensity.

7. Run full validation and coverage.

    npm test
    npm run smoke
    npm run coverage

Expected: unit/integration tests, smoke, and coverage pass. Changed implementation files do not appear in the default
coverage attention report unless an exception is recorded here.

## Milestone Review

After each implementation milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Milestone 0 review, 2026-07-04 12:16 +01:00 / Codex:

- Scope: default large validation scene, demo-scene stats API, runtime debug-status demo-scene diagnostics, browser/native
  smoke reporting, active docs, and focused tests.
- Reviewers: contract, code quality, legacy/docs, correctness, and validation passes were run locally. Sub-agent tools
  were available, but their tool contract requires an explicit user request for sub-agents, so no sub-agents were
  spawned for this milestone review.
- Required findings fixed: browser smoke did not report or assert the default scene object counts while native smoke did.
  The fix added `RuntimeDemoSceneStatus` to `RuntimeDebugStatus`, populated it from `demo_scene_validation_stats()`,
  parsed it in `src/app/wasmRuntime.ts`, asserted it in `tools/browser-smoke.mjs`, and kept native report output aligned.
- Follow-ups recorded: none for Milestone 0. The remaining culling/shadow work is already tracked by Milestones 1-5.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run test:cpp`, `npm run test:ts`, `npm run smoke:render`,
  `npm run smoke:browser`, `npm run coverage`, `npm run format:cpp:check`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: the scene is intentionally still rendered without camera culling, so frame cost rises until Milestone 1
  adds bounded render extraction and frustum culling. Smoke images were inspected and remain readable.

Milestone 1 review, 2026-07-04 12:45 +01:00 / Codex:

- Scope: mesh bounds, frustum/plane-set culling, bounded render-object extraction, renderer camera-culling integration,
  runtime/browser/native culling diagnostics, active docs, coverage summaries, and focused C++/TypeScript tests.
- Reviewers: contract, code quality, legacy/docs, correctness, and validation passes were run locally. Sub-agent tools
  were not used because the user did not explicitly request delegated sub-agent review for this milestone.
- Required findings fixed: active contracts/docs mentioned the large demo-scene diagnostics but not `renderCulling`, and
  still described the renderer as directly converting visible mesh renderers into the draw list. The fix updated
  `docs\API_CONTRACTS.md`, `docs\SYSTEMS.md`, and this ExecPlan to describe bounded render-object extraction,
  camera-frustum draw-list filtering, and culling diagnostics.
- Follow-ups recorded: `src/app/wasmRuntime.ts` is 584 lines after adding nested debug-status parsing; split parser
  helpers before adding much more runtime diagnostic surface.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run test:cpp`, `npm run test:ts`, `npm run smoke:render`,
  `npm run smoke:browser`, `npm run coverage`, `npm run format:cpp:check`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: this is whole-object CPU frustum culling only. It intentionally does not add occlusion culling,
  submesh/triangle culling, terrain chunk culling, or the shadow-specific caster-volume culling that Milestone 3 needs.

Milestone 2 review, 2026-07-04 13:05 +01:00 / Codex:

- Scope: shadow settings API, orthographic projection helper, CPU cascade matrix construction, low-sun fade/clamp,
  cascade culling planes, focused C++ tests, active docs, coverage summaries, and smoke report inspection.
- Reviewers: contract, code quality, legacy/docs, correctness, and validation passes were run locally. Sub-agent tools
  were not used because the user did not explicitly request delegated sub-agent review for this milestone.
- Required findings fixed: no behavioral required findings. A small code-quality polish made `ShadowCascadeSet` direction
  vector defaults explicit in the public header.
- Follow-ups recorded: none beyond the already-planned Milestone 3 GPU shadow target/caster culling and Milestone 4
  opaque shader sampling work.
- Rejected findings: none.
- Validation rerun: `npm run test:cpp`, `npm run format:cpp:check`, and
  `git -c safe.directory=C:/dev/ofg diff --check` after the final header polish. Earlier Milestone 2 validation also
  passed `npm run format:cpp`, `npm run coverage`, `npm run smoke:render`, and `npm run smoke:browser`.
- Remaining risk: cascade culling planes currently bound the texel-snapped receiver/caster light volume for CPU tests, but
  no GPU shadow maps or visible shadows are encoded until Milestone 3 and Milestone 4.

Milestone 3 review, 2026-07-04 13:44 +01:00 / Codex:

- Scope: `ShadowMapTarget`, `ShadowCasterPass`, depth-only WGSL shader, renderer-owned shadow resources/diagnostics,
  per-cascade caster culling, renderer integration with the current sun light, focused C++ tests, active docs, coverage
  summaries, and native/browser smoke reports/screenshots.
- Reviewers: contract, code quality, legacy/docs, correctness, and validation passes were run locally. Sub-agent tools
  were not used because the user did not explicitly request delegated sub-agent review for this milestone.
- Required findings fixed: `ShadowMapTarget::view_generation()` did not increment when `resize(0)` released texture
  views. The fix made texture/view release report whether handles changed, increments the generation on zero-size release
  and public `release()`, and added doctest assertions for release/recreate sequencing.
- Follow-ups recorded: none beyond the already-planned Milestone 4 opaque shader sampling and Milestone 5 public
  shadow diagnostics/smoke contract work. The existing `src/app/wasmRuntime.ts` split-pressure follow-up still applies
  before adding Milestone 5 runtime shadow status parsing.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run test:cpp`, `npm run coverage`, `npm run smoke:render`,
  `npm run smoke:browser`, `npm run format:cpp:check`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: this milestone encodes hidden shadow maps only. Opaque sampling, visible hard/soft shadows, cascade
  blending in the shader, and runtime/browser shadow diagnostics remain Milestone 4 and Milestone 5 work.

Milestone 4 review, 2026-07-04 14:12 +01:00 / Codex:

- Scope: `ShadowFrameState`, opaque pass group-3 shadow resources and fallback binding, four-group opaque pipeline cache
  layout, renderer current-sun shadow frame wiring, opaque WGSL shadow sampling/PCF/cascade blending/intensity, focused
  tests, active docs, coverage summaries, and native/browser smoke reports/screenshots.
- Reviewers: contract, code quality, legacy/docs, correctness, and validation passes were run locally. Sub-agent tools
  were not used because the user did not explicitly request delegated sub-agent review for this milestone.
- Required findings fixed: the first native smoke exposed that `textureSampleCompare` is rejected from the shader's
  cascade-dependent control flow. The fix switched opaque shadow lookups to `textureSampleCompareLevel` and reran native
  and browser smoke successfully.
- Follow-ups recorded: `cpp\src\render\opaque_pass.cpp` is now 586 lines, inside the repo's small-concern range. Before
  adding more opaque-renderer features, split the shadow binding/fallback helpers or shared pass resource utilities out
  of this file. Public runtime shadow diagnostics and optional cascade debug visuals remain scheduled for Milestone 5.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run test:cpp`, `npm run build`, `npm run smoke:render`,
  `npm run smoke:browser`, `npm run coverage`, `npm run format:cpp:check`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: the default visual path exercises five-tap PCF and cascade blending, but there is not yet a dedicated
  hard-shadow screenshot or cascade-overlay debug view. Milestone 5 owns the public shadow diagnostics and debug/smoke
  visual surface.

## Validation and Acceptance

Functional acceptance:

- The default app/demo scene is a deterministic large validation scene.
- The validation scene contains enough reused cube/box instances across near, mid, and far distances to visibly exercise
  camera culling and all three shadow cascades.
- The validation scene intentionally includes some overlapping/intersecting cubes and some cubes partly intersecting the
  ground.
- The validation scene includes off-camera objects that are useful for testing camera culling and later shadow-caster
  culling.
- Mesh resources expose local render bounds derived from their CPU vertices.
- Renderer extraction produces bounded render objects from authored-visible `MeshRenderer` components before pass
  filtering.
- A simple plane-set culling API generates draw lists from bounded render objects.
- Opaque rendering uses camera frustum culling and reports extracted/visible/culled counts.
- Shadow rendering does not reuse the camera-culled opaque draw list; each cascade performs its own caster culling from
  extracted render objects using planes derived from the overlapped visible receiver region and effective sun direction.
- Renderer owns a durable shadow-map target with exactly three cascade layers by default.
- The current sun directional light is the only light that casts shadows in this plan.
- CSM uses `LightProperties::m_direction` as the light travel direction and does not read or reintroduce
  `Scene::main_light()`.
- Cascades transition at configurable camera view-space distances.
- Adjacent cascades overlap enough for blend-band sampling to be valid.
- Cascade blending hides visible seams at cascade boundaries.
- PCF softens shadow edges independently of cascade blending.
- Shadow intensity controls direct-light darkening without changing ambient lighting.
- Shadow intensity fades to zero as sun elevation drops from the configured fade-start angle to the configured fade-end
  angle.
- The effective shadow matrix/caster direction clamps to a minimum sun elevation during the low-sun fade band so shadows
  and caster volumes do not become infinite.
- Camera movement does not cause obvious cascade swimming in ordinary play movement.
- No shadows are rendered when shadows are disabled, when no current sun exists, or when the sun is below the horizon.
- Renderer counters prove steady-state frames update uniforms and redraw depth, but do not recreate durable shadow
  textures, views, samplers, bind group layouts, shader modules, pipelines, or size-independent bind groups.
- Smoke/debug diagnostics report shadow pass count, per-cascade caster/draw/index counts, shadow map bytes, PCF mode,
  effective sun elevation, and effective shadow intensity.
- Browser and native smoke both show shadowed geometry from the same sun direction.

Test acceptance:

- `npm run test:cpp` passes after each C++ milestone.
- `npm run test:ts` passes when runtime debug status, TypeScript status parsing, or smoke helpers change.
- `npm test` passes before completion.
- `npm run smoke:browser` passes and stores or reports a screenshot with visible sun shadows.
- `npm run smoke:render` passes and writes a PNG/report with visible sun shadows.
- `npm run coverage` passes, with changed implementation files above the documented threshold or explicit exceptions
  recorded here.

Screenshot acceptance:

- Take and present screenshots after the large default scene lands, after hard shadows first appear, after PCF softness
  lands, after cascade blending lands, and before finalization. Camera culling acceptance should use counters/debug
  diagnostics rather than relying on a screenshot, because correct culling should not change visible output.
- Store durable screenshots under `C:\dev\ofg\artifacts\cascaded-shadows\` or the smoke artifact directory.

## Idempotence and Recovery

Shadow resources must be additive to the renderer lifecycle. `Renderer::release` must release `ShadowCasterPass` and
`ShadowMapTarget` even if preparation failed after only one was created. Repeated `Renderer::prepare` after ready must
not create duplicate shadow resources. Repeated resize with the same shadow settings must not recreate shadow textures.
Zero-size platform resize should leave shadow resources valid or released according to the renderer target policy, but
must not crash.

Settings changes must use explicit dirty behavior. Shadow intensity, fade angles, clamp angle, cascade distances, blend
widths, receiver bias, normal bias, and PCF mode update CPU cascade data and uniforms only. Shadow map size and format
are texture-dirty settings and recreate `ShadowMapTarget` only when values actually change. Static WebGPU pipeline
depth-bias changes, if exposed, are pipeline-dirty settings and deliberately recreate the shadow caster pipeline.

If shadow sampling breaks opaque rendering, keep the shadow caster pass disabled and force shader visibility to `1.0`
while retaining the CPU-side cascade tests. If shadow caster rendering fails but opaque rendering works, skip the shadow
pass and bind a disabled shadow uniform state. Do not mark the milestone complete until the intended shadowed path works
or this plan is explicitly revised.

If depth comparison sampling with `Depth32Float` exposes a browser/native portability issue, switch the shadow map format
after recording the failing backend and evidence here. Preserve the public `ShadowMapTarget::format()` API so the change
does not leak across the renderer.

## Artifacts and Notes

Milestone 0 validation evidence, 2026-07-04 12:16 +01:00 / Codex:

    npm run format:cpp
    npm run test:cpp
    npm run test:ts
    npm run smoke:render
    npm run smoke:browser
    npm run coverage
    npm run format:cpp:check
    git -c safe.directory=C:/dev/ofg diff --check

All commands passed. Coverage reported `cpp\src\render\demo_scene.cpp line coverage 92.96%`,
`cpp\src\runtime\runtime_debug_status.cpp line coverage 100.00%`, and TypeScript checked-file coverage passed. Native
smoke wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and
`C:\dev\ofg\artifacts\render-smoke\report.json`; browser smoke wrote
`C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and
`C:\dev\ofg\artifacts\browser-smoke\report.json`.

Both smoke reports contain:

    demoScene.name = "large-default-culling-shadow-validation"
    demoScene.boxCount = 184
    demoScene.nearBoxCount = 22
    demoScene.midBoxCount = 79
    demoScene.farBoxCount = 83
    demoScene.partlyBelowGroundCount = 24
    demoScene.overlapClusterBoxCount = 24
    demoScene.offCameraCandidateCount = 16

Native smoke pixel evidence after the camera adjustment:

    backgroundRatio = 0.308864
    groundRatio = 0.598502
    coloredRatio = 0.0926342
    nonBackgroundColorBuckets = 29
    passed = true

Browser smoke pixel evidence:

    backgroundRatio = 0.2738576779026217
    groundRatio = 0.6020724094881398
    coloredRatio = 0.12406991260923846
    nonBackgroundColorBuckets = 31

Milestone 1 validation evidence, 2026-07-04 12:45 +01:00 / Codex:

    npm run format:cpp
    npm run test:cpp
    npm run test:ts
    npm run smoke:render
    npm run smoke:browser
    npm run coverage
    npm run format:cpp:check
    git -c safe.directory=C:/dev/ofg diff --check

All commands passed. Coverage reported `cpp\src\render\bounds.cpp line coverage 100.00%`,
`cpp\src\render\frustum.cpp line coverage 100.00%`, `cpp\src\render\render_object.cpp line coverage 96.36%`,
`cpp\src\render\renderer.cpp line coverage 91.39%`, `cpp\src\resources\mesh.cpp line coverage 93.93%`,
`cpp\src\runtime\runtime_debug_status.cpp line coverage 100.00%`, and TypeScript checked-file coverage passed. Native
smoke wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and
`C:\dev\ofg\artifacts\render-smoke\report.json`; browser smoke wrote
`C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and
`C:\dev\ofg\artifacts\browser-smoke\report.json`.

Native smoke culling evidence:

    renderCulling.extractedObjectCount = 186
    renderCulling.cameraVisibleObjectCount = 118
    renderCulling.cameraCulledObjectCount = 68

Browser smoke culling evidence after the imported player model loaded:

    renderCulling.extractedObjectCount = 188
    renderCulling.cameraVisibleObjectCount = 105
    renderCulling.cameraCulledObjectCount = 83

Milestone 2 validation evidence, 2026-07-04 13:05 +01:00 / Codex:

    npm run format:cpp
    npm run test:cpp
    npm run coverage
    npm run smoke:render
    npm run smoke:browser
    npm run format:cpp:check
    git -c safe.directory=C:/dev/ofg diff --check

All commands passed. Coverage reported `cpp\src\math\transform.cpp line coverage 100.00%`,
`cpp\src\render\shadow_cascade.cpp line coverage 91.44%`, `cpp\src\render\shadow_settings.cpp line coverage 95.24%`,
and TypeScript checked-file coverage passed. Native smoke wrote
`C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; browser smoke
wrote `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and
`C:\dev\ofg\artifacts\browser-smoke\report.json`. These smoke images were inspected and remained healthy; no visible
shadows are expected before GPU shadow-map rendering and opaque sampling land.

Native smoke culling evidence after CPU shadow math landed:

    renderCulling.extractedObjectCount = 186
    renderCulling.cameraVisibleObjectCount = 118
    renderCulling.cameraCulledObjectCount = 68

Browser smoke culling evidence after CPU shadow math landed:

    renderCulling.extractedObjectCount = 188
    renderCulling.cameraVisibleObjectCount = 105
    renderCulling.cameraCulledObjectCount = 83

Milestone 3 validation evidence, 2026-07-04 13:44 +01:00 / Codex:

    npm run format:cpp
    npm run test:cpp
    npm run coverage
    npm run smoke:render
    npm run smoke:browser
    npm run format:cpp:check
    git -c safe.directory=C:/dev/ofg diff --check

All commands passed. Coverage reported `cpp\src\render\shadow_caster_pass.cpp line coverage 94.90%`
with 28 defensive WebGPU null-return/cleanup lines excluded, `cpp\src\render\shadow_map_target.cpp line coverage
94.82%` with 13 defensive WebGPU null-return/cleanup lines excluded, `cpp\src\render\shadow_cascade.cpp line
coverage 92.79%`, and `cpp\src\render\renderer.cpp line coverage 91.93%`. TypeScript checked-file coverage passed.
Generated coverage summaries match the committed `docs\coverage` copies by SHA-256. Native smoke wrote
`C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; browser smoke
wrote `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\browser-smoke\report.json`.
These smoke images were inspected and remain healthy; no visible shadows are expected until Milestone 4 opaque sampling.
The local review server is available at `http://127.0.0.1:5173`.

Native smoke evidence after hidden shadow-map rendering landed:

    renderCulling.extractedObjectCount = 186
    renderCulling.cameraVisibleObjectCount = 118
    renderCulling.cameraCulledObjectCount = 68
    pipelineCreateCount = 12
    bufferCreateCount = 10
    backgroundRatio = 0.308864
    groundRatio = 0.598502
    coloredRatio = 0.0926342
    nonBackgroundColorBuckets = 29

Browser smoke evidence after hidden shadow-map rendering landed:

    renderCulling.extractedObjectCount = 188
    renderCulling.cameraVisibleObjectCount = 105
    renderCulling.cameraCulledObjectCount = 83
    pipelineCreateCount = 15
    bufferCreateCount = 10
    backgroundRatio = 0.27370786516853934
    groundRatio = 0.6001248439450687
    coloredRatio = 0.126167290886392
    nonBackgroundColorBuckets = 31

Milestone 4 validation evidence, 2026-07-04 14:12 +01:00 / Codex:

    npm run format:cpp
    npm run test:cpp
    npm run build
    npm run smoke:render
    npm run smoke:browser
    npm run coverage
    npm run format:cpp:check
    git -c safe.directory=C:/dev/ofg diff --check

All final commands passed. The first `npm run smoke:render` in this milestone failed WebGPU shader validation because
`textureSampleCompare` was called from non-uniform cascade control flow. Opaque shadow sampling was changed to
`textureSampleCompareLevel`, then native and browser smoke both passed. Coverage reported
`cpp\src\render\opaque_pass.cpp line coverage 92.98%`, `cpp\src\render\pipeline_cache.cpp line coverage 96.72%`,
`cpp\src\render\renderer.cpp line coverage 92.20%`, `cpp\src\render\shadow_frame_state.cpp line coverage 95.83%`,
`cpp\src\render\shadow_cascade.cpp line coverage 92.79%`, `cpp\src\render\shadow_caster_pass.cpp line coverage 94.90%`
with 28 existing defensive WebGPU null-return/cleanup lines excluded, and
`cpp\src\render\shadow_map_target.cpp line coverage 94.82%` with 13 existing defensive WebGPU null-return/cleanup lines
excluded. TypeScript checked-file coverage passed. Generated coverage summaries match the committed `docs\coverage`
copies by SHA-256: C++ `0DECE23EBFC4BC55690D2E48CC2E2243869741CFA95E0B3D5147A83AF9F4D46C`, TypeScript
`2EE4B92EB9F83C76305BB92D481340EF5216482CE4D7470086EC240FB316593C`.

Native smoke wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and
`C:\dev\ofg\artifacts\render-smoke\report.json`; a durable shadow milestone copy is
`C:\dev\ofg\artifacts\cascaded-shadows\m4-native-visible-pcf.png`. Browser smoke wrote
`C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\browser-smoke\report.json`; a durable
shadow milestone copy is `C:\dev\ofg\artifacts\cascaded-shadows\m4-browser-visible-pcf.png`. Both screenshots were
inspected and show visible softened sun shadows in the large default validation scene. The local review server remains
available at `http://127.0.0.1:5173`.

Native smoke evidence after visible opaque shadow sampling landed:

    renderCulling.extractedObjectCount = 186
    renderCulling.cameraVisibleObjectCount = 118
    renderCulling.cameraCulledObjectCount = 68
    pipelineCreateCount = 12
    bufferCreateCount = 11
    backgroundRatio = 0.308864
    groundRatio = 0.47186
    coloredRatio = 0.219276
    nonBackgroundColorBuckets = 28
    passed = true

Browser smoke evidence after visible opaque shadow sampling landed:

    renderCulling.extractedObjectCount = 188
    renderCulling.cameraVisibleObjectCount = 105
    renderCulling.cameraCulledObjectCount = 83
    pipelineCreateCount = 15
    bufferCreateCount = 11
    backgroundRatio = 0.2737827715355805
    groundRatio = 0.4401248439450687
    coloredRatio = 0.2860923845193508
    nonBackgroundColorBuckets = 27

Research conclusion on ray tracing:

WebGL cannot use hardware ray tracing pipelines. Browser WebGPU cannot use standard hardware ray tracing pipelines today
either. Software ray tracing through fragment or compute shaders is possible in principle, but it would require custom
acceleration structures and would not be a practical first sun-shadow implementation for OFG.

## Interfaces and Dependencies

Expected new or changed interfaces by the end:

- `C:\dev\ofg\cpp\include\ofg\render\demo_scene.hpp`
  - deterministic large culling/shadow validation scene as the default demo
  - reusable scene builder that creates many box instances from shared mesh/material resources
  - test-visible counts for total validation boxes and broad near/mid/far distribution

- `C:\dev\ofg\cpp\src\native\render_smoke.cpp` and browser smoke tooling as needed
  - report fields identifying the large default scene and expected object counts
  - shadow diagnostics fields once shadows land

- `C:\dev\ofg\cpp\include\ofg\render\bounds.hpp`
  - `struct Bounds3`
  - `struct BoundingSphere`
  - finite/empty validation helpers
  - local-to-world transform helpers for conservative bounds
  - mesh-vertex bounds helpers

- `C:\dev\ofg\cpp\include\ofg\render\frustum.hpp`
  - `struct CullingPlane`
    - inward-facing normalized normal plus signed distance
  - `struct CullingPlaneSet`
    - span-like plane view with no ownership
  - `class ViewFrustum` or equivalent value type
  - `ViewFrustum view_frustum_from_camera(const CameraProperties& camera)`
  - `bool intersects_culling_planes(Bounds3 world_bounds, CullingPlaneSet planes)`
  - sphere/AABB intersection helpers

- `C:\dev\ofg\cpp\include\ofg\render\render_object.hpp`
  - `struct RenderObject`
  - non-owning mesh/material/property data copied from `MeshRenderer`
  - model matrix, sort origin, local bounds, world bounds, and stable scene extraction index
  - `void append_culled_draws(std::span<const RenderObject> objects, CullingPlaneSet planes, DrawList& output, CullingStats& stats)`

- `C:\dev\ofg\cpp\include\ofg\resources\mesh.hpp`
  - `Bounds3 Mesh::local_bounds() const noexcept` or equivalent
  - bounds recomputed by `init`, `init_dynamic_vertices`, `replace_vertices`, and `update_vertices_in_place`

- `C:\dev\ofg\cpp\include\ofg\render\draw_list.hpp`
  - draw commands may carry world bounds or a render-object index if needed by later pass diagnostics

- `C:\dev\ofg\cpp\include\ofg\render\renderer_counters.hpp`
  - per-frame or last-frame culling counters for extracted, camera-visible, camera-culled, and shadow-caster counts

- `C:\dev\ofg\cpp\include\ofg\math\transform.hpp`
  - `std::optional<Mat4> orthographic_lh(float left, float right, float bottom, float top, float near_z, float far_z, std::string& error)`

- `C:\dev\ofg\cpp\include\ofg\render\shadow_settings.hpp`
  - `struct ShadowSettings`
  - `enum class ShadowPcfMode { Hard, FiveTap, NineTap }`
  - cascade count fixed to `3` for the first implementation
  - default `map_size = 1024`
  - cascade end distances, blend widths, map size, PCF radius, receiver depth bias, normal bias, intensity, low-sun fade
    angles, shadow matrix minimum sun elevation, enabled flag, and validation helpers
  - static caster-pipeline bias defaults kept separate from per-frame receiver bias
  - `std::array<float, 3> practical_split_distances(float near_z, float far_z, float lambda)`

- `C:\dev\ofg\cpp\include\ofg\render\shadow_cascade.hpp`
  - `struct ShadowCascade`
  - `struct ShadowCascadeSet`
  - `ShadowCascadeSet build_shadow_cascades(const CameraProperties& camera, math::Vec3 light_direction, const ShadowSettings& settings)`
  - `CullingPlaneSet` or owned plane storage for each cascade's shadow-caster volume
  - helpers for low-sun fade factor and effective clamped shadow light direction

- `C:\dev\ofg\cpp\include\ofg\render\shadow_frame_state.hpp`
  - compact CPU-to-WGSL opaque shadow uniform contract
  - disabled/fallback and live shadow frame-state builders
  - helpers that pack cascade matrices, split distances, blend widths, texel sizes, bias, intensity, PCF mode, and map
    size into the opaque group-3 uniform layout

- `C:\dev\ofg\cpp\include\ofg\render\shadow_map_target.hpp`
  - `class ShadowMapTarget`
  - `static constexpr std::uint32_t cascade_count() noexcept`
  - `static constexpr WGPUTextureFormat format() noexcept`
  - `void resize(std::uint32_t size)`
  - array sampling view, per-cascade render views, sampler, generation, counters, and release helpers

- `C:\dev\ofg\cpp\include\ofg\render\shadow_caster_pass.hpp`
  - `class ShadowCasterPass`
  - durable shader module, pipeline, layouts, buffers, bind groups, counters
  - `void render(WGPUCommandEncoder encoder, ShadowMapTarget& target, const ShadowCascadeSet& cascades, std::span<const RenderObject> render_objects)` or an equivalent API that preserves per-cascade caster culling before draw submission
  - `ShadowPassDiagnostics diagnostics() const noexcept`

- `C:\dev\ofg\cpp\include\ofg\render\shadow_diagnostics.hpp` or equivalent
  - `struct ShadowCascadeDiagnostics`
  - `struct ShadowPassDiagnostics`
  - per-cascade tested/accepted caster counts, draw/submesh/index counts, pass count, map size, estimated bytes, PCF
    mode, effective sun elevation, effective intensity, and low-sun clamp flag

- `C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp`
  - pass receives a compact `ShadowFrameState` plus group 3 shadow bind group resources
  - opaque shader applies shadow visibility to direct lighting

- `C:\dev\ofg\cpp\include\ofg\render\pipeline_cache.hpp`
  - pipeline creation accepts the shadow bind group layout
  - the cache key/layout contract prevents reuse between incompatible shadow-layout pipeline shapes

- `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp`
  - renderer owns `ShadowSettings`, `ShadowMapTarget`, and `ShadowCasterPass`
  - renderer builds shadow cascades from resolved `LightProperties` sun state before opaque rendering
  - counters and diagnostics include shadow resources and last-frame shadow pass stats

- `C:\dev\ofg\cpp\include\ofg\runtime\runtime_debug_status.hpp`, `C:\dev\ofg\cpp\src\runtime\runtime_debug_status.cpp`,
  `C:\dev\ofg\cpp\include\ofg\game\game.hpp`, `C:\dev\ofg\cpp\src\game\game.cpp`,
  `C:\dev\ofg\cpp\src\web\browser_game.cpp`, and `C:\dev\ofg\src\app\wasmRuntime.ts`
  - currently expose default demo-scene identity/count diagnostics and render-culling extracted/visible/culled counts
  - later expose shadow diagnostics through the same status path used by existing renderer diagnostics

- `C:\dev\ofg\tools\browser-smoke.mjs`, `C:\dev\ofg\tools\browser-smoke-cpp.mjs`,
  `C:\dev\ofg\tools\smoke-render-cpp.mjs`, and `C:\dev\ofg\tools\smoke-contract.json`
  - validate default large-scene object counts and shadow diagnostic budgets

- `C:\dev\ofg\cpp\CMakeLists.txt`
  - add new implementation and doctest files

The implementation must not add a third-party engine or runtime dependency.
