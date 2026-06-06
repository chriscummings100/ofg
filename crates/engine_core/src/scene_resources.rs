// Logical scene resources: typed generational IDs for meshes and materials.
use std::hash::{Hash, Hasher};
use std::marker::PhantomData;

pub const DEBUG_PLAYER_MARKER_MESH_LABEL: &str = "debug.player_marker.mesh";
pub const DEBUG_PLAYER_MARKER_MATERIAL_LABEL: &str = "debug.player_marker.material";

#[derive(Debug)]
pub struct ResourceId<T> {
    index: u32,
    generation: u32,
    marker: PhantomData<fn() -> T>,
}

impl<T> ResourceId<T> {
    pub const fn new(index: u32, generation: u32) -> Self {
        Self {
            index,
            generation,
            marker: PhantomData,
        }
    }

    pub const fn index(self) -> u32 {
        self.index
    }

    pub const fn generation(self) -> u32 {
        self.generation
    }
}

impl<T> Copy for ResourceId<T> {}

impl<T> Clone for ResourceId<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T> PartialEq for ResourceId<T> {
    fn eq(&self, other: &Self) -> bool {
        self.index == other.index && self.generation == other.generation
    }
}

impl<T> Eq for ResourceId<T> {}

impl<T> Hash for ResourceId<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.index.hash(state);
        self.generation.hash(state);
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MeshResource {
    pub label: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MaterialResource {
    pub label: String,
}

pub type MeshId = ResourceId<MeshResource>;
pub type MaterialId = ResourceId<MaterialResource>;

#[derive(Clone, Debug)]
struct ResourceSlot<T> {
    generation: u32,
    value: Option<T>,
}

#[derive(Clone, Debug)]
struct ResourceArena<T> {
    slots: Vec<ResourceSlot<T>>,
    free_indices: Vec<u32>,
    len: usize,
}

#[derive(Clone, Debug, Default)]
pub struct SceneResources {
    meshes: ResourceArena<MeshResource>,
    materials: ResourceArena<MaterialResource>,
}

impl SceneResources {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register_mesh(&mut self, label: impl Into<String>) -> MeshId {
        self.meshes.insert(MeshResource {
            label: label.into(),
        })
    }

    pub fn register_material(&mut self, label: impl Into<String>) -> MaterialId {
        self.materials.insert(MaterialResource {
            label: label.into(),
        })
    }

    pub fn mesh(&self, id: MeshId) -> Option<&MeshResource> {
        self.meshes.get(id)
    }

    pub fn material(&self, id: MaterialId) -> Option<&MaterialResource> {
        self.materials.get(id)
    }

    pub fn mesh_count(&self) -> usize {
        self.meshes.len()
    }

    pub fn material_count(&self) -> usize {
        self.materials.len()
    }
}

impl<T> ResourceArena<T> {
    fn insert(&mut self, value: T) -> ResourceId<T> {
        if let Some(index) = self.free_indices.pop() {
            let slot = &mut self.slots[index as usize];
            debug_assert!(slot.value.is_none());
            slot.value = Some(value);
            self.len += 1;
            return ResourceId::new(index, slot.generation);
        }

        let index = self.slots.len() as u32;
        self.slots.push(ResourceSlot {
            generation: 0,
            value: Some(value),
        });
        self.len += 1;
        ResourceId::new(index, 0)
    }

    fn get(&self, id: ResourceId<T>) -> Option<&T> {
        let slot = self.slots.get(id.index as usize)?;
        if slot.generation != id.generation {
            return None;
        }

        slot.value.as_ref()
    }

    fn len(&self) -> usize {
        self.len
    }
}

impl<T> Default for ResourceArena<T> {
    fn default() -> Self {
        Self {
            slots: Vec::new(),
            free_indices: Vec::new(),
            len: 0,
        }
    }
}
