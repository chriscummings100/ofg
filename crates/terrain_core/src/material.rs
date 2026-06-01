use crate::*;

#[derive(Clone, Copy)]
pub(crate) struct BiomeWeights {
    pub(crate) grassland: f64,
    pub(crate) temperate_forest: f64,
    pub(crate) wetland: f64,
    pub(crate) coast_beach: f64,
    pub(crate) dry_badland: f64,
    pub(crate) alpine_meadow: f64,
    pub(crate) high_mountain_rock: f64,
    pub(crate) snow_tundra: f64,
}

#[derive(Clone, Copy)]
pub(crate) struct PackedTerrainMaterial {
    pub(crate) indices: [f32; 4],
    pub(crate) weights: [f32; 4],
}

pub(crate) fn material_pack_at(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    position: Vec3,
) -> PackedTerrainMaterial {
    let macro_sample = sample_macro_terrain(noise, preset, seed, position);
    let density_sample = density_at_position_with_macro(noise, preset, position, macro_sample);
    let biome = biome_weights_at(noise, preset, seed, position, macro_sample);
    let normal = normalize_vec3(density_sample.gradient);
    let slope = clamp(1.0 - normal.y, 0.0, 1.0);
    let lowland = clamp((4.0 - position.y) / 8.0, 0.0, 1.0);
    let highland = clamp((position.y - 28.0) / 28.0, 0.0, 1.0);
    let cliff = smoothstep(0.62, 0.86, slope);
    let rocky = smoothstep(0.34, 0.68, slope) * (1.0 - cliff);
    let snow = smoothstep(38.0, 56.0, position.y) * smoothstep(0.1, 0.65, normal.y);
    let wet = lowland * smoothstep(0.12, 0.72, normal.y) * (1.0 - rocky) * (1.0 - cliff);
    let sand = clamp((2.5 - position.y.abs()) / 5.0, 0.0, 1.0)
        * smoothstep(0.18, 0.82, normal.y)
        * (0.45 + macro_sample.continentality * 0.25);
    let dry = clamp(
        0.35 + macro_sample.continentality * 0.45 - macro_sample.mountainness * 0.25,
        0.0,
        1.0,
    );
    let moss = clamp(
        (macro_sample.mountainness + macro_sample.ridge) * 0.35,
        0.0,
        0.8,
    ) * (1.0 - cliff)
        * (1.0 - snow);
    let red_soil = clamp((macro_sample.cellular_edge - 0.42) / 0.45, 0.0, 0.75)
        * dry
        * (1.0 - rocky)
        * (1.0 - snow);
    let meadow = (1.0 - dry * 0.55) * smoothstep(0.2, 0.85, normal.y) * (1.0 - wet) * (1.0 - snow);
    let dry_ground = dry * smoothstep(0.28, 0.88, normal.y) * (1.0 - wet) * (1.0 - snow);
    let scree = rocky * highland * 0.65;

    pack_material_weights(&[
        (
            0,
            meadow * (0.72 + biome.grassland * 0.42 + biome.alpine_meadow * 0.18),
        ),
        (1, dry_ground * (0.72 + biome.dry_badland * 0.65)),
        (
            2,
            (1.0 - dry) * 0.2 * (1.0 - rocky) * (1.0 - wet) + biome.temperate_forest * 0.45,
        ),
        (
            4,
            lowland * 0.28 * (1.0 - wet) * (1.0 - sand) + biome.wetland * 0.1,
        ),
        (6, wet + biome.wetland * 0.65),
        (7, sand + biome.coast_beach * 0.55),
        (8, sand * rocky * 0.8 + biome.coast_beach * rocky * 0.22),
        (10, scree + biome.high_mountain_rock * rocky * 0.28),
        (
            11,
            rocky * (1.0 - highland * 0.35) + biome.high_mountain_rock * 0.3,
        ),
        (12, cliff + biome.high_mountain_rock * cliff * 0.35),
        (
            13,
            moss + biome.temperate_forest * 0.16 + biome.alpine_meadow * 0.14,
        ),
        (14, red_soil + biome.dry_badland * 0.4),
        (15, snow + biome.snow_tundra * 0.85),
    ])
}

pub(crate) fn biome_weights_at(
    noise: &SimplexNoise3D,
    _preset: TerrainPresetDefinition,
    _seed: u32,
    position: Vec3,
    macro_sample: MacroTerrainSample,
) -> BiomeWeights {
    let climate_noise = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: position.x + 971.2,
            y: 43.5,
            z: position.z - 211.7,
        },
        FractalNoiseOptions {
            octaves: 3,
            frequency: 0.0025,
            lacunarity: 2.0,
            persistence: 0.52,
        },
    );
    let moisture_noise = sample_fractal_simplex_3d(
        noise,
        Vec3 {
            x: position.x - 317.6,
            y: -29.25,
            z: position.z + 513.4,
        },
        FractalNoiseOptions {
            octaves: 3,
            frequency: 0.0032,
            lacunarity: 2.0,
            persistence: 0.5,
        },
    );
    let altitude = position.y;
    let high = smoothstep(14.0, 34.0, altitude);
    let very_high = smoothstep(30.0, 52.0, altitude);
    let near_sea_level = clamp(1.0 - altitude.abs() / 8.0, 0.0, 1.0);
    let temperature = clamp(
        0.72 - high * 0.34 - very_high * 0.22 - macro_sample.continentality * 0.05
            + climate_noise.value * 0.12,
        0.0,
        1.0,
    );
    let moisture = clamp(
        0.42 + (1.0 - macro_sample.continentality) * 0.22
            + macro_sample.erosion_susceptibility * 0.12
            - high * 0.09
            + moisture_noise.value * 0.18,
        0.0,
        1.0,
    );
    let wetness = smoothstep(0.5, 0.78, moisture) * (1.0 - high * 0.75);
    let dryness = smoothstep(0.48, 0.76, macro_sample.continentality)
        * (1.0 - smoothstep(0.42, 0.68, moisture))
        * (1.0 - high * 0.35);
    let coast = near_sea_level * smoothstep(0.4, 0.82, moisture) * (1.0 - high);
    let mountain_rock =
        smoothstep(0.46, 0.76, macro_sample.mountainness) * smoothstep(10.0, 26.0, altitude);
    let snow = smoothstep(34.0, 54.0, altitude) * (1.0 - smoothstep(0.28, 0.58, temperature));
    let alpine = smoothstep(16.0, 34.0, altitude) * (1.0 - snow) * (1.0 - mountain_rock * 0.5);
    let forest = smoothstep(0.52, 0.78, moisture)
        * smoothstep(0.34, 0.72, temperature)
        * (1.0 - high * 0.7)
        * (1.0 - coast * 0.5)
        * (1.0 - dryness * 0.55);
    let grassland = (1.0 - high * 0.55)
        * (1.0 - wetness * 0.6)
        * (1.0 - dryness * 0.45)
        * (1.0 - forest * 0.45);

    normalize_biome_weights([
        grassland,
        forest,
        wetness * (1.0 - coast * 0.35),
        coast,
        dryness,
        alpine,
        mountain_rock * (1.0 - snow * 0.5),
        snow,
    ])
}

pub(crate) fn normalize_biome_weights(weights: [f64; 8]) -> BiomeWeights {
    let total: f64 = weights.iter().copied().filter(|weight| *weight > 0.0).sum();
    if total <= f64::EPSILON {
        return BiomeWeights {
            grassland: 1.0,
            temperate_forest: 0.0,
            wetland: 0.0,
            coast_beach: 0.0,
            dry_badland: 0.0,
            alpine_meadow: 0.0,
            high_mountain_rock: 0.0,
            snow_tundra: 0.0,
        };
    }

    BiomeWeights {
        grassland: positive_weight(weights[0]) / total,
        temperate_forest: positive_weight(weights[1]) / total,
        wetland: positive_weight(weights[2]) / total,
        coast_beach: positive_weight(weights[3]) / total,
        dry_badland: positive_weight(weights[4]) / total,
        alpine_meadow: positive_weight(weights[5]) / total,
        high_mountain_rock: positive_weight(weights[6]) / total,
        snow_tundra: positive_weight(weights[7]) / total,
    }
}

pub(crate) fn pack_material_weights(candidates: &[(usize, f64)]) -> PackedTerrainMaterial {
    let mut positive: Vec<(usize, f64, usize)> = candidates
        .iter()
        .enumerate()
        .filter_map(|(order, (layer, weight))| {
            if *weight > 0.0 {
                Some((*layer, *weight, order))
            } else {
                None
            }
        })
        .collect();

    if positive.is_empty() {
        return default_material_pack();
    }

    positive.sort_by(|a, b| {
        b.1.partial_cmp(&a.1)
            .unwrap_or(core::cmp::Ordering::Equal)
            .then_with(|| a.2.cmp(&b.2))
    });
    positive.truncate(4);
    let total: f64 = positive.iter().map(|(_, weight, _)| *weight).sum();
    if total <= f64::EPSILON {
        return default_material_pack();
    }

    let mut indices = [0.0_f32; 4];
    let mut weights = [0.0_f32; 4];
    for (slot, (layer, weight, _)) in positive.iter().enumerate() {
        indices[slot] = *layer as f32;
        weights[slot] = (*weight / total) as f32;
    }

    if weights[0] == 0.0 {
        return default_material_pack();
    }

    PackedTerrainMaterial { indices, weights }
}

pub(crate) fn expand_terrain_mesh_for_triangle_material_palettes(
    source_vertices: &[f32],
    source_indices: &[u32],
) -> MeshData {
    if source_indices.is_empty() {
        return MeshData {
            vertices: Vec::new(),
            indices: Vec::new(),
        };
    }

    let mut vertices = vec![0.0_f32; source_indices.len() * FLOATS_PER_VERTEX];
    let mut indices = Vec::with_capacity(source_indices.len());

    for triangle_offset in (0..source_indices.len()).step_by(3) {
        let source_vertex_indices = [
            source_indices[triangle_offset] as usize,
            source_indices[triangle_offset + 1] as usize,
            source_indices[triangle_offset + 2] as usize,
        ];
        let palette = triangle_material_palette(source_vertices, source_vertex_indices);

        for corner in 0..3 {
            let source_vertex_offset = source_vertex_indices[corner] * FLOATS_PER_VERTEX;
            let expanded_vertex_index = triangle_offset + corner;
            let expanded_vertex_offset = expanded_vertex_index * FLOATS_PER_VERTEX;

            vertices[expanded_vertex_offset..expanded_vertex_offset + FLOATS_PER_VERTEX]
                .copy_from_slice(
                    &source_vertices
                        [source_vertex_offset..source_vertex_offset + FLOATS_PER_VERTEX],
                );
            let weights =
                vertex_weights_for_palette(source_vertices, source_vertex_offset, palette);
            write_packed_material_to_vertex(
                &mut vertices,
                expanded_vertex_offset,
                PackedTerrainMaterial {
                    indices: [
                        palette[0] as f32,
                        palette[1] as f32,
                        palette[2] as f32,
                        palette[3] as f32,
                    ],
                    weights,
                },
            );
            indices.push(expanded_vertex_index as u32);
        }
    }

    MeshData { vertices, indices }
}

pub(crate) fn triangle_material_palette(
    vertices: &[f32],
    source_vertex_indices: [usize; 3],
) -> [usize; 4] {
    let mut weight_by_layer = [0.0_f32; 16];

    for source_vertex_index in source_vertex_indices {
        let source_vertex_offset = source_vertex_index * FLOATS_PER_VERTEX;
        for slot in 0..4 {
            let layer = vertices[source_vertex_offset + MATERIAL_INDICES_VERTEX_OFFSET + slot]
                .round() as usize;
            let weight = vertices[source_vertex_offset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot];
            if layer < weight_by_layer.len() && weight > 0.0 {
                weight_by_layer[layer] += weight;
            }
        }
    }

    let mut ranked: Vec<usize> = (0..weight_by_layer.len())
        .filter(|layer| weight_by_layer[*layer] > 0.0)
        .collect();
    ranked.sort_by(|a, b| {
        weight_by_layer[*b]
            .partial_cmp(&weight_by_layer[*a])
            .unwrap_or(core::cmp::Ordering::Equal)
            .then_with(|| a.cmp(b))
    });

    let mut palette = [0_usize; 4];
    for (index, layer) in ranked.into_iter().take(4).enumerate() {
        palette[index] = layer;
    }

    palette
}

pub(crate) fn vertex_weights_for_palette(
    vertices: &[f32],
    source_vertex_offset: usize,
    palette: [usize; 4],
) -> [f32; 4] {
    let mut weights = [0.0_f32; 4];

    for slot in 0..4 {
        let source_layer =
            vertices[source_vertex_offset + MATERIAL_INDICES_VERTEX_OFFSET + slot].round() as usize;
        let source_weight = vertices[source_vertex_offset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot];
        if let Some(palette_slot) = palette.iter().position(|layer| *layer == source_layer) {
            weights[palette_slot] += source_weight;
        }
    }

    let total: f32 = weights.iter().sum();
    if total <= f32::EPSILON {
        weights[0] = 1.0;
        return weights;
    }

    for weight in &mut weights {
        *weight /= total;
    }

    weights
}

pub(crate) fn write_packed_material_to_vertex(
    vertices: &mut [f32],
    vertex_offset: usize,
    material: PackedTerrainMaterial,
) {
    for slot in 0..4 {
        vertices[vertex_offset + MATERIAL_INDICES_VERTEX_OFFSET + slot] = material.indices[slot];
        vertices[vertex_offset + MATERIAL_WEIGHTS_VERTEX_OFFSET + slot] = material.weights[slot];
    }
}

pub(crate) fn color_for_height(height: f64) -> [f32; 3] {
    if height > 2.2 {
        return [0.72, 0.75, 0.7];
    }

    if height > 0.4 {
        return [0.38, 0.48, 0.31];
    }

    if height < -2.0 {
        return [0.26, 0.35, 0.44];
    }

    [0.31, 0.55, 0.38]
}

pub(crate) fn default_material_pack() -> PackedTerrainMaterial {
    PackedTerrainMaterial {
        indices: [0.0, 0.0, 0.0, 0.0],
        weights: [1.0, 0.0, 0.0, 0.0],
    }
}

pub(crate) fn positive_weight(weight: f64) -> f64 {
    if weight > 0.0 {
        weight
    } else {
        0.0
    }
}
