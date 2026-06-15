// Runtime helpers for optional LOD transition edge meshes.
//
// Transition meshes are Rust-owned derived terrain geometry. They are keyed
// separately from canonical terrain nodes so visibility changes can toggle them
// without regenerating or replacing the child node mesh.

use std::collections::{BTreeMap, BTreeSet};
use std::sync::Arc;

#[cfg(any(target_arch = "wasm32", test))]
use terrain_core::terrain_node_key;
use terrain_core::{
    build_parent_lod_transition_edge_mesh, terrain_node_cell_size, MeshData, TerrainChunkCoord,
    TerrainNodeKey, TerrainTransitionFace, TerrainTransitionMeshConfig, TerrainTransitionMeshInput,
    TerrainTransitionMeshKey,
};

pub struct BrowserTerrainTransitionMeshUpdate {
    pub key: TerrainTransitionMeshKey,
    pub mesh: Arc<MeshData>,
}

#[derive(Default)]
pub(crate) struct TerrainTransitionCounterTotals {
    pub(crate) face_count: usize,
    pub(crate) mesh_count: usize,
    pub(crate) vertex_float_count: usize,
    pub(crate) index_count: usize,
}

#[derive(Default)]
pub(crate) struct TerrainTransitionMeshDelta {
    pub(crate) removed: Vec<TerrainTransitionMeshKey>,
    pub(crate) upserted: Vec<BrowserTerrainTransitionMeshUpdate>,
}

#[derive(Default)]
pub(crate) struct TerrainTransitionMeshCache {
    meshes: BTreeMap<TerrainTransitionMeshKey, Arc<MeshData>>,
    visible: BTreeSet<TerrainTransitionMeshKey>,
}

impl TerrainTransitionMeshCache {
    pub(crate) fn clear(&mut self) {
        self.meshes.clear();
        self.visible.clear();
    }

    pub(crate) fn counter_totals(&self) -> TerrainTransitionCounterTotals {
        transition_counter_totals(&self.visible, &self.meshes)
    }

    pub(crate) fn retain_desired(
        &mut self,
        desired: &BTreeSet<TerrainNodeKey>,
        visible: &BTreeSet<TerrainNodeKey>,
    ) {
        self.meshes.retain(|key, _mesh| {
            (desired.contains(&key.fine_key) || visible.contains(&key.fine_key))
                && (desired.contains(&key.parent_key) || visible.contains(&key.parent_key))
        });
    }

    pub(crate) fn remove_referencing(&mut self, node: TerrainNodeKey) {
        self.meshes
            .retain(|key, _mesh| key.fine_key != node && key.parent_key != node);
    }

    pub(crate) fn sync(
        &mut self,
        visible_nodes: &BTreeSet<TerrainNodeKey>,
        mesh_cache: &BTreeMap<TerrainNodeKey, Arc<MeshData>>,
        base_cell_size: f64,
    ) -> TerrainTransitionMeshDelta {
        let desired_transitions =
            required_transition_faces(visible_nodes, |key| mesh_cache.contains_key(&key))
                .into_iter()
                .collect::<BTreeSet<_>>();
        self.meshes
            .retain(|key, _mesh| desired_transitions.contains(key));

        let mut active_transitions = BTreeSet::new();
        let mut rebuilt_transitions = BTreeSet::new();
        for key in desired_transitions.iter().copied() {
            let cached = self.meshes.contains_key(&key);
            if !cached {
                self.cache_mesh(key, mesh_cache, base_cell_size);
            }
            if self.meshes.contains_key(&key) {
                if !cached {
                    rebuilt_transitions.insert(key);
                }
                active_transitions.insert(key);
            }
        }

        let mut delta = TerrainTransitionMeshDelta::default();
        delta
            .removed
            .extend(self.visible.difference(&active_transitions).copied());

        for key in active_transitions.iter().copied() {
            if !self.visible.contains(&key) || rebuilt_transitions.contains(&key) {
                if let Some(mesh) = self.meshes.get(&key) {
                    delta.upserted.push(BrowserTerrainTransitionMeshUpdate {
                        key,
                        mesh: Arc::clone(mesh),
                    });
                }
            }
        }

        self.visible = active_transitions;
        delta
    }

    fn cache_mesh(
        &mut self,
        key: TerrainTransitionMeshKey,
        mesh_cache: &BTreeMap<TerrainNodeKey, Arc<MeshData>>,
        base_cell_size: f64,
    ) {
        let Some(fine_mesh) = mesh_cache.get(&key.fine_key) else {
            return;
        };
        let Some(parent_mesh) = mesh_cache.get(&key.parent_key) else {
            return;
        };
        let fine_node_cell_size = terrain_node_cell_size(base_cell_size, key.fine_key.lod);
        let parent_node_cell_size = terrain_node_cell_size(base_cell_size, key.parent_key.lod);
        let Some(mesh) = build_parent_lod_transition_edge_mesh(TerrainTransitionMeshInput {
            fine_key: key.fine_key,
            parent_key: key.parent_key,
            face: key.face,
            fine_node_cell_size,
            parent_node_cell_size,
            fine_mesh,
            parent_mesh,
            config: TerrainTransitionMeshConfig::default(),
        }) else {
            return;
        };

        self.meshes.insert(key, Arc::new(mesh));
    }
}

/// Returns the transition faces needed by the current non-overlapping visible cover.
pub(crate) fn required_transition_faces(
    visible_nodes: &BTreeSet<TerrainNodeKey>,
    mesh_available: impl Fn(TerrainNodeKey) -> bool,
) -> Vec<TerrainTransitionMeshKey> {
    let mut transitions = Vec::new();
    for fine_key in visible_nodes {
        let Some(parent_key) = terrain_core::terrain_node_parent(*fine_key) else {
            continue;
        };
        if !mesh_available(*fine_key) || !mesh_available(parent_key) {
            continue;
        }

        for face in parent_outer_faces(*fine_key) {
            let neighbor = same_lod_neighbor(*fine_key, face);
            if visible_nodes.contains(&neighbor) {
                continue;
            }

            transitions.push(TerrainTransitionMeshKey {
                fine_key: *fine_key,
                parent_key,
                face,
            });
        }
    }

    transitions
}

/// Aggregates active transition meshes into stream/debug counters.
pub(crate) fn transition_counter_totals(
    visible_transitions: &BTreeSet<TerrainTransitionMeshKey>,
    meshes: &BTreeMap<TerrainTransitionMeshKey, Arc<MeshData>>,
) -> TerrainTransitionCounterTotals {
    visible_transitions.iter().fold(
        TerrainTransitionCounterTotals {
            face_count: visible_transitions.len(),
            ..TerrainTransitionCounterTotals::default()
        },
        |mut total, key| {
            if let Some(mesh) = meshes.get(key) {
                total.mesh_count += 1;
                total.vertex_float_count += mesh.vertices.len();
                total.index_count += mesh.indices.len();
            }
            total
        },
    )
}

/// Returns a renderer/debug key for a derived transition mesh.
#[cfg(any(target_arch = "wasm32", test))]
pub(crate) fn transition_mesh_key(key: TerrainTransitionMeshKey) -> String {
    format!(
        "terrain-transition:{}:{}:{}",
        terrain_node_key(key.fine_key),
        terrain_node_key(key.parent_key),
        transition_face_label(key.face)
    )
}

fn parent_outer_faces(key: TerrainNodeKey) -> Vec<TerrainTransitionFace> {
    let mut faces = Vec::with_capacity(4);
    if key.coord.x.rem_euclid(2) == 0 {
        faces.push(TerrainTransitionFace::NegX);
    } else {
        faces.push(TerrainTransitionFace::PosX);
    }
    if key.coord.z.rem_euclid(2) == 0 {
        faces.push(TerrainTransitionFace::NegZ);
    } else {
        faces.push(TerrainTransitionFace::PosZ);
    }

    faces
}

fn same_lod_neighbor(key: TerrainNodeKey, face: TerrainTransitionFace) -> TerrainNodeKey {
    let mut coord = key.coord;
    match face {
        TerrainTransitionFace::NegX => coord.x -= 1,
        TerrainTransitionFace::PosX => coord.x += 1,
        TerrainTransitionFace::NegZ => coord.z -= 1,
        TerrainTransitionFace::PosZ => coord.z += 1,
    }

    TerrainNodeKey {
        lod: key.lod,
        coord: TerrainChunkCoord {
            x: coord.x,
            y: coord.y,
            z: coord.z,
        },
    }
}

#[cfg(any(target_arch = "wasm32", test))]
fn transition_face_label(face: TerrainTransitionFace) -> &'static str {
    match face {
        TerrainTransitionFace::NegX => "neg-x",
        TerrainTransitionFace::PosX => "pos-x",
        TerrainTransitionFace::NegZ => "neg-z",
        TerrainTransitionFace::PosZ => "pos-z",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn required_transition_faces_only_include_parent_region_outer_faces() {
        let fine = node(0, 0, 0, 0);
        let parent = node(1, 0, 0, 0);
        let visible = BTreeSet::from([fine]);

        let transitions = required_transition_faces(&visible, |key| key == fine || key == parent);

        assert_eq!(
            transitions,
            vec![
                TerrainTransitionMeshKey {
                    fine_key: fine,
                    parent_key: parent,
                    face: TerrainTransitionFace::NegX,
                },
                TerrainTransitionMeshKey {
                    fine_key: fine,
                    parent_key: parent,
                    face: TerrainTransitionFace::NegZ,
                },
            ]
        );
    }

    #[test]
    fn required_transition_faces_skip_same_lod_visible_neighbors() {
        let fine = node(0, 0, 0, 0);
        let same_lod_neg_x_neighbor = node(0, -1, 0, 0);
        let parent = node(1, 0, 0, 0);
        let neighbor_parent = node(1, -1, 0, 0);
        let visible = BTreeSet::from([fine, same_lod_neg_x_neighbor]);

        let transitions = required_transition_faces(
            &visible,
            |key| matches!(key, k if k == fine || k == parent || k == same_lod_neg_x_neighbor || k == neighbor_parent),
        );

        assert!(!transitions
            .iter()
            .any(|key| { key.fine_key == fine && key.face == TerrainTransitionFace::NegX }));
        assert!(transitions
            .iter()
            .any(|key| { key.fine_key == fine && key.face == TerrainTransitionFace::NegZ }));
    }

    #[test]
    fn transition_counter_totals_include_only_active_meshes() {
        let transition = TerrainTransitionMeshKey {
            fine_key: node(0, 0, 0, 0),
            parent_key: node(1, 0, 0, 0),
            face: TerrainTransitionFace::NegX,
        };
        let missing = TerrainTransitionMeshKey {
            fine_key: node(0, 0, 0, 0),
            parent_key: node(1, 0, 0, 0),
            face: TerrainTransitionFace::NegZ,
        };
        let visible = BTreeSet::from([transition, missing]);
        let meshes = BTreeMap::from([(
            transition,
            Arc::new(MeshData {
                vertices: vec![0.0; 38],
                indices: vec![0, 1, 2],
            }),
        )]);

        let totals = transition_counter_totals(&visible, &meshes);

        assert_eq!(totals.face_count, 2);
        assert_eq!(totals.mesh_count, 1);
        assert_eq!(totals.vertex_float_count, 38);
        assert_eq!(totals.index_count, 3);
    }

    #[test]
    fn transition_mesh_keys_are_namespaced_and_stable() {
        let key = TerrainTransitionMeshKey {
            fine_key: node(0, -1, 0, 2),
            parent_key: node(1, -1, 0, 1),
            face: TerrainTransitionFace::NegX,
        };

        assert_eq!(
            transition_mesh_key(key),
            "terrain-transition:lod0:-1,0,2:lod1:-1,0,1:neg-x"
        );
    }

    fn node(lod: u8, x: i32, y: i32, z: i32) -> TerrainNodeKey {
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord { x, y, z },
        }
    }
}
