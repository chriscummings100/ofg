use std::collections::{BTreeMap, BTreeSet};
use std::sync::{Mutex, OnceLock};

use crate::*;

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct TerrainStreamConfig {
    pub(crate) horizontal_radius: i32,
    pub(crate) vertical_chunk_offsets: Vec<i32>,
    pub(crate) max_in_flight_jobs: usize,
}

impl Default for TerrainStreamConfig {
    fn default() -> Self {
        Self {
            horizontal_radius: 1,
            vertical_chunk_offsets: vec![-1, 0, 1],
            max_in_flight_jobs: 1,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainStreamJob {
    Density {
        generation: u64,
        coord: TerrainChunkCoord,
    },
    Lod {
        generation: u64,
        lod: u8,
        coord: TerrainChunkCoord,
    },
}

#[allow(dead_code)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainChunkStage {
    NotPresent,
    DensityInFlight { generation: u64 },
    DensityReady,
    LodInFlight { lod: u8, generation: u64 },
    LodReady { lod: u8 },
    LodEmpty { lod: u8 },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainStreamError {
    NegativeHorizontalRadius,
    EmptyVerticalOffsets,
    DuplicateVerticalOffsets,
    ZeroMaxInFlightJobs,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct TerrainStreamStatus {
    pub(crate) generation: u64,
    pub(crate) desired_density_count: usize,
    pub(crate) desired_lod0_count: usize,
    pub(crate) density_ready_count: usize,
    pub(crate) lod0_ready_count: usize,
    pub(crate) lod0_empty_count: usize,
    pub(crate) in_flight_density_count: usize,
    pub(crate) in_flight_lod_count: usize,
    pub(crate) missing_density_count: usize,
    pub(crate) missing_lod0_count: usize,
    pub(crate) max_in_flight_jobs: usize,
}

#[derive(Default)]
pub(crate) struct TerrainStreamScheduler {
    config: TerrainStreamConfig,
    generation: u64,
    center_coord: Option<TerrainChunkCoord>,
    desired_density: BTreeSet<TerrainChunkCoord>,
    desired_lod0: BTreeSet<TerrainChunkCoord>,
    chunks: BTreeMap<TerrainChunkCoord, TerrainChunkRecord>,
}

pub(crate) static TERRAIN_STREAM_SCHEDULER: OnceLock<Mutex<TerrainStreamScheduler>> =
    OnceLock::new();

pub(crate) fn terrain_stream_scheduler() -> &'static Mutex<TerrainStreamScheduler> {
    TERRAIN_STREAM_SCHEDULER.get_or_init(|| Mutex::new(TerrainStreamScheduler::default()))
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct TerrainChunkRecord {
    density: DensityStage,
    lod0: LodStage,
}

impl Default for TerrainChunkRecord {
    fn default() -> Self {
        Self {
            density: DensityStage::Missing,
            lod0: LodStage::Missing,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum DensityStage {
    Missing,
    InFlight { generation: u64 },
    Ready,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum LodStage {
    Missing,
    InFlight { generation: u64 },
    Ready,
    Empty,
}

impl TerrainStreamScheduler {
    pub(crate) fn new(config: TerrainStreamConfig) -> Result<Self, TerrainStreamError> {
        validate_stream_config(&config)?;

        Ok(Self {
            config,
            generation: 0,
            center_coord: None,
            desired_density: BTreeSet::new(),
            desired_lod0: BTreeSet::new(),
            chunks: BTreeMap::new(),
        })
    }

    pub(crate) fn generation(&self) -> u64 {
        self.generation
    }

    pub(crate) fn sync_center(&mut self, center_coord: TerrainChunkCoord) {
        self.center_coord = Some(center_coord);
        self.desired_lod0 = self
            .build_render_chunk_coords(center_coord)
            .into_iter()
            .collect();
        self.desired_density = self
            .desired_lod0
            .iter()
            .flat_map(|coord| self.density_dependencies(*coord))
            .collect();
        self.prune_outside_desired_sets();
    }

    pub(crate) fn reset(&mut self, center_coord: TerrainChunkCoord) {
        self.generation = self.generation.wrapping_add(1);
        self.center_coord = None;
        self.desired_density.clear();
        self.desired_lod0.clear();
        self.chunks.clear();
        self.sync_center(center_coord);
    }

    pub(crate) fn invalidate_all(&mut self) {
        self.generation = self.generation.wrapping_add(1);
        self.center_coord = None;
        self.desired_density.clear();
        self.desired_lod0.clear();
        self.chunks.clear();
    }

    pub(crate) fn tick(&mut self) -> Vec<TerrainStreamJob> {
        let mut jobs = Vec::new();

        while self.active_job_count() < self.config.max_in_flight_jobs {
            if let Some(coord) = self.next_density_job_coord() {
                self.record_mut(coord).density = DensityStage::InFlight {
                    generation: self.generation,
                };
                jobs.push(TerrainStreamJob::Density {
                    generation: self.generation,
                    coord,
                });
                continue;
            }

            if let Some(coord) = self.next_lod0_job_coord() {
                self.record_mut(coord).lod0 = LodStage::InFlight {
                    generation: self.generation,
                };
                jobs.push(TerrainStreamJob::Lod {
                    generation: self.generation,
                    lod: 0,
                    coord,
                });
                continue;
            }

            break;
        }

        jobs
    }

    pub(crate) fn complete_density(&mut self, generation: u64, coord: TerrainChunkCoord) -> bool {
        if generation != self.generation || !self.desired_density.contains(&coord) {
            return false;
        }

        let record = self.record_mut(coord);
        if record.density != (DensityStage::InFlight { generation }) {
            return false;
        }

        record.density = DensityStage::Ready;
        true
    }

    pub(crate) fn fail_density(&mut self, generation: u64, coord: TerrainChunkCoord) -> bool {
        if generation != self.generation || !self.desired_density.contains(&coord) {
            return false;
        }

        let record = self.record_mut(coord);
        if record.density != (DensityStage::InFlight { generation }) {
            return false;
        }

        record.density = DensityStage::Missing;
        true
    }

    pub(crate) fn complete_lod0(
        &mut self,
        generation: u64,
        coord: TerrainChunkCoord,
        empty: bool,
    ) -> bool {
        if generation != self.generation || !self.desired_lod0.contains(&coord) {
            return false;
        }

        let record = self.record_mut(coord);
        if record.lod0 != (LodStage::InFlight { generation }) {
            return false;
        }

        record.lod0 = if empty {
            LodStage::Empty
        } else {
            LodStage::Ready
        };
        true
    }

    pub(crate) fn fail_lod0(&mut self, generation: u64, coord: TerrainChunkCoord) -> bool {
        if generation != self.generation || !self.desired_lod0.contains(&coord) {
            return false;
        }

        let record = self.record_mut(coord);
        if record.lod0 != (LodStage::InFlight { generation }) {
            return false;
        }

        record.lod0 = LodStage::Missing;
        true
    }

    #[allow(dead_code)]
    pub(crate) fn chunk_stage(&self, coord: TerrainChunkCoord) -> TerrainChunkStage {
        let Some(record) = self.chunks.get(&coord) else {
            return TerrainChunkStage::NotPresent;
        };

        match record.lod0 {
            LodStage::Ready => return TerrainChunkStage::LodReady { lod: 0 },
            LodStage::Empty => return TerrainChunkStage::LodEmpty { lod: 0 },
            LodStage::InFlight { generation } => {
                return TerrainChunkStage::LodInFlight { lod: 0, generation };
            }
            LodStage::Missing => {}
        }

        match record.density {
            DensityStage::Missing => TerrainChunkStage::NotPresent,
            DensityStage::InFlight { generation } => {
                TerrainChunkStage::DensityInFlight { generation }
            }
            DensityStage::Ready => TerrainChunkStage::DensityReady,
        }
    }

    pub(crate) fn desired_density_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired_density.iter().copied().collect()
    }

    pub(crate) fn desired_lod0_coords(&self) -> Vec<TerrainChunkCoord> {
        self.desired_lod0.iter().copied().collect()
    }

    pub(crate) fn density_dependencies(&self, coord: TerrainChunkCoord) -> Vec<TerrainChunkCoord> {
        let mut coords = Vec::with_capacity(8);
        for z in coord.z..=coord.z + 1 {
            for y in coord.y..=coord.y + 1 {
                for x in coord.x..=coord.x + 1 {
                    coords.push(TerrainChunkCoord { x, y, z });
                }
            }
        }

        coords
    }

    pub(crate) fn status(&self) -> TerrainStreamStatus {
        let in_flight_density_count = self
            .chunks
            .values()
            .filter(|record| matches!(record.density, DensityStage::InFlight { .. }))
            .count();
        let in_flight_lod_count = self
            .chunks
            .values()
            .filter(|record| matches!(record.lod0, LodStage::InFlight { .. }))
            .count();
        let density_ready_count = self
            .desired_density
            .iter()
            .filter(|coord| {
                self.chunks
                    .get(coord)
                    .is_some_and(|record| record.density == DensityStage::Ready)
            })
            .count();
        let lod0_ready_count = self
            .desired_lod0
            .iter()
            .filter(|coord| {
                self.chunks
                    .get(coord)
                    .is_some_and(|record| record.lod0 == LodStage::Ready)
            })
            .count();
        let lod0_empty_count = self
            .desired_lod0
            .iter()
            .filter(|coord| {
                self.chunks
                    .get(coord)
                    .is_some_and(|record| record.lod0 == LodStage::Empty)
            })
            .count();

        TerrainStreamStatus {
            generation: self.generation,
            desired_density_count: self.desired_density.len(),
            desired_lod0_count: self.desired_lod0.len(),
            density_ready_count,
            lod0_ready_count,
            lod0_empty_count,
            in_flight_density_count,
            in_flight_lod_count,
            missing_density_count: self
                .desired_density
                .iter()
                .filter(|coord| self.should_submit_density(**coord))
                .count(),
            missing_lod0_count: self
                .desired_lod0
                .iter()
                .filter(|coord| self.should_submit_lod0(**coord))
                .count(),
            max_in_flight_jobs: self.config.max_in_flight_jobs,
        }
    }

    fn build_render_chunk_coords(&self, center_coord: TerrainChunkCoord) -> Vec<TerrainChunkCoord> {
        let mut coords = Vec::new();

        for z in center_coord.z - self.config.horizontal_radius
            ..=center_coord.z + self.config.horizontal_radius
        {
            for x in center_coord.x - self.config.horizontal_radius
                ..=center_coord.x + self.config.horizontal_radius
            {
                for offset in &self.config.vertical_chunk_offsets {
                    coords.push(TerrainChunkCoord {
                        x,
                        y: center_coord.y + offset,
                        z,
                    });
                }
            }
        }

        coords
    }

    fn next_density_job_coord(&self) -> Option<TerrainChunkCoord> {
        let center_coord = self.center_coord?;

        self.desired_density
            .iter()
            .copied()
            .filter(|coord| self.should_submit_density(*coord))
            .min_by_key(|coord| (chunk_priority(*coord, center_coord), *coord))
    }

    fn next_lod0_job_coord(&self) -> Option<TerrainChunkCoord> {
        let center_coord = self.center_coord?;

        self.desired_lod0
            .iter()
            .copied()
            .filter(|coord| self.should_submit_lod0(*coord))
            .min_by_key(|coord| (chunk_priority(*coord, center_coord), *coord))
    }

    fn should_submit_density(&self, coord: TerrainChunkCoord) -> bool {
        if !self.desired_density.contains(&coord) {
            return false;
        }

        !matches!(
            self.chunks.get(&coord).map(|record| record.density),
            Some(DensityStage::InFlight { .. } | DensityStage::Ready)
        )
    }

    fn should_submit_lod0(&self, coord: TerrainChunkCoord) -> bool {
        if !self.desired_lod0.contains(&coord) || !self.density_dependencies_ready(coord) {
            return false;
        }

        !matches!(
            self.chunks.get(&coord).map(|record| record.lod0),
            Some(LodStage::InFlight { .. } | LodStage::Ready | LodStage::Empty)
        )
    }

    fn density_dependencies_ready(&self, coord: TerrainChunkCoord) -> bool {
        self.density_dependencies(coord).iter().all(|dependency| {
            self.chunks
                .get(dependency)
                .is_some_and(|record| record.density == DensityStage::Ready)
        })
    }

    fn active_job_count(&self) -> usize {
        self.chunks
            .values()
            .filter(|record| matches!(record.density, DensityStage::InFlight { .. }))
            .count()
            + self
                .chunks
                .values()
                .filter(|record| matches!(record.lod0, LodStage::InFlight { .. }))
                .count()
    }

    fn record_mut(&mut self, coord: TerrainChunkCoord) -> &mut TerrainChunkRecord {
        self.chunks.entry(coord).or_default()
    }

    fn prune_outside_desired_sets(&mut self) {
        let desired_density = &self.desired_density;
        let desired_lod0 = &self.desired_lod0;

        self.chunks.retain(|coord, _record| {
            desired_density.contains(coord) || desired_lod0.contains(coord)
        });
    }
}

fn validate_stream_config(config: &TerrainStreamConfig) -> Result<(), TerrainStreamError> {
    if config.horizontal_radius < 0 {
        return Err(TerrainStreamError::NegativeHorizontalRadius);
    }

    if config.vertical_chunk_offsets.is_empty() {
        return Err(TerrainStreamError::EmptyVerticalOffsets);
    }

    let unique_offsets: BTreeSet<i32> = config.vertical_chunk_offsets.iter().copied().collect();
    if unique_offsets.len() != config.vertical_chunk_offsets.len() {
        return Err(TerrainStreamError::DuplicateVerticalOffsets);
    }

    if config.max_in_flight_jobs == 0 {
        return Err(TerrainStreamError::ZeroMaxInFlightJobs);
    }

    Ok(())
}

fn chunk_priority(coord: TerrainChunkCoord, center_coord: TerrainChunkCoord) -> i64 {
    let dx = i64::from(coord.x - center_coord.x);
    let dy = i64::from((coord.y - center_coord.y).abs());
    let dz = i64::from(coord.z - center_coord.z);

    (dx * dx + dz * dz) * 2 + dy
}
