// Terrain LOD node identity and parent/child helpers for the rootless
// multi-resolution terrain grid.

use crate::TerrainChunkCoord;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainNodeKey {
    pub lod: u8,
    pub coord: TerrainChunkCoord,
}

impl TerrainNodeKey {
    /// Returns the highest-detail node key for an existing chunk coordinate.
    pub fn lod0(coord: TerrainChunkCoord) -> Self {
        Self { lod: 0, coord }
    }
}

/// Formats a stable debug and renderer identity for one terrain LOD node.
pub fn terrain_node_key(key: TerrainNodeKey) -> String {
    format!(
        "lod{}:{},{},{}",
        key.lod, key.coord.x, key.coord.y, key.coord.z
    )
}

/// Returns the world-space cell size for a node at `lod`.
pub fn terrain_node_cell_size(base_cell_size: f64, lod: u8) -> f64 {
    base_cell_size * 2_f64.powi(i32::from(lod))
}

/// Returns the next coarser parent in the rootless LOD grid.
pub fn terrain_node_parent(key: TerrainNodeKey) -> Option<TerrainNodeKey> {
    if key.lod == u8::MAX {
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

/// Returns the up-to-eight finer children covered by a coarser node.
pub fn terrain_node_children(parent: TerrainNodeKey) -> Option<[TerrainNodeKey; 8]> {
    if parent.lod == 0 {
        return None;
    }

    let lod = parent.lod - 1;
    let base_x = parent.coord.x.saturating_mul(2);
    let base_y = parent.coord.y.saturating_mul(2);
    let base_z = parent.coord.z.saturating_mul(2);

    Some([
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x,
                y: base_y,
                z: base_z,
            },
        },
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x.saturating_add(1),
                y: base_y,
                z: base_z,
            },
        },
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x,
                y: base_y.saturating_add(1),
                z: base_z,
            },
        },
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x.saturating_add(1),
                y: base_y.saturating_add(1),
                z: base_z,
            },
        },
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x,
                y: base_y,
                z: base_z.saturating_add(1),
            },
        },
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x.saturating_add(1),
                y: base_y,
                z: base_z.saturating_add(1),
            },
        },
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x,
                y: base_y.saturating_add(1),
                z: base_z.saturating_add(1),
            },
        },
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord {
                x: base_x.saturating_add(1),
                y: base_y.saturating_add(1),
                z: base_z.saturating_add(1),
            },
        },
    ])
}

/// Converts a highest-detail chunk coordinate into the coordinate grid for `lod`.
pub fn terrain_node_coord_for_lod(coord: TerrainChunkCoord, lod: u8) -> TerrainChunkCoord {
    let scale = 1_i64.checked_shl(u32::from(lod)).unwrap_or(i64::MAX).max(1);

    TerrainChunkCoord {
        x: div_i32_by_i64(coord.x, scale),
        y: div_i32_by_i64(coord.y, scale),
        z: div_i32_by_i64(coord.z, scale),
    }
}

fn div_i32_by_i64(value: i32, divisor: i64) -> i32 {
    i64::from(value)
        .div_euclid(divisor)
        .clamp(i64::from(i32::MIN), i64::from(i32::MAX)) as i32
}
