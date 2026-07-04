# Polymorphic Resource Loading Refactor

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document is maintained in accordance with `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

The current player model loading path works, but the first implementation puts too much model-specific scheduling code inside `Resources`. `Resources` currently owns model load records, model dependency lists, pending parsed glTF documents, and a model-specific `advance_model_load(...)` state machine. That is acceptable for one resource type, but it will scale badly when the game has many streamable resource types such as textures, terrain chunks, audio, shader permutations, and generated world data.

After this refactor, `Resources` remains the single owner of durable resource objects and the single generic scheduler for loading resources, but each resource type owns its own loading state machine. `Resources::advance_loads()` will call a virtual `Resource::update_loading()` function on every loading resource and remove terminal resources from the loading list. `ModelResource` will own a temporary `ModelResourceLoader` while it is loading. All model-specific work, including glTF root blob loading, dependency discovery, dependency waiting, parsed document storage, generated sub-resource keys, and import into the final model template, will be encapsulated inside `ModelResourceLoader`.

The user-visible behavior should not change. The browser scene should still show the imported player model, runtime debug status should still report `modelLoadingState: "loaded"` and `playerModelLoaded: true`, and TypeScript should still only fetch generic blob requests. The architectural change is that future resource types can add their own loader objects without adding duplicate type-specific scheduler code to `Resources`.

## Progress

- [x] (2026-07-03 07:09Z) Created this ExecPlan to capture the agreed refactor direction: `Resources` owns all durable resources, `Resources` schedules loading generically, `Resource::update_loading()` is virtual, `ModelResource` owns a temporary `ModelResourceLoader`, and embedded model sub-resources use generated URI-like keys with `#` fragments.
- [x] (2026-07-03) Milestone 1: Moved `Resources` from model-specific load records to a generic `std::vector<Resource*>` loading list. `Resources::advance_loads()` now calls `Resource::update_loading()`, catches failures into resource state, and prunes terminal resources.
- [x] (2026-07-03) Milestone 2: Added `ModelResourceLoader` and moved glTF root blob waiting, dependency discovery, dependency waiting, parsed document storage, and import completion out of `Resources`.
- [x] (2026-07-03) Milestone 3: Removed `ModelResourceImportContext`; `ModelResourceLoader` now caches `Ptr<Mesh>`, `Ptr<Material>`, `Ptr<Texture>`, and `Ptr<Shader>`, while durable objects are created through `Resources::create_*`.
- [x] (2026-07-03) C++ validation after Milestones 1-3: `npm run test:cpp` passed. `resources_model_load_test` now checks that the generic loading queue drains, repeated model loads do not re-enqueue, imported sub-resources are owned by `Resources`, and generated labels use parent URI `#` fragments.
- [x] (2026-07-03) Milestone 4: Preserved player self-loading through `Resources::load_model_resource`; updated `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` to describe generic resource scheduling and `ModelResourceLoader` ownership.
- [x] (2026-07-03) Final validation passed: `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:render`, `npm run coverage`, `git -c safe.directory=C:/dev/ofg diff --check`, and the legacy player-byte-loader grep from this plan.
- [x] (2026-07-03) Browser smoke evidence recorded: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`; report showed `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`.
- [x] (2026-07-03) Milestone review completed locally for contract, code quality, legacy, correctness, and validation. Sub-agent tools were present, but the available sub-agent tool explicitly disallows spawning unless the user explicitly asks for delegation, so no sub-agents were spawned. Required finding fixed: updated `docs/plans/gltf-model-loading-animation-plan.md` so its current guidance no longer resurrects `ModelResourceImportContext` ownership or byte-specific `loadPlayerModel` APIs. Follow-ups: consider splitting `ModelResourceLoader` internals if `cpp/src/assets/gltf_importer_resources.cpp` grows past the current 564 lines or gains another responsibility. Review input note: `docs/ARCHITECTURE.md` is referenced by the skill but does not exist in this repository.

## Surprises & Discoveries

- Observation: `ModelResourceImportContext` currently owns imported mesh/material/texture/shader resources.
  Evidence: `C:\dev\ofg\cpp\include\ofg\assets\gltf_importer.hpp` stores `std::unique_ptr<Shader>`, `std::unique_ptr<Texture>`, `std::unique_ptr<Material>`, and `std::unique_ptr<Mesh>` inside `ModelResourceImportContext`.

- Observation: That ownership violates the intended invariant that `Resources` is the only durable resource owner.
  Evidence: `ModelResource` templates can hold persistent `Ptr<Mesh>` and material/texture references. If a temporary import context owns those target objects, destroying that helper after load would destroy resources the model still references.

- Observation: A separate temporary `ModelResourceImportContext` is probably unnecessary once durable resource ownership moves to `Resources`.
  Evidence: If the import context owns no resources and is discarded after load, its useful role is only temporary import lookup/deduplication. That can live directly inside `ModelResourceLoader`.

- Discovery: `ModelResource` needs an out-of-line constructor as well as an out-of-line destructor once it owns `std::unique_ptr<ModelResourceLoader>`.
  Evidence: `std::make_unique<ModelResource>()` in `Resources` instantiated `unique_ptr<ModelResourceLoader>` cleanup while `ModelResourceLoader` was still incomplete until the constructor was moved to `model_resource.cpp`.

- Discovery: Direct importer tests must create the central `Resources` singleton before importing.
  Evidence: Imported meshes, materials, textures, and shaders are no longer helper-owned CPU-only objects; `ModelResourceLoader::get_or_create_*` now calls `Resources::create_*`.

## Decision Log

- Decision: `Resources` is the only owner of durable resources, period.
  Rationale: Centralized ownership keeps lifetime predictable, lets persistent observers use `Ptr<T>`, and avoids hidden resource lifetimes inside parser/importer helper objects.
  Date/Author: 2026-07-03 / Codex

- Decision: `Resources` should schedule loading generically instead of owning per-resource-type load state machines.
  Rationale: With ten streamable resource types, `Resources` should not accumulate ten copies of dependency ids, pending parsed documents, state transitions, and import-specific helper code. It should own resources, own the generic loading list, and call each loading resource's virtual update hook.
  Date/Author: 2026-07-03 / Codex

- Decision: Do not add a `ResourceLoadContext`.
  Rationale: `Resources` is already a singleton service in OFG. Resource loaders can call `Resources::load_blob`, `Resources::blob`, `Resources::create_mesh`, `Resources::create_texture`, and related APIs directly. A context wrapper would add ceremony without solving a current problem.
  Date/Author: 2026-07-03 / Codex

- Decision: Collapse temporary model import context behavior into `ModelResourceLoader`.
  Rationale: If import context state is temporary and owns no durable resources, it does not need to be a separate long-lived concept. `ModelResourceLoader` can own temporary import maps, blob ids, parsed documents, and generated sub-resource keys until loading completes or fails.
  Date/Author: 2026-07-03 / Codex

- Decision: Use URI fragments with `#` for embedded/generated sub-resource identities.
  Rationale: Some durable resources, such as a texture embedded in a `.glb`, do not have standalone file URIs. They still need stable identity keys for `Resources` ownership and deduplication. URI fragments mirror existing URI semantics better than colon-separated pseudo paths.
  Date/Author: 2026-07-03 / Codex

- Decision: Keep the direct glTF import helper API but change its temporary cache parameter to `ModelResourceLoader`.
  Rationale: Importer unit tests still need to exercise structural conversion without going through host blob polling, but the cache must no longer own durable resources or preserve the old `ModelResourceImportContext` concept.
  Date/Author: 2026-07-03 / Codex

## Outcomes & Retrospective

Completed. `Resources` no longer owns `ModelResourceLoadRecord`, glTF dependency vectors, pending parsed glTF documents, or `advance_model_load(...)`. It owns durable resources, blob cache state, model resource storage, and a generic loading list that calls `Resource::update_loading()`. `ModelResource` owns a temporary `std::unique_ptr<ModelResourceLoader>` while it is loading and releases that loader once loading reaches `Loaded` or `Failed`. `ModelResourceLoader` now owns the root blob id, dependency ids/URIs, pending parsed document, and temporary `Ptr<T>` import caches, and all imported mesh/material/texture/shader objects are allocated through `Resources::create_*`.

Validation passed through formatting, C++/TypeScript tests, browser smoke, native render smoke, full coverage, whitespace diff check, and legacy player-byte-loader grep. Browser smoke confirmed the player model self-loads with `modelLoadingState: "loaded"`, `playerModelLoaded: true`, `lastError: null`, and screenshot `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`.

Remaining risk is modest: `ModelResourceLoader` is intentionally broad for this plan and lives in `cpp/src/assets/gltf_importer_resources.cpp`, now 564 lines. That is below the repo's review threshold, but if model loading grows again it should be split into smaller loader-state/import-cache units.

## Contract and Quality Baseline

This plan preserves and sharpens `OFG-BOOT-001`, `OFG-BOOT-002`, `OFG-BOOT-003`, `OFG-BOOT-006`, and `OFG-BOOT-009` in `C:\dev\ofg\docs\API_CONTRACTS.md`.

`OFG-BOOT-001 TypeScript Host Ownership` is preserved. TypeScript remains a generic browser transport for opaque blob requests. It must not choose player model assets, parse glTF, or own model resources.

`OFG-BOOT-002 C++ Runtime Ownership` is preserved. C++ continues to own model/resource loading and scene binding. The detail that changes is internal: model-specific loading logic moves from `Resources` to `ModelResource`/`ModelResourceLoader`.

`OFG-BOOT-003 WASM Facade` is preserved. The browser facade remains generic blob polling/completion/failure by id and must not regain player-specific byte APIs.

`OFG-BOOT-006 Resource Lifetime` is intentionally strengthened. `Resources` must be the only owner of durable resource objects: `Mesh`, `Texture`, `Shader`, `Material`, `ModelResource`, and future resource types. Temporary loaders may cache `Ptr<T>` or raw stack borrows during import, but they must not own durable resource objects. Persistent cross-resource references use `Ptr<T>` when they can outlive immediate stack scope.

`OFG-BOOT-009 Coverage` applies. Modified implementation files must pass the default coverage attention gate, or this plan must record an explicit exception with rationale. Because this affects browser model loading and rendering, browser smoke and a screenshot path are required before completion.

## Context and Orientation

The current first working resource-loading implementation is in `C:\dev\ofg\cpp\include\ofg\resources\resources.hpp` and `C:\dev\ofg\cpp\src\resources\resources.cpp`. It added `Resources::load_blob`, generic browser blob state, `Resource`, `ModelResource`, and `Resources::load_model_resource(...)`.

The problem is that `Resources` currently owns model-specific loading state:

    struct ModelResourceLoadRecord {
        ModelResource* m_resource;
        std::string m_cache_key;
        std::string m_uri;
        std::string m_model_name;
        BlobLoadId m_root_blob_id;
        std::vector<BlobLoadId> m_dependency_blob_ids;
        std::vector<std::string> m_dependency_uris;
        std::optional<GltfDocument> m_pending_document;
    };

`Resources::advance_model_load(...)` then implements the model-specific state machine. That shape duplicates poorly. A future `TextureResource`, `TerrainChunkResource`, or `AudioResource` would need different dependency and import state, and putting all of it into `Resources` would make `Resources` a large switchboard rather than a resource owner.

`Resource` currently exposes observable state in `C:\dev\ofg\cpp\include\ofg\resources\resource.hpp`, but only `Resources` mutates state. This refactor should make resource-derived classes responsible for their own loading transitions. `Resources` still owns the objects and decides when to call their loading update.

`ModelResource` currently lives in `C:\dev\ofg\cpp\include\ofg\assets\model_resource.hpp` and `C:\dev\ofg\cpp\src\assets\model_resource.cpp`. It stores the final model template data: node templates, mesh renderer templates, skin templates, and animation clips. After this refactor it will also hold a `std::unique_ptr<ModelResourceLoader>` while loading is in progress.

`ModelResourceImportContext` currently lives in `C:\dev\ofg\cpp\include\ofg\assets\gltf_importer.hpp` and owns imported resources. That must change. Either remove this class or reduce it to code folded into `ModelResourceLoader`; do not leave durable resource ownership inside it.

## Plan of Work

Milestone 1 changes the base loading API. Add a private or protected virtual loading hook to `Resource`, likely:

    virtual void update_loading() = 0;

Make `Resources` a friend if the hook is private. Move the state mutation helpers on `Resource` from private `friend class Resources` access to protected access so derived resource types can set their own state and failure diagnostics. Preserve public read-only state access for gameplay and rendering code.

In `Resources`, replace the model-only in-progress list with:

    std::vector<Resource*> m_loading_resources;

Add a private `enqueue_loading(Resource& resource)` helper that adds a resource only if it is non-terminal and not already in the list. `Resources::advance_loads_impl()` should iterate the generic list, call `resource->update_loading()`, catch exceptions into `resource->set_resource_failed(...)`, and remove terminal or destroyed/null entries after the pass. At this stage it is acceptable to keep `Resources::load_model_resource_impl(...)` mostly as-is until `ModelResourceLoader` exists, but the target is to remove `advance_model_load(...)` completely in Milestone 2.

Milestone 2 creates `ModelResourceLoader`. Put the declaration where it best preserves ownership boundaries, likely a private implementation type in `C:\dev\ofg\cpp\src\assets\model_resource.cpp` or a small private header if tests need access through public behavior. `ModelResource` should expose an internal `begin_loading(std::string source_uri, ModelResourceLoadOptions options)` used by `Resources::load_model_resource_impl(...)`. That method creates `m_loader`, sets the source URI, clears previous errors, and moves the resource to `Queued`.

`ModelResource::update_loading()` should delegate to `m_loader->update(*this)`. `ModelResourceLoader` should contain all model-specific loading state currently in `Resources::ModelResourceLoadRecord`: root blob id, dependency blob ids and URIs, optional parsed `GltfDocument`, root URI, effective model name, and any temporary import/dedup maps. It should call the `Resources` singleton directly for blob and resource operations. When the model reaches `Loaded` or `Failed`, `ModelResource` should reset `m_loader`.

Milestone 3 fixes durable ownership. Remove resource-owning maps from `ModelResourceImportContext` or delete that class if its behavior has been absorbed by `ModelResourceLoader`. Importing a mesh/material/texture/shader must allocate or find a durable object through `Resources`. Temporary loader maps may cache only `Ptr<Mesh>`, `Ptr<Material>`, `Ptr<Texture>`, and `Ptr<Shader>` by generated resource key.

For embedded or generated sub-resources, use stable URI-like keys with fragments. Examples:

    assets/models/player/quaternius-superhero-male.glb#mesh/Body
    assets/models/player/quaternius-superhero-male.glb#material/Suit
    assets/models/player/quaternius-superhero-male.glb#texture/BaseColor
    assets/models/tests/animated-cube.gltf#texture/AnimatedCube_BaseColor

If source names are missing or duplicated, generate deterministic index-based keys:

    assets/models/player/quaternius-superhero-male.glb#mesh/0
    assets/models/player/quaternius-superhero-male.glb#texture/3

The exact fragment vocabulary can evolve, but it must be normalized and covered by tests. Do not use `:` for these identities in this plan; use `#` to mirror URI fragment semantics.

Milestone 4 restores and proves behavior. `Resources::load_model_resource(...)` should remain the public API. It should normalize the root URI and model options, deduplicate `ModelResource` objects, allocate a `Resources`-owned `ModelResource`, call `begin_loading(...)`, enqueue the resource, and return `Ptr<ModelResource>`. `Player` should continue to call `Resources::load_model_resource(...)` and should not know about `ModelResourceLoader`.

Update `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` so they say `Resources` owns all durable resources and schedules loading generically, while model-specific loading logic lives in `ModelResourceLoader`. Update tests to prove model resources still load from single-file GLB and multi-file glTF fixtures, dependency failures still propagate, duplicate model requests still return the same `ModelResource`, loader state is destroyed or inert after terminal load, and imported sub-resources remain alive after the loader is gone because `Resources` owns them.

## Concrete Steps

Run these discovery commands from `C:\dev\ofg` before editing:

    rg -n "ModelResourceLoadRecord|advance_model_load|ModelResourceImportContext|m_in_progress_model_load_indices|load_model_resource|advance_loads" cpp
    rg -n "std::unique_ptr<Shader>|std::unique_ptr<Texture>|std::unique_ptr<Material>|std::unique_ptr<Mesh>" cpp/include/ofg/assets cpp/src/assets

After Milestone 1:

    npm run format:cpp
    npm run format:cpp:check
    npm run test:cpp

After Milestone 2:

    npm run format:cpp
    npm run test:cpp

After Milestone 3:

    npm run test:cpp
    npm run coverage:cpp

After Milestone 4 and before completing the plan:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check
    rg -n "loadPlayerModel|load_player_model|report_player_model_load_error|DEFAULT_PLAYER_MODEL_URL|DEFAULT_PLAYER_ANIMATION_URL|loadDefaultPlayerModel|fetchAssetBytes" cpp src tests docs/API_CONTRACTS.md docs/SYSTEMS.md

Expected final legacy grep result: no matches.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective.
2. Update any changed API contracts or active docs before review.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
4. Apply required findings before marking the milestone complete, or record any rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands.
6. Record review summary, commands, screenshot paths, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

This plan is complete only when all of these behaviors are true.

`Resources` owns every durable resource object created during model loading: meshes, textures, shaders, materials, and model resources. No temporary model loading/import helper owns a durable mesh/material/texture/shader/model resource object.

`Resources` has a generic loading-resource list. `Resources::advance_loads()` calls a virtual resource loading hook and removes terminal resources from that list. `Resources` no longer contains `ModelResourceLoadRecord`, `advance_model_load(...)`, pending glTF documents, or model dependency vectors.

`ModelResource` owns a temporary `std::unique_ptr<ModelResourceLoader>` while loading. `ModelResourceLoader` encapsulates all model-specific loading logic. It may be internally broad for this plan, but no model-specific loading state should remain in `Resources`.

`ModelResourceLoader` calls the `Resources` singleton directly. There is no `ResourceLoadContext` abstraction introduced by this plan.

Embedded or generated model sub-resources have stable generated identity keys based on the parent model URI plus `#` fragments. These keys are used for deduplication and diagnostics and are covered by tests.

`Resources::load_model_resource(...)` remains the public API. Repeated calls with the same normalized URI and equivalent options return the same `Resources`-owned `ModelResource`, do not enqueue duplicate loading work, and do not duplicate imports after load.

The player still self-loads through `Resources::load_model_resource(...)`. Browser smoke must observe `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`. A browser smoke screenshot path must be recorded in this plan before completion.

Failure behavior remains observable. Root blob failures, dependency blob failures, and import failures must transition the `ModelResource` to `Failed`; the player fallback remains visible; debug status reports failed model loading and a useful URI/error diagnostic.

Final validation commands must pass:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check

Coverage acceptance: every modified implementation file must pass the default coverage attention gate. If browser-only glue cannot be covered through native coverage, record the existing browser/WASM validation rationale and cover TypeScript wrapper behavior with Mocha tests.

Screenshot acceptance: because this touches browser model loading and rendering, capture at least one browser smoke screenshot after behavior is restored and record its path here. The human reviewer should verify that the imported player model is visible, the fallback box is hidden, and the scene still renders ground and cubes.

## Idempotence and Recovery

The refactor should preserve existing public APIs during intermediate milestones wherever possible. If Milestone 2 fails halfway through, keep `Resources::load_model_resource(...)` callable and return to the last passing model-specific scheduler implementation.

`Resources::release()` must clear the generic loading list before destroying owned resources. Existing `Ptr<T>` observers should be nulled by `Object` destruction. Temporary `ModelResourceLoader` instances must be destroyed either when their owning `ModelResource` is destroyed or when loading reaches `Loaded` or `Failed`.

Blob failure retry policy does not change in this plan. Failed blobs and failed model resources remain terminal until `Resources::release` unless a later plan adds an explicit retry API.

Generated `#` sub-resource keys must be deterministic. If a source asset has duplicate or empty names, index-based fallback keys must remain stable for the same source document.

## Artifacts and Notes

The completed predecessor plan is archived at `C:\dev\ofg\docs\archived\resources-asset-requests-player-self-load-plan.md`. It proved the browser/player behavior and introduced the current first-pass model-resource loader.

The core architectural correction in this plan is:

    Resources
      owns all durable resource objects
      owns blob cache
      owns generic loading-resource list
      calls Resource::update_loading()

    ModelResource
      owns final model template data
      owns std::unique_ptr<ModelResourceLoader> while loading

    ModelResourceLoader
      owns all temporary model loading/import state
      calls Resources::load_blob / blob / create_mesh / create_texture / create_material
      uses generated parent-uri#fragment keys for embedded sub-resources
      is destroyed after loaded or failed

## Interfaces and Dependencies

Target `Resource` API shape:

    class Resource : public Object {
    public:
        [[nodiscard]] ResourceState state() const noexcept;
        [[nodiscard]] const std::string& source_uri() const noexcept;
        [[nodiscard]] const std::string& load_error() const noexcept;
        [[nodiscard]] bool is_in_progress() const noexcept;
        [[nodiscard]] bool is_loaded() const noexcept;
        [[nodiscard]] bool is_failed() const noexcept;
        [[nodiscard]] bool is_terminal() const noexcept;

    protected:
        Resource() noexcept = default;
        void set_source_uri(std::string source_uri);
        void set_resource_state(ResourceState state) noexcept;
        void set_resource_failed(std::string message);
        void clear_resource_error();

    private:
        friend class Resources;
        virtual void update_loading() = 0;
    };

Target generic `Resources` loading members:

    std::vector<Resource*> m_loading_resources;

    void enqueue_loading(Resource& resource);
    void advance_loads_impl();

Target `ModelResource` loading members:

    class ModelResource : public Resource {
    private:
        friend class Resources;

        void begin_loading(std::string source_uri, ModelResourceLoadOptions options);
        void update_loading() override;

        std::unique_ptr<ModelResourceLoader> m_loader;
    };

Target `ModelResourceLoader` responsibility:

    class ModelResourceLoader {
    public:
        ModelResourceLoader(std::string source_uri, ModelResourceLoadOptions options);
        void update(ModelResource& target);

    private:
        BlobLoadId m_root_blob_id{invalid_blob_load_id};
        std::vector<BlobLoadId> m_dependency_blob_ids;
        std::vector<std::string> m_dependency_uris;
        std::optional<GltfDocument> m_pending_document;

        std::unordered_map<std::string, Ptr<Mesh>> m_meshes_by_key;
        std::unordered_map<std::string, Ptr<Material>> m_materials_by_key;
        std::unordered_map<std::string, Ptr<Texture>> m_textures_by_key;
        Ptr<Shader> m_pbr_shader;
        Ptr<Texture> m_default_white_texture;
        Ptr<Texture> m_default_metallic_roughness_texture;
        Ptr<Texture> m_default_normal_texture;
    };

The exact internal methods of `ModelResourceLoader` can evolve during implementation. The non-negotiable boundary is that all model-specific loading logic is encapsulated within it and all durable resource objects are owned by `Resources`.
