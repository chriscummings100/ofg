# Add histogram exposure, bloom and screen-space ambient occlusion

This ExecPlan follows [PLANS.md](../../PLANS.md). Status: proposed, 2026-10-04. The user requested planning
and feedback for bloom, histogram auto exposure and SSAO. Discovery and this specification are authorized;
rendering implementation has not started. Recommendations below remain reviewable design choices.

## Purpose / Big Picture

Make the laboratory readable across bright skies, shaded surfaces and dark scenes, add controlled glow around
bright sources, and make nearby surfaces and contacts easier to distinguish. Expose each effect independently
in Render Settings, with diagnostic views that explain the result. Deliver the same required behavior on native
D3D12 and browser WebGPU, with measured costs and preserved manual inspection modes.

Implement in this order: establish the HDR/exposure contract, histogram exposure, bloom, then SSAO. With SSAO enabled,
frame order is depth/normal prepass, AO/filtering, forward lighting sampling AO, metering, bloom and tone mapping.

## Progress

- [x] (2026-10-04) Inspected current HDR/output, outdoor exposure, IBL generations, material bindings and UI settings.
- [x] (2026-10-04) Wrote the proposed design, milestones, validation and explicit first-version limitations.
- [x] (2026-10-04 16:10Z) Revised per user direction: remove outdoor exposure override and the imposed 16-texture profile. Chrome adapter probe and offscreen draws passed with 17 and 48 sampled textures.
- [ ] Implement and verify the exposure contract and portable resource probes.
- [ ] Implement histogram metering, temporal adaptation and exposure diagnostics.
- [ ] Implement bloom and its visual controls.
- [ ] Implement SSAO inputs, filtering and indirect-diffuse composition.
- [ ] Complete combined native/browser validation, cost measurements and documentation.

## Surprises & Discoveries

The working tree already contains substantial outdoor-lighting changes. This plan is based on those current files,
including uncommitted work; preserve them. The application was not rebuilt or tested for this planning task;
a standalone Chrome WebGPU capability/draw probe was run as recorded below.

`src/render/graphics.cpp` currently renders lit opaque/masked objects into RGBA16Float HDR, tone maps, then draws
unlit opaque objects and sorted transparency into display-linear color. ImGui follows scene rendering. The scene
depth allocation is D32Float with DepthStencil usage only, so it cannot simply be sampled by a new AO shader.

Outdoor exposure is already applied before HDR storage. `outdoorExposureMultiplier` uses a sun-height curve or
manual EV, and the sky clamps values to finite float16. This is not histogram metering. Dynamic IBL generations
also carry exposure compensation. Reading HDR as if it were unscaled radiance would create incorrect metering.

The maximum PBR variant currently uses 16 sampled textures, the historical browser profile described in
[outdoor lighting](../outdoor-lighting.md). The user removed that imposed budget. The pinned RHI already requests
adapter maximum limits in `wgpu-device.cpp`; `tools/outdoor-smoke.mjs` explicitly overrides the requested texture
and sampler limits to 16. `src/resources/pbr-material.cpp` also rejects more than 12 material textures before any
device check. Remove that fixed material-count restriction and update its header contract/tests during implementation.

Chrome 154.0.8037.95 selected Intel gen-12lp: 48 sampled textures, 16 samplers, 16 storage buffers and 8 storage
textures per shader stage. The probe requested those limits and verified offscreen draws/readback at 16, 17 and
48 texture bindings with no validation errors. Default device creation still gives only 16 sampled textures.
This is actual browser evidence, not a native D3D12 limit claim or a test of OFG's future Slang AO shader.

## Decision Log

2026-10-04, proposed: keep the existing forward renderer, concrete RHI passes and current tone mapper. No render
graph, deferred material system, new engine dependency, TAA or motion-vector requirement is justified by this scope.

2026-10-04, user revision: remove the outdoor sun-height exposure override, its separate mode/controls and its
exposure-derived IBL compensation. One Manual/Histogram camera exposure system serves studio and outdoor scenes.
Begin with unexposed scene-linear HDR and enough storage range; do not retain the sun-height curve under the name
of numerical scaling. Manual exposure remains available for image comparisons.

2026-10-04, proposed: bloom uses a filtered downsample/upsample pyramid; SSAO starts with a deterministic hemisphere
kernel and spatial edge-aware filtering. A GTAO replacement remains a quality follow-up if measured artifacts
justify it. Avoid a temporal AO implementation before the application has the necessary history infrastructure.

2026-10-04, proposed: preserve late unlit/transparency composition in this first version. Those surfaces neither
contribute to the histogram nor emit bloom nor receive SSAO. Lit transparency must use the same resolved camera
exposure as opaque lighting. Moving all world transparency into HDR is a separate change to the existing material
contract, desirable later for glowing transparent particles and glass.

2026-10-04, user revision and probe result: use actual adapter/device limits instead of a fixed 16-texture ceiling.
Replace the proposed diffuse-split MRT workaround with a depth/normal prepass, AO generation and direct AO sampling
inside forward PBR. The previously allowed maximum variant plus AO needs 17 sampled textures, below this browser's verified 48.
Use texture loads or an existing compatible sampler for AO; the adapter's 16-sampler ceiling still applies.

## Outcomes & Retrospective

The revised plan removes both imposed constraints. A browser draw/readback probe establishes room for direct AO
sampling. The remaining integration risks are preserving unexposed HDR range and matching the depth/normal prepass
to forward geometry. No effect implementation, visual-quality or performance result is claimed here.

## Context and Orientation

| Existing location | Role in this work |
| --- | --- |
| `src/render/graphics.cpp` | HDR/depth ownership, material variants, draw ordering and output passes |
| `shaders/pbr/output.slang` | Existing exposure, PBR Neutral tone mapping and transfer encoding |
| `shaders/pbr/lighting.slang`, `mesh.slang` | Apply AO to indirect diffuse; preserve layered BRDF and alpha masking |
| `src/render/sky-renderer.*`, `environment-renderer.*` | Visible sky and captured IBL storage scaling |
| `src/scene/lighting.h`, `outdoor-lighting.*` | Existing CPU settings and outdoor EV behavior |
| `src/lab/render-settings.cpp`, `src/ui/render-settings-panel.*` | Atomic validation, edits, reset and diagnostics |
| `tests/pbr-core-test.cpp`, `outdoor-rendering-test.cpp` | Existing numerical and GPU regressions |
| `tools/pbr-shader-check.mjs`, `outdoor-smoke.mjs` | Shader portability and real browser rendering checks |

See [PBR contracts](../pbr.md), [outdoor lighting](../outdoor-lighting.md), [architecture](../architecture.md) and
[development commands](../../DEVELOPING.md). SSAO estimates local ambient visibility from visible depth; it cannot
recover offscreen or hidden geometry and does not replace shadows or global illumination.

## Contract and Quality Baseline

### Exposure and color

Use unexposed linear RGB throughout scene HDR: `H = L`, where L is scene radiance in the renderer's working units.
Meter H, then apply the single camera exposure E before tone mapping. No outdoor exposure or sun-height gain modifies
scene radiance. Studio assets use relative working units; do not claim absolute photometric calibration for them.

Start with RGBA32Float scene HDR and load-based fullscreen reads. Removing exposure while retaining float16 can
overflow the physical solar disk, so audit sky LUTs, aerial perspective, environment capture/filter intermediates
and scene output together. Remove obsolete 65504 clamps where storage is widened; keep intentional physical limits
separate from format limits. Test brightest permitted sun, tiny highlights, night and abrupt changes for finite values
and preserved radiance. Measure the bandwidth/memory cost before considering a shared camera pre-exposure scheme as
a later optimization. Such a scheme must not reintroduce outdoor-specific brightness control.

Keep environment generations in scene radiance units, with no capture/current EV compensation. Camera exposure alone
must not regenerate or recolor IBL. Removing the current sun-height ratio exposes stale bright IBL during abrupt edits;
resolve that as an environment publication issue, not hidden exposure correction. Initially invalidate old IBL on an
explicit discontinuous time/lighting preset edit, render direct light and sky while replacement completes, and prohibit
an obsolete in-flight generation from republishing. Preserve bounded capture work and ordinary continuous updates.
Test and document the temporary loss of indirect light; do not claim exposure adaptation fixes stale lighting.

Provide Manual and Histogram modes for every scene. Remove outdoor automatic/day/twilight/night/manual EV settings
and their UI/tests; migrate useful fixture exposure presets to the shared camera settings explicitly. Exposure
compensation in stops multiplies E by `2^compensation`. Debug material/shadow/AO views bypass exposure, bloom and tone mapping as appropriate and
freeze histogram adaptation; leaving debug mode resumes from valid history. Encode display transfer once. UI is
composited afterward and is never a metering source.

### Histogram and temporal feedback

Start with 256 log2-luminance bins and a fixed, documented range, provisionally [-16, +20] stops of working luminance.
Use linear Rec.709 luminance weights. Clamp finite negative RGB to zero for metering; count/reject nonfinite samples.
Keep zero/near-black samples in a separate count. Clamp out-of-range positive values into end bins and expose those
counts so a poor histogram range is visible. Meter the AO-composited scene and sky before bloom or tone mapping.

Start with one deterministic sample per 2x2 block; weight border blocks by their actual pixel coverage. This is
subsampling, not averaging RGB before taking its logarithm. Use a full-resolution diagnostic reference to quantify
missed thin sources. Initialize every bin each frame, accumulate integer counts in compute, then reduce a trimmed
log mean. Start with 2nd-to-98th percentile retention; partially occupied boundary bins contribute partial weights.
These are tuning defaults, not established optimal values.

Target exposure is `Etarget = 0.18 / exp2(meanLogLuminance)`, followed by compensation and configured exposure limits.
Label 0.18 as a working middle-gray target, not a display pixel value. Smooth in log2 exposure with
`xNext = x + (xTarget - x) * (1 - exp(-dt / tau))`. Use separate time constants for increasing scene brightness
(lowering exposure) and decreasing brightness (raising exposure); provisional values are 0.3 s and 1.0 s.

Define finite bounds, percentile ordering and positive time constants through CPU settings validation. Empty/all-black
or all-invalid histograms retain the last valid exposure; startup uses a deterministic fixture seed. Zero dt holds
history. Cap adaptation dt after suspension, provisionally at 0.1 s. Resize preserves history; fixture replacement,
explicit reset and device recreation initialize it. Camera cuts get an explicit reset hook; normal fly movement adapts.
Mode switches seed from the current effective exposure where possible to avoid an artificial flash.

The histogram/reduction/exposure buffer stay on the GPU. Dispatches and output draws execute in order on the graphics
queue; no same-frame CPU readback drives rendering. Ping-pong exposure buffers have explicit previous/next ownership.
Diagnostics use a bounded asynchronous staging ring at a low rate, provisionally 5 Hz, skipping collection when busy.
The UI displays sample age. A delayed diagnostic readback must never stall or alter adaptation.

### Bloom

Extract highlights in exposed linear HDR (`H * E`), with a soft knee around a user-facing threshold. This makes the
threshold track visible brightness. Start at half viewport resolution and build up to six progressively smaller
RGBA16Float levels, stopping at 1x1. Reduce in float32 and define a finite bloom working-range clamp before fp16 storage;
count clipping and validate bright solar impulses. This bloom-only range limit must not clip scene HDR or its histogram.
Define odd-size reduction footprints and clamp borders; normalize reconstruction
weights so adding a pyramid level does not arbitrarily increase gain. Keep downsample inputs and upsample outputs
in distinct allocations/subresources permitted by both backends; never sample an attachment being written.

Composite `exposedScene + intensity * bloom` before the existing tone mapper. The histogram excludes bloom, preventing
glow from reducing its own exposure. Controls: enabled, intensity, threshold, soft knee and scatter radius. Debug views:
highlight extraction, selected pyramid level and bloom-only. Intensity zero reproduces the baseline. Lens dirt, streaks,
chromatic aberration and lens flares are outside this scope. Thresholded additive bloom is an artistic approximation.

### SSAO and lighting composition

Allocate sampleable depth and prove D32Float depth loads on both backends before building AO. Retain the application's
current left-handed view convention, zero-to-one depth, near/far values and projection. Reconstruct view positions
using the actual inverse projection; verify Y orientation and near/far reconstruction with fixtures rather than guessed
formulas. Do not change reversed-depth policy or introduce MSAA as part of this task.

When AO is enabled, render an opaque/masked depth prepass plus encoded geometric view normals and a validity flag
in RGBA8Unorm. Clear validity for background. Include opaque unlit geometry as occluders even though those surfaces
do not receive AO. Reuse prepared meshes and matching alpha-mask evaluation; preserve UV transforms, cutoffs,
double-sided orientation, mirrored winding, culling and any supported deformation. Geometric normals avoid interpreting
material normal-map detail as unrepresented geometry. Generate AO before forward shading and preserve/load prepass
depth with an explicitly tested compatible depth comparison. No sampled depth attachment may be writable in that pass.

Add one AO texture to the PBR frame bindings. Sample full-resolution filtered visibility with pixel coordinates in
opaque/masked lighting; skip it for blended/unlit draws. Apply visibility to the indirect diffuse contribution before
layer composition and aerial perspective, retaining existing Fresnel, metallic, coat and sheen weights. Direct light,
emission, indirect specular and atmospheric in-scattering bypass SSAO. Visibility=1 must reproduce existing lighting
within numerical tolerance. Authored material AO retains its existing behavior; overlapping baked AO may still
over-darken the same feature, so include that case in tuning fixtures. No separated diffuse color target is needed.

Start with a fixed 16-sample oriented hemisphere at half resolution, world-space radius (provisionally 0.5 m), a small
distance bias and range falloff. Choose foreground depth/normal consistently when reducing 2x2 blocks. Background,
invalid or offscreen samples contribute no occlusion; handle near-plane projections explicitly. Use deterministic
spatial rotation without frame-to-frame jitter. Apply a small depth/normal-aware spatial filter and bilateral upsample
to avoid spreading contact darkness across silhouettes. Retain a full-resolution/high-sample diagnostic reference.

Controls: enabled, radius in metres, bias, strength and quality. Views: linear depth, normals, raw AO, filtered AO and
AO-only composition. Neutral visibility is 1. Direct lights, emission, sky, fog in-scattering and specular reflections
are unaffected at fixed exposure. Auto exposure can subsequently change the whole image in response to AO, so isolate
lighting assertions with manual exposure. Specular occlusion, bent normals and temporal accumulation are deferred.

### Ownership and resource limits

Graphics owns one concrete post-processing helper; scene settings are copyable CPU values, and exposure history belongs
to the rendered view, not an asset. The current single-view application needs no view registry. Proposed files are
`src/render/post-processing.{h,cpp}`, `src/scene/post-processing.{h,cpp}` and `shaders/post/` for the three effects.
Keep pure histogram/adaptation reference math independently testable; factor effect helpers only if responsibilities
outgrow a readable concrete implementation. Reuse existing shader compilation/pass helpers where they fit.

Skip disabled effect passes and release their optional allocations safely. Recreate size-dependent targets on viewport
pixel-size changes, including DPI/docking changes, without exposing partially initialized targets. Submitted RHI work
must retain replaced resources until completion; device teardown drains/releases using the existing host contract.
Minimized/zero-sized views perform no dispatch and accumulate no unbounded dt.

Query and record adapter limits and the limits actually granted to OFG's device on each host. Remove forced 16-texture
requests/assertions in normal smoke tests and the CPU-side 12-material-texture cap. Validate reflected resource counts
against the active device when creating the pipeline, keeping CPU material validation device-independent. Retain actual sampler,
binding, storage and format limits. Do not replace 16 with an unexplained hard-coded 48 or assume native/browser select
the same GPU. Smaller adapters may fail explicitly at the required operation; no alternate renderer is required now.
More texture slots do not guarantee all material maps fit simultaneously: independently declared sampler bindings also
count even if assigned the same sampler object. Exercise newly accepted combinations and report genuine sampler-limit
failures clearly. Consolidate equivalent shader sampler bindings only where actual material semantics permit it;
do not assume enabling all 15 material maps is already proved by the texture-only probe.

Prove storage-buffer integer atomics, compute-to-render ordering and bounded asynchronous readback in the pinned RHI
on both hosts. Query/request optional features only when an operation needs them; RGBA32Float texture loads do not
require filtering. The probe reports float32 filtering/blending and timestamps available, but OFG must prove their
enabled-device and RHI paths if used. Require neither float atomics nor subgroups. A capability failure must identify
the operation; do not silently disable an advertised effect. Track live payload bytes and pending retirement separately;
label payload accounting honestly when driver allocation sizes are unavailable. Timing queries are optional diagnostics,
with unavailable GPU timing explicitly reported instead of relabeling CPU submission time.

## Plan of Work

### Milestone 1: establish output contracts and portable inputs

Capture existing studio/outdoor images and material/transparency behavior. Remove outdoor exposure override/settings
and EV-derived IBL compensation; introduce shared validated camera settings and unexposed HDR storage. Preserve comparable
manual images by explicitly setting the same final camera exposure, not retaining a hidden outdoor gain. Query actual
native/browser capabilities and remove smoke-test device clamps. Prove HDR range, minimal histogram atomics/readback,
sampleable AO depth/normals and material-plus-AO bindings on D3D12 and the actual browser device. Establish late lit
transparency's access to resolved GPU exposure via a storage buffer and validate fragment-stage bindings. Update the
shader resource allowlist deliberately. Reject unsupported combinations with actionable diagnostics.

Visible result: studio and outdoor share one camera exposure system; manual comparisons and debug modes remain useful.
Proof: radiance invariance under camera exposure changes, native pixels, browser shader/runtime checks and revised
IBL-generation regressions including discontinuous-edit invalidation. Resolve any required
RHI adaptation here, narrowly and without modifying the pinned submodule.

### Milestone 2: histogram auto exposure and feedback

Implement clear/build/reduce/adapt passes and output consumption, then expose the histogram plot, clipping counts,
trim percentiles, target/current exposure, adaptation state and diagnostic age. Add bright/dark panels,
an emissive patch and a sheltered region to an original procedural inspection fixture. Reuse the outdoor clock.

Visible result: moving between bright and dark regions adapts smoothly; changing bloom/UI cannot change the histogram.
Proof: CPU reference cases, integer GPU histogram counts, adaptation sequences and native/browser day/night/camera tests.

### Milestone 3: bloom

Implement the pyramid and pre-tone-map composition, with diagnostic level selection and independent toggle. Establish
manual-exposure screenshots first, then test with histogram exposure. Include tiny highlights, emissive shapes, the sun,
odd dimensions and a bright source crossing the frame edge.

Visible result: tunable glow without hard threshold popping, UI glow or exposure feedback from bloom. Proof: zero/constant/
impulse inputs, normalization, finite HDR, smooth threshold sweeps and inspected captures/motion on both backends.

### Milestone 4: SSAO

Implement the matching depth/normal prepass, AO/filter/upsample and forward PBR sampling. Add corners, sphere/ground
contacts, separated planes, thin masked cards, emissive objects and metallic controls to the fixture. Meter after AO.

Visible result: local indirect contact shading without shadowing the sky, direct-only surfaces or emission. Proof:
neutral-AO identity, projection tests, isolated indirect/direct/emissive fixtures, silhouette and camera-motion
inspection, maximum material bindings and native/browser rendering. Record hemisphere SSAO quality limits before
considering a GTAO algorithm change.

### Milestone 5: integration and cost review

Exercise all toggles and combinations, debug transitions, exposure modes, UI docking/DPI resize, reset, hidden browser
tabs, teardown and restart. Measure each effect and combined rendering with exposure frozen, then separately inspect
adaptation in motion. Update DEVELOPING.md and rendering contracts; present representative native/browser images.
No production defaults or performance claim is accepted solely from attractive still images.

## Concrete Steps

Work in `C:\dev\ofg`. At implementation time use [build-native](../../.agents/skills/build-native/SKILL.md) and
[build-web](../../.agents/skills/build-web/SKILL.md) for toolchain setup. Existing repository commands, not run by this
planning task, are:

```powershell
cmake --preset cpu-tests
cmake --build --preset cpu-tests
ctest --preset cpu-tests --output-on-failure
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
cmake --preset web
cmake --build --preset web
node tools/pbr-shader-check.mjs
npm.cmd run smoke:web
npm.cmd run smoke:pbr
npm.cmd run smoke:ui
npm.cmd run smoke:outdoor
```

Add proposed `tests/post-processing-test.cpp` to the CPU suite, `tests/post-processing-rendering-test.cpp` to native GPU
CTest, and `tools/post-processing-smoke.mjs` to browser tooling. These files/commands do not exist yet. Extend shader
validation to all new entry points. Resolve and record exact test selectors and smoke commands during implementation,
including any new fixture switches; do not advertise proposed command-line flags as runnable.

## Validation and Acceptance

| Area | Required evidence |
| --- | --- |
| Histogram | Exact integer counts for constant, mixed, black, range-edge and odd-sized inputs; correct percentile boundaries; invalid values handled explicitly |
| Exposure | Camera exposure edits leave scene HDR/metering unchanged; outdoor time alone never overrides E; monotonic convergence; no overshoot; equivalent 30/60/120 Hz sequences for constant targets |
| Lifecycle | Deterministic startup/reset, bounded resume dt, preserved resize history, no late readback publication after teardown |
| Bloom | Black stays black, intensity zero is identity, constant-field normalized filtering, symmetric impulse away from borders, continuous knee, finite results |
| SSAO | Neutral visibility reconstructs lighting; flat plane nearly unoccluded; contacts darker than separated controls; direct/emission/specular unchanged at fixed E |
| Portability | Recorded granted limits, real compute/depth/prepass and maximum material-plus-AO bindings on each backend; no validation errors or synchronous readback in frame rendering |
| Composition | UI/debug views unaffected, correct transfer encoding, late lit transparency shares E, histogram independent of bloom toggle |
| Resources/cost | Repeated resize/toggle returns to bounded live resources after retirement; named adapter/backend/viewport/workload and method for timing |

Use exact assertions for integer bins and state transitions. Proposed tolerances: CPU normalized math 1e-5; GPU exposure
within one histogram bin of the analytic target (36/256 = 0.140625 stops for the proposed range), with a tighter CPU
reference comparison for identical bins; fp32 scene results within 1e-5 absolute plus 1e-5 relative for small analytic
fixtures, fp16 normalized bloom intermediates within 2e-3 absolute plus 2e-3 relative; final
UNORM comparisons within two code values away from raster boundaries. Derive tighter case-specific bounds for exactly
representable fixtures. AO image thresholds must be calibrated against the analytic flat plane and fixed geometry
before acceptance, not weakened after failures. Do not require identical native/browser pixels.

Save screenshots, short transition sequences, console logs, feature reports and measurements under
`artifacts/post-processing/{native,browser}`. Benchmark at 1280x720 and 1920x1080 after warm-up, with named adapter,
driver/browser, build configuration and fixture. Report median/p95 over a recorded sample count; separate GPU timestamp
results from CPU-plus-queue-wait or browser callback intervals. Establish budgets from the measured baseline before
final quality tuning; this plan makes no unmeasured millisecond promise.

Coverage policy: exercise every CPU validation/reset/empty-input branch and each numerical boundary listed above.
GPU bindings, ordering, format support and visuals require integration evidence. No numeric line-coverage percentage
is proposed without an installed measurement workflow; record this exclusion, and do not import a historical threshold.

## Milestone Review

Planning review, revised 2026-10-04: checked current renderer contracts, real browser limits, exposure feedback,
transparency scope, prepass consistency, atmospheric composition, ownership and verification requirements. Removed
the diffuse-split workaround and sun-height exposure mode following user direction. Browser texture-limit draws pass;
effect quality, performance, native limits, RHI compute/readback behavior and new layouts remain unverified gates.
At each implementation milestone review correctness, comments, ownership, API boundaries, obsolete paths and docs,
then record actual commands and evidence here before marking progress complete.

Documentation validation, 2026-10-04: local Markdown links across this plan and the updated bootstrap plan resolve.
The bootstrap diff passes `git diff --check`; the new plan was checked directly for trailing whitespace and conflict
markers. The standalone capability probe is separate from application validation; no graphics build was performed.

## Idempotence and Recovery

Preserve existing dirty files and historical reference trees. Build native/browser in their established separate
directories. Effect reset invalidates exposure history/readback generations without deleting scene assets. Repeated
enable/disable and resize must be safe while commands are in flight. On shader/resource failure retain clear error
reporting and release partial allocations; do not conceal the failure with a different algorithm. Keep manual exposure
and effect toggles available for comparisons. Archive this plan only when implementation acceptance is complete.

## Artifacts and Notes

Machine probe, 2026-10-04 16:10Z: `node artifacts/post-processing/capabilities/probe.mjs` passed in Chrome
154.0.8037.95 on Intel gen-12lp. `artifacts/post-processing/capabilities/report.json` records adapter/default/granted
limits and features. Fragment pipelines with 16, 17 and 48 live texture bindings rendered the expected RGBA
(64,128,192,255) pixel with exact readback and no validation errors. Bindings shared one source image; this proves
layout/draw support, not memory residency or performance with 48 distinct assets. Samplers remain limited to 16;
color attachments are 8 with 128 bytes/sample on the requested device. Native capabilities remain to be queried.
An initial probe attempted two devices from one consumed adapter and failed; retrying with a fresh adapter succeeded.

Revision, 2026-10-04: user removed the outdoor exposure override and self-imposed texture budget. The implementation
plan now uses shared Manual/Histogram exposure, scene-linear fp32 HDR, actual granted device limits and prepass SSAO.
Existing implementation documentation remains a description of the current renderer until those changes land.

Source inspection and external reference review occurred on 2026-10-04. The WGSL specification defines integer atomic
operations in workgroup/storage address spaces; their use through OFG's pinned Slang/RHI still needs the proposed
runtime probe. See [WGSL atomics](https://www.w3.org/TR/WGSL/#atomic-types).

[Practical Real-Time Strategies for Accurate Indirect Occlusion](https://research.activision.com/publications/2020-03/practical-real-time-strategies-for-accurate-indirect-occlusion)
and Intel's [XeGTAO](https://github.com/GameTechDev/XeGTAO) are primary references for a possible GTAO quality follow-up,
not dependencies or evidence of performance in OFG. Record exact source revisions and preserve licenses before adapting
code. The current design choices and defaults above are OFG proposals, not claims that these references prescribe them.

## Interfaces and Dependencies

Retain Slang RHI `16324a68af477baaede620e713644f5e9613b1a2` and ImGui
`b48d1afbe8ee8b238e2961dc363a949dd7304e23` unless a demonstrated blocker requires a separate dependency decision.
No new runtime package is proposed. Shared C++/Slang owns effect behavior; hosts retain event loops, asset loading,
canvas/window presentation and shutdown. Existing CPU-only tests must remain independent of graphics dependencies.
