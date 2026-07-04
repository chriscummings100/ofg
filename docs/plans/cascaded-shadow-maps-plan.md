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

Before culling or shadows land, OFG needs a larger deterministic validation scene. The current plane-and-few-cubes scene
is too small to prove culling and cascade transitions. Add a broad demo scene containing many cubes with varied sizes,
heights, rotations, spacing, and intentional intersections with each other and the ground so the renderer has enough
near, mid, far, partly hidden, and off-camera objects to exercise the systems.

This plan is intentionally tied to `C:\dev\ofg\docs\plans\procedural-sky-environment-plan.md`. The sky plan is adding
`Environment`, `Light`, and sun selection. Shadow rendering should consume that sun state rather than introduce a second
sun concept. If shadow implementation starts before the sky light contract has landed, use the current
`Scene::main_light()` only as a temporary adapter and retire it once `LightProperties` exists.

This plan also includes a standard render-culling prerequisite. OFG currently skips manually hidden renderers, but it
does not have mesh bounds, camera frustum culling, or shadow-caster culling. Add those foundations before CSM so opaque
rendering and shadow rendering can each generate a draw list from a pass-specific set of culling planes.

## Progress

- [x] (2026-07-04) Completed initial research pass for cascaded shadow maps, soft shadow filtering, cascade blending,
  shadow-map biasing, WebGPU depth comparison sampling, and browser ray tracing pipeline availability.
- [x] (2026-07-04) Read the current renderer, opaque pass, scene lighting, camera math, active sky ExecPlan, API
  contracts, and guide docs to place the shadow plan in OFG's architecture.
- [x] (2026-07-04) Confirmed current renderer has no standard culling beyond `MeshRenderer::visible()`.
- [ ] Review this research plan with the user and adjust defaults for cascade distances, map resolution, and softness.
- [ ] Implement Milestone 0: large deterministic culling/shadow validation scene.
- [ ] Implement Milestone 1: render bounds, extracted render objects, camera frustum culling, and culling stats.
- [ ] Implement Milestone 2: shadow math/settings types and CPU-side cascade split/matrix tests.
- [ ] Implement Milestone 3: shadow-map array target and shadow caster depth pass with per-cascade caster culling.
- [ ] Implement Milestone 4: sample shadows in the opaque PBR shader with intensity, bias, PCF, and cascade blending.
- [ ] Implement Milestone 5: integrate the current sun, smoke/debug visuals, screenshots, docs, and coverage.

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

- Observation: The local renderer already has part of the sky/HDR groundwork in the working tree.
  Evidence: `C:\dev\ofg\cpp\include\ofg\render\scene_color_target.hpp`, `depth_target.hpp`, and `tone_map_pass.hpp`
  exist as untracked or modified local files; `Renderer::render_impl` currently renders opaque content into scene color,
  then tone maps.

- Observation: The current checked C++ scene lighting contract still exposes `Scene::main_light()` and
  `Scene::ambient_light()`.
  Evidence: `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp` defines `DirectionalLight`, `AmbientLight`, and scene setters;
  the active sky plan intends to replace direct light storage with `Light` components and `Environment`.

- Observation: Current render extraction has no frustum, distance, occlusion, or shadow-caster culling.
  Evidence: `Renderer::build_draw_list_from_scene` iterates every `scene.mesh_renderer_count()` entry and only skips
  renderers where `MeshRenderer::visible()` is false. `DrawList` does not store world bounds, and `Mesh` does not store
  local bounds.

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

## Outcomes & Retrospective

Research plan drafted. No implementation has started. This section must be updated after each milestone with what
changed, what screenshots showed, and any remaining gaps.

## Contract and Quality Baseline

`OFG-BOOT-001 TypeScript Host Ownership` must be preserved. TypeScript may display debug data or smoke artifacts, but it
must not own shadow cascade calculation, shadow GPU resources, draw submission, or light selection.

`OFG-BOOT-002 C++ Runtime Ownership` changes after the sky plan lands because shadows should consume `Environment` and
scene-owned `Light` components. If shadows start before that, the temporary `Scene::main_light()` bridge must be recorded
and removed by the integration milestone.

`OFG-BOOT-004 Renderer Compatibility` changes because browser and native smoke must validate the same shadowed renderer
path. The smoke visual contract should add shadow-aware assertions rather than rely only on lit color categories.

`OFG-BOOT-005 WebGPU Baseline` must be preserved. The first CSM implementation must request no optional GPU features and
must not manually request higher adapter limits. Shadow maps should use standard render attachments, texture bindings,
depth comparison samplers, and ordinary render passes.

`OFG-BOOT-006 Resource Lifetime` must be preserved. Shadow map textures/views, comparison samplers, bind group layouts,
shader modules, pipelines, and uniform buffers are durable renderer resources. Ordinary frames may update shadow uniforms
and render depth, but must not recreate pipelines or size-independent bind groups. Resize or setting changes may recreate
size-dependent shadow textures/views.

`OFG-BOOT-009 Coverage` applies. Each modified implementation file must pass the default coverage attention gate,
currently about 90% line coverage unless this plan records an explicit exception with rationale.

## Context and Orientation

The current renderer is C++ owned. `Renderer::render_impl` in `C:\dev\ofg\cpp\src\render\renderer.cpp` resolves the main
camera into `CameraProperties`, builds a transient `DrawList` from visible `MeshRenderer` components, renders opaque
content, and tone maps the HDR scene color target. Shadow rendering should slot in before the opaque scene pass:

    extract visible-by-flag render objects with world bounds
    resolve camera
    resolve current sun light
    camera-cull render objects into the opaque draw list
    compute three sun-shadow cascades
    per-cascade cull shadow casters into shadow draw lists
    render shadow draw lists into shadow-map depth layers
    render opaque PBR into HDR scene color while sampling shadows
    render sky
    tone map to platform output

The active sky plan intends to add `LightProperties` and `Environment`. The shadow system should consume the same
current sun directional light used by sky rendering. Directional light direction in the sky plan is the direction light
travels, derived from the light entity's world `+Z`. The opaque shader currently computes the vector from surface to
light as `-main_light_direction`, so shadow code should preserve that convention.

`CameraProperties` in `C:\dev\ofg\cpp\include\ofg\render\camera_properties.hpp` provides left-handed camera matrices
with camera-local `+Z` forward and WebGPU depth range `[0, 1]`. Shadow cascade math should use this camera snapshot,
not scene camera pointers.

`DrawList` already carries mesh pointers, model matrices, material overrides, and submesh ranges. The first shadow
caster pass can reuse this draw list and draw indexed geometry with a depth-only shader. Because CPU skinning updates
dynamic mesh vertices before rendering, skinned player geometry can cast shadows through the same vertex buffers.

Today `DrawList` is built directly from scene mesh renderers. Culling work should split this into two steps. First,
renderer extraction creates bounded render objects from all authored-visible `MeshRenderer` components. Then each pass
filters those objects into its own draw list. Opaque rendering uses camera frustum culling. Shadow rendering uses each
cascade's light-space caster volume, expanded enough that off-camera casters can still cast into the visible cascade.

The culling API should be plane-set based, not camera-specific. Camera culling is the first user of the framework because
it turns the camera frustum planes into an opaque draw list. Shadow culling will use different planes: it starts from the
camera-visible receiver region for each cascade, then extends or bounds that region along the reverse sun-light direction
to find objects that can cast shadows into what the player can see.

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

Explicit distances satisfy the requested "transition at certain distances" behavior. A `practical_split_distances`
helper should also exist for tests and tuning:

    uniform_i = near_z + (shadow_far - near_z) * i / cascade_count
    logarithmic_i = near_z * pow(shadow_far / near_z, i / cascade_count)
    split_i = mix(uniform_i, logarithmic_i, split_lambda)

Clamp the final shadow distance to `camera.far_z` until OFG supports receiving shadows beyond the camera far plane.
When the current camera has `far_z = 80.0`, the default `[12, 32, 80]` maps cleanly to the existing render distance.

Each cascade uses `[near_z, end0]`, `[end0, end1]`, and `[end1, end2]`. The shader selects by camera view-space depth:

    cascade_index = 0 when depth <= end0
    cascade_index = 1 when depth <= end1
    cascade_index = 2 otherwise, while depth <= end2

Pixels beyond `max_shadow_distance` receive full direct light visibility.

### Cascade Matrices

For each cascade:

1. Compute the eight world-space corners of the camera frustum slice from `CameraProperties`.
2. Compute a stable cascade center and radius. Start with a bounding sphere around the frustum slice corners.
3. Build a sun view matrix using the sun light travel direction. The light "looks" along the direction light travels.
4. Snap the light-space cascade center to shadow texel increments:

       world_units_per_texel = (2 * radius) / shadow_map_size
       snapped_center_xy = floor(center_xy / world_units_per_texel) * world_units_per_texel

5. Build a left-handed orthographic projection covering `[-radius, radius]` in X/Y and a configurable caster depth
   range in Z.
6. Store `shadow_clip_from_world[cascade]` plus texel size and bias values in a shadow uniform buffer.

The first version may use a fixed caster depth margin around the cascade slice, for example 160 world units, because
OFG does not yet have broad-phase render culling or terrain chunks. Later terrain/entity culling should tighten this
range per cascade for precision and speed.

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
- constant and slope-scaled depth bias configured in the pipeline;
- draw all opaque draw-list submeshes into each cascade layer.

Render three passes per frame at first: one pass per cascade layer. This is simple and explicit. A later optimization can
use render bundles, multiview, or instanced/layered rendering if WebGPU support and OFG abstractions make that worthwhile.

### Opaque Shader Sampling

Extend the opaque PBR frame/shadow bind groups with:

- `ShadowFrameUniforms` containing three `shadow_clip_from_world` matrices, cascade end depths, blend widths, texel
  sizes, normal/depth bias controls, shadow intensity, and enabled flags;
- `texture_depth_2d_array` shadow map;
- `sampler_comparison` shadow sampler.

The fragment shader computes shadow visibility only for the current sun direct light:

    shadow_visibility = sample_csm_shadow(world_position, normal, camera_view_depth)
    direct_shadow_multiplier = 1.0 - shadow_intensity * (1.0 - shadow_visibility)
    direct = direct_lighting * direct_shadow_multiplier
    color = ambient + direct

`shadow_intensity = 0.0` disables darkening. `shadow_intensity = 1.0` fully removes direct sun where the shadow map says
occluded, while ambient/sky light remains.

The first PCF implementation should average a small fixed sample pattern. Start with nine taps arranged as a 3x3 kernel
in shadow-map texel units. Use a per-cascade texel radius so near shadows can be crisp and far shadows can be slightly
softer:

    near cascade radius: 1.0 texels
    mid cascade radius: 1.5 texels
    far cascade radius: 2.0 texels

If this is too expensive in browser smoke, drop to a five-tap cross pattern and record the tradeoff.

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

After `Environment` and `LightProperties` land, `Renderer` should build shadow cascades from the first current sun
directional light. If no live current sun exists, shadow rendering is skipped and opaque shading uses direct visibility
`1.0`.

The shadow subsystem should not mutate `Environment`, light entity transforms, time of day, or weather. It consumes the
resolved sun state exactly like `SkyPass` and `OpaquePass`.

Sun altitude can influence default settings later:

- sun below horizon: disable sun shadows;
- low sun: increase bias slightly and optionally lower intensity to avoid long noisy sunset shadows;
- storm or clouds: lower shadow intensity and direct light intensity together through `Environment`.

### Ray Tracing Answer

WebGL is not capable of using hardware ray tracing pipelines. WebGL 1/2 expose OpenGL ES style rasterization APIs, not
DXR/Vulkan RT style acceleration structures, ray-generation shaders, hit/miss shaders, shader tables, or trace commands.

Browser WebGPU also does not currently expose standard hardware ray tracing pipelines. The GPUWeb ray tracing extension
issue is still open. WebGPU compute can run a custom software ray tracer or shadow ray query over an engine-owned BVH,
but that would be a large custom renderer subsystem and not the same thing as using hardware ray tracing pipelines.

For OFG's browser target, CSM is the right first implementation. Keep the public shadow abstraction narrow enough that a
future ray-query backend could provide the same "sun visibility" value if browser standards eventually expose it.

## Plan of Work

Milestone 0 adds a large deterministic validation scene before culling or shadows are implemented.

Create a scene preset or builder that can be used by browser smoke, native smoke, and focused tests without relying on
wall-clock randomness. The scene should keep the existing player/camera controls usable, but it should add enough
geometry to inspect culling and cascades:

- a ground plane large enough to cover the full camera far distance;
- at least 150 cubes or box-like mesh instances, reusing existing mesh/material resources rather than creating unique GPU
  resources per cube;
- varied dimensions, including low slabs, tall pillars, small crates, and wide blocks;
- varied transforms across near, mid, and far ranges relative to the default camera;
- deliberate intersections: boxes partially below the ground, boxes crossing each other, and clustered stacks;
- objects outside the starting camera frustum but near enough to become visible after a small camera turn;
- objects outside the camera frustum that should still become shadow casters once CSM lands;
- deterministic material/color categories so smoke screenshots can classify that the large scene loaded.

Prefer adding this as a named deterministic validation preset rather than silently replacing every demo scene. For
example, `DemoScene` can expose a mode such as `DemoSceneMode::FactoryShadowCullingValidation`, or a new small builder
can create the large scene while sharing the existing resource helpers. Browser/native smoke should be able to request
this preset explicitly. The default app may use it only if performance is acceptable before culling exists.

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
frustum-corner reconstruction, sun-view matrix creation, texel snapping, matrix finite validation, disabled/no-sun
behavior, and default settings.

Milestone 3 adds GPU shadow target ownership and the shadow caster pass.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\shadow_map_target.hpp`
- `C:\dev\ofg\cpp\src\render\shadow_map_target.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\shadow_caster_pass.hpp`
- `C:\dev\ofg\cpp\src\render\shadow_caster_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\shadow_caster.wgsl.hpp`

`Renderer` owns one `ShadowMapTarget` and one `ShadowCasterPass`. Renderer counters include shadow texture/view,
sampler, layout, bind group, buffer, shader module, and pipeline creation counts. Tests verify resize/reuse/release
behavior and that steady-state frames do not recreate durable resources. Shadow rendering must filter the extracted
render objects per cascade rather than using the already camera-culled opaque draw list, because off-camera casters can
still cast shadows into visible pixels.

Milestone 4 wires shadow sampling into opaque PBR.

Update:

- `C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp`
- `C:\dev\ofg\cpp\src\render\opaque_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\opaque_uber.wgsl.hpp`
- `C:\dev\ofg\cpp\src\render\renderer.cpp`

Add a shadow bind group or extend the frame bind group in the smallest clean way. The shader applies shadow visibility
only to direct sun lighting. Tests cover uniform packing, disabled shadows, cascade selection boundaries, blend-band
math through CPU helper functions, shadow intensity math, and resource lifetime counters.

Milestone 5 integrates with the sky/environment sun contract and visual validation.

When `Environment` and `LightProperties` exist, shadows consume the resolved current sun. Update docs and smoke contracts.
Add screenshots after first visible hard shadows, after PCF softness, after cascade blend debugging, and before
finalization. Store durable screenshots under `C:\dev\ofg\artifacts\cascaded-shadows\`.

Add a debug mode if useful for smoke and human review:

- cascade color overlay;
- shadow map layer preview in native smoke artifacts or browser debug screenshot;
- counters for cascade count, map size, and shadow pass draw counts.

## Concrete Steps

Run from `C:\dev\ofg`.

1. Implement the large deterministic validation scene.

    npm run test:cpp
    npm run build
    npm run smoke:render

Expected: scene/demo tests prove the preset creates the expected number and spread of cubes without creating unique mesh
or material resources per instance; build succeeds; native smoke can render the large preset and writes a PNG/report.

2. Implement render bounds and camera frustum culling.

    npm run test:cpp

Expected: doctests pass for mesh local bounds, world bounds, frustum plane extraction, sphere/AABB culling, extracted
render object counts, invisible renderer exclusion, and camera-culled opaque draw-list counts.

3. Implement CPU shadow settings and cascade math.

    npm run test:cpp

Expected: doctests pass for shadow settings, split distances, orthographic projection, cascade matrices, and texel
snapping.

4. Implement shadow map target and shadow caster pass.

    npm run test:cpp

Expected: doctests pass for shadow target resize/release/reuse, per-cascade caster culling, and renderer counters. No
browser visual change is required yet if opaque sampling is still disabled.

5. Integrate opaque shadow sampling.

    npm run test:cpp
    npm run build

Expected: C++ tests pass and the browser/WASM build succeeds with the new WGSL shaders.

6. Run browser and native smoke with screenshots.

    npm run smoke:browser
    npm run smoke:render

Expected: smoke passes; screenshots or PNG artifacts show sun-cast shadows on the ground/player/cubes with no obvious
cascade seam in the default camera view.

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

## Validation and Acceptance

Functional acceptance:

- A deterministic large validation scene exists and can be selected by smoke/tests.
- The validation scene contains enough reused cube/box instances across near, mid, and far distances to visibly exercise
  camera culling and all three shadow cascades.
- The validation scene intentionally includes some overlapping/intersecting cubes and some cubes partly intersecting the
  ground.
- The validation scene includes off-camera objects that are useful for testing camera culling and later shadow-caster
  culling.
- Mesh resources expose local render bounds derived from their CPU vertices.
- Renderer extraction produces bounded render objects from authored-visible `MeshRenderer` components before pass
  filtering.
- Opaque rendering uses camera frustum culling and reports extracted/visible/culled counts.
- Shadow rendering does not reuse the camera-culled opaque draw list; each cascade performs its own caster culling from
  extracted render objects.
- Renderer owns a durable shadow-map target with exactly three cascade layers by default.
- The current sun directional light is the only light that casts shadows in this plan.
- Cascades transition at configurable camera view-space distances.
- Cascade blending hides visible seams at cascade boundaries.
- PCF softens shadow edges independently of cascade blending.
- Shadow intensity controls direct-light darkening without changing ambient lighting.
- Camera movement does not cause obvious cascade swimming in ordinary play movement.
- No shadows are rendered when shadows are disabled, when no current sun exists, or when the sun is below the horizon.
- Renderer counters prove steady-state frames update uniforms and redraw depth, but do not recreate durable shadow
  textures, views, samplers, bind group layouts, shader modules, pipelines, or size-independent bind groups.
- Browser and native smoke both show shadowed geometry from the same sun direction.

Test acceptance:

- `npm run test:cpp` passes after each C++ milestone.
- `npm run test:ts` passes if TypeScript smoke/debug UI changes.
- `npm test` passes before completion.
- `npm run smoke:browser` passes and stores or reports a screenshot with visible sun shadows.
- `npm run smoke:render` passes and writes a PNG/report with visible sun shadows.
- `npm run coverage` passes, with changed implementation files above the documented threshold or explicit exceptions
  recorded here.

Screenshot acceptance:

- Take and present screenshots after the large validation scene lands, after camera culling lands, after hard shadows
  first appear, after PCF softness lands, after cascade blending lands, and before finalization.
- Store durable screenshots under `C:\dev\ofg\artifacts\cascaded-shadows\` or the smoke artifact directory.

## Idempotence and Recovery

Shadow resources must be additive to the renderer lifecycle. `Renderer::release` must release `ShadowCasterPass` and
`ShadowMapTarget` even if preparation failed after only one was created. Repeated `Renderer::prepare` after ready must
not create duplicate shadow resources. Repeated resize with the same shadow settings must not recreate shadow textures.
Zero-size platform resize should leave shadow resources valid or released according to the renderer target policy, but
must not crash.

If shadow sampling breaks opaque rendering, keep the shadow caster pass disabled and force shader visibility to `1.0`
while retaining the CPU-side cascade tests. If shadow caster rendering fails but opaque rendering works, skip the shadow
pass and bind a disabled shadow uniform state. Do not mark the milestone complete until the intended shadowed path works
or this plan is explicitly revised.

If depth comparison sampling with `Depth32Float` exposes a browser/native portability issue, switch the shadow map format
after recording the failing backend and evidence here. Preserve the public `ShadowMapTarget::format()` API so the change
does not leak across the renderer.

## Artifacts and Notes

No screenshots or local artifacts yet.

Research conclusion on ray tracing:

WebGL cannot use hardware ray tracing pipelines. Browser WebGPU cannot use standard hardware ray tracing pipelines today
either. Software ray tracing through fragment or compute shaders is possible in principle, but it would require custom
acceleration structures and would not be a practical first sun-shadow implementation for OFG.

## Interfaces and Dependencies

Expected new or changed interfaces by the end:

- `C:\dev\ofg\cpp\include\ofg\render\demo_scene.hpp` or a new focused demo/validation-scene header
  - deterministic large culling/shadow validation preset selection
  - reusable scene builder that creates many box instances from shared mesh/material resources
  - test-visible counts for total validation boxes and broad near/mid/far distribution

- `C:\dev\ofg\cpp\src\native\render_smoke.cpp` and browser smoke tooling as needed
  - ability to render the large validation preset for culling/shadow screenshots
  - report fields identifying the selected scene preset and expected large-scene object counts

- `C:\dev\ofg\cpp\include\ofg\render\bounds.hpp`
  - `struct Bounds3`
  - `struct BoundingSphere`
  - finite/empty validation helpers
  - local-to-world transform helpers for conservative bounds
  - mesh-vertex bounds helpers

- `C:\dev\ofg\cpp\include\ofg\render\frustum.hpp`
  - `struct FrustumPlane`
  - `class ViewFrustum` or equivalent value type
  - `ViewFrustum view_frustum_from_camera(const CameraProperties& camera)`
  - sphere/AABB intersection helpers

- `C:\dev\ofg\cpp\include\ofg\render\render_object.hpp`
  - `struct RenderObject`
  - non-owning mesh/material/property data copied from `MeshRenderer`
  - model matrix, sort origin, local bounds, world bounds, and stable scene extraction index
  - helpers to append camera-visible objects to a `DrawList`

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
  - cascade count fixed to `3` for the first implementation
  - cascade end distances, blend widths, map size, PCF radius, depth bias, slope bias, normal bias, intensity, enabled
    flag, and validation helpers
  - `std::array<float, 3> practical_split_distances(float near_z, float far_z, float lambda)`

- `C:\dev\ofg\cpp\include\ofg\render\shadow_cascade.hpp`
  - `struct ShadowCascade`
  - `struct ShadowCascadeSet`
  - `ShadowCascadeSet build_shadow_cascades(const CameraProperties& camera, math::Vec3 light_direction, const ShadowSettings& settings)`

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

- `C:\dev\ofg\cpp\include\ofg\render\opaque_pass.hpp`
  - pass receives shadow resources or a compact `ShadowFrameState`
  - opaque shader applies shadow visibility to direct lighting

- `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp`
  - renderer owns `ShadowSettings`, `ShadowMapTarget`, and `ShadowCasterPass`
  - renderer builds shadow cascades from resolved current sun state before opaque rendering
  - counters include shadow resources

- `C:\dev\ofg\cpp\CMakeLists.txt`
  - add new implementation and doctest files

The implementation must not add a third-party engine or runtime dependency.
