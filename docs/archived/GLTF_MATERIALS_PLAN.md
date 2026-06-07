# GLTF Texture And Material Rendering

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

This plan follows `PLANS.md`.

## Purpose / Big Picture

Make imported glTF characters and test meshes render with their authored
materials instead of flat fallback colors. The first visible outcome is that the
current male/female Quaternius Superhero player bodies show their base-color
textures and render all their character primitives: hair or eyebrows, eyes, and
body. The next outcome is that the renderer shades glTF 2.0 core
metallic-roughness materials with the correct texture channels and factors. The
compatibility outcome is that assets using the archived
`KHR_materials_pbrSpecularGlossiness` extension can be imported, tested, and
rendered through a clearly identified specular-glossiness path.

The user should be able to run the browser app, press `C` into third-person, and
see a textured animated character rather than a single untextured body primitive.
Smoke screenshots should make that difference obvious.

## Progress

- [x] (2026-06-07) Read `PLANS.md` and confirmed this new material/texture
  feature needs an ExecPlan.
- [x] (2026-06-07) Researched the Khronos glTF material documents and sample
  renderer references.
- [x] (2026-06-07) Inspected the checked-in Quaternius GLBs for material,
  texture, image, primitive, and extension usage.
- [x] (2026-06-07) Drafted this active ExecPlan.
- [x] (2026-06-07) Regenerated the Quaternius Superhero male/female GLBs as
  self-contained GLBs with embedded mesh buffers and seven embedded PNG image
  buffer views per character.
- [x] (2026-06-07) Downloaded Khronos Damaged Helmet and
  `Material_SpecularGlossiness_00` reference assets into
  `artifacts/gltf-materials/` for later render validation.
- [x] (2026-06-07) Added Rust importer records for glTF images, textures,
  samplers, metallic-roughness material fields, normal/occlusion/emissive
  texture infos, alpha state, double-sided state, and
  `KHR_materials_pbrSpecularGlossiness`.
- [x] (2026-06-07) Added importer tests covering URI images, data URI images,
  GLB bufferView image bytes, sampler settings, core metallic-roughness texture
  infos, and specular-glossiness extension values.
- [x] (2026-06-07) Split the new material/image importer code and tests into
  `crates/engine_web/src/model_materials.rs` and
  `crates/engine_web/src/model_materials_tests.rs` after milestone review
  flagged file-size pressure.
- [x] (2026-06-07) Validated the importer/asset slice with `cargo test -p
  engine_web`, `npm test`, `npm run check:wasm`, and targeted `git diff
  --check`.
- [x] (2026-06-07) Chose Rust-side PNG/JPEG decode for embedded GLB model
  images, using one-layer WebGPU texture arrays so the existing object bind
  group shape can be reused.
- [x] (2026-06-07) Rendered all skinned Quaternius Superhero character
  primitives as separate Rust scene mesh items sharing one player-following root.
- [x] (2026-06-07) Added workflow-aware material packets and WGSL branches for
  terrain, core glTF metallic-roughness, and
  `KHR_materials_pbrSpecularGlossiness`.
- [x] (2026-06-07) Added a checked-in Khronos Asset Generator spec/gloss GLB
  fixture and render it as a small static scene item through the same Rust
  model material resource path.
- [x] (2026-06-07) Added browser-smoke assertions for active character
  primitive/material/texture counts and non-fallback albedo part count.
- [x] (2026-06-07) Validated the material/rendering slice with `cargo fmt`,
  `cargo test -p engine_web`, `npm test`, `npm run check:wasm`,
  `npm run check:shaders`, and `npm run smoke:browser`.
- [x] Confirm the preferred texture decode path with a small prototype:
  browser-native generic image decode versus Rust-side PNG/JPEG decode.
- [x] Implement texture/material import records and tests.
- [x] Render all character primitives with texture handles and material packets.
- [x] Implement core glTF metallic-roughness shading.
- [x] Add specular-glossiness extension import and rendering compatibility.
- [x] Run milestone reviews, validation, and archive the plan when complete.

## Surprises & Discoveries

- Observation: In glTF 2.0, metallic-roughness is the core PBR material model,
  not an extension. The older specular-glossiness workflow is the
  `KHR_materials_pbrSpecularGlossiness` extension, now archived and superseded
  by newer specular extensions.
  Evidence: Khronos glTF 2.0 specification section 5.22 defines
  `material.pbrMetallicRoughness`; the Khronos extension README is under
  `extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness`.

- Observation: The local Quaternius Superhero male/female GLBs use core
  `pbrMetallicRoughness` only. They have no `extensionsUsed` or
  `extensionsRequired`, and do not use `KHR_materials_pbrSpecularGlossiness`.
  Evidence: local GLB JSON inspection on 2026-06-07 found empty extension lists
  for `assets/models/player/quaternius-superhero-male.glb` and
  `assets/models/player/quaternius-superhero-female.glb`.

- Observation: The checked-in Superhero bodies reference external PNG image
  files, but those PNGs are not checked into `assets/models/player/`.
  Evidence: `rg --files assets/models/player` lists only `SOURCE.md` and four
  `.glb` files. The male GLB references `T_Hair_1_BaseColor.png`,
  `T_Eye_Brown.png`, `T_Superhero_Male_Dark.png`,
  `T_Superhero_Male_Roughness.png`, and normal maps. The female GLB references
  matching hair, eye, body, roughness, and normal PNGs.

- Observation: The visible character is currently incomplete even before
  texture support because the runtime selects one skinned primitive. The
  Superhero bodies are split into three primitives: hair/eyebrows, eyes, and
  body.
  Evidence: local GLB inspection found three mesh primitives for each
  Superhero body. Current `PlayerCharacterModel` stores one `ModelPrimitive` and
  `largest_skinned_primitive` chooses the body, leaving eyes and hair/eyebrows
  out of the render path.

- Observation: The UAL animation/mannequin GLBs are not good material test
  assets. They have factor-only materials and no images.
  Evidence: local GLB inspection found zero textures and zero images in
  `quaternius-ual1-standard.glb` and `quaternius-ual2-standard.glb`.

- Observation: The Quaternius source GLTFs reference `T_Hair_1_Normal_png.png`
  and `T_Eye_Normal_png.png`, but the extracted source folder contains those
  files as `T_Hair_1_Normal.png` and `T_Eye_Normal.png`.
  Evidence: the GLB embedding step failed on the exact source URI first, then
  succeeded after resolving the `_png.png` suffix to the matching extracted
  `.png` filename.

- Observation: Enabling typed specular-glossiness import only required enabling
  the `gltf` crate's `KHR_materials_pbrSpecularGlossiness` feature, not writing
  ad hoc JSON parsing for this extension.
  Evidence: `cargo test -p engine_web` passes with
  `ModelMaterialWorkflow::SpecularGlossiness` populated through
  `material.pbr_specular_glossiness()`.

## Decision Log

- Decision: Implement core glTF 2.0 metallic-roughness first, then add the
  archived specular-glossiness extension as a compatibility path.
  Rationale: The current player character assets use core metallic-roughness.
  Supporting their textures first gives immediate visible value and avoids
  making an extension the mainline material path.
  Date/Author: 2026-06-07 / Codex.

- Decision: Keep the runtime asset format as checked-in GLB, but make the asset
  milestone fix the current missing texture files by either embedding the
  Quaternius images into self-contained GLBs or checking in a same-directory
  model texture package.
  Rationale: `docs/API_CONTRACTS.md` says checked-in GLB is the intended runtime
  format. Self-contained GLBs are easiest to move around and match Khronos
  guidance for bundled binary glTF, but external URI images are valid glTF and
  worth supporting behind the same generic asset-loader boundary.
  Date/Author: 2026-06-07 / Codex.

- Decision: TypeScript may decode or fetch opaque image data only when Rust
  provides generic requests; TypeScript must not parse GLTF or decide what any
  texture means.
  Rationale: `OFG-API-010` keeps GLTF parsing, material interpretation, renderer
  resource resolution, scene ownership, animation, and skinning in Rust.
  Date/Author: 2026-06-07 / Codex.

- Decision: Use Khronos reference material equations and sample renderer code as
  implementation references, not as a blind copy-paste source.
  Rationale: The glTF spec gives the metallic-roughness BRDF inputs and
  equations. The Khronos glTF Sample Renderer includes GLSL material paths under
  Apache-2.0 and is useful for comparison, but any copied code would need
  explicit license handling and WGSL adaptation.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

The first implementation slice is complete. The Quaternius Superhero male/female
assets are now self-contained GLBs with embedded PNG images, and the Rust glTF
importer now preserves renderer-neutral material, image, texture, and sampler
records. The new material import code lives in a focused module rather than
making `model_assets.rs` larger.

The material/rendering implementation slice is complete. Runtime model images
embedded in GLBs are decoded in Rust, uploaded as one-layer WebGPU texture
arrays, and bound through Rust-owned scene material resources. The active
Quaternius male/female characters render all three skinned primitives with
non-fallback albedo textures. The shader now keeps terrain material semantics
separate from glTF material semantics, uses core metallic-roughness roughness
from green and metallic from blue, and has a specular-glossiness compatibility
branch. A Khronos Asset Generator spec/gloss fixture is checked in as an
embedded GLB and rendered as a small static scene item for smoke coverage.
Tangent-space normal-map application remains deferred until model tangents are
imported or generated.

## Contract and Quality Baseline

This plan preserves `OFG-API-010`: Rust owns GLTF parsing, model resource
registration, scene node/entity creation, animation clips, skeletons, skinning,
animation blending, and renderer resource resolution. TypeScript must not parse
GLTF JSON or GLB chunks; must not inspect meshes, nodes, skins, animation
channels, clips, materials, textures, images, or skeletons; and must not create
per-model or per-entity render calls.

This plan extends the browser substrate allowed by `OFG-API-010` only in generic
terms. The existing TypeScript asset loader in
`src/engine/browser/textureAssetLoader.ts` may gain opaque image decode/fetch
methods such as `loadImages(requests)` or a more general byte/image lane, but
Rust must define the requests and remain the only owner of GLTF material meaning.

This plan preserves the Rust-owned renderer boundary in `crates/engine_web`.
The WebGPU renderer may gain model texture handles, material workflow flags,
and shader bindings, but TypeScript should remain a startup/input/HUD/debug
shell.

Quality constraints:

    cargo fmt
    cargo test -p engine_web
    npm run check:wasm
    npm run check:shaders
    npm test
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg diff --check

Because this changes rendering, shader behavior, browser asset loading, and
visual output, `npm run smoke:browser` is required before completion.

After every milestone, run the repo-local `milestone-review` skill before
marking that milestone complete. Apply required findings or record a rejected
finding with rationale in this plan's Decision Log.

## Context and Orientation

Current GLTF import lives in `crates/engine_web/src/model_assets.rs`.
`ModelMaterial` stores only `name`, `base_color_factor`, `metallic_factor`, and
`roughness_factor`. `import_materials` calls
`material.pbr_metallic_roughness()` and ignores texture references, texture
coordinates, samplers, images, normal maps, occlusion maps, emissive maps, alpha
mode, double-sided state, and material extensions.

Current character animation lives in
`crates/engine_web/src/model_locomotion.rs`. `PlayerCharacterModel` stores one
selected `ModelPrimitive`, one material packet, one mesh node index, and one
skin. It CPU-skins that primitive each frame and uploads a same-size vertex
buffer. This is why the current Superhero character draws the body but not
eyes, hair, or eyebrows.

Current model rendering is wired through `crates/engine_web/src/wgpu_renderer.rs`.
`render_frame` binds real texture arrays for terrain items, but binds
`fallback_albedo`, `fallback_normal`, and `fallback_material` for all scene/model
items. `create_fallback_textures` creates one-layer white, normal, and material
textures.

Current shader code is in `src/engine/render/shaders/uber.wgsl`, then generated
into TypeScript shader artifacts by `tools/build-shaders.mjs`. The shader can
sample `albedoTexture`, `normalTexture`, and `materialTexture` as
`texture_2d_array<f32>`, but model items currently receive fallback textures.
For non-terrain model items, `sampleRoughness` reads the material texture red
channel, which is not glTF metallic-roughness convention. glTF core stores
roughness in the green channel and metallic in the blue channel of the
metallic-roughness texture.

Current generic browser asset loading lives in
`src/engine/browser/textureAssetLoader.ts`. It exposes
`loadTextureArrays(requests)` for Rust-owned terrain texture array requests and
`loadBytes(requests)` for Rust-owned model byte requests. It already decodes
browser image URLs into RGBA arrays for terrain without interpreting terrain
manifest semantics.

The current checked-in player assets are recorded in
`assets/models/player/SOURCE.md` and were sourced from Quaternius CC0 packs. The
Superhero male/female bodies are temporary same-rig placeholders until better
Regular male/female GLBs are available.

Definitions:

- glTF: Khronos runtime 3D asset format. A `.gltf` file is JSON plus external or
  embedded resources. A `.glb` file is the binary container form that can bundle
  JSON, buffers, and images.
- Material: the glTF object describing how a primitive should shade.
- Texture: a glTF reference from a material to an image plus a sampler.
- Image: PNG/JPEG/WebP/etc. pixel source referenced by a texture.
- Sampler: texture filtering and wrap mode metadata.
- Metallic-roughness: the core glTF 2.0 PBR workflow under
  `material.pbrMetallicRoughness`.
- Specular-glossiness: the older `KHR_materials_pbrSpecularGlossiness`
  extension workflow. It is archived and superseded, but still encountered in
  assets and useful for compatibility tests.

## Plan of Work

Milestone 1: asset packaging and research fixtures.

Recover or redownload the Quaternius texture PNGs referenced by the current
Superhero GLBs. Prefer producing self-contained checked-in GLBs with embedded
images so the player assets match the runtime GLB contract. If that conversion
is lossy or awkward, check in a same-directory package of `.glb` plus referenced
PNG images and make the importer resolve relative image URIs through Rust-owned
requests. Update `assets/models/player/SOURCE.md` with exact texture source and
conversion notes.

Download small Khronos test assets into `artifacts/` first, then check in only
minimal fixtures that are license-appropriate and needed by tests. Use at least
one core metallic-roughness model such as Damaged Helmet or a generated material
fixture, and at least one `KHR_materials_pbrSpecularGlossiness` fixture from the
Khronos glTF Asset Generator positive material tests. Record licenses in a
source note if any fixture becomes checked-in.

Milestone 2: model texture and material import records.

Extend `crates/engine_web/src/model_assets.rs` with explicit records for glTF
images, textures, samplers, texture info, normal texture info, occlusion texture
info, alpha mode, double-sided state, and material workflow. Keep these as
renderer-neutral imported data:

    ModelImage
    ModelImageSource
    ModelTexture
    ModelSampler
    ModelTextureInfo
    ModelNormalTextureInfo
    ModelOcclusionTextureInfo
    ModelMaterialWorkflow

`ModelMaterialWorkflow` should have at least:

    MetallicRoughness {
        base_color_factor,
        base_color_texture,
        metallic_factor,
        roughness_factor,
        metallic_roughness_texture
    }

    SpecularGlossiness {
        diffuse_factor,
        diffuse_texture,
        specular_factor,
        glossiness_factor,
        specular_glossiness_texture
    }

Import core `pbrMetallicRoughness` through the `gltf` crate APIs. Import
`KHR_materials_pbrSpecularGlossiness` from the raw material extension JSON if
the `gltf` crate does not expose a typed helper. Preserve extension-required
failure behavior: if a material requires a spec/gloss extension before the
renderer supports it, fail with a precise error instead of silently rendering an
incorrect fallback.

Add focused tests in `crates/engine_web/src/tests.rs` or a new model asset test
module. Tests should cover base-color texture index, metallic-roughness texture
index, roughness/metallic channel convention, normal texture scale, sampler wrap
and filter values, external URI image records, bufferView image records, data
URI image records, and spec/gloss extension values.

Milestone 3: generic image decode/upload path.

Prototype and then choose one decode path:

- Browser-native decode path: Rust creates opaque `RgbaImageAssetRequest`
  records. TypeScript fetches or decodes image URLs/bytes into RGBA without
  knowing their material role. This reuses browser image support and keeps image
  decoder code out of Wasm.
- Rust-side decode path: Rust decodes PNG/JPEG bytes with a small decoder crate
  and uploads RGBA directly. This supports embedded GLB images without a Rust to
  JavaScript round trip, but increases Wasm dependency size.

Whichever path wins, expose one renderer-facing `ModelTextureSet` or equivalent
that maps imported glTF texture indices to WebGPU texture handles. Use one-layer
texture arrays initially so existing `texture_2d_array` bindings can be reused.
Create fallback one-layer textures for missing maps: white base color, flat
normal, and material values matching glTF defaults. Ensure external URI images
cannot escape the asset root through `..` paths or absolute filesystem paths.

Milestone 4: multi-primitive character rendering.

Refactor `PlayerCharacterModel` so it keeps all renderable skinned primitives
needed by the chosen character body instead of only the largest primitive. All
parts should share the same sampled animation pose and skinning matrices where
they use the same skin. Each part should retain its primitive material index and
use the matching material texture handles.

Update renderer resource registration so a character slot owns one mesh handle
per primitive and one material/texture binding per primitive. Update the
Rust-owned scene item path so a player character can emit multiple mesh items
that follow the same player transform and first-person visibility rules. Do not
move scene graph or per-entity render-call ownership to TypeScript.

Acceptance for this milestone is that the Superhero male and female character
show body plus eyes plus hair/eyebrows in third-person mode, even if the shader
is still a simple albedo-lit path.

Milestone 5: core metallic-roughness material shading.

Update material packets and `src/engine/render/shaders/uber.wgsl` so model
materials follow glTF metallic-roughness semantics:

- Base color = vertex color times `baseColorFactor` times base-color texture.
- Base-color texture RGB is authored in sRGB; alpha is linear coverage.
- Metallic = `metallicFactor` times metallic-roughness texture blue channel.
- Roughness = `roughnessFactor` times metallic-roughness texture green channel.
- Missing base-color texture samples as `[1, 1, 1, 1]`.
- Missing metallic-roughness texture samples as green and blue equal to `1`.
- Terrain material sampling must keep its existing terrain channel convention
  or move behind an explicit terrain material workflow flag.

Replace the current Blinn-Phong-ish model lighting with a small direct-light PBR
path based on the glTF 2.0 metallic-roughness equations: dielectric `f0 = 0.04`,
`c_diff = baseColor * (1 - metallic)`, `f0 = mix(0.04, baseColor, metallic)`,
perceptual roughness squared for alpha roughness, Lambertian diffuse, Schlick
Fresnel, and GGX-style specular. Keep this renderer lightweight; image-based
lighting and full environment maps are future work.

Defer normal-map application until tangents exist. The importer should record
normal textures now, but the shader should keep using geometric/skinned normals
unless a later milestone imports or generates tangents. Record that limitation
in docs and debug output so it does not look like a bug.

Milestone 6: specular-glossiness compatibility.

Add renderer support for `KHR_materials_pbrSpecularGlossiness` after the core
metallic-roughness path is passing. Follow the extension's BRDF inputs:

    c_diff = diffuse.rgb * (1 - max(specular.r, specular.g, specular.b))
    F0 = specular
    alpha = (1 - glossiness) ^ 2

Import diffuse texture and specular-glossiness texture. The diffuse texture is
sRGB. The specular-glossiness texture stores specular color in RGB and
glossiness in alpha. If the renderer supports the extension and a material
contains both workflows, render the extension workflow as the extension's best
practice says. If the extension is required and unsupported, fail to load with a
clear error.

Use Khronos Asset Generator spec/gloss fixtures for importer and browser smoke
coverage. The goal is compatibility and correctness for known test assets, not
making spec/gloss the preferred content path for OFG.

Milestone 7: documentation, debug state, and validation.

Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` to describe the
Rust-owned model texture/material path, the generic image loader lane, supported
glTF material workflows, and known limitations such as no tangent-space normal
maps yet if tangents are deferred.

Expose compact Rust debug snapshot fields for smoke tests, such as active model
primitive count, model material count, model texture count, material workflow
names, and whether the player character has non-fallback albedo textures. Keep
debug fields factual; do not expose raw glTF internals to TypeScript for
decision-making.

Run the validation commands, inspect browser smoke screenshots, record evidence
in this plan, run `milestone-review`, fix required findings, and archive this
plan under `docs/archived/` when complete.

## Concrete Steps

Run from `C:\dev\ofg`.

Initial inspection commands:

    git -c safe.directory=C:/dev/ofg status --short
    rg --files assets/models/player
    rg "struct ModelMaterial|import_materials|fallback_albedo|sampleRoughness" crates/engine_web/src src/engine/render/shaders -n

Expected starting point:

    assets/models/player/SOURCE.md
    assets/models/player/quaternius-superhero-male.glb
    assets/models/player/quaternius-superhero-female.glb
    assets/models/player/quaternius-ual1-standard.glb
    assets/models/player/quaternius-ual2-standard.glb

After each milestone:

    cargo fmt
    cargo test -p engine_web
    npm run check:wasm
    npm test
    git -c safe.directory=C:/dev/ofg diff --check

After shader or generated shader changes:

    npm run build:shaders
    npm run check:shaders
    npm test

After browser asset loading, rendering, character scene, or shader changes:

    npm run smoke:browser

Before final completion:

    npm run build
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg status --short

Inspect browser smoke screenshots under `artifacts/browser-smoke/`. The
third-person character captures should clearly show texture detail on the body
and separate eye/hair/eyebrow primitives.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective.
2. Update any changed API contracts or active architecture docs.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands.
6. Record the review summary, commands, artifacts, and remaining risks in this
   plan.

Review result for the asset-packaging/importer milestone on 2026-06-07:

- Scope: regenerated Quaternius player GLBs with embedded PNG images, Khronos
  material fixture downloads under `artifacts/gltf-materials/`, Rust glTF image,
  texture, sampler, metallic-roughness, and specular-glossiness import records,
  focused importer tests, generated engine_web Wasm artifacts, and source notes.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent reviewers were not spawned because the user
  did not explicitly ask for delegated milestone review.
- Required findings fixed: code-quality review found that adding the material
  importer directly to `model_assets.rs` pushed an already split-pressure file
  further in the wrong direction. The fix was to extract
  `model_materials.rs` and `model_materials_tests.rs`, leaving
  `model_assets.rs` at 895 lines while keeping material records renderer-neutral.
- Follow-ups recorded: `crates/engine_web/src/tests.rs` remains over 1000 lines
  from existing coverage at 1299 lines. Future feature tests should continue
  moving into focused test modules instead of growing that central file.
- Rejected findings: no contract or correctness findings were rejected.
- Validation rerun: `cargo fmt`, `cargo test -p engine_web`,
  `npm test`, `npm run check:wasm`, and targeted
  `git -c safe.directory=C:/dev/ofg diff --check` all passed after the split.
- Remaining risk: the importer preserves encoded image bytes and material
  workflow records, but the renderer still binds fallback model textures. The
  next milestone must choose and implement the browser-vs-Rust image decode
  path, then install real model texture handles.

Review result for the material rendering completion milestone on 2026-06-07:

- Scope: Rust-side model image decode, model texture upload as one-layer texture
  arrays, scene material resources with real albedo/normal/material handles,
  multi-primitive player character rendering, core metallic-roughness WGSL,
  specular-glossiness WGSL, a checked-in Khronos spec/gloss GLB fixture,
  browser debug material counts, and docs/contracts updates.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were performed locally. Sub-agent reviewers were available through tooling,
  but that tool is restricted to explicit user requests for delegated or
  parallel agent work, so no sub-agents were spawned.
- Required findings fixed: no contract or correctness must-fix findings were
  found. The implementation keeps GLTF parsing/material interpretation in Rust,
  and TypeScript remains limited to startup, generic bytes/image substrate,
  input, HUD, and debug pass-through.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs` remains
  oversized and now owns model fixture registration plus model material GPU
  resource resolution. The next renderer/model feature should extract focused
  model resource registration helpers instead of continuing to grow that file.
- Rejected findings: no required findings were rejected.
- Validation rerun: `cargo fmt`, `cargo test -p engine_web`,
  `npm test`, `npm run check:wasm`, `npm run check:shaders`, and
  `npm run smoke:browser` all passed. The final browser smoke artifact is
  `artifacts/browser-smoke/2026-06-07T08-30-39-820Z`, including
  `third-person.png` and `third-person-female.png`.
- Remaining risk: imported glTF normal textures are recorded/uploaded when
  present but not applied in lighting until tangents are imported or generated.
  The spec/gloss fixture is intentionally visible as a small red/black test quad
  near spawn so browser smoke renders the compatibility path.

## Validation and Acceptance

The feature is accepted when all of the following are true:

- `cargo test -p engine_web` includes importer coverage for glTF images,
  textures, samplers, core metallic-roughness fields, and
  `KHR_materials_pbrSpecularGlossiness` fields.
- Shader contract tests distinguish terrain material channel semantics from
  glTF metallic-roughness channel semantics.
- Browser smoke passes and saves screenshots showing a third-person textured
  male character and textured female character.
- The player character renders all body primitives needed by the checked-in
  Superhero assets: body, eyes, and hair/eyebrows.
- The renderer uses real model texture handles for textured model scene items
  and fallback handles only for missing maps.
- The core metallic-roughness shader uses base-color texture, base-color factor,
  metallic factor, roughness factor, roughness from green, and metallic from
  blue.
- A Khronos spec/gloss fixture either renders through the spec/gloss path or,
  for an intermediate milestone, fails with a precise "required extension
  unsupported" error. Final completion requires rendering the fixture.
- `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and
  `assets/models/player/SOURCE.md` describe the new material/texture behavior
  and asset source state.
- `npm run build`, `npm test`, `npm run check:wasm`,
  `npm run check:shaders`, and `npm run smoke:browser` pass.

## Idempotence and Recovery

Keep downloaded source zips and exploratory Khronos assets under `artifacts/`
until a fixture or texture is intentionally checked in. `artifacts/` output
should not be committed.

If self-contained GLB conversion fails, leave the existing checked-in GLBs in
place and add a new source note describing the failed conversion. Do not delete
the current GLBs until replacement GLBs are generated, imported, and rendered.

If the generic image decode path is wrong, revert only the newly added loader
method and renderer texture-resolution calls. The existing `loadBytes` and
terrain `loadTextureArrays` lanes should continue to work independently.

If shader PBR changes break terrain visual behavior, separate terrain and model
material workflow flags before continuing. Terrain material arrays should not be
silently reinterpreted as glTF metallic-roughness textures.

The unrelated dirty file `docs/TOUCH_CONTROLS_PLAN.md` existed before this
plan. Do not stage, revert, or modify it as part of this work unless the user
explicitly asks.

## Artifacts and Notes

Research links used for this plan:

- Khronos glTF 2.0 specification:
  `https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html`
- Khronos archived specular-glossiness extension:
  `https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness/README.md`
- Khronos glTF Sample Renderer:
  `https://github.com/KhronosGroup/glTF-Sample-Renderer`
- Khronos glTF Sample Renderer license:
  `https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Renderer/main/LICENSE.md`
- Khronos glTF Sample Assets:
  `https://github.khronos.org/glTF-Assets/`
- Khronos glTF Asset Generator specular-glossiness tests:
  `https://github.khronos.org/glTF-Asset-Generator/Output/Positive/Material_SpecularGlossiness/`
- Quaternius source pages recorded in `assets/models/player/SOURCE.md`.

Local GLB inspection summary from 2026-06-07:

    quaternius-superhero-male.glb:
      extensionsUsed: []
      materials: MI_Hair_1, MI_Eyes, MI_Superhero_Male
      workflow: core pbrMetallicRoughness
      textures/images: 7 external PNG URI images
      primitives: Face, Face.001, Sphere.005_Retopology.004

    quaternius-superhero-female.glb:
      extensionsUsed: []
      materials: MI_Hair_2, MI_Eyes, MI_Superhero_Female
      workflow: core pbrMetallicRoughness
      textures/images: 7 external PNG URI images
      primitives: Eyebrows, Eyes, Superhero_Female

    quaternius-ual1-standard.glb:
      extensionsUsed: []
      textures/images: 0
      materials: factor-only M_Main and M_Joints

    quaternius-ual2-standard.glb:
      extensionsUsed: []
      textures/images: 0
      materials: factor-only M_Main and M_Joints

Implementation evidence from 2026-06-07:

    cargo test -p engine_web
      result: 47 passed

    npm test
      result: 59 passing

    npm run check:wasm
      result: passed after `npm test` regenerated the stale engine_web Wasm
      artifacts

    git -c safe.directory=C:/dev/ofg diff --check -- crates/engine_web/Cargo.toml crates/engine_web/src/lib.rs crates/engine_web/src/model_assets.rs crates/engine_web/src/tests.rs assets/models/player/SOURCE.md docs/GLTF_MATERIALS_PLAN.md
      result: passed

    artifacts/gltf-materials/DamagedHelmet.glb
      source: Khronos glTF Sample Models, Damaged Helmet binary glTF

    artifacts/gltf-materials/Material_SpecularGlossiness_00.gltf
    artifacts/gltf-materials/Material_SpecularGlossiness_00.bin
    artifacts/gltf-materials/Textures/BaseColor_X.png
      source: Khronos glTF Asset Generator positive specular-glossiness case 00

Relevant Khronos material facts:

- `material.pbrMetallicRoughness` is part of glTF 2.0 core.
- `baseColorTexture` RGB is sRGB and alpha is linear coverage.
- `metallicRoughnessTexture` samples roughness from G and metalness from B.
- `KHR_materials_pbrSpecularGlossiness` is archived, ratified, and superseded by
  `KHR_materials_specular`.
- Specular-glossiness uses diffuse RGB, specular RGB, and glossiness. Glossiness
  is related to roughness by `glossiness = 1 - roughness`, and the BRDF alpha
  input is `(1 - glossiness) ^ 2`.

## Interfaces and Dependencies

Likely Rust data shapes:

    pub struct ModelImage {
        pub name: Option<String>,
        pub mime_type: Option<String>,
        pub source: ModelImageSource,
    }

    pub enum ModelImageSource {
        Uri(String),
        DataUri(Vec<u8>),
        BufferView { buffer_view_index: usize },
    }

    pub struct ModelSampler {
        pub mag_filter: Option<ModelTextureFilter>,
        pub min_filter: Option<ModelTextureFilter>,
        pub wrap_s: ModelTextureWrap,
        pub wrap_t: ModelTextureWrap,
    }

    pub struct ModelTexture {
        pub name: Option<String>,
        pub source: usize,
        pub sampler: Option<usize>,
    }

    pub struct ModelTextureInfo {
        pub texture: usize,
        pub texcoord: u32,
    }

    pub enum ModelMaterialWorkflow {
        MetallicRoughness { ... },
        SpecularGlossiness { ... },
    }

    pub struct ModelMaterial {
        pub name: Option<String>,
        pub workflow: ModelMaterialWorkflow,
        pub normal_texture: Option<ModelNormalTextureInfo>,
        pub occlusion_texture: Option<ModelOcclusionTextureInfo>,
        pub emissive_texture: Option<ModelTextureInfo>,
        pub emissive_factor: [f32; 3],
        pub alpha_mode: ModelAlphaMode,
        pub alpha_cutoff: f32,
        pub double_sided: bool,
    }

Likely TypeScript generic loader extension, if browser decode wins:

    export type RgbaImageAssetRequest = {
      readonly id: string;
      readonly url?: string;
      readonly data?: Uint8Array;
      readonly mimeType?: string;
    };

    export type RgbaImageAsset = {
      readonly id: string;
      readonly width: number;
      readonly height: number;
      readonly data: Uint8Array;
    };

    export type BrowserAssetLoader = {
      loadTextureArrays(...): Promise<readonly RgbaTextureArrayAsset[]>;
      loadBytes(...): Promise<readonly ByteAsset[]>;
      loadImages(...): Promise<readonly RgbaImageAsset[]>;
    };

Likely renderer-facing material resource:

    pub struct ModelGpuMaterial {
        pub workflow: ModelMaterialWorkflowKind,
        pub packet: [f32; MATERIAL_PACKET_FLOATS],
        pub albedo_texture: ResourceHandle,
        pub normal_texture: ResourceHandle,
        pub material_texture: ResourceHandle,
    }

Any final API should be narrower than these sketches if implementation proves a
smaller interface is enough.
