# Render a procedural cube through mesh resources, materials and scene components

This ExecPlan follows [PLANS.md](../../PLANS.md) and [AGENTS.md](../../AGENTS.md). **Status: proposed, for review. No implementation code has been changed for this slice.** The user explicitly requested planning before implementation. Earlier state/scene/resource work remains uncommitted and must be preserved.

## Purpose / Big Picture

Create a cube mesh on demand, attach it to a scene-owned MeshRenderer, and render it through a material referencing the checkerboard shader. The cube should appear in the native D3D12 window and browser WebGPU canvas. The important result is a working ownership and draw path that can later support loaded meshes and textures without replacing the foundations.

The visible fixture will demonstrate material instancing: two entities share one cube mesh; one renderer overrides a material slot with a tinted clone. Editing that clone must not change the other entity or the mesh's default materials. Keep the scene stationary for clear inspection and deterministic tests.

## Progress

- [x] (2026-10-01) Confirmed mesh/submesh, renderer material-slot, material-cloning and shader/pipeline-cache design with the user.
- [x] (2026-10-01) Inspected the old mesh/mesh-renderer headers, current hosts/checkerboard, and pinned RHI input-layout/draw interfaces.
- [x] (2026-10-01) Wrote this reviewable plan before changing implementation code.
- [x] (2026-10-01) Incorporated the user's static Renderer with a drawScene entry point borrowing the scene and extracting renderer components/world transforms.
- [ ] Review the proposed first-slice boundaries with the user.
- [ ] Implement and test procedural assets and component ownership.
- [ ] Implement indexed rendering, shader-owned GPU state and material parameters.
- [ ] Integrate the cube fixture in native/browser hosts, verify both, and update documentation.

## Surprises & Discoveries

The current checkerboard is a screen-filling triangle generated from vertex IDs. Its vertex stage cannot draw a cube unchanged: geometry needs vertex/index buffers, a vertex layout, transforms and a depth attachment. Its checker pattern and original regression test remain useful.

The backup mesh already uses a shared vertex buffer, a shared index buffer and submeshes containing starting index/count and default material. The old MeshRenderer also contains skinning, property bags and sorting concerns; these are outside this slice.

The new Resource base currently assumes cooperative loading through a virtual loadStep. Procedurally created, immediately ready assets need a small explicit construction path; they should not pretend to perform asynchronous file loads. This must preserve the existing async resource tests and shared/weak ownership contract.

The GPU-independent core must remain buildable without Slang RHI. Resource ownership of GPU allocations cannot be implemented by adding RHI headers or linkage to that target.

## Decision Log

User decisions: a static Renderer exposes drawScene, borrowing a scene pointer and orchestrating mesh-renderer extraction, world transforms and GPU draw setup. MeshRenderer retains a shared mesh. Mesh owns vertex/index data and submesh ranges, with default materials corresponding to the ranges. MeshRenderer has corresponding material slots for per-instance choices. Material supports explicit cloning; arbitrary resource cloning is not required. Materials share their shader and texture dependencies. Shader owns the compiled program and a cache of pipelines for the render configurations used with that program.

Proposed first-slice scope: a unit cube, one vertex format (position, normal, UV), 32-bit triangle indices, a checkerboard shader, an RGBA tint parameter, one colour target, depth testing, and opaque single-sample rendering. Six cube faces provide six submeshes so material-slot behavior is observable. One default material may be shared by all six faces. Do not add texture loading, lighting components, generic parameter/property systems, skinning, editable mesh buffers or file import yet.

Proposed camera boundary: use a fixed view/projection in the diagnostic fixture initially. A scene Camera component remains a subsequent small step; it is not required to prove the three requested mesh/material/MeshRenderer pieces. Keep view/projection passed explicitly to the scene renderer so adding the camera does not change mesh ownership.

Proposed shader boundary: retain the original checkerboard entry points and add mesh entry points using the same checker-pattern helper. A mesh material references the mesh-capable shader resource. Do not claim the existing screen-triangle program or pipeline can be reused unchanged for indexed geometry.

Proposed resource boundary: CPU geometry, material values and shader description stay in ofg-core. Private forward-declared graphics-state objects, created by the graphics implementation, hold the mesh's RHI buffers and shader's program/pipelines. Their ownership remains on the asset, not an independent permanent owning registry. This small boundary preserves CPU-only testing without introducing another graphics interface. Use standard smart pointers for private state whose complete type is only available in the graphics target. The static Renderer may access that state to prepare and bind it, but must not become a permanent owner of scene assets.

## Outcomes & Retrospective

Planning only. The implementation, signatures below and visual fixture are proposals. Current verification still applies only to the existing state/scene/resource foundation and checkerboard, not to cube rendering or materials.

## Contract and Quality Baseline

### Mesh and submeshes

Mesh derives from Resource and owns immutable CPU vertices, indices and submeshes. Each submesh has a first index, index count and shared default material; index ranges address the index buffer, whose entries address vertices. Validate nonempty geometry, finite vertex attributes, valid vertex indices, triangle-aligned ranges and overflow-safe bounds. Do not introduce a base-vertex offset or a general topology system before there is a use for them.

The cube factory creates a new ready mesh on demand. Share that returned pointer between renderers when sharing geometry; creating procedural geometry is not a clone operation. Use a side length of one, centered at the origin, with 24 vertices (independent UVs/normals on each face), 36 indices and six six-index submeshes. Validate winding and normals independently of the initial culling choice. Materials are held strongly by submeshes so defaults remain alive while the mesh exists.

Resource readiness means the validated CPU asset description is available. GPU buffers/programs are prepared lazily by the graphics layer on first use and cached on the resource. GPU preparation errors must propagate clearly as rendering failures; do not silently mark a failed upload as successful. Document this boundary explicitly instead of implying CPU readiness proves GPU preparation.

### Materials

Material derives from Resource, retains a shared Shader and exposes only the material values actually used by this shader: initially a finite RGBA tint. Its clone operation copies values and shares dependencies. A clone is a distinct, ready material outside the original asset's lookup-cache identity. Mutating a shared material deliberately affects every renderer using that same object; cloning first provides isolation. No implicit clone-on-access or copy-on-write behavior.

MeshRenderer owns a vector of nullable shared material overrides aligned to submesh slots. A null slot falls back to that submesh's default. Provide a clear slot setter/accessor and reject an out-of-range slot. Replacing the mesh resets overrides to its new submesh count, avoiding accidental application of old slots to different geometry. Do not duplicate fallback defaults into strong override entries merely to express inheritance.

### Scene components

MeshRenderer derives from Component. Scene owns a typed vector of unique MeshRenderer allocations, Entity has a non-owning typed pointer, and component allocation remains stable. Enforce at most one MeshRenderer per entity and reject foreign entities. Preserve scene moves, clear and observer invalidation; clear components before destroying entities. MeshRenderer owns its shared mesh and override materials and borrows its associated entity through the existing Component contract.

Transforms remain on Entity. The rendering layer traverses the scene's renderer components, obtains each entity's world transform, and draws each submesh with its effective material. No generic ECS registry, command-buffer framework or property bag is needed.

### Shader and pipeline ownership

Shader derives from Resource and represents the program's source/entry-point identity. Its private GPU state retains the RHI program and any input layout/pipelines it creates. Materials keep shaders alive through shared references. Cloned materials reuse those same objects.

Use a small pipeline cache scoped to the shader and device. The key must distinguish every configuration that can vary in this implementation. Initially vertex layout, triangle topology, depth format/state, blending and sample count are fixed and documented; the colour target format varies between offscreen tests and presentation. Do not build a generalized variant system now. Before introducing a second vertex layout or mutable render-state setting, make it part of the key and add a separation/reuse test. Never reuse device-specific GPU objects on another device.

The shader material parameter block contains the tint, and the per-draw block contains the transform needed for the vertex stage. Use explicit, verified packing and RHI's shader cursor. Do not assume CPU matrix storage matches shader layout without checking it. Material value changes must not manufacture new pipelines.

### Static renderer and platform boundary

Renderer exposes a static drawScene entry point taking a borrowed const Scene pointer. Reject null explicitly and never retain the scene across calls. Device/queue association belongs to an explicit renderer initialization/shutdown lifecycle. The current target and view/projection are explicit draw inputs, either direct arguments or one small draw context if that keeps the call readable. The scene does not store swap-chain or canvas objects.

For each call, Renderer starts the colour/depth pass, iterates scene-owned MeshRenderer components, resolves their entity world transforms and effective materials, ensures required mesh/shader GPU state exists, selects a cached pipeline, binds buffers/parameters and issues indexed submesh draws. It then ends the pass and submits commands. Missing or pending assets produce no draw; a failed asset or failed GPU preparation must produce a useful diagnostic instead of masquerading as a successful rendered object. Define and test this policy during implementation.

The hosts acquire their presentation image, call Renderer::drawScene with the scene and target, and present. They retain window/canvas, resize events and native/browser event-loop ownership. Renderer owns temporary frame/depth resources and orchestration; resources own geometry buffers and shader program/pipeline caches. Static access does not imply permanent strong ownership of every mesh or material ever drawn.

Start with one active device per renderer lifecycle. Shutdown must release renderer-owned state in the correct order and preserve the existing native GPU drain. Retained asset GPU state is device-specific; reinitializing on another device requires deliberate re-preparation or a clear rejection, never reuse of old device objects.

### Frames and lifetime

Depth storage follows target dimensions and is recreated on resize. Mesh buffers are uploaded once per device preparation, not each frame. Retain GPU resources through in-flight commands: inspect the pinned RHI's reference retention for pipeline, buffers and shader bindings, and verify release-after-submit behavior. If that path does not retain an object needed by the GPU, add only the necessary frame-owned retention. Do not use a global owning asset cache to solve this.

Native teardown continues to drain submitted work. Browser frames continue to return to the browser event loop without a blocking wait. No browser-only test-suite port, hidden worker pool or new device backend is required.

## Context and Orientation

Existing files: src/resources/resource.h and resources.h define pollable shared resources and weak static lookup; src/scene owns entity hierarchy and the component base; src/math provides transforms. src/checkerboard.cpp and shaders/checkerboard.slang implement the current screen triangle. src/main.cpp and src/web-main.cpp are the platform hosts. tests/checkerboard-test.cpp is the native GPU regression; tools/browser-smoke.mjs verifies the current browser checkerboard.

Proposed additions are src/resources/mesh, material and shader headers/sources; src/scene/mesh-renderer; and a small src/render/renderer static facade plus shared cube-fixture setup. Keep source placement consistent with existing hyphenated filenames and avoid empty framework directories. Exact helper names can be settled during implementation without changing these ownership boundaries.

## Plan of Work

### 1. CPU assets and scene ownership

Add the explicit ready-resource construction path, mesh/submesh data, cube factory, material clone/tint and shader description. Add typed scene storage and entity access for MeshRenderer. Unit tests must establish geometry validity, default/override resolution, clone isolation, shared dependency lifetime, clear/move behavior and invalid component allocation. The CPU preset must still configure without the graphics submodule.

### 2. Indexed rendering and GPU preparation

Prepare mesh vertex/index buffers and shader program/input layout/pipelines lazily in the graphics target. Extend the Slang shader with mesh vertex inputs and a material-aware checker fragment stage, retaining the existing screen-triangle path. Add the static Renderer::drawScene entry point with a borrowed scene pointer, explicit view/projection and target, and explicit device lifecycle. Use depth testing and draw each submesh's index range with its material tint.

Test offscreen output through RHI, including a format change exercising pipeline caching, changing a material value without a new pipeline, two objects sharing one mesh, independent transforms, and releasing caller handles after submission. Use counters or inspectable cache metadata only where they verify actual reuse; do not build a general telemetry layer.

### 3. Two-host fixture and evidence

Make the small cube scene the normal demonstration in both hosts, with a way to select the original checkerboard diagnostic. Keep the finite native device-startup mode. Prefer a simple native command-line option and browser query selector over building UI controls for this step.

Retain the original exact checkerboard GPU test and browser smoke path. Add focused cube checks: visible geometry against clear background, multiple checker cells, expected tint isolation, resize and reload with no validation/console errors. Use interior pixel regions and tolerances appropriate to the declared target format; do not require pixel equality at rasterized edges across backends. Inspect and present fresh native and browser screenshots. The scene remains stationary; no character, controls or animation is required yet.

### 4. Review and documentation

Review ownership and resource release, invalid mesh handling, clone behavior, cache keys/device scope, shader packing, scene lifetime, browser portability and unnecessary abstractions. Update architecture, resource and scene contracts, build/run instructions and applicable skills. Record tests and screenshot paths. Only then mark this plan complete and archive it.

## Concrete Steps

After implementation is authorized, use the documented Visual Studio x64 environment and existing build-native/build-web skills. From the repository root run:

    cmake --build --preset cpu-tests --parallel
    ctest --preset cpu-tests
    cmake --build --preset native-debug --parallel
    ctest --preset native-debug
    cmake --build --preset web
    npm.cmd run smoke:web

These are existing commands, but the new cube tests/fixture are not implemented yet. Extend the appropriate targets and smoke script, rather than inventing a parallel build workflow. Browser linking may take longer because the existing build includes the Slang compiler.

## Milestone Review

Planning review: this plan preserves the user's material and mesh ownership decisions, separates resource readiness from GPU preparation, keeps the CPU build independent and acknowledges the new vertex/depth work. Concrete asset loading, camera components, texture sampling and generalized pipeline variants are explicitly outside the proposed first slice. Implementation review and new validation evidence remain pending.

## Validation and Acceptance

Acceptance requires valid cube geometry and submesh ranges; one scene-owned renderer per entity; tested material default/override and clone semantics; demonstrable shared mesh/program/pipeline reuse; native offscreen depth/index/material tests; and a visibly correct native and browser scene through resize/reload. Existing 48 CPU tests, device startup and checkerboard checks must remain passing.

Use scenario-based coverage, with exact ownership/state assertions and declared floating-point/pixel tolerances. Do not claim a coverage percentage, performance budget, actual file loading or unsupported GPU lifecycle behavior from this fixture. Save evidence under artifacts/cube and artifacts/browser-smoke; inspect images as well as test summaries.

## Idempotence and Recovery

Preserve the backup, pinned Slang RHI, separate build trees and earlier uncommitted changes. Keep the original checkerboard diagnostic intact to isolate geometry/material failures. Do not publish, change dependency revisions or add toolchain installations for this slice without a demonstrated requirement.

## Artifacts and Notes

Source references inspected: the backup's mesh.hpp and mesh_renderer.hpp, current checkerboard/host/test sources, and RHI's triangle example plus shader-program, input-layout and render-pipeline descriptors. This plan creates no new runtime evidence.

## Interfaces and Dependencies

Ownership: Scene -> unique MeshRenderer -> shared Mesh; Mesh -> shared default Materials; MeshRenderer -> shared override Materials; Material -> shared Shader and later textures. Mesh and Shader own their private device-specific GPU state. Shader GPU state -> RHI program/input layout/pipeline cache. Static Renderer borrows Scene per draw and orchestrates passes/bindings/submission; platform hosts own presentation. No owning back-reference is required.

Use C++20, existing math, shared/unique pointers, doctest, Slang RHI and the current native/browser hosts. No new external package is proposed.

Revision note, 2026-10-01: incorporated the user's static Renderer / borrowed-scene drawScene design. This remains planning only; no implementation code has been changed for this slice.
