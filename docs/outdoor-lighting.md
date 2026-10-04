# Outdoor lighting laboratory

Run `build/native/ofg.exe --outdoor`, or open the browser build at `?demo=outdoor`.
The existing Render Settings window contains the day/night clock, atmosphere, clouds, exposure and cascaded-shadow
controls. Reset restores the fixture's lighting, including its clock. The studio remains the default fixture.
`--no-ui` / `&ui=0` retain the direct presentation path. Browser `hour=0..24` and `clouds=0..1` select reproducible
starting conditions; for example `?demo=outdoor&hour=18.5&ui=0`.

The fixture has rough/metallic spheres, shadow/haze landmarks, a receiving ground plane, and perforated cards
including a mirrored instance. R restores the outdoor overview; F moves to its close-up. Camera controls match the
existing PBR laboratory. Scene settings remain copyable CPU values and edits pass through the existing validation.

## Code and ownership

| Location | Responsibility |
| --- | --- |
| `src/scene/outdoor-lighting.{h,cpp}` | Clock, control values, opposite sun/moon directions, optical transmission and exposure |
| `src/render/shadow-cascades.{h,cpp}` | Pure CPU split fitting, caster depth inclusion, stable radius and texel snapping |
| `src/render/draw-list.{h,cpp}` | Freeze lighting/camera state; retain loaded offscreen casters separately from visible draws |
| `src/render/sky-renderer.{h,cpp}` | Own atmosphere lookup, aerial lookup and disk-free cube capture passes |
| `src/render/environment-renderer.{h,cpp}` | Upload baked environments and filter/publish dynamic cube-array generations |
| `src/render/shadow-renderer.{h,cpp}` | Own four depth layers and opaque/masked depth pipelines |
| `src/render/lighting-pass.{h,cpp}` | Shared compilation, matrix packing and explicit raster pass operations |
| `src/render/graphics.cpp` | Prepare shared mesh/material resources and sequence the concrete renderers |
| `src/ui/render-settings-panel.{h,cpp}` | Existing settings widgets; Workspace retains edit/reset ownership |
| `shaders/sky`, `shaders/environment`, `shaders/shadows` | Atmosphere/cloud generation, IBL filtering, depth and PCF sampling |
| `src/lab/outdoor-fixture.{h,cpp}` | Original procedural lighting inspection scene |

Graphics owns the three concrete helpers for its device lifetime. Submitted RHI commands retain resources, including
replaced depth allocations. Native shutdown drains the queue before releasing Graphics. No scene resource registry,
render graph or reflection-probe manager is introduced. Outdoor rendering requires HDR and an Environment resource;
its existing precomputed BRDF lookup is shared with the generated environment. Baked environment files are unchanged.

## Lighting model

One directional slot is derived from the clock: the sun above the horizon and an opposite full moon below it.
Other authored light slots remain available. Defaults are a paused 10:00 clock, a 20-minute full cycle, 60-degree
noon elevation, 120,000 lux sunlight and 0.25 lux moonlight before atmospheric transmission. These are controllable
laboratory values, not an astronomical calendar or lunar-phase model. The clock also supplies deterministic cloud wind.

The atmosphere uses Earth-scale Rayleigh, Mie and ozone extinction, analytic segment integration and an isotropic
multiple-scattering closure adapted from the MIT-licensed Hillaire reference. See [provenance](../shaders/sky/NOTICE.md).
The raster lookups are transmittance 256x64, multiple scattering 32x32, sky view 512x256, and aerial perspective
32x32x32. Density changes regenerate the first two. Celestial/altitude changes regenerate sky view; camera/projection
changes also regenerate aerial perspective. Identical paused frames reuse these lookups.

Two procedural horizontal cloud layers share world-space noise, wind and optical depth between visible sky,
environment capture and direct-light cloud shadows. Cloud color is approximate sky fill plus attenuated direct light.
Near-horizontal unresolved detail blends toward a mean density. There is no volumetric cloud traversal, cloud self-shadow
transport, precipitation or weather simulation. The aerial lookup stores RGB in-scattering and a scalar mean transmission;
colored extinction and coarse spatial sampling remain approximations. The camera volume stores only atmospheric
scattering: the lit planet surface is included in sky/closure lookups, never composited as fog onto scene geometry.
Quadratically spaced slices are interpolated by physical distance, preserving the near-camera zero-scattering limit.

The **Haze (aerial perspective)** checkbox disables only scene haze; exposure, sky and IBL remain active. **Haze lookup
range (m)** sets the furthest represented distance, not a start distance or density. At 10,000 m the first nonzero
slice is about 10.4 m away, but correct interpolation makes foreground haze proportional to the short ray length.
Use Rayleigh/aerosol density to control atmospheric strength. The [haze correction](archived/fix-aerial-perspective-banding.md) records the reproduced bugs and validation.
Beyond the lookup range, composition reuses the final slice, so attenuation does not continue with distance.
Extending coverage and adding height fog are deferred. The pervasive magenta twilight remains an unresolved visual
issue; its cause has not been isolated.

Visible sun/moon disks use their solid angle and RGB atmospheric transmission. Capture omits both disks, so direct
illumination is supplied once by the directional light; atmospheric scattering and clouds remain in IBL. Exposure is
applied before writing visible HDR radiance to float16. Values beyond finite float16 range saturate before storage
to prevent solar-disk/specular overflow from producing infinities in tone mapping. Automatic EV transitions between day 15, horizon 10 and night -3;
manual EV and the existing exposure multiplier remain available. The night value deliberately supports inspection.
This is a clock-driven exposure curve, not scene-metered auto exposure: increasing moon lux does not change exposure.
Scene-metered auto exposure is planned for later; the current moon intensity and exposure defaults remain unchanged.

## Environment generations and bindings

Baked Lambertian/GGX/Charlie filters occupy one cube array with three cubes. The dynamic array has six cubes:
`cube = generation * 3 + filter`, with six physical face layers per cube. A 128-square disk-free capture feeds
64-square, seven-mip filters using 128 deterministic samples. Each generation now takes **24 rendered frames**:
first six frames capture one face each; the following eighteen filter one face of one filter kind per frame, including
its seven mips. Thus each frame submits either one capture draw or seven filtering draws, instead of all 132.
The first capture also copies the current sky-view lookup into a dedicated 512x256 texture. Camera, atmosphere,
clouds, wind and celestial inputs are frozen with that lookup so faces cannot mix different moments.

Materials continue sampling the last complete generation throughout the job. Only after all faces/filters/mips are
submitted does the ordered graphics queue publish the replacement, without a CPU wait or readback. Ordinary updates
blend over 0.1 seconds; the previous slot is not reused until that blend finishes. Starts are at most four per second,
but actual update frequency is limited by the 24-frame job and blend. There is no catch-up burst after a slow frame.
The first generation also uses this schedule: the sky and direct lights render immediately, with zero indirect light
until the first complete environment becomes available. No uninitialized cube storage is sampled.

Edits during work are coalesced into the next job rather than repeatedly cancelling captures. A deliberate time or
appearance change bypasses the start cadence, but still uses the 24-frame schedule and waits for any active blend.
Its completed generation switches without blending. An edit during an existing job can therefore take the rest of
that job plus another 24 frames to reach IBL. The visible sky/direct light update immediately. Each complete IBL
slot retains its captured sun height; sampling compensates its radiance by the source/current time-of-day exposure
ratio, separately for each side of the blend. This prevents stale daylight from blowing out under midnight exposure.
Current manual EV and authored exposure controls still apply. Reflection direction/color and cloud state remain stale
until publication; exposure compensation does not synthesize the new sky. Unchanged paused snapshots do no IBL work. This is bounded work on the normal graphics queue, not asynchronous-compute execution.

The portable profile is **16 sampled textures per shader stage**, not 16 total bindings. Twelve material textures
plus the packed IBL, BRDF lookup, shadow array and aerial array fill this budget; a maximum material uses 14 samplers.
Stronger adapters can expose more, but the required browser path is verified with a requested device limit of 16.
Cube arrays save binding slots and allow generation selection in one resource. No speed advantage over separate
cubes is claimed. Future reflection probes can reuse the representation, but no probe ownership/blending scheme is
implemented now.

The pinned RHI WebGPU backend needed explicit depth-resource/comparison-sampler reflection and single-face attachment
views. `cmake/rhi-webgpu-lighting.cmake` generates narrowly adapted source copies, leaving the submodule unchanged.
Because this revision has no view-dimension field, `lightingView` labels an attachment `ofg-lighting-attachment`;
the adaptation interprets that label as a 2D view. Ordinary cube/array sampled views retain their original dimension.
CMake validates replacement anchors. Revisit this workaround when updating the RHI pin.

## Shadows and diagnostics

Four 1024-square D32 layers cover 250 metres by default (2048 is selectable). Practical splits use lambda 0.7,
rotation-independent receiver spheres, light-space texel snapping and a 10% transition. Caster bounds extend depth
independently of camera visibility. The final cascade fades out; 3x3 comparison sampling uses geometric-normal offset
(1.5 texels by default) and depth bias. Opaque and masked geometry cast; blended geometry does not. Masking uses base alpha, vertex alpha,
UV selection/transform, cutoff and the material sampler. Double-sided and mirrored winding are preserved.
`MeshRenderer::setCastsShadows(false)` excludes a caster without changing its received lighting. The flat fixture
ground uses this to avoid unnecessarily extending caster depth over its entire ten-kilometre width.

Shadows multiply only direct celestial light. Cascade-color and visibility modes, cascade end distances and IBL
snapshot age/blend and update progress/pass counts are available in Render Settings. The shadow payload is 16 MiB
at 1024 or 64 MiB at 2048; sky/capture/aerial/dynamic-filter payload is approximately 4.63 MiB (including the 1 MiB
frozen sky lookup). These are texture payload sizes, **not measured driver residency**: alignment, shared BRDF storage and pending retirement are excluded. Full allocation/retirement
telemetry and GPU timestamp profiling remain limitations.

## Verification

`tests/outdoor-lighting-test.cpp` covers clock wrap/value independence, invalid settings, celestial direction,
transmission boundaries, cascade receiver/caster coverage, snapping and offscreen/excluded casters.
`tests/outdoor-rendering-test.cpp` uses real D3D12 with validation, renders noon/sunset/twilight/night, exercises mirrored
masked cards, verifies all 126 face/mip subresources against a frozen reference while live sky inputs change,
checks partial publication and latest-edit coalescing, and compares direct and indirect lighting with shadows
enabled/disabled. Readback allows one 8-bit level for the indirect comparison; it does not require identical pixels between backends. A solar-disk regression
checks the brightest allowed sun at low manual exposure for finite float16 storage. Vacuum-volume readback rejects
ground radiance leaking into fog; a synthetic GPU volume linear in metres tests foreground/far-distance interpolation
and exposure preservation when haze is disabled.

Run `npm run smoke:outdoor` after the web build. It saves noon/sunset/twilight/night/overcast/settings/resize captures and
actual device diagnostics, including the ImGui midnight preset and running clock, under `artifacts/lighting/browser`, with a 16-texture device limit. Native evidence is in
`artifacts/lighting/native`. The timing file reports 30 warmed paused frames at 640x400 including CPU encoding,
submission and a GPU drain. Browser timing reports callback frame intervals, including browser scheduling; neither
is a GPU-only timing or a production performance guarantee. Broader regression results live in the [completed implementation plan](archived/outdoor-lighting.md).
Incremental scheduling and exposure-transition evidence are recorded in the
[completed update plan](archived/amortize-environment-updates.md).

Remaining quality limits: coarse sky/IBL quadrature and cloud layers can show low-frequency banding or flat cloud
shapes. IBL work is spread across frames, but total filtering cost is unchanged, and update latency grows at low
frame rates (24 frames is 0.4 seconds at 60 fps or 2.4 seconds at 10 fps, before any blend wait). Atmospheric/aerial
regeneration and first-use shader compilation are not amortized by this change. This is a local-world
laboratory with float rendering coordinates, not a large-world rebasing solution. Sky/cloud lighting does not account
for local terrain occlusion in IBL. Skinned/morph geometry follows the existing undeformed renderer limitation.
