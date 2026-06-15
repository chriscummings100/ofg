use std::sync::{Mutex, OnceLock};

use crate::*;

pub(crate) const TERRAIN_WORKER_POOL_MAX_WORKERS: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainWorkerTaskKind {
    Density,
    Lod,
}

impl TerrainWorkerTaskKind {
    pub(crate) fn from_code(code: u32) -> Option<Self> {
        match code {
            0 => Some(Self::Density),
            1 => Some(Self::Lod),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct TerrainWorkerTask {
    pub(crate) request_id: u32,
    pub(crate) worker_index: usize,
    pub(crate) kind: TerrainWorkerTaskKind,
    pub(crate) lod: u8,
    pub(crate) generation: u64,
    pub(crate) coord: TerrainChunkCoord,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainWorkerTaskFinish {
    Stale,
    Matched,
    Mismatched,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TerrainWorkerPoolError {
    ZeroWorkers,
    TooManyWorkers,
    NoIdleWorkers,
    InvalidLod,
    InvalidRequestId,
}

#[derive(Debug)]
pub(crate) struct TerrainWorkerPool {
    worker_count: usize,
    runtime_generation: u64,
    next_request_id: u32,
    slots: Vec<Option<TerrainWorkerTask>>,
}

pub(crate) static TERRAIN_WORKER_POOL: OnceLock<Mutex<TerrainWorkerPool>> = OnceLock::new();

pub(crate) fn terrain_worker_pool() -> &'static Mutex<TerrainWorkerPool> {
    TERRAIN_WORKER_POOL.get_or_init(|| Mutex::new(TerrainWorkerPool::default()))
}

impl Default for TerrainWorkerPool {
    fn default() -> Self {
        Self {
            worker_count: 1,
            runtime_generation: 0,
            next_request_id: 1,
            slots: vec![None],
        }
    }
}

impl TerrainWorkerPool {
    pub(crate) fn configure(&mut self, worker_count: usize) -> Result<(), TerrainWorkerPoolError> {
        if worker_count == 0 {
            return Err(TerrainWorkerPoolError::ZeroWorkers);
        }

        if worker_count > TERRAIN_WORKER_POOL_MAX_WORKERS {
            return Err(TerrainWorkerPoolError::TooManyWorkers);
        }

        self.worker_count = worker_count;
        self.runtime_generation = self.runtime_generation.wrapping_add(1);
        self.next_request_id = 1;
        self.slots.clear();
        self.slots.resize(worker_count, None);
        Ok(())
    }

    pub(crate) fn reset(&mut self) {
        self.runtime_generation = self.runtime_generation.wrapping_add(1);
        self.next_request_id = 1;
        for slot in &mut self.slots {
            *slot = None;
        }
    }

    pub(crate) fn worker_count(&self) -> usize {
        self.worker_count
    }

    pub(crate) fn runtime_generation(&self) -> u64 {
        self.runtime_generation
    }

    pub(crate) fn in_flight_count(&self) -> usize {
        self.slots.iter().filter(|slot| slot.is_some()).count()
    }

    pub(crate) fn begin_task(
        &mut self,
        kind: TerrainWorkerTaskKind,
        lod: u32,
        generation: u64,
        coord: TerrainChunkCoord,
    ) -> Result<TerrainWorkerTask, TerrainWorkerPoolError> {
        let lod = u8::try_from(lod).map_err(|_| TerrainWorkerPoolError::InvalidLod)?;
        let Some(worker_index) = self.slots.iter().position(Option::is_none) else {
            return Err(TerrainWorkerPoolError::NoIdleWorkers);
        };

        let request_id = self.next_request_id;
        if request_id == 0 {
            return Err(TerrainWorkerPoolError::InvalidRequestId);
        }
        self.next_request_id = self.next_non_zero_request_id();

        let task = TerrainWorkerTask {
            request_id,
            worker_index,
            kind,
            lod,
            generation,
            coord,
        };
        self.slots[worker_index] = Some(task);

        Ok(task)
    }

    pub(crate) fn finish_task(
        &mut self,
        request_id: u32,
        kind: TerrainWorkerTaskKind,
        lod: u32,
        generation: u64,
        coord: TerrainChunkCoord,
    ) -> TerrainWorkerTaskFinish {
        let Some(slot_index) = self.slots.iter().position(|slot| {
            slot.as_ref()
                .is_some_and(|task| task.request_id == request_id)
        }) else {
            return TerrainWorkerTaskFinish::Stale;
        };
        let Some(task) = self.slots[slot_index].take() else {
            return TerrainWorkerTaskFinish::Stale;
        };
        let Ok(lod) = u8::try_from(lod) else {
            return TerrainWorkerTaskFinish::Mismatched;
        };
        if task.kind == kind
            && task.lod == lod
            && task.generation == generation
            && task.coord == coord
        {
            TerrainWorkerTaskFinish::Matched
        } else {
            TerrainWorkerTaskFinish::Mismatched
        }
    }

    pub(crate) fn fail_task(&mut self, request_id: u32) -> bool {
        let Some(slot_index) = self.slots.iter().position(|slot| {
            slot.as_ref()
                .is_some_and(|task| task.request_id == request_id)
        }) else {
            return false;
        };
        self.slots[slot_index] = None;
        true
    }

    fn next_non_zero_request_id(&self) -> u32 {
        let mut next = self.next_request_id.wrapping_add(1);
        if next == 0 {
            next = 1;
        }

        next
    }
}
