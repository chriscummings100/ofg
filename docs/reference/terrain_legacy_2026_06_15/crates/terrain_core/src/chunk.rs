use crate::*;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TerrainChunkCoord {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

#[derive(Clone, Copy)]
pub(crate) struct TerrainCellCoord {
    pub(crate) x: usize,
    pub(crate) y: usize,
    pub(crate) z: usize,
}

#[derive(Clone, Copy)]
pub(crate) struct TerrainSampleCoord {
    pub(crate) x: usize,
    pub(crate) y: usize,
    pub(crate) z: usize,
}

pub(crate) struct TerrainDensityChunk {
    pub(crate) coord: TerrainChunkCoord,
    pub(crate) cell_size: f64,
    pub(crate) densities: Vec<f32>,
}

#[derive(Clone, Copy)]
pub(crate) struct TerrainChunkBounds {
    pub(crate) min: Vec3,
    pub(crate) max: Vec3,
}

impl TerrainDensityChunk {
    pub(crate) fn density_at_sample(&self, sample: TerrainSampleCoord) -> f32 {
        self.densities[terrain_chunk_sample_index(sample.x, sample.y, sample.z)]
    }

    pub(crate) fn sample_position(&self, sample: TerrainSampleCoord) -> Vec3 {
        let origin = terrain_chunk_origin(self.coord, self.cell_size);

        Vec3 {
            x: origin.x + sample.x as f64 * self.cell_size,
            y: origin.y + sample.y as f64 * self.cell_size,
            z: origin.z + sample.z as f64 * self.cell_size,
        }
    }

    pub(crate) fn bounds(&self) -> TerrainChunkBounds {
        let min = terrain_chunk_origin(self.coord, self.cell_size);
        let chunk_size = TERRAIN_CHUNK_CELLS_PER_AXIS as f64 * self.cell_size;

        TerrainChunkBounds {
            min,
            max: Vec3 {
                x: min.x + chunk_size,
                y: min.y + chunk_size,
                z: min.z + chunk_size,
            },
        }
    }

    pub(crate) fn cell_bounds(&self, cell: TerrainCellCoord) -> TerrainChunkBounds {
        let min = self.sample_position(TerrainSampleCoord {
            x: cell.x,
            y: cell.y,
            z: cell.z,
        });

        TerrainChunkBounds {
            min,
            max: Vec3 {
                x: min.x + self.cell_size,
                y: min.y + self.cell_size,
                z: min.z + self.cell_size,
            },
        }
    }
}

pub(crate) fn terrain_chunk_origin(coord: TerrainChunkCoord, cell_size: f64) -> Vec3 {
    let chunk_size = TERRAIN_CHUNK_CELLS_PER_AXIS as f64 * cell_size;

    Vec3 {
        x: coord.x as f64 * chunk_size,
        y: coord.y as f64 * chunk_size,
        z: coord.z as f64 * chunk_size,
    }
}

pub fn terrain_chunk_coord_containing_position(
    x: f32,
    y: f32,
    z: f32,
    cell_size: f64,
) -> TerrainChunkCoord {
    let chunk_size = TERRAIN_CHUNK_CELLS_PER_AXIS as f64 * cell_size;

    TerrainChunkCoord {
        x: (f64::from(x) / chunk_size).floor() as i32,
        y: (f64::from(y) / chunk_size).floor() as i32,
        z: (f64::from(z) / chunk_size).floor() as i32,
    }
}

pub fn terrain_chunk_key(coord: TerrainChunkCoord) -> String {
    format!("{},{},{}", coord.x, coord.y, coord.z)
}

pub(crate) fn terrain_chunk_sample_index(x: usize, y: usize, z: usize) -> usize {
    x + y * TERRAIN_CHUNK_SAMPLES_PER_AXIS
        + z * TERRAIN_CHUNK_SAMPLES_PER_AXIS * TERRAIN_CHUNK_SAMPLES_PER_AXIS
}
