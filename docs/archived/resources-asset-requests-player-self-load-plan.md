# Resource Blob Loading And Player Self-Loading

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document is maintained in accordance with `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

After this work, the player model is no longer selected and pushed into C++ by the TypeScript host. The C++ `Player` component asks the engine resource system for its own hardcoded model and animation-library URIs. The browser host still physically fetches bytes, because browser WASM cannot freely read files or make network requests without going through browser APIs, but TypeScript acts only as a generic platform file transport. It does not know that the requested bytes are a player model, does not decide which player assets to load, and does not call a player-specific model API.

The observable result is the same browser scene as today: the imported player model appears in place of the fallback box, third-person movement drives idle/walk/sprint animation, and runtime debug status reports `modelLoadingState: "loaded"` and `playerModelLoaded: true`. The important architectural change is that the path is now C++ directed:

    Player -> Resources::load_blob("assets/models/player/...") -> BrowserGame generic pending blob loads -> TypeScript fetch(uri) -> Resources::complete_blob_load(...) -> Player imports when both blobs are loaded

That first proof point was completed in Milestone 3. The final proof point is a higher-level model-resource API:

    Player -> Resources::load_model_resource("assets/models/player/...") -> Resources::load_blob(...) and glTF import happen internally -> Player stores Ptr<ModelResource> and binds once loaded

At that point, player code no longer asks for blobs directly. `load_blob` remains the low-level platform boundary for arbitrary binary data, while `load_model_resource` is the resource-specific API used by game systems for model assets.

This is the standard shape for browser WASM asset loading. JavaScript or TypeScript has to perform the browser `fetch`, but it should behave like an operating-system file API behind a generic resource request boundary, not like a game system that chooses and injects assets.

## Progress

- [x] (2026-07-02 20:12Z) Read `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\GUIDES.md`, current `Resources`, `BrowserGame`, `wasmRuntime.ts`, and `main.ts` asset-loading code.
- [x] (2026-07-02 20:12Z) Created this ExecPlan for moving player model loading to C++-directed `Resources` blob loads.
- [x] (2026-07-02 20:20Z) Updated the plan terminology and final abstraction target: the low-level arbitrary binary API is `load_blob`, and the final player path should call a model-resource loader rather than touching blobs directly.
- [x] (2026-07-02 20:28Z) Clarified that resource-specific loading needs a dependency-discovery stage: a resource may load its root blob, discover more required blobs or resource dependencies, request them, and only then finish import.
- [x] (2026-07-02 20:42Z) Clarified resource ownership: load records must not own resources. `Resources` owns `Resource` objects, model-resource loading returns a `Ptr<ModelResource>`, and callers poll the resource object's load state.
- [x] (2026-07-02 20:49Z) Clarified the internal scheduler model: `Resources` keeps a private list of in-progress resources and advances any resource whose current state can transition during a resource update step.
- [x] (2026-07-02 20:55Z) Replaced the generic `load<T>` public API goal with resource-specific loading, starting with `Resources::load_model_resource(...)`, so each resource type can grow its own options without being forced through one rigid template API.
- [x] (2026-07-02 21:03Z) Defined the target `Resource` and `Resources` APIs precisely, including state meanings, ownership/mutation rules, `load_model_resource(...)`, and the requirement that asynchronous model import populates the stable `ModelResource` object in place.
- [x] (2026-07-02 22:18Z) Implemented the first blob request/cache state machine inside `Resources`, with native tests for URI normalization/deduplication, queued-to-loaded completion, queued-to-failed completion, invalid ids/URIs, and lifecycle rejection after release.
- [x] (2026-07-02 22:19Z) Ran `npm run format:cpp`, `cmake --build --preset native-tests-debug`, and `ctest --preset native-tests --output-on-failure`; all passed.
- [x] (2026-07-02 22:31Z) Ran `npm run format:cpp:check`, `npm run test:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`; all passed. `npm run test:cpp` noted the existing local Dawn checkout revision differs from `dawn-version.txt`.
- [x] (2026-07-02 22:32Z) Completed Milestone 1 review locally using the milestone-review skill. Sub-agents were not used because the available sub-agent tool requires an explicit user request for delegation. Required finding fixed: stale plan snippets still described the earlier `BlobView`/`complete_blob_load` API sketch. Follow-up: `docs/ARCHITECTURE.md` is referenced by the skill but does not exist in this repo.
- [x] Milestone 1: Add blob load/cache state to `Resources` and native tests for load deduplication, completion, failure, and lifecycle cleanup.
- [x] (2026-07-02 23:02Z) Added the generic browser blob-load bridge: `BrowserGame::blob_loads_json`, `mark_blob_loading`, `complete_blob_load`, and `fail_blob_load`; TypeScript `BrowserGameRuntime` now parses blob requests, tracks in-flight ids, fetches generic blobs, and reports completion/failure.
- [x] (2026-07-02 23:03Z) Updated `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` for the transitional contract: generic blob bridge is supported, while the old direct player byte path remains temporarily until player self-loading replaces it.
- [x] (2026-07-02 23:04Z) Ran `npm run build:wasm`, `npm run test:ts`, `npm run format:cpp`, `npm run test:cpp`, `npm run format:cpp:check`, and a final `npm run build:wasm`; all passed. `npm run test:cpp` again noted the existing local Dawn checkout revision differs from `dawn-version.txt`.
- [x] (2026-07-02 23:12Z) Completed Milestone 2 review locally using the milestone-review skill. Required finding fixed: `BrowserGame::blob_loads_json` now records an error and returns `[]` instead of returning non-array debug-status JSON on exceptions. Follow-up recorded: `cpp/src/web/browser_game.cpp` is 976 lines, so the next browser-facade edits should split helper code or remove the old player path before it crosses 1000 lines.
- [x] (2026-07-02 23:13Z) Reran `npm run format:cpp:check`, `npm run build:wasm`, `npm run test:ts`, and `git -c safe.directory=C:/dev/ofg diff --check`; all passed.
- [x] Milestone 2: Expose a generic browser blob-load bridge through `BrowserGame` and `wasmRuntime.ts`, with TypeScript tests proving generic request polling/completion.
- [x] (2026-07-02 23:41Z) Moved default player asset URI selection into `Player`; `Player::update` now requests the model and animation-library blobs through `Resources::load_blob`, imports after both blobs load, records blob/import failures on the player, and keeps the fallback visible on failure.
- [x] (2026-07-02 23:42Z) Removed app-facing TypeScript player byte transport: `src/app/main.ts` no longer hardcodes player GLB URLs, and `BrowserGameRuntime` no longer exposes `loadPlayerModel` or `reportPlayerModelLoadError`. `rg` over `src` and `tests` found no `loadPlayerModel`, `reportPlayerModelLoadError`, `DEFAULT_PLAYER_MODEL_URL`, `DEFAULT_PLAYER_ANIMATION_URL`, or `loadDefaultPlayerModel` matches.
- [x] (2026-07-02 23:43Z) Added native tests proving player update enqueues the two default player blobs and records failed blob loads on the player while leaving fallback visible.
- [x] (2026-07-02 23:44Z) Ran `npm run test:cpp`, `npm run test:ts`, `npm run smoke:browser`, `npm run smoke:render`, and `npm test`; all passed. Browser smoke reported `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`.
- [x] (2026-07-02 23:45Z) Browser screenshot evidence: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`; browser smoke report: `C:\dev\ofg\artifacts\browser-smoke\report.json`; native render smoke report: `C:\dev\ofg\artifacts\render-smoke\report.json`.
- [x] (2026-07-02 23:53Z) Removed the legacy player-specific byte transport from C++ and Embind as well: `BrowserGame::load_player_model`, `BrowserGame::report_player_model_load_error`, `Game::load_player_model`, and `Game::record_player_model_load_failure` are gone. A grep over active `cpp`, `src`, `tests`, `docs/API_CONTRACTS.md`, and `docs/SYSTEMS.md` found no old player byte API names.
- [x] (2026-07-02 23:54Z) Reran `npm run test:cpp`, `npm run test:ts`, `npm run smoke:browser`, `npm run smoke:render`, and `git -c safe.directory=C:/dev/ofg diff --check`; all passed after legacy removal. `cpp/src/web/browser_game.cpp` dropped from 976 to 892 lines.
- [x] (2026-07-02 23:56Z) Completed Milestone 3 review locally using the milestone-review skill. Required finding fixed: the old C++/Embind player byte API was still present after TypeScript moved to generic blobs, so it was removed in the same milestone. Follow-up: `Player` still directly consumes blob byte spans until `Resources::load_model_resource(...)` lands in Milestone 4/5.
- [x] (2026-07-02 23:57Z) Reran `npm run format:cpp:check`; passed.
- [x] Milestone 3: Move default player URI selection and interim blob-based model import triggering into `Player`, while `Game` only mirrors player load status into runtime debug status.
- [x] (2026-07-02 22:05Z) Added `Resource` / `ResourceState`, made `ModelResource` inherit `Resource`, added in-place `ModelResourceBuilder::build_into(...)`, and implemented `Resources::load_model_resource(...)` with a private scheduler, root blob loading, dependency discovery, dependency waiting, import, terminal loaded/failed states, and `Resources::advance_loads()` called from `Game::update_impl` before scene update.
- [x] (2026-07-02 22:05Z) Added native model-resource loader tests covering state diagnostics, duplicate model-resource request deduplication, single-file GLB import, multi-file `.gltf` dependency discovery/import, root blob failure, external dependency failure diagnostics, stable `Ptr<ModelResource>` observation, and pointer nulling on `Resources::release`.
- [x] (2026-07-02 22:05Z) Updated `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` to describe `Resource`, `ModelResource`, `Resources::load_model_resource(...)`, `Resources::advance_loads()`, model dependency discovery, and the absence of direct player-byte browser APIs.
- [x] (2026-07-02 22:05Z) Ran `npm run format:cpp`, `npm run test:cpp`, `npm run build:wasm`, and `git -c safe.directory=C:/dev/ofg diff --check`; all passed. `npm run test:cpp` again noted the existing local Dawn checkout revision differs from `dawn-version.txt`.
- [x] (2026-07-02 22:05Z) Completed Milestone 4 review locally using the milestone-review skill. Sub-agents were not used because the available sub-agent tool requires an explicit user request for delegation. Required findings fixed: active docs lacked the new `Resource`/`ModelResource` loader contract, and active plan rollback/current-state text still implied the removed direct player-byte API could remain. Follow-up: `docs/ARCHITECTURE.md` is referenced by the skill but does not exist in this repo; `cpp/src/resources/resources.cpp` is now 657 lines, which is acceptable but should be watched before future resource-loader growth.
- [x] Milestone 4: Add `Resources::load_model_resource(...)` support for `ModelResource`, implemented on top of `load_blob`, including a dependency-discovery stage for internal glTF relative-resource blob requests.
- [x] (2026-07-02 22:12Z) Switched `Player` from direct blob polling/import to `Resources::load_model_resource(...)`: it stores `Ptr<ModelResource>` handles for the player mesh and animation library, polls resource state, reports resource failures, instantiates once both resources are loaded, remaps idle/walk/sprint clips, and no longer owns a `ModelResourceImportContext` or parsed GLB byte spans.
- [x] (2026-07-02 22:12Z) Updated scene tests so player update proves two default `ModelResource` requests are created, underlying generic blob requests still appear for browser transport, and root blob failure propagates through the model resource into player debug/fallback state.
- [x] (2026-07-02 22:12Z) Updated `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` so the active player path is `Player -> Ptr<ModelResource> -> Resources::load_model_resource(...)`, with TypeScript still only servicing generic blobs.
- [x] (2026-07-02 22:12Z) Ran `npm test`, `npm run format:cpp:check`, `npm run smoke:browser`, `npm run smoke:render`, and `git -c safe.directory=C:/dev/ofg diff --check`; all passed. Browser smoke report showed `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`. Screenshot evidence: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`.
- [x] (2026-07-02 22:12Z) Completed Milestone 5 review locally using the milestone-review skill. Sub-agents were not used because the available sub-agent tool requires an explicit user request for delegation. Required finding fixed: the ExecPlan context still called the direct blob-span player path current after the code moved to model resources. No new follow-ups beyond the existing missing `docs/ARCHITECTURE.md` and file-size watch on `cpp/src/resources/resources.cpp`.
- [x] Milestone 5: Switch `Player` from direct `load_blob` calls to model-resource loads and verify the player-specific byte API remains absent from C++ Embind and TypeScript.
- [x] (2026-07-02 23:26Z) Added Milestone 6 hardening tests for complete `ResourceState`/blob status diagnostics, pending root-blob scheduler state, invalid root parse failure, waiting dependency state, invalid blob ids/URIs, animation-resource player failures, resource-system setup failures, and native player self-load/bind with the checked-in Quaternius GLBs. This raised the modified resource/player implementation files above the coverage gate.
- [x] (2026-07-02 23:26Z) Ran `npm run coverage`; passed. Current checked line coverage includes `cpp/src/resources/resource.cpp` 96.67%, `cpp/src/resources/resources.cpp` 92.68%, `cpp/src/scene/player.cpp` 91.43%, and `src/app/wasmRuntime.ts` 93.70%. Refreshed `C:\dev\ofg\docs\coverage\cpp-summary.json`, `C:\dev\ofg\docs\coverage\ts-coverage-summary.json`, and `C:\dev\ofg\docs\coverage\latest.md`.
- [x] (2026-07-02 23:27Z) Ran final validation commands `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:render`, `npm run coverage`, and `git -c safe.directory=C:/dev/ofg diff --check`; all passed. `diff --check` printed only existing LF-to-CRLF working-copy warnings.
- [x] (2026-07-02 23:27Z) Final browser smoke report `C:\dev\ofg\artifacts\browser-smoke\report.json` shows `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`; screenshot evidence is `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`. Native render smoke report `C:\dev\ofg\artifacts\render-smoke\report.json` shows `"passed": true`.
- [x] (2026-07-02 23:27Z) Final legacy API grep over active `cpp`, `src`, `tests`, `docs/API_CONTRACTS.md`, and `docs/SYSTEMS.md` found no `loadPlayerModel`, `load_player_model`, `report_player_model_load_error`, `DEFAULT_PLAYER_MODEL_URL`, `DEFAULT_PLAYER_ANIMATION_URL`, `loadDefaultPlayerModel`, or `fetchAssetBytes` matches.
- [x] (2026-07-02 23:34Z) Completed Milestone 6 review locally using the milestone-review skill. Sub-agents were not used because the available sub-agent tooling requires an explicit user request for delegation. Required findings fixed: none. Follow-ups recorded: `docs/ARCHITECTURE.md` is still missing despite the skill referencing it; `cpp/src/web/browser_game.cpp` is 985 lines and should be split before more browser-facade growth; `cpp/src/resources/resources.cpp` is 741 lines and should be watched as future resource loaders land.
- [x] Milestone 6: Run coverage, smoke, screenshot, and milestone review gates; archive or update any superseded plan notes if needed.

## Surprises & Discoveries

- Observation: At planning time, browser model loading was player-specific at the host API boundary; Milestone 3 removed that direct player-byte path.
  Evidence: `C:\dev\ofg\src\app\main.ts` hardcodes `DEFAULT_PLAYER_MODEL_URL` and `DEFAULT_PLAYER_ANIMATION_URL`, fetches both, and calls `runtime.loadPlayerModel(playerBytes, animationBytes)`. `C:\dev\ofg\cpp\src\web\embind_module.cpp` exposes `load_player_model` and `report_player_model_load_error`.

- Observation: `Resources` is already the global, lifecycle-owned place for high-level asset storage.
  Evidence: `C:\dev\ofg\cpp\include\ofg\resources\resources.hpp` owns the static resource facade for textures, shaders, materials, and meshes during one WebGPU device lifetime. Extending it with blob load/cache state keeps the asset service inside the existing global resource system as requested.

- Observation: The original `GltfResourceProvider` did not solve top-level asset loading, but the Milestone 4 `Resources::load_model_resource(...)` scheduler now makes both top-level and relative glTF blob requests internal to `Resources`.
  Evidence: `C:\dev\ofg\cpp\src\resources\resources.cpp` creates a root blob request, then uses `ResourcesGltfResourceProvider` to request relative glTF resources through the same blob cache.

- Observation: Returning owning URI/error strings from `BlobView` avoids dangling views for "missing" lookups and makes diagnostics stable for callers.
  Evidence: `Resources::blob_by_uri(...)` may need to return a `Missing` result for a normalized URI that is not stored in `Resources`; a `std::string_view` would otherwise point at a temporary string.

- Observation: Dependency discovery must distinguish "missing dependency, wait" from "parse failed even though dependencies are loaded".
  Evidence: The first Milestone 4 scheduler treated any parse exception after a provider touched dependencies as `WaitingForDependencies`, creating an infinite wait once all dependencies were already loaded but parse still failed. `Resources::advance_model_load` now waits only when at least one provider-requested dependency is not loaded; otherwise it fails with the parse diagnostic.

## Decision Log

- Decision: Add blob load state to `Resources`, not a separate `AssetService` class.
  Rationale: The user explicitly wants this service to be part of the existing global `Resources` object. `Resources` already owns high-level resource lifetime for one GPU/device session, and blobs should be cached and cleared on the same lifecycle. "Blob" is the standard term here for arbitrary opaque binary data.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep TypeScript fetching generic and URI-driven.
  Rationale: Browser WASM needs the browser platform to perform `fetch`, but TypeScript should not choose gameplay assets or parse asset types. It should poll C++ for requested URIs, fetch those URIs, and report success/failure by request id.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep the first version asynchronous and frame-polled.
  Rationale: Existing runtime flow is frame based: TypeScript calls `runtime.frame(timeMs)`, then can inspect state. A frame-polled blob load list avoids blocking WASM, avoids threading assumptions, and makes retries/failures inspectable through the same debug-status path.
  Date/Author: 2026-07-02 / Codex

- Decision: Deduplicate blobs by normalized URI.
  Rationale: Multiple systems may eventually request the same texture, terrain chunk, or model. The first implementation can use URI equality as the cache key, which is simple and matches the existing model-resource import-context cache style.
  Date/Author: 2026-07-02 / Codex

- Decision: Add resource-specific model loading after `load_blob` is proven with the player.
  Rationale: `load_blob` is the low-level platform boundary, but game code should ultimately ask for domain resources. `Resources::load_model_resource(...)` can create or find the `Resources`-owned `ModelResource`, request the underlying blob, parse/import the resource when the blob is loaded, update the resource state, and return a `Ptr<ModelResource>` to the caller. The final proof is that `Player` calls the `ModelResource` loader and polls the returned resource rather than directly inspecting blob bytes.
  Date/Author: 2026-07-02 / Codex

- Decision: Resource-specific loaders should have a dependency-discovery stage before final import.
  Rationale: Some resources only know their dependencies after their root data is available. A text `.gltf` can reveal external buffers and images, and later resource types may similarly reveal textures, shaders, or child resources. The loader should be able to request discovered dependencies, wait for them, and then resume import inside `Resources`, keeping dependency orchestration out of gameplay components.
  Date/Author: 2026-07-02 / Codex

- Decision: Public resource-specific loading returns resource pointers, not load-record objects.
  Rationale: `Resources` already owns stable storage for high-level resource objects, and persistent external references should use `Ptr<T>` safe pointers. A public `ResourceLoad<T>` object that owns or appears to own the loaded resource would split ownership and make callers reason about a second lifetime model. Internal load records may exist as scheduler metadata, but the externally visible object is the resource itself, with a load state that callers can poll.
  Date/Author: 2026-07-02 / Codex

- Decision: Prefer resource-specific load APIs over a generic `load<T>` facade for now.
  Rationale: Different resource types will likely need different load options: model import flags, texture color-space choices, terrain generation settings, shader permutations, or debug labels. Starting with `Resources::load_model_resource(...)` avoids prematurely locking every resource into one rigid template signature while preserving the shared `Resource` state model and private scheduler.
  Date/Author: 2026-07-02 / Codex

- Decision: `Resources` advances resource loading through a private in-progress list.
  Rationale: Resource objects own their observable state, but the resource system still needs one place to drive transitions. Keeping a private list of non-terminal resources inside `Resources` lets each update check whether root blobs are loaded, dependencies are satisfied, imports can run, or failures should propagate. Callers only hold `Ptr<T>` and poll state; they do not manually resume resources.
  Date/Author: 2026-07-02 / Codex

- Decision: Failed blob requests remain failed until `Resources::release` in the first implementation.
  Rationale: This keeps retry semantics explicit and avoids silently reissuing browser fetches from repeated `load_blob(...)` calls. A later retry API can clear or requeue a failed entry deliberately if the game needs recoverable asset fetches.
  Date/Author: 2026-07-02 / Codex

- Decision: `BlobView` owns its URI and error snapshots, while byte data remains a span into `Resources` storage.
  Rationale: Blob lookup is not a hot loop, and copied diagnostic strings avoid dangling references for missing or transient lookup results. Blob bytes can be large, so those remain non-owning and valid only while the `Resources` blob entry remains alive.
  Date/Author: 2026-07-02 / Codex

- Decision: `complete_blob_load(...)` accepts zero-length byte blobs.
  Rationale: `load_blob` is a generic platform byte boundary, and an empty HTTP/file payload is still a well-defined blob. Resource-specific importers such as glTF loading can reject empty or invalid content with type-specific diagnostics.
  Date/Author: 2026-07-02 / Codex

- Decision: Make Milestone 2 additive and keep the old direct player byte path until the player has moved to generic blob/resource loading.
  Rationale: The generic browser transport can be validated independently without removing the only currently working player-model path. This preserves browser smoke behavior and matches the rollback guidance to delete `load_player_model` only after self-loading is proven.
  Date/Author: 2026-07-02 / Codex

- Decision: Remove the player-specific byte transport in Milestone 3 after generic blob self-loading passed browser smoke.
  Rationale: The user's core concern was that browser TypeScript should not choose or inject gameplay assets. Once `Player` could request blobs itself and browser smoke proved the model loads, keeping `load_player_model` created legacy surface area and pushed `browser_game.cpp` toward the file-size warning threshold. Removing it now keeps the supported bridge generic and leaves Milestone 5 focused on moving from blob polling to `ModelResource` polling.
  Date/Author: 2026-07-02 / Codex

- Decision: Extend `SceneUpdateContext` with optional `Scene*` and `GpuContext` fields.
  Rationale: `Player` needs the current scene and borrowed GPU handles to import its model from loaded blobs, but that should not push player-specific asset orchestration back into `Game`. Appending optional fields with a constructor preserves existing test ergonomics and lets non-resource tests omit loader context.
  Date/Author: 2026-07-02 / Codex

- Decision: Failed model-resource loads remain terminal and deduplicated until `Resources::release`.
  Rationale: This matches the blob failure policy, keeps retry semantics explicit, and avoids accidental repeated browser fetch/import loops from repeated `Resources::load_model_resource(...)` calls. A later retry API can clear or requeue failed model resources deliberately.
  Date/Author: 2026-07-02 / Codex

## Outcomes & Retrospective

The implementation now has a C++-directed asset path for the player. `Resources` owns normalized blob request/cache state, generic browser pending-blob transport, observable `Resource` state, `ModelResource` storage, and a private model-resource scheduler that loads root blobs, discovers glTF dependencies, waits for dependency blobs, imports in place, and removes terminal loads from its in-progress list without destroying the resource object. `ModelResource` inherits `Resource`, and gameplay code receives `Ptr<ModelResource>` handles rather than load-record owners.

The player now owns its hardcoded default model and animation-library URIs in C++, calls `Resources::load_model_resource(...)`, stores `Ptr<ModelResource>` values, polls resource state, binds the loaded model into the scene, hides the fallback renderer, remaps idle/walk/sprint animation clips by node name, and reports model or animation resource failures through the existing debug state. The old TypeScript/C++ player-specific byte APIs were removed; the browser host now only polls generic blob requests and completes or fails opaque byte loads by id.

Final validation passed with `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:render`, `npm run coverage`, and `git -c safe.directory=C:/dev/ofg diff --check`. Browser smoke wrote `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and reported `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`; native render smoke wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and reported `"passed": true`. Coverage summaries were refreshed under `C:\dev\ofg\docs\coverage`.

Follow-ups remain deliberately outside this plan: add an explicit retry API for failed blobs/model resources when needed, split `cpp/src/resources/resources.cpp` before future loaders make it too large, and generalize the scheduler only when a second resource-specific loader proves the shared shape.

## Contract and Quality Baseline

This plan intentionally changes `OFG-BOOT-001`, `OFG-BOOT-002`, `OFG-BOOT-003`, and the `BrowserHost` / `CppRuntime` sections of `C:\dev\ofg\docs\SYSTEMS.md`.

`OFG-BOOT-001 TypeScript Host Ownership` must preserve the rule that TypeScript does not parse glTF, choose animation poses, own gameplay simulation, or own scene graph state. The new rule should say TypeScript may fetch opaque byte requests by URI on behalf of C++.

`OFG-BOOT-002 C++ Runtime Ownership` must state that C++ chooses runtime asset URIs and owns blob load/cache state in `Resources`. `Game` should not own player model bytes or imported player resources. `Player` should own the default player model-resource load requests and model binding behavior.

`OFG-BOOT-003 WASM Facade` must replace the narrow player-specific byte API with a generic blob-load facade. The facade may expose pending blob load IDs/URIs and completion/failure calls, but it must not expose renderer internals, scene pointers, or resource objects.

`OFG-BOOT-006 Resource Lifetime` must include blob cache lifetime and resource-specific loading lifetime. Cached blob bytes and resources live in `Resources` until explicit release or until a later eviction policy is introduced. Introduce a common `Resource` base class, derived from `Object`, for resources whose load state can be observed. Loading resource objects may progress through root blob loading, dependency discovery, dependency waiting, importing, loaded, and failed states. Persistent observers remain `Ptr<T>` where appropriate. `Resources::load_model_resource(...)` returns a `Ptr<ModelResource>` to a `Resources`-owned resource object; blob load ids and private dependency handles are value handles, not raw pointers and not public resource owners. `Resources` owns the private in-progress resource list that decides which non-terminal resources should be advanced each update.

Quality constraints from `C:\dev\ofg\docs\GUIDES.md` apply: readable code, public functions documented with comments, focused modularity, and coverage at or above the current threshold unless an explicit exception is recorded. Every implementation milestone must run the repo-local `milestone-review` skill before being marked complete.

## Context and Orientation

The original player model path was temporary. Before Milestone 3, `C:\dev\ofg\src\app\main.ts` fetched two hardcoded player GLBs:

    /assets/models/player/quaternius-superhero-male.glb
    /assets/models/player/quaternius-ual1-standard.glb

It then called `BrowserGameRuntime.loadPlayerModel`, which mapped to raw Embind `load_player_model`. Milestone 3 removed that path. Milestone 5 moved the player off direct blob spans as well; the current path is `Player -> Resources::load_model_resource(...) -> Resources::load_blob(...) -> BrowserGame generic blob bridge -> TypeScript fetch -> Resources import -> Player binds loaded ModelResource`.

`Resources` currently owns GPU-context-bound high-level objects: textures, shaders, materials, and meshes. It has a static lifecycle:

    Resources::create(gpu)
    Resources::prepare()
    Resources::release()
    Resources::destroy()

This plan extends that same singleton with blob loading. A blob is an opaque byte buffer loaded from a URI such as `assets/models/player/quaternius-superhero-male.glb`. It is not yet a `Mesh`, `Texture`, `ModelResource`, or parsed glTF document. It is the source data that an importer consumes. "Blob" is also the browser/Web API term for arbitrary binary data, so it is the right low-level name for this boundary.

This plan also adds a small `Resource` base class for loadable engine resources. `Resource` should inherit `Object` so existing `Ptr<T>` safe pointers continue to work for stored references. It should expose a compact load state, the source URI where applicable, and a load error string for failed resources. `Mesh`, `Texture`, `Material`, `Shader`, and `ModelResource` already live in `Resources`-owned storage or are referenced as `Object` types; the first required migration is for `ModelResource` to become a `Resource`. Other existing resource classes can inherit `Resource` in the same milestone if the change stays mechanical and low-risk, but the player self-load proof only depends on `ModelResource`.

For browser WASM, C++ cannot directly use ordinary desktop filesystem APIs. The correct architecture is for C++ to publish requests, and for the browser host to satisfy those requests through `fetch`. This is analogous to the operating system satisfying a file read for a native executable. The important boundary is that C++ decides the URI and owns the interpretation of the bytes.

## Plan of Work

Milestone 1 adds blob load storage to `Resources`. Add small value types under `C:\dev\ofg\cpp\include\ofg\resources`, either in `resources.hpp` if compact or in a new `blob.hpp` or `blob_load.hpp` included by `resources.hpp` if the declarations would make `resources.hpp` noisy. Do not create an `AssetService` class. Prefer names like:

    using BlobLoadId = std::uint32_t;

    enum class BlobLoadStatus {
        Missing,
        Queued,
        Loading,
        Loaded,
        Failed,
    };

    struct PendingBlobLoad {
        BlobLoadId m_id;
        std::string m_uri;
    };

    struct BlobView {
        BlobLoadId m_id;
        std::string m_uri;
        BlobLoadStatus m_status;
        std::span<const std::byte> m_bytes;
        std::string m_error;
    };

Add static `Resources` methods that request, inspect, complete, and fail blob loads:

    [[nodiscard]] static BlobLoadId load_blob(std::string_view uri);
    [[nodiscard]] static BlobView blob(BlobLoadId id);
    [[nodiscard]] static BlobView blob_by_uri(std::string_view uri);
    [[nodiscard]] static std::span<const PendingBlobLoad> pending_blob_loads();
    static void mark_blob_loading(BlobLoadId id);
    static void complete_blob_load(BlobLoadId id, std::span<const std::byte> bytes);
    static void fail_blob_load(BlobLoadId id, std::string message);

The implementation stores requests in `Resources`, deduplicates by normalized URI, clears bytes and pending requests during `Resources::release`, and throws `EngineError` for invalid ids, empty URIs, unsafe URIs, or invalid state transitions. Empty byte blobs are allowed at this low-level platform boundary; resource-specific importers should reject empty or invalid typed content. The first normalization rule should be conservative: accept non-empty relative paths, normalize leading `/` away, reject URI strings containing `..`, backslashes, URL or drive-path separators, query strings, fragments, empty path segments, or `.` path segments. This keeps browser fetch rooted in packaged site assets.

Milestone 2 replaces the browser player-byte transport with a generic blob bridge. Extend `BrowserGame` with Embind-visible methods that do not mention player models:

    std::string blob_loads_json() const;
    void mark_blob_loading(double blob_id);
    void complete_blob_load(double blob_id, emscripten::val bytes);
    void fail_blob_load(double blob_id, std::string message);

Use JSON for polling because it keeps the Embind surface simple and testable from TypeScript. The JSON should be an array of objects such as:

    [{"id":1,"uri":"assets/models/player/quaternius-superhero-male.glb"}]

In `C:\dev\ofg\src\app\wasmRuntime.ts`, remove `loadPlayerModel` and `reportPlayerModelLoadError` from the public runtime interface and replace them with generic blob helpers. The wrapper should track in-flight blob load ids so it does not start duplicate browser fetches for the same pending id. It should fetch `/${uri}` for relative packaged assets, pass `Uint8Array` bytes to `complete_blob_load`, and call `fail_blob_load` on non-OK HTTP status or exceptions.

Milestone 3 makes `Player` self-load through the low-level blob API as the first proof. `Player` should store two `BlobLoadId` values for the default model and animation library. On update, or during a new explicit `request_default_model_assets()` called from player setup, it requests:

    assets/models/player/quaternius-superhero-male.glb
    assets/models/player/quaternius-ual1-standard.glb

Each frame, the player checks `Resources::blob(id)`. If either blob is queued or loading, the fallback renderer stays visible and no import occurs. If either blob failed, the player records a model-load failure message and keeps fallback visible. If both are loaded and the model is not already imported, the player calls its existing model import path using the loaded byte spans. After successful import, it hides the fallback renderer and reports loaded state.

`Game` should not expose or call `load_player_model`. Instead, `Game::update_impl` reads the primary player's model-load state and mirrors it into `RuntimeDebugStatus` fields. If a player is absent, status remains not loaded. If the player reports failure, `Game` stores the error in `m_status.m_last_error` without taking ownership of bytes or assets.

Milestone 4 adds model-resource loading on top of blobs. Add a `Resource` base class and a `Resources::load_model_resource(...)` API for `ModelResource`. `Resource` should expose a load state and error accessor so game code can poll the resource it already references. Keep the public load function resource-specific rather than templated so future model import options can be added without affecting unrelated resource types. Game code should read as a model-resource request:

    Ptr<ModelResource> player_model = Resources::load_model_resource(
        "assets/models/player/quaternius-superhero-male.glb");

`Resources::load_model_resource(...)` should return the same `Ptr<ModelResource>` for repeated calls with the same normalized URI and equivalent model load options. The returned `ModelResource` exists immediately in `Resources` storage with a pending/loading state; it is not usable for instantiation until its state is loaded. `Player` and other callers store only `Ptr<ModelResource>` and poll `resource->state()` or an equivalent resource-state accessor. They do not store a public load-record object and do not own the resource.

Internally, `Resources` should keep a private list of in-progress resources. The list can store resource pointers, resource ids, or private load-record indices, but it is non-owning with respect to the public resource object: ownership stays in the canonical `Resources` storage. Private load records may track root blob ids, discovered dependency ids, retry policy, import attempts, and transient parser state. Those records are scheduler metadata only. They must point at or index the `Resources`-owned `ModelResource`; they must not own the public resource object or define its lifetime.

The model-resource loader should automatically call `load_blob(uri)`, wait until the root blob is loaded, discover dependencies, wait for discovered dependencies, parse/import the glTF or GLB, fill the existing `ModelResource`, and move that resource to loaded or failed state. The `ModelResource` loader should keep any `ModelResourceImportContext` or equivalent resource cache inside `Resources`, not inside `Player`.

Because `load_model_resource(...)` returns a stable `Ptr<ModelResource>` before import finishes, the importer cannot replace that object with a separate `std::unique_ptr<ModelResource>` later. Adapt the model-resource build path so import can populate the already-owned `ModelResource` in place, for example by adding a `ModelResourceBuilder::build_into(ModelResource&)` helper or an internal `ModelResource::replace_contents(...)` method used only by the importer/resource system. The public pointer returned to callers must keep observing the same `ModelResource` object from queued through loaded or failed states.

Add a resource-loading update step, either named `Resources::advance_loads`, folded into `Resources::prepare` when already ready, or implemented as a private method called from the frame driver. Each update walks the in-progress list and attempts at most the valid transition for each resource's current state:

    Queued -> LoadingRootBlob, when the root blob request has been submitted
    LoadingRootBlob -> DiscoveringDependencies, when the root blob is loaded
    LoadingRootBlob -> Failed, when the root blob failed
    DiscoveringDependencies -> WaitingForDependencies, when dependencies are found and requested
    DiscoveringDependencies -> Importing, when no external dependencies are needed
    WaitingForDependencies -> Importing, when every dependency is loaded
    WaitingForDependencies -> Failed, when any dependency failed
    Importing -> Loaded, when import succeeds
    Importing -> Failed, when import throws or validation fails

Loaded and failed resources should be removed from the in-progress list after their terminal state is recorded. Repeated calls to `Resources::load_model_resource(uri)` for a terminal resource return the same resource pointer and do not put it back into the in-progress list unless an explicit retry API is later added.

Implement resource loading as a small state machine. Use the `ResourceState` values defined in Interfaces and Dependencies; the phases are:

    Queued
    LoadingRootBlob
    DiscoveringDependencies
    WaitingForDependencies
    Importing
    Loaded
    Failed

For a single-file GLB, dependency discovery should find no external dependencies and proceed directly to import. For a text `.gltf`, discovery should parse enough of the root document to enumerate external buffers and images, enqueue those dependencies through `Resources::load_blob`, and enter `WaitingForDependencies` until every required blob is loaded. If any dependency fails, the model-resource load fails with an error that includes both the root URI and the dependency URI.

For glTF files that reference external buffers or images, the `ModelResource` loader should make that internal too. Implement a `GltfResourceProvider` backed by `Resources::load_blob` for relative resources. If a `.gltf` references `mesh.bin` or `texture.png`, the provider should request `mesh.bin` or `texture.png` relative to the model URI during dependency discovery, report pending while they load, and only import the `ModelResource` once every required blob is loaded. This unifies top-level GLB loading and multi-file glTF loading under one resource system.

This dependency mechanism should be general enough that a future resource-specific loader can discover resource dependencies rather than just blob dependencies. For this plan, it is acceptable for `ModelResource` to discover only blob dependencies, but avoid an implementation that hardcodes the concept exclusively to glTF or `Player`. The preferred frame order is: browser blob completions are applied to `Resources`, `Resources` advances its in-progress resource list, `Player` observes resource states, and then normal scene component updates proceed. If implementation folds advancement into a different existing hook, document and test the exact order.

Milestone 5 switches `Player` from direct `load_blob` calls to model-resource loads. Player should request the player model and animation library through `Resources::load_model_resource(...)`, store the returned `Ptr<ModelResource>` values, poll their resource load states, and use loaded `ModelResource` values for instantiation and clip remapping. This is the proof that game code no longer consumes arbitrary bytes directly and no longer needs load records. The old player-specific byte API and TypeScript hardcoded player fetches were already retired in Milestone 3; Milestone 5 should verify they stay gone while updating `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` so they describe resource-specific model loading, resource-state polling, and final player self-loading. Update `tools\package-site.mjs` and deployment checks only if model packaging assumptions need a more generic list; the first pass can keep the explicit selected GLBs as packaged runtime assets.

Milestone 6 validates and hardens. Add C++ tests for `Resources` blob lifecycle, duplicate URI requests, invalid state transitions, request clearing on release, `ModelResource` loading from completed blobs, in-progress resource list insertion/removal, state advancement when root blobs or dependencies become ready, no duplicate in-progress entries for repeated `load_model_resource(...)` calls with the same URI/options, external-resource `.gltf` loading through dependency discovery and internal blob requests, dependency failure propagation, and player self-load behavior through the model-resource loader. Add TypeScript tests for parsing blob-load JSON, starting one fetch per id, completing loads, failing loads on HTTP errors, and not exposing player-specific load methods. Run browser smoke and inspect a screenshot showing the player model loaded via the model-resource path.

## Concrete Steps

Run these commands from `C:\dev\ofg` unless stated otherwise.

Initial discovery:

    rg -n "loadPlayerModel|load_player_model|report_player_model|assets/models/player|load_blob|blob_load|load_model_resource" cpp src tests docs

After Milestone 1:

    npm run format:cpp
    npm run format:cpp:check
    npm run test:cpp

After Milestone 2:

    npm run test:ts
    npm run build:wasm

After Milestone 3 and 5:

    npm test
    npm run smoke:browser
    npm run smoke:render

After Milestone 4:

    npm run test:cpp
    npm run build:wasm

Before completing the plan:

    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check
    rg -n "loadPlayerModel|load_player_model|report_player_model_load_error|DEFAULT_PLAYER_MODEL_URL|DEFAULT_PLAYER_ANIMATION_URL" cpp src tests docs

Expected final grep result: no player-specific byte transport API remains. References to packaged player GLB paths may remain only in C++ player code, model-resource load tests, deployment/package checks, asset audit docs, or tests that explicitly verify the selected asset exists.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective.
2. Update any changed API contracts or active docs before review.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
4. Apply required findings before marking the milestone complete, or record any rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands.
6. Record review summary, commands, screenshot paths, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

The plan is complete only when all of these behaviors are true.

The player component chooses its own default player model and animation-library URIs in C++. TypeScript no longer contains `DEFAULT_PLAYER_MODEL_URL`, `DEFAULT_PLAYER_ANIMATION_URL`, `loadDefaultPlayerModel`, `loadPlayerModel`, or `reportPlayerModelLoadError`.

`Resources` owns blob load/cache state for the active resource lifecycle. Calling `Resources::load_blob` with the same normalized URI twice returns the same load id or an equivalent stable handle; it does not start duplicate fetches. Loaded blob bytes remain available until `Resources::release` unless a later eviction policy is explicitly added.

The browser host receives only generic pending blob loads with ids and URIs. It fetches opaque bytes and reports generic completion or failure by id. It does not parse glTF, know the requested blob is part of a player model, choose animation clips, or call any player-specific API.

`Resource` exists as the common base class for loadable resources that need observable load state. `ModelResource` inherits `Resource`, and `Resources` owns all loaded or loading `ModelResource` objects for the active resource lifecycle.

`Resources::load_model_resource(...)` exists and is implemented on top of `load_blob`. It returns a `Ptr<ModelResource>` to a `Resources`-owned object immediately, even while the resource is pending. It can load a GLB from one blob and can load a multi-file `.gltf` by first discovering external buffers/images, then requesting them through the same blob layer before final import. It deduplicates model-resource loads by normalized URI and equivalent model load options by returning a pointer to the existing resource object.

Resource-specific loading supports dependencies discovered during load. A resource that discovers dependencies enters a pending/waiting state, requests each dependency through `Resources`, and resumes only when all required dependencies are loaded. Dependency failures propagate to the parent resource with enough URI context to diagnose the chain.

`Resources` owns a private in-progress resource list. Calling `Resources::load_model_resource(...)` for a new URI/options pair creates or finds the resource, puts it in that list if it is not terminal, and returns a `Ptr<ModelResource>`. Each resource update advances resources whose current prerequisites are satisfied, and terminal loaded/failed resources are removed from the in-progress list without being destroyed.

`Player` self-loads through the model-resource loader: after the scene creates a `Player`, the fallback box remains visible while model resources are pending, the player model appears after browser fetch completion and C++ import, and idle/walk/sprint movement still works. Player code should call `Resources::load_model_resource(...)`, store `Ptr<ModelResource>` values, poll the resource state, and not inspect blob bytes directly in the final implementation.

Runtime debug status still reports model loading state. Browser smoke must observe `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null` after the player model-resource loads complete.

Failure behavior is observable. If a requested blob URI fails to fetch, or if model-resource import fails, the player fallback remains visible, `modelLoadingState` becomes `"failed"`, `playerModelLoaded` remains false, and `lastError` names the failed URI and browser/network/import error.

Final validation commands must pass:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage
    git -c safe.directory=C:/dev/ofg diff --check

Coverage acceptance: every modified implementation file must pass the default coverage attention gate. If browser-only glue cannot be covered through native coverage, record the existing browser/WASM validation rationale and cover the TypeScript wrapper behavior with Mocha tests.

Screenshot acceptance: because this touches browser rendering and model loading, capture at least one browser smoke screenshot after Milestone 3 or 4 and record its path here. The human reviewer should verify that the imported player model is visible, the fallback box is hidden, and the scene still renders ground and cubes.

## Idempotence and Recovery

Calling `Resources::load_blob` repeatedly with the same normalized URI must be idempotent. It must return the existing blob load/cache entry and must not enqueue duplicate browser work. Calling `Resources::load_model_resource(...)` repeatedly with the same normalized URI and equivalent model load options must return a pointer to the same `Resources`-owned `ModelResource`, must not add duplicate in-progress entries, and must not duplicate dependency discovery or import work once loaded.

If a browser fetch is in flight and the runtime is disposed, the TypeScript wrapper should ignore late completions or catch disposed-runtime errors without crashing the page. `Resources::release` clears blob state, private load scheduler state, bytes, pending requests, and owned resource objects, so a new `Game`/`Resources` lifecycle starts cleanly and existing `Ptr<T>` observers are nulled by `Object` destruction.

If a blob load or model-resource load fails, requesting the same URI again should have an explicit policy. The first implementation should either return the failed entry until a clear/retry API is added, or allow `load_blob` / `load_model_resource(...)` with a failed URI/options pair to requeue. Choose one policy in Milestone 1 for blobs and extend it in Milestone 4 for model-resource loads, test it, and document it in the Decision Log. Prefer explicit retry only after the first version is stable.

If dependency discovery finds new work after a parent resource load has already been requested, the parent resource pointer must remain stable. Callers should not need to re-request the parent resource manually; the resource system advances its state as dependencies become available.

If a resource remains blocked because blobs or dependencies are still pending, it stays in the in-progress list without busy work beyond checking the relevant dependency states during the next resource update. The first implementation does not need a priority scheduler, but it should make future throttling or per-frame import budgeting possible.

Rollback after Milestone 3 no longer uses the deleted direct `load_player_model` path. If Milestone 5 needs to be backed out, return `Player` to the already-tested interim `Resources::load_blob(...)` self-load path while keeping TypeScript on the generic blob bridge.

## Artifacts and Notes

Historical temporary player-specific path found during planning and removed in Milestone 3:

    C:\dev\ofg\src\app\main.ts
      DEFAULT_PLAYER_MODEL_URL
      DEFAULT_PLAYER_ANIMATION_URL
      loadDefaultPlayerModel(...)
      runtime.loadPlayerModel(playerBytes, animationBytes)

    C:\dev\ofg\src\app\wasmRuntime.ts
      BrowserGameRuntime.loadPlayerModel(...)
      RawBrowserGame.load_player_model(...)

    C:\dev\ofg\cpp\src\web\embind_module.cpp
      .function("load_player_model", &ofg::BrowserGame::load_player_model)

    C:\dev\ofg\cpp\src\web\browser_game.cpp
      BrowserGame::load_player_model(...)
      BrowserGame::accept_player_model_bytes(...)

These were removed in Milestone 3 and should stay absent. The current supported bridge is the generic blob-load API plus, from Milestone 4 onward, `Resources::load_model_resource(...)` inside C++.

## Interfaces and Dependencies

Final C++ public resource interface should include value-handle blob APIs on `Resources`. Put the declarations in `C:\dev\ofg\cpp\include\ofg\resources\resources.hpp`, or move the value types to a small included header such as `blob_load.hpp` if `resources.hpp` becomes noisy. The public blob API is:

    using BlobLoadId = std::uint32_t;
    inline constexpr BlobLoadId invalid_blob_load_id = 0;

    enum class BlobLoadStatus {
        Missing,
        Queued,
        Loading,
        Loaded,
        Failed,
    };

    [[nodiscard]] const char* blob_load_status_name(BlobLoadStatus status) noexcept;

    struct PendingBlobLoad {
        BlobLoadId m_id{invalid_blob_load_id};
        std::string m_uri;
    };

    struct BlobView {
        BlobLoadId m_id{invalid_blob_load_id};
        std::string m_uri;
        BlobLoadStatus m_status{BlobLoadStatus::Missing};
        std::span<const std::byte> m_bytes;
        std::string m_error;
    };

    BlobLoadId Resources::load_blob(std::string_view uri);
    BlobView Resources::blob(BlobLoadId id);
    BlobView Resources::blob_by_uri(std::string_view uri);
    std::span<const PendingBlobLoad> Resources::pending_blob_loads();
    void Resources::mark_blob_loading(BlobLoadId id);
    void Resources::complete_blob_load(BlobLoadId id, std::span<const std::byte> bytes);
    void Resources::fail_blob_load(BlobLoadId id, std::string message);

Blob ids are assigned by `Resources`, start at 1, and are stable until `Resources::release`. `Resources::load_blob(uri)` normalizes and validates the URI, deduplicates by normalized URI, returns the existing id if present, and creates a `Queued` entry otherwise. `Resources::blob(id)` throws `EngineError` for `invalid_blob_load_id` or unknown ids. `Resources::blob_by_uri(uri)` returns a `Missing` view when no entry exists. `Resources::pending_blob_loads()` returns only `Queued` requests; `mark_blob_loading(id)` moves `Queued` to `Loading` and removes it from the pending list. `complete_blob_load` and `fail_blob_load` are valid for `Loading` entries only in the first implementation.

Final resource loading system should include a `Resource` base class, resource-specific load functions, and one defined place where load state advances. Put `Resource` in `C:\dev\ofg\cpp\include\ofg\resources\resource.hpp` and include it from resources that expose load state. The target `Resource` API is:

    enum class ResourceState {
        Unloaded,
        Queued,
        LoadingRootBlob,
        DiscoveringDependencies,
        WaitingForDependencies,
        Importing,
        Loaded,
        Failed,
    };

    [[nodiscard]] const char* resource_state_name(ResourceState state) noexcept;

    class Resource : public Object {
    public:
        Resource(const Resource&) = delete;
        Resource& operator=(const Resource&) = delete;
        Resource(Resource&&) = delete;
        Resource& operator=(Resource&&) = delete;
        ~Resource() override = default;

        [[nodiscard]] ResourceState state() const noexcept;
        [[nodiscard]] const std::string& source_uri() const noexcept;
        [[nodiscard]] const std::string& load_error() const noexcept;
        [[nodiscard]] bool is_in_progress() const noexcept;
        [[nodiscard]] bool is_loaded() const noexcept;
        [[nodiscard]] bool is_failed() const noexcept;
        [[nodiscard]] bool is_terminal() const noexcept;

    protected:
        Resource() noexcept = default;

    private:
        friend class Resources;

        void set_source_uri(std::string source_uri);
        void set_resource_state(ResourceState state) noexcept;
        void set_resource_failed(std::string message);
        void clear_resource_error();

        ResourceState m_state{ResourceState::Unloaded};
        std::string m_source_uri;
        std::string m_load_error;
    };

Only `Resources` should mutate `Resource` state in the first implementation. Callers and components may read state, source URI, and error text, but they must not drive transitions manually. `Unloaded` means the object exists but is not scheduled. `Queued`, `LoadingRootBlob`, `DiscoveringDependencies`, `WaitingForDependencies`, and `Importing` are in-progress states and imply membership in the private in-progress resource list. `Loaded` and `Failed` are terminal states and imply the resource has been removed from that list.

`ModelResource` should inherit `Resource`:

    class ModelResource : public Resource {
        ...
    };

Because `Resources::load_model_resource(...)` returns a `Ptr<ModelResource>` before import completes, the imported content must be written into the same object. Adapt the builder/importer path to support in-place population, for example:

    class ModelResourceBuilder {
    public:
        void build_into(ModelResource& resource);
    };

or an equivalent private `ModelResource` content replacement API. Do not create a placeholder `ModelResource`, return a pointer to it, then replace it with a different `unique_ptr`.

The first resource-specific loader API is:

    struct ModelResourceLoadOptions {
        std::string m_model_name;
    };

    Ptr<ModelResource> Resources::load_model_resource(
        std::string uri,
        ModelResourceLoadOptions options = {});

    void Resources::advance_loads();
    std::span<const std::unique_ptr<ModelResource>> Resources::model_resources();

`ModelResourceLoadOptions::m_model_name` is optional. When empty, derive a stable model name from the normalized URI stem. The model-resource cache key is the normalized URI plus the effective model name; if later options change imported output, those options must also become part of the cache key. `Resources::load_model_resource(uri, options)` returns a `Ptr<ModelResource>`, creates a `Queued` resource when the key is new, adds it to the private in-progress list, and returns the existing resource when the key already exists. It internally calls `load_blob` during advancement; caller code does not parse blobs manually. Do not add a generic `load<T>` facade unless a later plan has at least two resource-specific loaders and a real shared call shape has emerged.

`Resources::advance_loads()` is the public frame hook for the first implementation. It walks the private in-progress list once and advances each resource by at most one major state transition. `Game::update_impl` should call it after browser blob completions have been applied and before `Player` observes model-resource state. Private load records may contain scheduler-only states, ids, dependency lists, and counters, but those records must not be part of the public ownership model.

Final browser runtime interface should include generic blob-load handling. Exact method names may change, but it should expose:

    blobLoads(): readonly BlobLoadRequest[];
    completeBlobLoad(id: number, bytes: Uint8Array): void;
    failBlobLoad(id: number, message: string): void;

The TypeScript app should call a generic blob pump from the animation frame loop or runtime wrapper. It may use browser `fetch`, but it must not know the semantics of the URI beyond treating it as an opaque packaged blob path.

`Player` should own constants or configuration for:

    assets/models/player/quaternius-superhero-male.glb
    assets/models/player/quaternius-ual1-standard.glb

No TypeScript file should hardcode those player model paths after this plan completes, except possibly deployment/package validation scripts that ensure selected runtime assets are included in `.deploy`.
