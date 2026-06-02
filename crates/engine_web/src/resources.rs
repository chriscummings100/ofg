#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct ResourceHandle {
    slot: u32,
    generation: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ResourceStoreError {
    StaleHandle,
}

#[derive(Clone, Debug)]
struct ResourceSlot<T> {
    generation: u32,
    value: Option<T>,
}

#[derive(Clone, Debug)]
pub struct ResourceStore<T> {
    slots: Vec<ResourceSlot<T>>,
    free_slots: Vec<u32>,
    len: usize,
}

impl ResourceHandle {
    pub const INVALID_RAW: u64 = u64::MAX;

    pub fn from_raw(raw: u64) -> Option<Self> {
        if raw == Self::INVALID_RAW {
            return None;
        }

        Some(Self {
            slot: raw as u32,
            generation: (raw >> 32) as u32,
        })
    }

    pub fn new(slot: u32, generation: u32) -> Self {
        Self { slot, generation }
    }

    pub fn raw(self) -> u64 {
        ((self.generation as u64) << 32) | self.slot as u64
    }

    pub fn slot(self) -> u32 {
        self.slot
    }

    pub fn generation(self) -> u32 {
        self.generation
    }
}

impl<T> ResourceStore<T> {
    pub fn new() -> Self {
        Self {
            slots: Vec::new(),
            free_slots: Vec::new(),
            len: 0,
        }
    }

    pub fn insert(&mut self, value: T) -> ResourceHandle {
        if let Some(slot_index) = self.free_slots.pop() {
            let slot = &mut self.slots[slot_index as usize];
            debug_assert!(slot.value.is_none());
            slot.value = Some(value);
            self.len += 1;
            return ResourceHandle::new(slot_index, slot.generation);
        }

        let slot_index = self.slots.len() as u32;
        self.slots.push(ResourceSlot {
            generation: 0,
            value: Some(value),
        });
        self.len += 1;
        ResourceHandle::new(slot_index, 0)
    }

    pub fn remove(&mut self, handle: ResourceHandle) -> Result<T, ResourceStoreError> {
        let slot = self
            .slots
            .get_mut(handle.slot as usize)
            .ok_or(ResourceStoreError::StaleHandle)?;

        if slot.generation != handle.generation {
            return Err(ResourceStoreError::StaleHandle);
        }

        let Some(value) = slot.value.take() else {
            return Err(ResourceStoreError::StaleHandle);
        };

        slot.generation = slot.generation.wrapping_add(1);
        self.free_slots.push(handle.slot);
        self.len -= 1;
        Ok(value)
    }

    pub fn contains(&self, handle: ResourceHandle) -> bool {
        self.get(handle).is_some()
    }

    pub fn get(&self, handle: ResourceHandle) -> Option<&T> {
        let slot = self.slots.get(handle.slot as usize)?;
        if slot.generation != handle.generation {
            return None;
        }

        slot.value.as_ref()
    }

    pub fn len(&self) -> usize {
        self.len
    }
}

impl<T> Default for ResourceStore<T> {
    fn default() -> Self {
        Self::new()
    }
}
