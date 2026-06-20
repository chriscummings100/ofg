# Build C++ mutable render assets and an opaque draw-list renderer

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

Replace the current bootstrap triangle renderer with the first reusable C++ rendering slice for OFG. After this work, `npm run dev`, `npm run smoke:browser`, and `npm run smoke:render` should show and verify a simple 3D scene: a large ground plane plus several cubes at different depths, with browser frames animating cube rotation and vertical motion over time. The scene should be submitted through a draw list rather than through hard-coded bootstrap draw calls.

The renderer should use a standard asset-handle model. C++ owns mutable asset records in typed stores. Materials, meshes, textures, shaders, and draw commands refer to assets by typed handles, not by ownership graphs and not through one large manager object with every possible method.

The first asset types are:

Texture: pixel data created from generated or caller-provided RGBA8 pixels, with linear or sRGB formats and explicit mip-map policy. Textures can update their pixels after creation and keep their WebGPU texture, view, and sampler in sync for the single active device. General image decoding can wait for a later asset-loading plan unless it stays small, pinned, and fully covered in this plan.

Shader: WGSL source plus explicit parameter and pipeline schemas. Shaders own their shader module for the active device and can be replaced during development.

Material: a shader handle plus a property bag containing named shader parameters, including texture handles. Materials own material uniform and bind-group state when GPU-ready.

Mesh: vertices, indices, and submesh ranges, where each submesh has a default material handle. Meshes can update vertices or indices after creation and keep their vertex and index buffers in sync.

PropertyBag: a shared named-value structure used by materials and draw commands. A draw command uses this for per-command properties such as `model`, with room for object id, instance tint, animation phase, or later render parameters.

This is not intended to become a full asset database yet. The goal is a concrete, mutable, tested resource interface that feels like a game engine: assets live in stores, callers hold typed handles, and the renderer consumes handles and property bags.

## Progress

- [x] (2026-06-20 18:05Z) Re-read `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `README.md`, `AGENTS.md`, `cpp/CMakeLists.txt`, `package.json`, and the archived pre-migration renderer/resource plan.
- [x] (2026-06-20 18:05Z) Confirmed the active runtime is C++/WASM with TypeScript as a narrow host, browser WebGPU through Emdawnwebgpu, native offscreen rendering through pinned Dawn, C++ tests through doctest/CTest, and coverage through Clang/LLVM.
- [x] (2026-06-20 18:05Z) Drafted this C++-first replacement plan at `C:\dev\ofg\docs\plans\cpp-renderer-resources-pipeline-plan.md`.
- [x] (2026-06-20 18:15Z) Reviewed this plan through local correctness, completeness, clarity, efficiency, performance, contract, code-quality, legacy, and validation passes during Milestone 6 of the C++/WASM migration.
- [ ] Re-review and refine this plan before implementation if resource ownership, shader variant, asset loading, or scope expectations change.
- [ ] Milestone 1: add CPU-side C++ typed handles, stores, asset records, property bags, validation, and doctest coverage.
- [ ] Milestone 2: add single-device WebGPU state and mutation/update methods to Texture, Shader, Material, and Mesh.
- [ ] Milestone 3: replace the BootstrapRenderer path with an opaque-pass renderer that consumes DrawList and asset stores.
- [ ] Milestone 4: build the animated plane-and-cubes demo scene in C++ and integrate it into browser and native smoke.
- [ ] Milestone 5: update docs, API contracts, visual smoke contracts, screenshots, coverage records, and any package/deployment assumptions.

## Surprises & Discoveries

- Observation: The useful design from the archived renderer/resource plan is the asset-handle model, not its implementation path.
  Evidence: The archived plan defines typed handles, generic stores, mutable Texture/Shader/Material/Mesh records, PropertyBag, DrawList, OpaqueRenderer, and an animated plane-and-cubes acceptance scene. Its old package and command references no longer match the active C++ project.

- Observation: The current C++ renderer already shares bootstrap scene data between browser and native smoke.
  Evidence: `cpp/include/ofg/render/bootstrap_scene.hpp`, `cpp/include/ofg/render/bootstrap_renderer.hpp`, browser runtime code, and the native Dawn smoke are all built by `cpp/CMakeLists.txt` and validated by `npm run smoke`.

- Observation: The current public contracts still name the bootstrap triangle visual contract, so this plan must intentionally update them in the milestone that changes the visible scene.
  Evidence: `docs/API_CONTRACTS.md` currently defines OFG-BOOT-004 and OFG-BOOT-005 around the dark blue-gray clear color and red/green/blue bootstrap triangle.

## Decision Log

- Decision: Build the renderer/resource pipeline in C++20 under the existing `cpp/` tree.
  Rationale: The active runtime, build, test, coverage, browser WebGPU, and native Dawn smoke paths are now C++-first. Creating a separate language or runtime boundary would reintroduce the migration problem this project just removed.
  Date/Author: 2026-06-20 / Codex

- Decision: Use typed handles into typed stores instead of shared ownership graphs, bespoke per-resource managers, or one large resources API.
  Rationale: Assets need to remain mutable after creation. Typed stores give stable handles, `get`, `get_mut`, and removal semantics without multiplying manager functions across every future asset type.
  Date/Author: 2026-06-20 / Codex

- Decision: Assume one active WebGPU device for this milestone and allow assets to hold GPU state for that device.
  Rationale: OFG currently creates one browser device or one native smoke device. Storing prepared GPU state on the asset is simpler than a multi-device prepared-resource cache. Future device-loss or multi-device work can split CPU asset data from prepared GPU resources later.
  Date/Author: 2026-06-20 / Codex

- Decision: Do not make the device global.
  Rationale: Browser runtime and native smoke own the active `WGPUDevice` and `WGPUQueue`. Asset mutation methods should receive an explicit borrowed GPU context only when they need GPU work.
  Date/Author: 2026-06-20 / Codex

- Decision: Keep pipeline caching in the renderer, not in generic asset stores.
  Rationale: Render pipelines depend on shader revision, variant, target color/depth formats, vertex layout, primitive state, depth state, sample count, and bind group layouts. They are render-state combinations rather than standalone assets.
  Date/Author: 2026-06-20 / Codex

- Decision: Define shader parameter schemas explicitly beside each Shader instead of parsing WGSL reflection in this milestone.
  Rationale: WebGPU and `webgpu.h` do not provide a stable WGSL reflection API. Explicit schemas give deterministic name-to-offset behavior and are easier to cover with doctest cases.
  Date/Author: 2026-06-20 / Codex

- Decision: Match the current C++ error-reporting style for public renderer/resource factories unless a tiny local expected-style helper earns its keep.
  Rationale: Existing C++ renderer code returns `std::unique_ptr` or `bool` and fills a caller-provided `std::string& error`. The resource pipeline should not introduce a broad result framework by accident. If implementation creates a small `Expected` helper, it must be documented, covered by doctest, and used consistently enough to remove real complexity.
  Date/Author: 2026-06-20 / Codex

- Decision: Use small local math types for the initial camera, transform, and vector data unless implementation proves that a dependency is justified.
  Rationale: The first scene only needs matrices, vectors, perspective projection, and basic transforms. A local, tested math slice is enough for Milestone 4 and avoids adding a dependency before OFG knows the shape of its scene/physics math.
  Date/Author: 2026-06-20 / Codex

- Decision: Require purpose comments or doc comments for every function and detailed file headers for new files in this plan.
  Rationale: `AGENTS.md` requires function comments and top-of-file purpose comments. Renderer/resource code will introduce enough types and ownership rules that comments are part of correctness, not polish.
  Date/Author: 2026-06-20 / User and Codex

## Outcomes & Retrospective

Implementation has not started. At completion, record whether the asset-handle model was sufficient for the animated demo, which interfaces needed simplification, which validation artifacts were produced, whether the comments/readability rule stayed satisfied without last-minute cleanup, and whether any coverage or browser-smoke exceptions remain.

## Contract and Quality Baseline

This plan must preserve or intentionally update the active contracts in `C:\dev\ofg\docs\API_CONTRACTS.md`.

OFG-BOOT-001 TypeScript Host Ownership is preserved. TypeScript may keep creating the canvas, resizing it, loading WASM, and displaying errors. It must not own asset stores, mesh/material/texture/shader mutation, GPU pipeline setup, scene data, or draw submission.

OFG-BOOT-002 C++ Runtime Ownership is preserved and expanded. C++ continues to own frame state, scene data, WebGPU resources, renderer setup, draw submission, browser runtime behavior, and native Dawn offscreen rendering. The hard-coded bootstrap scene becomes a C++ demo scene built from mutable asset stores.

OFG-BOOT-003 WASM Facade is preserved. The browser facade should still expose create, resize, frame, debug status, and dispose. The existing `frame(time_ms)` input supplies time to C++; this plan does not require a new TypeScript render API.

OFG-BOOT-004 Renderer Compatibility must be rewritten in the milestone that replaces BootstrapRenderer. The old shared-triangle contract should become a shared opaque-renderer contract: browser and native smoke must use the same C++ resource layer, shader source, demo scene builder, draw-list renderer, clear color, and visual smoke expectations. Allowed differences remain the final output target and adapter/surface format.

OFG-BOOT-005 WebGPU Baseline must be intentionally updated. The old "one render pipeline for the bootstrap scene" rule no longer fits materials and shader variants. The new baseline should still request no optional GPU features and no manual limits above adapter defaults, but it may create durable pipelines, bind groups, buffers, textures, samplers, and a depth texture during initialization, explicit asset mutation, renderer construction, or resize.

OFG-BOOT-006 Resource Lifetime remains important. Shaders, buffers, textures, samplers, bind groups, and pipelines must not be recreated on ordinary frames unless a caller explicitly mutates an asset or the render target is resized/reconfigured. Ordinary frames may update frame and draw uniform buffer contents and submit draw calls.

OFG-BOOT-007 Generated Artifacts is preserved. New screenshots and local smoke output should live under `C:\dev\ofg\artifacts`, not in source-controlled generated directories.

OFG-BOOT-008 Deployment is preserved unless the generated C++ WASM/JS paths change. This plan should not change Cloudflare packaging unless it deliberately adds runtime asset files that must be copied.

OFG-BOOT-009 Coverage is preserved. Modified implementation files should meet the coverage gate unless this plan records a specific exception with rationale. Browser-only WebGPU code continues to be validated by WASM builds, TypeScript adapter tests, and browser smoke.

Quality constraints from `C:\dev\ofg\AGENTS.md` apply: every function written should have a doc string or purpose comment, functions over 50 lines should contain internal comments explaining their workings, files should have maintained top comments, and files in the 500-1000 line band should be considered for splitting before they grow further.

## Context and Orientation

The repository root is `C:\dev\ofg`. It is now a C++/WASM runtime with a TypeScript browser host.

`C:\dev\ofg\cpp\CMakeLists.txt` builds the portable core library, doctest executable, browser Emscripten module, and native Dawn render-smoke executable. C++ browser builds use Emscripten, Embind, and Emdawnwebgpu. Native render smoke uses pinned Dawn through `tools/setup-dawn.mjs` and `tools/smoke-render-cpp.mjs`.

`C:\dev\ofg\cpp\include\ofg\render\bootstrap_renderer.hpp` and `C:\dev\ofg\cpp\src\render\bootstrap_renderer.cpp` contain the current hard-coded bootstrap renderer. It creates one shader module, one render pipeline, and one vertex buffer for the red/green/blue triangle.

`C:\dev\ofg\cpp\include\ofg\render\bootstrap_scene.hpp` and `C:\dev\ofg\cpp\src\render\bootstrap_scene.cpp` own the current deterministic triangle vertices and clear color. These are native-checkable and covered by doctest.

`C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp` and `C:\dev\ofg\cpp\src\web\browser_game.cpp` own browser WebGPU setup, surface configuration, resize behavior, frame submission, and lifecycle.

`C:\dev\ofg\cpp\include\ofg\native\render_smoke.hpp`, `C:\dev\ofg\cpp\src\native\render_smoke.cpp`, and `C:\dev\ofg\cpp\src\native\render_smoke_main.cpp` own browser-free native rendering through Dawn. `render_smoke.cpp` is already in the 500-1000 line concern band and should be split before this plan adds more native smoke behavior.

Definitions used in this plan:

Asset: a mutable game/render resource such as Texture, Shader, Material, or Mesh.

Handle: a small typed id into a Store, such as `Handle<Texture>`. Handles are copied into materials, meshes, and draw commands. They are not the asset itself.

Store: a generic container that owns assets of one type and exposes insert, lookup, mutable lookup, and removal.

Assets: a lightweight aggregate of stores, such as `textures`, `shaders`, `materials`, and `meshes`. It should not forward every resource-specific method.

GpuContext: a simple borrowed context containing the active `WGPUDevice` and `WGPUQueue`, or equivalent method arguments. It is not global.

PropertyBag: a named collection of shader parameter values. Materials and draw commands both use it.

Draw list: an ordered or sortable collection of draw commands passed to the renderer for one frame.

Draw command: one mesh instance to render, with a mesh handle, per-command properties such as world transform, optional material override handles, and sort metadata.

Opaque pass: the first render pass for non-transparent geometry. It should clear color and depth, sort front-to-back or use an explicitly documented stable-order policy, bind each command's shader, material, mesh, and draw uniforms, then issue indexed draw calls.

Uber shader: one shader source that supports multiple compile-time or pipeline-time variants, such as textured versus untextured material rendering.

## Plan of Work

Milestone 1 adds the CPU-side asset model. Add resource headers under `C:\dev\ofg\cpp\include\ofg\resources\` and source files under `C:\dev\ofg\cpp\src\resources\`. Implement `Handle<T>`, generic `Store<T>`, `Assets`, `Texture`, `Shader`, `Material`, `Mesh`, `SubMesh`, `PropertyBag`, `PropertyValue`, and validation errors. In this milestone, assets can be constructed and mutated as CPU data; GPU state can be absent or represented by explicit empty state types that are filled in Milestone 2. This milestone should not require a GPU adapter to test.

Milestone 2 adds single-device GPU state to the asset types using `webgpu.h`. Texture creation/update methods upload or rewrite texture data. Shader creation/replacement creates a shader module. Material creation/update validates its property bag against its shader and creates or refreshes material uniform and bind-group state. Mesh creation/update creates or refreshes vertex and index buffers. These methods take a `GpuContext` or explicit `WGPUDevice` and `WGPUQueue`; the device is not stored globally. Resource mutation is eager: when a method changes pixels, vertices, shader source, or material properties, it updates the related GPU state in the same call.

Milestone 3 introduces reusable renderer interfaces under `C:\dev\ofg\cpp\include\ofg\render\` and `C:\dev\ofg\cpp\src\render\`. Add draw-list, camera/render-view, opaque-renderer, pipeline-cache, shader-layout, and demo-scene modules or similarly named files. Replace BootstrapRenderer rather than preserving it as a public compatibility interface. The renderer should own pass-level resources: frame uniform buffer, dynamic draw uniform arena, depth texture, and pipeline cache. The draw list should reference handles into `Assets`. Per-draw uniforms must use dynamic offsets, a per-frame uniform arena, storage-buffer indexing, or another explicit strategy that prevents all draws from seeing the last model matrix.

Milestone 4 replaces the visible bootstrap triangle with the animated demo scene. Build a ground plane mesh, a cube mesh, a generated checker texture, a basic opaque uber shader, and several draw commands whose per-command PropertyBag includes model transforms derived from frame time. Use deterministic demo constants for camera pose, field of view, near/far planes, ground size, cube sizes, cube positions, cube colors, and the native smoke frame time.

Milestone 5 updates remaining docs, smoke expectations, coverage artifacts, and screenshots. Update `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` in the milestone where each contract changes, then do a final consistency pass here for the resource model, `OpaqueRenderer`, and DemoScene. Update `C:\dev\ofg\tools\smoke-contract.json` and smoke pixel classification so a blank frame, missing depth, missing ground, or missing cube colors fail clearly.

After each milestone, run the repo-local `milestone-review` skill before marking that milestone complete. Apply required findings or record a rejected finding with rationale in this plan's Decision Log.

## Concrete Steps

From `C:\dev\ofg`, create the C++ resource module files and wire them into `cpp/CMakeLists.txt`:

    cpp/include/ofg/resources/handle.hpp
    cpp/include/ofg/resources/store.hpp
    cpp/include/ofg/resources/assets.hpp
    cpp/include/ofg/resources/texture.hpp
    cpp/include/ofg/resources/shader.hpp
    cpp/include/ofg/resources/material.hpp
    cpp/include/ofg/resources/mesh.hpp
    cpp/include/ofg/resources/property_bag.hpp
    cpp/include/ofg/resources/resource_error.hpp
    cpp/src/resources/texture.cpp
    cpp/src/resources/shader.cpp
    cpp/src/resources/material.cpp
    cpp/src/resources/mesh.cpp
    cpp/src/resources/property_bag.cpp
    cpp/src/resources/resource_error.cpp

Add doctest files and register them in the existing `ofg_cpp_tests` target:

    cpp/tests/resource_store_test.cpp
    cpp/tests/property_bag_test.cpp
    cpp/tests/texture_resource_test.cpp
    cpp/tests/shader_resource_test.cpp
    cpp/tests/material_resource_test.cpp
    cpp/tests/mesh_resource_test.cpp
    cpp/tests/opaque_renderer_test.cpp
    cpp/tests/demo_scene_test.cpp

Milestone 1 validation:

    npm run test:cpp
    npm run coverage:cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 2 validation:

    npm run test:cpp
    npm run build:wasm
    npm run smoke:render
    npm run coverage:cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 3 validation:

    npm run test:cpp
    npm run smoke:render
    npm run smoke:browser
    npm run coverage:cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 4 validation:

    npm test
    npm run smoke
    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 5 final validation:

    npm test
    npm run smoke
    npm run coverage
    npm run build:cloudflare
    git -c safe.directory=C:/dev/ofg diff --check

For browser or visual work, keep a dev server available for human review:

    npm run dev

Report the URL printed by the server. If port 5173 is busy, use the alternate URL printed by the tool. Take and share screenshots after the first 3D render appears, after animation/material changes, and before finalizing.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, screenshots, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

Milestone 1 is accepted when C++ exposes documented CPU-side `Handle<T>`, `Store<T>`, `Assets`, `Texture`, `Shader`, `Material`, `Mesh`, `PropertyBag`, and `PropertyValue` types. Doctest coverage must cover typed handle allocation, stale handle rejection after removal, lookup and mutable lookup, texture format mapping, sRGB versus linear selection, pixel-array validation, mip count or CPU mip generation, generated pixel texture construction, shader parameter lookup, property value type validation, material property validation, draw-scope property validation, and mesh submesh range validation. If general image-byte decoding is added, its success and failure paths must be covered too.

Milestone 2 is accepted when asset methods can eagerly create and update GPU state for the single active device. Native or browser-backed tests and smokes should cover texture upload/update, shader module creation/replacement, material uniform/bind-group creation, mesh buffer creation/update, and that explicit asset mutation changes GPU resources while ordinary read-only frames do not recreate durable assets.

Milestone 3 is accepted when C++ exposes DrawList, DrawCommand, Camera or RenderView, OpaqueRenderer, PipelineCache, and RendererCounters or equivalent diagnostics. Tests must cover front-to-back sort order or an explicitly deferred stable-order policy, material override resolution, draw-list command validation, draw PropertyBag validation, draw-scope uniform packing, dynamic uniform-buffer offsets or the chosen equivalent per-draw data strategy, pipeline cache key separation, depth state, and cleanup of old BootstrapRenderer exports/imports. Native smoke should render through DrawList.

Milestone 4 is accepted when browser and native smoke render a large ground plane plus multiple cubes at different depths. The browser frame loop should animate cube rotation and vertical sine-wave motion. Native smoke should render a deterministic time sample, such as 1250 ms. Browser screenshots and native PNGs should visibly show the plane and cubes with depth testing.

Milestone 5 is accepted when docs and contracts describe the asset-handle resource model and renderer ownership, smoke expectations are updated, Cloudflare packaging still contains the right generated C++ runtime assets, and coverage passes. The coverage command must confirm changed implementation files do not appear in the default filtered coverage attention report unless this plan records an explicit exception.

Visual acceptance:

The first viewport should show the actual running render surface, not a marketing or explanatory page. The rendered image should not be blank. The ground plane should be visibly large relative to the cubes. At least three cubes should appear at distinct depths, with perspective and depth ordering visible. Browser screenshots should be stored under `C:\dev\ofg\artifacts\browser-smoke` or a clearly named subdirectory under `C:\dev\ofg\artifacts`.

Comment/readability acceptance:

Every new or changed C++ header, C++ source file, TypeScript file, or tool script should have a maintained top-of-file purpose comment unless the file's established local style clearly uses an equivalent header. Every new or changed function should have a purpose comment or doc comment. Any function over 50 lines should have internal comments that explain its phases. Milestone reviews must check this explicitly before marking a milestone complete.

## Idempotence and Recovery

Source edits may replace the bootstrap renderer path outright once the new opaque renderer is ready for the same milestone's tests. There is no requirement to keep BootstrapRenderer, bootstrap scene helpers, or bootstrap shader source as public compatibility interfaces after the new renderer is wired in and the contracts are updated.

Generated directories `C:\dev\ofg\dist`, `C:\dev\ofg\dist-test`, `C:\dev\ofg\.deploy`, `C:\dev\ofg\artifacts`, and `C:\dev\ofg\assets\wasm\ofg_cpp` can be regenerated by the existing npm scripts. Do not manually preserve generated files as source of truth.

If a GPU smoke command fails because no adapter is available, record the adapter/environment error in Surprises & Discoveries and continue with CPU tests only if the user agrees or the environment limitation is clear. Do not weaken smoke expectations for real rendering failures.

If shader variants become too complex for simple explicit variant keys, fall back to a minimal deterministic variant source builder that only substitutes declared boolean or numeric constants at known markers. Record that decision here before implementing it.

If the single-device assumption stops holding, revise this plan before coding the affected feature. That future revision can split asset CPU data from prepared GPU resources, but that complexity is intentionally out of scope for this milestone.

## Artifacts and Notes

Expected durable implementation artifacts:

    C:\dev\ofg\cpp\include\ofg\resources\handle.hpp
    C:\dev\ofg\cpp\include\ofg\resources\store.hpp
    C:\dev\ofg\cpp\include\ofg\resources\assets.hpp
    C:\dev\ofg\cpp\include\ofg\resources\texture.hpp
    C:\dev\ofg\cpp\include\ofg\resources\shader.hpp
    C:\dev\ofg\cpp\include\ofg\resources\material.hpp
    C:\dev\ofg\cpp\include\ofg\resources\mesh.hpp
    C:\dev\ofg\cpp\include\ofg\resources\property_bag.hpp
    C:\dev\ofg\cpp\include\ofg\resources\resource_error.hpp
    C:\dev\ofg\cpp\src\resources\texture.cpp
    C:\dev\ofg\cpp\src\resources\shader.cpp
    C:\dev\ofg\cpp\src\resources\material.cpp
    C:\dev\ofg\cpp\src\resources\mesh.cpp
    C:\dev\ofg\cpp\src\resources\property_bag.cpp
    C:\dev\ofg\cpp\src\resources\resource_error.cpp
    C:\dev\ofg\cpp\include\ofg\render\draw_list.hpp
    C:\dev\ofg\cpp\include\ofg\render\camera.hpp
    C:\dev\ofg\cpp\include\ofg\render\opaque_renderer.hpp
    C:\dev\ofg\cpp\include\ofg\render\pipeline_cache.hpp
    C:\dev\ofg\cpp\include\ofg\render\demo_scene.hpp
    C:\dev\ofg\cpp\src\render\draw_list.cpp
    C:\dev\ofg\cpp\src\render\camera.cpp
    C:\dev\ofg\cpp\src\render\opaque_renderer.cpp
    C:\dev\ofg\cpp\src\render\pipeline_cache.cpp
    C:\dev\ofg\cpp\src\render\demo_scene.cpp
    C:\dev\ofg\cpp\src\render\shaders\opaque_uber.wgsl.hpp

Expected visual artifacts:

    C:\dev\ofg\artifacts\render-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    C:\dev\ofg\artifacts\browser-smoke\*.png

Record final command transcripts here in concise form as milestones complete.

Initial plan review during migration Milestone 6:

    Required fixes applied: removed stale implementation-path wording, aligned interface sketches with the current C++ error-reporting style, narrowed texture scope to generated/caller-provided pixels unless image decoding remains small and covered, and added explicit decisions for local math types plus comment/readability acceptance.
    Validation: stale retired-language/tooling search against this plan found no active implementation-path drift; git diff --check passed with only existing LF-to-CRLF warnings elsewhere in the worktree.
    Remaining risks: per-draw uniform packing, durable WebGPU resource lifetime, native smoke file split, and keeping browser/native visual expectations aligned remain implementation risks for this plan's later milestones.

## Interfaces and Dependencies

The exact names may adjust during implementation, but the final public shape after Milestone 2 should remain close to these interfaces. Milestone 1 may omit or stub GPU fields while CPU ownership and validation are established. The important constraint is that assets are mutable through typed stores, and methods that need GPU work receive the active device/queue explicitly. These sketches use the current project style of returning nullable objects or `bool` and filling a caller-provided error string.

Generic handles and stores:

    template <typename T>
    struct Handle {
      uint32_t index = 0;
      uint32_t generation = 0;
    };

    using TextureHandle = Handle<Texture>;
    using ShaderHandle = Handle<Shader>;
    using MaterialHandle = Handle<Material>;
    using MeshHandle = Handle<Mesh>;

    template <typename T>
    class Store {
     public:
      Handle<T> insert(T value);
      const T* get(Handle<T> handle) const;
      T* get_mut(Handle<T> handle);
      std::optional<T> remove(Handle<T> handle);
      size_t size() const;
      bool empty() const;
    };

    struct Assets {
      Store<Texture> textures;
      Store<Shader> shaders;
      Store<Material> materials;
      Store<Mesh> meshes;
    };

GPU context:

    struct GpuContext {
      WGPUDevice device = nullptr;
      WGPUQueue queue = nullptr;
    };

Shared property data:

    using PropertyValue = std::variant<float, Vec2, Vec3, Vec4, Mat4, TextureHandle>;

    class PropertyBag {
     public:
      void set(std::string name, PropertyValue value);
      const PropertyValue* get(std::string_view name) const;
      bool validate_for_scope(const Shader& shader, ShaderParameterScope scope, std::string& error) const;
      std::optional<std::vector<std::byte>> pack_uniforms_for_scope(
          const Shader& shader,
          ShaderParameterScope scope,
          std::string& error) const;
    };

Texture resource:

    enum class TextureColorSpace { Srgb, Linear };
    enum class TexturePixelFormat { Rgba8, Rgba8Srgb };
    enum class MipMapPolicy { None, GenerateCpuFullChain };

    class Texture {
     public:
      static std::optional<Texture> from_rgba8_pixels(
          GpuContext gpu,
          std::string label,
          uint32_t width,
          uint32_t height,
          TextureColorSpace color_space,
          std::vector<std::byte> pixels,
          MipMapPolicy mip_map_policy,
          std::string& error);

      bool update_pixels(GpuContext gpu, std::vector<std::byte> pixels, std::string& error);
      WGPUTextureView view() const;
      WGPUSampler sampler() const;
    };

Shader and material interfaces:

    class Shader {
     public:
      static std::optional<Shader> create(
          GpuContext gpu,
          std::string label,
          std::string wgsl_source,
          ShaderParameterLayout parameter_layout,
          std::vector<PipelineDefinition> pipelines,
          std::string& error);

      bool replace_source(GpuContext gpu, std::string wgsl_source, std::string& error);
      const ShaderParameter* parameter(std::string_view name) const;
      WGPUShaderModule module() const;
      uint64_t revision() const;
    };

    class Material {
     public:
      static std::optional<Material> create(
          GpuContext gpu,
          const Store<Shader>& shaders,
          const Store<Texture>& textures,
          std::string label,
          ShaderHandle shader,
          PropertyBag properties,
          std::string& error);

      bool set_property(
          GpuContext gpu,
          const Store<Shader>& shaders,
          const Store<Texture>& textures,
          std::string name,
          PropertyValue value,
          std::string& error);

      ShaderHandle shader() const;
      WGPUBindGroup bind_group() const;
      uint64_t revision() const;
    };

Mesh interface:

    struct MeshVertex {
      float position[3];
      float normal[3];
      float uv[2];
    };

    struct SubMesh {
      std::string label;
      uint32_t index_start = 0;
      uint32_t index_count = 0;
      MaterialHandle default_material;
    };

    class Mesh {
     public:
      static std::optional<Mesh> create(
          GpuContext gpu,
          std::string label,
          std::vector<MeshVertex> vertices,
          std::vector<uint32_t> indices,
          std::vector<SubMesh> submeshes,
          std::string& error);

      bool replace_vertices(GpuContext gpu, std::vector<MeshVertex> vertices, std::string& error);
      bool replace_indices(
          GpuContext gpu,
          std::vector<uint32_t> indices,
          std::vector<SubMesh> submeshes,
          std::string& error);
      WGPUBuffer vertex_buffer() const;
      WGPUBuffer index_buffer() const;
      std::span<const SubMesh> submeshes() const;
      uint64_t revision() const;
    };

Renderer interface:

    struct DrawCommand {
      MeshHandle mesh;
      PropertyBag properties;
      std::vector<MaterialOverride> material_overrides;
      Vec3 sort_origin;
    };

    class OpaqueRenderer {
     public:
      static std::unique_ptr<OpaqueRenderer> create(
          WGPUDevice device,
          WGPUTextureFormat color_format,
          std::string& error);
      bool resize(WGPUDevice device, uint32_t width, uint32_t height, std::string& error);
      bool render_to_view(
          GpuContext gpu,
          WGPUCommandEncoder encoder,
          WGPUTextureView target,
          const Assets& assets,
          const RenderView& view,
          const DrawList& draw_list,
          std::string& error);
      RendererCounters counters() const;
    };

Initial shader parameters:

Frame scope, bind group 0:

    view_projection: mat4x4<f32>

Draw scope, bind group 1:

    model: mat4x4<f32>

Material scope, bind group 2:

    base_color_factor: vec4<f32>
    base_color_texture: texture_2d<f32>
    base_color_sampler: sampler derived from base_color_texture

Initial shader variants:

    ShaderVariantKey { base_color_texture: false }
    ShaderVariantKey { base_color_texture: true }

The second variant should be used by the generated checker ground material. The first variant can be used by simple colored cube materials.
