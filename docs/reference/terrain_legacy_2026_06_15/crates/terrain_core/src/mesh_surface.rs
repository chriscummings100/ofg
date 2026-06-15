// Convenience build helpers that pair terrain mesh output with exact surface
// query data.

use crate::*;

#[derive(Clone)]
pub struct TerrainNodeBuildSurface {
    pub mesh: MeshData,
    pub surface: Option<TerrainSurfaceIndex>,
}

/// Builds one terrain node mesh and a surface query index over that exact mesh.
pub fn build_node_mesh_and_surface_for_variant(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    key: TerrainNodeKey,
    base_cell_size: f64,
) -> TerrainNodeBuildSurface {
    let mesh = build_node_mesh_for_variant(seed, descriptor, key, base_cell_size);
    let surface =
        TerrainSurfaceIndex::from_mesh(key, terrain_node_cell_size(base_cell_size, key.lod), &mesh);

    TerrainNodeBuildSurface { mesh, surface }
}
