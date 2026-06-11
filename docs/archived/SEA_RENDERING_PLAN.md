# Sea-Level Water Rendering

Archived note, 2026-06-11 / Codex: This ExecPlan was completed. The active
source of truth for sea rendering is now the Rust implementation, shader
artifacts, smoke reports, `docs/API_CONTRACTS.md`, and `docs/ARCHITECTURE.md`.

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

The goal is to add a good-looking sea at a fixed sea level to the playable
browser runtime. The first complete result should make the current generated
terrain read as islands, coastlines, shallow shelves, and deep water without
requiring a hydrology simulation. The water should be visible in normal browser
play, in Rust offscreen smoke captures, and through debug views that expose the
depth quantities that drive the effect.

Success means a player can stand near or above the coast and see a stable
horizontal sea surface at `y = 0.0` meters. Shallow water should reveal the
bottom with a lighter tint. Deeper water should become darker and bluer. Distant
underwater terrain should become denser and murkier because the eye ray travels
through more water, even when the vertical depth is similar. At grazing angles
the water should reflect the sky and above-water terrain through a planar
reflection pass. The final image must still flow through the existing Rust/wgpu
post-process path for bloom, tone mapping, and depth of field.

This plan deliberately does not implement rivers, lakes, flow, erosion,
wetness propagation, or terrain-carving hydrology. Those are future terrain and
world-generation systems.

## Progress

- [x] (2026-06-10 22:05+01:00) Read `PLANS.md` before drafting this ExecPlan.
- [x] (2026-06-10 22:05+01:00) Reviewed current renderer, shader, terrain, and
  architecture contracts relevant to sea rendering.
- [x] (2026-06-10 22:05+01:00) Created this initial ExecPlan with an explicit
  water-depth model and terrain-generation boundary.
- [x] (2026-06-10 22:20+01:00) Corrected the water-depth model so vertical
  bottom depth comes from terrain-derived bathymetry at `water_world.xz`, while
  optical path length comes from the opaque scene linear-depth texture.
- [x] (2026-06-10 22:35+01:00) Completed an authoring audit against `PLANS.md`,
  clarified the bathymetry texture source, and added the no-water guard for
  dry land at or above sea level.
- [x] (2026-06-11 00:33+01:00) Milestone 1: added Rust sea-level/water settings,
  `terrain_core::sea_depth_at_for_variant`, browser command/status typing, and
  debug snapshot water status without adding TypeScript water ownership.
- [x] (2026-06-11 00:33+01:00) Milestone 2: added Rust-owned water resources,
  opaque color/depth targets, bathymetry texture upload/reuse, generated
  `water.wgsl` artifact, and native resource tests.
- [x] (2026-06-11 00:33+01:00) Milestone 3: implemented visible sea compositing
  with terrain-derived vertical bathymetry, screen-space optical path length,
  procedural waves, Fresnel/specular, absorption, open-water fallback, and
  bottom-depth/path-length/Fresnel debug views.
- [x] (2026-06-11 00:33+01:00) Milestone 4: added half-resolution planar
  reflection targets, mirrored camera frame rendering, reflection debug/status
  fields, and enabled/disable commands.
- [x] (2026-06-11 00:33+01:00) Milestone 5: added browser render-debug water
  controls, browser and Rust smoke coverage, API/architecture documentation,
  coverage-focused water renderer tests, and final validation evidence.

## Surprises & Discoveries

- Observation: The current renderer already writes a scene-linear HDR color
  target and an `R32Float` linear-depth target before fullscreen post-process.
  Evidence: `crates/engine_web/src/post_process.rs` owns `scene_color` and
  `linear_depth`; `src/engine/render/shaders/uber.wgsl` writes
  `SceneFragmentOutput.linearDepth`.
- Observation: The current sky pass writes scene color but stores linear depth
  as `0.0`.
  Evidence: `skyFragmentMain` in `src/engine/render/shaders/uber.wgsl` assigns
  `output.linearDepth = 0.0`. The water shader must treat non-positive opaque
  depth as "no rendered bottom" rather than as an opaque object at the camera.
- Observation: The repository already has sea-level-adjacent terrain material
  logic, but it is heuristic and not a water body.
  Evidence: `crates/terrain_core/src/material.rs` uses altitude near zero for
  coast/wetland material classification. It does not generate water surfaces.
- Observation: Browser WebGPU rejected the first reflection sample because
  `textureSample` uses implicit derivatives and must be called from uniform
  control flow.
  Evidence: The first `npm run smoke:browser` attempt failed in Chrome while
  creating the `water composite pipeline`, pointing at
  `reflectionColorTexture` sampling inside non-uniform water-pixel control
  flow. Changing that sample to `textureSampleLevel(..., 0.0)` fixed the
  browser smoke failure.
- Observation: A 256x256 CPU-filled bathymetry grid was more work than needed
  for the first smokeable sea pass. A 128x128 grid over the same 4096 meter
  span provides stable water status and practical smoke runtime.
  Evidence: The implemented default is
  `DEFAULT_WATER_BATHYMETRY_GRID_SIZE = 128`; browser smoke and Rust smoke both
  verify `waterBathymetryGridSize` / `bathymetryGridSize` as 128.
- Observation: The initial water renderer coverage was just below the default
  attention threshold.
  Evidence: `npm run coverage:rust` reported
  `crates/engine_web/src/water_renderer.rs` at 89.4% line coverage. Extending
  the native water resource test to cover resize, reflection accessors,
  bathymetry reuse, invalid sea level, and uniform packing moved the filtered
  report to `none`.

## Decision Log

- Decision: The first sea is a Rust/wgpu renderer feature over a fixed
  horizontal sea plane, not a generated terrain mesh or TypeScript-owned water
  system.
  Rationale: Runtime terrain generation, renderer resources, and draw
  submission are Rust-owned by contract. A sea-level plane can be rendered from
  existing camera, terrain, sky, and depth data without adding a hydrology layer
  or reintroducing TypeScript ownership.
  Date/Author: 2026-06-10 / Codex.

- Decision: Water visibility and optical path length will be calculated in the
  water shader from the opaque scene linear-depth texture, not stored per
  terrain vertex or generated as part of terrain chunks.
  Rationale: The screen-space depth texture reflects the terrain and model
  geometry actually rendered this frame, including LOD selection and occlusion,
  so it is the right source for "does this pixel see water" and "how far does
  the eye ray travel through water before the opaque hit." It is not the right
  source for vertical depth directly below the surface point.
  Date/Author: 2026-06-10 / Codex.

- Decision: The previous draft's use of `opaque_world.y` for bottom depth is
  insufficient for true vertical water depth. The renderer needs a bathymetry
  texture or equivalent sampled at the sea-surface hit point's world `x,z`.
  Rationale: The opaque depth buffer gives the terrain hit along the camera ray,
  not the terrain directly below the water-surface point. At oblique view
  angles those are different points. The depth buffer remains correct for
  occlusion and optical path length, but vertical depth must come from a
  terrain-derived depth query at `water_world.xz`.
  Date/Author: 2026-06-10 / Codex.

- Decision: `terrain_core` may expose a small pure helper for CPU-side sea
  depth probes, but it must not become a full hydrology or water-surface
  generator in this plan.
  Rationale: Debug probes, tests, and future gameplay may need
  `max(sea_level - height_at_for_variant(...), 0.0)`. That helper is different
  from generating water geometry, deciding water visibility, or carving rivers.
  Date/Author: 2026-06-10 / Codex.

- Decision: Planar reflection is a second visual milestone after the depth and
  absorption composite pass.
  Rationale: Depth/path-length absorption is the foundation of the requested
  visual behavior. Reflection adds beauty but also adds a second scene render,
  mirrored-camera culling concerns, and more performance risk.
  Date/Author: 2026-06-10 / Codex.

- Decision: The first bathymetry texture uses `R32Float` and a 128x128
  camera-centered grid over 4096 meters.
  Rationale: `R32Float` keeps CPU upload and shader sampling simple while the
  water model is still changing. The 128x128 grid is enough for the first
  visible sea and keeps smoke/runtime cost manageable; future tuning can change
  resolution once visual artifacts justify it.
  Date/Author: 2026-06-11 / Codex.

- Decision: Reflection texture sampling in `water.wgsl` uses explicit LOD.
  Rationale: Water pixels branch per fragment based on ray-plane intersection,
  terrain depth, and bathymetry. Explicit LOD avoids implicit-derivative
  requirements in non-uniform control flow and is accepted by browser WebGPU.
  Date/Author: 2026-06-11 / Codex.

## Outcomes & Retrospective

Implementation is complete. The playable browser renderer now has a Rust-owned
sea-level water pass at `y = 0.0` with terrain-derived bathymetry, optical
path-length absorption, procedural waves, Fresnel/specular response, planar
reflection, water debug views, browser debug controls, Rust/browser smoke
coverage, generated shader artifacts, and updated API/architecture contracts.

Remaining risks are visual tuning rather than missing architecture: the first
water style is procedural and intentionally simple, reflection is half
resolution, and bathymetry currently uses the heightfield surface. If future
terrain gains caves, overhangs, or multiple floors below sea level, the
bathymetry source should be upgraded to a downward terrain query or
orthographic bathymetry pass as described in this plan.

## Contract and Quality Baseline

This plan preserves the active runtime ownership contracts in
`docs/API_CONTRACTS.md`:

- `OFG-API-001`: Browser shell calls continue through `RustBrowserGame.create`,
  `resize`, `tick`, `command`, and `debugSnapshot`. New water controls must be
  commands and debug snapshot fields, not new public wasm methods.
- `OFG-API-003`: Debug hooks may expose Rust-assembled water status and select
  debug views. TypeScript must not compute water depth, water visibility,
  reflection cameras, renderer targets, or water draw behavior.
- `OFG-API-004`: Scene fragment output currently has scene color and linear
  depth. If the water pass changes target ownership or adds shader artifacts,
  `uber.wgsl`, the new `water.wgsl`, generated shader artifacts, Rust pipeline
  descriptors, and shader tests must change together.
- `OFG-API-005`: Terrain variants remain geometry-shape descriptors. This plan
  may read the active terrain variant for CPU probes, but it must not turn
  terrain variants into biomes, climate, or hydrology.
- `OFG-API-009`: TypeScript must not regain terrain generation, terrain
  scheduling, water generation, WebGPU resources, or draw submission ownership.

Quality constraints:

- Keep new renderer code out of oversized files where practical.
  `crates/engine_web/src/wgpu_renderer.rs` is already over the preferred size,
  so water-specific resource and settings code should live in focused modules
  such as `crates/engine_web/src/water.rs` or `water_renderer.rs`.
- All new Rust functions need behavior-focused tests near the implementation.
- Shader changes must update generated artifacts through `npm run
  build:shaders` and validate with `npm run check:shaders`.
- Every implementation milestone must run the repo-local `milestone-review`
  skill before being marked complete.
- The final implementation must satisfy the Rust coverage attention gate for
  modified implementation files through `npm run coverage:rust`, or record an
  explicit exception with rationale.

## Water Depth Model

This section is the source of truth for the depth calculation.

Definitions:

- Sea level is a horizontal plane at `sea_level_meters`, initially `0.0`.
- The camera ray is the normalized world-space ray through a screen pixel,
  reconstructed from the camera eye and inverse view-projection matrix.
- The water surface hit is where that ray intersects the sea-level plane.
- The opaque hit is the first rendered opaque scene point behind the water,
  reconstructed from the opaque linear-depth texture when that texture contains
  a positive value.
- The bathymetry texture is a renderer-owned 2D texture over a world-space XZ
  region around the camera. Each texel stores vertical sea depth:
  `max(sea_level_meters - terrain_floor_y, 0.0)`.

Per pixel, the water shader computes:

1. Reconstruct `ray_dir` for the current pixel.
2. Intersect the ray with the sea plane:

       water_t = (sea_level_meters - eye_world.y) / ray_dir.y
       water_world = eye_world + ray_dir * water_t
       water_distance = length(water_world - eye_world)

   If `ray_dir.y` is nearly zero or `water_t <= 0.0`, the sea plane is not in
   front of the camera for that pixel and the shader should leave the opaque
   scene unchanged.

3. Sample vertical water depth at the water-surface point:

       bottom_depth = sample_bathymetry_depth(water_world.xz)

   This is the distance from the sea surface straight down to terrain directly
   below that surface point. It is not inferred from the current pixel's opaque
   depth hit. For current terrain, Rust can populate the bathymetry texture by
   sampling `height_at_for_variant(seed, variant, x, z)` on a regular XZ grid:

       bottom_depth = max(sea_level_meters - terrain_height(x, z), 0.0)

   This is terrain-derived render data, not water generation. It does not add
   rivers, lakes, erosion, flow, water-body IDs, or chunk-owned water meshes.
   If terrain later supports caves, overhangs, or multiple vertical surfaces,
   this source should be upgraded to a `terrain_core` downward raycast or
   "first floor below sea level" query rather than a heightfield query.

4. If `bottom_depth` is at or below a small shoreline epsilon, the sea-level
   point is over terrain at or above sea level. The shader should leave the
   opaque scene unchanged for that pixel, except for a deliberately tuned
   wet-edge or foam effect added in the shoreline polish milestone. This avoids
   drawing a water sheet across dry land simply because the camera ray crosses
   the sea plane.

5. Read `opaque_distance` from the opaque linear-depth texture.

   A positive `opaque_distance` means a terrain/model fragment was rendered at
   that pixel. A non-positive value means the pixel is sky or background.

6. If `opaque_distance > 0.0` and `opaque_distance <= water_distance`, opaque
   geometry is in front of the water surface, so the shader leaves the opaque
   scene unchanged.

7. If `opaque_distance > water_distance`, the camera ray travels through water
   before hitting opaque geometry. Compute optical path length:

       water_path_length = max(opaque_distance - water_distance, 0.0)

   The shader may reconstruct the opaque hit point for future diagnostics or
   caustic-like effects:

       opaque_world = eye_world + ray_dir * opaque_distance

   But `opaque_world.y` is not the true vertical water depth unless the view ray
   is vertical. It is the y coordinate of the ray-hit point, not necessarily the
   terrain directly below `water_world.xz`.

   `bottom_depth` from the bathymetry texture drives shoreline style: clear
   shallows, sand visibility, foam, and the deep-water color transition.

   `water_path_length` drives optical density: Beer-Lambert absorption and
   murk increase with the distance the eye ray travels through water. This is
   the requested "water density appearing to change with the distance light
   travels through it from your eye" behavior.

8. If `opaque_distance <= 0.0`, there is no rendered bottom behind the water
   pixel. Treat it as open water or horizon water:

       water_path_length = configured_open_water_path_length(water_distance)

   Keep `bottom_depth` from bathymetry when `water_world.xz` is inside the
   bathymetry coverage. If the surface point is outside coverage, use
   `configured_deep_water_depth` as a stable fallback.

   The exact open-water function should be tuned in Milestone 3, but it must be
   capped so horizon pixels remain stable and do not overflow the exponential
   absorption math.

This visual depth is not part of terrain generation in Milestones 1 through 5.
The existing terrain surface is the sea floor when it is below sea level. The
renderer gets the sea-floor y position for vertical water depth from a
terrain-derived bathymetry texture sampled at the water surface point's world
`x,z`. The opaque linear-depth texture is still required, but for a different
job: it decides whether water is visible for the current pixel and how much
water the eye ray travels through before hitting opaque geometry.

Terrain generation may later grow hydrology, bathymetry, erosion, lakes, rivers,
or water-body descriptors, but those systems are not required to calculate the
first sea rendering. The first bathymetry source is a render auxiliary generated
from existing terrain height sampling:

    sea_depth_at(seed, variant, x, z, sea_level) =
        max(sea_level - height_at_for_variant(seed, variant, x, z), 0.0)

That helper is suitable for probes, tests, future gameplay, rough debug
readouts, and filling the renderer bathymetry texture. It should not be confused
with optical path length or visibility; those still come from the opaque
linear-depth texture because that is what the camera actually sees.

## Bathymetry Texture Source

The first implementation should build the bathymetry texture in Rust inside
`engine_web` as a renderer auxiliary resource. It is not loaded from disk, not
authored by TypeScript, and not emitted by terrain mesh generation.

For each water frame, the renderer has a camera-centered bathymetry grid:

    WaterBathymetryGrid {
        center_x,
        center_z,
        world_span_meters,
        texel_count,
        depths_meters,
    }

The grid covers the water area that the shader can sample around the camera,
for example a square span matching or slightly exceeding the current visible
terrain span. Each texel represents one world-space XZ sample point:

    u, v -> x, z
    terrain_y = terrain_core::height_at_for_variant(seed, variant, x, z)
    depth = max(sea_level_meters - terrain_y, 0.0)

Rust uploads the resulting depth values to a single-channel GPU texture such as
`R16Float` or `R32Float`. The water shader receives the grid origin/span as
uniforms, maps `water_world.xz` into bathymetry UV coordinates, and samples the
texture to get vertical bottom depth.

The grid should be retained and updated only when needed:

- The player/camera moves far enough that the previous grid no longer covers
  the useful water sampling area.
- The terrain seed, preset, or variant revision changes.
- The configured sea level changes.
- The bathymetry resolution or world span changes.

This CPU-filled grid is acceptable for the first sea because current terrain
height is already a deterministic Rust function and player grounding uses the
same heightfield-style query. It also avoids adding a second terrain render pass
before the visual model is proven.

If future terrain supports overhangs, caves, arches, or non-heightfield sea
floors, this source should be replaced or augmented. The likely upgrade is a
top-down orthographic bathymetry pass or a `terrain_core` downward raycast that
finds the first solid floor below sea level for each XZ sample. That is future
work; the initial sea only needs the current terrain height surface as its sea
floor.

## Context and Orientation

Current renderer shape:

- `crates/engine_web/src/wgpu_renderer.rs` owns the browser WebGPU renderer.
  It builds render packets from Rust game state, uploads terrain/model object
  uniforms, renders shadow cascades, renders the main scene, draws sky, and
  then runs post-process.
- `crates/engine_web/src/post_process.rs` owns resize-dependent HDR scene color
  and linear-depth targets plus bloom and final present passes.
- `src/engine/render/shaders/uber.wgsl` contains terrain, model, shadow, and
  sky shader entry points. The main scene fragment output writes color at
  location 0 and linear distance from camera to fragment at location 1.
- `src/engine/render/shaders/post.wgsl` reads the scene color and linear-depth
  textures for debug views, bloom, tone mapping, and depth of field.
- `crates/engine_web/src/render_uniforms.rs` packs the camera uniform consumed
  by WGSL. It already includes `inverseViewProjection`, `eyeWorld`, sun
  direction/intensity, sky, and cloud data.
- `crates/terrain_core/src/field.rs` owns `height_at_for_variant`, which can
  provide the first CPU-side sea-depth query used to fill the renderer
  bathymetry texture.

The current frame order is:

1. Shadow map passes.
2. Opaque scene pass into HDR scene color and linear depth.
3. Sky draw after opaque scene items.
4. Bloom extraction from scene color.
5. Final post-process present to the WebGPU surface.

The water implementation should become:

1. Shadow map passes.
2. Update or retain a renderer bathymetry texture for the camera-centered
   world-space XZ region. This texture stores vertical sea depth under the water
   surface and changes when the stream center, sea level, terrain seed, or
   terrain variant changes.
3. Opaque scene pass into opaque HDR color and opaque linear depth.
4. Sky draw into the opaque color target.
5. Optional reflection pass into a reflection color target.
6. Water composite pass, reading opaque color/depth and bathymetry, then writing final scene
   color/depth.
7. Bloom extraction from final scene color.
8. Final post-process present to the WebGPU surface.

The water pass should be fullscreen rather than a large world mesh for the first
implementation. A fullscreen pass avoids needing a giant sea mesh, naturally
uses the current camera and depth buffer, and keeps water visible across the
whole screen. The shader decides per pixel whether the sea-level plane is in
front of the camera and behind opaque geometry.

## Plan of Work

Milestone 1 establishes data contracts without visible water. Add a sea-level
constant and a small water settings type in Rust. A good home is a new
`crates/engine_web/src/water.rs` module for renderer-facing settings and
`crates/terrain_core/src/water.rs` for the pure sea-depth helper. The helper
should calculate vertical sea depth at a world XZ point from the active terrain
variant. Add command parsing for water debug/settings through
`RustBrowserGame.command`. Add debug snapshot fields under renderer status or a
dedicated water status: water runtime string, enabled flag, reflection enabled
flag, sea level, bathymetry coverage, absorption coefficients, shallow/deep
colors, and current debug view. Update `src/engine/web/browserGameTypes.ts`
only as a typed debug/command shell. Add unit tests for settings validation,
browser command names, debug status serialization, and the CPU `sea_depth_at`
helper.

Milestone 2 adds the renderer bathymetry resource and frame-graph plumbing with
no visual dependency on water being enabled. Introduce a camera-centered
bathymetry texture, probably `R16Float` or `R32Float`, plus metadata that maps
world `x,z` to texture `u,v`. Fill it in Rust from `terrain_core` height/depth
queries at a fixed grid spacing and update it only when the camera moves far
enough, the active seed/variant changes, or sea level changes. Introduce water
composite resources in a focused Rust module: target textures or ping-pong
views, bind-group layouts, sampler, uniform buffer, bathymetry texture, and a
render pipeline using a new `src/engine/render/shaders/water.wgsl`. Do not
sample a texture while it is bound as a render target. The safe layout is to
render opaque scene into one pair of color/depth targets, then water into the
final scene color/depth pair that post-process already reads. If water is
disabled, perform a copy or no-op pass that preserves the previous final scene
behavior. Update shader generation so `water.wgsl` has a generated artifact and
contract tests. Add native tests for bathymetry fill/reuse and a GPU test that
creates the resources and renders the disabled/no-op path to an offscreen
target.

Milestone 3 implements visible water without planar reflection. In
`water.wgsl`, reconstruct camera rays from the inverse view-projection matrix,
intersect them with sea level, sample bathymetry at `water_world.xz`, sample
opaque scene color and linear depth, and apply the depth model in this plan. Add
procedural wave normals from a few low-cost moving sine/noise bands in world
space. Add Fresnel, base water tint, bottom-depth color transition,
optical path-length absorption, mild surface specular from the Rust sky sun
direction, and open-water fallback for sky pixels. Add debug views for at least
`bottomDepth`, `pathLength`, and `fresnel`. Native Rust smoke should include
shallow coast and deep-water captures with pixel-diversity checks and report
fields for water settings and bathymetry coverage.

Milestone 4 adds planar reflection. Add a half-resolution reflection color
target and render the scene from a camera mirrored across the sea plane. The
reflection pass should initially include sky and above-water terrain/models
that can contribute to water reflection. Clip or discard below-water reflected
geometry so the reflection does not show underwater terrain mirrored above the
surface. If mirrored view matrices invert winding, handle that explicitly with
front-face/cull-mode configuration or a dedicated reflection pipeline. Sample
the reflection texture in `water.wgsl`, distort the UV by wave normal, and blend
it by Fresnel. Add controls to disable reflection and expose reflection target
size/status in debug output. Add performance counters or GPU timing for the
reflection pass if timestamp support is available.

Milestone 5 polishes shoreline behavior and finishes validation. Add
depth/noise-based foam in very shallow water, a wet-edge/darkening term near
coasts, stronger sun glitter at grazing angles, and distance haze so large open
water does not read as a flat sheet. Add a browser render-debug panel control
only if it stays a thin command wrapper. Update `docs/API_CONTRACTS.md` and
`docs/ARCHITECTURE.md` with the new water renderer ownership. Extend
`tools/browser-smoke.mjs` only with black-box integration checks for water
runtime/debug fields and nonblank frames. Keep visual acceptance in Rust
offscreen smoke.

## Concrete Steps

All commands run from `C:\dev\ofg`.

Before implementation:

    git -c safe.directory=C:/dev/ofg status --short
    npm run check:shaders
    npm test
    npm run smoke:rust
    npm run smoke:browser

Milestone 1 expected validation:

    cargo test -p terrain_core sea_depth
    cargo test -p engine_web water
    npm run test:ts

Milestone 2 expected validation:

    npm run build:shaders
    npm run check:shaders
    cargo test -p engine_web water
    npm run test:rust

Milestone 3 expected validation:

    npm run check:shaders
    npm run smoke:rust
    npm run smoke:terrain-presets
    npm run test:rust

Milestone 4 expected validation:

    npm run check:shaders
    npm run smoke:rust
    npm run smoke:browser
    npm run bench:terrain:rust

Milestone 5 and final validation:

    npm test
    npm run build
    npm run check:shaders
    npm run check:wasm
    npm run coverage:rust
    npm run smoke:rust
    npm run smoke:browser
    npm run smoke
    npm run smoke:terrain-presets

Expected final evidence:

    - Rust tests pass for water settings, depth math, and renderer resources.
    - TypeScript tests pass for command/debug typing without TypeScript water
      ownership.
    - Rust smoke writes water-specific PNGs and report JSON under
      artifacts/rust-smoke/<run-id>/.
    - Browser smoke reports Rust/wgpu water runtime status and nonblank frames.
    - Coverage output does not list modified implementation files below the
      default attention threshold, or this plan records a justified exception.

## Milestone Review

Final milestone review, 2026-06-11 / Codex:

    Scope:
      Milestones 1 through 5 of this ExecPlan: Rust terrain sea-depth helper,
      Rust/wgpu water renderer resources, water shader, planar reflection,
      browser debug controls, smoke coverage, and API/architecture docs.

    Reviewers:
      Local five-pass review only: contract, code quality, legacy, correctness,
      and validation. A multi-agent tool exists in this session, but its tool
      contract allows spawning only when the user explicitly requests
      sub-agents, so no sub-agents were spawned for this process-required
      milestone review.

    Required findings fixed:
      - Browser WebGPU rejected `textureSample` in non-uniform water control
        flow. Fixed `water.wgsl` reflection sampling with
        `textureSampleLevel(..., 0.0)` and reran shader, TypeScript, browser
        smoke, Rust smoke, and combined smoke validation.
      - `crates/engine_web/src/water_renderer.rs` initially appeared below the
        default Rust coverage attention threshold at 89.4%. Added focused
        native resource coverage for resize, reflection accessors, bathymetry
        reuse, invalid sea level, and uniform packing; reran coverage and the
        filtered report now lists `none`.

    Follow-ups recorded:
      - `tools/browser-smoke.mjs` is 1563 lines and should be split before more
        browser smoke flows are added. This was already an oversized script;
        the water additions extend it but do not require an immediate split to
        complete the sea renderer.
      - `crates/engine_web/src/water_renderer.rs` is 820 lines including native
        tests. It is still a focused water module and below the stricter 1000
        line concern, but further water feature work should split tests or
        target helpers before adding more responsibilities.
      - `crates/engine_web/src/wgpu_renderer.rs` remains oversized. This plan
        kept water-specific resource ownership in `water_renderer.rs`, but the
        browser frame orchestration still lives in the large renderer file.

    Rejected findings:
      - `#![cfg_attr(not(target_arch = "wasm32"), allow(dead_code))]` in
        `water_renderer.rs` is accepted. Some accessors are used by the browser
        renderer on wasm while native tests exercise the resource path
        selectively; the allow is target-scoped and does not hide browser
        warnings.

    Validation rerun:
      `cargo fmt --all --check`, `npm run check:shaders`, `npm run
      check:wasm`, `npm run build`, targeted Rust tests, `npm run test:ts`,
      `npm test`, `npm run coverage:rust`, `npm run smoke:rust`, `npm run
      smoke:browser`, `npm run smoke:terrain-presets`, `npm run
      bench:terrain:rust`, and `npm run smoke` all passed.

    Remaining risk:
      Visual quality still needs taste tuning after real playtesting. The
      architecture and validation gates for the first sea renderer are complete.

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective.
2. Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` if the milestone
   changes active contracts or architecture.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands.
6. Record the review summary, commands, artifacts, and remaining risks in this
   ExecPlan.

## Validation and Acceptance

Behavioral acceptance:

- The normal browser view renders a sea at `y = 0.0` that is stable while moving
  the camera/player.
- Terrain and models above the sea surface continue to occlude water.
- Dry terrain at or above sea level is not covered by the water composite pass
  just because the camera ray crosses the sea plane.
- Terrain below the sea surface appears through shallow water.
- The visual style changes with vertical bottom depth: shallow water is lighter
  and clearer; deep water is darker and bluer.
- The perceived water density changes with optical path length: underwater
  terrain farther along the eye ray becomes murkier even when vertical bottom
  depth is similar.
- Open-water/sky pixels render as water when the sea plane is visible, rather
  than disappearing because sky linear depth is `0.0`.
- Planar reflection can be enabled and disabled through a Rust command, and
  enabled reflection visibly reflects sky and above-water scene content at
  grazing angles.
- Water debug views can present bottom depth, path length, and reflection or
  Fresnel information without TypeScript computing those values.
- Water status reports bathymetry runtime, grid size, and world span so smoke
  tests can prove the bathymetry source is active.
- Existing bloom, tone mapping, depth of field, sky debug, shadow debug, terrain
  LOD debug, and browser smoke controls continue to work.

Test and artifact acceptance:

- `npm run check:shaders` passes after adding `water.wgsl` and generated shader
  artifacts.
- `npm test` passes.
- `npm run build` passes when generated artifacts or wasm bindings change.
- `npm run smoke:rust` produces nonblank water screenshots and report JSON with
  water runtime and bathymetry fields.
- `npm run smoke:browser` passes and verifies the Rust/wgpu water runtime
  sentinel through debug snapshot fields.
- `npm run coverage:rust` passes with no modified implementation files listed
  below the default filtered coverage threshold, unless this plan records a
  specific exception.

## Idempotence and Recovery

Water settings should default to production rendering with water enabled only
after the composite path is proven. During early milestones, keep a command or
debug option that disables the water pass and returns to the current opaque
scene plus post-process behavior.

If the water composite pass causes a regression, disable water in default
settings while keeping the resource creation tests. If the ping-pong target
layout causes WebGPU validation errors, revert to the last known passing target
layout and add a focused GPU resource test before retrying. If planar reflection
is too costly or unstable, keep Milestone 3 water as the accepted baseline and
record reflection as disabled-by-default until performance evidence supports it.

Do not roll back unrelated dirty worktree changes. This plan should be
implemented through small patches that touch only the water, renderer, shader,
debug typing, smoke, and documentation files required for each milestone.

## Artifacts and Notes

Expected artifact locations:

- Rust image smoke screenshots and reports:
  `artifacts/rust-smoke/<run-id>/`
- Browser smoke screenshots and reports:
  `artifacts/browser-smoke/<run-id>/`
- Rust coverage summaries:
  `artifacts/coverage/rust/`
- Optional performance captures:
  `artifacts/browser-perf-debug/` or existing perf-debug capture locations if
  extended.

Final validation evidence, 2026-06-11 / Codex:

    cargo fmt --all --check
    npm run check:shaders
    npm run check:wasm
    npm run build
    cargo test -p terrain_core sea_depth
    cargo test -p engine_web water
    cargo test -p ofg_test_harness render_smoke
    npm run test:ts
    npm test
    npm run coverage:rust
    npm run smoke:rust
    npm run smoke:browser
    npm run smoke:terrain-presets
    npm run bench:terrain:rust
    npm run smoke

All commands passed after the `textureSampleLevel` water shader fix. The
coverage gate reported:

    files below 90% line coverage (excluding 24 default-ignored file(s)):
      none

Key generated artifacts and reports:

    Browser smoke:
      artifacts/browser-smoke/2026-06-11T04-30-37-163Z/report.json
      artifacts/browser-smoke/2026-06-11T04-30-37-163Z/browser-water-final.png
      artifacts/browser-smoke/2026-06-11T04-30-37-163Z/browser-water-bottom-depth.png

    Rust smoke:
      artifacts/rust-smoke/run-1781133812-913/report.json
      artifacts/rust-smoke/run-1781133812-913/boot-frame.png

    Terrain preset smoke:
      artifacts/rust-smoke/run-1781133611-301/report.json

    Terrain benchmark:
      artifacts/terrain-bench/run-1781133742-222/report.json

The Rust smoke report includes water entries such as:

    "runtime": "rust-wgpu"
    "enabled": true
    "reflectionEnabled": false
    "bathymetryRuntime": "rust-heightfield"
    "bathymetryGridSize": 128
    "bathymetryWorldSpanMeters": 4096.0

Post-completion correction, 2026-06-11 / Codex: The first browser water debug
screenshot used the default first-person hillside view and did not visibly show
water. `tools/browser-smoke.mjs` now places the debug-fly camera at a
deterministic coastal view, captures both `browser-water-final.png` and
`browser-water-bottom-depth.png`, and asserts water-colored/depth-debug pixels
so a dry terrain view cannot satisfy water smoke coverage.

Initial draft note, 2026-06-10 / Codex: This plan was created from the proposal
to simulate a sea at sea level with water style changing by bottom depth,
perceived density changing by optical path length, and planar reflection. The
explicit decision is that vertical water depth comes from a terrain-derived
renderer bathymetry texture sampled at the water-surface point, while optical
path length comes from the opaque scene linear-depth texture. This is not
terrain chunk generation or hydrology.

## Interfaces and Dependencies

Likely Rust interfaces:

    pub const SEA_LEVEL_METERS: f32 = 0.0;

    pub enum WaterDebugView {
        Final,
        BottomDepth,
        PathLength,
        Fresnel,
        Reflection,
    }

    pub struct WaterSettings {
        pub enabled: bool,
        pub reflection_enabled: bool,
        pub sea_level_meters: f32,
        pub shallow_depth_meters: f32,
        pub deep_depth_meters: f32,
        pub absorption_rgb: [f32; 3],
        pub shallow_color: [f32; 3],
        pub deep_color: [f32; 3],
        pub wave_scale: f32,
        pub wave_strength: f32,
        pub debug_view: WaterDebugView,
    }

    pub struct WaterStatus {
        pub runtime: &'static str,
        pub enabled: bool,
        pub reflection_enabled: bool,
        pub sea_level_meters: f32,
        pub bathymetry_runtime: &'static str,
        pub bathymetry_grid_size: u32,
        pub bathymetry_world_span_meters: f32,
        pub debug_view: WaterDebugView,
    }

    pub struct WaterBathymetryGrid {
        pub center_x: f32,
        pub center_z: f32,
        pub world_span_meters: f32,
        pub texel_count: u32,
        pub depths_meters: Vec<f32>,
    }

    pub fn sea_depth_at_for_variant(
        seed: u32,
        descriptor: TerrainVariantDescriptor,
        x: f64,
        z: f64,
        sea_level: f64,
    ) -> Result<f64, TerrainVariantValidationError>;

Likely browser command shape, added to `src/engine/web/browserGameTypes.ts` as
thin typed shell only:

    { type: "setWaterDebugView", view: "final" | "bottomDepth" |
      "pathLength" | "fresnel" | "reflection" }

    { type: "setWaterOptions", enabled?, reflectionEnabled?,
      seaLevelMeters?, shallowDepthMeters?, deepDepthMeters?,
      waveScale?, waveStrength? }

Likely shader and generated-artifact changes:

- Add `src/engine/render/shaders/water.wgsl`.
- Extend `tools/build-shaders.mjs` to generate
  `src/generated/render/waterShader.ts`.
- Add `src/engine/render/shaders/WaterShader.test.ts`.
- Keep `post.wgsl` reading the final scene color and final linear depth after
  water compositing.

Likely renderer modules:

- Add `crates/engine_web/src/water.rs` for settings, status, uniforms, debug
  view parsing, bathymetry grid metadata, and pure math tests.
- Add `crates/engine_web/src/water_renderer.rs` if resource/pipeline code is
  large enough to avoid growing `wgpu_renderer.rs` further.
- Update `crates/engine_web/src/perf.rs` if water and reflection receive GPU
  timing or render counters.
