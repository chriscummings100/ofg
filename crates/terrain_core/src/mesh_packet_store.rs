use std::sync::{Mutex, OnceLock};

use crate::*;

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub(crate) struct TerrainMeshPacketKey {
    pub(crate) chunk_x: i32,
    pub(crate) chunk_y: i32,
    pub(crate) chunk_z: i32,
    pub(crate) lod: u8,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum TerrainMeshPacketStoreError {
    EmptyMesh,
    VertexStrideMismatch,
    IndexCountMismatch,
    IndexOutOfBounds,
}

pub(crate) struct StoredTerrainMeshPacket {
    pub(crate) key: TerrainMeshPacketKey,
    pub(crate) vertices: Vec<f32>,
    pub(crate) indices: Vec<u32>,
}

pub(crate) struct TerrainMeshPacketStore {
    entries: Vec<StoredTerrainMeshPacket>,
    version: u64,
}

pub(crate) static TERRAIN_MESH_PACKET_STORE: OnceLock<Mutex<TerrainMeshPacketStore>> =
    OnceLock::new();

pub(crate) fn terrain_mesh_packet_store() -> &'static Mutex<TerrainMeshPacketStore> {
    TERRAIN_MESH_PACKET_STORE.get_or_init(|| Mutex::new(TerrainMeshPacketStore::new()))
}

pub(crate) fn terrain_mesh_packet_key(coord: TerrainChunkCoord, lod: u8) -> TerrainMeshPacketKey {
    TerrainMeshPacketKey {
        chunk_x: coord.x,
        chunk_y: coord.y,
        chunk_z: coord.z,
        lod,
    }
}

impl TerrainMeshPacketStore {
    fn new() -> Self {
        Self {
            entries: Vec::new(),
            version: 0,
        }
    }

    pub(crate) fn reset(&mut self) {
        if self.entries.is_empty() {
            return;
        }

        self.entries.clear();
        self.bump_version();
    }

    pub(crate) fn len(&self) -> usize {
        self.entries.len()
    }

    pub(crate) fn version(&self) -> u64 {
        self.version
    }

    pub(crate) fn contains(&self, key: TerrainMeshPacketKey) -> bool {
        self.entries.iter().any(|entry| entry.key == key)
    }

    pub(crate) fn insert(
        &mut self,
        key: TerrainMeshPacketKey,
        vertices: Vec<f32>,
        indices: Vec<u32>,
    ) -> Result<(), TerrainMeshPacketStoreError> {
        validate_mesh_packet(&vertices, &indices)?;

        for entry in &mut self.entries {
            if entry.key == key {
                entry.vertices = vertices;
                entry.indices = indices;
                self.bump_version();
                return Ok(());
            }
        }

        self.entries.push(StoredTerrainMeshPacket {
            key,
            vertices,
            indices,
        });
        self.entries.sort_by_key(|entry| entry.key);
        self.bump_version();
        Ok(())
    }

    pub(crate) fn remove(&mut self, key: TerrainMeshPacketKey) -> bool {
        let Some(index) = self.entries.iter().position(|entry| entry.key == key) else {
            return false;
        };

        self.entries.remove(index);
        self.bump_version();
        true
    }

    pub(crate) fn get(&self, key: TerrainMeshPacketKey) -> Option<(&[f32], &[u32])> {
        self.entries
            .iter()
            .find(|entry| entry.key == key)
            .map(|entry| (entry.vertices.as_slice(), entry.indices.as_slice()))
    }

    pub(crate) fn keys(&self) -> Vec<TerrainMeshPacketKey> {
        self.entries.iter().map(|entry| entry.key).collect()
    }

    fn bump_version(&mut self) {
        self.version = self.version.wrapping_add(1);
    }
}

fn validate_mesh_packet(
    vertices: &[f32],
    indices: &[u32],
) -> Result<(), TerrainMeshPacketStoreError> {
    if vertices.is_empty() || indices.is_empty() {
        return Err(TerrainMeshPacketStoreError::EmptyMesh);
    }

    if vertices.len() % FLOATS_PER_VERTEX != 0 {
        return Err(TerrainMeshPacketStoreError::VertexStrideMismatch);
    }

    if indices.len() % 3 != 0 {
        return Err(TerrainMeshPacketStoreError::IndexCountMismatch);
    }

    let vertex_count = vertices.len() / FLOATS_PER_VERTEX;
    if indices.iter().any(|index| *index as usize >= vertex_count) {
        return Err(TerrainMeshPacketStoreError::IndexOutOfBounds);
    }

    Ok(())
}
