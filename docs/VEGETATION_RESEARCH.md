# Vegetation Research

This note records early research for adding vegetation and natural props to OFG.
It is intentionally design-level: no runtime vegetation exists yet, and the
current engine architecture still says terrain/world/render ownership should stay
inside Rust.

## Current OFG Constraints

- `terrain_core` already owns deterministic density, height, macro fields, biome
  weights, material classification, chunk meshing, exact polygonized surface
  queries, and mesh-backed placement sample generation.
- `engine_web` owns the playable terrain stream and Rust/wgpu renderer. The
  current stream emits terrain chunk meshes and Rust-owned aggregate placement
  sample counters, but no vegetation instances or foliage rendering yet.
- The renderer currently has one terrain-shaped mesh pipeline: one vertex buffer
  layout, one object uniform per draw, and one indexed draw per mesh/object.
  There is no vegetation-specific pipeline, no instance buffer, no alpha-cutout
  material, and no GPU culling path yet.
- Texture assets are still a temporary TypeScript-to-Rust bridge. The repo has
  terrain material textures, but no mesh asset pipeline for trees, rocks, shrubs,
  or grass cards.
- Browser/WebGPU constraints matter. WebGPU/wgpu supports instanced indexed
  draws and indirect draws, but browser WGSL does not expose geometry shaders,
  tessellation shaders, mesh shaders, or GPU work graphs. Techniques that depend
  on those stages are useful references but not first implementation targets.

## External Research Takeaways

Vegetation should be treated as layered world generation, not as a single scatter
pass. GPU Gems 2 describes outdoor botany as layers such as grass, ground
clutter, and trees, each managed in camera-relative planting grids with
deterministic random generation so the same world location regenerates the same
content. That model fits OFG's chunk stream well.

Placement quality depends on continuous surface masks. O3DE's vegetation system
uses surface data and gradient signals to drive placement; Unreal and Unity
similarly separate foliage types, placement filters, culling, density, LOD, and
instanced rendering. For OFG, the equivalent masks should come from Rust terrain
samples: biome weights, material weights, slope/normal, altitude, wetness,
future water/hydrology, cave/overhang state, and player/building exclusion.

Blue-noise or Poisson-disk style candidates are a good default for trees, rocks,
shrubs, and larger clumps because they avoid obvious random clustering. Bridson's
Poisson-disk sketch is useful as a reference for O(N) candidate generation, but
infinite chunked terrain needs a local deterministic variant: hash cell identity,
generate candidates with an apron around each vegetation cell, then keep only the
instances owned by the cell to avoid seam duplicates.

Dense grass should not start as individual blades. GPU Gems' grass chapter makes
the older but still relevant point that many blades need to be represented by few
polygons. For OFG's browser target, the practical first grass renderer is clumps
or cards, optionally in cross-card clusters, with wind in the vertex shader and
distance fade into the terrain material. Geometry/tessellation shader grass is
not a WebGPU-first path.

Instancing is the key render primitive once counts rise. NVIDIA's instancing
chapter explicitly calls vegetation and trees a strong fit for geometry
instancing, and Unreal/Unity production documentation backs that up with
instanced static mesh foliage and instanced terrain details. OFG should add an
instanced pipeline before trying to render dense grass or forests.

Alpha-cutout foliage will need its own material path. SpeedTree's GPU Gems 3
chapter and wgpu's `MultisampleState` documentation both point toward
alpha-to-coverage as a useful option for cutout leaves/fronds when MSAA is
enabled. OFG currently uses `MultisampleState::default()` and no blending, so
foliage cards should be planned as a separate render slice.

Recent GPU-generated tree research is interesting but too advanced for the
current browser path. Work-graph/mesh-node tree generation can render extremely
detailed trees from compact procedural descriptions, but WebGPU does not expose
that pipeline today. The useful lesson is to keep source vegetation descriptions
compact and procedural; the immediate implementation should stay with CPU/Rust
generated instance records and WebGPU instancing.

## Placement Model

Use a Rust-owned vegetation layer that is parallel to, but separate from, density
chunks:

```text
WorldDescriptor seed
  -> terrain sample/macro/biome/material fields
  -> vegetation rules and exclusion masks
  -> deterministic vegetation cells
  -> instance batches by species and LOD
  -> Rust/wgpu instance buffers and draw calls
```

Suggested initial data:

```rust
struct VegetationCellCoord {
    x: i32,
    z: i32,
}

struct VegetationInstance {
    species_id: u16,
    position: [f32; 3],
    normal: [f32; 3],
    yaw: f32,
    scale: f32,
    variation_seed: u32,
    flags: u32,
}

struct VegetationRule {
    species_id: u16,
    density_per_square_meter: f32,
    min_spacing: f32,
    min_slope_y: f32,
    max_slope_y: f32,
    min_altitude: f32,
    max_altitude: f32,
    biome_weights: [f32; 8],
    material_weights: [f32; 16],
}
```

Cell size should be surface-oriented rather than tied directly to 3D density
chunks. A 16 m or 32 m square cell is a sensible starting point because it lines
up with the current terrain chunk scale but does not force every vertical density
chunk to own duplicate vegetation.

For every candidate point:

- Query exact polygonized terrain surface height and normal from Rust.
- Reject if slope, altitude, biome, material, wetness, or water masks fail.
- Reject if future building/factory/road/player-edit exclusion masks fail.
- Apply deterministic jitter, yaw, scale, and species variation from hashed
  `(world_seed, cell_coord, candidate_index, species_id)`.
- For large props, reserve a clearance radius and optionally reject nearby
  candidates in the same or apron cells.
- Store only persistent deltas for gameplay edits, such as chopped trees,
  mined rocks, or cleared grass. Base placement remains procedural.

## Large Props

Trees, boulders, logs, and larger shrubs should use sparse instance records first.
They need stable identity if they can be chopped, mined, burned, or cleared, but
non-interactive distant props can remain ephemeral.

Recommended first slice:

- Add deterministic Rust candidate generation for boulders and tree placeholders.
- Generate simple procedural boulder meshes in Rust or add a tiny checked-in mesh
  asset path later. Boulders avoid alpha, wind, and asset complexity.
- Render only a small count near the player at first, even if that means one draw
  per prop temporarily.
- Move quickly to per-species instanced draws:
  - one shared mesh per species/LOD
  - one instance buffer per visible species or vegetation cell
  - one `draw_indexed(..., instance_count)` per species/LOD batch
- Add debug snapshot counts: candidate count, accepted count, rendered count,
  rejected-by-mask counts, and per-species counts.

Tree-specific concerns:

- Start with trunk plus low-poly leaf clusters or cross-card billboards, not a
  full procedural tree generator.
- Keep tree canopies away from steep cliffs and obvious rock/scree materials.
- Use forest biome weight, moisture, slope, altitude, and future hydrology masks.
- Plan for player interaction early: tree removal should be a save delta keyed by
  deterministic instance ID.

Rock-specific concerns:

- Rocks can appear in grassland as sparse props, but dense rocks should be driven
  by scree, rocky-ground, cliff, high-mountain-rock, and dry-badland masks.
- Large boulders should have terrain clearance and avoid floating on thin
  overhangs until collision/surface queries mature.
- Procedural mesh variation is acceptable for rocks and avoids adding a full
  asset pipeline too early.

## Grass And Small Ground Cover

Grass should be a short-range presentation layer that fades into terrain
materials, not a persistent world object.

Recommended first grass renderer:

- Generate grass clump/card instances from meadow grass, forest ground, wetland,
  alpine meadow, and low-slope masks.
- Use crossed quads or small hand-authored clump meshes with alpha-cutout
  textures.
- Animate wind in the vertex shader using world position, time, and a per-instance
  variation seed.
- Fade out by distance and density-dither near the cutoff.
- Do not add collision or save data for individual grass clumps; store only area
  edits such as "cleared vegetation mask" if gameplay needs it.

Avoid for the first implementation:

- Per-blade geometry at terrain scale.
- GPU-generated geometry that needs mesh/geometry/tessellation shaders.
- A TypeScript grass manager or terrain-aware browser worker protocol.
- A grass system before the renderer has an instanced path and a cutout material
  path.

## Rendering Roadmap

1. Reuse current mesh/object plumbing only for very sparse placeholder props.
2. Add a Rust/wgpu instanced pipeline:
   - base mesh vertex buffer
   - per-instance transform/normal/yaw/scale/color/variation buffer
   - separate shader entry point or vegetation WGSL module
   - `draw_indexed` with `instance_count`
3. Add alpha-cutout material support for foliage cards:
   - sampled albedo with alpha
   - discard or alpha mask threshold
   - optional MSAA plus alpha-to-coverage when the renderer supports it
   - distance fade/dither to avoid hard pop
4. Add per-species LOD:
   - LOD0 mesh close to the player
   - LOD1 simplified mesh or cards
   - LOD2 billboard/impostor or fade-out
5. Consider indirect draws and GPU culling only after CPU-side instanced batches
   are measurable bottlenecks.

## Testing And Debugging

Vegetation needs tests before density becomes high:

- Determinism: same seed, preset, and cell produces identical instances.
- Cell ownership: neighboring cells do not duplicate tree/rock instances at
  boundaries.
- Mask behavior: slope, material, biome, water, and exclusion masks reject
  expected candidates.
- Streaming: moving the player prunes and loads vegetation cells without losing
  stable identities.
- Renderer accounting: debug snapshot reports instance batches, instance counts,
  and draw counts.
- Browser smoke: fixed seed screenshots show trees/rocks/grass from near and
  mid distance, with no blank frames and no obvious alpha artifacts.
- Performance: fixed-scene benchmark reports CPU generation time, upload bytes,
  visible instance count, and draw count.

## Recommended Direction

The best near-term path is:

1. Add a Rust-only vegetation candidate generator for large prop placeholders,
   with tests and debug data, but no rendering.
2. Render a small number of deterministic boulders using the current mesh path to
   validate placement masks and streaming behavior.
3. Add the instanced render pipeline, then move boulders and simple tree
   placeholders onto it.
4. Add alpha-cutout card rendering and only then add grass clumps.
5. Add saved vegetation deltas after the first interactive clearing/mining use
   case is known.

This keeps the first visible win small while building the architecture needed for
real forests and grasslands.

## Sources

- NVIDIA GPU Gems 2, "Toward Photorealism in Virtual Botany":
  https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-1-toward-photorealism-virtual-botany
- NVIDIA GPU Gems 2, "Inside Geometry Instancing":
  https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-3-inside-geometry-instancing
- NVIDIA GPU Gems, "Rendering Countless Blades of Waving Grass":
  https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass
- NVIDIA GPU Gems 3, "Next-Generation SpeedTree Rendering":
  https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-4-next-generation-speedtree-rendering
- Robert Bridson, "Fast Poisson Disk Sampling in Arbitrary Dimensions":
  https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf
- Deussen et al., "Realistic Modeling and Rendering of Plant Ecosystems":
  https://algorithmicbotany.org/papers/ecosys.sig98.pdf
- Dimitris Papavasiliou, "Real-Time Grass (and Other Procedural Objects) on
  Terrain":
  https://jcgt.org/published/0004/01/02/
- Unreal Engine Foliage Mode documentation:
  https://dev.epicgames.com/documentation/unreal-engine/foliage-mode-in-unreal-engine
- Unreal Engine Grass Quick Start:
  https://dev.epicgames.com/documentation/unreal-engine/grass-quick-start-in-unreal-engine
- Unity Manual, "Grass and other details":
  https://docs.unity.cn/Manual/terrain-Grass.html
- Open 3D Engine Vegetation Gem documentation:
  https://www.docs.o3de.org/docs/user-guide/gems/reference/environment/vegetation/
- WebGPU render command reference:
  https://gpuweb.github.io/types/interfaces/GPURenderCommandsMixin.html
- wgpu `MultisampleState` documentation:
  https://wgpu.rs/doc/wgpu/struct.MultisampleState.html
- Eurographics/HPG 2025, "Real-Time GPU Tree Generation":
  https://diglib.eg.org/items/93fc78c0-71fa-4511-8564-a7e5268bf27a
