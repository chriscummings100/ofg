# Integrate Khronos-based PBR into OFG's shared renderer

This ExecPlan follows [PLANS.md](../../PLANS.md). Status: **core/surface implementation delivered; extended validation remains open**. On 2026-10-03 the user authorized the revised sphere-grid/core-and-surface PBR plan. Deferred follow-ups A-C remain outside this implementation scope. Re-read this plan after context compaction during future authorized implementation and keep its living sections current.

## Purpose / Big Picture

Build a solid glTF metallic/roughness PBR foundation through shared C++/Slang on native D3D12 and browser WebGPU. The primary test system is a **reviewable grid of spheres with different materials**, lit by punctual lights and a filtered environment. Basic debug fly-camera controls arrive early so we can inspect highlights, texture mapping and material response from different distances and angles. The sphere grid remains the main review scene as surface extensions are added.

Use a close port of the reference's mathematical modules, adapted to OFG ownership and platform contracts. An uber shader means one maintainable source family specialized into the resource layouts actually needed by each material. It does not mean one binary containing every texture or an unlimited material-combination guarantee. Define and report the supported device/material profile explicitly.

The first acceptance checkpoint is the sphere grid with fly-camera inspection, textured metallic/roughness shading, correct normals, punctual lights, IBL, HDR/exposure/tone mapping, unlit/emission and reliable resource ownership on both targets. IOR, specular, clearcoat, sheen, iridescence, anisotropy and material variants build on that foundation in subsequent surface-extension slices. Texture transforms and ordinary alpha coverage/blending remain part of the foundation; alpha blending does not require optical transmission.

**Transmission, volume absorption, dispersion and scattering are deferred follow-up work and do not block foundation acceptance or completion of milestones 0–5.** Diffuse transmission, retroreflection and archived specular/glossiness are also later scope. Preserve their research below without adding speculative passes/resources for them now. Shadows, ambient-occlusion generation, SSR, bloom, temporal antialiasing, many-light clustering, full animation and general glTF conformance are not implied by this PBR milestone.

## Progress

- [x] (2026-10-04) User-requested naming cleanup: replaced numbered texture defines, bindings, uniforms and UV helpers with material-property names; updated the explicit CPU mapping, enum-based fixture/test assignments and documentation. Native CTest, WGSL checks and browser PBR smoke pass; both fixed-pose renders are pixel-identical to their saved pre-change images.

- [x] (2026-10-03) User authorized implementation of milestones 0-5; inspected the completed texture foundation and preserved its uncommitted changes.

- [x] (2026-10-03) Downloaded Sample Renderer revision `cc27919cacbb235d2f58a0c0203387efce9375f8` into `artifacts/reference/glTF-Sample-Renderer` and inspected the PBR shaders and their host dependencies.
- [x] (2026-10-03) Compared the reference with OFG revision `90cde318319d342aed7d347f4b183efc9b8e88ad`, checked extension status and prepared the [source guide](../research/khronos-pbr-reference.md).
- [x] (2026-10-03) Defined proposed milestones, ownership, portability gates and validation below. This was the documentation-only research checkpoint, before implementation authorization.
- [x] (2026-10-03) Revised priorities per user: make the material sphere grid the primary test system, add early debug fly-camera controls and defer transmission/scattering until the PBR foundation is solid. That priority update was documentation-only; the later user instruction authorized implementation.
- [x] (2026-10-03) Milestone 0: establish the sphere grid and debug fly camera, then prove PBR resource/layout and output contracts on both targets.
- [x] (2026-10-03) Milestone 1: implement texture/material and static mesh foundations.
- [x] (2026-10-03) Milestone 2: implement core direct-light PBR and HDR output.
- [x] (2026-10-03) Milestone 3: implement matched environment lighting and review the solid PBR foundation on the sphere grid.
- [x] (2026-10-03) Milestone 4: implement layered surface extensions and material variants.
- [ ] Milestone 5: functional sphere-grid review, native/browser validation, documentation and payload/timing baseline are complete. Full Khronos differential comparisons and measured live/pending GPU residency remain unverified; retain this plan for those checks rather than claim full acceptance.

Deferred follow-ups A–C below are outside these completion gates, not unfinished prerequisites: transmission/volume/dispersion, RC extensions, and draft scattering/legacy compatibility. Reference-asset import is supplementary and does not block the procedural sphere-grid foundation.

- [x] (2026-10-03) Added the 7x7 sphere sweep, two supplementary swatch rows, shared fly camera, surface material description, shader packaging, punctual lighting, matched baked IBL, HDR and display-linear alpha composition.
- [x] (2026-10-03) CPU-only and native core validation: 65 cases / 1611 assertions. Final native CTest: all four targets pass; scene/PBR/texture tests: 19 cases / 1009 assertions; original checkerboard: 22 assertions.
- [x] (2026-10-03) Web build, emitted WGSL validation, PBR runtime smoke and retained texture/checkerboard smoke pass. Inspected native/browser overview and close-up captures, native resize/minimize/restore/close and browser capture/movement/release/reset/blur/resize/reload. The maximum layout runs with 16 sampled textures and 13 samplers.
- [x] (2026-10-03) Added actual GPU checks for layer neutrality, IOR/specular/clearcoat analytic limits, thin-film zero-thickness neutrality and finite combined layers at grazing view. A matched 960x640 cross-backend comparison has mean absolute channel error 0.0148/255; 99% of silhouette-excluded interior channel differences are at most one code value. This is descriptive evidence, not Khronos parity.

## Surprises & Discoveries

Naming review, 2026-10-04: numbered texture names exposed CPU array order despite fixed property semantics; numbered UV helpers also obscured their distinction from geometry UV sets. The renamed shader tokens preserve the exact computations.

Scope refinement, 2026-10-03: a procedural sphere grid can expose the material response and support repeatable review before a glTF importer or transmission/scattering passes exist. Free camera movement is useful early, but automated comparisons need a fixed reset pose and deterministic lighting.

Khronos already uses compile-time material/geometry/pass variants. Its WebGL2 renderer is not a drop-in WebGPU pipeline. The filtering shaders needed for IBL live in `source/shaders`, outside the directory highlighted in the request. The current reference has a final tone-map pass, transmission background rendering and a diffuse-scattering prepass.

The baseline WebGPU resource limit can be exceeded by a highly textured material plus IBL even without transmission. Removing unused resources solves ordinary cases, but not all valid combinations. The first milestone must prove actual reflected layouts, not assume compiler stripping or native limits.

The current snapshot includes RC retroreflection and draft volume scattering. Sparse scattering is not implemented as an advertised completed feature in the reference README; `scatterAnisotropy` is parsed without corresponding shader evaluation found. Several shader structures rely on partial initialization, and the reference's GL depth reconstruction, RGBA8 transmission background and gamma approximations need deliberate adaptation. Detailed evidence is in the source guide.

Browser ABI finding, 2026-10-03: the pinned RHI WebGPU input layout maps attributes by sequential location and the pinned Slang WGSL emitter derives locations from numeric semantic indices. Adding UV1 implicitly shifted normal/UV0/tangent locations. Vulkan location attributes did not change WGSL output. A dedicated PBR input layout with matching explicit numeric semantics fixes the demonstrated mismatch without changing diagnostic shader contracts. Emitted WGSL is retained under `artifacts/pbr/shaders`; the corrected normal-view regression and captures are under `artifacts/pbr/browser`.

## Decision Log

2026-10-04, shader maintainability: property identities are fixed, so shader defines, texture/sampler bindings and per-property UV metadata use semantic names. Keep enum-indexed CPU storage and bitmask cache keys internally, with one explicit C++ mapping to names; no generic binding framework or compatibility aliases are needed. This changes the named material interface without changing lighting equations or the portable texture budget.

2026-10-03, research recommendation: port the recognizable BRDF/material/IBL functions closely and record every intentional mathematical deviation. Preserve upstream provenance. Do not port JavaScript/WebGL resource management or import viewer/physics/interactivity infrastructure.

2026-10-03, proposed architecture: extend the current forward renderer with an explicit pass sequence and concrete resource types. Avoid a general render graph or material inheritance hierarchy. Keep GPU-independent material/mesh/import contracts in `ofg-core` and RHI operations in `ofg-render`.

2026-10-03, proposed specialization: use typed feature/resource keys and uniforms for factors. Select variants by meaningful extension, texture, geometry and pass differences; include all variable fixed-function state in pipeline keys. Do not key by arbitrary material values, exact light counts or unordered define hashes.

2026-10-03, proposed lighting preparation: load reproducibly baked environment maps/LUTs first, rather than making runtime filtering or Python a prerequisite. Pin a concrete baking method and asset format in milestone 3 after proving the bake/runtime pairing.

2026-10-03, revised user priority: demonstrate a solid core PBR basis on a sphere grid first, with early debug fly-camera controls. Transmission, volume, dispersion and scattering move outside the current acceptance gates; RC/legacy features remain later work. This supersedes the original primary target covering all ratified material extensions. A factor-only extension or native-only path is not full support for a textured extension on the declared portable profile.

2026-10-03, review-scene decision: use shared procedural sphere geometry with independent materials, a fixed roughness/metallic grid and identified supplementary material swatches. Keep a reset camera pose and fixed lighting for screenshots; fly-camera movement does not drag the lights with it. Imported models supplement this scene and are not required to make it useful.

2026-10-03, implemented output decision: lit opaque/masked surfaces render into RGBA16F and receive exposure plus Khronos Neutral in a fullscreen pass. Unlit opaque/masked surfaces then compose with the same depth attachment; a single back-to-front queue composites both lit and unlit alpha materials into display-linear RGBA16F. Lit transparent fragments are tone mapped before blending. This explicit display-linear composition preserves unlit values and mixed coverage; it differs from scene-linear blending before a nonlinear tone map. Final UNORM output uses exact sRGB encoding; sRGB attachments encode in hardware; float diagnostic targets stay linear. No integer classification buffer or approximate inverse tone map is used.

2026-10-03, implemented bake decision: embed a 229,348-byte analytic studio bake (32-square cube faces, six mips, 64-square combined lookup) so both hosts have identical reproducible lighting without loading or Python at runtime. The optional NumPy baker evaluates the continuous analytic environment directly; it adapts Khronos's GGX/Charlie importance sampling and lookup integration and separately integrates the runtime sheen energy scale. This small laboratory bake is not a general HDR image importer or a production-resolution environment asset pipeline.

2026-10-03, implemented geometry decision: the procedural sphere has analytic seam-split tangents with V-down handedness. W=0 denotes missing authored tangents and requests a derivative UV0 frame; each normal map uses its own transformed UV derivative frame, including UV1. Zero normals use a geometric derivative normal. No MikkTSpace dependency or importer is introduced. GGX/Charlie evaluation clamps perceptual roughness to 0.045 for finite pixel-scale lobes while retaining authored zero in material data.

## Outcomes & Retrospective

Naming cleanup, 2026-10-04: all fifteen texture properties now have descriptive shader defines, bindings and UV helpers. The C++ mapping is explicit and fixtures use named enum values. Native and browser fixed-pose output is unchanged; the existing extended-validation gaps below remain open.

The default runnable application is now a 63-sphere PBR laboratory on D3D12 and WebGPU. The 7x7 metallic/roughness grid, fourteen identified supplementary swatches, fly camera and debug views make the foundation reviewable. Core direct/IBL shading, HDR output, texture transforms, alpha modes and the selected surface extension equations are implemented; transmission/scattering remain deferred. See [the delivered contracts and controls](../pbr.md).

The decisive portability findings were shader vertex locations and derivative uniformity. Native-only success did not expose either problem. Dedicated WGSL inspection and the browser normal view now guard both. Native front-face culling also needed to match the existing left-handed camera convention.

Runtime material switching uses existing overrides; glTF asset import and `KHR_materials_variants` mapping are not implemented. All four IBL resources are deliberately shared by core/surface variants, trading four fixed bindings for a simple common frame ABI. The small embedded analytic environment is ready at startup; a general asynchronous environment loader is not implemented. No full Khronos conformance, long-running GPU-residency measurement or GPU-only frame-time claim is made. These validation limits remain visible in milestone 5, rather than being hidden by the passing smoke tests.

## Contract and Quality Baseline

Preserve existing scene/resource ownership and diagnostic behavior. A Material holds shared Shader/Texture dependencies; a Mesh holds its default materials; MeshRenderer overrides remain per instance. Material clones copy editable values and texture-slot assignments while sharing immutable dependencies. Draw extraction retains assets and snapshots the data needed for submission; material changes must not race or invalidate an extracted frame.

Texture descriptions, sampler descriptions and PBR factors remain GPU-independent. Graphics prepares private RHI data and owns frame-sized targets, environment GPU bindings and weak caches. Texture data publication is atomic after successful creation/upload. Failed or canceled loads do not leave partially ready materials. Submitted work retains everything it needs through completion; verify textures, mip views and resized targets under the existing RHI lifetime model before inventing additional retirement machinery.

Define scene lengths in metres for lighting/attenuation, local thickness in mesh units transformed by model scale, film thickness in nanometres and angles in radians at the CPU/shader boundary. Use linear RGB for lighting, exact sRGB interpretation for color textures and exactly one final display encoding. Preserve HDR values until output mapping. Base color/alpha is independent of transmission, and IOR/specular/sheens/coats must preserve their documented interactions.

Use [0,1] depth, an explicitly tested screen-UV convention, inverse-transpose normals and consistent tangent handedness. All shading positions/light positions must share one origin. The initial fixtures are near-origin, but the shader ABI should accept camera-relative positions without baking global coordinates into material evaluation.

Every feature has an explicit supported/unsupported state. Unknown required glTF extensions and unsupported binding combinations fail with asset/material context. Optional-extension fallback follows the corresponding specification and reports what was omitted. Do not silently relabel a simplified material as full extension support.

## Context and Orientation

Read [architecture](../architecture.md), [resources](../resources.md), [state and scene](../state-and-scene.md) and the [Khronos source guide](../research/khronos-pbr-reference.md). The inspected research baseline was a single-pass indexed checker-mesh renderer with one UV set, tint uniforms, no material textures, no lighting, no blend/cull state and no importer. The separate [completed texture-support plan](../archived/texture-support.md) is now available; inspect its delivered contracts and validation before implementing overlapping work here. `Graphics` caches shader programs/pipelines; `DrawList` preserves asset ownership; browser presentation uses a host-owned texture before acquiring the canvas image.

PBR is physically based rendering. A BRDF describes reflected light; a BTDF describes transmitted light. IBL uses prefiltered environment textures to approximate lighting from many directions. A LUT is a precomputed lookup texture. A shader variant is a compiled selection of features/resources from the shared source. These mechanisms need compatible data and passes, not just translated equations.

The downloaded renderer is a reference input, not an OFG dependency. Its exact revision and the specification/asset revisions are recorded in the source guide. RHI remains pinned at `16324a68af477baaede620e713644f5e9613b1a2`. New decoder/import/baking dependencies must be inspected, pinned and documented when selected; none is selected or installed by this plan.

## Plan of Work

### Review scene: a deterministic material sphere grid

Make a procedural grid of equal-sized, separated spheres the default PBR laboratory fixture, for example in `src/lab/pbr-fixture.*`. Reuse one sphere mesh with smooth normals, seam-correct UVs and a tested tangent frame. Each cell has its own material values while sharing immutable geometry, textures and shader programs where appropriate. This scene must work without importing glTF files or waiting for ImGui.

Start with a 7-by-7 parameter grid: columns increase authored roughness from 0 to 1, rows increase metallic from 0 to 1, with constant base color so each axis has a clear meaning. Include the authored zero-roughness boundary while applying the separately documented finite BRDF evaluation policy. Add a clearly separate set of identified spheres for different base colors, textured/normal-mapped materials, emission and unlit; add paired baseline/extension spheres as surface extensions arrive. Avoid changing unrelated inputs within a parameter sweep.

Give each cell a stable row/column identity and record its material parameters in the fixture definition/capture manifest. Show a simple legend through existing UI if available, or document the layout alongside the fixture until it is. Keep a fixed overview/reset pose, an additional close-up comparison pose, repeatable environment orientation, exposure and world-space lighting. The initial overview must frame the whole grid; close inspection must make seams, normal orientation and highlight shape visible. Do not animate lights/materials by default. Save native/browser overview and close-up captures for review at each material milestone. Additional geometry/import fixtures supplement the grid for edge cases it cannot prove.

### Milestone 0: sphere-grid inspection and cross-platform contracts

Bring up the sphere grid first using the existing diagnostic material, then add a small shared debug fly-camera controller, for example in `src/lab/fly-camera.*`. It controls the selected scene Camera through its Entity; native/browser hosts provide only input state and pointer capture. Use WASD for horizontal movement, Q/E for vertical movement, mouse look while explicitly captured, Shift for faster travel and a reset action restoring the fixed overview pose. Document the final bindings. Movement speed is in metres per second using frame delta; yaw/pitch have no roll and pitch is clamped. Mouse displacement must not receive an extra frame-delta multiplier. This is a terrain/material inspection tool, not a character controller or generic input framework.

On native, use deliberate mouse capture; on web, request pointer lock only from a user gesture. Release capture on Escape or focus loss, clear held input on blur, and do not move the camera while UI captures input. Preserve existing close behavior when no camera capture is active. A reset pose lets interactive review return to reproducible screenshots. Keep automatic smoke captures stationary except for a focused input exercise; do not depend on nondeterministic manual navigation for image comparisons.

Next prove a minimal textured draw using the intended constant-buffer, texture and sampler layout, a filtered cube mip, an RGBA16F render target and a fullscreen output pass on D3D12 and WebGPU. Inspect reflected/emitted bindings for core and representative surface-extension layouts, including a deliberately over-budget material. Transmission layouts are deferred with their follow-up. Record actual created-device limits and format support; the pinned RHI does not expose all WebGPU per-stage limits directly. Log the backend-selected adapter and the source of each reported limit.

Choose deterministic shader packaging. Proposed `shaders/pbr/` modules should cover BRDFs, material evaluation, textures, punctual lighting, IBL, iridescence and the mesh entries; CMake must embed all transitive sources or supply a tested virtual-module loader. Prefer the smallest working option with useful diagnostics. Shader variants with identical source/layout features share a program. A resource-only final presentation pass must preserve the browser's no-yield canvas acquisition contract.

Settle output handling here: lit HDR receives exposure and the selected tone mapper; unlit and debug output have defined bypass semantics. Use Khronos PBR Neutral initially, plus a linear diagnostic mode. Test opaque unlit and blended unlit over lit content; unlit content seen through transmission belongs to deferred follow-up A. Compare the reference flag-buffer approach, but do not assume one unblended integer flag correctly represents mixed coverage. Record the chosen composition behavior and any compatibility limitation before later milestones depend on it.

Proof: inspect the sphere grid and fly-camera movement on both targets, including capture/release, focus loss and reset. Add focused CPU tests for delta-based translation, diagonal-speed normalization, pitch limits and reset, and host smoke checks for capture/focus handling. Run native GPU tests for sampling/layout/HDR and save fresh browser captures with no validation errors. Publish the portable resource budget, explicit unsupported-combination behavior and an output-contract decision. This is an inspection/capability slice, not complete PBR.

### Milestone 1: texture, material and geometry foundations

Reuse the texture/sampler resources and tests delivered by the separate texture-support work. Add only the missing PBR requirements for decoded texture/mip descriptions, sRGB/data interpretation and image reuse across interpretations, default texture semantics, asynchronous decode/upload failure and cancellation. Review the selected decoder's native/WASM contracts rather than selecting a second decoder. Keep native loading independent of browser tools. Supply deterministic procedural textures for the sphere-grid tests so file import does not block GPU work.

Extend `Material` with explicit texture slots and alpha/cull state, and add a concrete PBR material description, for example `src/resources/pbr-material.*`. Prefer a helper/description over a new virtual hierarchy. Use typed defaults and feature extraction; retain the existing named-uniform path for diagnostic/custom shaders. Define how ordinary PBR factor changes and texture-presence changes affect variant selection. Do not maintain two conflicting authoritative copies of PBR state.

Extend the static mesh description with tangents, UV1 and vertex color using a simple documented layout and presence/default metadata. Missing normals/tangents must have a deliberate import/evaluation path. Pin MikkTSpace if adopted for generated tangents; handle split vertices and mirrored UVs correctly. Preserve submesh and resource-clone behavior.

Support sampler wrap/filter/mip rules, per-slot UV transforms and UV-set overrides. Mips for sRGB color must be filtered in linear light; normal/data textures need appropriate semantics. The same image used for AO and MR can share storage without losing distinct slot metadata. Over-budget layouts produce a diagnostic before draw submission.

Proof: CPU tests for defaults, clone isolation, transforms, feature keys, validation and failed loads; native sampled-color/data/alpha/mip fixtures; native and browser transformed-UV and mirrored-normal screenshots. Retain the original checker shader unchanged as a useful diagnostic.

### Milestone 2: core forward PBR, lights and HDR

Port the core BRDF functions and material evaluation with recognizable upstream provenance, complete default initialization and documented numerical safeguards. Implement normals/TBN, base color, metallic/roughness, AO input, emission, unlit and emissive strength. AO only affects the intended indirect contribution once IBL is present; it must not darken direct lights or emission by accident.

Add concrete directional/point/spot light descriptions and scene extraction. Begin with a bounded forward light array and runtime active count; set its capacity from the tested profile and report overflow rather than truncating silently. Match the punctual-light specification's intensity/range/cone conventions. Snapshot camera position, view/projection, model/normal transform and handedness in frame/draw data. Keep host event/window code small.

Render into HDR scene color with depth, then apply the milestone-0 output contract. Implement opaque, masked and sorted blend queues, correct depth writes, culling/double-sided normals and mirrored-transform winding. Update pipeline keys for feature program, vertex layout, color/depth formats, blend/depth/cull/front-face state, topology and sample count wherever these vary. Preserve depth contents across sequential passes.

Proof: the defined sphere grid now uses PBR materials under each light type, with distinguishable dielectric/metallic behavior and roughness progression. Review from the fixed poses and with the fly camera. Supplement it with nonuniform/mirrored transforms, alpha mask and blend ordering, bright emission and exposure tests. CPU light/draw classification tests plus native float-target readback and browser visual checks must pass. No scene shadowing claim is made.

### Milestone 3: matched IBL and foundation review

Implement proposed `src/resources/environment.*` and the IBL functions. Define a versioned baked bundle with face orientation, linear HDR format, actual roughness mip range, diffuse normalization, GGX/Charlie distribution, LUT channel/axis convention, intensity normalization and source/bake hashes. Initially load the prefiltered bundle; avoid expensive unmeasured runtime filtering. Reuse or adapt the reviewed filtering recipe in an offline tool after recording its dependencies and licenses. Native application startup must not require that tool, Node or Python.

Verify the bake with a constant environment, an orientation-marked cubemap and roughness sweeps before photographic HDRs. Introduce Charlie/sheens resources when needed by milestone 4, without binding them to every core variant. Load the sheen-energy LUT deliberately; the current reference generates GGX/Charlie LUTs but reads that separate image. Async environment replacement retains the old ready environment until the new bundle is complete, then retires it safely.

Use the sphere grid to compare direct-only, IBL-only and combined lighting with fixed material values, exposure and camera poses. This is the first foundation acceptance checkpoint: review the full roughness/metallic sweep, normal/color/data texture handling, emission/unlit and HDR output on both backends, then inspect highlights while flying around the grid. Record any unexplained differences before expanding the material model. Neither a glTF importer nor transmission/scattering is required to pass this checkpoint.

Reference assets are supplementary: once needed, add a minimal glTF/GLB path, for example `src/resources/gltf-loader.*`, after selecting a maintained, pinned native/WASM-compatible parser. Limit initial rendering to static triangle meshes, material textures, node transforms and punctual lights. Validate accessors, index ranges, image/URI dependencies and required extensions. Use external byte fetching supplied asynchronously by hosts while keeping parsing/material mapping shared. Extend the development server's explicit asset routes and smoke packaging when external fixtures are introduced. Unsupported animation/compression/topology must be reported as such. This importer can be a separate follow-up and must not delay the procedural grid.

Proof: sphere-grid overview and close-up captures under a pinned environment, stable reset after free navigation, and controlled scene-linear checks on native and browser. Verify cancellation and environment replacement residency. Match camera/exposure/tone mapper for reference comparisons; use imported `MetalRoughSpheres`, normal/tangent and UV-transform fixtures when an importer is available, or matched parameter samples in the Khronos renderer without requiring OFG import. Record the foundation review result before starting the surface-extension milestone.

### Milestone 4: layered surface extensions and variants

Port IOR and specular first, then clearcoat, sheen, iridescence and anisotropy. Keep the reference's layer order, colored Fresnel mixing, direct/IBL contributions and sheen energy scaling. Add textures as well as factors. Test separate clearcoat normals and independent normal UV transforms. Correct any demonstrated reference defect with a spec citation, a focused regression and an intentional-difference note rather than blindly matching it.

Use the sphere grid's supplementary swatches for paired baseline/extension review. Exercise material switching through the existing MeshRenderer override mechanism; map `KHR_materials_variants` when an importer is available. Rebuild classification/feature selection when a chosen variant changes alpha mode or texture requirements; material choice must precede draw-queue classification. Include transmission reclassification when that deferred feature is eventually implemented. Keep clone edits independent and program sharing intact.

Proof: each extension enabled/disabled, zero-strength neutrality, textured and factor-only cases, and representative valid pairings (IOR/specular, clearcoat/normal, sheen/base, iridescence/metal and dielectric, anisotropy/rotated UVs). Use the source guide's verified asset candidates and exercise the portable binding boundary. Support claims name any excluded over-budget combinations. Measure actual compiled variants and first-use cost; do not precompile the combinatorial power set.

### Milestone 5: complete sphere-grid review and the surface PBR integration

Complete the primary sphere-grid laboratory, its fixed overview/close-up views, fly-camera controls and lighting/material debug views. If the shared ImGui foundation exists by then, use it for exposure, environment, feature toggles and normals/roughness/metallic channels. Otherwise, use deterministic fixture configuration and a documented grid legend; keep UI implementation a separate bootstrap slice. Retain the checkerboard and checker-mesh diagnostics.

Run the declared core/surface support matrix on native and focused browser smoke scenarios. Inspect the grid while moving the camera, reset to the saved poses, and check highlight changes, ordinary alpha blending and resize. Review CPU/GPU boundaries, ownership, numerical assumptions, source comments, license attribution, obsolete scaffolding and cache lifetime. Update architecture, resource contracts, DEVELOPING.md and the bootstrap plan with actual behavior and remaining limits. This milestone can complete with every transmission/scattering follow-up still deferred. Before archiving the completed scope, carry the deferred notes into a clearly labeled future plan or preserve them in the research guide.

### Deferred follow-up A: transmission, volume and dispersion

This work follows a reviewed, solid PBR foundation. It is not part of milestones 0–5 acceptance; do not create its render targets or bindings early solely to anticipate it.

Introduce an explicit HDR background capture plus mip generation. Match the reference ordering: environment and non-transmissive opaque/masked/blended objects form the background; the main scene renders opaque, then transmissive, then ordinary blended objects. Avoid sampling the active attachment. A reusable snapshot of the main opaque color alone is insufficient when blended objects must appear behind glass. Preserve scene depth in the main path and use separate background depth as needed.

Use viewport-aware dimensions and the actual mip extent in LOD calculations, including odd/non-square sizes and resize. Port refraction, IOR-adjusted roughness, thickness scaling, Beer attenuation and RGB dispersion. Test thin transmission without a volume extension by explicitly supplying neutral volume fields. Keep alpha coverage independent from transmission. Handle projection bounds and invalid refraction directions according to a documented approximation policy.

Proof: thinwall, rough glass, absorbing volume, nonuniformly scaled glass, dispersion, transparent objects behind glass, multiple ordered glass objects, resize and environment changes. Compare to the reference within its screen-space limits. Document that offscreen/hidden geometry, recursive refraction, caustics and intersecting transparency remain unsupported approximations. Verify no mip/attachment hazards or resource leaks in either backend.

### Deferred follow-up B: diffuse transmission and retroreflection

These RC-extension slices are outside the current foundation/surface completion gates. Their ordering can be reconsidered after the sphere-grid basis is reviewed.

Pin the RC specifications again at implementation time and record any change from the research revision. Add diffuse transmission's back-light contribution, factor/color texture channels and volume absorption interactions. It can run without specular transmission background capture. Test leaves/paper-like surfaces under front/back punctual light and IBL, with and without volume attenuation.

Add retroreflection's reflected-view lobe, including anisotropy and iridescence interactions already represented in the reviewed shader. Verify the expected light/camera alignment response and the zero-weight baseline. Treat these features as RC semantics in the support table until status is reverified; their existence in the source is not ratification evidence.

Proof: controlled back-light and alignment fixtures on both targets, reference captures, combination tests and binding-budget checks. Keep these features separate from the stable-profile completion report.

### Deferred follow-up C: draft scattering and archived compatibility

For dense volume scattering, evaluate the Burley prepass/lookup path only after the PBR foundation is reviewed and the volume/diffuse-transmission dependencies needed by the selected model are implemented. Proposed `shaders/pbr/scattering.slang` and a small `src/render/scattering.*` helper own its distinct responsibility. Use illumination, depth and a separate integer identity target; do not encode IDs in filtered color alpha. Reconstruct view positions using OFG's [0,1] depth convention. Move inverse projection out of per-pixel inversion. Validate cross-object bleed, radius versus resolution, grazing views, nonuniform scale and IDs beyond 255.

Set a tested sample-count/resolution budget from named hardware measurements. The reference uses 55 samples, but that is not an OFG performance commitment. Dense screen-space scattering does not implement sparse volume scattering or the parsed anisotropy parameter. Label the delivered subset, and do not close a full-volume-scatter claim while those semantics are absent. This feature remains an explicitly deferred experiment without blocking the core/surface profile.

If archived specular/glossiness is needed, use a separate material workflow sharing low-level BRDF helpers. Prefer faithful legacy evaluation to a lossy runtime conversion presented as exact. Test its texture color/alpha interpretation and exclusions. It is not a prerequisite for newly authored metallic/roughness assets.

Proof when undertaken: separate experimental/legacy reports, feature status and native/browser captures. Keep these features marked deferred in Progress and the support table until actually implemented.

## Concrete Steps

For continued implementation or validation, start in `C:/dev/ofg` with `git status --short`, this plan and the source guide. Follow the [native build skill](../../.agents/skills/build-native/SKILL.md) and [web build skill](../../.agents/skills/build-web/SKILL.md), using [DEVELOPING.md](../../DEVELOPING.md) for current x64 Visual Studio and Emscripten setup. Initialize each toolchain in the same shell invocation that needs it; do not assume environment changes persist between command calls.

Commands executed successfully during implementation (toolchain setup is in DEVELOPING.md):

```powershell
cmake --preset cpu-tests
cmake --build --preset cpu-tests --parallel
ctest --preset cpu-tests --output-on-failure

cmake --preset native-debug
cmake --build --preset native-debug --parallel
ctest --preset native-debug --output-on-failure
.\build\native\ofg.exe --check-device

cmake --preset web
cmake --build --preset web --parallel
node tools/pbr-shader-check.mjs
node tools/pbr-smoke.mjs
npm.cmd run smoke:web
```

These blocks require the respective documented toolchain environments. Inspect exit status after every command. Run `npm.cmd ci` if the locked browser tooling is missing. Use the documented clang-format 22.1.3 command for C++ and `git diff --check` for whitespace. Browser shader/binding/presentation changes require runtime smoke immediately; CPU-only parsing changes require focused CPU tests rather than automatic browser rebuilds for every edit.

The PBR fixture and existing CTest targets are implemented. `DEVELOPING.md` and `docs/pbr.md` contain runnable fixture selection, controls, debug views and bake commands. The full C++ suite remains native; browser smoke exercises the shared WASM/WebGPU path.

## Validation and Acceptance

Foundation acceptance requires milestones 0–3: a reviewable material sphere grid with working debug fly camera and reset, core textured PBR, direct lights, matched IBL and correct HDR/output on both targets. Surface-extension acceptance follows in milestones 4–5. The deferred follow-ups have their own tests and cannot block either checkpoint. glTF-specific importer tests apply when that supplementary path is introduced.

| Boundary | Required proof |
| --- | --- |
| Sphere-grid review | Stable 7-by-7 roughness/metallic axes, identified supplementary material spheres, independent material values over shared geometry, whole-grid overview and close-up screenshots with recorded lighting/exposure. Review highlights while moving, then return exactly to the comparison pose. |
| Debug fly camera | Delta-based and normalized movement, pitch bounds, mouse capture/release, focus-loss input clearing, UI capture exclusion, native/browser input exercise and deterministic reset. Automated image captures start at a fixed pose. |
| Material/import CPU contracts | Defaults, texture channels, per-slot UV transforms, invalid required extensions, attribute/index validation, feature keys, independent clones and variant draw reclassification. |
| Shader math | Selected analytic cases and finite results at grazing/roughness boundaries; zero extension strength matches baseline; directional/point/spot attenuation. Execute the actual shader in native tests. Absorption tests belong to deferred volume work. |
| Texture/render integration | sRGB RGB with linear alpha, normal/data sampling, mip behavior, cube orientation, reflection offsets, resource counts, HDR values above one and exact single output encoding. |
| Geometry | Missing/generated tangents, mirrored UVs, nonuniform/negative transforms, double-sided normals and independent clearcoat normal UVs. |
| Foundation passes | Mask cutoff boundaries, blend/depth order, scene HDR, mixed lit/unlit alpha blending and output classification. Transmission background mips and transmission-without-volume tests belong to deferred follow-up A. |
| Loading/lifetime | Failed decode/upload, canceled/reordered requests, replacement during in-flight draws, resize, shutdown/reinitialize and bounded live CPU/GPU bytes including pending work. |
| Cross-platform | Native GPU suite and inspected native window; browser initial/resize/reload captures and clean console/validation report on each rendering milestone. |
| Reference parity | Pinned model/environment, camera, exposure, tone mapper and feature configuration; retain reference/output/difference artifacts and document intentional deviations. |

For bounded scalar BRDF checks away from singular limits, begin with `absolute error <= 1e-5 + 1e-4 * abs(reference)` using independent analytic/double-precision expectations. This is a proposed float32 tolerance, to be justified for each test's units and magnitude, not a universal screenshot threshold. For direct 8-bit transfer tests, allow one code value of quantization error away from raster edges. FP16 HDR storage needs separately justified relative/absolute bounds. Physical invariants and NaN/Inf rejection complement numeric comparisons.

Image comparison thresholds must be fixed from matched controlled captures before accepting an implementation. Mask only documented raster-edge differences; retain interior color/structure assertions and inspect error images. Do not increase thresholds simply to hide unexplained BRDF, orientation or encoding errors. Browser smoke establishes presented behavior and diagnostics, not bitwise equality with D3D12.

Measure shader compilation latency/variant count, frame CPU/GPU cost where supported, texture/environment/frame-target bytes and replacement/resize residency. Record GPU, driver, backend, browser, viewport, lights, material workload, sample count and timing method. Do not invent an FPS target before the baseline. Primary acceptance requires staying within the declared device limits and bounded live residency; a cumulative allocation count or process RSS alone is insufficient.

Coverage policy: test every new CPU contract and failure path; shader/upload/pass behavior requires GPU integration. Keep a table mapping supported features to fixtures. No numerical coverage threshold is inherited and no coverage percentage is claimed. Select coverage tooling only if useful for the new CPU importer/material logic; record exclusions and any measured result honestly.

## Milestone Review

Commit review after glTF/ImGui integration, 2026-10-04: the combined main checkout passed native-debug configure/build and all four CTest targets after restoring these PBR edits. The core suite now contains 92 cases / 2174 assertions. A token comparison against HEAD confirms both shader files change only the fifteen property identifiers and whitespace; formatting and whitespace checks pass. The combined tree also passes web configure/build, `node tools/pbr-shader-check.mjs`, `node tools/pbr-smoke.mjs` and `node tools/model-smoke.mjs`. Fresh PBR and model-workspace screenshots were inspected; browser reports contain no unexpected errors. The known Asyncify/exception and Windows powerPreference warnings remain. No further code fixes were needed after the merge integration.

Naming review, 2026-10-04: native and web incremental builds pass; native CTest passes all four targets (65 core cases/1611 assertions, 19 scene/PBR/texture cases/1009 assertions, checkerboard 22 assertions). `node tools/pbr-shader-check.mjs` passes core/all-texture/unlit WGSL checks; `node tools/pbr-smoke.mjs` passes presentation, camera, normal-view and maximum-layout checks in Chrome 154.0.8037.95. Native window resize/minimize/restore/close passes. Inspected fresh native/browser captures and verified both 960x640 fixed-pose images match their pre-change pixels exactly; evidence is in `artifacts/pbr/naming/comparison.json`. Source comparison confirms the shaders changed only property names and whitespace. No lighting or resource-budget changes were introduced. clang-format 22.1.3 and `git diff --check` pass. Existing Emscripten Asyncify/exception and Chrome power-preference warnings remain.

Implementation review, 2026-10-03: inspected material ownership, private GPU publication, weak shutdown tracking, submission retention, cache keys, alpha classification, culling, transfer functions, numeric guards, source comments and provenance. Removed redundant draw preparation and corrected a non-strict generic transparent-sort comparator. Native action callbacks retain brief reset/close keys without sticky movement. CPU/native tests, native window capture, WGSL validation and both browser smoke suites pass. The existing Asyncify/WASM-exception compiler warning and Chrome Windows power-preference warning remain recorded. clang-format 22.1.3 verification, 71 local Markdown links/code fences and `git diff --check` pass. The final WASM is 27,616,935 bytes before compression. Current extended-validation limits are stated in Progress and Outcomes; the earlier entries below are historical research checks.

Scope revision, 2026-10-03: made the sphere grid and early fly camera explicit in purpose, milestone work and acceptance; moved final core/surface review ahead of transmission/scattering and removed those features from early proof requirements. Imported models are supplementary. Preserved existing concurrent implementation edits. Checked the three revised documents: 24 local links, reference labels, code fences, whitespace and milestone ordering passed, as did the scoped `git diff --check`. This revision changes documentation only; graphics builds and runtime checks are not applicable.

Research review, 2026-10-03: compared source facts with proposals; checked core versus extension scope, current OFG gaps, owner lifetimes, WebGPU limits, browser presentation, shader composition and stale/experimental upstream behavior. The design uses concrete additions and an explicit forward pass sequence. At that research checkpoint, no implementation milestone was complete. Validation passed for 27 local links and 29 exact-case pinned renderer paths across the four changed documents, Markdown reference labels/fences, trailing whitespace and `git diff --check`. The reference checkout remains clean. Runtime checks were intentionally not run for documentation-only work.

At each future milestone, review correctness, API/ABI contracts, resource retention, invalid-input diagnostics, simplicity, human-readable comments, upstream notices, cache identity, cross-platform proof and documentation. Resolve findings before marking that milestone complete. No agent delegation or nonexistent review skill is required.

## Idempotence and Recovery

Preserve unrelated edits and the historical `C:/dev/ofg-old2`. Keep reference downloads and capture outputs under ignored artifacts, with immutable upstream links in tracked docs. Never update the RHI pin as an automatic workaround. Reuse existing build trees; configuration/toolchain changes need an intentional separate tree rather than recursive cleanup.

Publish resource replacements only after successful preparation; cancellation leaves the previous ready resource usable. Include source/features/layout in variant identity so changing a feature cannot reuse an incompatible shader. Prune weak application caches, account for compiler-session retention separately and keep a tested shutdown/reinitialize path. Use the existing diagnostic modes to isolate PBR failures. Do not silently fall back to a materially different shader after compilation or capability failure.

## Artifacts and Notes

Research source: `artifacts/reference/glTF-Sample-Renderer` at the recorded SHA. Selected spec snapshots: `artifacts/reference/gltf-spec-notes`. The source guide contains permanent links, function locations, extension status and texture channels.

Implementation evidence: `artifacts/pbr/native` contains window captures, logs and the 960x640 offscreen result; `artifacts/pbr/browser` contains runtime reports and captures; `artifacts/pbr/shaders` contains emitted WGSL and compiler/browser diagnostics. `artifacts/pbr/comparison.json` and `difference-x8.png` record the cross-backend comparison. These are OFG comparisons, not captures of the Khronos reference renderer.

Native baseline: NVIDIA GeForce RTX 3050 Ti Laptop GPU, driver 32.0.15.9608, D3D12 debug validation, 960x640, 63 spheres, two lights, 11 pipelines. Last measured first frame (including compilation/upload) 2233.54 ms; mean of ten warm render-submit-and-queue-wait frames 16.50 ms. These are wall-clock debug timings, not isolated GPU timings. Browser: Chrome 154.0.8037.95, Intel gen-12lp (host driver 32.0.101.7085); the actual created device exposes 48 sampled textures, 16 samplers and a 65,536-byte uniform binding limit. OFG deliberately enforces a conservative 16/16 texture/sampler profile.

Unpadded payload baseline: 229,328 bytes for four IBL textures; 114,696 bytes for the shared sphere vertex/index data; 16,380 bytes for three 32-square full-mip material textures. At 960x640, two RGBA16F targets plus D32 depth occupy 12,288,000 texel bytes; the browser presentation image adds 2,457,600 bytes. These are format/dimension payload calculations, not measured driver allocations or live/pending residency. That distinction is why the residency gate remains open.

## Interfaces and Dependencies

Implemented concrete boundaries are Texture, PbrMaterialDesc, Environment, Lighting, DrawList and the explicit Graphics pass sequence. Texture support came from its separate completed milestone. Use direct Slang RHI objects and ShaderCursor wherever they already express the operation. Extend existing resource GPU data and weak shutdown tracking rather than duplicating registries.

Ported shaders retain Khronos origin and revision. Image decoding, glTF parsing, tangent generation and bake tooling need individual version/license/platform review before introduction. Browser hosts own asynchronous bytes and canvas presentation; shared C++ owns parsing/material/render behavior. A native development build must not acquire a WebGL renderer, JavaScript physics engine or Python runtime dependency.

Revision note, 2026-10-03: the user's review priorities supersede the original full-extension critical path. A procedural sphere grid and early debug fly camera establish a solid PBR basis; transmission, volume, dispersion and scattering remain documented future work rather than prerequisites.
