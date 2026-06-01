use crate::math::{Quat, Vec3};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LocalTransform {
    pub translation: Vec3,
    pub rotation: Quat,
    pub scale: Vec3,
}

impl Default for LocalTransform {
    fn default() -> Self {
        Self {
            translation: Vec3::ZERO,
            rotation: Quat::IDENTITY,
            scale: Vec3::ONE,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WorldTransform {
    pub translation: Vec3,
    pub rotation: Quat,
    pub scale: Vec3,
}

impl Default for WorldTransform {
    fn default() -> Self {
        Self {
            translation: Vec3::ZERO,
            rotation: Quat::IDENTITY,
            scale: Vec3::ONE,
        }
    }
}

impl WorldTransform {
    fn from_local(local: LocalTransform) -> Self {
        Self {
            translation: local.translation,
            rotation: local.rotation,
            scale: local.scale,
        }
    }

    fn compose(self, local: LocalTransform) -> Self {
        let scaled_translation = local.translation.mul(self.scale);

        Self {
            translation: self
                .translation
                .add(self.rotation.rotate_vec3(scaled_translation)),
            rotation: self.rotation.mul(local.rotation),
            scale: self.scale.mul(local.scale),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct EntityId {
    index: u32,
    generation: u32,
}

impl EntityId {
    pub const fn index(self) -> u32 {
        self.index
    }

    pub const fn generation(self) -> u32 {
        self.generation
    }

    pub const fn from_raw(raw: u64) -> Self {
        Self {
            index: raw as u32,
            generation: (raw >> 32) as u32,
        }
    }

    pub const fn to_raw(self) -> u64 {
        ((self.generation as u64) << 32) | self.index as u64
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum WorldError {
    InvalidEntity(EntityId),
    CannotParentEntityToItself(EntityId),
    EntityHierarchyCycle { child: EntityId, parent: EntityId },
}

#[derive(Clone, Debug)]
struct EntitySlot {
    generation: u32,
    alive: bool,
    parent: Option<EntityId>,
    children: Vec<EntityId>,
    local_transform: LocalTransform,
    world_transform: WorldTransform,
}

impl EntitySlot {
    fn vacant() -> Self {
        Self {
            generation: 0,
            alive: false,
            parent: None,
            children: Vec::new(),
            local_transform: LocalTransform::default(),
            world_transform: WorldTransform::default(),
        }
    }

    fn reset_for_reuse(&mut self) {
        self.alive = true;
        self.parent = None;
        self.children.clear();
        self.local_transform = LocalTransform::default();
        self.world_transform = WorldTransform::default();
    }
}

#[derive(Default)]
pub struct World {
    slots: Vec<EntitySlot>,
    free_indices: Vec<u32>,
    alive_count: usize,
}

impl World {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn create_entity(&mut self) -> EntityId {
        if let Some(index) = self.free_indices.pop() {
            let slot = &mut self.slots[index as usize];
            slot.reset_for_reuse();
            self.alive_count += 1;

            return EntityId {
                index,
                generation: slot.generation,
            };
        }

        let index = self.slots.len() as u32;
        let mut slot = EntitySlot::vacant();
        slot.alive = true;
        self.slots.push(slot);
        self.alive_count += 1;

        EntityId {
            index,
            generation: 0,
        }
    }

    pub fn destroy_entity(&mut self, entity: EntityId) -> Result<(), WorldError> {
        self.validate(entity)?;
        self.destroy_entity_recursive(entity);
        Ok(())
    }

    pub fn is_alive(&self, entity: EntityId) -> bool {
        self.validate(entity).is_ok()
    }

    pub fn entity_count(&self) -> usize {
        self.alive_count
    }

    pub fn parent(&self, entity: EntityId) -> Result<Option<EntityId>, WorldError> {
        Ok(self.slot(entity)?.parent)
    }

    pub fn children(&self, entity: EntityId) -> Result<&[EntityId], WorldError> {
        Ok(&self.slot(entity)?.children)
    }

    pub fn local_transform(&self, entity: EntityId) -> Result<LocalTransform, WorldError> {
        Ok(self.slot(entity)?.local_transform)
    }

    pub fn set_local_transform(
        &mut self,
        entity: EntityId,
        transform: LocalTransform,
    ) -> Result<(), WorldError> {
        self.slot_mut(entity)?.local_transform = transform;
        Ok(())
    }

    pub fn world_transform(&self, entity: EntityId) -> Result<WorldTransform, WorldError> {
        Ok(self.slot(entity)?.world_transform)
    }

    pub fn set_parent(
        &mut self,
        child: EntityId,
        parent: Option<EntityId>,
    ) -> Result<(), WorldError> {
        self.validate(child)?;
        if let Some(parent) = parent {
            self.validate(parent)?;
            if child == parent {
                return Err(WorldError::CannotParentEntityToItself(child));
            }

            if self.would_create_cycle(child, parent)? {
                return Err(WorldError::EntityHierarchyCycle { child, parent });
            }
        }

        let previous_parent = self.slot(child)?.parent;
        if previous_parent == parent {
            return Ok(());
        }

        if let Some(previous_parent) = previous_parent {
            self.remove_child_reference(previous_parent, child);
        }

        self.slot_mut(child)?.parent = parent;
        if let Some(parent) = parent {
            self.slot_mut(parent)?.children.push(child);
        }

        Ok(())
    }

    pub fn update_world_transforms(&mut self) {
        let roots: Vec<_> = self
            .slots
            .iter()
            .enumerate()
            .filter_map(|(index, slot)| {
                if slot.alive && slot.parent.is_none() {
                    Some(EntityId {
                        index: index as u32,
                        generation: slot.generation,
                    })
                } else {
                    None
                }
            })
            .collect();

        for root in roots {
            self.update_world_transform_subtree(root, None);
        }
    }

    fn validate(&self, entity: EntityId) -> Result<(), WorldError> {
        self.slot(entity).map(|_| ())
    }

    fn slot(&self, entity: EntityId) -> Result<&EntitySlot, WorldError> {
        let slot = self
            .slots
            .get(entity.index as usize)
            .ok_or(WorldError::InvalidEntity(entity))?;
        if !slot.alive || slot.generation != entity.generation {
            return Err(WorldError::InvalidEntity(entity));
        }

        Ok(slot)
    }

    fn slot_mut(&mut self, entity: EntityId) -> Result<&mut EntitySlot, WorldError> {
        let slot = self
            .slots
            .get_mut(entity.index as usize)
            .ok_or(WorldError::InvalidEntity(entity))?;
        if !slot.alive || slot.generation != entity.generation {
            return Err(WorldError::InvalidEntity(entity));
        }

        Ok(slot)
    }

    fn destroy_entity_recursive(&mut self, entity: EntityId) {
        let children = self.slots[entity.index as usize].children.clone();
        for child in children {
            if self.is_alive(child) {
                self.destroy_entity_recursive(child);
            }
        }

        if let Some(parent) = self.slots[entity.index as usize].parent {
            self.remove_child_reference(parent, entity);
        }

        let slot = &mut self.slots[entity.index as usize];
        slot.alive = false;
        slot.parent = None;
        slot.children.clear();
        slot.local_transform = LocalTransform::default();
        slot.world_transform = WorldTransform::default();
        slot.generation = slot.generation.wrapping_add(1);
        self.free_indices.push(entity.index);
        self.alive_count -= 1;
    }

    fn remove_child_reference(&mut self, parent: EntityId, child: EntityId) {
        if let Ok(parent_slot) = self.slot_mut(parent) {
            parent_slot.children.retain(|candidate| *candidate != child);
        }
    }

    fn would_create_cycle(&self, child: EntityId, parent: EntityId) -> Result<bool, WorldError> {
        let mut current = Some(parent);
        while let Some(entity) = current {
            if entity == child {
                return Ok(true);
            }

            current = self.slot(entity)?.parent;
        }

        Ok(false)
    }

    fn update_world_transform_subtree(
        &mut self,
        entity: EntityId,
        parent_world: Option<WorldTransform>,
    ) {
        let local = self.slots[entity.index as usize].local_transform;
        let world = match parent_world {
            Some(parent_world) => parent_world.compose(local),
            None => WorldTransform::from_local(local),
        };
        self.slots[entity.index as usize].world_transform = world;

        let children = self.slots[entity.index as usize].children.clone();
        for child in children {
            if self.is_alive(child) {
                self.update_world_transform_subtree(child, Some(world));
            }
        }
    }
}
