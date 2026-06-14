// Deferred terrain mesh removal helpers used by the Rust renderer upload queue.

use std::collections::VecDeque;
#[cfg(test)]
use std::sync::Arc;

use terrain_core::{terrain_node_parent, TerrainNodeKey};
#[cfg(test)]
use terrain_core::{MeshData, TerrainChunkCoord};

use crate::terrain_stream::BrowserTerrainMeshUpdate;

/// Pops the first terrain removal that is not waiting for an overlapping upload.
pub(crate) fn pop_ready_terrain_removal(
    pending_removals: &mut VecDeque<TerrainNodeKey>,
    pending_uploads: &VecDeque<BrowserTerrainMeshUpdate>,
) -> Option<TerrainNodeKey> {
    let candidate_count = pending_removals.len();
    for _ in 0..candidate_count {
        let key = pending_removals.pop_front()?;
        if terrain_removal_waits_for_pending_upload(key, pending_uploads) {
            pending_removals.push_back(key);
            continue;
        }

        return Some(key);
    }

    None
}

fn terrain_removal_waits_for_pending_upload(
    key: TerrainNodeKey,
    pending_uploads: &VecDeque<BrowserTerrainMeshUpdate>,
) -> bool {
    pending_uploads
        .iter()
        .any(|mesh_update| terrain_nodes_hierarchy_conflict(key, mesh_update.key))
}

fn terrain_nodes_hierarchy_conflict(left: TerrainNodeKey, right: TerrainNodeKey) -> bool {
    left == right || terrain_node_is_ancestor(left, right) || terrain_node_is_ancestor(right, left)
}

fn terrain_node_is_ancestor(ancestor: TerrainNodeKey, child: TerrainNodeKey) -> bool {
    let mut current = terrain_node_parent(child);
    while let Some(parent) = current {
        if parent == ancestor {
            return true;
        }
        current = terrain_node_parent(parent);
    }

    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hierarchy_conflicts_cover_parent_child_replacements() {
        let parent = node(1, 0, 0, 0);
        let child = node(0, 0, 0, 0);
        let distant = node(0, 8, 0, 8);

        assert!(terrain_nodes_hierarchy_conflict(parent, child));
        assert!(terrain_nodes_hierarchy_conflict(child, parent));
        assert!(!terrain_nodes_hierarchy_conflict(parent, distant));
    }

    #[test]
    fn terrain_removals_wait_for_pending_replacement_uploads() {
        let parent = node(1, 0, 0, 0);
        let child = node(0, 0, 0, 0);
        let unrelated = node(0, 8, 0, 8);
        let mut pending_removals = VecDeque::from([parent, unrelated]);
        let pending_uploads = VecDeque::from([pending_upload(child)]);

        assert_eq!(
            pop_ready_terrain_removal(&mut pending_removals, &pending_uploads),
            Some(unrelated)
        );
        assert_eq!(
            pending_removals.iter().copied().collect::<Vec<_>>(),
            vec![parent]
        );
        assert_eq!(
            pop_ready_terrain_removal(&mut pending_removals, &pending_uploads),
            None
        );

        let pending_uploads = VecDeque::new();
        assert_eq!(
            pop_ready_terrain_removal(&mut pending_removals, &pending_uploads),
            Some(parent)
        );
    }

    fn pending_upload(key: TerrainNodeKey) -> BrowserTerrainMeshUpdate {
        BrowserTerrainMeshUpdate {
            key,
            mesh: Arc::new(MeshData {
                vertices: Vec::new(),
                indices: Vec::new(),
            }),
        }
    }

    fn node(lod: u8, x: i32, y: i32, z: i32) -> TerrainNodeKey {
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord { x, y, z },
        }
    }
}
