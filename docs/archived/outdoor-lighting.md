# Implement outdoor lighting

This living ExecPlan follows [PLANS.md](../../PLANS.md). Implementation was authorized by the user and completed on 2026-10-04. The existing ImGui workspace is extended, not replaced. Native D3D12 and browser WebGPU are required.

## Purpose and contracts

Deliver a selectable outdoor laboratory with a game-clock sun/opposite moon, atmospheric sky, two cloud layers, cloud shadows, aerial perspective, dynamic Lambertian/GGX/Charlie IBL, and four stabilized shadow cascades. The visible disks are excluded from environment capture; geometry/cloud visibility affects direct celestial light only. Scene lighting settings remain copyable values so validated UI drafts and reset are independent. Keep studio/model diagnostics and the 12-material-texture portable profile.

CPU code belongs in scene/outdoor-lighting and render/shadow-cascades. Graphics owns concrete SkyRenderer, EnvironmentRenderer and ShadowRenderer helpers. Draw extraction retains offscreen shadow casters separately. Extend the existing Render Settings window. No reflection-probe manager, render graph, large-world system, precipitation or volumetric cloud traversal is authorized by this scope.

## Progress

- [x] (2026-10-04) Inspected current workspace, renderer, settings, RHI binding implementation and build workflows. Working tree clean at implementation start.
- [x] (2026-10-04) Portable cube-array/depth-sampling foundation and baked-environment regression.
- [x] (2026-10-04) CPU settings, clock, cascade calculations, outdoor fixture and existing-panel controls.
- [x] (2026-10-04) Atmosphere, celestial light and aerial perspective.
- [x] (2026-10-04) Shadow extraction/rendering/filtering.
- [x] (2026-10-04) Dynamic IBL and layered weather.
- [x] (2026-10-04) Native/browser tests, inspected visual evidence, generation/cascade/payload diagnostics and documentation review. Performance and driver-residency limits are explicit.

## Surprises and discoveries

The pinned WebGPU backend requests adapter limits; saved device evidence reports 48 sampled textures and 16 samplers. OFG's 16 sampled-texture limit is a portable layout policy. The pinned shader-object layout incorrectly classifies comparison samplers/depth resources and has incomplete storage texture support. Use a revision-checked generated-source correction and raster generation passes.

## Decisions

Keep cube arrays: cube index = generation * 3 + filter, physical layer = cube * 6 + face. Baked assets keep their file format. Use 16 total sampled textures: 12 material, packed IBL, BRDF lookup, shadow array, aerial-perspective array. Use the existing ImGui input and composition. Defaults: paused daytime, 20-minute cycle, 60-degree noon elevation, four 1024-square cascades over 250 metres, split lambda 0.7, 10% overlap, 3x3 PCF, 128-square capture, 64-square filtered environment, 128 filter samples, 4 Hz update with 0.1-second transition. Scalar aerial transmission is an explicit approximation.

## Plan of work

First prove GPU layouts and preserve studio rendering. Add CPU values/evaluation and cascade fitting with meaningful tests. Implement sky lookup/capture passes and direct celestial attenuation, then independent shadow caster extraction and depth passes. Complete environment generation and weather; expose controls through a cohesive settings-panel helper. Keep CPU headers free of RHI/ImGui. Shared resource preparation stays in Graphics; GPU helpers use direct RHI handles and submission retention.

## Validation and acceptance

Use the build-native/build-web skills and DEVELOPING.md environment setup. Run cmake --preset cpu-tests, cmake --build --preset cpu-tests, ctest --preset cpu-tests; native-debug equivalents; web configure/build; node tools/pbr-shader-check.mjs; npm.cmd run smoke:pbr; npm.cmd run smoke:ui; npm.cmd run smoke:web, plus the new outdoor smoke. Each command requires its documented toolchain environment. These are planned checks, not implementation evidence.

CPU checks cover finite inputs, clock wrap/reset, opposite bodies, cascade coverage/stability and offscreen casters. GPU checks cover cube orientation/filtering, actual depth comparisons, masks, independent lighting contributions and atomic environment publication. Inspect noon/twilight/night/overcast, camera/cascade motion, resize and UI capture on both hosts. Save screenshots and diagnostics under artifacts/lighting. Record hardware, viewport and measurement method. No numerical coverage percentage or performance claim is inherited.

## Recovery and ownership

Preserve existing work and historical ofg-old2. Keep dependency pins unchanged. Publish complete allocations/generations only; retain submitted resources through RHI. Reset transient lighting state on shutdown and regenerate after initialization. No unbounded environment update queue.

## Outcomes and retrospective

The outdoor laboratory is implemented on native D3D12 and browser WebGPU. Launch with `--outdoor` or
`?demo=outdoor`; the existing settings window includes clock presets, atmosphere/clouds, exposure, cascaded shadows
and generation/cascade diagnostics. The final native CPU run passes 97 cases / 2245 assertions. The final outdoor
GPU run passes 40 assertions, including HDR overflow and direct-only shadow comparisons. All four native CTest
targets passed (latest complete run 84.17 seconds); the separate CPU-only build/CTest also passed.

Browser configure/build and outdoor, PBR, ImGui, texture/checkerboard and model smoke checks passed. The final
outdoor report proves a requested device limit of 16 sampled textures, with no errors, on Intel gen-12lp,
Chrome 154.0.8037.95. Noon/sunset/twilight/moon/overcast images, camera motion, resize, midnight ImGui preset and an
advancing game clock were captured and inspected. Native window presentation, maximize, minimize/restore,
resize and clean shutdown passed. Native GPU: NVIDIA GeForce RTX 3050 Ti Laptop GPU, D3D12 Debug with validation.

Evidence: `artifacts/lighting/native`, `artifacts/lighting/browser/report.json`, and the existing PBR/ImGui/texture/model
artifact directories. Shader ABI/WGSL validation, clang-format 22.1.3, JS syntax, local documentation links and
`git diff --check` passed. The Emscripten link retained its existing Asyncify/WASM-exceptions warning.

This completes the lighting implementation baseline. Cloud layers and coarse atmospheric/environment quadrature
are approximations; low-sun banding, synchronous update spikes, GPU profiling and full allocation/retirement
telemetry remain documented limitations. The representation supports future cube-array probes, but no probe system
was added. The following notes preserve the implementation decisions and observed failures that informed the result.

## Implementation discoveries and review notes

2026-10-04: the first native captures exposed daylight IBL remaining during a deliberate jump to night. Such edits
now publish the complete new generation without crossfade; normal 4 Hz updates still blend. Sampling skips the
uninitialized previous generation on first publication. Tests now reject predominantly white time-jump captures.
Cloud illumination uses attenuated direct light and sky fill rather than an artificial below-horizon light source.
Night EV -3 makes moonlight readable. Unchanged atmosphere/camera lookups are cached.

The existing frame budget now has 16 textures / 14 samplers at maximum material occupancy. Cube arrays provide
binding space, not a demonstrated performance advantage. Revision-checked WebGPU patches preserve the dependency
pin. Alpha cards verify texture/UV/culling parity, and an explicit caster flag keeps the enormous flat receiving
ground from unnecessarily expanding cascade depth. All source layouts and numerical approximations are documented
in [the implementation note](../outdoor-lighting.md).

An early warm native sample measured 44.69 ms per paused clear-noon 640x400 frame on NVIDIA GeForce RTX 3050 Ti
Laptop GPU, native Debug D3D12 with validation, using 30 encode+submit+GPU-drain wall-time samples. This is not a
GPU timestamp measurement or a 60 fps claim. Diagnostic texture payload excludes driver alignment and in-flight
retirement; full driver residency telemetry remains unimplemented. Layered clouds, scalar aerial transmission,
coarse LUT/filter quadrature and float world coordinates remain explicit laboratory limits.

Native shadow-on/off comparison isolated receiver striping to insufficient normal bias, rather than sky filtering.
The default offset now covers 1.5 shadow texels for the 3x3 PCF footprint; the repeated capture removes the artifact
while retaining ground contact shadows. The bias remains adjustable. Native live-window presentation, maximize,
minimize/restore, resize and clean exit passed; the inspected maximum-size screenshot is
`artifacts/lighting/native/maximized.png`. The latest outdoor browser run passes the actual time presets and limits.

The full browser regression sequence passed: outdoor, PBR including maximum binding layout, ImGui input/edit/reset,
texture/checkerboard and model loading. The UI test was updated to click Reset at its new inspected row after the
outdoor toggle was inserted. Final native CPU suite: 97 cases / 2245 assertions. The post-bias outdoor GPU check
passes 28 assertions with no validation errors. A later native timing sample: 37.66 ms under the same documented
Debug/validation workload; browser paused callback interval was 124.18 ms on Intel gen-12lp, Chrome 154.0.8037.95,
1000x761 scene canvas, measured across 30 callbacks. These measurements include host work and are not comparable
GPU-only results. No production frame-rate or full driver-residency claim is made.

The solar-disk regression now covers the allowed 200,000-lux / manual EV -2 boundary and verifies finite FP16
values, including a saturated center pixel. Raw lookup and exposed surface/sky writes saturate before FP16 storage.
The added near-horizon sunset capture also exposed overexposure with the original horizon EV 6. The default horizon
anchor is now EV 10; noon and night anchors remain 15 and -3. Sunset is included in native and browser captures.

Final review covered ownership, resource views, shader binding limits, root parameter layout, first-generation IBL,
mask/cull parity, HDR range, cached snapshot invalidation and failure publication, settings copy/reset, comments,
license provenance and documentation. Environment BRDF bindings now follow scene asset changes even when no new
sky capture is needed; lookup snapshot state publishes only after successful submission. No unrelated dependency
changes or general framework were added. Formatting (clang-format 22.1.3), whitespace, JS syntax and 49 local
documentation links passed. The post-exposure native outdoor test passes 40 assertions.

Timing samples taken during development sometimes overlapped compilation/browser work and varied substantially;
they are diagnostics rather than an isolated performance acceptance benchmark. CPU/GPU timestamp separation and
allocation/retirement telemetry remain explicit follow-ups. Functional coverage is defined by the deterministic
CPU cases, native integration comparisons and cross-host smoke matrix; no line-coverage percentage is asserted.


## Final verification record

The final web rebuild and `node tools/outdoor-smoke.mjs` passed after the sunset EV adjustment. The inspected
running-cycle capture shows a moonlit scene and clock advancement from midnight to 0.04 hours through real ImGui
input. The final paused-browser measurement was 107.47 ms per callback across 30 frames at 1000x761 on Intel
WebGPU, with validation and browser scheduling included; it is not a GPU-only measurement. Performance remains a
follow-up rather than an inferred benefit of the cube-array representation.
