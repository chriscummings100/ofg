//! Narrow raw WASM facade for the browser worker terrain build path.

use std::cell::RefCell;

use crate::mesh::{build_chunk_mesh, build_node_mesh_for_variant, MeshData};
use crate::node::{TerrainChunkCoord, TerrainNodeKey};
use crate::variant::{
    terrain_variant_flat_values, terrain_variant_for_preset, terrain_variant_from_flat_values,
    TerrainVariantDescriptor, TERRAIN_VARIANT_FLAT_VALUE_COUNT,
};

thread_local! {
    static VARIANT_BUFFER: RefCell<[f64; TERRAIN_VARIANT_FLAT_VALUE_COUNT]> =
        RefCell::new(terrain_variant_flat_values(terrain_variant_for_preset(crate::DEFAULT_TERRAIN_PRESET)));
    static MESH_BUFFER: RefCell<MeshData> = RefCell::new(MeshData::default());
}

#[no_mangle]
pub extern "C" fn ofg_terrain_core_version() -> u32 {
    2
}

#[no_mangle]
pub extern "C" fn ofg_terrain_core_preset_count() -> u32 {
    crate::terrain_preset_count()
}

#[no_mangle]
pub extern "C" fn ofg_terrain_variant_flat_value_count() -> u32 {
    TERRAIN_VARIANT_FLAT_VALUE_COUNT as u32
}

#[no_mangle]
pub extern "C" fn ofg_terrain_variant_buffer_ptr() -> *mut f64 {
    VARIANT_BUFFER.with(|buffer| buffer.borrow_mut().as_mut_ptr())
}

#[no_mangle]
pub extern "C" fn ofg_write_terrain_variant_preset(preset: u32) {
    let values = terrain_variant_flat_values(terrain_variant_for_preset(preset));
    VARIANT_BUFFER.with(|buffer| *buffer.borrow_mut() = values);
}

#[no_mangle]
pub extern "C" fn ofg_build_chunk_mesh(
    seed: u32,
    preset: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) -> u32 {
    let mesh = build_chunk_mesh(
        seed,
        preset,
        TerrainChunkCoord {
            x: chunk_x,
            y: chunk_y,
            z: chunk_z,
        },
        cell_size,
    );
    store_mesh(mesh)
}

#[no_mangle]
pub extern "C" fn ofg_build_chunk_mesh_for_variant(
    seed: u32,
    chunk_x: i32,
    chunk_y: i32,
    chunk_z: i32,
    cell_size: f64,
) -> u32 {
    let variant = current_variant();
    let mesh = build_node_mesh_for_variant(
        seed,
        variant,
        TerrainNodeKey {
            lod: 0,
            coord: TerrainChunkCoord {
                x: chunk_x,
                y: chunk_y,
                z: chunk_z,
            },
        },
        cell_size,
    );
    store_mesh(mesh)
}

#[no_mangle]
pub extern "C" fn ofg_mesh_vertex_buffer_ptr() -> *const f32 {
    MESH_BUFFER.with(|mesh| mesh.borrow().vertices.as_ptr())
}

#[no_mangle]
pub extern "C" fn ofg_mesh_vertex_buffer_len() -> u32 {
    MESH_BUFFER.with(|mesh| mesh.borrow().vertices.len().min(u32::MAX as usize) as u32)
}

#[no_mangle]
pub extern "C" fn ofg_mesh_index_buffer_ptr() -> *const u32 {
    MESH_BUFFER.with(|mesh| mesh.borrow().indices.as_ptr())
}

#[no_mangle]
pub extern "C" fn ofg_mesh_index_buffer_len() -> u32 {
    MESH_BUFFER.with(|mesh| mesh.borrow().indices.len().min(u32::MAX as usize) as u32)
}

#[no_mangle]
pub extern "C" fn ofg_height_at(seed: u32, preset: u32, x: f64, z: f64) -> f64 {
    crate::height_at(seed, preset, x, z)
}

fn current_variant() -> TerrainVariantDescriptor {
    VARIANT_BUFFER.with(|buffer| {
        terrain_variant_from_flat_values(buffer.borrow().as_slice())
            .unwrap_or_else(|_| terrain_variant_for_preset(crate::DEFAULT_TERRAIN_PRESET))
    })
}

fn store_mesh(mesh: MeshData) -> u32 {
    let has_indices = !mesh.indices.is_empty();
    MESH_BUFFER.with(|buffer| *buffer.borrow_mut() = mesh);
    u32::from(has_indices)
}
