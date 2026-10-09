# Build an interactive terrain generation service and stream its results

This ExecPlan follows [PLANS.md](../../PLANS.md). Status: **implemented; verification limits recorded below**. The user authorized implementation
on 2026-10-09 after committing and pushing all existing work. Server rental is not required or authorized.
Keep the living sections current during implementation. Complete each stage with a demonstrable result
before starting the next; do not introduce erosion to compensate for an unfinished delivery/rendering path.

## Purpose / Big Picture

Run a Python service that generates a small, flat island inside a curved outline derived from a bounded
Voronoi cell. Inspect its terrain in the existing native D3D12 and browser WebGPU laboratory. Change
parameters through ImGui, regenerate, and see a consistent replacement without freezing the viewer.
Then add CPU FastScape erosion, publish intermediate results and inspect how the island evolves.

Live generation and saved content expose the same read-only terrain contract. The game consumes terrain
data, not a particular simulation implementation. Generation controls are an additional laboratory API.
Python and Fortran belong to the separate generator environment, not the application or browser build.

The initial deliverable is one selected island per service session, with surrounding seabed. Deterministic
world addressing must support distant and negative regions, but discovering and simulating an entire infinite
archipelago is deferred. The long-term approximately 128 km island envelope informs addressing; it is not
the starting simulation size or a performance promise.

The user clarified that sufficiently fine detail will eventually be generated locally in real time using
the baked terrain as input. These stages establish the baked foundation and its delivery. They do not
require the service to simulate or store every fine feature visible near the player.

## Progress

- [x] (2026-10-08 21:21Z) Read planning conventions, current terrain contracts, source ownership, addressing,
  generator/worker interfaces, renderer retirement, HTTP asset loading and browser hosting.
- [x] (2026-10-08 21:21Z) Checked upstream FastScape capabilities and native/browser HTTP documentation.
- [x] (2026-10-08 21:21Z) Drafted four implementation stages, data/publication contracts and acceptance tests.
- [x] (2026-10-09 08:00Z) Revised client acquisition per user clarification: terrain build jobs always
  request through a simple asynchronous disk-first cache, with a per-request skip-cache option.
- [x] (2026-10-09) Specified richer 1024-cell terrain tiles and one conservative full-source AABB per
  requested footprint, following the user's clarification that separate child bounds are unnecessary.
- [x] (2026-10-09) Corrected tile selection: each node requests its own footprint for shading detail until
  source resolution is reached; finer descendants inherit that terminal-resolution input and GPU textures.
- [x] (2026-10-09) Adopted the user's selected 256-cell default: 257 edge-inclusive samples plus a one-sample
  apron on each side, giving 259x259 stored samples. Updated sizes, limits, examples and acceptance tests.
- [x] (2026-10-09) Stage 1: Python service, bounded Voronoi island, immutable content and contract fixtures.
  56 pytest cases pass, including real HTTP, spawned-process lifecycle, source bounds and corruption checks.
  Inspected `artifacts/terrain-service/data/demo/revisions/a292ddd152bd4500a8437ce1c4240ede/diagnostic.png`.
- [x] (2026-10-09) Committed and pushed the existing streaming implementation and this plan as `40fbf5d`.
- [x] (2026-10-09) Stage 2: Native/browser HTTP terrain streaming and a saved-content playback proof.
  Decoder, source meshing, native HTTP/persistent-cache and portable shading paths are implemented.
  Native terrain-fast passes 60 cases / 103976 assertions. Real HTTP tests pass 2 cases / 19908 assertions,
  including a regression proving zero acquisitions below the terminal source depth. Browser cold loading,
  offline IndexedDB replay after reload, SkipCache with no terrain-storage calls, resize and aerial capture pass.
  Native presentation resize/minimize/restore/close and intermediate-sample shading tests pass.
  Final native regression passes all 9 CTest groups (68.06 s), Python passes 56 cases (7.20 s), and the
  reviewed native sea-reference capture passes. Final browser checks and sea capture pass too.
  Committed/pushed as `8445084`; performance/coverage limitations are recorded below.
- [x] (2026-10-09) Stage 3: Editor parameters, regeneration and coherent revision replacement.
  Native real-service regeneration passes 36 assertions including unchanged global camera coordinates.
  The real HTTP suite passes 3 cases / 42138 assertions, covering cancellation, queued revisions, failure/retry
  and minimal-budget refusal. Terrain-fast passes 62 cases / 104035 assertions; all 9 native CTest groups pass
  in 59.54 s. Browser recording proves a held HTTP candidate preserves old coverage, release adopts the new
  revision, and a later HTTP 503 candidate retains the old displayed revision. Inspected docked editor and
  regeneration screenshots under `artifacts/terrain-service/stage-3`. The displayed-leaf counter is also
  checked in the final browser regression, proving the preserved old cut contains actual visible leaves.
- [x] (2026-10-09) Stage 4: FastScape worker, progressive previews and numerical experiments delivered.
  Optional pinned Linux worker, Play/Pause/Step, source-validity/material export, full-simulation bounds
  for reduced previews and bounded publication acknowledgements are implemented. Linux passes 64 Python
  cases; Windows passes 57 with the optional Fortran suite skipped. Coverage including spawned workers
  and collector is 94.7% lines / 88.3% branches, with no project exclusions.
  Final native Release regression passes all nine CTest groups (228.83 s); real HTTP passes 3 cases /
  45016 assertions; fresh-island native erosion passes 60 assertions. Browser erosion renders three paused
  states at 0/1000/2000 years with no errors; final texture/checkerboard, PBR and editor-regeneration
  browser checks pass, including held/failed replacements, visible old coverage, reload and five C++ threads.
  The final owned-service ten-minute run passes 452788 assertions across 90555 frames and five regenerations;
  tracked terrain allocations drain to zero. This is stability evidence, not a frame-rate benchmark.
  Native Release and browser builds pass. Remaining coverage/performance/numerical limits are recorded below.

## Surprises & Discoveries

The starting checkout contained the sparse streaming implementation, including a coordinator thread
and two C++ mesh workers on both hosts. That work and the archived previous plan are now preserved in
`40fbf5d` on `origin/main` before generation-service implementation.

The terrain mesher currently evaluates procedural noise internally. Its conservative height bounds,
root admission and `teleportToSurface` also assume that noise generator. Replacing just its vertex samples
would leave incorrect culling and camera placement. All four sites need an explicit service-data path.

Browser assets already use asynchronous Emscripten Fetch. Native assets use local file reads, not HTTP.
The browser server already supplies COOP/COEP headers needed for shared-memory C++ threads.

`RenderSnapshot::revision` is a local display-publication serial, and `RequestId::epoch` rejects obsolete
asynchronous work. Neither currently identifies immutable server content. Keep those meanings separate.

The Fortran FastScape library supplies sediment/deposition features that the newer C++ library's documented
built-in eroders do not yet match. Use the Python bindings to the Fortran library for the first scientific
backend. Its module-global state requires exclusive worker-process ownership of an active simulation.
The pinned v2.8.4 binding is now built with Python 3.11.17, NumPy 1.26.4 and GNU Fortran 12.4.0 in WSL Ubuntu.
Small numerical fixtures and 257/513/1025/2049-square scaling experiments ran; no full-island estimate is claimed.

The initial tile-selection proposal incorrectly sized data requests only for mesh vertices. The user
clarified that intermediate samples are needed for shading. This changes both request selection and resource
lifetime: textures are rendered node content, not temporary meshing inputs that can all be discarded after a job.

## Decision Log

2026-10-09: Full regression selected Intel Iris Xe on a later run, exposing a pre-existing one-ULP half-float
mipmap error for constant fields. Area reduction now averages differences from a reference sample, preserving
constants exactly instead of dividing two rounded totals. The existing exact constant-field assertions remain
unchanged. Cross-material tests also rejected using RHI's uninitialized m_uid for the descriptor cache; its final
key uses a retained pointer, version and layout. All nine final native CTest groups pass, including the
unchanged exact-value mip assertions and parameter-block snapshot regressions. Adapter names are recorded per run rather than assumed from earlier NVIDIA results.

2026-10-09: Final native erosion replay exposed D3D12 sampler-heap exhaustion in an aerial view. Each terrain
material had an identical sampler in its persistent material block, exceeding the hardware heap's 2048 entries.
Terrain now samples with the existing linear-clamp sampler in the shared scene block, always at mip zero.
A GPU regression encodes 2200 distinct terrain materials in one pass. It also exposed that the pinned D3D12
BindingCache is empty: even shared scene tables were reallocated per draw. A generated build-only adaptation
caches leaf parameter-block descriptors by shader-object identity, version and specialized layout within one
command buffer. It retains snapshot identities and replays resource-state requirements, and resets with the
GPU-completed command buffer. Nested/root-descriptor blocks keep the upstream path. No upstream allocator or
capacity limit was changed, and the pinned checkout remains untouched. The upstream finalize method does not
set its finalized flag, so versioned keys are necessary rather than relying on that flag.

2026-10-09: Raise the whole graphics CTest allowance from 120 to 300 seconds after a direct timed run passed
all 36 cases / 3125 assertions with 138.59 seconds inside tests, plus device setup. The suite recreates validated
devices and shader pipelines repeatedly; individual asynchronous deadlines and correctness assertions stay
unchanged. Earlier CTest timeouts are retained as failed attempts, not attributed solely to concurrent builds.
The real HTTP suite rerun passes all 3 cases / 45016 assertions without changing its deadlines.

2026-10-09: Use FastScape's single-flow solver. The required paused/exported versus uninterrupted comparison
exposed heap-dependent results in v2.8.4's multiple-flow path. Valgrind traced uninitialized lake-water scratch
in StreamPowerLaw. Single-flow makes the comparison exact without patching the external solver. Residual
warnings in the upstream unused marine compaction calculation are documented; no clean memory audit is claimed.
Marine diffusion defaults to zero after the nonzero experiment showed large coastal sediment imbalance.
See [erosion notes](../terrain-erosion.md) for equations, conventions, limitations and retained failed experiments.

2026-10-09: Live WSL data uses the Linux filesystem. A Windows-mounted output directory returned PermissionError
on atomic directory rename; previous published content stayed intact. Windows and browser viewers successfully
use the Linux-local service over HTTP. The setup script records actual dependency/compiler provenance.

2026-10-09: The short 10,000-year, 257-square, 32 m experiment with zero marine diffusion converged from
100/50/25-year steps: successive RMS differences 0.6571/0.3273 m for 121 m initial relief. The earlier
100,000-year experiment at 1000/500/250 years did not pass convergence; those results remain recorded.
Initial scaling reached 2049-square samples at about 1.83 GiB process peak RSS, 33.18 seconds for five steps
and 6.51 seconds for export. It grows seabed extent, not island detail, and ran alongside development work;
these are preliminary costs, not a full-island performance forecast.

2026-10-09: Keep one combined client CPU residency budget rather than a separate source-data allocator.
Service terrain receives 256 MiB of additional CPU headroom (512 MiB total); noise remains 256 MiB and GPU
terrain remains 256 MiB. Real integration saturated the old CPU budget at about 226 MiB of retained data,
leaving insufficient room for a reserved 32 MiB sibling group and stopping at depth 4. Full-source empty
certificates now discard their unnecessary input arrays immediately. A complete 16 m laboratory workload
measured 419.0 MiB retained CPU, 189.8 MiB GPU, including 176.6 MiB decoded source and 105.9 MiB texture
images on each host side. The original 128 MiB source allowance was insufficient; the real integration test
now verifies convergence under the revised 512/256 MiB bounds. Retained decoded inputs, texture CPU
images and GPU textures are deduplicated by source allocation across inherited descendants and retirement.
Native/browser transport uses two dedicated I/O threads waiting on asynchronous host completions; the
coordinator, mesh workers and render thread never wait on network/storage. Browser fetch is proxied to
the main runtime and uses streaming size checks, AbortController and cache:'no-store' for bypass.

2026-10-09: Face balancing can subdivide certified-empty nodes even though geometric demand cannot.
Propagate the parent's empty certificate in BuildRequest instead of reacquiring a source tile for these
children. A new actual-acquisition-depth assertion caught redundant requests hidden by address clamping;
the real HTTP integration now proves no request is issued below the source cutoff.

2026-10-09: Revision replacement always coarsens to retained roots first. This intentionally simplifies
ownership and headroom checks rather than maintaining two alternative handoff paths. The coordinator waits
for explicit frame-boundary adoption and retirement acknowledgements, then resets the existing controller
into the candidate epoch while pinning old root payloads. Cancellation restores those still-charged roots
without HTTP or a second controller. Later publications occupy one pending slot until adoption completes.

2026-10-09: Release timing evidence is in `artifacts/terrain-service/stage-2/profile/summary.json` and per-frame
CSVs. i7-12700H / RTX 3050 Ti Laptop GPU, D3D12, validation off, scopes disabled, 1440x1200 offscreen with
an explicit GPU wait, loopback saved content, 513x513 source at 32 m. Cold/warm/bypass reached 16 m in
14.75/11.60/27.33 seconds; total-run update p95 was 1.21/1.27/1.18 ms. Warm loading alone had 3.00 ms p95,
so the loading target is not uniformly met. Startup shader/layout/pipeline preparation still causes stalls
(75-94 ms terrain update maxima, 676-851 ms full-frame maxima); this is not steady-state frame time.
Settled cold/warm frame p95 was 9.62/9.35 ms. This measurement preceded the empty-certificate request fix
and opaque sea-reference adjustment; preserve it as a baseline, not a final performance claim.
OpenCppCoverage is not installed, so the proposed new C++ line-coverage percentage remains unmeasured.

2026-10-08: Keep generation in a separate Python service, as requested. Use one FastAPI/Uvicorn HTTP process
and one separately spawned generation process initially. Do not add a distributed task queue, database,
plugin registry or multiple HTTP worker processes with independent job state.

2026-10-09: Serve **terrain data tiles**, not renderer-specific meshes or full 3D density volumes. The
demonstrated producer is a heightfield, and C++ already owns working Transvoxel extraction and all six
transition faces. This format intentionally does not promise caves; a later volumetric representation
would be a new explicit format, not speculative fields in this one.

2026-10-08: Retain the existing sparse octree, parent retention, sibling-group scheduling, face balancing,
camera-relative coordinates and GPU retirement. Terrain build jobs acquire their inputs through the cache
described below, then run extraction on the existing mesh workers. Do not block threads on disk or network I/O.

2026-10-08: Make published content immutable and pin a content revision for every request. Reuse the same
decoder, mesher and URLs for generated and saved data. Show job completion and viewer adoption separately.

2026-10-08: Begin with a geometrically flat plateau and curved coastal slope. No fractal inland noise,
mountain synthesis, climate simulation, vegetation or advanced water renderer is needed to prove stages 1-3.
Use a simple sea-level visual reference so the coastline can be judged.

2026-10-08: Preserve the boundary between source resolution and local render detail. Below the source grid
spacing, stages 1-4 interpolate the served field; they do not claim new geological information. Future
real-time refinement may add detail using height, drainage, material or ecological inputs. Its algorithm,
activation level and ecological channels are deferred. Surface, bedrock, water and material data are
part of the current delivery contract, as clarified on 2026-10-09.

2026-10-08: Revision replacement may temporarily use coarse terrain, but must not mix old and new surfaces.
Reuse the existing coordinator and workers; do not create a second streaming engine or additional browser
pthread pool merely to replace a revision. The exact handoff is specified below and must be tested first.

2026-10-09: Per explicit user clarification, use a naive asynchronous client terrain cache: local persistent
hit returns the data; a miss fetches HTTP and saves the result. Every build that acquires new source data
calls this same service, including with `skipCache=true`. Descendants below the source cutoff inherit already
loaded data, as clarified below; bypass does not force redundant descendant requests. Remove the previously proposed resident
tile-cache/coalescing machinery from this milestone. No prefetcher, LRU, revalidation engine or cache policy
framework is required. Duplicate concurrent misses are acceptable initially.

2026-10-09: Initially proposed 1024 horizontal intervals per data tile; superseded by the 256 default below.
Include surface elevation, bedrock elevation, water depth and a material classification; derive normals
locally. Return one conservative terrain AABB for the requested footprint, computed from the complete
published source rather than the returned downsample. Separate per-child AABBs are not required.

2026-10-09: Supersede the mesh-spacing-based data-depth offset. Each node above the source-resolution cutoff
requests a full texture over its own X/Z footprint. The first node reaching source resolution also requests
its own data; finer descendants read a subregion through their parent's retained source reference. The extra
samples between mesh vertices drive shading.

2026-10-09: The user selected 256 data intervals per axis after comparing shading density and quadratic
payload costs. Use 32 mesh intervals (ratio 8), with 257 edge-inclusive samples plus a one-sample apron on
each side, giving 259x259 stored samples. No runtime texture-resolution policy is required. Apron means the
same sampling halo used below; GPU filtering/mip construction must preserve correct borders at every level.

## Outcomes & Retrospective

The four implementation stages are delivered: a deterministic Voronoi island service, immutable richer
terrain tiles, native/browser streaming with the simple persistent cache, editor regeneration with coherent
revision replacement, and progressive FastScape erosion in an optional isolated Linux worker. Checkpoint
`40fbf5d` preserved the previous work before implementation. Current setup and experiment commands are in
[the service README](../../services/terrain/README.md) and [erosion notes](../terrain-erosion.md).

The 256-cell format provides 259x259 samples including shared edges and apron. Nodes own their shading
data until reaching source resolution; finer nodes inherit it without more requests. Full-source bounds
preserve features absent from coarse samples. Measured working sets required 512 MiB combined client CPU
residency; GPU residency stays capped at 256 MiB. Progressive exports use the same immutable content path.

Remaining verification limits are explicit: new C++ line coverage is unmeasured because the coverage tool
is unavailable; the browser has rendering/format/visual checks but no dedicated intermediate-sample pixel
oracle matching the native fixture. The original 2 ms loading-update target was missed in one baseline,
and startup shader preparation still causes stalls. The final descriptor-cache fix has correctness tests,
not a new isolated performance benchmark. These gaps are not reported as passing acceptance measurements.

FastScape single-flow fixes reproducibility of paused exports without patching the external solver.
Marine diffusion remains experimental and defaults to zero. Small timestep-refinement fixtures converge,
but default long-run parameters need calibration. Metre-resolution 128 km islands, climate/growth and
local procedural refinement remain later research. Failed numerical experiments are retained alongside
successful ones; neither plausible screenshots nor small-grid timings establish large-world quality/cost.

## Context and Orientation

Relevant existing files, relative to `C:/dev/ofg`:

| Location | Current responsibility / intended integration |
| --- | --- |
| `src/terrain/terrain-address.*` | Signed 64-bit root IDs, local octree addresses and root-relative double coordinates. Preserve. |
| `src/terrain/terrain-stream.*` | GPU-independent demand, readiness, balanced display cuts, request IDs and accounting. Preserve core rules. |
| `src/terrain/terrain-streaming-service.*` | Single coordinator, root selection, build-job dispatch and mailboxes. Add revision handoff; keep cache policy out of the octree. |
| `src/terrain/terrain-data-cache.*` | Asynchronous local-storage lookup, HTTP on miss, persistence and explicit skip-cache flag. |
| `src/terrain/terrain-workers.*` | Terrain build jobs request their input through the cache, yield while acquiring, then use two persistent C++ mesh workers for extraction. |
| `src/terrain/terrain-generator.*` | Noise field, conservative bounds and Transvoxel mesher. Separate field evaluation from extraction. |
| `src/lab/terrain-laboratory.*` | Application-thread uploads, draws, camera and real GPU-completion retirement. |
| `src/ui/terrain-panel.cpp` | Existing terrain diagnostics and fault controls. Add generator controls here. |
| `src/resources/asset-read.h` | Existing browser fetch example; native branch is a file reader. Do not turn this into a generic networking framework. |
| `tools/serve-web.mjs` | Allowlisted browser build hosting with cross-origin isolation. Add a narrowly scoped local API proxy. |
| `tests/terrain-*.cpp`, `tests/graphics-test.cpp` | Existing streaming, meshing, worker and renderer tests. Extend at changed boundaries. |
| `docs/terrain-streaming.md`, `DEVELOPING.md` | Current contracts and actual workflows; update alongside implementation. |
| `docs/archived/sparse-terrain-streaming.md` | Previous implementation and validation evidence, not evidence for this work. |

Current laboratory roots are 131072 m wide, with depth 13 reaching 16 m nodes. Each node has 32 mesh
intervals; node width is not simulation resolution. Detailed demand extends 4096 m and the far plane is
16000 m. Noise terrain retains 256 MiB CPU/GPU budgets; service terrain uses 512 MiB CPU and 256 MiB GPU,
including decoded inputs and shading textures. The decision log records the measured reason for this increase.

A **content revision** is one immutable terrain field. A **cut** is the complete non-overlapping set of
octree leaves currently representing admitted roots. A **terrain tile** covers a dyadic X/Z footprint,
including shared edge samples and a sampling halo. Until source resolution is reached, its footprint matches
the requesting node's X/Z footprint. Finer descendants reuse a subregion of the terminal source tile.
An **empty certificate** proves
that the entire node volume has no surface, including at finer detail; an empty coarse mesh alone does not.

## Contract and Quality Baseline

### Coordinates and the first island

Use a deterministic jittered square lattice of Voronoi seeds in the horizontal X/Z plane. An island is
identified by world seed plus signed lattice indices, independent of request order. A seed's displacement
has a bounded Euclidean magnitude `j`; grid spacing is `s`. Enforce `0 <= j < s/2` initially to avoid
coincident adjacent seeds. The covering radius satisfies `R <= s/sqrt(2) + j`, so cell diameter is at most
`2R`. Choose parameters with this bound at most 131072 m when using the maximum-island preset.

Calculate each cell by clipping half-planes in coordinates relative to its seed. Enumerate a provably
sufficient finite neighbourhood, not an arbitrary 3x3 window. One conservative construction starts from
the square `[-R,R]^2` and includes every lattice index in a square of radius
`ceil((2*sqrt(2)*R + 2*j)/s)` around the selected index. Any excluded seed is too distant to cut that
initial square. Verify this against an independently larger-window construction in tests. Hash integer
indices before converting only their bounded differences to doubles; never form enormous float world
positions. Fix the hash, seed-to-jitter mapping and tie ordering in the format/generator version.

Inset the convex cell by the ocean clearance parameter using inward-offset half-planes. Reject parameters
that erase the interior. Form a closed curved contour inside that inset polygon using one cubic segment
per corner: endpoints are the midpoints of its two incident edges, and control points lie on those edges
toward the corner. A shared handle length on each edge gives matching tangents between successive segments.
All control points lie inside the convex region. The corner construction also permits a direct simplicity
check; do not assume containment alone prevents self-intersection. Vary shared handle lengths with a
bounded seeded signal for gentle irregularity. This is equivalent to a Hermite spline; store one representation.
Additional inward bays/noise are deferred until this basic contour passes containment and seam tests.

Flatten the contour adaptively with a documented geometric error no greater than one eighth of the
finest published source-raster spacing. Classify inside/outside and evaluate signed distance against this
same closed contour. Set the interior plateau to `plateauHeight > seaLevel`; blend monotonically through
the coastal band to `seabedHeight < seaLevel`. Ensure the chosen contour is the sea-level crossing, not
an arbitrary edge of the blend. Beyond the finite source rectangle the field is constant seabed, and a
seabed margin separates the coast from that rectangle's boundary. No cliff discontinuity at the raster edge.

Initial suggested fixture: seed spacing 4096 m, maximum jitter 512 m, plateau 40 m, sea level 0 m,
seabed -80 m, coastal blend width 256 m and a 16384 m square source domain with 513x513 samples
(32 m source spacing). These are starting experiment values, not calibrated physical parameters. Centre
the source domain near the selected seed, snapping its origin to the published source lattice. Preserve the continuous contour
and parameters as metadata for inspection; the published raster is the canonical sampled heightfield.

### Read-only content API

Implement `/v1` with JSON metadata and a bounded binary terrain tile. Decimal 64-bit world seeds and root or
region indices travel as strings in JSON, avoiding JavaScript's integer precision limit. Native C++ validates
conversions and checked address arithmetic. Heights are local elevations in metres; X/Z and Y semantics
match the existing application, which uses Y up.

| Request | Response / rule |
| --- | --- |
| `GET /v1/health` | Service version and enabled generator capabilities; remains responsive during simulation. |
| `GET /v1/islands/{island}/manifest` | Latest fully published content revision and its immutable manifest URL; revalidate rather than cache indefinitely. |
| `GET /v1/islands/{island}/revisions/{revision}/manifest` | Immutable schema/encoding version, identity, placement, source dimensions/spacing, bounds, seabed, sea level, content hash, source parameters and tile URL template. |
| `GET /v1/islands/{island}/revisions/{revision}/terrain/{rootX}/{rootZ}/{dataDepth}/{tileX}/{tileZ}.bin` | Immutable terrain tile; no generation side effect and no implicit request for latest. |

Root width and 256 data intervals are part of v1's manifest contract; mesh resolution is a client choice.
Require data intervals to be an integer multiple of mesh intervals while each node has its own texture.
The current 256/32 choice supplies 8 data intervals between consecutive mesh-grid sample positions.
The service must reject an unsupported data depth or out-of-range tile index. Missing revision is an error,
never substitution with latest.
A valid query outside the island still returns seabed data; a 404 is never interpreted as empty terrain.
Use 400/422 for invalid requests, 404 for unknown content, 409 for conflicting control state, and explicit
5xx errors for server failures. Do not add compressed payloads or range requests to the first format.

A tile represents **256x256 horizontal cells**. To include both shared edges it has 257x257 vertex
samples, plus one halo/apron sample on each side: 259x259 samples per channel on the wire, X varying fastest.
This defines the chosen resolution without losing boundary samples. At root width 131072 m
the spacing is exactly 512 m; each data depth halves both width and spacing. A literal 128000 m region
would instead have 500 m spacing. Source simulation spacing, transfer spacing and mesh spacing are distinct.
Resampling the initial 32 m source raster more densely does not generate additional geological detail.
Use node-local X/Z to address the intermediate samples during shading; do not reduce the tile to vertex
attributes and discard its extra detail. Define texture coordinates at texel centres, explicitly accounting
for shared edges and halo. A mesh-grid position `i` maps to data sample `i * (dataIntervals / meshIntervals)`
before the halo offset. Extracted surface vertices between grid positions sample continuously in X/Z.

Use concrete, planar little-endian channel arrays with this initial meaning:

| Channel | Encoding and meaning |
| --- | --- |
| Surface elevation | float32 metres, always valid; the ground surface used for terrain extraction. |
| Bedrock elevation | float32 metres; surface minus bedrock gives loose sediment thickness, not a soil fertility model. Must not exceed surface where valid. |
| Water depth | float32 metres above the ground, nonnegative where valid; zero means known dry. Water surface is ground plus depth. Drainage area/discharge is not water depth. |
| Material ID | uint16 indexing the manifest's small material palette; categorical, never linearly interpolate IDs. Sediment thickness expresses the dirt/bedrock layering separately. |
| Validity | uint8 bits indicating known bedrock, water and material values. Unknown is distinct from dry water or bare bedrock; unused slots have canonical zero values. |

Stage 1 exports a bare-rock fixture (bedrock equals surface), a documented rock material and static sea
depth `max(seaLevel - surface, 0)`. It does not claim simulated rivers or soil. Normals are initially
derived from the height field and halo; no normal channel is required. Stage 4 exports only physical
quantities actually supplied or explicitly derived by its model, marking unavailable values unknown.

The header has fixed magic/version, explicit header/payload sizes, revision and tile identity, interval/halo
counts, channel layout and one float64 conservative AABB for the entire core footprint. X/Z coordinates
are root-relative and Y uses the manifest's elevation reference. Specify byte offsets in
`docs/terrain-content.md` in stage 1. Parse fields explicitly in C++; never cast untrusted bytes to a
compiler-packed struct. The arrays above occupy 1,006,215 bytes (about 0.96 MiB) per tile. Cap a complete v1
response at 2 MiB; unknown versions, extra/truncated bytes, invalid channel values and identity mismatch fail.

Sample every level from one canonical bilinear interpolation of the revision's finest raster, not from
independently smoothed LOD rasters. Compute coincident sample locations through a reduced dyadic integer
address before evaluating and rounding to float32. Shared fine/coarse lattice positions and same-level
halo samples must agree bit-for-bit. This preserves the existing 2:1 transition requirement. It may alias
small features at a coarse LOD; it must not erase them through a false empty certificate.
Use bilinear interpolation for continuous attributes only when all contributing samples are valid;
otherwise mark that attribute unknown. Material IDs use nearest-neighbour sampling with a fixed tie rule.
Apply the same rules on export and in client sampling, and validate sediment/depth constraints after rounding.

The AABB's X/Z extent is the requested footprint; its Y extent encloses the **complete source terrain
within that footprint**, including features missed by every returned coarse sample. Include all fine source
samples whose bilinear cells intersect the footprint, plus the constant seabed extension. Build a min/max
hierarchy once per publication so serving coarse bounds does not scan the full island. Round bounds outward
to contain exported float32 values. This is a bound on the published source field, not a claim about unknown
geology below its resolution. If future procedural displacement adds detail, its envelope must expand it.

One AABB is sufficient: descendants inherit it conservatively until a finer requested tile provides tighter
bounds. It may cause unnecessary refinement, but cannot justify removing a child that might contain terrain.
Only nodes strictly above/below the certified height range can be declared surface-empty; touching keeps
them eligible. An empty coarse mesh does not imply an empty subtree. Missing/malformed bounds are errors,
never empty certificates. Retain existing logical eight-child groups, allowing certified-empty members to
complete without extraction. No per-child-bound protocol or neighbour requests are required.

The manifest supplies whole-field bounds for root admission. These tile bounds certify the ground surface;
bedrock and water are attributes initially. A future water/vegetation renderer needs its own content bounds
before ground-surface emptiness can prune those renderables.

### Per-node textures and inheritance at source resolution

Let `R` be the data interval count and `s` the published source spacing. A node of width `W` requests its
own footprint while its parent has not yet acquired source-resolution data. Its returned spacing is `W/R`.
The first node with `W/R <= s` becomes the terminal source owner; descendants make no further data requests
and inherit the parent's source reference with a smaller X/Z subregion. Thus data depth equals node depth
until the cutoff, then remains at that ancestor's depth. Mesh resolution is not the request cutoff criterion.

For a 1 m source, R=256 and a 131072 m root:

| Node width | Data used | Data spacing |
| --- | --- | --- |
| 131072 m | Own 256-interval tile | 512 m |
| 512 m | Own 256-interval tile | 2 m |
| 256 m | Own terminal tile | 1 m |
| 128 m | Half-width subregion of terminal ancestor | 1 m |
| 16 m | 16 m subregion of the same terminal ancestor | 1 m |

At 16 m node width the current mesh has 0.5 m intervals, so its samples interpolate the retained 1 m field.
Finer mesh geometry does not imply a new texture request or new source information. The 32 m initial source
reaches this cutoff at an 8192 m node, earlier than the 1 m example.

For the initial format, publish source rasters on a world-aligned dyadic lattice compatible with root width
and R. Record the source origin, spacing and terminal data depth in the manifest; reject inconsistent values.
The terminal tile then reproduces source samples, including at boundaries, without a shifted-grid resampling
loss. Place the island independently inside that lattice. If a later solver uses an incompatible grid, export
once to this canonical lattice and compute bounds from that published representation. Supporting arbitrary
source lattices without resampling is outside v1. Test terminal interpolation against the canonical source.

Each node that requests data owns its immutable decoded input and the GPU textures needed to shade it.
At and below the cutoff, children hold owning references to the terminal data/texture allocation through
their parent's source reference; offsets are derived from integer node addresses, not repeatedly resampled
child images. Do not share decoded tiles between separate coarse nodes as an initial optimization. Identical
horizontal requests from vertical nodes may hit the disk cache, without requiring shared-request machinery.
Keep decoded data needed by future descendant builds while its owner subtree is resident. Retained parents
must also keep textures required for immediate redisplay. Release allocations when their last node/build
owner releases them; GPU references additionally follow the existing completion-based retirement rules.
Charge shared terminal allocations once. This explicit node ownership adds no LRU policy to the cache.

The client constructs density as `height(x,z) - y` and meshes locally, retaining all six transition ranges.
Refactor the mesher to consume concrete sampled-field data and bounds; keep the noise fixture as a concrete
test input, not a new virtual provider framework. Halo data supports normals near edges. Define normals
from the tile interpolation without calling the old noise function; LOD-dependent normal approximation
is allowed initially, but topology cracks and nonfinite/incorrectly oriented normals are not. Derive shading
normals from the higher-resolution height texture so detail between mesh vertices affects lighting. Sample
material and valid water/sediment attributes at fragment positions. This changes shading, not the silhouette;
additional geometric displacement is deferred. Use halo samples at boundaries and the same source-coordinate
mapping on regular and transition faces. Texture binding must use the existing material/instance binding path.

Future local refinement belongs between decoded source data and extraction. It must use canonical world
coordinates and the source revision, agree at shared boundaries, and supply a conservative displacement
bound before old empty certificates can be reused. This is a future constraint, not a refinement framework
to implement here. Display source-raster spacing separately from node width and mesh sample spacing.

### Service ownership, saved content and controls

Create `services/terrain/` with a Python package, project metadata, dependency lock, tests and README.
Use NumPy arrays and vectorized raster operations; do not write per-cell Python loops for erosion or full
island sampling. A single generator process owns mutable arrays. The HTTP process serves completed
read-only revisions and owns the small job state machine. Use explicit process messages and staged files
for publication; avoid passing full terrain arrays repeatedly through pickled queues.

For the first implementation a revision directory stores metadata, the canonical channel rasters and their
min/max hierarchy. A worker writes to a temporary sibling directory and finishes with an atomic publication
step; only then may the latest manifest pointer change. Published files are never modified in place.
On-demand tile encoding samples immutable data and may populate a bounded disposable tile cache. Keep
old published revisions available until an explicit cleanup while the service is idle. Cap total output
storage and pause/fail publication visibly when full; do not silently evict a revision an active viewer uses.

Provide a read-only serving mode that opens an exported revision directory with **no generator worker and
no FastScape import**. It exposes the identical manifest and tile endpoints. This is the first baked-content
proof; a CDN/static tile archive and bulk materialization of every possible fine tile are deferred.

| Control | Behavior |
| --- | --- |
| `POST /v1/jobs` | Validated full parameter set, island ID and client operation ID; return job ID immediately with 202. Duplicate operation ID plus identical body returns the same job; conflicting body fails. |
| `GET /v1/jobs/{job}` | State, phase, progress, immutable input parameters, error, published revision, and later simulation year/step timings. |
| `POST /v1/jobs/{job}/cancel` | Cooperative cancellation; retain previously published content. |
| `POST /v1/jobs/{job}/pause`, `/resume`, `/step` | Added with FastScape. Pause between steps; a step from paused advances exactly one numerical step. |

Initially permit one active generation job; a second conflicting request receives 409. Keep editor draft
parameters separate from the submitted job's immutable parameters. To change them, cancel/wait and submit
a new job. Poll status at a modest fixed cadence (initially 2 Hz); no WebSocket or SSE requirement.

Job states are `queued -> running -> completed`, with `failed` and `cancelled` terminal alternatives.
FastScape adds `pausing -> paused -> running` and `cancelling`; record a requested pause immediately
but never claim it has taken effect while Fortran is still executing a step. A crashed worker marks the job
failed; it cannot change latest content. Publication is a sub-operation, not a second mutable terrain state.

Bind the development service to loopback. Extend the existing browser development host with a fixed `/v1/`
proxy to that service, preserving COOP/COEP and existing fixture hosting. Avoid an arbitrary URL forwarding
proxy. Native may contact the service directly. A deployed viewer/service can use the same-origin proxy
pattern; public authentication and remote-job infrastructure are outside these first stages.

### Simple asynchronous terrain data cache

The application-side terrain build job is the caller. Where a build currently prepares noise samples, it
requests its own-footprint terrain data through `TerrainDataCache`, unless its parent already carries terminal
source-resolution data. In that case it inherits that data and samples the appropriate subregion. A requesting
job decodes the arriving bytes before meshing. The octree chooses what to build, not how to cache it. Keep this distinct from
the Python generation service and its separately published terrain files.

Expose one asynchronous request operation with a `skipCache` boolean. The following pseudocode describes
the implemented queue/completion flow; it does not require a coroutine framework:

```text
terrain build job:
    if parent exists and parent.source.isAtSourceResolution:
        source = parent.source.forSubregion(node.boundsXZ)
    else:
        key = contentKeyForNodeFootprint(node, contentRevision)
        bytes = await terrainCache.request(key, skipCache)
        source = decodeAndValidate(bytes)
    mesh = buildTerrain(source, node.bounds)
    publish mesh with source ownership and shading texture mapping

terrainCache.request(key, skipCache):
    if not skipCache:
        cached = await localStorage.read(key)
        if cached exists and passes payload validation:
            return cached

    bytes = await http.get(urlFor(key), bypassHttpCache=skipCache)
    validate response and payload
    if not skipCache:
        await localStorage.writeAtomically(key, bytes)
    return bytes
```

`await` means yielding the job until completion; implement this with the existing queues and a small
Acquiring/Meshing job phase if that is simpler. A pending read must not occupy either of the two mesh
threads or block the coordinator or UI. Cache hits are also asynchronous to their callers.

Use ordinary files in an application cache directory on Windows and browser-local persistent storage
(IndexedDB) on web. The browser path has the same behavior but cannot access arbitrary native disk paths.
Store the downloaded encoded bytes, not generated meshes. Hash the canonical content key to a safe local
filename/storage key; validate its embedded identity on read. A bad/truncated entry is a miss and is replaced
only after a successful validated download. Temporary-file rename or a browser storage transaction prevents
partial writes from appearing as hits. Concurrent misses may independently fetch and atomically store the
same immutable bytes; do not require shared-consumer bookkeeping in the first version.

`skipCache=true` bypasses persistent cache reads and writes and requests HTTP cache bypass through the host
transport. It does not delete existing entries. It uses the same request/completion API and preserves pinned
revision identity; it is not an instruction to silently substitute latest terrain. Capture the flag at request
submission. A laboratory checkbox applies it to subsequent requests, with equivalent native/browser launch
options for automated tests. Mutable latest-manifest and job/control calls always bypass this terrain cache.

Keep cache failures separate from source failures: if a cache write fails (including quota/disk full), return
the valid downloaded data and report that persistence failed. A missing cache entry plus failed HTTP returns
an error. Never return another revision as a fallback. Initially provide explicit cache clearing while idle,
with verified cache-directory scope, rather than background eviction or cache maintenance machinery.

### HTTP transport, identities and budgets

Add a small `src/platform/http-client.*` contract with Windows WinHTTP and Emscripten Fetch implementations.
Each request has one terminal outcome: response, transport failure or cancelled. Store status and bounded
body bytes separately from transport errors. Callbacks own request state only and push completion records;
they never mutate the octree, UI or GPU objects. Cancel/shutdown must be safe even if a completion races.

Cache requests progress through short I/O/completion queues. Bound active acquisitions initially to two,
retaining scheduler order (coarse first, then near). An acquiring terrain job yields; a ready one enters the
mesh queue. Cancel each request independently, including duplicate misses for the same tile. No predictive
prefetch, HTTP batch protocol or second terrain scheduler is required.

Keep the existing `Pending / Loading / Loaded / Failed` node states. Fetching, decoding, meshing and uploading
are internal phases of Loading. Preserve parent readiness before child dispatch and complete sibling-group
reservation. A failure retains visible parent coverage and awaits explicit retry. Do not invent empty meshes
to conceal HTTP failures or repeatedly retry a missing service in a tight loop.

Capture `{source identity, contentRevision, NodeAddress, RequestId}` for each logical build. Source identity
includes the configured base URL and island. Decoder and completion acceptance compare the captured revision;
redirects/latest pointers cannot change it. Distinguish `contentRevision` from the existing display serial.
The persistent cache key contains source identity, content revision, encoding version and the horizontal
tile address, not transient RequestId. Different vertical nodes can therefore reuse the same disk data.
Drain cancellation and stale replies without accepting them into a newer epoch.

Do not add a separate retained in-memory LRU cache. Bound encoded acquisitions to two 2 MiB responses.
Use one 512 MiB combined CPU residency budget for service terrain, including decoded node-owned inputs,
geometry, shading images and inherited ownership. The original separate 128 MiB source allowance did not
fit the measured workload; see the decision log. GPU geometry, textures, uploads and retirement share the
256 MiB terrain budget. Charge copies, halos and inherited terminal inputs once per allocation.
Reserve decode space before issuing an acquisition, and release unused inputs before admitting more work;
a full input budget defers work without blocking a mesh worker or evicting an input still in use. Reserve
required sibling completion capacity including source data/textures, or retain the parent and report a budget
limit; do not dispatch a partially funded sibling group that cannot finish. Bytes
retained by a pending write or mesh job remain charged until released. Bound JSON controls/manifests separately
(256 KiB initially). Disk usage and cache hits/misses/bypasses are diagnostics, not live memory residency.
Request/body, mesh scratch, pending uploads, GPU and retired-resource counters must remain distinct.
Reuse connections; no new connection per terrain cell by design. Compression is a measured follow-up.
Enforce advertised and observed byte limits and cancel oversized responses. Browser-managed network buffers
are outside the application's exact residency accounting; report that limit instead of claiming the HTTP
reservation is a hard cap on browser-process memory.

### Coherent revision handoff

Stage 2 pins one revision for its entire session. Stage 3 adds switching. Do not implement seamless refresh
by calling today's destructive reset and immediately displaying whatever new roots finish first.

Use one coordinator, one controller and the existing mesh workers. Add a small explicit handoff state:
`Viewing -> PreparingReplacement -> Viewing`, with failure retaining the old view. At handoff start,
capture the admitted/displayed root set. Keep the old complete rendered cut and its payload ownership;
stage the new content at coarse root coverage for that captured set. Temporarily defer new observer-driven
root admissions/refinement, but keep the camera and UI responsive and coalesce the latest observer.

Advance the build epoch and direct new work to the captured immutable revision. Old payloads remain charged
until the renderer really releases them. Retirement messages cannot release a payload still in the pinned
display cut. A new snapshot is eligible for adoption only when **every captured root** is represented by
ready new-revision coverage, including legitimate empty coverage. At one frame boundary replace the whole
cut and its content revision, then retire the old cut after GPU completion and resume normal demand.
Request IDs remain unique across resets; no callbacks use a bare node address.

When headroom is insufficient, first coarsen the old displayed cut to already-loaded parents and retire
unselected descendants, then stage the coarse replacement. If even both minimal cuts cannot fit, preserve
the old view and report a budget block. Do not exceed budgets or deadlock waiting for memory held by an
unnecessarily detailed old cut. Switching may briefly reduce detail; blending or preserving identical detail
is explicitly not required. Newly exposed areas during a long handoff may wait for admission.

Only one replacement is active. Later published revisions occupy one latest-pending slot and are processed
after the current handoff, rather than repeatedly cancelling it and starving visibility. Explicit user cancel
may discard the candidate. Content publication continues independently of viewer adoption.

## Plan of Work

### Stage 1: Serve a deterministic flat island

Implement the Python package, one job worker, contour/raster generation, immutable revision publication,
manifest/tile endpoints and read-only serving mode. Freeze `docs/terrain-content.md` with byte layout,
canonical sampling, addressing examples and limits before C++ integration. Commit small generated protocol
fixtures, including a negative/distant root and a deliberately off-grid narrow coast feature.
Export all initial channels and the full-source footprint AABB. Include a narrow peak and valley absent
from the returned coarse samples to prove that the AABB still contains both. Freeze the 256-cell plus
edge/halo convention and categorical/validity sampling rules alongside the binary layout.

Keep module responsibilities concrete: `island.py` for geometry/raster generation, `content.py` for revision
files and sampling, `protocol.py` for wire encoding, `jobs.py` for worker ownership, and `app.py` for routes.
Do not split trivial functions into extra modules. Generate a top-down diagnostic image showing seed points,
the cell, inset, spline, sea-level contour and heightfield. This diagnostic can be a test artifact; it need
not become a second editor application.

Visible result: start the service, submit a job, see a flat island diagnostic, request coarse/fine tiles,
restart in read-only mode and receive the same data. A service test must prove that status/content requests
remain usable while the worker is held at an explicit gate.

### Stage 2: Stream and render that content on both hosts

Implement the HTTP client, simple persistent cache and decoder using the stage-1 fixtures. Add an optional terrain-source URL
and island selection to native/browser launch settings. Fetch and validate the manifest, pin its revision,
and use manifest bounds for root admission and a safe initial camera placement. Preserve the noise diagnostic.

Refactor field sampling into an immutable mesher input without changing Transvoxel tables or face selection.
Make each terrain build job request its data through the cache, yield while acquiring, and resume decoding
and meshing when ready; preserve cancellation, failure and accounting. Replace noise-specific surface teleports
with source height queries through the same cache or conservative
manifest-based placement until a sample is available. No synchronous HTTP query during camera movement.
Request one full data tile per node footprint until the source-resolution cutoff. Below it inherit the
terminal source reference and address subregions without HTTP, disk reads or copied child textures.
Carry the validated AABB into streaming metadata, including for empty coarse meshes. Descendants may use
the broader ancestor bound until tighter data arrives. Do not derive emptiness from coarse mesh vertices.

Upload the channels needed for shading and use the full texture resolution in the shared terrain shader,
including height-derived shading normals and material inspection between mesh vertices. Bind per-node
source textures and coordinate mapping through existing material/instance properties, including transition
faces. Validate formats and sampling on both D3D12 and WebGPU; do not assume float32 filtering is available.
Keep original arrays for builds and derive only the GPU representation actually needed. Document its precision.
Render the coast, flat plateau and seabed with simple diagnostic materials and an optional flat sea-level
reference. Existing LOD colours and node bounds remain available. Add no vegetation or water simulation.
Demonstrate a moving camera, cross-root coordinates, delayed neighbouring data and a disconnected service.
Switch the configured URL to read-only saved content and prove identical terrain decoding and geometry.
Repeat a cached tile request after application restart with the HTTP endpoint unavailable and verify a disk
hit. Then enable skip-cache, request that same tile and prove it attempts HTTP rather than returning disk data.

Visible result: both Windows and browser show the same service-generated island and retain coarse coverage
under slow responses. Streaming continues independently of the generator process and simulation technology.

### Stage 3: Edit parameters and regenerate safely

Add an ImGui generator section with service connection state, island/region seed, seed spacing and jitter,
ocean clearance, contour rounding/variation, plateau/sea/seabed heights, coast width and source resolution.
Use explicit Apply/Regenerate and Cancel actions; editing a slider must not submit a job every frame.
Display validation errors next to the relevant controls and preserve the last accepted parameters.
Display source spacing, data-tile spacing and mesh spacing separately. Add a conservative-AABB overlay
and basic channel inspection for surface, sediment thickness, water depth and material, including unknowns.
Include a **Skip terrain cache** checkbox for live development, passed to the same cache API on subsequent
build requests. Changing it does not automatically rebuild already loaded terrain; regeneration/reload does.

Show job phase/progress/error, latest published revision and currently displayed revision. Add the coherent
handoff above with tests before wiring automatic adoption. Preserve camera position unless the user chooses
Frame Island; regeneration must not unexpectedly teleport the camera. Bound/sea-level metadata switches
with the terrain revision. Add a small contour overlay if needed to compare the rendered shoreline with
the generator diagnostic, using the manifest's contour data or a separate bounded debug response.

Visible result: change the island shape/height, trigger regeneration and observe a single coherent switch
while navigating. A failed generation or HTTP replacement leaves the previous island inspectable. Document
that sparse low-resolution previews can initially miss sub-cell features before finer demand resolves them.

### Stage 4: Run FastScape and iterate on the terrain

Add FastScape as an optional dependency of the generator worker only. First prove its Python binding and a
small known upstream example in an isolated environment; pin the exact library revision, Python/NumPy and
Fortran compiler/toolchain used. Prefer the Linux environment intended for eventual CPU-server baking if
native Windows packaging creates unnecessary friction. Provide one documented runnable setup, with a tested
Windows-viewer-to-service connection; browser/native viewers do not acquire this dependency.

Initialize the solver from the published island raster. Map application X/Z to the solver's horizontal axes
explicitly, including flattening order. Set domain extents, boundary conditions, timestep, uplift, erosion,
diffusion and precipitation parameters. Keep a seabed margin and fixed outer boundary with zero boundary
uplift. Test continental/marine interpretation and avoid unintentionally eroding the ocean floor with a
terrestrial-only setup. Pin sea-level handling; do not clamp coastlines after each step to hide solver behavior.
This is routed-discharge landscape evolution, not a transient moving-fluid simulation. Describe the selected
erosion/transport equations in the experiment notes rather than labelling every output a hydraulic solution.

The worker owns all FastScape calls. Run one numerical step at a time, acknowledge controls at step boundaries,
and publish copied completed heightfields alongside available bedrock, sediment and water outputs using the
same exporter. Do not infer water depth from drainage area; mark unavailable water values unknown and retain
the explicit static-sea convention where applicable. Keep the solver's full state alive across pause/resume.
Do not restart it from height alone, which would lose sediment/history state. Actual
on-disk solver restart and timeline scrubbing are deferred; exported previews are viewable terrain snapshots.

Expose Play/Pause/Step, timestep in years, requested duration, uplift and primary erosion/deposition parameters,
plus relative precipitation. Start spatial forcing uniformly except the boundary mask; climate maps come later.
Parameter changes that alter the experiment start a new run from an explicit initial condition. Preserve the
run recipe, seed, versions and step/year metadata with published outputs.

Separate numerical step size from preview frequency. Initially offer publication every N completed steps
with a minimum wall-time interval; explicit paused Step publishes its result. Allow an explicitly labelled
lower-resolution preview export for larger solver grids, recording its actual source spacing. This changes
preview fidelity, not the simulation grid. Apply publication backpressure and retain only a bounded number
of pending notifications; no unlimited queue of whole-island copies.

Visible result: from a fixed camera observe coastline, channels and hillslopes changing over simulated time,
including a stopped intermediate revision. Inspect height change and drainage diagnostics alongside the
terrain. Begin with 257x257 or 513x513 solver samples, then benchmark 1025x1025 and 2049x2049 only after
correctness and memory measurements. Do not promise the full 128 km island at metre spacing in this stage.

Run controlled comparisons: uplift on/off, precipitation multiplier, timestep refinement and sediment
transport off/on. A perfectly flat plateau is a delivery fixture, not a guarantee of interesting erosion:
include a documented small seeded perturbation or broad uplift field for the erosion experiment. Evaluate
channels, drainage connectivity, slopes, deposition and coast behavior; save the recipes and comparisons.
Only consider an alternative solver or GPU backend after identifying a specific measured limitation.

## Concrete Steps

Work from `C:/dev/ofg`. At implementation start reread this plan and `git status --short`, preserve the
current uncommitted work, and read applicable nested instructions. Use the existing
[native skill](../../.agents/skills/build-native/SKILL.md) for MSVC setup and
[web skill](../../.agents/skills/build-web/SKILL.md) for browser builds and screenshots. Do not assume a
plain shell has the compiler environment. Pin new dependencies during their first implementation stage.

Repository check interfaces (actual results are recorded in Progress; not every command is repeated for every edit):

```powershell
cmake --preset cpu-tests
cmake --build --preset cpu-tests --parallel
ctest --preset cpu-tests --output-on-failure
cmake --preset native-release
cmake --build --preset native-release --parallel 6
ctest --preset native-release --output-on-failure
cmake --preset web
cmake --build --preset web --parallel 6
node tools/terrain-workers-smoke.mjs
node tools/terrain-smoke.mjs
```

The following service commands are implemented. Dependencies are pinned in the isolated service lockfiles.
See [erosion setup and verification](../terrain-erosion.md) for the optional tested Linux solver environment.

```powershell
python -m venv .venv-terrain
.venv-terrain/Scripts/python -m pip install -r services/terrain/requirements.lock
.venv-terrain/Scripts/python -m pip install --no-deps -e services/terrain
.venv-terrain/Scripts/python -m terrain_service serve --host 127.0.0.1 --port 8765 --data-dir artifacts/terrain-service/data
.venv-terrain/Scripts/python -m pytest services/terrain/tests --cov=terrain_service --cov-branch --cov-report=term-missing
.venv-terrain/Scripts/python -m terrain_service serve --read-only artifacts/terrain-service/export --port 8766
build/native-release/ofg.exe --terrain --terrain-service http://127.0.0.1:8765 --island demo
.venv-terrain/Scripts/python tools/terrain-client-test.py --browser
.venv-terrain/Scripts/python tools/terrain-client-test.py --browser --editor
.venv-terrain/Scripts/python tools/terrain-client-test.py --soak --executable build/native-release/ofg-render-test.exe
```

The browser launch is `http://127.0.0.1:8080/?demo=terrain&terrainService=/v1&island=demo`.
Normalize whether the supplied base includes `/v1` in one place and document it; no duplicate path prefixes.
The smoke runner must start service/proxy processes itself with ephemeral ports, wait on health/readiness
conditions, and stop only processes it owns. Native process launches used by automation must be hidden.
Add an explicit Release service profile command with its recipe/output paths before making cost claims.

## Validation and Acceptance

### Required automated tests

| Area | Tests that must exist / observable invariant |
| --- | --- |
| Voronoi geometry | Identical seed/region gives identical output regardless of query order; negative/distant indices; bound on diameter; clipping agrees with larger-neighbour oracle; no coincident seeds; exact-boundary/tie cases. |
| Contours | Closed curve with matching join tangents, containment, no intersections, nonzero area, ocean margin, invalid inset rejection; seeded variation reproducible. |
| Initial heightfield | Plateau constant away from coast; correct sea-level crossing, monotone coast profile and seabed extension; finite samples; raster edge continuous. |
| Terrain channels | Bare-rock fixture has zero sediment thickness; static sea depth is correct; known dry differs from unknown water; bedrock above surface and negative water depth fail validation; material IDs remain categorical and palette-valid. |
| Publication | Held writer, crash before rename, crash before latest-pointer update, restart and interrupted cancellation never expose partial data; old revision unchanged after regeneration. |
| HTTP/control | Real loopback requests for GET/POST, invalid values, unknown IDs, duplicate operation IDs, conflicting jobs, worker crash, timeout and disconnect; no job creation from content reads. |
| Persistent terrain cache | Miss fetches and saves; hit makes zero HTTP requests, including after restart; skip-cache hits HTTP and neither reads nor writes local storage; existing entries remain unchanged; source/revision/format keys isolate data; corrupt entry refetches; interrupted write is never a hit; cache write failure still returns valid downloaded data with a diagnostic. |
| Protocol | Python-encoded fixtures decoded in C++; exact size/endian/version/address/revision checks; truncated/oversized/nonfinite/corrupted headers; 64-bit IDs above 2^53. |
| Sampling | Same-depth edges/halos and coincident 2:1 samples bit-identical; negative/root boundaries; constant/planar/shore fixtures; min/max bounds independently checked against all intersecting source cells. |
| Data resolution | 256 cells, 257 edge-inclusive samples and 259 apron-inclusive samples; 8 data intervals per mesh interval with current settings; root spacing 512 m; own-footprint requests at each depth until cutoff; a 1 m source stops at 256 m node width, and finer descendants perform no acquisition. |
| Source inheritance | Parent-to-descendant X/Z mapping preserves canonical values at edges/corners and negative roots; descendants keep terminal input alive; nonterminal coarse nodes have independent inputs; terminal lattice reproduces the published source without repeated resampling; inconsistent manifest lattice is rejected. |
| Texture shading | A fixture varies height/material between otherwise identical mesh samples; native/browser renders must show that detail with unchanged geometry. Verify texel-centre/halo mapping, derived normals, categorical sampling, regular/transition-face boundaries, inherited texture regions and supported formats. |
| Conservative AABB | Coarse samples miss a narrow peak and valley but returned bounds contain both; bounds enclose all intersecting source cells and seabed extension; inherited bounds never falsely prune children; touching remains eligible; malformed/missing bounds fail rather than certify empty. |
| Empty certificates | Entirely air/solid nodes certify; exact touching does not; thin island missed by coarse sample vertices remains refinable and appears at finer detail. |
| Mesh integration | Flat plane, slope, coast and fine/coarse faces/corners; compare shared contour edges with existing tolerance of 1e-5 times sample spacing; finite normals and correct winding. |
| Acquisition lifetime | Reordered responses, held sibling, cancellation during disk read/HTTP/write, independent duplicate misses, stale revision/epoch and shutdown; every reservation released exactly once and no cancelled job resumes meshing. |
| Input memory | Real-size multi-channel tiles obey response/acquisition/decode limits; inherited terminal allocations are charged once; retained parents remain renderable with their textures; releases wait for all CPU/GPU users; sibling admission cannot deadlock on retained inputs; budget saturation retains parent coverage; teardown drains inputs, texture retirement and pending writes. |
| Stream regression | Existing retained-parent, all-sibling readiness, face balance, nearest-within-depth scheduling, negative-address and retirement tests continue to pass. |
| Revision handoff | No mixed-content cut; delayed one-of-eight and one-of-several roots; old cut retained on error; queued newer revision cannot starve current switch; unique IDs; moving camera; budget coarsening and minimal-budget refusal. |
| Saved-content equivalence | Live and read-only service return identical canonical tile bytes for origin/distant and multiple depths; C++ geometry is identical for those inputs. |
| FastScape basics | Zero forcing/rates preserves initial field; simple downhill bedrock case lowers interior with fixed boundary; finite outputs; drainage reaches permitted outlets on a small fixture. |
| FastScape time/state | One step changes step/year once; pause acknowledgement only between steps; pause/resume equals uninterrupted run at same steps; changing preview cadence does not change final solver state. |
| Sediment/numerics | Small deposition fixture and documented sediment-budget diagnostic; verify solver flux conventions before asserting conservation; timestep-halving comparison with an explicit physically scaled tolerance. |

Use explicit gates/events for ordering and cancellation tests, not sleeps. HTTP integration tests own a real
local server; pure tests use small literal data and independent oracles. C++ tests remain native doctest/CTest;
browser tests prove actual fetch, threads and WebGPU, rather than porting the full suite to WebAssembly.

For initial terrain comparisons require flat/constant fixtures to be exact after float32 conversion;
bilinear sampling error versus a double reference must be at most `max(1e-4 m, 2 float32 ULPs)` per sample.
Topology uses the existing mesher tolerance above. Same-build paused/uninterrupted solver fixtures should
agree to 1e-10 m; cross-toolchain fixtures initially use 1e-6 m plus 1e-9 relative tolerance. For a timestep
convergence fixture choose its parameters and interval so the two finer runs differ by less than 1% of
initial relief in RMS elevation and improve over the coarse run; record the actual values, not just a pass.
Resolve failed physical tolerances through numerical diagnosis, not relaxed assertions without evidence.

Coverage policy: target at least 90% line and 80% branch coverage for new Python geometry, protocol and job
logic using pytest-cov, excluding third-party solvers/generated files. For new pure C++ decoding and handoff
logic target 90% line coverage in a focused instrumented native CPU build; establish and record the actual
available coverage tool/command during stage 2. Native HTTP callbacks and GPU paths additionally require
integration tests; line coverage cannot establish their lifecycle correctness. Report exclusions and missing
coverage tooling explicitly. Do not inherit a claimed percentage from earlier work.

### Native and browser demonstrations

Save screenshots and short motion captures under `artifacts/terrain-service/stage-N/{native,web}`. Inspect
top-down outline, shore-level view, LOD boundaries and a negative/distant island. Show both LOD colours and
normal terrain shading, including detail between mesh vertices and crossing the source-inheritance cutoff.
Compare against the Python contour/height diagnostic. Camera movement, resize,
reload, regeneration and service interruption must remain responsive. Show the actual sea-level reference,
not an outline that only exists in metadata. Check browser console/network errors and shared-memory isolation.
Include native disk and browser persistent-storage hit/miss/bypass checks across application/page reloads.

Stage 3 must include a recording that holds one candidate root while the old revision remains visible, then
releases it and proves an atomic switch. Stage 4 must include at least three intermediate erosion states,
their simulated times and matching generator parameters. Supply representative images to the user.

### Performance and memory evidence

Measure native in Release with validation disabled for timing, naming CPU/GPU, backend, viewport, source grid,
visible region, cache state and loopback/remote network conditions. Record time to first coarse surface and
16 m node convergence, frame p50/p95/p99, main-thread terrain update time, HTTP latency/bytes, decode/meshing
time, shading-texture upload/residency, retained source arrays and all other tracked residency categories.
Record request counts above and below the source cutoff; descendants below it must add zero data requests.
Compare with the same existing noise workload where meaningful.

Initial targets, not verified promises: no network waits on the render/coordinator threads; terrain main-thread
update p95 below 2 ms and no new terrain-induced stalls above 16 ms on the development machine. Slow-server
tests should affect time-to-detail rather than frame responsiveness. If a target fails, profile and record
the cause before altering it. Preserve budgets during a 10-minute moving/regeneration run; after teardown
require zero tracked requests, cache ownership, mesh/upload/GPU retirement and generator worker ownership.
Persistent cache files intentionally survive teardown; zero cache ownership refers to in-flight memory/handles,
not deleting downloaded content. Compare cold-cache, warm-cache and skip-cache runs explicitly.

For FastScape report per-step and total wall time, peak process RSS, snapshot/export/HTTP costs separately,
and the actual compiler/build and numerical settings. Test disk-space backpressure. No CPU/GPU cost comparison
or 128 km runtime extrapolation is valid until the matching workload and peak memory have been measured.

## Idempotence and Recovery

Keep previous published revisions during regeneration and service restart. Temporary incomplete directories
are never served and may be removed only inside the configured service data root after verifying their paths.
Do not delete unrelated artifacts or the historical repository. Data directories and virtual environments
are ignored build/output state; commit recipes, small fixtures, protocol and dependency pins.

GET is repeatable. A timed-out POST is resolved by its operation ID or job status, not blind duplicate job
creation. A failed candidate can be explicitly retried while the previous view remains intact. Disconnects
do not clear visible terrain. Reconnection revalidates service/content identity and never reuses spatial-only
cache keys. A solver failure is visible and preserves its last completed publication.
Local cache bypass leaves existing entries untouched. Failed/interrupted cache writes cannot become visible
hits, and explicit clearing must remain inside the designated application cache namespace.

Stage 4 initially restarts failed simulations from the stored initial recipe, not from a height-only snapshot
masquerading as complete solver state. Document that recovery limit in the UI and service README.

## Milestone Review

At every stage review correctness, ownership and cancellation, integer/float boundaries, byte limits,
unnecessary abstractions, comments, stale noise assumptions, protocol compatibility and documentation.
Review service and application independently of whether a screenshot looks plausible. Implementation review
covered source inheritance/accounting, revision adoption, cancellation, preview bounds, categorical shading,
numerical reproducibility and documentation. Final review fixed shutdown with an unread publication message:
closing now discards pending previews, joins the owned process and closes its endpoint explicitly. A regression
proves the last adopted revision remains intact. Ambiguous Step responses now retry with status reads instead
of replaying a non-idempotent control. Numerical findings and remaining performance/coverage gaps are recorded
above and in the erosion notes.

## Interfaces and Dependencies

Use FastAPI/Uvicorn, NumPy and pytest/pytest-cov in the isolated service environment; freeze exact versions
when stage 1 starts. Use standard-library process and filesystem facilities for the single worker. A compact
half-plane clipper avoids bringing in a general geometry stack solely for a handful of Voronoi vertices.
Do not manually implement an HTTP protocol stack. Use WinHTTP on Windows and existing Emscripten Fetch on web.
Keep CPU protocol/mesher tests independent of RHI and live network access.

FastScape's Python binding is optional until stage 4 and disabled in read-only content mode. Preserve upstream
license/provenance and record the exact dependency revision. No claims of feature parity between the Fortran
and newer C++ packages. No requirement to compile Fortran into WebAssembly.

Deferred: real-time local detail generation, infinite-world discovery, full-island metre-scale bake,
GPU erosion, climate/vegetation simulation,
arbitrary voxel/cave editing, progressive resolution changes inside FastScape, temporal interpolation,
multi-user jobs, server authentication, cloud orchestration, CDN export, full solver checkpoints and persistent
gameplay modifications. These must not become prerequisites for the first island appearing in both viewers.

## Artifacts and Notes

Primary references checked during planning:

- [Current terrain contracts](../terrain-streaming.md) and [previous streaming plan](../archived/sparse-terrain-streaming.md).
- [FastScape Fortran API](https://fastscape.org/fastscapelib-fortran/) for initialization, steps and data extraction.
- [Fortran source](https://github.com/fastscape-lem/fastscapelib-fortran) for worker ownership and version pinning.
- [New C++ eroder scope](https://fastscapelib.readthedocs.io/en/latest/guide_eroders.html) for the feature distinction.
- [Emscripten Fetch](https://emscripten.org/docs/api_reference/fetch.html) for asynchronous requests and cancellation.
- [WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/about-winhttp) for the native transport.
- [FastAPI worker deployment](https://fastapi.tiangolo.com/deployment/server-workers/) for HTTP process configuration.

Planning validation on 2026-10-09: local Markdown links resolve; all required ExecPlan sections are present;
code fences are balanced and trailing-whitespace checks pass. No builds, service tests, browser demonstrations
or performance measurements were run for this proposal.
