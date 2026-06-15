// Disabled terrain edge-transition mesh bridge for the clean terrain rebuild.
//
// The new baseline does not generate apron or zipper meshes. Renderer-facing
// transition packets remain as empty compatibility shapes until dissolve
// transitions are wired into the shader path.

#![allow(dead_code)]

use std::collections::{BTreeMap, BTreeSet};
use std::sync::Arc;

use terrain_core::{terrain_node_key, MeshData, TerrainNodeKey, TerrainTransitionMeshKey};

pub struct BrowserTerrainTransitionMeshUpdate {
    pub key: TerrainTransitionMeshKey,
    pub mesh: Arc<MeshData>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
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
pub(crate) struct TerrainTransitionMeshCache;

impl TerrainTransitionMeshCache {
    pub(crate) fn clear(&mut self) {}

    pub(crate) fn retain_desired(
        &mut self,
        _desired_nodes: &BTreeSet<TerrainNodeKey>,
        _visible_nodes: &BTreeSet<TerrainNodeKey>,
    ) {
    }

    pub(crate) fn remove_referencing(&mut self, _key: TerrainNodeKey) {}

    pub(crate) fn sync(
        &mut self,
        _visible_nodes: &BTreeSet<TerrainNodeKey>,
        _mesh_cache: &BTreeMap<TerrainNodeKey, Arc<MeshData>>,
        _base_cell_size: f64,
    ) -> TerrainTransitionMeshDelta {
        TerrainTransitionMeshDelta::default()
    }

    pub(crate) fn counter_totals(&self) -> TerrainTransitionCounterTotals {
        TerrainTransitionCounterTotals::default()
    }
}

pub(crate) fn transition_mesh_key(key: TerrainTransitionMeshKey) -> String {
    format!(
        "transition:{}:{:?}",
        terrain_node_key(key.fine_key),
        key.face
    )
}
