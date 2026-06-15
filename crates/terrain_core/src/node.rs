//! Terrain node identity and metric helpers.

pub const DEFAULT_TERRAIN_PRESET: u32 = 0;
pub const MAX_PLAYABLE_LOD: u8 = 5;
pub const TERRAIN_CHUNK_CELLS_PER_AXIS: u32 = 32;
pub const TERRAIN_NODE_SAMPLES_PER_AXIS: u32 = TERRAIN_CHUNK_CELLS_PER_AXIS + 1;
pub const LOD0_NODE_SIZE_METERS: f64 = 32.0;

#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainChunkCoord {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainNodeKey {
    pub lod: u8,
    pub coord: TerrainChunkCoord,
}

/// Returns the terrain node cell size for an LOD.
pub fn terrain_node_cell_size(base_cell_size: f64, lod: u8) -> f64 {
    base_cell_size * 2_f64.powi(i32::from(lod))
}

/// Returns the terrain node world span for an LOD.
pub fn terrain_node_size(base_cell_size: f64, lod: u8) -> f64 {
    terrain_node_cell_size(base_cell_size, lod) * TERRAIN_CHUNK_CELLS_PER_AXIS as f64
}

/// Returns the stable debug key for a terrain node.
pub fn terrain_node_key(key: TerrainNodeKey) -> String {
    format!(
        "lod{}:{},{},{}",
        key.lod, key.coord.x, key.coord.y, key.coord.z
    )
}

/// Returns the stable debug key for an LOD0 compatibility chunk.
pub fn terrain_chunk_key(coord: TerrainChunkCoord) -> String {
    format!("{},{},{}", coord.x, coord.y, coord.z)
}

/// Returns the LOD0 chunk coordinate containing the world position.
pub fn terrain_chunk_coord_containing_position(
    x: f32,
    y: f32,
    z: f32,
    cell_size: f64,
) -> TerrainChunkCoord {
    let span = terrain_node_size(cell_size, 0);
    TerrainChunkCoord {
        x: floor_to_i32(f64::from(x) / span),
        y: floor_to_i32(f64::from(y) / span),
        z: floor_to_i32(f64::from(z) / span),
    }
}

/// Returns the node coordinate at `lod` containing the world position.
pub fn terrain_node_coord_for_lod(
    x: f64,
    y: f64,
    z: f64,
    base_cell_size: f64,
    lod: u8,
) -> TerrainChunkCoord {
    let span = terrain_node_size(base_cell_size, lod);
    TerrainChunkCoord {
        x: floor_to_i32(x / span),
        y: floor_to_i32(y / span),
        z: floor_to_i32(z / span),
    }
}

/// Returns the next coarser parent node, if the key is below the playable root grid.
pub fn terrain_node_parent(key: TerrainNodeKey) -> Option<TerrainNodeKey> {
    if key.lod >= MAX_PLAYABLE_LOD {
        return None;
    }

    Some(TerrainNodeKey {
        lod: key.lod + 1,
        coord: TerrainChunkCoord {
            x: key.coord.x.div_euclid(2),
            y: key.coord.y.div_euclid(2),
            z: key.coord.z.div_euclid(2),
        },
    })
}

/// Returns the eight children covered by a coarser node.
pub fn terrain_node_children(parent: TerrainNodeKey) -> Option<[TerrainNodeKey; 8]> {
    if parent.lod == 0 {
        return None;
    }

    let lod = parent.lod - 1;
    let base_x = parent.coord.x * 2;
    let base_y = parent.coord.y * 2;
    let base_z = parent.coord.z * 2;
    Some([
        child(lod, base_x, base_y, base_z),
        child(lod, base_x + 1, base_y, base_z),
        child(lod, base_x, base_y + 1, base_z),
        child(lod, base_x + 1, base_y + 1, base_z),
        child(lod, base_x, base_y, base_z + 1),
        child(lod, base_x + 1, base_y, base_z + 1),
        child(lod, base_x, base_y + 1, base_z + 1),
        child(lod, base_x + 1, base_y + 1, base_z + 1),
    ])
}

fn child(lod: u8, x: i32, y: i32, z: i32) -> TerrainNodeKey {
    TerrainNodeKey {
        lod,
        coord: TerrainChunkCoord { x, y, z },
    }
}

fn floor_to_i32(value: f64) -> i32 {
    value.floor().clamp(i32::MIN as f64, i32::MAX as f64) as i32
}
