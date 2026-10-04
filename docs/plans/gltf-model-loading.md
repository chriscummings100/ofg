# Import glTF models and bind their resource hierarchies

This ExecPlan follows [PLANS.md](../../PLANS.md). Status: in progress. The user authorized beginning implementation in a separate worktree on 2026-10-04. The accepted design includes Skin and Animation resources, Mesh-owned deformation data and a passive root Animator. Playback and deformation are excluded.

## Purpose / Big Picture

Load glTF 2.0 JSON and GLB assets through `Resources::loadResourceAsync<Model>`, inspect their complete CPU resources, and instantiate independent scene hierarchies sharing meshes, materials, skins and animation clips. Both native D3D12 and browser WebGPU must render the undeformed geometry. This is not a bind-pose or initial-animation rendering guarantee.

## Progress

- [x] (2026-10-04) Inspected existing resource, scene, PBR and platform-loading contracts; agreed public ownership and import scope with the user.
- [x] (2026-10-04) Created attached worktree `C:/Users/Chris/.codex/worktrees/gltf-model-loading/ofg`, branch `gltf-model-loading`, from the current HEAD. Captured and applied the original checkout's pending PBR changes; the baseline patch is `artifacts/models/inherited-working-tree.patch`.
- [x] (2026-10-04) Milestone 1 implementation: Skin/Animation resources, Mesh deformation storage, per-instance MeshRenderer palettes/weights, typed passive Animator ownership, tests and contracts.
- [x] (2026-10-04) CPU configure/build/CTest: 77 cases / 1807 assertions. Focused resource tests: 12 cases / 196 assertions. Native configure/build and all four CTest targets pass. New-source clang-format 22.1.3 dry-run and git diff whitespace checks pass.
- [x] (2026-10-04) Milestone 1 portability/review: web configure/build, texture/checkerboard smoke and PBR smoke pass. Browser reports contain no errors; native offscreen and browser overview captures inspected. Resource/component milestone complete.
- [x] (2026-10-04) Milestone 2: pinned tinygltf, staged dependency loading and resource conversion; CPU import/boundary tests and native rendering pass.
- [x] (2026-10-04) Milestone 3: staged atomic instantiation, source-index bindings, independent instances and lifetime tests pass.
- [x] (2026-10-04) Milestone 4 laboratory delivery: native/browser model modes, original glTF/GLB fixtures, inspected captures and model/texture/PBR smoke checks pass.
- [ ] Follow-up acceptance coverage: wider licensed sample assets and imported normal-map/color/sampler reference comparisons. The implemented profile is not a general glTF-conformance claim.

## Surprises & Discoveries

The installed Emscripten libc++ does not supply char_traits<unsigned char> for the pinned JSON parser. The preflight parser now receives char pointers into the same bytes. Native accepted the original unsigned-byte iterators, so the web build was necessary to expose this.

The glTF specification permits final matrix-column padding to be omitted and inverse-bind accessors to exceed the joint count. Focused regression cases now protect both boundaries.

Tinygltf's upstream mainline has moved to a C v3 rewrite. This pass uses the established C++ v2.9.6 API. Its callbacks are synchronous, so external browser dependencies must be fetched before parsing.

OFG already provides PBR materials and a weak cooperative resource scheduler. Child assets can use ready uncached factories without changing scheduler identity. At the start, Texture privately owned reusable native-read/browser-fetch logic and Samplers lacked mirrored repeat and explicit base-level-only sampling. This pass extracted the shared reader and added both sampler policies.

Scene has no individual deletion API. Model instantiation must stage allocations before attaching a hierarchy, rather than leave partial entities on failure.

The first test compile required an explicit `<ostream>` include before doctest for MSVC's shared_ptr diagnostic formatter, matching the existing scene tests. Correcting the test include fixed the compile; no assertions were weakened.

## Decision Log

2026-10-04: Complete parsing/instantiation and prove both hosts using small original glTF/GLB fixtures, without downloading a broad external asset suite in this pass. Wider licensed sample coverage and imported normal-map reference comparisons remain tracked follow-ups. Preserve the existing PBR and texture regressions.

2026-10-04: Limit imported hierarchies to depth 256, retain trailing default material/sampler entries, and document the accounted CPU payload budget separately from parser scratch, allocator overhead and generated shader programs. These keep index domains and practical limits explicit without adding a resource registry or transaction framework.

2026-10-04: Model owns shared child resources without independently loadable subasset keys. Repeated import references share source-indexed objects within one Model; cross-file child deduplication is excluded.

2026-10-04: Skin and Animation derive from Resource and use validated ready factories. Mesh owns joint influence arrays, morph deltas and default weights. MeshRenderer owns its optional Skin, observed instance joint palette and instance morph weights. Animator is a Scene-owned passive component on the synthetic model root and retains clips plus source-node-indexed observed entity bindings. No resource owns its Model or live entities.

2026-10-04: Preserve all influence sets, morph targets and STEP/LINEAR/CUBICSPLINE channels. Do not add playback clocks, blending, retargeting, deformation shaders, joint buffers or animated bounds.

2026-10-04: Map all currently implemented PBR extensions. Render deformable meshes as explicitly undeformed previews. Retain the PBR grid as the default laboratory mode.

2026-10-04: Added borrowed Entity::scene() accessors to validate joint membership directly without walking parent chains or exposing component internals. Their owner pointer follows existing Scene move rebinding. Empty morph attributes/default weights expand to dense zero arrays at Mesh creation. Resource factories preserve supplied animation values rather than normalize/resample them. MeshRenderer rejects zero total joint weights but does not renormalize; future import validates source conformance.

## Outcomes & Retrospective

Milestones 1-3 are implemented: Model loads glTF/GLB documents and dependencies, publishes ready child resources,
and instantiates independent scene hierarchies with skin palettes, morph overrides and a passive root Animator.
Native and browser laboratory modes load the original cube fixtures. CPU and native integration checks pass;
final browser model smoke covers external/embedded assets, delayed fetches, cancellation and malformed-model errors. Texture/checkerboard and PBR regressions also pass. No playback or deformation is implemented. Wider licensed sample coverage, including
imported normal-map GPU comparisons, remains a follow-up rather than a glTF-conformance claim. The original
checkout and inherited PBR edits are preserved.

## Contract and Quality Baseline

All resource access and component mutation remain on the application thread. Model publishes only after all CPU descriptions and required images are valid. Graphics prepares resources lazily; Loaded does not promise successful shader compilation. Failure/cancellation must release pending reads and intermediate allocations. CPU resources can outlive the Model or Scene that initially used them.

SkinJoint node indices and AnimationTrack target indices belong to the originating Model's node table, not runtime EntityIds. Skin joints are stored in palette order with affine inverse bind matrices. The instance palette is a separate vector of Ptr<Entity>. Animator binds source node indices to Ptr<Entity>; null entries identify nodes outside the selected scene. Matching by name and automatic retargeting are excluded.

Mesh deformation arrays align with final concatenated vertices. Morph arrays hold deltas, not absolute values. Mesh defaults are immutable; renderer weights can differ by instance. Changing Mesh clears Skin/joint bindings and material overrides and installs new mesh defaults; assigning the same Mesh preserves state. Bindings update atomically on validation failure. Components remain passive.

Every new function needs a concise contract comment. Keep headers self-contained, private parser and RHI types out of resource APIs, and follow the checked-in clang-format configuration. Coverage acceptance is behavioral for the cases below; no numerical percentage or broad glTF conformance is claimed.

## Context and Orientation

`src/resources` contains Resource, Resources, Mesh, Material, Texture, Shader and Sampler. `src/scene` contains Scene's stable, unique-owned Entity and typed component allocations. `ofg-core` compiles and tests without a graphics dependency. `ofg-render` supplies lazy RHI preparation. Existing PBR material and rendering contracts are in `docs/pbr.md`; scene/resource ownership is in `docs/state-and-scene.md` and `docs/resources.md`.

## Plan of Work

### Milestone 1: resource and component foundation

Add SkinDesc/SkinJoint/Skin and AnimationDesc/AnimationTrack/Animation. Factories produce validated Loaded resources with empty keys. Skin validates finite affine matrices and unique nonempty joint references. Animation validates enum values, target/property uniqueness, finite increasing nonnegative times and value cardinalities; compute duration from the last key time. Preserve cubic incoming/value/outgoing groups and quaternion signs.

Extend Mesh::create with defaulted MeshDeformationData, preserving existing callers. Add separate influence and morph arrays without modifying Vertex's GPU layout. Add MeshRenderer skin/palette and morph-weight setters/accessors. Add Animator::setBindings and Scene/Entity typed creation/accessors, including scene moves, clear and observer destruction. Test invalid assignments leave old data intact, independent instance bindings share resources, and Scene::update performs no animation.

### Milestone 2: import resources

Vendor tinygltf v2.9.6 at commit `26422192e2908a562b641175dde18489824e609e`, matching JSON dependency and licenses; compile its implementation once with default filesystem and bundled image implementations disabled. Extract the existing Texture byte reader and PNG/JPEG memory decoding into cohesive reusable helpers.

Model reads its document, inspects JSON (including a validated GLB JSON chunk) for dependency URIs, fetches dependencies, parses prepared memory, converts resources, validates references and publishes. Support embedded buffers/images, data URIs and relative references including parent directories. Resolve relative to the model path/URL with correct escaping. Absolute dependency paths/URLs are excluded initially. Deduplicate requests within an import, with one active dependency read per Model. Callbacks record completion only; decode/publish happens through Resources::update.

Retain image limits; initial model limits are 64 MiB per input file, 256 MiB aggregate encoded data, and 512 MiB converted CPU payload. Check overflow before allocations. These are not total process/GPU memory guarantees. Parser, decoder and individual conversion stages remain synchronous CPU work with no hard frame-time guarantee.

Use a shared checked accessor reader for offsets, strides, component types, normalized attributes, sparse indices and byte ranges. Convert indexed/nonindexed triangles, strips and fans into triangle lists, one Mesh per source mesh and one SubMesh per primitive. Reject point/line primitives. Load position/normal/tangent/UV0/UV1/color; use existing geometric-normal/derivative-tangent behavior for absent attributes. Reject required UV sets above UV1. Append deformation arrays alongside vertex ranges; preserve every influence set.

Convert to OFG coordinates with C=diag(1,1,-1,1): reflect Z in positions/directions/deltas; use C*M*C for node and inverse bind matrices; convert XYZW quaternions to (-x,-y,z,w); negate tangent W and exchange triangle indices 2/3; preserve scales, UVs and image rows. Convert animation values and derivatives consistently. Decompose matrix nodes to TRS, including negative/zero scales; reject shear using recomposition tolerance 1e-5*max(1,maxAbsElement).

Use PbrMaterialDesc for core material fields and KHR_materials_unlit/emissive_strength/ior/specular/clearcoat/sheen/iridescence/anisotropy plus KHR_texture_transform. Respect per-slot transforms and texCoord overrides. Decode each image once and create textures by source image plus sRGB/linear role; share samplers independently. Add MirroredRepeat and default-true SamplerDesc::useMipmaps (false clamps to base level). Map all filters, defaulting to linear/trilinear. Enforce 12 surface-texture budget. Share one default material. Unknown required extensions fail; optional unsupported features warn and use a usable core fallback. Compression, KTX2, variants, transmission and other unimplemented extensions are excluded.

### Milestone 3: instantiate and bind

ModelData owns source-indexed nodes/scenes and vectors of shared Mesh/Material/Skin/Animation resources plus textures/samplers. ModelNode carries name, local TRS, children, optional mesh/skin indices and optional morph weight override. Model exposes data only after loading and retains import warnings.

Scene::instantiateModel(shared_ptr<Model>, Entity* parent, optional<uint32_t> sceneIndex) returns ModelInstance retaining Model and observing its synthetic root, Animator and source-indexed node map. Select explicit scene, declared default, or scene zero. Without scenes the Model is inspectable but cannot instantiate. Stage all entities first, then mesh renderers/weight overrides, joint bindings and root Animator; reserve destination storage and commit without partial attachment. Add Animator only if a clip targets the selected scene; retain model clips and null mappings outside it. Cameras/lights are omitted with warnings while preserving their nodes. Discarding ModelInstance does not remove scene objects. No scene deletion framework is introduced.

### Milestone 4: laboratory and portability proof

Add native --model <path> and browser ?demo=model&asset=<url>, existing fly camera and laboratory lighting. Report loading failures, warnings, counts and undeformed-preview limitation. Keep PBR grid default. Use small original fixtures for exact tests and pinned licensed samples for visual evidence. Package dependencies and extend smoke-server allowlists. Add tools/model-smoke.mjs for focused browser loading and rendering.

## Concrete Steps

Work from this worktree. Use the x64 Visual Studio environment setup in DEVELOPING.md in the same invocation as native configure/build. Execute `cmake --preset cpu-tests`, `cmake --build --preset cpu-tests --parallel`, and `ctest --preset cpu-tests`. Native integration uses the native-debug configure/build/test presets. Browser uses the documented Emscripten environment and web configure/build preset, followed by `npm.cmd run smoke:web` and `node tools/pbr-smoke.mjs`. `node tools/model-smoke.mjs` is a proposed command until implemented. Do not claim any planned command passed before running it.

## Milestone Review

Milestones 2/3 review: conversion helpers validate source references, preserve Model node domains and convert coordinate spaces consistently. Shared resources contain no instance observers; staging transfers stable scene allocations only after all validation and reservations. Texture/Model share a single cancellable reader and decoder. Parser objects are private and temporary. Tests cover file formats, URI/dependency paths, accessors, topology, material roles, malformed graphs, lifetime and independent instances. Incidental formatting of inherited GPU tests was removed. Final padding/inverse-bind/color corrections have regression tests. No playback/deformation scaffolding was introduced.

Milestone 1 review: factories own immutable CPU data; no resource holds live entities or a parent Model. Setters validate and allocate before replacing bindings. Scene destroys Animators before entities and transfers them during both move operations. Header contracts identify source-node versus palette domains and passive behavior. Shader vertex layout is unchanged, existing three-argument Mesh factories still compile, and new resource code builds without RHI. New tests exercise invalid inputs, independent instances, shared lifetime and scene moves/destruction. No generic registry, ECS update loop, playback placeholder or GPU deformation scaffolding was introduced. docs/resources.md, docs/state-and-scene.md, docs/architecture.md and DEVELOPING.md now distinguish delivered contracts from the future importer.

## Validation and Acceptance

CPU tests must cover factory invalid inputs; independent resource lifetime; multiple influence sets and morph cardinalities; source-index versus palette-index separation; invalid binding atomicity; same/different mesh assignment; shared resources with distinct instances; scene growth/moves/clear; no passive Animator updates; cross-scene and expired observers. Import tests cover byte-range overflow, sparse/normalized accessors, topology, dependency failures/cancellation/retry, extension policy, TRS and coordinate conversion, every animation interpolation and morph/node overrides. Instantiation tests prove reference order independence and unchanged destination on failure.

Use exact index/ownership assertions; 1e-5 absolute tolerance for near-origin CPU conversions and explicit quantization-aware tolerances for encoded values. Native/browser GPU tests verify culling/winding, normal maps, color/data images, sampling and multiple instances. Browser tests hold responses while checking continued frames and ensure cancellation cannot publish late results. Preserve existing diagnostic regressions.

Save inspected captures and diagnostics under artifacts/models. New visual behavior requires both native and browser evidence. No FPS, allocation-residency, full glTF-conformance or coverage-percentage claim is made without measurements.

## Idempotence and Recovery

Preserve inherited edits. Failed Models retain terminal diagnostics until all owners release them; a new request then retries. No partial ModelData is visible. Cancel pending I/O on final-owner release. Scene staging must not alter existing entities on failure. Rebuild only in this worktree's separate native/web directories and never reset unrelated checkouts or dependencies.

## Artifacts and Notes

Baseline patch: artifacts/models/inherited-working-tree.patch (ignored local artifact). Validation commands/results will be appended as run. No tests were run during the preceding planning conversation.

2026-10-04: Executed x64 MSVC developer-shell initialization followed by `cmake --preset cpu-tests`, `cmake --build --preset cpu-tests --parallel`, `ctest --preset cpu-tests` (pass). Full doctest output reports 77 cases / 1807 assertions; focused `--source-file="*model-resources-test.cpp" --no-colors` reports 12 / 196 and is saved under artifacts/models/resource-tests.txt.

2026-10-04: Initialized the pinned RHI submodule in this worktree (revision unchanged), then executed native-debug configure/build (`--parallel 8`) and CTest. All four targets pass; GPU checks take about 33 seconds in total, which is test runtime rather than a rendering performance claim. Native offscreen PBR regression capture is artifacts/pbr/native/offscreen.ppm. New/changed feature C++ files pass clang-format 22.1.3 --dry-run --Werror; git diff --check passes.

2026-10-04: `EMSDK=C:/tools/emsdk`, `cmake --preset web`, `cmake --build --preset web --parallel 8`, `npm.cmd ci`, `npm.cmd run smoke:web` and `node tools/pbr-smoke.mjs` all pass. Chrome reports version 154.0.8037.95. The known Asyncify/WASM-exception build warning remains. Browser reports at artifacts/textures/browser/report.json and artifacts/pbr/browser/report.json have empty error arrays; the documented Windows powerPreference warning is present. Inspected artifacts/pbr/browser/overview.png and artifacts/models/native-regression.png (lossless conversion of the offscreen PPM). They show the intact PBR grid and texture swatches. This validates existing rendering after CPU interface changes, not new model import or native window interactions. This milestone does not claim a new visual feature or a runtime exercise of passive Animator in WASM; its behavior is tested natively and its implementation compiles for web.

Worktree base commit: be53023bb54a0859f492966b37091264e99fa780. The inherited-working-tree patch identifies pre-existing PBR edits separately from this milestone's source changes.

## Interfaces and Dependencies

Skin::create(SkinDesc), Animation::create(AnimationDesc), Mesh::create(..., MeshDeformationData={}), MeshRenderer::setSkin/shared Skin/joint observers and setMorphWeights, Animator::setBindings(shared clips, node observers), Scene::createAnimator and Entity::animator are the first milestone interfaces. Model::data(), Resources::loadResourceAsync<Model>() and Scene::instantiateModel() are implemented in this pass. All resource factories return std::shared_ptr; Scene uniquely owns components; Ptr only observes objects. See the contracts above for node-domain and lifetime rules.

2026-10-04 implementation update: Model loading now stages relative dependencies and uses private pinned tinygltf
callbacks; shared byte/image helpers serve Texture and Model. glTF/GLB conversion, PBR material roles, checked
accessors, coordinate/TRS conversion, Skin/Animation resources and staged Scene instantiation are implemented.
The public Model tables retain source indices, with trailing default material/sampler entries and a separate list
of image/color-role texture variants. The original laboratory fixture is reproducible from
`tools/generate-model-fixture.mjs`; its GLB embeds the checker image. Native --model and browser ?demo=model use
shared inspection setup and display resource counts plus the undeformed-preview limitation.

Milestone 2/3 review: Parser data and callbacks remain private; no keyed child assets or generic import framework
were introduced. Mesh conversion is divided into attribute, influence, morph and topology operations. Every
instance is built in temporary Scene storage and only attached after destination reservations. Shared assets
survive model release; Scene continues to own all entities/components. Matrix import bounds depth at 256 to avoid
unbounded scene recursion. The 512 MiB converted-payload check excludes parser scratch, allocator overhead and
generated shared shader programs; it is not total process residency. This refines the initial budget wording.

Validation so far: native configure/build and all four CTest checks passed, including the new imported-model GPU
case (two instances/four visible cubes with culling). Inspected artifacts/models/native/instances.png, converted
losslessly from the test's PPM. Initial CPU import tests passed. Added further data-URI, embedded-GLB-image,
normalized/sparse/matrix, strip/fan/multiple-primitive and malformed-container cases afterward; final checks pending.
The first web compile exposed nlohmann/json's unsigned-char iterator incompatibility with the installed libc++.
Passing the JSON span as char pointers fixes that boundary without modifying vendored headers. Web link and
browser runtime verification remain in progress.

2026-10-04 final review discoveries: glTF matrix padding is required at column starts but optional after the final
column; the checked reader now permits that final omission. Inverse-bind accessors may contain more matrices than
a particular skin consumes; import now requires at least the palette length and preserves palette order. Floating
vertex colors clamp to [0,1] without coordinate reflection. Focused regression tests cover all three cases. These
rules were checked against the Khronos 2.0 specification linked in docs/resources.md.

Native window verification uses the owned process only, loads laboratory.glb by absolute path from the Windows temp
directory, resizes, minimizes/restores and closes cleanly. stdout names NVIDIA GeForce RTX 3050 Ti Laptop GPU;
stderr is empty. A DPI-aware PrintWindow capture at artifacts/models/native/window.png was inspected. The first
screen-copy attempt captured an obscuring unrelated window, so it was replaced with window-targeted capture and
is not used as evidence. The native offscreen multi-instance capture and browser model captures were also inspected.

Formatting review removed incidental formatting changes to the inherited GPU tests. New/changed feature sources
pass clang-format 22.1.3 --dry-run --Werror using the authoritative file configuration; no vendored headers were
formatted. The final browser link includes the matrix/color fixes and is followed by model, texture/checkerboard
and PBR regression scripts. The known Asyncify/WASM-exception warning remains.

2026-10-04 final CPU evidence: `cmake --build --preset cpu-tests --parallel 8` and `ctest --preset cpu-tests` pass.
The full doctest executable reports 90 cases / 2115 assertions; importer-only output reports 13 cases / 308 assertions.
Saved as artifacts/models/core-tests.txt and artifacts/models/import-tests.txt. Native-debug build and all four
CTest targets pass after the final functional changes; subsequent changes only apply the authoritative formatter.
Native model window verification passes from a foreign working directory. No FPS or resident-memory claim is made.

2026-10-04 browser verification: the final web build succeeded; index.html, index.js and index.wasm are present.
`node tools/model-smoke.mjs` passed external glTF, embedded-image GLB, resize, continued frames during held dependency
fetches, cancellation without late publication, and invalid-model diagnostics without publication. Chrome is
154.0.8037.95; the model report has no unexpected errors. `npm.cmd run smoke:web` passed texture/checkerboard
regressions. The first final PBR smoke failed its unchanged image-content assertion after resize: its screenshot
contained only the canvas's cleared background, while the helper waited on cumulative frames from before resize.
The capture helpers now wait for three fresh submitted frames as well as readiness, rather than sleeps. Image
assertions remain unchanged. Saved failing evidence is artifacts/models/pbr-resize-before-wait.png and
artifacts/models/pbr-smoke-before-wait.json. PBR/model reruns are in progress.

2026-10-04 final result: web configure/build and all three runtime suites pass: `node tools/model-smoke.mjs`,
`npm.cmd run smoke:web`, and `node tools/pbr-smoke.mjs`. After the fresh-frame capture fix, both PBR and model suites
were rerun successfully with unchanged content assertions. Reports under artifacts/models/browser,
artifacts/textures/browser and artifacts/pbr/browser contain no unexpected errors. The deliberately invalid Model
has a separate expected-failure log and never publishes or reaches textureReady. Final model and resized PBR
screenshots were inspected. Known compiler Asyncify/WASM-exception and Chrome Windows powerPreference warnings
remain; no new rendering validation diagnostics were recorded. Parsing and instantiation requested in this pass
are complete. Broader external-asset/reference coverage remains the active plan follow-up.
