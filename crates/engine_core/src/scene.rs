// Browser-free scene graph primitives: stable entity IDs, hierarchy, transforms, and components.
use crate::math::{Quat, Vec3};
use crate::scene_access::{EntityMut, EntityRef};
use crate::scene_components::Components;
use crate::scene_resources::SceneResources;

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

    pub fn to_matrix(self) -> [f32; 16] {
        let rotation = self.rotation.normalize();
        let x2 = rotation.x + rotation.x;
        let y2 = rotation.y + rotation.y;
        let z2 = rotation.z + rotation.z;
        let xx = rotation.x * x2;
        let yy = rotation.y * y2;
        let zz = rotation.z * z2;
        let xy = rotation.x * y2;
        let xz = rotation.x * z2;
        let yz = rotation.y * z2;
        let wx = rotation.w * x2;
        let wy = rotation.w * y2;
        let wz = rotation.w * z2;

        [
            (1.0 - (yy + zz)) * self.scale.x,
            (xy + wz) * self.scale.x,
            (xz - wy) * self.scale.x,
            0.0,
            (xy - wz) * self.scale.y,
            (1.0 - (xx + zz)) * self.scale.y,
            (yz + wx) * self.scale.y,
            0.0,
            (xz + wy) * self.scale.z,
            (yz - wx) * self.scale.z,
            (1.0 - (xx + yy)) * self.scale.z,
            0.0,
            self.translation.x,
            self.translation.y,
            self.translation.z,
            1.0,
        ]
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
pub enum SceneError {
    InvalidEntity(EntityId),
    CannotParentEntityToItself(EntityId),
    EntityHierarchyCycle { child: EntityId, parent: EntityId },
    CannotDestroyRoot,
    CannotReparentRoot,
    MissingTerrain,
    MissingPlayer,
    MissingActiveCamera,
}

#[derive(Clone, Debug)]
pub struct Entity {
    pub(crate) generation: u32,
    pub(crate) alive: bool,
    pub(crate) parent: Option<EntityId>,
    pub(crate) children: Vec<EntityId>,
    pub(crate) local_transform: LocalTransform,
    pub(crate) world_transform: WorldTransform,
    pub(crate) components: Components,
}

impl Entity {
    fn vacant() -> Self {
        Self {
            generation: 0,
            alive: false,
            parent: None,
            children: Vec::new(),
            local_transform: LocalTransform::default(),
            world_transform: WorldTransform::default(),
            components: Components::default(),
        }
    }

    fn reset_for_reuse(&mut self) {
        self.alive = true;
        self.parent = None;
        self.children.clear();
        self.local_transform = LocalTransform::default();
        self.world_transform = WorldTransform::default();
        self.components = Components::default();
    }
}

pub struct Scene {
    entities: Vec<Entity>,
    free_indices: Vec<u32>,
    alive_count: usize,
    root: EntityId,
    terrain: Option<EntityId>,
    player: Option<EntityId>,
    active_camera: Option<EntityId>,
    resources: SceneResources,
}

impl Scene {
    pub fn new() -> Self {
        let root = EntityId {
            index: 0,
            generation: 0,
        };
        let mut root_entity = Entity::vacant();
        root_entity.alive = true;

        Self {
            entities: vec![root_entity],
            free_indices: Vec::new(),
            alive_count: 1,
            root,
            terrain: None,
            player: None,
            active_camera: None,
            resources: SceneResources::new(),
        }
    }

    pub fn root_id(&self) -> EntityId {
        self.root
    }

    pub fn terrain_id(&self) -> Option<EntityId> {
        self.terrain
    }

    pub fn player_id(&self) -> Option<EntityId> {
        self.player
    }

    pub fn active_camera_id(&self) -> Option<EntityId> {
        self.active_camera
    }

    pub fn set_terrain(&mut self, entity: Option<EntityId>) -> Result<(), SceneError> {
        self.validate_optional(entity)?;
        self.terrain = entity;
        Ok(())
    }

    pub fn set_player(&mut self, entity: Option<EntityId>) -> Result<(), SceneError> {
        self.validate_optional(entity)?;
        self.player = entity;
        Ok(())
    }

    pub fn set_active_camera(&mut self, entity: Option<EntityId>) -> Result<(), SceneError> {
        self.validate_optional(entity)?;
        self.active_camera = entity;
        Ok(())
    }

    pub fn create_entity(&mut self) -> EntityId {
        let entity = self.allocate_entity();
        self.set_parent(entity, Some(self.root))
            .expect("new entity should parent under the scene root");
        entity
    }

    pub fn create_child(&mut self, parent: EntityId) -> Result<EntityId, SceneError> {
        self.validate(parent)?;
        let entity = self.allocate_entity();
        self.set_parent(entity, Some(parent))?;
        Ok(entity)
    }

    fn allocate_entity(&mut self) -> EntityId {
        if let Some(index) = self.free_indices.pop() {
            let entity = &mut self.entities[index as usize];
            entity.reset_for_reuse();
            self.alive_count += 1;

            return EntityId {
                index,
                generation: entity.generation,
            };
        }

        let index = self.entities.len() as u32;
        let mut entity = Entity::vacant();
        entity.alive = true;
        self.entities.push(entity);
        self.alive_count += 1;

        EntityId {
            index,
            generation: 0,
        }
    }

    pub fn destroy_entity(&mut self, entity: EntityId) -> Result<(), SceneError> {
        if entity == self.root {
            return Err(SceneError::CannotDestroyRoot);
        }
        self.validate(entity)?;
        self.destroy_entity_recursive(entity);
        Ok(())
    }

    pub fn is_alive(&self, entity: EntityId) -> bool {
        self.validate(entity).is_ok()
    }

    pub fn entity_count(&self) -> usize {
        self.alive_count.saturating_sub(1)
    }

    pub fn entity_ids(&self) -> Vec<EntityId> {
        self.entities
            .iter()
            .enumerate()
            .filter_map(|(index, entity)| {
                if !entity.alive {
                    return None;
                }

                Some(EntityId {
                    index: index as u32,
                    generation: entity.generation,
                })
            })
            .collect()
    }

    pub fn entity(&self, entity: EntityId) -> Result<EntityRef<'_>, SceneError> {
        Ok(EntityRef {
            id: entity,
            entity: self.entity_record(entity)?,
        })
    }

    pub fn entity_mut(&mut self, entity: EntityId) -> Result<EntityMut<'_>, SceneError> {
        Ok(EntityMut {
            id: entity,
            entity: self.entity_record_mut(entity)?,
        })
    }

    pub fn root(&self) -> EntityRef<'_> {
        self.entity(self.root)
            .expect("scene root should always be alive")
    }

    pub fn root_mut(&mut self) -> EntityMut<'_> {
        self.entity_mut(self.root)
            .expect("scene root should always be alive")
    }

    pub fn terrain(&self) -> Result<EntityRef<'_>, SceneError> {
        self.entity(self.terrain.ok_or(SceneError::MissingTerrain)?)
    }

    pub fn terrain_mut(&mut self) -> Result<EntityMut<'_>, SceneError> {
        self.entity_mut(self.terrain.ok_or(SceneError::MissingTerrain)?)
    }

    pub fn player(&self) -> Result<EntityRef<'_>, SceneError> {
        self.entity(self.player.ok_or(SceneError::MissingPlayer)?)
    }

    pub fn player_mut(&mut self) -> Result<EntityMut<'_>, SceneError> {
        self.entity_mut(self.player.ok_or(SceneError::MissingPlayer)?)
    }

    pub fn active_camera(&self) -> Result<EntityRef<'_>, SceneError> {
        self.entity(self.active_camera.ok_or(SceneError::MissingActiveCamera)?)
    }

    pub fn active_camera_mut(&mut self) -> Result<EntityMut<'_>, SceneError> {
        self.entity_mut(self.active_camera.ok_or(SceneError::MissingActiveCamera)?)
    }

    pub fn resources(&self) -> &SceneResources {
        &self.resources
    }

    pub fn resources_mut(&mut self) -> &mut SceneResources {
        &mut self.resources
    }

    pub fn parent(&self, entity: EntityId) -> Result<Option<EntityId>, SceneError> {
        Ok(self.entity_record(entity)?.parent)
    }

    pub fn children(&self, entity: EntityId) -> Result<&[EntityId], SceneError> {
        Ok(&self.entity_record(entity)?.children)
    }

    pub fn local_transform(&self, entity: EntityId) -> Result<LocalTransform, SceneError> {
        Ok(self.entity_record(entity)?.local_transform)
    }

    pub fn set_local_transform(
        &mut self,
        entity: EntityId,
        transform: LocalTransform,
    ) -> Result<(), SceneError> {
        self.entity_record_mut(entity)?.local_transform = transform;
        Ok(())
    }

    pub fn world_transform(&self, entity: EntityId) -> Result<WorldTransform, SceneError> {
        Ok(self.entity_record(entity)?.world_transform)
    }

    pub fn set_parent(
        &mut self,
        child: EntityId,
        parent: Option<EntityId>,
    ) -> Result<(), SceneError> {
        if child == self.root {
            return Err(SceneError::CannotReparentRoot);
        }
        self.validate(child)?;
        let parent = parent.unwrap_or(self.root);
        self.validate(parent)?;
        if child == parent {
            return Err(SceneError::CannotParentEntityToItself(child));
        }

        if self.would_create_cycle(child, parent)? {
            return Err(SceneError::EntityHierarchyCycle { child, parent });
        }

        let previous_parent = self.entity_record(child)?.parent;
        if previous_parent == Some(parent) {
            return Ok(());
        }

        if let Some(previous_parent) = previous_parent {
            self.remove_child_reference(previous_parent, child);
        }

        self.entity_record_mut(child)?.parent = Some(parent);
        self.entity_record_mut(parent)?.children.push(child);

        Ok(())
    }

    pub fn update_world_transforms(&mut self) {
        if self.is_alive(self.root) {
            self.update_world_transform_subtree(self.root, None);
        }
    }

    fn validate(&self, entity: EntityId) -> Result<(), SceneError> {
        self.entity_record(entity).map(|_| ())
    }

    fn validate_optional(&self, entity: Option<EntityId>) -> Result<(), SceneError> {
        if let Some(entity) = entity {
            self.validate(entity)?;
        }

        Ok(())
    }

    fn entity_record(&self, entity: EntityId) -> Result<&Entity, SceneError> {
        let entity_record = self
            .entities
            .get(entity.index as usize)
            .ok_or(SceneError::InvalidEntity(entity))?;
        if !entity_record.alive || entity_record.generation != entity.generation {
            return Err(SceneError::InvalidEntity(entity));
        }

        Ok(entity_record)
    }

    fn entity_record_mut(&mut self, entity: EntityId) -> Result<&mut Entity, SceneError> {
        let entity_record = self
            .entities
            .get_mut(entity.index as usize)
            .ok_or(SceneError::InvalidEntity(entity))?;
        if !entity_record.alive || entity_record.generation != entity.generation {
            return Err(SceneError::InvalidEntity(entity));
        }

        Ok(entity_record)
    }

    fn destroy_entity_recursive(&mut self, entity: EntityId) {
        let children = self.entities[entity.index as usize].children.clone();
        for child in children {
            if self.is_alive(child) {
                self.destroy_entity_recursive(child);
            }
        }

        if self.terrain == Some(entity) {
            self.terrain = None;
        }
        if self.player == Some(entity) {
            self.player = None;
        }
        if self.active_camera == Some(entity) {
            self.active_camera = None;
        }

        if let Some(parent) = self.entities[entity.index as usize].parent {
            self.remove_child_reference(parent, entity);
        }

        let entity_record = &mut self.entities[entity.index as usize];
        entity_record.alive = false;
        entity_record.parent = None;
        entity_record.children.clear();
        entity_record.local_transform = LocalTransform::default();
        entity_record.world_transform = WorldTransform::default();
        entity_record.components = Components::default();
        entity_record.generation = entity_record.generation.wrapping_add(1);
        self.free_indices.push(entity.index);
        self.alive_count -= 1;
    }

    fn remove_child_reference(&mut self, parent: EntityId, child: EntityId) {
        if let Ok(parent_entity) = self.entity_record_mut(parent) {
            parent_entity
                .children
                .retain(|candidate| *candidate != child);
        }
    }

    fn would_create_cycle(&self, child: EntityId, parent: EntityId) -> Result<bool, SceneError> {
        let mut current = Some(parent);
        while let Some(entity) = current {
            if entity == child {
                return Ok(true);
            }

            current = self.entity_record(entity)?.parent;
        }

        Ok(false)
    }

    fn update_world_transform_subtree(
        &mut self,
        entity: EntityId,
        parent_world: Option<WorldTransform>,
    ) {
        let local = self.entities[entity.index as usize].local_transform;
        let world = match parent_world {
            Some(parent_world) => parent_world.compose(local),
            None => WorldTransform::from_local(local),
        };
        self.entities[entity.index as usize].world_transform = world;

        let children = self.entities[entity.index as usize].children.clone();
        for child in children {
            if self.is_alive(child) {
                self.update_world_transform_subtree(child, Some(world));
            }
        }
    }
}

impl Default for Scene {
    fn default() -> Self {
        Self::new()
    }
}
