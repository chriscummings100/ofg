# Add Bloom Post Effect and Intermediate Render Target Pool

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

OFG needs a bloom post effect so bright HDR features such as the procedural sun, high-energy sky highlights, emissive factory parts, and future energy systems can glow naturally after lighting but before final display conversion. Bloom should be implemented as a renderer-owned post effect: it reads the completed HDR scene color texture, builds a blurred bloom texture at reduced resolutions, and the tone mapper composites that bloom with the HDR scene before applying exposure, ACES tone mapping, and output encoding.

The first user-visible result should be subtle and controllable. With bloom enabled, the sun disc and other values above the HDR threshold should produce a soft halo without washing out the whole scene. With bloom disabled or intensity set to zero, screenshots should match the tone-mapped sky/shadow renderer apart from ordinary floating-point noise.

This plan also introduces recyclable intermediate render targets. Bloom needs several temporary color textures, and future post effects will need more. Rather than each pass owning one-off scratch textures, the renderer should own an `IntermediateTargetPool` that leases frame-scoped render targets by descriptor, reuses them across frames and resizes, and reports counters so tests can prove ordinary frames do not recreate textures, texture views, pipelines, layouts, or shader modules.

## Progress

- [x] (2026-07-04 06:20Z) Read `C:\dev\ofg\docs\plans\procedural-sky-environment-plan.md` and identified the intended pass order: opaque and sky into HDR scene color, then tone mapping to the platform target.
- [x] (2026-07-04 06:35Z) Researched bloom implementation patterns from production and vendor references: Unreal standard bloom and convolution bloom, Unity bloom settings and filter choices, NVIDIA GPU Gems real-time glow, AMD FidelityFX SPD and Blur, and the Call of Duty Advanced Warfare post-process talk metadata.
- [x] (2026-07-04 06:43Z) Read the current renderer contracts and local worktree state, including HDR `SceneColorTarget`, shared `DepthTarget`, `ToneMapPass`, `RendererCounters`, and the active cascaded shadow map plan.
- [x] (2026-07-04 06:43Z) Drafted this ExecPlan with a bloom pyramid, tone-map composite integration, and a renderer-owned intermediate target pool.
- [ ] Implement Milestone 0: intermediate target pool and frame lease contract.
- [ ] Implement Milestone 1: bloom settings, pyramid sizing, and CPU-side packing/math tests.
- [ ] Implement Milestone 2: bloom downsample/prefilter pass and durable GPU resource tests.
- [ ] Implement Milestone 3: bloom upsample chain and tone-map composite.
- [ ] Implement Milestone 4: renderer integration, visual smoke, screenshots, docs, and coverage.

## Surprises & Discoveries

- Observation: The active sky plan already requires almost all of the post-process boundary bloom needs.
  Evidence: `C:\dev\ofg\docs\plans\procedural-sky-environment-plan.md` specifies `opaque PBR pass -> RGBA16Float scene color -> tone-map pass -> platform color target`, then later extends scene rendering to opaque plus sky before tone mapping.

- Observation: The working tree already contains part of the sky/HDR groundwork.
  Evidence: `C:\dev\ofg\cpp\include\ofg\render\scene_color_target.hpp`, `depth_target.hpp`, `tone_map_pass.hpp`, and their `.cpp` files exist locally; `C:\dev\ofg\cpp\src\render\renderer.cpp` renders opaque content into `SceneColorTarget` and then runs `ToneMapPass`.

- Observation: A single downscaled buffer plus one horizontal/vertical blur is a valid minimal bloom, but a pyramid better matches production game bloom.
  Evidence: NVIDIA GPU Gems describes low-resolution glow sources and separable blur for performance. Unreal documents combining multiple blur sizes, with wide blur work shifted to lower resolutions. Unity exposes bloom downscale, max iterations, scatter/radius, and filter choices such as Gaussian, Dual, and Kawase.

- Observation: The active cascaded shadow map plan creates another durable offscreen target family, but not a recyclable target need.
  Evidence: `C:\dev\ofg\docs\plans\cascaded-shadow-maps-plan.md` specifies a persistent `ShadowMapTarget` depth texture array, whereas bloom requires temporary color textures whose lifetimes are limited to the post-process chain.

- Observation: `C:\dev\ofg\GUIDES.md` is referenced by project instructions but is not present in this checkout.
  Evidence: `Get-Content -Path GUIDES.md -Raw` from `C:\dev\ofg` failed with `Cannot find path 'C:\dev\ofg\GUIDES.md'`.

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

- Decision: Add a renderer-owned `IntermediateTargetPool` before implementing bloom.
  Rationale: Bloom immediately needs multiple same-frame scratch targets. A small pool provides a reusable pattern for later post effects without introducing a full render graph.
  Date/Author: 2026-07-04 / Codex.

- Decision: Keep the first post-effect orchestration explicit in `Renderer`, not through a generic render graph or virtual `PostEffect` interface.
  Rationale: The current renderer has a small set of concrete passes. An explicit `BloomPass` plus target pool is easier to test and can evolve into a graph after there are several post effects with real scheduling pressure.
  Date/Author: 2026-07-04 / Codex.

- Decision: Composite bloom in `ToneMapPass` instead of writing bloom back into `SceneColorTarget`.
  Rationale: The tone mapper is already the final HDR-to-display boundary. Adding bloom there avoids an extra full-resolution HDR combine pass and preserves the original scene color for future effects or debug captures.
  Date/Author: 2026-07-04 / Codex.

- Decision: Make bloom settings C++ owned, with no TypeScript UI in the first version.
  Rationale: Renderer state belongs in C++ under the existing ownership contracts. Browser UI controls can be added later once settings are stable.
  Date/Author: 2026-07-04 / Codex.

## Outcomes & Retrospective

Not yet implemented. This section must be updated after each milestone with what changed, what screenshots showed, and any remaining gaps.

## Contract and Quality Baseline

`OFG-BOOT-001 TypeScript Host Ownership` must be preserved. TypeScript may run smoke tooling and display screenshots, but it must not own bloom settings, post-effect scheduling, render targets, GPU resources, or draw submission.

`OFG-BOOT-002 C++ Runtime Ownership` changes by adding C++ renderer ownership of the intermediate target pool and bloom pass. `Game` should remain orchestration and status glue; renderer post-effect internals belong behind `Renderer`, `BloomPass`, `ToneMapPass`, and target-pool APIs.

`OFG-BOOT-004 Renderer Compatibility` changes because browser and native smoke must validate equivalent bloom behavior once it is enabled. The smoke visual contract should remain tone-map-aware and should add bloom-specific assertions only when deterministic bright pixels are available.

`OFG-BOOT-005 WebGPU Baseline` must be preserved. The first bloom implementation must request no optional GPU features and must not manually request higher adapter limits. It should use standard render attachments, sampled texture bindings, uniform buffers, full-screen render passes, and existing platform target formats.

`OFG-BOOT-006 Resource Lifetime` is central to this plan. Bloom pipelines, shader modules, bind group layouts, uniform buffers, reusable intermediate textures, texture views, and size-independent bind groups are durable resources. Ordinary steady-state frames may update uniform buffers and lease pool textures, but they must not recreate durable pass resources or allocate fresh textures after warm-up. Size changes may create new size-dependent textures and views.

`OFG-BOOT-009 Coverage` applies. Each modified implementation file must pass the default coverage attention gate, currently about 90% line coverage, unless this plan records an explicit exception with rationale. Browser-only visual confidence must still be backed by `npm run smoke:browser` and `npm run smoke:render`.

## Context and Orientation

The renderer is C++ owned. `Renderer::render_impl` in `C:\dev\ofg\cpp\src\render\renderer.cpp` resolves the current scene camera into `CameraProperties`, builds a transient `DrawList`, renders opaque geometry, and tone maps. The sky plan changes that into a scene pass that writes opaque geometry and procedural sky into an `RGBA16Float` scene color target, then tone maps that HDR color into the platform target.

The relevant current and planned files are:

- `C:\dev\ofg\cpp\include\ofg\render\scene_color_target.hpp` and `C:\dev\ofg\cpp\src\render\scene_color_target.cpp`: renderer-owned full-resolution HDR color target.
- `C:\dev\ofg\cpp\include\ofg\render\depth_target.hpp` and `C:\dev\ofg\cpp\src\render\depth_target.cpp`: renderer-owned full-resolution depth target.
- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp` and `C:\dev\ofg\cpp\src\render\tone_map_pass.cpp`: final HDR-to-platform pass using exposure, ACES-fitted tone mapping, and correct sRGB output encoding.
- `C:\dev\ofg\cpp\include\ofg\render\renderer_counters.hpp`: cumulative counters used to prove resource lifetime behavior.
- `C:\dev\ofg\docs\plans\procedural-sky-environment-plan.md`: active plan that adds HDR scene color, tone mapping, sky, `Environment`, and `Light`.
- `C:\dev\ofg\docs\plans\cascaded-shadow-maps-plan.md`: active plan that adds shadow map targets and shadow passes before opaque scene rendering.

Terms used in this plan:

Bloom means a post-process approximation of optical glow around very bright image regions. It does not cast light into the scene; it only affects the final image.

HDR scene color means linear color values in `RGBA16Float` before tone mapping. Values may exceed `1.0`.

Prefilter means extracting the part of HDR scene color that contributes to bloom. The first implementation should use a soft threshold so pixels near the threshold transition smoothly.

Bloom pyramid means a chain of reduced-resolution textures. Each level is half the width and height of the previous level, down to a small minimum or configured maximum count. Lower levels represent wider blur radii.

Upsample chain means reconstructing a bloom texture from the smallest pyramid level back toward the first bloom level by filtered upsampling and additive or lerped accumulation.

Intermediate target means a renderer-owned, reusable texture plus view that is used only inside a frame or pass sequence. It is not a scene resource and it is not directly exposed to TypeScript.

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

After the sky and shadow plans have landed, the intended renderer order is:

    build draw list
    resolve camera
    resolve environment and current sun light
    render shadow maps
    render opaque PBR into HDR scene color while sampling shadows
    render procedural sky into remaining HDR scene color pixels
    run BloomPass from HDR scene color into pooled bloom targets
    run ToneMapPass, adding optional bloom before exposure and ACES
    write platform target

Bloom must not run before sky, because the procedural sun and sky highlights are expected bloom sources. Bloom must not run after tone mapping, because LDR output has already compressed the high-intensity signal that bloom needs.

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
- `initial_downscale`: first bloom level size divisor. Use `2` for half resolution by default; allow `4` for cheaper quarter-resolution bloom.
- `max_levels`: maximum number of bloom pyramid levels.
- `min_level_extent`: stop building levels when either dimension would fall below this value.

Validation must reject non-finite values, negative threshold, negative knee, negative intensity, negative scatter, negative clamp, non-positive downscale, and `max_levels == 0`. `scatter` may be clamped to `[0, 1]` or rejected outside that range; choose one behavior and test it.

### Prefilter

The prefilter pass reads the full-resolution HDR scene color and writes bloom level 0 at half or quarter resolution. It should use `textureLoad` and manually average source pixels rather than relying on filtering. The first implementation should use max RGB component brightness:

    brightness = max(hdr.r, max(hdr.g, hdr.b))

Using max component preserves saturated colored bloom sources better than luminance-only extraction. A future setting can offer luminance extraction if art direction needs it.

Use a soft threshold:

    knee = threshold * soft_knee
    soft = clamp(brightness - threshold + knee, 0, 2 * knee)
    soft = soft * soft / max(4 * knee, epsilon)
    contribution = max(brightness - threshold, soft) / max(brightness, epsilon)
    bloom_source = min(hdr, vec3(clamp)) * clamp(contribution, 0, 1)

When `soft_knee == 0`, the helper should avoid division by zero and behave like a hard threshold.

### Downsample Pyramid

After prefiltering, each subsequent downsample level reads the previous level and writes the next smaller pooled target.

Use a stable weighted kernel. The preferred first kernel is the 13-tap bloom downsample pattern commonly used in modern bloom articles and production-inspired samples:

    a   b   c

      j   k

    d   e   f

      l   m

    g   h   i

Where `e` is the center, `a..i` are a 3x3 grid at two-source-texel spacing, and `j..m` are one-source-texel diagonal samples. The weights should sum to 1:

    e: 0.125
    a, c, g, i: 0.03125 each
    b, d, f, h: 0.0625 each
    j, k, l, m: 0.125 each

If implementation pressure is high, a 5-tap or 9-tap tent kernel can land first, but that downgrade must be recorded in the Decision Log with visual comparison screenshots.

### Upsample Chain

The upsample chain starts from the smallest bloom level and works back to level 0. Each pass reads the current lower-resolution bloom accumulation and the next higher-resolution downsample level, then writes a temporary higher-resolution accumulation target.

Use a 3x3 tent reconstruction kernel:

    1 2 1
    2 4 2
    1 2 1

Divide the weighted sum by 16. The shader can sample by manual bilinear reconstruction with `textureLoad`, or by integer-coordinate tent sampling in lower-level texel space. Avoid mandatory filterable float sampling in the first version.

Combine as:

    combined = higher_downsample + upsampled_lower * scatter

Clamp only if needed to avoid half-float overflow. Prefer preserving HDR energy and controlling final strength through `intensity`.

The `BloomPass` returns `BloomResult` containing:

- final bloom texture view;
- final bloom width and height;
- settings used for composite;
- whether the result is valid.

The final bloom result may be level 0 size, not full platform size. `ToneMapPass` can upsample it to platform pixel coordinates while compositing.

### Tone Map Composite

Extend `ToneMapPass` so it can composite optional bloom before exposure and ACES:

    hdr = textureLoad(scene_color, pixel, 0).rgb
    if bloom_enabled:
        bloom = sample_bloom_result_for_output_pixel(pixel)
        hdr += bloom * bloom_intensity * bloom_tint
    exposed = hdr * exposure
    mapped = aces_fitted(exposed)
    output = encode_for_platform(mapped)

If bloom is disabled, `ToneMapPass` should preserve the existing output path. Implementation options:

1. Keep two tone-map pipelines or bind group layouts: one without bloom, one with bloom.
2. Always bind a bloom texture and set intensity to zero, using a durable 1x1 black fallback texture when no bloom result exists.

Choose the simpler implementation that keeps resource counters stable. If using a fallback texture, it should be owned by the renderer or tone-map pass and created once during prepare.

### Intermediate Target Pool

Add an `IntermediateTargetPool` owned by `Renderer`. It is not a resource manager for game assets. It only owns device textures used by renderer passes.

A target descriptor should include:

    width
    height
    format
    usage
    mip_level_count
    array_layer_count
    sample_count

The first bloom targets should use:

    format = WGPUTextureFormat_RGBA16Float
    usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
    mip_level_count = 1
    array_layer_count = 1
    sample_count = 1

The pool should expose a movable, non-copyable lease:

    class IntermediateTargetLease {
        RenderTarget render_target() const;
        WGPUTextureView view() const noexcept;
        std::uint32_t width() const noexcept;
        std::uint32_t height() const noexcept;
        WGPUTextureFormat format() const noexcept;
    };

The pool should expose:

    class IntermediateTargetPool {
        explicit IntermediateTargetPool(GpuContext gpu);
        IntermediateTargetLease acquire(const IntermediateTargetDesc& desc, std::string_view debug_label);
        void release(IntermediateTargetLease lease) noexcept;
        void begin_frame() noexcept;
        void release_all() noexcept;
        RendererCounters counters() const noexcept;
    };

Rules:

- `acquire` may return an existing free target whose descriptor matches exactly.
- `acquire` creates a new target only when no matching free target exists.
- A target cannot be leased twice at the same time.
- A target may be reused later in the same frame only after the previous lease has been explicitly released and the command order no longer needs it as a source.
- `begin_frame` should assert or defensively release leaked leases in test builds if a previous render path failed to return them.
- `release_all` releases all WebGPU texture and view handles.
- Zero-size descriptors must be rejected with clear `EngineError`s; zero-size platform resize should skip bloom and not try to acquire targets.

Do not implement aliasing inside one texture or memory heap in the first version. WebGPU does not expose explicit memory aliasing in the same style as lower-level APIs, and descriptor-level texture reuse is enough for OFG's first post effects.

### Debug and Observability

Renderer counters should include pool-created textures/views and bloom-created shader modules, pipelines, bind group layouts, bind groups, buffers, and fallback textures. Existing counter names can be extended, but tests must avoid assuming exact global counts unless the plan records the count.

If useful for smoke and screenshots, add a native-only or test-only bloom debug output mode that writes:

- prefilter level 0;
- smallest pyramid level;
- final bloom accumulation;
- final tone-mapped output.

This debug mode should not expose renderer internals to TypeScript as mutable state.

## Plan of Work

Milestone 0 introduces the intermediate target pool without changing visuals.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\intermediate_target_pool.hpp`
- `C:\dev\ofg\cpp\src\render\intermediate_target_pool.cpp`
- `C:\dev\ofg\cpp\tests\intermediate_target_pool_test.cpp`

The pool should store target objects with exact descriptors, a live texture, a live view, and an in-use flag. It should create texture/view handles with labels that include the caller label plus dimensions where possible. It should preserve counters across releases in the same style as `SceneColorTarget` and `DepthTarget`. Tests should prove descriptor validation, same-descriptor reuse, different-descriptor allocation, explicit release behavior, `release_all`, move-only lease behavior, and no same-frame double lease of one target.

Milestone 1 adds CPU-side bloom settings, pyramid sizing, and uniform packing.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\bloom_settings.hpp`
- `C:\dev\ofg\cpp\src\render\bloom_settings.cpp`
- `C:\dev\ofg\cpp\tests\bloom_settings_test.cpp`

Define `BloomSettings`, `BloomPyramidLevel`, `BloomPyramidPlan`, and helper functions such as:

    BloomSettings default_bloom_settings() noexcept;
    void validate_bloom_settings(const BloomSettings& settings);
    BloomPyramidPlan build_bloom_pyramid_plan(std::uint32_t width, std::uint32_t height, const BloomSettings& settings);
    float bloom_prefilter_contribution(float brightness, float threshold, float soft_knee);

Tests should cover default values, invalid values, hard threshold behavior, soft-knee behavior, odd viewport dimensions, small viewports, max-level capping, half and quarter initial downscale, and deterministic pyramid dimensions.

Milestone 2 implements the prefilter and downsample side of `BloomPass`.

Create:

- `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp`
- `C:\dev\ofg\cpp\src\render\bloom_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\bloom_prefilter_downsample.wgsl.hpp`

`BloomPass::create` should create durable shader module, pipeline layout, bind group layout, render pipeline or pipelines, uniform buffer, and any size-independent state. `BloomPass::render` should take a command encoder, source HDR scene-color view, source width/height, `BloomSettings`, and `IntermediateTargetPool&`. It should build the pyramid plan, acquire pooled targets for each level, render prefilter into level 0, and render downsample levels 1..N. It should return a `BloomPyramid` or internal result object that holds leases until upsample/composite is complete.

If this milestone lands before the upsample chain, add a test-only or debug method to render a deterministic single prefilter/downsample pass and validate resource counters. No browser visual change is required yet.

Milestone 3 implements upsample, accumulation, and tone-map composite.

Create:

- `C:\dev\ofg\cpp\src\render\shaders\bloom_upsample.wgsl.hpp`

Update:

- `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp`
- `C:\dev\ofg\cpp\src\render\bloom_pass.cpp`
- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp`
- `C:\dev\ofg\cpp\src\render\tone_map_pass.cpp`
- `C:\dev\ofg\cpp\src\render\shaders\tone_map.wgsl.hpp`

The upsample chain should combine from the smallest level back to level 0 using a tent filter and `settings.scatter`. The final result lease remains live until after `ToneMapPass::render` encodes the final draw. `ToneMapPass` should add bloom before exposure and ACES. Tests should prove disabled bloom matches the old tone-map path, intensity zero matches disabled output, settings update through uniforms without recreating pipelines, and bind groups are recreated only when input views change.

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

`Renderer` should own `IntermediateTargetPool`, `BloomPass`, and `BloomSettings`. The first public interface can keep settings internal and defaulted. Do not expose bloom controls to TypeScript. `Renderer::render_impl` should skip bloom when the target is zero size, when settings are disabled, or when no valid scene color view exists. Renderer counters should prove that repeated ordinary frames do not create additional bloom or pool resources after the required warm-up allocations.

Visual validation should use the procedural sun once the sky plan has landed. If sky is not yet available when this plan is implemented, use native test fixtures or a temporary deterministic bright render fixture and record that limitation in Surprises & Discoveries; do not add fake game content solely to make bloom visible in the final demo.

## Concrete Steps

Run from `C:\dev\ofg`.

After Milestone 0:

    npm run test:cpp

Expected result: doctests pass for intermediate target descriptor validation, acquire/release behavior, reuse behavior, release-all behavior, and counters.

After Milestone 1:

    npm run test:cpp

Expected result: doctests pass for bloom settings validation, threshold helpers, and pyramid planning.

After Milestone 2:

    npm run test:cpp
    npm run build:wasm

Expected result: C++ tests pass, the WASM build succeeds, and bloom prefilter/downsample shader code compiles for the browser target.

After Milestone 3:

    npm run test:cpp
    npm run build

Expected result: C++ tests pass, TypeScript app build succeeds, and `ToneMapPass` still works with bloom disabled and enabled.

For visual verification during Milestone 4, keep a dev server available:

    npm run dev

Expected result: the command prints a local URL, normally `http://127.0.0.1:5173`, or the next available port. Report the URL in chat when started or restarted.

Run browser and native smoke:

    npm run smoke:browser
    npm run smoke:render

Expected result: browser and native smoke pass and write screenshots/reports under `C:\dev\ofg\artifacts\browser-smoke` and `C:\dev\ofg\artifacts\render-smoke`. The screenshots should show a controlled glow around the procedural sun or deterministic bright source without obvious full-screen haze.

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
- Bloom runs before tone mapping and is composited before exposure and ACES output mapping.
- Bloom can be disabled, and disabled or zero-intensity bloom preserves the existing tone-mapped visual path.
- Bright HDR pixels above the configured threshold produce a soft halo whose strength changes with `intensity`.
- `scatter` changes the apparent bloom radius without changing the original scene lighting.
- Bloom extraction uses a soft threshold and a clamp that affect only the bloom source, not the original scene color.
- Bloom intermediate targets are leased from `IntermediateTargetPool`, not owned as ad hoc one-off textures by every pass.
- Renderer counters prove steady-state frames do not recreate bloom pipelines, shader modules, bind group layouts, uniform buffers, or intermediate textures after warm-up.
- Resize creates or reuses only size-dependent targets; repeated same-size frames reuse existing targets.
- Browser and native smoke both render through the same bloom-capable C++ path.
- The first implementation requests no optional WebGPU features and no custom adapter limits.

Test acceptance:

- `npm run test:cpp` passes after each C++ milestone.
- `npm run test:ts` passes if TypeScript smoke/debug code changes.
- `npm test` passes before completion.
- `npm run build` passes before browser visual validation.
- `npm run smoke:browser` passes and stores a screenshot showing the bloom-capable scene.
- `npm run smoke:render` passes and writes a PNG/report showing the bloom-capable scene.
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

The work should be additive and recoverable. `Renderer::release` must release `BloomPass`, `IntermediateTargetPool`, tone-map bloom fallback resources, and any pooled textures even if preparation failed after only some resources were created. Repeated `Renderer::prepare` after ready must not create duplicate bloom resources. Repeated render at the same target size must reuse the same pool textures after warm-up. Zero-size resize must not attempt bloom target allocation.

If `IntermediateTargetPool` exposes lifetime bugs, keep it unused by `Renderer` until tests pass. Bloom should not land with ad hoc per-pass scratch textures as a workaround unless this ExecPlan is explicitly revised.

If bloom shaders fail validation in browser but native tests pass, disable bloom by default through `BloomSettings` while retaining pool and settings tests, then isolate the WGSL incompatibility. Do not mark the visual milestone complete until browser and native smoke both pass with the intended shader path.

If bloom output is too strong or hazy, tune `intensity`, `threshold`, and `scatter` before changing sky brightness or material albedo. The scene lighting should remain physically meaningful; bloom should adapt to it.

If the pyramid implementation proves too expensive, first reduce `max_levels` or use `initial_downscale = 4`. If still too expensive, record a decision to switch the first version to a single lower-resolution separable blur and include visual/performance evidence.

## Artifacts and Notes

No bloom screenshots or local artifacts yet.

Current intended pass graph:

    ShadowCasterPass -> ShadowMapTarget
    OpaquePass + SkyPass -> SceneColorTarget RGBA16Float
    BloomPass -> IntermediateTargetPool RGBA16Float pyramid targets
    ToneMapPass(scene_color, optional bloom) -> platform target

Memory estimate for a 1920x1080 viewport with half-resolution bloom level 0 and six `RGBA16Float` levels:

    Level 0: 960 x 540 x 8 bytes  ~= 3.96 MiB
    Level 1: 480 x 270 x 8 bytes  ~= 0.99 MiB
    Level 2: 240 x 135 x 8 bytes  ~= 0.25 MiB
    Level 3: 120 x 67  x 8 bytes  ~= 0.06 MiB
    Level 4: 60  x 33  x 8 bytes  ~= 0.02 MiB
    Level 5: 30  x 16  x 8 bytes  ~= 0.004 MiB

The downsample chain therefore costs about 5.3 MiB for one set of levels at 1080p. Upsample accumulation may need additional ping-pong targets, but the pool should reuse released levels so peak memory stays bounded and visible in counters.

## Interfaces and Dependencies

Expected new or changed public interfaces by the end:

- `C:\dev\ofg\cpp\include\ofg\render\intermediate_target_pool.hpp`
  - `struct IntermediateTargetDesc`
  - `class IntermediateTargetLease`
  - `class IntermediateTargetPool`
  - Descriptor validation helper.
  - `IntermediateTargetPool::acquire(...)`, `release(...)`, `begin_frame()`, `release_all()`, and `counters()`.

- `C:\dev\ofg\cpp\include\ofg\render\bloom_settings.hpp`
  - `struct BloomSettings`
  - `struct BloomPyramidLevel`
  - `struct BloomPyramidPlan`
  - `default_bloom_settings()`
  - `validate_bloom_settings(...)`
  - `build_bloom_pyramid_plan(...)`
  - `bloom_prefilter_contribution(...)`

- `C:\dev\ofg\cpp\include\ofg\render\bloom_pass.hpp`
  - `class BloomPass`
  - `struct BloomResult`
  - `static std::unique_ptr<BloomPass> BloomPass::create(GpuContext gpu, WGPUTextureFormat bloom_format)`
  - `BloomResult BloomPass::render(WGPUCommandEncoder encoder, WGPUTextureView scene_color_view, std::uint32_t width, std::uint32_t height, const BloomSettings& settings, IntermediateTargetPool& pool)`
  - `RendererCounters BloomPass::counters() const noexcept`

- `C:\dev\ofg\cpp\include\ofg\render\tone_map_pass.hpp`
  - Add a compact bloom input type, for example `ToneMapBloomInput`, carrying a texture view, width, height, intensity, and tint.
  - Extend `ToneMapPass::render(...)` to accept optional bloom input, or add a sibling `render_with_bloom(...)` while keeping the old path for disabled bloom.
  - Counters must include any new bloom bind groups or fallback texture resources.

- `C:\dev\ofg\cpp\include\ofg\render\renderer.hpp`
  - Renderer owns `std::unique_ptr<IntermediateTargetPool>`.
  - Renderer owns `std::unique_ptr<BloomPass>`.
  - Renderer owns `BloomSettings`.
  - Renderer counters aggregate pool and bloom pass counters.

- `C:\dev\ofg\cpp\src\render\shaders\bloom_prefilter_downsample.wgsl.hpp`
  - WGSL source for prefilter and downsample full-screen passes.

- `C:\dev\ofg\cpp\src\render\shaders\bloom_upsample.wgsl.hpp`
  - WGSL source for upsample/accumulate full-screen passes.

- `C:\dev\ofg\cpp\src\render\shaders\tone_map.wgsl.hpp`
  - Updated WGSL source that composites optional bloom before exposure and ACES tone mapping.

- `C:\dev\ofg\cpp\CMakeLists.txt`
  - Add new source and doctest files.

The first implementation must not add a third-party engine, renderer, or runtime dependency.

