//! Lean terrain core for the active terrain rebuild.
//!
//! The legacy terrain implementation lives under
//! `docs/reference/terrain_legacy_2026_06_15/`. Active code in this crate starts
//! again from a deliberately small baseline: 3D multi-LOD node identity, a sine
//! wave heightfield, grass-only mesh packets, and a minimal stream state machine
//! that keeps parent cover until child groups are ready.

#[cfg(feature = "benchmark")]
pub mod benchmark;
mod facade;
mod heightfield;
mod mesh;
mod node;
mod stream;
mod variant;

pub use heightfield::{height_at, height_at_for_variant, height_at_with_shape};
pub use mesh::{
    build_chunk_mesh, build_node_mesh, build_node_mesh_for_variant, mesh_height_at, MeshData,
    TERRAIN_VERTEX_FLOATS,
};
pub use node::{
    terrain_chunk_coord_containing_position, terrain_chunk_key, terrain_node_cell_size,
    terrain_node_children, terrain_node_coord_for_lod, terrain_node_key, terrain_node_parent,
    TerrainChunkCoord, TerrainNodeKey, DEFAULT_TERRAIN_PRESET, LOD0_NODE_SIZE_METERS,
    MAX_PLAYABLE_LOD, TERRAIN_CHUNK_CELLS_PER_AXIS, TERRAIN_NODE_SAMPLES_PER_AXIS,
};
pub use stream::{
    TerrainLodBand, TerrainLodBoundedVerticalPolicy, TerrainLodStatus, TerrainLodVerticalPolicy,
    TerrainStreamConfig, TerrainStreamError, TerrainStreamJob, TerrainStreamScheduler,
    TerrainStreamStatus,
};
pub use variant::{
    terrain_preset_count, terrain_preset_metadata, terrain_variant_cache_key,
    terrain_variant_flat_values, terrain_variant_for_preset, terrain_variant_from_flat_values,
    terrain_variant_probe_summary, TerrainBiomeWeightsProbe, TerrainMaterialBias,
    TerrainPresetMetadata, TerrainShapeParameters, TerrainVariantDescriptor,
    TerrainVariantProbeSummary, TerrainVariantValidationError, TERRAIN_BASE_HEIGHT_MAX,
    TERRAIN_BASE_HEIGHT_MIN, TERRAIN_CELLULAR_HEIGHT_SCALE_MAX, TERRAIN_DETAIL_AMPLITUDE_MAX,
    TERRAIN_HEIGHT_SCALE_MAX, TERRAIN_HEIGHT_SCALE_MIN, TERRAIN_RIDGE_HEIGHT_SCALE_MAX,
    TERRAIN_VARIANT_DESCRIPTOR_VERSION, TERRAIN_VARIANT_FLAT_VALUE_COUNT,
    TERRAIN_WARP_AMPLITUDE_MAX,
};

/// Compatibility placeholder for disabled baseline water.
#[derive(Clone, Debug, PartialEq)]
pub struct WaterNodePacket {
    pub texel_count: u32,
    pub origin_x: f32,
    pub origin_z: f32,
    pub world_span_x: f32,
    pub world_span_z: f32,
    pub sea_level_meters: f32,
    pub max_depth_meters: f32,
    pub depths_meters: Vec<f32>,
}

pub const SEA_LEVEL_METERS: f64 = 0.0;
pub const WATER_NODE_BATHYMETRY_TEXEL_COUNT: u32 = 0;
pub const WATER_NODE_MAX_RELEVANT_DEPTH_METERS: f32 = 0.0;

/// Returns no water packet in the sine-wave terrain baseline.
pub fn build_water_node_packet_for_variant(
    _seed: u32,
    _variant: TerrainVariantDescriptor,
    _key: TerrainNodeKey,
    _cell_size: f64,
    _sea_level: f64,
    _max_depth_meters: f32,
) -> Result<Option<WaterNodePacket>, TerrainVariantValidationError> {
    Ok(None)
}

/// Returns zero sea depth because water is disabled in this baseline.
pub fn sea_depth_at_for_variant(
    _seed: u32,
    _variant: TerrainVariantDescriptor,
    _x: f64,
    _z: f64,
    _sea_level: f64,
) -> Result<f32, TerrainVariantValidationError> {
    Ok(0.0)
}

/// Compatibility placeholder for disabled placement sampling.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct TerrainPlacementSamplePacket {
    pub candidate_count: usize,
    pub samples: Vec<TerrainPlacementSample>,
    pub missed_surface_count: usize,
    pub rejected_below_water_count: usize,
    pub rejected_slope_count: usize,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TerrainPlacementSample {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

/// Compatibility placeholder for disabled edge transition meshes.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainTransitionMeshKey {
    pub fine_key: TerrainNodeKey,
    pub face: TerrainTransitionFace,
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum TerrainTransitionFace {
    NegativeX,
    PositiveX,
    NegativeY,
    PositiveY,
    NegativeZ,
    PositiveZ,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainTransitionMeshConfig;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TerrainTransitionMeshInput;

/// Returns an empty mesh because apron-style transition geometry is removed.
pub fn build_parent_lod_transition_edge_mesh(
    _input: TerrainTransitionMeshInput,
    _config: TerrainTransitionMeshConfig,
) -> MeshData {
    MeshData::default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn baseline_heightfield_is_sine_and_deterministic() {
        let variant = terrain_variant_for_preset(DEFAULT_TERRAIN_PRESET);
        let a = height_at_for_variant(7, variant, 12.0, -4.0).unwrap();
        let b = height_at_for_variant(7, variant, 12.0, -4.0).unwrap();
        let c = height_at_for_variant(8, variant, 12.0, -4.0).unwrap();

        assert_eq!(a, b);
        assert_ne!(a, c);
    }

    #[test]
    fn baseline_mesh_is_grass_only_and_empty_outside_vertical_span() {
        let key = TerrainNodeKey {
            lod: 0,
            coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
        };
        let mesh = build_node_mesh(0, DEFAULT_TERRAIN_PRESET, key, 1.0);
        assert!(!mesh.vertices.is_empty());
        assert!(!mesh.indices.is_empty());
        assert_eq!(mesh.vertices.len() % 19, 0);

        let high_key = TerrainNodeKey {
            lod: 0,
            coord: TerrainChunkCoord { x: 0, y: 100, z: 0 },
        };
        let empty = build_node_mesh(0, DEFAULT_TERRAIN_PRESET, high_key, 1.0);
        assert!(empty.indices.is_empty());
    }

    #[test]
    fn baseline_mesh_triangles_face_up_for_culled_rendering() {
        let key = TerrainNodeKey {
            lod: 0,
            coord: TerrainChunkCoord { x: 0, y: -1, z: 0 },
        };
        let mesh = build_node_mesh(0x0F6, DEFAULT_TERRAIN_PRESET, key, 1.0);
        let [a, b, c] = [mesh.indices[0], mesh.indices[1], mesh.indices[2]];
        let [ax, _, az] = mesh_vertex_position(&mesh, a);
        let [bx, _, bz] = mesh_vertex_position(&mesh, b);
        let [cx, _, cz] = mesh_vertex_position(&mesh, c);

        let ab_x = bx - ax;
        let ab_z = bz - az;
        let ac_x = cx - ax;
        let ac_z = cz - az;
        let normal_y = ab_z * ac_x - ab_x * ac_z;

        assert!(normal_y > 0.0);
    }

    #[test]
    fn mesh_height_query_uses_generated_triangle_vertices() {
        let seed = 0x0F6;
        let variant = terrain_variant_for_preset(DEFAULT_TERRAIN_PRESET);
        let key = TerrainNodeKey {
            lod: 0,
            coord: TerrainChunkCoord { x: 0, y: -1, z: 0 },
        };
        let mesh = build_node_mesh_for_variant(seed, variant, key, 1.0);

        let vertex_height = mesh.height_at(8.0, 12.0).unwrap();
        let expected_vertex_height =
            height_at_for_variant(seed, variant, 8.0, 12.0).unwrap() as f32;
        assert!((vertex_height - expected_vertex_height).abs() <= 0.0001);

        let triangle_height = mesh.height_at(0.25, 0.25).unwrap();
        let a_y = mesh.vertices[1];
        let b_y = mesh.vertices[TERRAIN_VERTEX_FLOATS + 1];
        let c_y =
            mesh.vertices[(TERRAIN_NODE_SAMPLES_PER_AXIS as usize * TERRAIN_VERTEX_FLOATS) + 1];
        let expected_triangle_height = a_y * 0.5 + b_y * 0.25 + c_y * 0.25;
        assert!((triangle_height - expected_triangle_height).abs() <= 0.0001);

        assert_eq!(mesh.height_at(-1.0, -1.0), None);
    }

    #[test]
    fn mesh_height_query_skips_malformed_triangles() {
        let mut vertices = Vec::new();
        for [x, y, z] in [[0.0, 1.0, 0.0], [1.0, 2.0, 0.0], [0.0, 3.0, 1.0]] {
            vertices.extend_from_slice(&[x, y, z]);
            vertices.resize(vertices.len() + TERRAIN_VERTEX_FLOATS - 3, 0.0);
        }
        let mesh = MeshData {
            vertices,
            indices: vec![99, 0, 1, 0, 1, 2],
        };

        assert!((mesh.height_at(0.25, 0.25).unwrap() - 1.75).abs() <= 0.0001);
    }

    fn mesh_vertex_position(mesh: &MeshData, index: u32) -> [f32; 3] {
        let start = index as usize * TERRAIN_VERTEX_FLOATS;
        [
            mesh.vertices[start],
            mesh.vertices[start + 1],
            mesh.vertices[start + 2],
        ]
    }

    #[test]
    fn stream_waits_for_child_group_before_replacing_parent() {
        let parent = TerrainNodeKey {
            lod: 1,
            coord: TerrainChunkCoord { x: 0, y: 0, z: 0 },
        };
        let mut stream = TerrainStreamScheduler::new(TerrainStreamConfig::default()).unwrap();
        stream.force_ready_for_test(parent, false);

        let children = terrain_node_children(parent).unwrap();
        for child in children.iter().take(7) {
            stream.force_ready_for_test(*child, false);
        }
        assert_eq!(stream.visible_cover_from(parent), vec![parent]);

        stream.force_ready_for_test(children[7], false);
        assert_eq!(stream.visible_cover_from(parent), children.to_vec());
    }
}
