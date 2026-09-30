# OFG terrain-first restart: findings and recommendation

Research date: 2026-09-30. This is a source review and proposed direction, not a successful build or benchmark. Three independent investigations covered terrain, engine architecture, and Slang frameworks. The backup at `C:\dev\ofg-old2` was inspected read-only. No historical test results were treated as current validation.

Subsequent user decision, 2026-09-30: proceed with C++/Slang RHI, native D3D12 for daily development, browser WebGPU proved early and checked periodically, thin SGL-inspired helpers and Dear ImGui. Begin with a terrain laboratory and add a walking character/follow camera early as a terrain inspection tool. The alternatives below remain research context rather than an open framework selection. Current implementation guidance is in [the bootstrap plan](../plans/terrain-lab-bootstrap.md).

## Recommended direction

Keep the browser as a first-class target. Evaluate a small C++ runtime using Slang RHI, with native D3D12 and Emscripten/WebGPU hosts and shared Slang shaders. Use Python and SlangPy for terrain experiments and offline landmass baking. This gives Python a useful role immediately without making the browser depend on a Python extension port.

Adopt that runtime only after a focused graphics-and-compute portability spike. Native backend support does not imply identical browser behavior. Slang RHI remains under active development, and upstream explicitly cautions that it is not ready for general use. Pin the entire compatible dependency set and keep RHI usage concentrated in a small renderer boundary. Avoid rebuilding a general engine before proving terrain.

The first product should be a terrain laboratory: navigate a large landmass, inspect drainage and detail levels, watch residency and streaming queues, and test boundaries under hostile conditions. A factory simulation, multiplayer game, and evolving world remain the application direction, but they should not precede proof of terrain scale and continuity.

## What the backup actually contains

| Area | Source evidence | Consequence for the restart |
| --- | --- | --- |
| Application | `README.md`; `cpp/src/native/render_smoke.cpp:636` | Substantial browser graphics demo, including sky, shadows, bloom, UI and animated player. Native work is an offscreen Dawn/Vulkan smoke path, not a native playable DX12 application. |
| Terrain | `cpp/src/terrain/terrain.cpp:115`; `cpp/src/terrain/terrain_chunk.cpp:305` | Synchronously reconciles 25 LOD0 chunks, each 32 m wide: a 160 m square. Other detail levels are rejected. |
| Generation | `cpp/src/terrain/terrain.cpp:151` | Four sine-wave octaves. No active hydrology, erosion, climate, network paging, or multiresolution terrain implementation. |
| Useful continuity | `cpp/src/terrain/terrain_chunk.cpp:70` | Shared world-coordinate sampling and samples beyond boundaries for normals are good concepts to retain. |
| Lifetime defect | `cpp/src/terrain/terrain.cpp:136`; `cpp/include/ofg/core/ptr.hpp:1`; `cpp/src/resources/resources.cpp:349` and `:594` | Evicting a chunk drops non-owning references. The global resource registry retains its meshes until global teardown. A bounded chunk map is not a bounded cache. |
| Precision | `cpp/src/terrain/terrain.cpp:151` and `:235`; `cpp/src/terrain/terrain_chunk.cpp:163` | Local mesh vertices still depend on absolute float sampling and transforms. This cannot preserve metre-scale addressing across the entire supported coordinate range. |
| Tests | `cpp/tests/terrain_test.cpp:35`; `cpp/tests/terrain_chunk_test.cpp:65` | Tests establish some deterministic sampling and same-resolution edge behavior, not mixed-resolution continuity or sustained streaming. |
| Scope | `docs/plans/terrain-chunk-renderer-plan.md` | The old milestone intentionally deferred async streaming, LOD, hydrology and erosion; some documentation/review work remains unchecked. |

All paths in this table are relative to `C:\dev\ofg-old2`. Stale compiled files named after hydro, volume, or climate features are not evidence of live implementations. The two research documents in `docs/research` contain useful ideas but also unresolved citation tokens; verify their claims against original sources when implementing.

Retain the thin platform host, shared application logic, explicit frame ownership, stable addresses, request identity, deterministic data, and visual regression concepts. Reconsider global singleton lifetime, backend-specific resources throughout the scene, synchronous generation, and one newly allocated mesh/material per traversed chunk. Preserve ideas rather than importing an engine with obsolete constraints.

## Slang choices

| Choice | Browser outlook | Native outlook | Assessment |
| --- | --- | --- | --- |
| C++ + Slang RHI; optional Python/SlangPy baker | Upstream Emscripten/WebGPU path exists; graphics/compute workload needs validation | Direct native graphics backends, including D3D12 | Recommended experiment; fits the preference for both targets. |
| Python + SlangPy for the application | No documented supported browser runtime found | Supported desktop GPU framework with convenient Python experimentation | Attractive if browser support is deliberately dropped; not a browser-ready substitute. |
| Python + Falcor2 on SlangPy | No supported browser path found; supplied rendering architecture adds portability work | Scene, material, editor and rendering toolkit on native C++/Slang | Stronger native-first candidate when existing rendering infrastructure is valuable; must prove dynamic terrain integration. |
| Existing direct WebGPU/Dawn approach + Slang-generated shaders | Familiar browser boundary, though shader/binding integration still needs work | Native WebGPU implementation can target desktop APIs, with WebGPU feature constraints | Fallback if RHI integration cost outweighs benefits. |

Slang is the shader language/compiler. Slang RHI is a C++ interface over GPU backends. SlangPy supplies native bindings and higher-level Python conveniences. Sharing a shader language helps portability; sharing Python call-generation and automatic binding behavior is a separate problem.

At review time Slang RHI `main` resolved to `16324a68af477baaede620e713644f5e9613b1a2` (2026-09-29). SlangPy `main` resolved to `bdbc9f3f809c5f6eda7e61e6eecae787db3f9620` (2026-09-30), pinning that RHI revision. These are research snapshots, not yet validated OFG dependency pins.

The RHI browser integration uses Emscripten and `emdawnwebgpu`. Current build configuration downloads Slang 2026.17.1 WASM libraries; browser compiler integration is more concrete than merely exporting WGSL from an offline tool. However, its shader-program interface uses Slang component/reflection objects. Do not assume raw precompiled WGSL is a supported replacement for that machinery, or claim compiler-free browser delivery before testing it.

The inspected browser example exercises device, queue and buffer setup. Upstream Emscripten CI is build-only, without tests. The WebAssembly command path disables indirect draw calls, so a first terrain renderer should use CPU-selected patches and direct/instanced draws. Indirect rendering can become a later capability-gated optimization. The first spike must also measure bundle size, startup, shader compilation, uploads and readback, instead of validating only a triangle.

Use small explicit compute entry points and versioned data layouts for algorithms intended to move from SlangPy to C++. SlangPy-generated wrappers, tensors and binding conventions do not automatically become C++ RHI entry points. Offline-only algorithms may use native features freely; runtime algorithms must fit the browser capability baseline. Query actual limits and optional features rather than assuming native GPU limits.

## Falcor2 as a native-first alternative

The user's follow-up adds [NVlabs/falcor2](https://github.com/NVlabs/falcor2), which is a distinct repository from the older NVIDIAGameWorks/Falcor. The reviewed revision is `ad70611b56d0dd47b6b6f17de1f0ca484d0662ff` (2026-09-16). Its [README](https://github.com/NVlabs/falcor2/blob/ad70611b56d0dd47b6b6f17de1f0ca484d0662ff/README.md) describes editable Python development with native C++ extensions and a dependency chain of Falcor2, SlangPy/SGL, then Slang RHI. This is a choice about how much toolkit to adopt above the same GPU foundation, not an unrelated rendering backend.

Falcor2 supplies scene, material, editor and render-node infrastructure, including path-tracing facilities. Windows/Linux setup is documented; no supported browser deployment was found. Its native dependencies and supplied rendering paths make a future browser port more substantial than preserving portable numerical kernels and data formats. Do not infer browser compatibility simply because the underlying RHI has a WebGPU backend.

Concrete portability constraints include its [fully bindless texture manager](https://github.com/NVlabs/falcor2/blob/ad70611b56d0dd47b6b6f17de1f0ca484d0662ff/src/falcor2/render/texture_manager.h) and [ray-tracing reference renderer](https://github.com/NVlabs/falcor2/blob/ad70611b56d0dd47b6b6f17de1f0ca484d0662ff/falcor2/rendernodes/reference_pathtracer_node.py). Its [static-mesh interface](https://github.com/NVlabs/falcor2/blob/ad70611b56d0dd47b6b6f17de1f0ca484d0662ff/src/falcor2/render/geometry/static_mesh_geometry.h) permits dirty vertex updates, which is a useful integration lead but not proof of efficient streamed terrain. The reviewed Falcor2 tree pins SlangPy `47f06a191e05df612282686ea2c45ff5394187da`, different from the separate latest-version review above.

For a native terrain laboratory, this is a serious candidate: Python can drive experiments while Slang handles GPU work and the toolkit supplies inspection/rendering infrastructure. For a shipping terrain runtime, first demonstrate repeated procedural mesh or height-buffer updates, tile insertion/removal, local-coordinate rendering, resource retirement and a suitable raster path. A mature-looking scene viewer or path tracer does not establish terrain streaming, mixed-resolution seams, hydrology, collision or multiplayer. No implementation of those terrain systems was established by this review.

Public release maturity is limited: no published releases were found at review time. Pin its own dependency tree for evaluation rather than forcing it to use the separately reviewed newest SlangPy/RHI revisions. Its top-level package is Apache-2.0; audit the actual optional dependency/data licenses when choosing what to ship.

With browsers optional, compare two small native experiments before selecting the framework: plain SlangPy with a minimal custom terrain renderer, and Falcor2 hosting the same dynamic tile fixture. Measure setup/iteration cost, integration code, memory behavior and rendering throughput. Prefer Falcor2 if its facilities remove work without forcing terrain into an unsuitable static-scene representation; prefer plain SlangPy if the toolkit becomes the obstacle. The C++ RHI spike remains the browser-preserving alternative. Neither comparison has been executed yet.

## World structure and hydrology

The old `docs/TERRAIN.md:5` proposes an indefinitely explorable world composed of landmasses no larger than 128 km square. It distinguishes offline whole-landmass climate/hydrology/erosion from runtime detail generation. Carry this forward as a provisional design: begin with one complete landmass, then validate placement and streaming of additional landmasses. The finite-versus-indefinite preference remains open; finite simulation domains do not require a finite overall world.

Separate four scales. A **hydrological domain** includes connected drainage and its outlets. A **generation tile** is a unit of detail synthesis. A **storage page** is a fetch/decode/cache unit. A **render patch** is a unit of visible geometry. Forcing them all into one chunk size causes unnecessary work and incorrect boundary assumptions.

Generate coarse relief, rainfall and material resistance, then solve drainage and erosion over the domain. Store heights alongside river graphs/profiles, outlets, lake levels, flow and material descriptors. Stream a multiresolution bake and refine it under these constraints. Noise should add local detail without damming rivers or changing their prescribed course. Start with uniform rainfall; richer climate can follow once the drainage behavior is visible and measurable.

Decide how sinks behave: some should be corrected to create drainage, others should become lakes with explicit spill levels. Local hydraulic solvers need boundary inflow/outflow and sediment conditions. A small halo alone cannot recreate unknown upstream watersheds. Runtime water animation and player-triggered terrain changes are separate problems from geological generation and need later explicit contracts.

Author research supports hydrology-organized terrain and large-scale uplift/erosion as useful approaches, but does not prove an arbitrary infinite, independently generated tile system: [Génevaux et al., 2013](https://doi.org/10.1145/2461912.2461996), [Cordonnier et al., 2016](https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.12820). The proposed offline-domain/runtime-detail separation is our engineering recommendation.

## Continuity is several contracts

| Boundary | Required property | Proof to implement |
| --- | --- | --- |
| Same-resolution samples | One integer global lattice, deterministic sample ownership, agreed units and reconstruction | Generate adjacent tiles independently and in different orders; compare all edges and corners, including negative addresses. |
| Coarse/fine geometry | Defined parent reconstruction and matching topology at mixed detail levels | Exercise every transition orientation/corner and view it in wireframe and silhouette. A skirt alone does not prove continuity. |
| Surface shading | Shared derivative convention, filtered height/normal data and adequate halo | Render grazing light across edges and detail transitions; test numerical derivative agreement. |
| Stored pages | Border-safe quantization, filtering, compression and mip construction | Encode/decode neighbors independently and compare reconstructed shared boundaries. |
| Hydrological tiles | Shared river elevation, discharge/outlet and lake constraints | Follow a river across storage and generation boundaries without steps, new dams or duplicated flow. |
| Streaming transitions | Coarse fallback remains resident until finer children are ready for atomic replacement | Delay, reorder, fail and cancel requests; teleport during pending uploads; keep the world covered. |
| Distant coordinates | Integer global addresses; double or integer-plus-local CPU positions; camera-relative GPU floats | Repeat fixtures far from origin and rebase the camera without moving the apparent terrain. |

For the first surface renderer, a balanced quadtree with regular patches, explicit edge stitching and parent-height morphing is a reasonable starting candidate. A quadtree recursively divides the surface into four regions; balancing limits neighboring resolution differences. Geometry clipmaps, which cache nested grids around the viewer, are an alternative worth considering if regular update cost wins. Choose one during the terrain prototype rather than building both engines. [The original GPU clipmap description](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry) explains its regular grids and transition regions; neither approach resolves hydrological continuity by itself.

Start with surface heightfields. Later caves and local digging can use sparse volumetric regions, but their openings need consistent distant representations and collision. Defer those implementations explicitly rather than pretending a heightfield can represent overhangs.

## Scale and budgets

The old 1 km runtime tile proposal needs a memory budget before adoption. One 1,024 m tile with 1 m samples has 1,025 squared vertices: a single float height channel costs 4,202,500 bytes, about 4 MiB. Eight such channels cost about 32 MiB before halos, working buffers, meshes, mip levels, upload copies and cache overlap. A generation tile can produce smaller independently resident render/storage pages. Its full working set need not remain in memory.

A 128,000 m square sampled every 32 m has 4,001 squared samples: approximately 61 MiB per float channel, or 489 MiB for eight channels before scratch and double buffering. At 1 m it has over 16 billion samples and requires approximately 61 GiB for one float channel alone. Resolution, sparse graphs and offline preprocessing matter much more than choosing Python versus C++ for orchestration.

These are storage arithmetic examples, not recommended budgets or benchmarks. Use explicit metres in manifests; do not silently equate 128,000 m with 131,072 m. Power-of-two page spacing can simplify hierarchy without changing the landmass's declared physical extent.

CPU and GPU caches need byte budgets, eviction, upload staging limits and safe retirement of resources still in flight. Track outstanding HTTP/decode/generation work as well as visible patches. A ten-minute traversal and repeated teleports must reach a stable memory ceiling, including resources waiting for GPU completion.

## Terrain-first sequence

1. Prove shared Slang RHI rendering and compute in native DX12 and browser WebGPU, including asynchronous startup, uploads/readback and compatible shader layouts. Record limitations and select the framework from evidence.
2. Define global coordinates, versioned page formats, boundary ownership and resource lifetime. Show a large synthetic surface with multiple detail levels, camera-relative coordinates and explicit coarse fallback. Keep rendering simple and visible.
3. Add genuinely bounded asynchronous storage, decode, generation and upload queues. Establish real eviction, cancellation, request epochs, fallback under failure and a sustained travel benchmark.
4. Build the SlangPy hydrology laboratory on a representative smaller landmass, then scale toward the 128 km target. Inspect rainfall, outlets, accumulated flow, lakes, erosion, sediment and mass-balance diagnostics. Start this work alongside surface experiments once formats are defined; do not defer it behind graphics polish.
5. Connect a baked drainage-aware landmass to the live streamer and refine detail across page boundaries. Prove complete source-to-runtime river continuity, collision/height query agreement and repeatable artifact loading on both targets.
6. Then extend biomes, materials, water appearance, local volumes, editing, factories and multiplayer. Server authority should consume versioned terrain artifacts rather than require bit-identical floating-point erosion on every client GPU.

Initial scale and performance targets should be explicit proposals: a 16 km diagnostic landmass, then a 128 km stress case; fixed device, viewport, travel speed and resource budgets. Establish timing thresholds from the portability spike. Do not promise 60 FPS on unspecified browser hardware.

## Remaining decisions

The proposed first slice uses one finite hydrological domain while preserving space for multiple landmasses. Caves/editing are future extensions. The user has selected Slang RHI with native and browser targets; abandoning the browser would be a new product decision, not an automatic response to an RHI problem. The detailed bootstrap is in `docs/plans/terrain-lab-bootstrap.md`.

## Sources for framework claims

[Slang RHI repository and maturity statement](https://github.com/shader-slang/slang-rhi), [SlangPy supported native backends and workflow](https://slangpy.shader-slang.org/en/latest/), [Slang RHI browser implementation PR](https://github.com/shader-slang/slang-rhi/pull/609). Pinned source links for build and runtime limitations are recorded in the spike plan.
