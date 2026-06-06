# GLTF Character Loading And Animation

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it and link back to it by path.

## Purpose / Big Picture

Add a Rust-owned GLTF/GLB model path for OFG, then layer animation on top until
the player character visibly plays a walk animation while moving. After this
plan is complete, the browser game no longer uses the yellow box as the normal
debug player representation. Instead, the Rust scene contains a character model
loaded from GLB, the Rust renderer draws it through scene mesh renderer items,
and the Rust player state chooses and blends character animation clips from
movement state.

This is a new feature plan. The scene/component plan in
`docs/RUST_SCENE_COMPONENT_PLAN.md` is complete and is only background context.

The staged outcome is:

1. Static GLB meshes can be loaded, attached to scene entities, and rendered.
2. GLTF node animation clips can be sampled and applied to non-skinned nodes.
3. GLTF skinned meshes can be posed from skeleton joints and animation clips.
4. Animation state can blend idle and walk clips.
5. The player character plays the walk animation when movement input is active
   and returns to idle when movement stops.

## Progress

- [x] (2026-06-06) Confirmed with the user that this is a new feature plan, not
  an extension of the finished Rust scene/component plan.
- [x] (2026-06-06) Re-read `PLANS.md`, `docs/API_CONTRACTS.md`,
  `docs/ARCHITECTURE.md`, `docs/RUST_SCENE_COMPONENT_PLAN.md`, and current
  scene/renderer files to anchor the plan on the finished Rust scene path.
- [x] (2026-06-06) Checked current GLTF facts and source assets: Khronos glTF
  2.0 defines scenes/nodes/meshes/materials/textures/skins/animations and GLB;
  the Rust `gltf` crate exposes mesh readers for positions, normals, indices,
  texcoords, joints, and weights plus animation readers; Quaternius provides
  CC0 base characters and a CC0 GLB animation library.
- [x] (2026-06-06) Updated `docs/API_CONTRACTS.md` with `OFG-API-010`, a
  GLTF/model asset boundary that keeps TypeScript limited to generic byte
  fetching and keeps GLTF parsing, animation, skinning, and renderer resource
  resolution in Rust.
- [x] (2026-06-06) Added the asset acquisition rule: download small Khronos
  glTF Sample Assets for importer/render/animation/skinning tests, and download
  the Quaternius Universal Base Characters pack for the real humanoid character
  once the static GLB path is ready.
- [x] (2026-06-06) Implemented milestone 1: downloaded Khronos fixtures into
  `assets/models/test-fixtures/`, added `SOURCE.md`, extended the browser asset
  loader with opaque byte requests, added a Rust-owned GLB/glTF importer
  foundation in `crates/engine_web/src/model_assets.rs`, and covered it with
  unit tests.
- [x] (2026-06-06) Implemented milestone 2: the browser runtime fetches the
  Khronos `Box.glb` fixture through `loadBytes`, imports it in Rust, registers
  a static model mesh/material, attaches it to a Rust scene mesh renderer item,
  and draws it through a dedicated model vertex pipeline.
- [x] (2026-06-06) Implemented milestone 3: downloaded Khronos
  `BoxAnimated.glb`, added Rust GLTF animation import/sampling for
  translation, rotation, and scale channels, applied sampled node transforms to
  the Rust scene item, and extended browser smoke to verify the Rust animation
  clock advances.
- [x] (2026-06-06) Implemented milestone 4: imported GLTF `JOINTS_0`,
  `WEIGHTS_0`, skin joint lists, and inverse bind matrices, added CPU skinning
  for sampled poses, rendered Khronos `RiggedSimple.glb` as a posed skinned
  model, and extended browser smoke to verify Rust CPU skinning state.
- [x] (2026-06-06) Implemented milestone 5: downloaded Quaternius Universal
  Base Characters and Universal Animation Library 2 standard packs, checked in
  selected CC0 player GLBs with source notes, added Rust idle/walk locomotion
  selection and blending, updates the CPU-skinned mesh vertices every frame,
  and extended browser smoke to verify `W` selects walk and release blends back
  to idle.

## Surprises & Discoveries

- Observation: The current scene path is ready for model instances, but the
  renderer still resolves only the built-in debug marker mesh/material label.
  Evidence: `crates/engine_web/src/wgpu_renderer.rs` resolves
  `DEBUG_PLAYER_MARKER_MESH_LABEL` and `DEBUG_PLAYER_MARKER_MATERIAL_LABEL`
  specially; unknown scene mesh/material labels currently error.
- Observation: The current WebGPU pipeline is terrain-shaped. It can draw
  non-terrain scene mesh items only because the debug marker is packed into the
  19-float terrain vertex layout and uses fallback textures.
  Evidence: `create_main_pipeline` in
  `crates/engine_web/src/wgpu_renderer.rs` has a vertex buffer layout using
  `TERRAIN_VERTEX_FLOATS` and shader locations for material indices and weights.
- Observation: Before milestone 1, the browser asset loader was
  image-array-only, so GLB bytes needed a generic byte asset request before Rust
  could own GLTF parsing.
  Evidence: pre-milestone `src/engine/browser/textureAssetLoader.ts` exposed
  only `loadTextureArrays(requests)`.
- Observation: The Rust `gltf` crate default feature set pulls image decoding
  dependencies that require newer Cargo support than this repository currently
  uses.
  Evidence: `cargo test -p engine_web` failed when default features selected
  `image` through `gltf` import support because `moxcms-0.8.1` requires the
  unstable `edition2024` Cargo feature on Cargo 1.78.0.
- Observation: The first importer should reject file-relative external buffers
  until the browser/Rust asset handoff can resolve multi-file glTF assets
  intentionally.
  Evidence: `animated-cube.gltf` references `AnimatedCube.bin`; the test
  `gltf_importer_rejects_file_relative_external_buffers` verifies this returns
  `UnsupportedExternalBuffer`.
- Observation: The first rendered static GLB can be validated through existing
  renderer debug counts without adding TypeScript model semantics.
  Evidence: `tools/browser-smoke.mjs` now compares terrain render chunk count
  to Rust/wgpu mesh, object, and draw counts, expecting terrain chunks plus the
  marker mesh and imported model mesh.
- Observation: The GLTF loader/parser split should stay narrow as animation
  work begins.
  Evidence: milestone 2 moved the wasm `assetLoader.loadBytes` bridge into
  `crates/engine_web/src/model_asset_loader.rs`, keeping
  `crates/engine_web/src/model_assets.rs` focused on model import and packing.
- Observation: Khronos `BoxAnimated.glb` is the right compact binary fixture
  for the first runtime node-animation path; the earlier `AnimatedCube.gltf`
  fixture is useful for external-buffer rejection, not browser runtime loading.
  Evidence: `assets/models/test-fixtures/box-animated.glb` is a checked-in GLB
  with translation and rotation animation channels, while
  `animated-cube.gltf` references a sidecar `.bin`.
- Observation: Runtime model placement needs to stay separate from imported
  node-local animation.
  Evidence: milestone 3 uses a placed/scaled Rust scene root entity with the
  imported mesh node as a child, so sampled GLTF local transforms can move the
  mesh without overwriting the world placement used to show the model in terrain.
- Observation: Some sample animation clips are unnamed, so smoke validation
  should trust Rust runtime/time/duration fields rather than require a non-empty
  clip label.
  Evidence: browser smoke reports `runtime: "rust"` and advances animation time
  for the Khronos `BoxAnimated.glb` clip even though the active clip string is
  empty.
- Observation: Khronos `RiggedSimple.glb` is a useful first skinning runtime
  fixture, but its source scale is too large for the existing static model
  placement.
  Evidence: the first smoke pass rendered the CPU-skinned sample huge in
  first-person view; milestone 4 added configurable static-model scene scale and
  uses `0.45` for the rigged fixture.
- Observation: CPU skinning can be proven without a new GPU skinning pipeline,
  but the first runtime path is a baked sampled pose, not a per-frame skinned
  mesh update.
  Evidence: `crates/engine_web/src/model_skinning.rs` computes joint matrices
  and skins vertices in tests and startup model preparation; browser smoke
  verifies `modelSkinning.runtime == "rust-cpu"` and a positive joint count.
- Observation: Model render preparation needed a split before the renderer file
  grew further.
  Evidence: milestone 4 extracts `crates/engine_web/src/model_render_assets.rs`
  for skinned render-asset baking, while `wgpu_renderer.rs` remains oversized
  and should not absorb dynamic skinning/blending logic.
- Observation: Quaternius' free Itch downloads can be fetched non-interactively
  through the standard `download_url` and `file/<upload_id>` endpoints once the
  CSRF token is read from the purchase/download pages.
  Evidence: milestone 5 downloaded
  `Universal Base Characters[Standard].zip` and
  `Universal Animation Library 2[Standard].zip` into
  `artifacts/quaternius-downloads/`, then committed only selected assets under
  `assets/models/player/`.
- Observation: The Quaternius Universal Animation Library 2 GLB is the best
  first runtime player asset because it combines a skinned mannequin mesh, a
  65-joint humanoid skeleton, and named animation clips in one GLB.
  Evidence: `assets/models/player/quaternius-ual2-standard.glb` imports with
  43 clips, including `Idle_FoldArms_Loop` and `Walk_Carry_Loop`, while the
  Universal Base Characters pack provides external-buffer `.gltf` character
  files that need conversion or future retargeting work.
- Observation: Per-frame CPU skinning is acceptable for this first player
  slice when limited to one selected primitive.
  Evidence: browser smoke on 2026-06-06 rendered the Quaternius character,
  updated the model vertex buffer every frame through Rust/wgpu, and held
  steady at a reported `16.7 ms` frame time in the sampled smoke run.

## Decision Log

- Decision: Use GLB as the checked-in runtime model format, while allowing GLTF
  fixtures during tests if they make importer coverage easier.
  Rationale: GLB is the single-file binary form of glTF, so it keeps character
  model, mesh buffers, animation data, and often images together. That is less
  brittle for browser asset loading and deployment.
  Date/Author: 2026-06-06 / Codex.
- Decision: Parse GLTF in Rust, not TypeScript.
  Rationale: TypeScript must remain browser startup/input/HUD glue. Rust owns
  scene state, render extraction, player state, and renderer resources. A
  TypeScript GLTF scene mirror would violate `OFG-API-009`.
  Date/Author: 2026-06-06 / Codex.
- Decision: Add a generic byte asset request to the browser asset loader rather
  than a GLTF-specific TypeScript loader.
  Rationale: Browser `fetch` is a convenient substrate, but TypeScript should
  not parse GLTF, choose meshes/materials, inspect nodes, or own animation data.
  A byte loader preserves Rust ownership while reusing browser fetch.
  Date/Author: 2026-06-06 / Codex.
- Decision: Build static GLB rendering before animation.
  Rationale: Mesh, material, resource lifetime, labels, scene attachment, and
  browser smoke validation all need to work before animation can be debugged
  sanely. Animation bugs are easier to see once a known model renders.
  Date/Author: 2026-06-06 / Codex.
- Decision: Implement node transform animation before skinned animation.
  Rationale: GLTF animation sampling, interpolation, clip time wrapping, and
  scene transform updates can be validated without the additional joint matrix
  and skinning math. This de-risks the skeleton milestone.
  Date/Author: 2026-06-06 / Codex.
- Decision: Implement CPU skinning first unless profiling proves it is too slow
  for one player.
  Rationale: CPU skinning keeps the first skinned milestone inspectable and
  avoids coupling pose correctness to a new WGSL joint-buffer pipeline. The
  plan still leaves a path to move skinning into a GPU pipeline afterward.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use Quaternius as the preferred character/animation source.
  Rationale: Quaternius Universal Base Characters are CC0, rigged humanoids
  available in glTF, and compatible with the Universal Animation Library. The
  Universal Animation Library 2 is CC0 and available as GLB, giving a clean
  checked-in prototype source.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use Khronos sample assets as importer fixtures before relying on
  the Quaternius humanoid pack.
  Rationale: Khronos sample assets are small, feature-focused, and intended to
  exercise glTF capabilities. They are better for deterministic importer tests
  than a production character pack. Quaternius should be used for the final
  humanoid player model and idle/walk source clips after the loader can already
  prove static mesh, animation, and skinning behavior.
  Date/Author: 2026-06-06 / Codex.
- Decision: Add `gltf` to `engine_web` with `default-features = false` and
  only `utils` and `names`, plus a tiny local base64 data-URI buffer resolver.
  Rationale: The first milestone needs mesh/node/material buffer parsing, not
  image decoding. Avoiding the crate's default importer keeps the dependency
  compatible with the repo's Cargo 1.78.0 toolchain and keeps image/material
  policy out of this first slice.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep the first parser module in `engine_web`.
  Rationale: The browser-fetched bytes arrive at the browser-facing Rust crate,
  and the next milestone needs renderer-side resource upload. Pure model and
  animation data can move toward `engine_core` once the runtime ownership split
  is clearer.
  Date/Author: 2026-06-06 / Codex.
- Decision: Give static GLB meshes a dedicated model vertex layout and pipeline
  entry point.
  Rationale: Packing model primitives into the terrain 19-float vertex layout
  would blur the terrain contract and make skinning harder. A 12-float
  position/normal/uv/color layout with `modelVertexMain` keeps the current
  static model path explicit and testable.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep the first visible static-render fixture as Khronos `Box.glb`.
  Rationale: It is tiny, checked in, deterministic, and already covered by the
  importer tests. Later animation milestones can switch the live fixture to an
  animated sample, while the Quaternius humanoid pack remains the right source
  for the player character once animation and skinning paths exist.
  Date/Author: 2026-06-06 / Codex.
- Decision: Move node animation import and sampling into a dedicated
  `model_animation.rs` Rust module.
  Rationale: Animation channel validation, time wrapping, step/linear
  interpolation, and quaternion slerp are already enough logic to deserve a
  focused module. Keeping that logic out of the renderer reduces pressure on
  the already-large renderer file before skinning work begins.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use Khronos `BoxAnimated.glb` as the first runtime animation
  fixture.
  Rationale: It is a compact binary GLB with node translation and rotation
  channels, so it exercises the runtime byte loader and animation sampler
  without requiring external buffer resolution or skeleton support.
  Date/Author: 2026-06-06 / Codex.
- Decision: Expose model animation runtime, clip, time, and duration only as
  Rust debug snapshot fields.
  Rationale: Smoke tests and HUD/debug hooks need observability, but TypeScript
  must not choose clips, sample animation, inspect nodes, or own model state.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use Khronos `RiggedSimple.glb` as the first runtime skinning
  fixture.
  Rationale: It is already checked in, compact, has two skin joints, contains a
  GLB animation clip, and is simple enough for deterministic CPU-skinning tests
  before the Quaternius humanoid assets enter the runtime path.
  Date/Author: 2026-06-06 / Codex.
- Decision: Bake one sampled CPU-skinned pose at startup for milestone 4 rather
  than adding dynamic mesh-buffer updates immediately.
  Rationale: This proves GLTF joints, weights, inverse binds, sampled skeleton
  pose math, and model-pipeline rendering without coupling correctness to a new
  renderer update lifecycle. Per-frame skinning remains milestone 5/follow-up
  work.
  Date/Author: 2026-06-06 / Codex.
- Decision: Split model render-asset preparation into `model_render_assets.rs`.
  Rationale: The WebGPU facade is already oversized. Model primitive selection,
  material packet creation, and CPU-skinned vertex baking are pure Rust
  preparation steps with direct tests, not WebGPU surface/device logic.
  Date/Author: 2026-06-06 / Codex.
- Decision: Expose skinning runtime and joint count only as Rust debug snapshot
  fields.
  Rationale: Browser smoke needs to observe that the rendered model came through
  Rust CPU skinning, while TypeScript still must not inspect or process GLTF
  skins, joints, weights, or skeletons.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use Quaternius UAL2 `Idle_FoldArms_Loop` and `Walk_Carry_Loop` for
  the first movement-driven player runtime.
  Rationale: These clips are named, loopable, and live in the same GLB and
  skeleton as the skinned mannequin mesh, avoiding retargeting complexity while
  proving Rust-owned locomotion selection, blending, and dynamic CPU skinning.
  Date/Author: 2026-06-06 / Codex.
- Decision: Commit selected Quaternius GLBs and source notes, not the downloaded
  source zips.
  Rationale: The standard source zips are useful acquisition artifacts but too
  broad for runtime. The repo needs only `quaternius-ual2-standard.glb`, a
  converted `quaternius-superhero-male.glb` for future retargeting, and a
  concise CC0 source note under `assets/models/player/`.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep CPU skinning single-primitive for milestone 5.
  Rationale: One primitive is enough to prove visible idle/walk behavior and
  same-size WebGPU vertex-buffer updates. Multi-primitive model assembly and GPU
  skinning are separable follow-ups.
  Date/Author: 2026-06-06 / Codex.

## Outcomes & Retrospective

Milestone 1 is complete. The browser asset loader now supports
`loadBytes(requests)` for opaque byte fetches without TypeScript model
semantics, while preserving the existing `loadTextureArrays(requests)` texture
path. `engine_web` can import checked-in GLB or embedded-buffer glTF bytes into
Rust-owned model nodes, materials, primitives, vertices, and indices. Tests
cover a static Khronos `Box.glb`, an embedded `SimpleSkin.gltf` skin-count
fixture for later work, and rejection of file-relative external buffers.

Validation completed on 2026-06-06:

    cargo test -p engine_core
    cargo test -p engine_web
    npx tsc -p tsconfig.json --noEmit
    npm test
    git -c safe.directory=C:/dev/ofg diff --check -- docs/GLTF_CHARACTER_PLAN.md src/engine/browser/textureAssetLoader.ts src/engine/browser/textureAssetLoader.test.ts src/engine/web/engineWebWasm.test.ts crates/engine_web/Cargo.toml crates/engine_web/src/lib.rs crates/engine_web/src/model_assets.rs crates/engine_web/src/tests.rs assets/models/test-fixtures/SOURCE.md docs/API_CONTRACTS.md docs/ARCHITECTURE.md

`npm test` passed with 58 tests after fixing a brittle byte-loader test
expectation. Browser smoke was not run for milestone 1 because no rendered
browser behavior changed; it becomes required in milestone 2 when imported GLB
meshes are drawn.

Milestone review:

- Scope: milestone 1 asset pipeline and static GLB import tests.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agents were not used because the available sub-agent
  tool requires explicit user permission for delegation.
- Required findings fixed: updated `docs/ARCHITECTURE.md` and
  `OFG-API-002` in `docs/API_CONTRACTS.md` so active docs mention the new
  opaque byte lane and still forbid TypeScript model semantics.
- Follow-ups recorded: milestone 2 must add a real model renderer vertex layout
  and browser smoke; future milestones must intentionally handle multi-file glTF
  assets if they are needed.
- Rejected findings: none.
- Remaining risk: `crates/engine_web/src/wgpu_renderer.rs` is still oversized,
  and model rendering should be split rather than growing that file further.

Milestone 2 is complete. The runtime now loads
`/assets/models/test-fixtures/static-box.glb` through the generic browser
`loadBytes` lane, imports the GLB in Rust, packs the first primitive into a
12-float static model vertex buffer, registers a Rust/wgpu model mesh and
material packet, and resolves it from a Rust scene mesh renderer item. The
terrain-shaped pipeline remains unchanged for terrain and the debug marker;
static models draw through `modelVertexMain` and a separate model vertex buffer
layout.

Validation completed on 2026-06-06:

    cargo test -p engine_core
    cargo test -p engine_web
    npm run check:shaders
    npm test
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg diff --check

Browser smoke passed with artifacts in
`C:\dev\ofg\artifacts\browser-smoke\2026-06-06T19-55-47-524Z`. The report
showed Rust/wgpu runtime, 11 terrain render chunks, 13 mesh resources, 12
objects, and 12 frame draws, which covers terrain plus the marker/model
resources. The first-person and debug-fly screenshots were inspected and show
the imported red GLB box in the terrain scene, with the yellow marker still
available in debug-fly mode.

Milestone review:

- Scope: milestone 2 static GLB runtime loading, scene attachment, shader
  layout, renderer registration/draw, generated artifacts, and browser smoke.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agents were not used because the user did not
  explicitly ask for delegated review.
- Required findings fixed: updated `docs/API_CONTRACTS.md` and
  `docs/ARCHITECTURE.md` so active docs describe static GLB loading/rendering
  as live, and split the wasm byte-loader bridge out of `model_assets.rs`.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs` remains over
  the preferred file size and must be split before adding substantial
  animation/skinning renderer code.
- Rejected findings: none.
- Remaining risk: milestone 2 renders only the first primitive/material from a
  small fixture. Multi-primitive models, material textures, node hierarchies as
  scene children, and real humanoid assets remain future milestones.

Milestone 3 is complete. The runtime now loads
`/assets/models/test-fixtures/box-animated.glb` through the generic browser
`loadBytes` lane, imports GLTF animation channels in Rust, samples translation,
rotation, and scale at clip-local looping time, applies the sampled node-local
transform to the imported mesh child entity, and exposes Rust-owned animation
debug fields for smoke tests. The earlier `animated-cube.gltf` fixture remains
useful for external-buffer rejection; the live animation fixture is a checked-in
GLB.

Validation completed on 2026-06-06:

    cargo test -p engine_core
    cargo test -p engine_web
    npm test
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg diff --check

Browser smoke passed with artifacts in
`C:\dev\ofg\artifacts\browser-smoke\2026-06-06T20-15-40-666Z`. The report
showed Rust/wgpu runtime, 13 mesh resources, 12 objects, 12 frame draws, and
Rust model animation time advancing from `1.1165851` to `1.4165901` seconds on
a `3.7083299` second clip. The first-person and debug-fly screenshots were
inspected and show the animated GLB box in the terrain scene, with the yellow
debug marker still visible separately in debug-fly mode.

Milestone review:

- Scope: milestone 3 non-skinned GLTF node animation import/sampling, scene
  transform updates, debug snapshot fields, checked-in Khronos animation
  fixture, generated wasm artifacts, and browser smoke validation.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agents were not used because the available delegation
  tool requires the user to explicitly request sub-agents.
- Required findings fixed: updated `docs/API_CONTRACTS.md` and
  `docs/ARCHITECTURE.md` so active docs describe node animation as live, and
  hardened browser smoke so animation clock wraparound is not treated as a
  failure.
- Follow-ups recorded: `crates/engine_web/src/model_assets.rs` is now just over
  the 600-line split-pressure threshold, and
  `crates/engine_web/src/wgpu_renderer.rs` remains oversized. Split model import
  and renderer support before adding substantial skinning/GPU update code.
- Rejected findings: none.
- Remaining risk: milestone 3 only animates one non-skinned primitive node from
  one small fixture. Multi-node hierarchy propagation, multi-primitive model
  instances, skins, animation blending, and locomotion-driven clip selection
  remain future milestones.

Milestone 4 is complete. The importer now preserves `JOINTS_0`, `WEIGHTS_0`,
skin joint node lists, and inverse bind matrices. `model_skinning.rs` computes
model node world matrices, evaluates skin joint matrices, and CPU-skins vertices
into the existing static model vertex format. Runtime now loads Khronos
`RiggedSimple.glb`, samples its first clip for one posed skinning bake, registers
that posed mesh through Rust/wgpu, and exposes `modelSkinningRuntime` set to
`"rust-cpu"` plus joint count through Rust debug snapshot state.

Validation completed on 2026-06-06:

    cargo test -p engine_core
    cargo test -p engine_web
    npm run check:wasm
    npm test
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg diff --check

Browser smoke passed with artifacts in
`C:\dev\ofg\artifacts\browser-smoke\2026-06-06T20-38-07-657Z`. The report
showed Rust/wgpu runtime, 13 mesh resources, 12 objects, 12 frame draws, Rust
model animation time advancing from `1.1332649` to `1.4498447` seconds on a
`2.0833330` second clip, and Rust CPU skinning with 2 joints. The first-person
and debug-fly screenshots were inspected and show the green posed skinned sample
in terrain, with the yellow debug marker still visible separately in debug-fly
mode.

Milestone review:

- Scope: milestone 4 GLTF skin import, joint/weight preservation, inverse bind
  import, CPU skinning math, render-asset baking, Rust debug snapshot skinning
  fields, generated wasm artifacts, and browser smoke validation.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agents were not used because the available delegation
  tool requires the user to explicitly request sub-agents.
- Required findings fixed: updated `docs/API_CONTRACTS.md` and
  `docs/ARCHITECTURE.md` so active docs describe CPU-skinned posed rendering as
  live, split model render-asset baking into `model_render_assets.rs`, and added
  fixture-specific model scale so the skinned sample remains visually
  inspectable in smoke screenshots.
- Follow-ups recorded: `crates/engine_web/src/model_assets.rs` remains over the
  600-line split-pressure threshold and `crates/engine_web/src/wgpu_renderer.rs`
  remains over 1000 lines. Continue splitting before adding per-frame skinned
  mesh updates, animation blending, or player-character runtime selection.
- Rejected findings: none.
- Remaining risk: milestone 4 bakes one sampled CPU-skinned pose at startup.
  It does not yet update skinned vertices every frame, blend clips, load the
  Quaternius humanoid player, or choose idle/walk from player movement.

Milestone 5 is complete. The repo now includes selected Quaternius CC0 player
assets under `assets/models/player/`: `quaternius-ual2-standard.glb` copied from
Universal Animation Library 2 Standard, `quaternius-superhero-male.glb`
converted from the Universal Base Characters standard `.gltf` plus `.bin`, and
`SOURCE.md` documenting source URLs, license, and extraction notes. Runtime now
loads the UAL2 GLB through the generic byte loader, imports the skinned humanoid
in Rust, samples `Idle_FoldArms_Loop` and `Walk_Carry_Loop`, crossfades between
them from horizontal movement input, CPU-skins the selected primitive each
frame, and updates the existing model vertex buffer through Rust/wgpu.

Validation completed on 2026-06-06:

    cargo test -p engine_web
    npm run check:wasm
    npm test
    npm run smoke:browser

`npm run check:wasm` initially reported stale generated `engine_web` wasm
artifacts after the Rust changes; `npm run build:wasm` regenerated them and the
subsequent `npm test`/smoke builds completed cleanly. Browser smoke passed with
artifacts in `C:\dev\ofg\artifacts\browser-smoke\2026-06-06T21-08-52-884Z`.
The report showed Rust/wgpu runtime, 13 mesh resources, 12 objects, 12 frame
draws, holding `W` reaching active `Walk_Carry_Loop`, releasing `W` selecting
`Idle_FoldArms_Loop` as `nextClip` with blend weight `0.09333333`, settling
back to active `Idle_FoldArms_Loop` with blend weight `0`, and Rust CPU skinning
with 65 joints. First-person and debug-fly screenshots were inspected and show
the orange Quaternius humanoid standing in the terrain; the yellow debug marker
remains visible only in debug-fly mode.

Milestone review:

- Scope: milestone 5 Quaternius asset acquisition, selected player GLB source
  notes, Rust locomotion animation controller, transform blending, per-frame CPU
  skinning, same-size WebGPU model vertex-buffer updates, debug snapshot blend
  fields, generated wasm artifacts, TypeScript debug forwarding, and browser
  smoke locomotion validation.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were done locally. Sub-agents were not used because the available delegation
  tool requires the user to explicitly request sub-agents.
- Required findings fixed: tightened browser smoke so holding `W` must reach
  active `Walk_Carry_Loop`, releasing `W` must select `Idle_FoldArms_Loop` as
  `nextClip`, and the animation must settle back to idle with blend weight `0`;
  renamed the selected player asset note to `assets/models/player/SOURCE.md`;
  updated `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` so active docs
  describe locomotion blending and per-frame CPU skinning as live.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs` remains over
  1000 lines, `crates/engine_web/src/tests.rs` is now over 1000 lines, and
  `crates/engine_web/src/model_assets.rs` remains over the 600-line
  split-pressure threshold. Do not add the next model/rendering slice until the
  renderer and model tests are split into focused modules. Multi-primitive
  character assembly, retargeting the separate Quaternius base character, and
  GPU skinning remain follow-up milestones.
- Rejected findings: none.
- Validation rerun: `cargo test -p engine_core`, `cargo test -p engine_web`,
  `npm run check:wasm`, `npm test`, `npm run smoke:browser`, and
  `git -c safe.directory=C:/dev/ofg diff --check` all passed.
- Remaining risk: the visible player path still skins one selected primitive on
  CPU and uses fallback material texture handles. It proves the idle/walk
  locomotion behavior, but it is not a complete authored character pipeline yet.

## Contract and Quality Baseline

This plan preserves `OFG-API-001`, the browser shell to Rust browser game API.
The browser still creates one `RustBrowserGame`, calls `tick(frame)`, sends
commands through `command(command)`, and reads debug state through
`debugSnapshot()`. Model loading must not add per-entity TypeScript calls or raw
wasm export usage.

This plan intentionally extends `OFG-API-002`. The existing asset loader supports
generic texture-array decode requests for Rust-owned terrain textures. This plan
adds generic byte asset requests, probably:

    export type ByteAssetRequest = {
      readonly id: string;
      readonly url: string;
    };

    export type ByteAsset = {
      readonly id: string;
      readonly data: Uint8Array;
    };

    export type BrowserAssetLoader = {
      loadTextureArrays(requests): Promise<readonly RgbaTextureArrayAsset[]>;
      loadBytes(requests): Promise<readonly ByteAsset[]>;
    };

Rust may ask for `/assets/models/player/player.glb` bytes. TypeScript only
fetches bytes and returns them by ID. TypeScript must not parse GLTF JSON,
inspect meshes, assign materials, read animation clips, or mirror model nodes.

This plan preserves `OFG-API-003`, debug and smoke hooks. New debug fields may
report active model ID, clip name, animation state, and skinning runtime, but
the values must come from Rust `debugSnapshot()`.

This plan preserves `OFG-API-004`, the terrain vertex and material layout.
Static and skinned model meshes should get their own renderer vertex layouts
instead of pretending to be terrain. Temporary reuse of fallback texture arrays
is acceptable, but model vertex stride and shader locations must be explicitly
documented and tested.

This plan preserves `OFG-API-009`, forbidden TypeScript ownership. The new
model loader, model resource registry, animation clips, skeletons, skinning,
animation state machine, and render extraction all live in Rust.

Quality constraints:

- Keep `engine_core` browser-free. Scene components, animation clip state,
  animation sampling, and locomotion decisions belong there when they do not
  need WebGPU or browser APIs.
- Keep `engine_web` responsible for browser asset fetch handoff, GLB parsing if
  parsing needs loaded bytes near renderer resources, WebGPU mesh/texture
  upload, shader pipelines, and browser-facing debug snapshots.
- Keep WebGPU handles out of `engine_core` scene resources. Use logical
  model/mesh/material IDs and resolve them inside `engine_web`.
- Do not create scene entities for every terrain chunk. Model nodes and bones
  may become scene entities only for imported model hierarchies or debug/socket
  needs.
- Keep unsupported GLTF features explicit. Reject or ignore unsupported
  primitive modes, morph targets, cameras, lights, extensions, multiple UV sets,
  and unusual material features with tests.
- Use behavior-focused tests near the code. Examples:
  `loads triangle primitives from a glb model`,
  `samples node rotation animation between keyframes`,
  `computes joint matrices from inverse bind poses`,
  `crossfades idle to walk when movement starts`.

## Context and Orientation

Current relevant files:

- `src/engine/browser/textureAssetLoader.ts` is the generic TypeScript browser
  asset helper. It currently decodes Rust-provided texture-array URL lists into
  RGBA bytes. This plan extends it with byte fetch, but not GLTF parsing.
- `src/engine/web/browserGameTypes.ts` defines browser-facing frame input,
  commands, and debug snapshot types.
- `crates/engine_core/src/scene.rs` owns the Rust scene tree. It stores
  `Entity` records with `EntityId` handles, parent/child links, local/world
  transforms, and typed components.
- `crates/engine_core/src/scene_components.rs` currently defines camera,
  player, mesh renderer, and terrain components. This plan will add model and
  animation components here or split them into focused modules if the file grows.
- `crates/engine_core/src/scene_resources.rs` owns logical mesh/material
  resources with labels and typed IDs. This plan will extend resource metadata
  for imported models without storing WebGPU handles in `engine_core`.
- `crates/engine_core/src/engine.rs` owns player/camera behavior and extracts
  visible scene mesh renderer items.
- `crates/engine_core/src/render_packet.rs` defines the camera/light snapshot
  and visible mesh item packets.
- `crates/engine_web/src/game_state.rs` bridges `engine_core` scene render items
  to browser renderer labels and world matrices.
- `crates/engine_web/src/wgpu_renderer.rs` owns WebGPU resources. It currently
  uploads terrain meshes and the debug player marker, resolves only the marker
  scene mesh/material labels, and draws with one terrain-shaped mesh pipeline.
- `src/engine/render/shaders/uber.wgsl` is the shared WGSL shader source.
  Static model and skinned model vertex entry points should either be added here
  with tests or split into clearly generated shader sources.

Definitions for this plan:

- GLTF is the Khronos glTF 2.0 asset format. It describes scenes, nodes, meshes,
  materials, textures, skins, and animations. GLB is the binary single-file
  container form.
- A node is a GLTF transform item. A node may have a mesh, a skin, children, or
  animation channels targeting its translation, rotation, scale, or weights.
- A mesh is a collection of primitives. A primitive is the actual draw unit: one
  topology mode, one vertex/index set, and one material.
- A skin is the GLTF skeleton binding. It lists joint nodes and optional inverse
  bind matrices. In linear blend skinning, each vertex is influenced by weighted
  joint transforms.
- A clip is one GLTF animation converted to an engine-owned set of sampled
  channels.
- Blending means evaluating two or more clips at once and mixing their target
  transforms. For this plan the required blend is only idle-to-walk and
  walk-to-idle.

## Plan of Work

Milestone 1 adds the asset and importer foundation. Download a minimal set of
Khronos glTF Sample Assets into `assets/models/test-fixtures/`, with
`SOURCE.md` documenting source URLs, licenses, and why each fixture exists. The
initial fixture set should include one static triangle/box-style asset for mesh
loading, one node-animation asset for clip sampling, and one simple skin asset
for the later skinning milestone. Extend
`src/engine/browser/textureAssetLoader.ts` into a more general browser asset
loader with `loadBytes`. Update TypeScript tests for successful byte fetch and
error paths. Add the Rust `gltf` crate to the crate that owns parsing. The
initial parser should support GLB files, embedded buffers, triangle primitives,
positions, normals, texcoord 0, vertex color 0, unsigned indices, node
hierarchies, node transforms, and base-color material factors. It should reject
non-triangle primitive modes and malformed required attributes with clear
errors. Add importer unit tests using minimal checked-in fixtures under
`assets/models/test-fixtures/` or deterministic byte fixtures generated inside
tests.

Milestone 2 renders static imported GLB meshes through the Rust scene. Add model
resource metadata so one imported model can register logical mesh/material
resources and create child scene entities under the player or a test scene root.
Add renderer-side GPU resource storage for imported static mesh primitives. Add
a model vertex layout, such as position, normal, uv, and color. Add a WGSL model
vertex entry point or a small model shader pipeline that shares the existing
camera/object/material uniforms. At this point material support can be limited
to base color plus fallback textures. Browser smoke should show the imported
model in debug-fly mode, preferably replacing the yellow marker only after the
static render path is stable.

Milestone 3 implements non-skinned GLTF animation. Add animation clip structs in
Rust for channels targeting node translation, rotation, and scale. Add linear
interpolation for translation/scale and normalized spherical interpolation for
rotation. Apply a clip to imported model node transforms, update scene world
transforms, and render the moving static model. Validate with a simple fixture
whose animated node motion is easy to assert numerically. Browser smoke should
verify the animation clock advances via Rust debug snapshot state or by
capturing two distinct frames.

Milestone 4 implements skinned animation. Import `JOINTS_0`, `WEIGHTS_0`, skin
joint lists, and inverse bind matrices. Start with CPU skinning for one player
model: evaluate the skeleton pose from animation clips, compute joint matrices,
skin positions and normals into a dynamic mesh buffer, and upload/update that
mesh before drawing. Add tests for bind-pose identity, one-joint motion, and
weighted two-joint interpolation. Use a Quaternius or Khronos skinned sample
fixture to prove a real humanoid or sample skin can load. If CPU skinning causes
visible cost, add a later GPU-skinning follow-up with a skinned vertex layout
and joint matrix storage buffer; do not block the first character milestone on
that pipeline unless correctness requires it.

Before or during milestone 5, download the Quaternius Universal Base Characters
pack and import only the needed GLB/glTF humanoid character file plus
`SOURCE.md`. Do not commit the whole source pack if it is large. If the needed
idle/walk clips are separate from the base character, download the Quaternius
Universal Animation Library 2 and import only the required idle and walk GLB
clips, again with source/license documentation.

Milestone 5 adds animation state and blending for player locomotion. Add an
animation controller component or player animation state in Rust. It chooses
`idle` when no horizontal movement input is active and `walk` when movement is
active. It advances clip time in seconds, crossfades between idle and walk over
a short fixed duration, blends local joint/node transforms, and exposes
`activeClip`, `nextClip`, `blendWeight`, and `skinningRuntime` in
`debugSnapshot()`. The final acceptance behavior is that holding movement keys
starts a visible walk animation and releasing movement keys blends back to idle.

## Concrete Steps

Work from `C:\dev\ofg`.

Before editing implementation:

    git -c safe.directory=C:/dev/ofg status --short
    rg -n "loadTextureArrays|BrowserTextureAssetLoader|MeshRendererComponent|SceneResources|scene_mesh_handle|create_main_pipeline|TERRAIN_VERTEX_FLOATS" src crates docs

Milestone 1, asset loader and importer:

    Download selected Khronos glTF Sample Assets into assets/models/test-fixtures/
    Add assets/models/test-fixtures/SOURCE.md with exact source URLs and licenses
    Edit src/engine/browser/textureAssetLoader.ts
    Edit src/engine/browser/textureAssetLoader.test.ts
    Add Rust GLTF importer module(s), likely under crates/engine_web/src/model_assets.rs or crates/engine_core/src/model_assets.rs depending on ownership after the first parser sketch.
    Add gltf dependency to the parsing crate.
    cargo test -p engine_core
    cargo test -p engine_web
    npm test

Milestone 2, static render path:

    Add model mesh/material resource metadata and renderer-side GPU resource maps.
    Add a static model vertex layout and shader entry point/pipeline.
    Instantiate one imported model under the player or debug scene path.
    cargo test -p engine_core
    cargo test -p engine_web
    npm run check:shaders
    npm test
    npm run smoke:browser

Milestone 3, node animation:

    Add animation clip/channel structs and interpolation tests.
    Apply sampled node transforms to imported model scene nodes.
    Expose debug snapshot fields for active animation time/clip.
    cargo test -p engine_core
    cargo test -p engine_web
    npm test
    npm run smoke:browser

Milestone 4, skinned animation:

    Import JOINTS_0, WEIGHTS_0, skins, inverse bind matrices, and skeleton node maps.
    Compute joint matrices and CPU-skinned dynamic mesh output.
    Render a posed skinned character.
    cargo test -p engine_core
    cargo test -p engine_web
    npm run check:wasm
    npm test
    npm run smoke:browser

Milestone 5, locomotion blending:

    Download the Quaternius Universal Base Characters pack or selected GLB character file
    Add assets/models/player/SOURCE.md with exact source URL, author, license, and extraction notes
    If needed, download selected idle/walk clips from Quaternius Universal Animation Library 2
    Add idle/walk animation controller state.
    Blend idle and walk based on Rust movement intent.
    Replace or demote the yellow debug marker once the model is reliable.
    cargo test -p engine_core
    cargo test -p engine_web
    npm run check:wasm
    npm test
    npm run smoke:browser

After every browser smoke run, inspect the newest
`artifacts/browser-smoke/<run-id>/report.json` and screenshots. A black, blank,
solid, or visually frozen frame is a failure for rendering milestones.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective as needed.
2. Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` if the milestone
   changes supported boundaries or runtime ownership.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands after fixes.
6. Record commands, artifacts, and remaining risks in this plan.

## Validation and Acceptance

This plan is complete when all of the following are true:

- The browser asset loader has a generic byte-fetch method used by Rust, and
  TypeScript does not parse GLTF.
- Rust imports at least one checked-in static GLB fixture and validates its
  meshes, materials, node hierarchy, and transforms in tests.
- Rust/wgpu renders an imported static GLB mesh through scene mesh renderer
  extraction and renderer-side resource resolution.
- Rust imports and samples at least one GLTF animation clip targeting node
  transforms.
- Rust imports a skinned GLB character with joints and weights and renders an
  animated pose.
- The player character chooses idle or walk from Rust player movement state.
- Idle-to-walk and walk-to-idle transitions blend rather than snap.
- Browser smoke shows the character in the world and verifies movement input
  causes the active animation state to become walk.
- The yellow debug marker is removed from the normal character path or remains
  only as an explicit fallback/debug command.
- No TypeScript scene graph, GLTF parser, animation runtime, skinning runtime,
  render extractor, or WebGPU ownership is introduced.

Required command results by final acceptance:

    cargo test -p engine_core
    cargo test -p engine_web
    npm run check:shaders
    npm run check:wasm
    npm test
    npm run smoke:browser

All commands must pass, and browser smoke artifacts must be inspected.

## Idempotence and Recovery

Keep the model path additive until each milestone passes. Do not delete the
debug marker until an imported model renders reliably in browser smoke. If an
imported asset fails to load, keep the fallback marker path available and expose
the GLTF load error in `debugSnapshot()` or browser console diagnostics.

Check assets into `assets/models/` only with a small `LICENSE.md` or
`SOURCE.md` next to them documenting the original URL, author, license, and any
conversion command. Do not commit large unused source packs. Prefer one small
test fixture and one player fixture at first. Khronos sample fixtures should
live under `assets/models/test-fixtures/`; Quaternius player assets should live
under `assets/models/player/`.

If the renderer path becomes too large, split `crates/engine_web/src/wgpu_renderer.rs`
before adding more model/skinning code. A separate `model_renderer.rs` or
`model_gpu_resources.rs` is preferable to growing the already-large renderer
file.

If CPU skinning is visibly too slow, keep the CPU path as a correctness fixture
and add a new GPU-skinning milestone instead of trying to optimize prematurely.

Never use `git reset --hard` or destructive checkout commands unless the user
explicitly requests them.

## Artifacts and Notes

Reference sources checked while drafting:

- Khronos glTF page:
  `https://www.khronos.org/gltf/`
- Khronos glTF 2.0 specification:
  `https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html`
- Rust `gltf` crate docs:
  `https://docs.rs/gltf/latest/gltf/`
- Quaternius Universal Base Characters:
  `https://quaternius.com/packs/universalbasecharacters.html`
- Quaternius Universal Animation Library 2:
  `https://quaternius.com/packs/universalanimationlibrary2.html`

Required download sources:

- Khronos glTF Sample Assets for small importer fixtures:
  `https://github.khronos.org/glTF-Sample-Assets/`
- Khronos sample asset repository for direct downloads:
  `https://github.com/KhronosGroup/glTF-Sample-Assets`
- Quaternius Universal Base Characters for the humanoid player model:
  `https://quaternius.com/packs/universalbasecharacters.html`
- Quaternius Universal Animation Library 2 for idle/walk humanoid animation
  clips if the base character pack does not include the needed clips:
  `https://quaternius.com/packs/universalanimationlibrary2.html`

Relevant source facts:

- Khronos describes glTF as a scene format whose top-level elements include
  scenes/nodes, meshes, buffers, materials, textures, skins, and animations.
- The glTF 2.0 specification defines a mesh as a collection of mesh primitives,
  and a mesh primitive as indexed or non-indexed geometry bound to a material.
- The glTF 2.0 specification defines linear blend skinning, skin joints,
  inverse bind matrices, and joint hierarchy rules.
- The Rust `gltf` crate version shown in docs is `1.4.1`; its mesh reader
  exposes positions, normals, indices, texcoords, joints, and weights, and its
  animation reader exposes input/output sampling data.
- Quaternius Universal Base Characters are CC0, rigged humanoids available in
  FBX and glTF. Quaternius Universal Animation Library 2 is CC0 and available
  as GLB, FBX, and Blend.

Candidate asset layout:

    assets/models/player/SOURCE.md
    assets/models/player/player.glb
    assets/models/player/idle.glb
    assets/models/player/walk.glb
    assets/models/test-fixtures/SOURCE.md
    assets/models/test-fixtures/static-triangle.glb
    assets/models/test-fixtures/node-animation.glb
    assets/models/test-fixtures/simple-skin.glb

The actual imported assets should be kept small. If Quaternius source packs are
large, import only the needed GLB files and document the extraction step.

## Interfaces and Dependencies

Expected TypeScript asset-loader shape:

    export type ByteAssetRequest = {
      readonly id: string;
      readonly url: string;
    };

    export type ByteAsset = {
      readonly id: string;
      readonly data: Uint8Array;
    };

    export type BrowserAssetLoader = {
      loadTextureArrays(
        requests: readonly RgbaTextureArrayAssetRequest[]
      ): Promise<readonly RgbaTextureArrayAsset[]>;
      loadBytes(
        requests: readonly ByteAssetRequest[]
      ): Promise<readonly ByteAsset[]>;
    };

Expected Rust model asset types may be split between `engine_core` and
`engine_web`, but the ownership should look like this:

    pub struct ModelAsset {
        pub nodes: Vec<ModelNode>,
        pub primitives: Vec<ModelPrimitive>,
        pub materials: Vec<ModelMaterial>,
        pub animations: Vec<AnimationClip>,
        pub skins: Vec<SkinAsset>,
    }

    pub struct ModelPrimitive {
        pub vertices: Vec<ModelVertex>,
        pub indices: Vec<u32>,
        pub material: ModelMaterialId,
        pub skin: Option<SkinId>,
    }

    pub struct ModelVertex {
        pub position: [f32; 3],
        pub normal: [f32; 3],
        pub uv: [f32; 2],
        pub color: [f32; 4],
        pub joints: [u16; 4],
        pub weights: [f32; 4],
    }

    pub struct AnimationClip {
        pub name: String,
        pub duration_seconds: f32,
        pub channels: Vec<AnimationChannel>,
    }

    pub struct AnimationControllerComponent {
        pub current_clip: AnimationClipId,
        pub next_clip: Option<AnimationClipId>,
        pub current_time_seconds: f32,
        pub next_time_seconds: f32,
        pub blend_weight: f32,
        pub blend_duration_seconds: f32,
    }

The first static renderer milestone can ignore `joints` and `weights`, but the
importer should preserve them once skinning work begins.

Do not add a third-party ECS. Do not add TypeScript GLTF loader packages. Use a
Rust GLTF parser crate and small in-repo runtime types that match OFG's scene
and renderer contracts.

## Revision Note

2026-06-06: Initial ExecPlan created after the completed Rust scene/component
plan. It scopes the new feature as GLB/static mesh loading first, then GLTF
node animation, skinned animation, blending, and finally movement-driven walk
animation for the player character.
