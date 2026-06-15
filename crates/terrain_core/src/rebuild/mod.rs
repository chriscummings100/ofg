//! New terrain model under construction.
//!
//! This module is intentionally small and specification-shaped. It captures the
//! terrain rebuild's core LOD, node, and replacement rules before generation,
//! streaming, worker execution, or renderer integration are rebuilt around it.

use std::collections::BTreeSet;

/// The current coarsest playable terrain grid.
pub const MAX_PLAYABLE_LOD: u8 = 5;

/// The number of voxel cells along one axis in every terrain node.
pub const TERRAIN_NODE_CELLS_PER_AXIS: u32 = 32;

/// The number of density samples along one axis in every terrain node.
pub const TERRAIN_NODE_SAMPLES_PER_AXIS: u32 = TERRAIN_NODE_CELLS_PER_AXIS + 1;

/// The world span of one highest-detail LOD0 node.
pub const LOD0_NODE_SIZE_METERS: f64 = 32.0;

/// Terrain level of detail, where 0 is highest detail and larger values are coarser.
pub type TerrainLod = u8;

/// Integer grid coordinate for one terrain node at a specific LOD.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainNodeCoord {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

impl TerrainNodeCoord {
    /// Creates a terrain node coordinate from integer grid components.
    pub const fn new(x: i32, y: i32, z: i32) -> Self {
        Self { x, y, z }
    }
}

/// Stable identity for one terrain node in the infinite LOD grid.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainNodeKey {
    pub lod: TerrainLod,
    pub coord: TerrainNodeCoord,
}

impl TerrainNodeKey {
    /// Creates a terrain node key from an LOD and integer grid coordinate.
    pub const fn new(lod: TerrainLod, coord: TerrainNodeCoord) -> Self {
        Self { lod, coord }
    }
}

/// Derived metric values for terrain nodes at one LOD.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainNodeMetrics {
    pub lod: TerrainLod,
    pub node_size_meters: f64,
    pub cell_size_meters: f64,
    pub cells_per_axis: u32,
    pub samples_per_axis: u32,
}

/// A parent node and the eight child node keys it covers.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainChildGroup {
    pub parent: TerrainNodeKey,
    pub children: [TerrainNodeKey; 8],
}

/// A cubic region of parent nodes used to derive desired child nodes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainParentRegion {
    pub lod: TerrainLod,
    pub center: TerrainNodeCoord,
    pub radius: i32,
}

impl TerrainParentRegion {
    /// Creates the normal 3x3x3 parent-region around the player.
    pub const fn three_by_three(lod: TerrainLod, center: TerrainNodeCoord) -> Self {
        Self {
            lod,
            center,
            radius: 1,
        }
    }
}

/// Readiness state for one child node in a parent replacement group.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerrainNodeReadiness {
    Missing,
    Generated,
    Empty,
}

/// Returns the world size of a terrain node at `lod`.
pub fn terrain_node_size_meters(lod: TerrainLod) -> f64 {
    LOD0_NODE_SIZE_METERS * 2_f64.powi(i32::from(lod))
}

/// Returns the world size of one voxel cell inside a terrain node at `lod`.
pub fn terrain_node_cell_size_meters(lod: TerrainLod) -> f64 {
    terrain_node_size_meters(lod) / f64::from(TERRAIN_NODE_CELLS_PER_AXIS)
}

/// Returns the derived terrain node metrics for `lod`.
pub fn terrain_node_metrics(lod: TerrainLod) -> TerrainNodeMetrics {
    TerrainNodeMetrics {
        lod,
        node_size_meters: terrain_node_size_meters(lod),
        cell_size_meters: terrain_node_cell_size_meters(lod),
        cells_per_axis: TERRAIN_NODE_CELLS_PER_AXIS,
        samples_per_axis: TERRAIN_NODE_SAMPLES_PER_AXIS,
    }
}

/// Returns a stable debug key for a terrain node.
pub fn terrain_node_debug_key(key: TerrainNodeKey) -> String {
    format!(
        "lod{}:{},{},{}",
        key.lod, key.coord.x, key.coord.y, key.coord.z
    )
}

/// Returns the playable parent of `key`, or `None` for the coarsest grid.
pub fn terrain_node_parent(key: TerrainNodeKey) -> Option<TerrainNodeKey> {
    if key.lod >= MAX_PLAYABLE_LOD {
        return None;
    }

    Some(TerrainNodeKey::new(
        key.lod + 1,
        TerrainNodeCoord::new(
            key.coord.x.div_euclid(2),
            key.coord.y.div_euclid(2),
            key.coord.z.div_euclid(2),
        ),
    ))
}

/// Returns the eight direct child nodes covered by `parent`.
pub fn terrain_node_children(parent: TerrainNodeKey) -> Option<[TerrainNodeKey; 8]> {
    if parent.lod == 0 {
        return None;
    }

    let child_lod = parent.lod - 1;
    let base_x = parent.coord.x * 2;
    let base_y = parent.coord.y * 2;
    let base_z = parent.coord.z * 2;
    Some([
        TerrainNodeKey::new(child_lod, TerrainNodeCoord::new(base_x, base_y, base_z)),
        TerrainNodeKey::new(child_lod, TerrainNodeCoord::new(base_x + 1, base_y, base_z)),
        TerrainNodeKey::new(child_lod, TerrainNodeCoord::new(base_x, base_y + 1, base_z)),
        TerrainNodeKey::new(
            child_lod,
            TerrainNodeCoord::new(base_x + 1, base_y + 1, base_z),
        ),
        TerrainNodeKey::new(child_lod, TerrainNodeCoord::new(base_x, base_y, base_z + 1)),
        TerrainNodeKey::new(
            child_lod,
            TerrainNodeCoord::new(base_x + 1, base_y, base_z + 1),
        ),
        TerrainNodeKey::new(
            child_lod,
            TerrainNodeCoord::new(base_x, base_y + 1, base_z + 1),
        ),
        TerrainNodeKey::new(
            child_lod,
            TerrainNodeCoord::new(base_x + 1, base_y + 1, base_z + 1),
        ),
    ])
}

/// Returns the parent/child group for `parent`.
pub fn terrain_child_group(parent: TerrainNodeKey) -> Option<TerrainChildGroup> {
    terrain_node_children(parent).map(|children| TerrainChildGroup { parent, children })
}

/// Returns desired child nodes for a cubic parent region.
///
/// A normal terrain refinement step uses `TerrainParentRegion::three_by_three`.
/// Invalid regions, negative radii, and LOD0 parent regions produce no child
/// nodes because LOD0 has no finer playable children.
pub fn desired_children_for_parent_region(region: TerrainParentRegion) -> Vec<TerrainNodeKey> {
    if region.radius < 0 || region.lod == 0 {
        return Vec::new();
    }

    let mut children = BTreeSet::new();
    for parent_x in region.center.x - region.radius..=region.center.x + region.radius {
        for parent_y in region.center.y - region.radius..=region.center.y + region.radius {
            for parent_z in region.center.z - region.radius..=region.center.z + region.radius {
                let parent = TerrainNodeKey::new(
                    region.lod,
                    TerrainNodeCoord::new(parent_x, parent_y, parent_z),
                );
                if let Some(parent_children) = terrain_node_children(parent) {
                    children.extend(parent_children);
                }
            }
        }
    }

    children.into_iter().collect()
}

/// Returns whether a parent can be replaced by its child group.
pub fn child_group_can_replace_parent(children: [TerrainNodeReadiness; 8]) -> bool {
    children.iter().all(|state| {
        matches!(
            state,
            TerrainNodeReadiness::Generated | TerrainNodeReadiness::Empty
        )
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn terrain_node_metrics_match_rebuild_spec() {
        assert_eq!(TERRAIN_NODE_CELLS_PER_AXIS, 32);
        assert_eq!(TERRAIN_NODE_SAMPLES_PER_AXIS, 33);

        let lod0 = terrain_node_metrics(0);
        assert_eq!(lod0.node_size_meters, 32.0);
        assert_eq!(lod0.cell_size_meters, 1.0);
        assert_eq!(lod0.cells_per_axis, 32);
        assert_eq!(lod0.samples_per_axis, 33);

        let lod5 = terrain_node_metrics(MAX_PLAYABLE_LOD);
        assert_eq!(lod5.node_size_meters, 1024.0);
        assert_eq!(lod5.cell_size_meters, 32.0);
    }

    #[test]
    fn terrain_node_parent_maps_negative_coords_with_floor_division() {
        let child = TerrainNodeKey::new(0, TerrainNodeCoord::new(-1, -2, -3));
        let parent = terrain_node_parent(child).expect("LOD0 should have a parent");
        assert_eq!(
            parent,
            TerrainNodeKey::new(1, TerrainNodeCoord::new(-1, -1, -2))
        );

        let children = terrain_node_children(parent).expect("LOD1 should have children");
        assert!(children.contains(&child));
    }

    #[test]
    fn lod5_is_the_coarsest_playable_infinite_grid() {
        let key = TerrainNodeKey::new(MAX_PLAYABLE_LOD, TerrainNodeCoord::new(42, -7, 13));
        assert_eq!(terrain_node_parent(key), None);
        assert_eq!(terrain_node_debug_key(key), "lod5:42,-7,13");
    }

    #[test]
    fn terrain_node_children_cover_a_two_by_two_by_two_grid() {
        let parent = TerrainNodeKey::new(3, TerrainNodeCoord::new(-2, 4, 9));
        let children = terrain_node_children(parent).expect("LOD3 should have children");
        assert_eq!(children.len(), 8);
        assert_eq!(
            children[0],
            TerrainNodeKey::new(2, TerrainNodeCoord::new(-4, 8, 18))
        );
        assert_eq!(
            children[7],
            TerrainNodeKey::new(2, TerrainNodeCoord::new(-3, 9, 19))
        );
        assert!(children
            .iter()
            .all(|child| terrain_node_parent(*child) == Some(parent)));
    }

    #[test]
    fn desired_children_for_parent_region_uses_three_by_three_parent_grid() {
        let region = TerrainParentRegion::three_by_three(1, TerrainNodeCoord::new(0, 0, 0));
        let children = desired_children_for_parent_region(region);
        let unique_children = children.iter().copied().collect::<BTreeSet<_>>();

        assert_eq!(children.len(), 27 * 8);
        assert_eq!(unique_children.len(), children.len());
        assert!(children.contains(&TerrainNodeKey::new(0, TerrainNodeCoord::new(-2, -2, -2))));
        assert!(children.contains(&TerrainNodeKey::new(0, TerrainNodeCoord::new(1, 1, 1))));
    }

    #[test]
    fn desired_children_for_lod0_parent_region_is_empty() {
        let region = TerrainParentRegion::three_by_three(0, TerrainNodeCoord::new(0, 0, 0));
        assert!(desired_children_for_parent_region(region).is_empty());
    }

    #[test]
    fn child_group_readiness_requires_every_child_generated_or_empty() {
        let ready = [
            TerrainNodeReadiness::Generated,
            TerrainNodeReadiness::Generated,
            TerrainNodeReadiness::Generated,
            TerrainNodeReadiness::Generated,
            TerrainNodeReadiness::Empty,
            TerrainNodeReadiness::Generated,
            TerrainNodeReadiness::Empty,
            TerrainNodeReadiness::Generated,
        ];
        assert!(child_group_can_replace_parent(ready));

        let mut missing = ready;
        missing[3] = TerrainNodeReadiness::Missing;
        assert!(!child_group_can_replace_parent(missing));
    }
}
