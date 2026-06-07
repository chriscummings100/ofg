use crate::*;

#[derive(Clone, Copy)]
pub(crate) struct HermiteIntersection {
    pub(crate) position: Vec3,
    pub(crate) normal: Vec3,
}

#[derive(Clone)]
pub struct MeshData {
    pub vertices: Vec<f32>,
    pub indices: Vec<u32>,
}

#[derive(Clone)]
pub(crate) struct RawTerrainMesh {
    pub(crate) vertices: Vec<f32>,
    pub(crate) indices: Vec<u32>,
}

pub fn build_chunk_mesh(
    seed: u32,
    preset: u32,
    coord: TerrainChunkCoord,
    cell_size: f64,
) -> MeshData {
    if cell_size <= 0.0 {
        return MeshData {
            vertices: Vec::new(),
            indices: Vec::new(),
        };
    }

    let noise = SimplexNoise3D::new(seed);
    let preset_id = terrain_preset_index(preset);
    let preset = terrain_preset(preset_id);
    let chunks = generate_neighbor_apron_chunks(&noise, preset, preset_id, seed, coord, cell_size);

    build_neighbor_aware_chunk_mesh(&noise, preset, seed, &chunks, coord)
}

pub fn build_node_mesh(
    seed: u32,
    preset: u32,
    key: TerrainNodeKey,
    base_cell_size: f64,
) -> MeshData {
    build_chunk_mesh(
        seed,
        preset,
        key.coord,
        terrain_node_cell_size(base_cell_size, key.lod),
    )
}

pub(crate) const CELL_CORNERS: [TerrainSampleCoord; 8] = [
    TerrainSampleCoord { x: 0, y: 0, z: 0 },
    TerrainSampleCoord { x: 1, y: 0, z: 0 },
    TerrainSampleCoord { x: 0, y: 1, z: 0 },
    TerrainSampleCoord { x: 1, y: 1, z: 0 },
    TerrainSampleCoord { x: 0, y: 0, z: 1 },
    TerrainSampleCoord { x: 1, y: 0, z: 1 },
    TerrainSampleCoord { x: 0, y: 1, z: 1 },
    TerrainSampleCoord { x: 1, y: 1, z: 1 },
];
pub(crate) const CELL_EDGES: [(usize, usize); 12] = [
    (0, 1),
    (2, 3),
    (4, 5),
    (6, 7),
    (0, 2),
    (1, 3),
    (4, 6),
    (5, 7),
    (0, 4),
    (1, 5),
    (2, 6),
    (3, 7),
];

pub(crate) fn build_neighbor_aware_chunk_mesh(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    chunks: &[TerrainDensityChunk],
    center_coord: TerrainChunkCoord,
) -> MeshData {
    let raw_mesh = build_neighbor_aware_chunk_mesh_raw(noise, preset, seed, chunks, center_coord);
    expand_terrain_mesh_for_triangle_material_palettes(&raw_mesh.vertices, &raw_mesh.indices)
}

pub(crate) fn build_neighbor_aware_chunk_mesh_raw(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    chunks: &[TerrainDensityChunk],
    center_coord: TerrainChunkCoord,
) -> RawTerrainMesh {
    let center_chunk = match neighbor_chunk(chunks, center_coord, center_coord) {
        Some(chunk) => chunk,
        None => {
            return RawTerrainMesh {
                vertices: Vec::new(),
                indices: Vec::new(),
            };
        }
    };
    let mut vertex_indices = vec![-1_i32; TERRAIN_CHUNK_APRON_CELL_COUNT];
    let mut vertices = Vec::new();
    let mesh_bounds = center_chunk.bounds();

    for z in 0..=TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..=TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..=TERRAIN_CHUNK_CELLS_PER_AXIS {
                let (chunk_coord, cell) = local_apron_cell_ref(center_coord, x, y, z);
                let chunk = match neighbor_chunk(chunks, center_coord, chunk_coord) {
                    Some(chunk) => chunk,
                    None => continue,
                };
                let intersections = extract_hermite_intersections(noise, preset, seed, chunk, cell);
                if intersections.is_empty() {
                    continue;
                }

                let position = centroid_of_intersections(&intersections, chunk.cell_bounds(cell));
                let normal = average_normal(&intersections);
                let vertex_index = vertices.len() / FLOATS_PER_VERTEX;
                vertex_indices[apron_cell_index(x, y, z)] = vertex_index as i32;
                write_dual_contouring_vertex(
                    &mut vertices,
                    mesh_bounds,
                    position,
                    normal,
                    noise,
                    preset,
                    seed,
                );
            }
        }
    }

    let mut indices = Vec::new();
    emit_owned_x_edge_quads(center_chunk, &vertex_indices, &mut indices);
    emit_owned_y_edge_quads(center_chunk, &vertex_indices, &mut indices);
    emit_owned_z_edge_quads(center_chunk, &vertex_indices, &mut indices);

    RawTerrainMesh { vertices, indices }
}

pub(crate) fn extract_hermite_intersections(
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
    chunk: &TerrainDensityChunk,
    cell: TerrainCellCoord,
) -> Vec<HermiteIntersection> {
    let mut corner_densities = [0.0_f32; 8];
    for (index, corner) in CELL_CORNERS.iter().enumerate() {
        corner_densities[index] = chunk.density_at_sample(sample_for_cell_corner(cell, *corner));
    }

    let mut intersections = Vec::new();
    for (start_corner_index, end_corner_index) in CELL_EDGES {
        let start_density = corner_densities[start_corner_index];
        let end_density = corner_densities[end_corner_index];
        if !has_sign_change(start_density, end_density) {
            continue;
        }

        let start_sample = sample_for_cell_corner(cell, CELL_CORNERS[start_corner_index]);
        let end_sample = sample_for_cell_corner(cell, CELL_CORNERS[end_corner_index]);
        let start_position = chunk.sample_position(start_sample);
        let end_position = chunk.sample_position(end_sample);
        let t = clamp(
            start_density as f64 / (start_density as f64 - end_density as f64),
            0.0,
            1.0,
        );
        let position = lerp_vec3(start_position, end_position, t);
        let normal = normalize_vec3(density_at_position(noise, preset, seed, position).gradient);

        intersections.push(HermiteIntersection { position, normal });
    }

    intersections
}

pub(crate) fn emit_owned_x_edge_quads(
    chunk: &TerrainDensityChunk,
    cell_vertex_indices: &[i32],
    indices: &mut Vec<u32>,
) {
    for z in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
                let start_density = chunk.density_at_sample(TerrainSampleCoord {
                    x,
                    y: y + 1,
                    z: z + 1,
                });
                let end_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z: z + 1,
                });
                if !has_sign_change(start_density, end_density) {
                    continue;
                }

                emit_quad(
                    indices,
                    [
                        local_cell_vertex_index(cell_vertex_indices, x as i32, y as i32, z as i32),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32 + 1,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32,
                            z as i32 + 1,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32 + 1,
                            z as i32 + 1,
                        ),
                    ],
                    start_density <= 0.0 && end_density > 0.0,
                );
            }
        }
    }
}

pub(crate) fn emit_owned_y_edge_quads(
    chunk: &TerrainDensityChunk,
    cell_vertex_indices: &[i32],
    indices: &mut Vec<u32>,
) {
    for z in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
                let start_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y,
                    z: z + 1,
                });
                let end_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z: z + 1,
                });
                if !has_sign_change(start_density, end_density) {
                    continue;
                }

                emit_quad(
                    indices,
                    [
                        local_cell_vertex_index(cell_vertex_indices, x as i32, y as i32, z as i32),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32,
                            z as i32 + 1,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32,
                            z as i32 + 1,
                        ),
                    ],
                    start_density <= 0.0 && end_density > 0.0,
                );
            }
        }
    }
}

pub(crate) fn emit_owned_z_edge_quads(
    chunk: &TerrainDensityChunk,
    cell_vertex_indices: &[i32],
    indices: &mut Vec<u32>,
) {
    for z in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
        for y in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
            for x in 0..TERRAIN_CHUNK_CELLS_PER_AXIS {
                let start_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z,
                });
                let end_density = chunk.density_at_sample(TerrainSampleCoord {
                    x: x + 1,
                    y: y + 1,
                    z: z + 1,
                });
                if !has_sign_change(start_density, end_density) {
                    continue;
                }

                emit_quad(
                    indices,
                    [
                        local_cell_vertex_index(cell_vertex_indices, x as i32, y as i32, z as i32),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32,
                            y as i32 + 1,
                            z as i32,
                        ),
                        local_cell_vertex_index(
                            cell_vertex_indices,
                            x as i32 + 1,
                            y as i32 + 1,
                            z as i32,
                        ),
                    ],
                    start_density <= 0.0 && end_density > 0.0,
                );
            }
        }
    }
}

pub(crate) fn emit_quad(indices: &mut Vec<u32>, vertices: [i32; 4], forward: bool) {
    if vertices.iter().any(|vertex| *vertex < 0) {
        return;
    }

    let [a, b, c, d] = vertices.map(|vertex| vertex as u32);
    if forward {
        indices.extend_from_slice(&[a, b, c, c, b, d]);
    } else {
        indices.extend_from_slice(&[a, c, b, c, d, b]);
    }
}

pub(crate) fn write_dual_contouring_vertex(
    vertices: &mut Vec<f32>,
    chunk_bounds: TerrainChunkBounds,
    position: Vec3,
    normal: Vec3,
    noise: &SimplexNoise3D,
    preset: TerrainPresetDefinition,
    seed: u32,
) {
    let color = color_for_height(position.y);
    let width = chunk_bounds.max.x - chunk_bounds.min.x;
    let depth = chunk_bounds.max.z - chunk_bounds.min.z;
    let material = material_pack_at(noise, preset, seed, position);

    vertices.extend_from_slice(&[
        position.x as f32,
        position.y as f32,
        position.z as f32,
        color[0],
        color[1],
        color[2],
        normal.x as f32,
        normal.y as f32,
        normal.z as f32,
        if width == 0.0 {
            0.0
        } else {
            ((position.x - chunk_bounds.min.x) / width) as f32
        },
        if depth == 0.0 {
            0.0
        } else {
            ((position.z - chunk_bounds.min.z) / depth) as f32
        },
        material.indices[0],
        material.indices[1],
        material.indices[2],
        material.indices[3],
        material.weights[0],
        material.weights[1],
        material.weights[2],
        material.weights[3],
    ]);
}

pub(crate) fn neighbor_chunk<'a>(
    chunks: &'a [TerrainDensityChunk],
    center_coord: TerrainChunkCoord,
    coord: TerrainChunkCoord,
) -> Option<&'a TerrainDensityChunk> {
    let dx = coord.x - center_coord.x;
    let dy = coord.y - center_coord.y;
    let dz = coord.z - center_coord.z;
    if !(0..=1).contains(&dx) || !(0..=1).contains(&dy) || !(0..=1).contains(&dz) {
        return None;
    }

    chunks.get(dx as usize + dy as usize * 2 + dz as usize * 4)
}

pub(crate) fn local_apron_cell_ref(
    center_coord: TerrainChunkCoord,
    x: usize,
    y: usize,
    z: usize,
) -> (TerrainChunkCoord, TerrainCellCoord) {
    (
        TerrainChunkCoord {
            x: center_coord.x
                + if x == TERRAIN_CHUNK_CELLS_PER_AXIS {
                    1
                } else {
                    0
                },
            y: center_coord.y
                + if y == TERRAIN_CHUNK_CELLS_PER_AXIS {
                    1
                } else {
                    0
                },
            z: center_coord.z
                + if z == TERRAIN_CHUNK_CELLS_PER_AXIS {
                    1
                } else {
                    0
                },
        },
        TerrainCellCoord {
            x: if x == TERRAIN_CHUNK_CELLS_PER_AXIS {
                0
            } else {
                x
            },
            y: if y == TERRAIN_CHUNK_CELLS_PER_AXIS {
                0
            } else {
                y
            },
            z: if z == TERRAIN_CHUNK_CELLS_PER_AXIS {
                0
            } else {
                z
            },
        },
    )
}

pub(crate) fn sample_for_cell_corner(
    cell: TerrainCellCoord,
    corner: TerrainSampleCoord,
) -> TerrainSampleCoord {
    TerrainSampleCoord {
        x: cell.x + corner.x,
        y: cell.y + corner.y,
        z: cell.z + corner.z,
    }
}

pub(crate) fn centroid_of_intersections(
    intersections: &[HermiteIntersection],
    bounds: TerrainChunkBounds,
) -> Vec3 {
    let mut x = 0.0;
    let mut y = 0.0;
    let mut z = 0.0;

    for intersection in intersections {
        x += intersection.position.x;
        y += intersection.position.y;
        z += intersection.position.z;
    }

    let scale = 1.0 / intersections.len() as f64;
    clamp_vec3_to_bounds(
        Vec3 {
            x: x * scale,
            y: y * scale,
            z: z * scale,
        },
        bounds,
    )
}

pub(crate) fn average_normal(intersections: &[HermiteIntersection]) -> Vec3 {
    let mut normal = Vec3 {
        x: 0.0,
        y: 0.0,
        z: 0.0,
    };
    for intersection in intersections {
        normal.x += intersection.normal.x;
        normal.y += intersection.normal.y;
        normal.z += intersection.normal.z;
    }

    normalize_vec3(normal)
}

pub(crate) fn apron_cell_index(x: usize, y: usize, z: usize) -> usize {
    x + y * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS
        + z * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS * TERRAIN_CHUNK_APRON_CELLS_PER_AXIS
}

pub(crate) fn local_cell_vertex_index(indices: &[i32], x: i32, y: i32, z: i32) -> i32 {
    if x < 0
        || y < 0
        || z < 0
        || x > TERRAIN_CHUNK_CELLS_PER_AXIS as i32
        || y > TERRAIN_CHUNK_CELLS_PER_AXIS as i32
        || z > TERRAIN_CHUNK_CELLS_PER_AXIS as i32
    {
        return -1;
    }

    indices[apron_cell_index(x as usize, y as usize, z as usize)]
}

pub(crate) fn has_sign_change(a: f32, b: f32) -> bool {
    (a <= 0.0 && b > 0.0) || (a > 0.0 && b <= 0.0)
}
