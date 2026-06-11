// Sea-level helpers for renderer-facing water and bathymetry probes.
// Terrain jobs use this module to produce small node-local water packets while
// the renderer remains responsible for WebGPU texture and plane resources.

use crate::{
    density_at_position_with_macro, height_at_for_variant, sample_macro_terrain, SimplexNoise3D,
    TerrainNodeKey, TerrainPresetDefinition, TerrainVariantDescriptor,
    TerrainVariantValidationError, Vec3, TERRAIN_CHUNK_CELLS_PER_AXIS,
};

pub const SEA_LEVEL_METERS: f64 = 0.0;
pub const WATER_NODE_BATHYMETRY_TEXEL_COUNT: u32 = 32;
pub const WATER_NODE_MAX_RELEVANT_DEPTH_METERS: f64 = 64.0;
const WATER_HEIGHT_BISECTION_STEPS: usize = 8;

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

/// Returns vertical sea depth at a world XZ point for the active terrain variant.
pub fn sea_depth_at_for_variant(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    x: f64,
    z: f64,
    sea_level: f64,
) -> Result<f64, TerrainVariantValidationError> {
    let terrain_height = height_at_for_variant(seed, descriptor, x, z)?;
    Ok((sea_level - terrain_height).max(0.0))
}

/// Builds a node-local sea-depth texture for the vertical node containing sea level.
pub fn build_water_node_packet_for_variant(
    seed: u32,
    descriptor: TerrainVariantDescriptor,
    key: TerrainNodeKey,
    cell_size: f64,
    sea_level: f64,
    max_depth_meters: f64,
) -> Result<Option<WaterNodePacket>, TerrainVariantValidationError> {
    descriptor.validate()?;
    if !max_depth_meters.is_finite() || max_depth_meters <= 0.0 {
        return Ok(None);
    }
    let Some(bounds) = water_node_bounds(key, cell_size, sea_level) else {
        return Ok(None);
    };

    let noise = SimplexNoise3D::new(seed);
    let shape = descriptor.shape;
    let texel_count = WATER_NODE_BATHYMETRY_TEXEL_COUNT;
    let texel_count_usize = texel_count as usize;
    let texel_size_x = bounds.world_span_x / f64::from(texel_count);
    let texel_size_z = bounds.world_span_z / f64::from(texel_count);
    let mut depths_meters = Vec::with_capacity(texel_count_usize * texel_count_usize);
    let mut packet_max_depth_meters = 0.0_f32;

    for row in 0..texel_count {
        let z = bounds.origin_z + (f64::from(row) + 0.5) * texel_size_z;
        for column in 0..texel_count {
            let x = bounds.origin_x + (f64::from(column) + 0.5) * texel_size_x;
            let depth = bounded_sea_depth_at_for_shape(
                &noise,
                shape,
                seed,
                x,
                z,
                sea_level,
                max_depth_meters,
            ) as f32;
            packet_max_depth_meters = packet_max_depth_meters.max(depth);
            depths_meters.push(depth);
        }
    }

    if packet_max_depth_meters <= 0.0 {
        return Ok(None);
    }

    Ok(Some(WaterNodePacket {
        texel_count,
        origin_x: bounds.origin_x as f32,
        origin_z: bounds.origin_z as f32,
        world_span_x: bounds.world_span_x as f32,
        world_span_z: bounds.world_span_z as f32,
        sea_level_meters: sea_level as f32,
        max_depth_meters: packet_max_depth_meters,
        depths_meters,
    }))
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct WaterNodeBounds {
    origin_x: f64,
    origin_z: f64,
    world_span_x: f64,
    world_span_z: f64,
}

fn bounded_sea_depth_at_for_shape(
    noise: &SimplexNoise3D,
    shape: TerrainPresetDefinition,
    seed: u32,
    x: f64,
    z: f64,
    sea_level: f64,
    max_depth: f64,
) -> f64 {
    let macro_sample = sample_macro_terrain(noise, shape, seed, Vec3 { x, y: 0.0, z });
    let sea_density =
        density_at_position_with_macro(noise, shape, Vec3 { x, y: sea_level, z }, macro_sample)
            .density;
    if sea_density <= 0.0 {
        return 0.0;
    }

    let bottom_y = sea_level - max_depth.max(0.0);
    let bottom_density =
        density_at_position_with_macro(noise, shape, Vec3 { x, y: bottom_y, z }, macro_sample)
            .density;
    if bottom_density > 0.0 {
        return max_depth;
    }

    let mut solid_y = bottom_y;
    let mut air_y = sea_level;
    for _ in 0..WATER_HEIGHT_BISECTION_STEPS {
        let mid_y = (solid_y + air_y) * 0.5;
        let mid_density =
            density_at_position_with_macro(noise, shape, Vec3 { x, y: mid_y, z }, macro_sample)
                .density;
        if mid_density <= 0.0 {
            solid_y = mid_y;
        } else {
            air_y = mid_y;
        }
    }

    (sea_level - (solid_y + air_y) * 0.5).clamp(0.0, max_depth)
}

fn water_node_bounds(
    key: TerrainNodeKey,
    cell_size: f64,
    sea_level: f64,
) -> Option<WaterNodeBounds> {
    if !cell_size.is_finite() || cell_size <= 0.0 || !sea_level.is_finite() {
        return None;
    }

    let node_size = cell_size * TERRAIN_CHUNK_CELLS_PER_AXIS as f64;
    let origin_y = key.coord.y as f64 * node_size;
    let end_y = origin_y + node_size;
    if sea_level < origin_y || sea_level >= end_y {
        return None;
    }

    Some(WaterNodeBounds {
        origin_x: key.coord.x as f64 * node_size,
        origin_z: key.coord.z as f64 * node_size,
        world_span_x: node_size,
        world_span_z: node_size,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{terrain_variant_for_preset, TerrainChunkCoord};

    #[test]
    fn sea_depth_is_zero_when_terrain_is_above_sea_level() {
        let mut variant = terrain_variant_for_preset(1);
        variant.shape.base_height = 80.0;

        let depth = sea_depth_at_for_variant(0x0F6, variant, 0.0, 0.0, SEA_LEVEL_METERS).unwrap();

        assert_eq!(depth, 0.0);
    }

    #[test]
    fn sea_depth_matches_positive_vertical_gap_below_sea_level() {
        let variant = terrain_variant_for_preset(1);
        let height = height_at_for_variant(0x0F6, variant, 12.0, -8.0).unwrap();
        let sea_level = height + 5.5;

        let depth = sea_depth_at_for_variant(0x0F6, variant, 12.0, -8.0, sea_level).unwrap();

        assert!((depth - 5.5).abs() < 0.000001);
    }

    #[test]
    fn sea_depth_validates_the_variant_descriptor() {
        let mut variant = terrain_variant_for_preset(1);
        variant.shape.base_height = f64::NAN;

        let error = sea_depth_at_for_variant(0x0F6, variant, 0.0, 0.0, SEA_LEVEL_METERS)
            .expect_err("invalid variant should be rejected");

        assert_eq!(error, TerrainVariantValidationError::InvalidBaseHeight);
    }

    #[test]
    fn water_node_packet_samples_only_sea_level_vertical_owner() {
        let mut variant = terrain_variant_for_preset(1);
        variant.shape.base_height = -6.0;
        let key = node(0, 0, 0, 0);

        let packet = build_water_node_packet_for_variant(
            0x0F6,
            variant,
            key,
            1.0,
            SEA_LEVEL_METERS,
            WATER_NODE_MAX_RELEVANT_DEPTH_METERS,
        )
        .unwrap()
        .expect("sea-level node should contain water");

        assert_eq!(packet.texel_count, WATER_NODE_BATHYMETRY_TEXEL_COUNT);
        assert_eq!(
            packet.depths_meters.len(),
            (WATER_NODE_BATHYMETRY_TEXEL_COUNT * WATER_NODE_BATHYMETRY_TEXEL_COUNT) as usize
        );
        assert_eq!(packet.origin_x, 0.0);
        assert_eq!(packet.origin_z, 0.0);
        assert_eq!(packet.world_span_x, TERRAIN_CHUNK_CELLS_PER_AXIS as f32);
        assert!(packet.max_depth_meters > 0.0);

        assert_eq!(
            build_water_node_packet_for_variant(
                0x0F6,
                variant,
                node(0, 0, -1, 0),
                1.0,
                0.0,
                WATER_NODE_MAX_RELEVANT_DEPTH_METERS,
            )
            .unwrap(),
            None
        );
    }

    #[test]
    fn water_node_packet_omits_dry_nodes() {
        let mut variant = terrain_variant_for_preset(1);
        variant.shape.base_height = 80.0;

        assert_eq!(
            build_water_node_packet_for_variant(
                0x0F6,
                variant,
                node(0, 0, 0, 0),
                1.0,
                0.0,
                WATER_NODE_MAX_RELEVANT_DEPTH_METERS,
            )
            .unwrap(),
            None
        );
    }

    #[test]
    fn water_node_packet_clamps_depths_to_render_relevant_range() {
        let mut variant = terrain_variant_for_preset(1);
        variant.shape.base_height = -256.0;

        let packet = build_water_node_packet_for_variant(
            0x0F6,
            variant,
            node(0, 0, 0, 0),
            1.0,
            0.0,
            WATER_NODE_MAX_RELEVANT_DEPTH_METERS,
        )
        .unwrap()
        .expect("deep water should still emit a capped water packet");

        assert_eq!(
            packet.max_depth_meters,
            WATER_NODE_MAX_RELEVANT_DEPTH_METERS as f32
        );
        assert!(packet
            .depths_meters
            .iter()
            .all(|depth| *depth <= WATER_NODE_MAX_RELEVANT_DEPTH_METERS as f32));
    }

    fn node(lod: u8, x: i32, y: i32, z: i32) -> TerrainNodeKey {
        TerrainNodeKey {
            lod,
            coord: TerrainChunkCoord { x, y, z },
        }
    }
}
