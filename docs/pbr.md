# PBR material laboratory

Run `build/native/ofg.exe` for the sphere grid. Native: hold the right mouse button to look and move with WASD; Q/E move down/up, Shift increases speed, R resets the overview, F selects the close-up, and 0–4 select shaded/normals/roughness/metallic/base-color views. Escape releases capture; when uncaptured it closes the native window. Focus loss releases capture. In the browser, click the canvas to capture instead of holding RMB; Escape releases pointer lock. Keyboard movement only applies while captured. No ImGui layer exists yet.

`--scene` and `?demo=scene` retain the textured cube diagnostic. `--checkerboard` and `?demo=checkerboard` retain the original checkerboard. `?pbr=budget` exercises the maximum supported material binding layout.

## Grid legend

The main 7-by-7 grid has constant linear base color `(0.8, 0.55, 0.25)`. Columns increase authored perceptual roughness from 0 to 1 in steps of 1/6. Rows increase metallic from bottom to top, also in steps of 1/6. Each sphere has an independent material, over one shared 48-by-24 procedural mesh. Spheres have radius 0.65 metres and centres 1.65 metres apart. Lights stay fixed while the camera moves.

| Row below main grid | Left to right |
| --- | --- |
| First, Y=-6.6 | Brown dielectric baseline; clearcoat; red sheen; metallic iridescence; metallic anisotropy; HDR cyan emission; cyan unlit |
| Second, Y=-8.25 | Shared MR/AO data texture; rotated normal map; IOR + textured specular; textured clearcoat with its own rotated UV1 normal map; textured sheen; textured iridescence + anisotropy; alpha-masked emissive texture |

`src/lab/pbr-fixture.cpp` is the exact capture manifest for material values and lighting. Overview is `(0,-1.65,-15)` looking along +Z, close-up `(0,0,-5)`, vertical FOV 60 degrees. Environment intensity/exposure are 1 and environment rotation is 0. The directional light has direction `(0.4,-0.5,1)` and intensity 2 lux. The point light is at `(-4,4,-4)`, color `(1,.85,.65)`, intensity 45 candela. Scene lengths are metres.

## Renderer and material contract

`createPbrMaterial(PbrMaterialDesc)` validates physical factor ranges and creates an ordinary `Material`. Named uniform values and resource bindings are its authoritative editable state; the descriptor is construction input only. Factors can be edited with `setUniform` within their documented descriptor domains. Recreate a material to change texture presence or unlit classification; assign it through `MeshRenderer::setMaterialOverride`. `clone()` copies factors, render state and slot transforms while sharing immutable shader/image/sampler dependencies. The single application thread must finish edits before render submission; each encoded draw has its own uniform storage.

Core metallic/roughness, emissive strength, unlit, IOR, specular, clearcoat, sheen, iridescence and anisotropy have shader implementations, including all their surface texture slots. This is a glTF-compatible surface model, not a claim of complete glTF asset/extension conformance: no glTF importer or `KHR_materials_variants` file mapping is included. Runtime material switching uses existing renderer overrides. Transmission, volume absorption, dispersion, diffuse transmission, retroreflection, scattering and legacy specular/glossiness remain deferred.

| Texture slot | Sample interpretation |
| --- | --- |
| BaseColor | sRGB RGB, linear alpha; multiplied by vertex color and factor |
| MetallicRoughness | Linear G roughness, B metallic |
| Normal / ClearcoatNormal | Linear tangent-space RGB; independent normal scales and UV frames |
| Occlusion | Linear R, strength affects IBL only |
| Emissive | sRGB RGB, factor times emissive strength |
| Specular / SpecularColor | Linear A weight / sRGB RGB color |
| Clearcoat / ClearcoatRoughness | Linear R weight / linear G roughness |
| SheenColor / SheenRoughness | sRGB RGB / linear A roughness |
| Iridescence / IridescenceThickness | Linear R weight / linear G interpolates min–max nanometres |
| Anisotropy | Linear RG direction remapped to [-1,1], B strength; rotation in radians |

Every slot selects UV0 or UV1 and its own offset, scale, rotation and sampler. Sharing MR/AO image storage does not share slot transforms. Use sRGB texture formats for color and linear formats for data; there is no implicit format reinterpretation. Existing texture resources handle mips, loading/cancellation and immutable image lifetimes.

Vertices now include normal, UV0, tangent XYZW, UV1 and RGBA color. Tangent W is +/-1; W=0 requests a derivative UV0 frame. A zero normal requests a geometric derivative normal. A degenerate UV frame falls back to the geometric frame. Normal maps derive a frame independently from their transformed UVs, including mirrored coordinates. This is not MikkTSpace generation or an importer promise. Sphere tangents are analytic and account for V increasing downward. World normals use inverse transpose, tangents use the forward transform, and negative determinants adjust tangent handedness and culling.

The source family is embedded in fixed order by CMake from `shaders/pbr/{common,brdf,iridescence,material,lighting,mesh}.slang`; no working-directory shader search is needed. Source variants are keyed by texture presence and unlit mode. Surface factors do not create variants. The pipeline key additionally includes target format, alpha/cull classification, mirrored winding and fullscreen state. Slang WGSL numeric semantic indices explicitly match a separate PBR input layout; the older diagnostic ABI is preserved. Root reflection checks material fields and the portable resource budget.

The conservative portable profile permits **12 material textures plus four IBL textures**, with no more than 16 sampled textures or samplers. A maximum material uses 16 textures and 13 samplers. A thirteenth material texture produces an explicit error before submission even on a stronger native device. This excludes some heavily textured combinations; it does not silently drop extensions. Factor-only extensions remain available without extra image bindings. Actual enabled WebGPU limits and compiled layouts are recorded by the smoke report.

## Lighting, IBL and output

`Scene::lighting` owns up to four directional, point or spot lights plus a shared immutable `Environment`; `DrawList` copies those frame settings and the camera position. Light directions describe travel; point/spot positions and camera/geometry use the same coordinate space. Point lights use inverse-square attenuation with optional range cutoff; spots add squared cone attenuation. No shadows are generated.

IBL means **image-based lighting**: prefiltered environment radiance supplies diffuse illumination and roughness-dependent reflections from all directions. The runtime uses a Lambertian cube, GGX cube, Charlie cube and combined lookup (RG GGX split sum, B Charlie radiance, A sheen energy scaling). Environment orientation is applied to all cube lookups. It is lighting, not merely a background image; the laboratory intentionally keeps a flat background.

`tools/bake-pbr.py` reproducibly generates `assets/pbr-studio.bin` from a continuous analytic studio environment. NumPy is only needed to regenerate this asset. Normal native/browser builds embed the committed bake. It has 32-square faces, six mip levels, a 64-square lookup and 1,024 importance samples per texel. The binary starts with `OFGIBL1\0`, followed by little-endian uint32 cube size, mip count and LUT size; each of three cubes is face-major (+X,-X,+Y,-Y,+Z,-Z), then mip-major RGBA16F; the RGBA16F lookup follows. The loader rejects malformed sizes and negative/nonfinite payloads before GPU allocation. Upload publishes all four textures atomically. Existing RHI command buffers retain submitted GPU objects through completion; weak application tracking clears surviving resources at Graphics shutdown.

Khronos source/licensing and intentional adaptations are recorded in [shaders/pbr/NOTICE.md](../shaders/pbr/NOTICE.md). The small analytic bake samples its continuous source directly instead of filtering an HDR input image's mips. Its sheen energy channel integrates the runtime sheen lobe. It is a laboratory asset, not a general environment importer. Perceptual roughness is clamped to 0.045 for finite BRDF evaluation (including authored zero); factors retain their authored values. The roughness debug view shows the evaluated value.

Pass order is explicit:

1. Lit opaque/masked draws write scene-linear RGBA16F plus D32 depth.
2. A fullscreen pass applies exposure and Khronos PBR Neutral into display-linear RGBA16F. Linear diagnostic mode skips the tone mapper.
3. Unlit opaque/masked draws compose with the existing depth; a single sorted back-to-front transparent queue then combines lit and unlit surfaces. Lit transparent fragments receive tone mapping before blending. Unlit values bypass exposure/tone mapping.
4. Final output applies exact sRGB encoding for UNORM targets, relies on hardware for sRGB attachments, and leaves floating diagnostic targets linear. Browser presentation then copies the completed host-owned image after acquiring the canvas, without yielding.

This chooses **display-linear alpha composition**. It preserves mixed lit/unlit coverage without a lossy integer classification flag or approximate inverse tone mapping. It differs from blending all lit HDR layers before tone mapping. Transparency sorts object origins, does not write depth, and cannot correctly order intersecting transparent surfaces. No transmission is implied by alpha blending.

## Validation and current limits

CPU tests cover camera movement/reset/pitch, sphere winding/tangent seam, material defaults/clones/UV transforms/binding limits, and malformed environment data. Native GPU tests run the actual shaders for analytic GGX limits, white furnace, cube orientation/rotation/replacement, point/spot attenuation, HDR emission, unlit transfer, alpha mask boundaries, mixed blending, nonuniform transforms and texture channels. Float checks account for intermediate FP16 quantization (typically 0.2% relative); direct 8-bit transfer checks allow one code value.

`node tools/pbr-shader-check.mjs` checks the pinned compiler's emitted vertex ABI and actual Chrome WGSL uniformity validation for core, all-texture and unlit sources. `node tools/pbr-smoke.mjs` exercises presentation, camera capture/movement/release/reset/blur, close-up, debug normals, resize/reload and the maximum binding layout. `npm run smoke:web` retains the texture/checkerboard regressions. The normal-view check guards the demonstrated vertex-location mismatch. Reports/captures live under `artifacts/pbr`.

There is no blanket Khronos pixel-parity or extension-conformance claim. The analytic limits and cross-platform captures establish this implementation's basis; imported reference assets and a full reference-renderer differential suite remain future validation. The environment's low resolution can be visible in smooth reflections. There is no general HDR file loader, asynchronous environment loader, runtime filtering, animation, shadowing, occlusion generation, bloom or transmission/scattering path.


Verified 2026-10-03: native CTest passes all four targets; core tests have 65 cases/1611 assertions and scene/PBR/texture GPU tests have 19 cases/1009 assertions. Native and browser overview/close-up images were inspected. Both browser smoke suites and WGSL checks pass. A matched 960x640 D3D12/WebGPU capture comparison has mean absolute channel error 0.0148/255; 99% of interior differences are at most one code value, excluding a two-pixel silhouette band. Outliers remain at texture/mask boundaries; this is descriptive cross-backend evidence, not a Khronos reference tolerance.

The [active plan](plans/pbr-rendering.md#artifacts-and-notes) records timing and unpadded payload budgets. Physical GPU allocation/residency, including pending command retention, has not been measured; payload totals must not be presented as that measurement. The plan remains open for this and the full Khronos differential validation.
