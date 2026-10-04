# Khronos PBR reference renderer: source guide and OFG integration findings

Research completed on 2026-10-03 before implementation authorization. This document preserves that source inventory; the companion [PBR integration ExecPlan](../plans/pbr-rendering.md) now records implementation and validation. The delivered [PBR laboratory](../pbr.md) is the current runtime contract; historical gap descriptions below refer to the inspected baseline.

Priority update, 2026-10-03: the integration plan now starts with a reviewable grid of material spheres and early debug fly-camera controls. Core PBR, direct lighting, IBL and HDR form the first acceptance checkpoint. Transmission, volume, dispersion and scattering remain in this source inventory for future use, but are deferred and do not block that foundation. Imported reference models supplement the procedural grid.

## Recommendation

A close Slang port of Khronos's shading equations is a sound starting point. Its metallic/roughness model, extension composition, punctual lighting and image-based lighting already form the material system we want. Preserve those equations and their recognizable boundaries; implement resource ownership, bindings, shader selection and render passes in OFG's existing C++/Slang RHI architecture.

Use an **uber-shader source family with compiled material variants**. Khronos itself selects GLSL programs from material, geometry, lighting and pass defines. A single binary declaring every extension texture is unsuitable for OFG's required WebGPU baseline. Variants remove unused resources, but cannot make an arbitrarily texture-heavy material fit a device limit. Such combinations need an explicit capability error or a separately verified asset preparation path.

The shader port is only part of the pipeline. OFG first needs sampled texture resources, normals/tangents and UV contracts, material render state, camera/light data, HDR targets, environment prefiltering, color management, transparent draw ordering and transmission background passes. Volume scattering adds another pass family. These are concrete additions to the current forward renderer; a deferred renderer, generic render graph, bindless framework or material-node system is unnecessary.

## Reproducible checkout and evidence

| Input | Inspected revision / location |
| --- | --- |
| Khronos Sample Renderer | `cc27919cacbb235d2f58a0c0203387efce9375f8`, commit dated 2026-09-22, downloaded 2026-10-03. |
| Local source | `C:/dev/ofg/artifacts/reference/glTF-Sample-Renderer`; ignored by OFG Git through `/artifacts/`. This is a research checkout, not a new runtime dependency or submodule. |
| OFG | `90cde318319d342aed7d347f4b183efc9b8e88ad`; initial working tree clean. |
| Slang RHI | Existing pin `16324a68af477baaede620e713644f5e9613b1a2`; unchanged. |
| glTF specification status | `8e691206fa2e981fc3b8c8bc57e671595576c971`; selected documents saved in `artifacts/reference/gltf-spec-notes`. |
| Sample Assets inventory | `edc7c9e67c639d230715049ee31f9a96a6babbbe`; inspected the Models directory through GitHub's API. Models were not downloaded or rendered. |

Commands actually run from the OFG root:

```powershell
git clone --depth 1 https://github.com/KhronosGroup/glTF-Sample-Renderer.git artifacts/reference/glTF-Sample-Renderer
git -C artifacts/reference/glTF-Sample-Renderer rev-parse HEAD
git -C artifacts/reference/glTF-Sample-Renderer log -1 --format='%cI %s'
```

To reproduce after upstream moves, clone into a new directory, fetch the recorded commit and check it out detached. Do not reset an existing checkout containing someone's edits. The permanent links below remain useful if ignored artifacts are removed. The renderer's `.gitmodules` is empty at this revision. npm dependencies were not installed, and upstream builds/tests were not executed. Findings about execution are deductions from source, not measured visual parity or performance.

The repository carries [Apache-2.0 licensing][license], a [license banner][banner] and [third-party attribution][thirdparty]. Future adapted files must retain applicable notices and identify OFG's modifications and original revision. Audit copied LUTs, filtering sources, MikkTSpace and sample assets individually; the renderer's license is not a blanket license for external environments or models. In particular, `ibl_filtering.frag` attributes its Hammersley sampling code to Holger Dammertz under CC BY 3.0; preserve that source-specific attribution when adapting the filtering code.

## Source navigation

Paths in this section are relative to the downloaded repository. Links target the exact reviewed revision. Parenthesized line numbers locate starting points; named functions are the more useful landmarks when comparing a later revision. The upstream directory is lowercase `source/gltf`; preserve that case in GitHub links even on Windows.

| Source | Relevant landmarks and responsibility | Intended use in OFG |
| --- | --- | --- |
| [`source/Renderer/shaders/pbr.frag`][pbr] | `main`: base color/alpha, unlit path, material extension assembly, IBL, punctual loop, scattering addition, emission, alpha mask and debug channels. | Main reference for composition and order of operations. |
| [`material_info.glsl`][material-info] | `MaterialInfo`, `getNormalInfo` (125), `getBaseColor` (211), and `get*Info` helpers (224 onwards). | Material evaluation, defaults, texture channels and tangent basis. |
| [`brdf.glsl`][brdf] | `F_Schlick` (11), `V_GGX` (74), `D_GGX` (93), `BRDF_lambertian` (152), `BRDF_specularGGX` (159), anisotropy (171), Charlie/sheen (101–149, 208). | Closest mathematical port; separate from texture/host glue. |
| [`ibl.glsl`][ibl] | `getIBLGGXFresnel` (26), `getIBLRadianceGGX` (45), `getTransmissionSample` (59), `getIBLVolumeRefraction` (70), anisotropy (119), Charlie (142). | Split-sum environment integration, multi-scattering compensation and screen-space refraction. |
| [`punctual.glsl`][punctual] | `Light`; range/spot attenuation (36/48), `getLighIntensity` (64, upstream spelling), transmission/clearcoat/sheen, volume attenuation (117), transmission ray (133), Burley scattering (151 onwards). | Analytic lights and absorption; split scattering into its own OFG module when introduced. |
| [`iridescence.glsl`][iridescence] | `evalSensitivity` (28), `evalIridescence` (42), Fresnel/IOR conversion helpers. | Thin-film spectral approximation. Keep its units and numerical safeguards. |
| [`textures.glsl`][textures] | Environment samplers; per-slot samplers, UV-set selectors and transform matrices; `get*UV` helpers. | Complete binding inventory and per-texture UV semantics. |
| [`functions.glsl`][functions] | `NormalInfo`, vertex colors, `clampedDot`, `applyIorToRoughness`, `rgb_mix`. | Small shared math/surface helpers; preserve colored Fresnel mixing. |
| [`primitive.vert`][vertex] | Local/world/clip transforms, normal matrix, tangent handedness, UV0/UV1, colors, optional animation/instancing. | Static mesh vertex path first; adapt conventions explicitly. |
| [`animation.glsl`][animation] | Texture-backed joints/morph targets, `getSkinningMatrix`, `getTarget*`. | Later animation reference, not a PBR prerequisite. |
| [`specular_glossiness.frag`][specgloss] | Separate legacy shading path using diffuse, specular and glossiness inputs. | Optional archived extension compatibility; do not blend its workflow indiscriminately with modern extensions. |
| [`scatter.frag`][scatter] | Diffuse-light prepass; RGB illumination and draw identity in alpha. | Experimental dense-volume scattering, with a safer identity format in OFG. |
| [`tonemapping.glsl`][tonemapping] | Exposure, Khronos PBR Neutral, ACES variants, gamma approximations and inverse mappings. | Exposure/tone-mapper math; output encoding needs a deliberate OFG contract. |
| [`tonemap_main.frag`][tonemap-main] | Fullscreen final pass with integer per-pixel tone-map classification. | Distinguishes lit, unlit and debug output; investigate mixed transparency before adopting its flag scheme. |
| [`cubemap.vert` / `cubemap.frag`][cubemap] | Environment background and transmission-pass treatment. | Sky/environment background, separate from reflected environment data. |
| [`source/Renderer/renderer.js`][renderer] | `prepareScene` (613), `drawScene` (729), `tonemapPass` (1523), `drawPrimitive` (1626), `applyEnvironmentMap` (2278). | Pass ordering, binding dependencies, raster/blend/depth state and feature selection. Port responsibilities, not the monolithic JS structure. |
| [`source/Renderer/shader_cache.js`][shader-cache] | Include expansion; `selectShader` inserts `#version 300 es` and defines; program cache. | Confirms compile-time variants. Replace its XOR define hash with a comparable structured OFG key. |
| [`source/Renderer/webgl.js`][webgl] | Texture upload, separate linear/sRGB representations, sampler state and mip generation. | Color/data texture distinction and upload requirements; WebGL calls themselves do not transfer. |
| [`source/gltf/material.js`][material-js] | Constructors/defaults, `getDefines` (54), `updateTextureTransforms` (120), `initGl` (185), extension classes (749 onwards). | CPU-side defaults, extension exclusions to verify, slot setup and material selection. |
| [`source/gltf/primitive.js`][primitive-js] | Attribute handling; `generateTangents` (1428), using bundled MikkTSpace. | Import-side tangent generation and missing-attribute policy. |
| [`source/gltf/light.js`][light-js] | Light descriptions and transforms. | glTF light import and units, paired with the extension specification. |
| [`source/ResourceLoader/resource_loader.js`][loader] | `loadGltf`, `loadEnvironment`, `_loadEnvironmentFromPanorama`. | Async loading and the environment asset bundle. OFG keeps its own resource contract. |
| [`source/ibl_sampler.js`][ibl-sampler] | `filterAll` (357), panorama conversion (369), `applyFilter` (413), Lambertian/GGX/Charlie filtering, LUT integration (499). | Environment preparation recipe, preferably offline initially. |
| [`source/shaders/ibl_filtering.frag`][filtering] | Distribution sampling and LUT integration. This is **outside** `source/Renderer/shaders`. | Required partner to runtime IBL equations; ordinary mipmaps are not an IBL prefilter. |
| [`source/shaders/panorama_to_cubemap.frag`][panorama] | Panorama-to-face direction mapping. | Cubemap orientation and bake convention reference. |
| [`assets/images`][images] | `lut_ggx.png`, `lut_charlie.png`, `lut_sheen_E.png`. | Inspect before reuse: current loader generates GGX/Charlie LUTs and loads sheen E separately. |

Picking, splats, physics, interactivity and viewer controls are outside the requested PBR port. Their presence in the repository does not make them prerequisites.

## How shading and passes fit together

The main shader evaluates texture/factor material inputs, constructs the shading frame, evaluates IBL and direct lights separately, applies extension layers, then adds emission. The perceptual roughness input is squared to obtain microfacet alpha. Dielectric normal-incidence reflectance defaults to 0.04 from IOR 1.5; metal reflectance comes from base color. The specular extension modifies dielectric reflectance and weight. Iridescence modifies Fresnel response. Sheen uses a Charlie distribution and an energy-scaling LUT; clearcoat sits over the underlying response and attenuates emission too. AO is applied to the IBL result, before direct lights and emission. These relationships matter as much as individual formulas. See [main assembly][pbr], [BRDFs][brdf] and [material evaluation][material-info].

The environment path is a matched bundle: a Lambertian diffuse cubemap, GGX roughness-prefiltered cubemap, GGX integration LUT, and, for sheen, Charlie cubemap/LUT plus sheen-energy LUT. `getIBLGGXFresnel` includes a multiple-scattering compensation term. The diffuse bake/runtime pair defines where the Lambertian normalization lives; do not introduce an extra division by pi simply because punctual diffuse uses one. The runtime maps perceptual roughness to the stored mip range. Match the **exposed** mip count and distribution to the bake, rather than assuming every allocated mip is a valid roughness level. See [IBL sampling][ibl], [baking orchestration][ibl-sampler] and [filtering shader][filtering].

Source-derived mesh frame sequence at this revision:

1. Gather lights and drawables, select camera and environment. Separate opaque/masked, transmissive, alpha-blended and scattering drawables.
2. If scattering is active, render the front diffuse-light/depth prepass.
3. If transmission is present, render the background environment plus non-transmissive opaque **and alpha-blended** geometry into a separate target. Resolve its MSAA and generate a mip chain. The source's nominally opaque background also includes transparency.
4. Render the main environment and opaque/masked geometry, then sorted transmissive geometry, then sorted ordinary transparency. Transmissive surfaces sample the background from step 3, not a recursive refraction of other transmissive surfaces.
5. Apply the final tone-map/output pass. The current implementation also handles splats and picking; those paths are outside OFG's material scope.

This follows [`drawScene`][renderer]. The source background is fixed at 1024 by 1024 and uses RGBA8/MSAA storage; the main framebuffer optionally uses RGBA16F. OFG should use viewport-aware HDR scene/background targets, with explicit cost and mip sizing. Copying those reference framebuffer choices would clamp bright transmitted lighting and import viewer-specific resolution assumptions.

Transmission is **not alpha blending**. Its factor replaces part of the diffuse lighting with refracted/filtered background radiance while retaining reflection. Alpha mode independently controls coverage/compositing. Volume adds path-length attenuation and thickness; dispersion samples different refracted directions for RGB. Screen-space transmission cannot recover hidden/offscreen geometry, recursive glass layers or caustics. Sorting cannot fix intersecting transparent triangles. These are declared approximation limits, not shader-port defects.

## Extension coverage and implementation order

Status is pinned to the inspected specification revision, not inferred from a shader macro or a `KHR` prefix. The [extension registry][registry] lists the stable material extensions below as ratified and specular/glossiness as archived. Diffuse transmission is a [Release Candidate][diffuse-spec]. Retroreflection has an [RC specification][retro-spec] despite not appearing in that registry's material list. The renderer links volume scattering to a [separate draft revision][scatter-spec]; the same path returned 404 at the inspected specification HEAD.

Every feature below has source in the downloaded renderer; none is implemented in OFG yet.

| Material feature | Key source / shader switch | Additional dependencies | Proposed phase |
| --- | --- | --- | --- |
| Core metallic/roughness | `getMetallicRoughnessInfo`, `MATERIAL_METALLICROUGHNESS` | Base/normal/MR/AO/emissive slots, tangents, HDR, lights and IBL. | Foundation. |
| `KHR_materials_unlit` | Early path in `pbr.frag`, `MATERIAL_UNLIT` | Base color/alpha only; tone-map bypass must be specified and tested. | Foundation. |
| `KHR_materials_emissive_strength` | Emission in `pbr.frag` | HDR emission; does not light nearby objects without a GI system. | Foundation. |
| `KHR_materials_ior` | `getIorInfo`, `MATERIAL_IOR` | Dielectric Fresnel; affects transmission when enabled. | Surface extensions. |
| `KHR_materials_specular` | `getSpecularInfo`, `MATERIAL_SPECULAR` | Scalar weight and color texture, preserving energy relationships. | Surface extensions. |
| `KHR_materials_clearcoat` | `getClearCoatInfo`, `MATERIAL_CLEARCOAT` | Separate normal, factor and roughness; direct/IBL layer composition. | Surface extensions. |
| `KHR_materials_sheen` | `getSheenInfo`, Charlie BRDF/IBL | Charlie environment, LUT and sheen E LUT; base-layer energy reduction. | Surface extensions. |
| `KHR_materials_iridescence` | `getIridescenceInfo`, `evalIridescence` | Film IOR and thickness in nanometres; colored Fresnel composition. | Surface extensions. |
| `KHR_materials_anisotropy` | `getAnisotropyInfo`, anisotropic GGX | Reliable tangent basis and texture/rotation convention; bent-normal IBL approximation. | Surface extensions. |
| `KHR_materials_transmission` | `getTransmissionInfo`, `getIBLVolumeRefraction` | Background pass, mip generation, view/projection, screen size. | Transmission. |
| `KHR_materials_volume` | `getVolumeInfo`, `applyVolumeAttenuation` | Local thickness, model scale and world-distance attenuation; closed-volume assumptions from the spec. | Transmission. |
| `KHR_materials_dispersion` | RGB path in `getIBLVolumeRefraction` | Transmission and IOR; three refraction samples. | Transmission. |
| `KHR_materials_variants` | Selection in `renderer.js::drawPrimitive`, primitive mappings | CPU material substitution and draw reclassification. No new BRDF. | Import/material integration. |
| `KHR_texture_transform` | Material transform setup and `get*UV` | Per-slot offset/rotation/scale and optional UV-set override. No new BRDF. | Texture foundation. |
| `KHR_lights_punctual` | `light.js`, `punctual.glsl` | Directional/point/spot descriptions, units, transforms and finite active light count. | Foundation. |
| `KHR_materials_diffuse_transmission` (RC) | `getDiffuseTransmissionInfo`, back-facing diffuse terms | Back illumination; optional volume absorption. Does not inherently need the specular transmission background pass. | Subsequent extension slice. |
| `KHR_materials_retroreflection` (RC) | `getRetroreflectionInfo`, reflected-view lobe in `pbr.frag` | Composition with anisotropy/iridescence; no new pass. | Subsequent extension slice. |
| `KHR_materials_volume_scatter` (draft) | `scatter.frag`, `getSubsurfaceScattering` | Diffuse/depth/identity buffers and Burley sampling. The inspected code addresses dense scattering; sparse scattering is unchecked in README. | Separate experimental milestone. |
| `KHR_materials_pbrSpecularGlossiness` (archived) | `specular_glossiness.frag` | Legacy diffuse/specular/glossiness inputs and exclusions. | Optional compatibility milestone. |

Texture compression (`KHR_texture_basisu`), geometry compression and animation are loading/geometry features, not additional PBR lobes. Track their support independently. A correct shader does not establish full glTF support.

## Material input contract to preserve

The table follows [material evaluation][material-info] and [texture setup][material-js]. Color textures need sRGB decoding on RGB only; alpha stays linear. Factors are already linear. Normal, scalar, LUT and direction data must not receive sRGB decoding.

| Slot | Channels / interpretation |
| --- | --- |
| Base color | sRGB RGB, linear A; multiply by baseColorFactor and vertex color. |
| Metallic/roughness | Linear G roughness, B metallic; R unused here. |
| Occlusion | Linear R, strength interpolation; may share the MR image, but retains its own UV/sampler contract. |
| Normal / clearcoat normal | Linear RGB remapped to signed tangent-space XYZ; scale XY, normalize. |
| Emissive | sRGB RGB multiplied by linear emissive factor and strength. |
| Clearcoat | Linear R factor; separate linear G roughness. |
| Sheen | sRGB RGB color; linear A roughness. |
| Specular | Linear A strength; separate sRGB RGB color. |
| Transmission / volume | Linear R transmission; linear G thickness. |
| Iridescence | Linear R factor; linear G interpolation between minimum/maximum thickness. |
| Anisotropy | Linear RG mapped to signed direction, B strength, then rotation. |
| Diffuse transmission | Linear A factor; separate sRGB RGB color. |
| Retroreflection | Linear R factor. |
| Legacy specular/glossiness | sRGB RGB specular, linear A glossiness; diffuse uses sRGB RGB and linear A. |

Default core factors are white base color, metallic 1, roughness 1, black emission, alpha mode OPAQUE, alpha cutoff 0.5 and single-sided rendering. IOR defaults to 1.5, specular weight/color to 1, and emissive strength to 1. Extension layer strengths default to zero; sheen color defaults to black. Iridescence film IOR defaults to 1.3 and thickness range to 100–400 nm. Volume thickness defaults to zero and attenuation color to white. The reference encodes infinite attenuation distance as zero internally; an importer must distinguish that sentinel from an invalid explicit finite distance. See [constructors][material-js].

Texture absence must give the documented neutral value without multiplying an uninitialized field. Each material texture slot owns its UV selection, transform and sampler choice, even when images are shared. An image used once as color and once as data requires compatible separate views or uploads, not a single globally chosen color space. Start with UV0/UV1 as a declared profile, matching this shader; reject unsupported requested sets explicitly until added.

## Portability and correctness hazards

### Binding limits and shader variants

WebGPU's baseline has 16 sampled textures and 16 samplers per shader stage; inspect the [WebGPU limit definitions][webgpu-limits] and the actual created device. A core material with five slots, three clearcoat slots, two sheen slots and two specular slots already uses 12 material textures; the six environment/LUT textures bring that example to 18. This is an illustrative independent-slot count before deduplication, not a measured compiled layout. Transmission and scattering add more resources.

Specialize on extension/texture presence and pass requirements. Share equivalent samplers and deduplicate identical resource uses where semantics allow, but distinct UV transforms alone do not create new images. Constant factors stay uniforms. Do not generate variants for every factor value or every exact active-light count. Reflect the compiled layout and fail unsupported combinations before submitting. Texture arrays are not a transparent fix: dimensions, format, mip count and sampling semantics must agree. A future asset-packing solution needs those contracts and tests. Do not silently drop extensions or count shader dead-code elimination as proven without checking emitted layouts.

The pinned RHI's `DeviceLimits` exposes dimensions and sampler counts but not the complete WebGPU per-stage texture-limit set. Use the browser host's actual WebGPU limits where accessible, or an explicitly conservative baseline with pipeline-creation probes. Validate color/depth format usage with RHI `getFormatSupport`. Native success does not establish browser binding compatibility.

### GLSL-to-Slang changes that affect results

Use Slang entry-point semantics, explicit constant buffers, `Texture2D`/`TextureCube` and `SamplerState`; replace combined GLSL samplers. Translate `texture`/`textureLod`/`texelFetch` deliberately to implicit-gradient sampling, explicit LOD sampling or loads. Keep fragment derivatives outside divergent control flow and retain the late alpha-mask discard that avoids derivative artifacts. Compile to both RHI targets from the first textured slice.

OFG uses [0,1] clip depth. Khronos's screen-space scattering reconstructs GL depth with `depth * 2 - 1`; that expression cannot transfer unchanged. Test framebuffer UV orientation, cubemap faces, front-face semantics and normal-map handedness independently. GLSL matrix constructors/indexing use columns; Slang's storage annotation does not by itself preserve constructor or multiplication semantics. Translate model-axis scale extraction, TBN assembly and UV rotations explicitly using `mul` and tested layouts. Normals need inverse-transpose transforms under nonuniform scale; tangents use the model's linear transform plus orthogonalization/handedness. Mirrored transforms must change winding consistently with the tangent frame.

All view-dependent shading inputs must use the same coordinate origin. Begin with current near-origin fixtures, but define the draw ABI using camera-relative positions and separate scale/normal information so a later global-coordinate system does not require rewriting PBR equations. This research does not implement large-world coordinates.

### Numerical and reference-code review findings

These are source-level hazards to test, not reproduced upstream bug reports:

| Observation | Evidence | OFG requirement |
| --- | --- | --- |
| `MaterialInfo` is not comprehensively initialized; transmission can read volume fields when the volume macro is absent. Scattering also references independently optional layer fields. | `pbr.frag` assembly and `getIBLVolumeRefraction` call. | Initialize a complete material surface description with neutral defaults before applying enabled layers. Test transmission without volume. |
| GGX distribution can encounter a zero denominator at exactly zero roughness and aligned normal/half vector. | `D_GGX` in `brdf.glsl`. | Define a documented finite evaluation floor/limit and regression tests, retaining authored roughness separately. Do not change highlight shape casually. |
| Derivative TBN divides by a UV determinant; the small-derivative substitutions do not prove all degeneracies safe. | `getNormalInfo`. | Test missing/degenerate UVs and zero tangents; prefer imported/generated tangents where valid. |
| Clearcoat normal mapping reuses the base normal frame, despite a separate UV selector. | `getClearcoatNormal`, `textures.glsl`. | Test independently transformed clearcoat normal UVs; derive the appropriate frame when needed. |
| Dispersion uses the final blue ray length for absorption; source includes a TODO. | `ibl.glsl::getIBLVolumeRefraction`. | Choose and record reference parity versus a spec-grounded correction, backed by a fixture. |
| Reference color conversion uses power 2.2 approximations. | `tonemapping.glsl`. | Prefer exact sRGB transfer or hardware sRGB views/targets, encode once, and record this intentional difference in parity comparisons. |
| A single integer tone-map flag does not represent fractional mixtures of lit/unlit/debug classes. | `tonemap_main.frag`, `pbr.frag`, alpha blending in `drawPrimitive`. | Explicitly test mixed lit/unlit transparency; resolve compositing policy before claiming parity. Copying the flag buffer alone is insufficient. |
| Scattering identity is encoded as `drawID / 255` in alpha and compared using equality after texture sampling. | `scatter.frag`, `getSubsurfaceScattering`. | Use a separate integer identity texture with unfiltered access; test adjacent objects and IDs beyond 255. |
| `scatterAnisotropy` is parsed but no use was found in renderer/shader evaluation. | `material.js` versus renderer/shader search. | Do not advertise full volume-scatter semantics from this port. Dense Burley scattering is a limited implementation. |
| Shader cache uses XOR of define hashes. | `shader_cache.js::selectShader`. | Use canonical, equality-checked feature/layout keys; no collision-only identity. |

Validate denominators, zero-distance lights, zero-length half vectors, singular transforms and black/zero-distance attenuation boundaries. Report programmer-invalid transforms and unsupported assets usefully. Numerical safeguards should have an explained valid-domain effect, not be an assortment of clamps added until images look plausible.

## Fit with current OFG

The inspected baseline is described in [architecture](../architecture.md), [resource ownership](../resources.md) and [scene contracts](../state-and-scene.md).

| Existing OFG source | Current behavior | Required change during implementation |
| --- | --- | --- |
| `shaders/mesh.slang` | UV checker; `clipFromLocal` and tint. Input normal is unused. | Keep as diagnostic; add PBR modules and a PBR fixture. |
| `src/resources/mesh.h` | Fixed position/normal/UV vertex; triangle indices and submesh materials. | Add a concrete PBR vertex contract for tangents, UV1 and color, with attribute-presence/default policy. Avoid a generic vertex framework. |
| `src/resources/material.*` | Shared Shader and named scalar/vector/matrix values; clones copy values. The current binder requires an exact set of direct uniform fields. | Add textures and render state plus a concrete typed PBR description. Extend binding explicitly for resources/structured frame data; do not force texture handles through `UniformValue`. Preserve shader sharing and clone independence. |
| `src/resources/shader.*` | One source string and vertex/fragment entries; program prepared lazily. | Deterministic source/module packaging, feature variants and diagnostics; keep embedded shaders independent of launch directory. |
| `src/render/graphics.cpp` | One opaque pass, D32Float/Less/write, no blend/cull; pipeline cache keyed by shader owner and target format. | Bind frame/draw/material resources; key every varying pipeline setting; add HDR/pass targets and explicit pass sequence. |
| `src/render/draw-list.*` | Creation-order draws with clip matrix and world transform; assets retained. | Camera/view data, normal transform/handedness, lights/environment snapshot and opaque/masked/transmission/blend queues. |
| `src/scene/mesh-renderer.*` | Shared materials, nullable overrides, explicit unique clones. | Reuse these ownership semantics for PBR and material variants; never mutate shared defaults accidentally. |
| `src/resources/resources.*` | Pollable cooperative loading with weak lookup. | Add image/environment/optional glTF loading within this lifetime model and reject stale requests. |
| `src/web-main.cpp`, `src/render/present.*` | Host-owned image before acquire; resource-only final canvas pass avoids Asyncify invalidation. | Complete PBR preparation and HDR composition before acquisition; preserve the final no-yield canvas path. |

Use explicit `FrameParameters`, `DrawParameters` and typed material parameters as proposed shader boundaries. Materials retain textures; draws retain their assets; Graphics owns frame targets and weak caches. RHI command submission already retains referenced GPU resources for the exercised paths. Extend and verify that behavior for textures and resized targets; do not add a second retirement framework without a demonstrated missing lifetime guarantee.

No shadow-map, SSAO, SSR, bloom, TAA, clustered-light or global-illumination pipeline was found in the inspected PBR path. Microfacet masking/shadowing in GGX is not scene shadowing. Those systems may be useful later for terrain, but are separate from this reference port. Start with a bounded forward light list and one environment.

## Verification strategy and remaining questions

Use small native GPU/readback fixtures for numeric and binding contracts, a procedural material grid for immediate inspection, then imported Khronos assets. Candidate assets verified in the [pinned Models inventory][models] include `MetalRoughSpheres`, `NormalTangentMirrorTest`, `TextureTransformMultiTest`, `AlphaBlendModeTest`, `ClearCoatTest`, `SheenTestGrid`, `SpecularTest`, `IORTestGrid`, `IridescenceDielectricSpheres`, `AnisotropyRotationTest`, `EmissiveStrengthTest`, `TransmissionOrderTest`, `TransmissionRoughnessTest`, `TransmissionThinwallTestGrid`, `DispersionTest`, `DiffuseTransmissionTest` and `UnlitTest`. Select a small licensed subset; this is not a claim that they have passed.

For meaningful reference comparison, record the exact asset, environment/bake, camera, viewport, light values, exposure, tone mapper, extension settings and reference commit. Compare scene-linear offscreen results for controlled tests and inspect native/browser display captures separately. Different rasterization edges and the deliberate exact-sRGB change need explicit tolerances. Save mismatch images and diagnostics under `artifacts/pbr`; a successful build is insufficient.

The first implementation milestone must settle the compiled resource budget, Slang module/reflection route, RGBA16F filtering/rendering support and tone-map/unlit compositing policy. Later milestones settle the minimal importer library and baked-environment container after inspecting their actual requirements. Avoid selecting new dependencies solely from this source survey. Browser variant compilation cost, GPU cost and live residency are unmeasured.

## Research validation

Downloaded and inspected the renderer, its material and primitive preparation, shader cache, environment loader/filtering, pass orchestration and PBR-related shader modules. Cross-checked selected extension status against pinned specifications and compared OFG's source/ownership/presentation boundaries. Validation passed for 27 local documentation links and 29 exact-case pinned renderer paths across the four changed documents, reference-label resolution, code-fence balance and trailing whitespace. `git diff --check` passed; the downloaded checkout remains unmodified. No PBR compilation, runtime test, upstream rendering, screenshot comparison or benchmark is claimed.

[license]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/LICENSE.md
[banner]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/LICENSE_BANNER.txt
[thirdparty]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/THIRDPARTY.md
[pbr]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/pbr.frag
[material-info]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/material_info.glsl
[brdf]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/brdf.glsl
[ibl]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/ibl.glsl
[punctual]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/punctual.glsl
[iridescence]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/iridescence.glsl
[textures]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/textures.glsl
[functions]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/functions.glsl
[vertex]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/primitive.vert
[animation]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/animation.glsl
[specgloss]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/specular_glossiness.frag
[scatter]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/scatter.frag
[tonemapping]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/tonemapping.glsl
[tonemap-main]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders/tonemap_main.frag
[cubemap]: https://github.com/KhronosGroup/glTF-Sample-Renderer/tree/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shaders
[renderer]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/renderer.js
[shader-cache]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/shader_cache.js
[webgl]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/Renderer/webgl.js
[material-js]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/gltf/material.js
[primitive-js]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/gltf/primitive.js
[light-js]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/gltf/light.js
[loader]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/ResourceLoader/resource_loader.js
[ibl-sampler]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/ibl_sampler.js
[filtering]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/shaders/ibl_filtering.frag
[panorama]: https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/cc27919cacbb235d2f58a0c0203387efce9375f8/source/shaders/panorama_to_cubemap.frag
[images]: https://github.com/KhronosGroup/glTF-Sample-Renderer/tree/cc27919cacbb235d2f58a0c0203387efce9375f8/assets/images
[registry]: https://github.com/KhronosGroup/glTF/blob/8e691206fa2e981fc3b8c8bc57e671595576c971/extensions/README.md
[diffuse-spec]: https://github.com/KhronosGroup/glTF/blob/8e691206fa2e981fc3b8c8bc57e671595576c971/extensions/2.0/Khronos/KHR_materials_diffuse_transmission/README.md
[retro-spec]: https://github.com/KhronosGroup/glTF/blob/8e691206fa2e981fc3b8c8bc57e671595576c971/extensions/2.0/Khronos/KHR_materials_retroreflection/README.md
[scatter-spec]: https://github.com/KhronosGroup/glTF/blob/e17468db6fd9ae3ce73504a9f317bd853af01a30/extensions/2.0/Khronos/KHR_materials_volume_scatter/README.md
[webgpu-limits]: https://gpuweb.github.io/gpuweb/#limits
[models]: https://github.com/KhronosGroup/glTF-Sample-Assets/tree/edc7c9e67c639d230715049ee31f9a96a6babbbe/Models

## Implemented OFG source map (2026-10-03)

| Responsibility | OFG code |
| --- | --- |
| Close-port BRDF / thin film | `shaders/pbr/brdf.slang`, `iridescence.slang` |
| Material factors / texture slots / source variants | `src/resources/pbr-material.*`, `shaders/pbr/material.slang` |
| Vertex frames / surface evaluation | `shaders/pbr/mesh.slang` |
| Direct / IBL / layer assembly | `shaders/pbr/lighting.slang` |
| Transfer / Neutral tone mapping | `shaders/pbr/common.slang`, `output.slang` |
| Environment bake / decode / upload | `tools/bake-pbr.py`, `src/resources/environment.*`, `Graphics::prepareEnvironment` |
| Explicit forward passes / reflected bindings | `src/render/graphics.cpp` |
| Sphere grid / navigation | `src/lab/pbr-fixture.*`, `sphere.*`, `fly-camera.*` |
| CPU / actual GPU / browser proof | `tests/pbr-core-test.cpp`, `tests/graphics-test.cpp`, `tools/pbr-shader-check.mjs`, `tools/pbr-smoke.mjs` |

Upstream notices and adaptation differences are in [shader provenance](../../shaders/pbr/NOTICE.md). Transmission/scattering remain research for future work; no general glTF importer or blanket reference-renderer parity is claimed.
