use std::sync::{Mutex, OnceLock};

pub const ENGINE_CORE_VERSION: u32 = 1;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self::new(0.0, 0.0, 0.0);
    pub const ONE: Self = Self::new(1.0, 1.0, 1.0);

    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    fn add(self, other: Self) -> Self {
        Self::new(self.x + other.x, self.y + other.y, self.z + other.z)
    }

    fn mul(self, other: Self) -> Self {
        Self::new(self.x * other.x, self.y * other.y, self.z * other.z)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Quat {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

impl Quat {
    pub const IDENTITY: Self = Self::new(0.0, 0.0, 0.0, 1.0);

    pub const fn new(x: f32, y: f32, z: f32, w: f32) -> Self {
        Self { x, y, z, w }
    }

    pub fn from_axis_angle(axis: Vec3, angle_radians: f32) -> Self {
        let half_angle = angle_radians * 0.5;
        let s = half_angle.sin();

        Self::new(axis.x * s, axis.y * s, axis.z * s, half_angle.cos()).normalize()
    }

    pub fn from_yaw(yaw_radians: f32) -> Self {
        Self::from_axis_angle(Vec3::new(0.0, 1.0, 0.0), yaw_radians)
    }

    pub fn normalize(self) -> Self {
        let length = (self.x * self.x + self.y * self.y + self.z * self.z + self.w * self.w).sqrt();
        if length <= f32::EPSILON {
            return Self::IDENTITY;
        }

        Self::new(
            self.x / length,
            self.y / length,
            self.z / length,
            self.w / length,
        )
    }

    fn mul(self, other: Self) -> Self {
        Self::new(
            self.w * other.x + self.x * other.w + self.y * other.z - self.z * other.y,
            self.w * other.y - self.x * other.z + self.y * other.w + self.z * other.x,
            self.w * other.z + self.x * other.y - self.y * other.x + self.z * other.w,
            self.w * other.w - self.x * other.x - self.y * other.y - self.z * other.z,
        )
        .normalize()
    }

    fn rotate_vec3(self, value: Vec3) -> Vec3 {
        let q = self.normalize();
        let x2 = q.x + q.x;
        let y2 = q.y + q.y;
        let z2 = q.z + q.z;
        let xx = q.x * x2;
        let yy = q.y * y2;
        let zz = q.z * z2;
        let xy = q.x * y2;
        let xz = q.x * z2;
        let yz = q.y * z2;
        let wx = q.w * x2;
        let wy = q.w * y2;
        let wz = q.w * z2;

        Vec3::new(
            (1.0 - (yy + zz)) * value.x + (xy - wz) * value.y + (xz + wy) * value.z,
            (xy + wz) * value.x + (1.0 - (xx + zz)) * value.y + (yz - wx) * value.z,
            (xz - wy) * value.x + (yz + wx) * value.y + (1.0 - (xx + yy)) * value.z,
        )
    }
}

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

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngineUpdateInput {
    pub delta_seconds: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngineUpdateSummary {
    pub tick: u64,
    pub delta_seconds: f32,
    pub elapsed_seconds: f64,
    pub entity_count: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub enum EngineError {
    InvalidDeltaSeconds(f32),
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngineDebugSnapshot {
    pub version: u32,
    pub tick: u64,
    pub elapsed_seconds: f64,
    pub entity_count: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EngineDebugSnapshotRaw {
    pub version: u32,
    pub tick: u64,
    pub elapsed_seconds: f64,
    pub entity_count: u32,
}

impl EngineDebugSnapshot {
    pub fn into_raw(self) -> EngineDebugSnapshotRaw {
        EngineDebugSnapshotRaw {
            version: self.version,
            tick: self.tick,
            elapsed_seconds: self.elapsed_seconds,
            entity_count: self.entity_count.min(u32::MAX as usize) as u32,
        }
    }
}

#[derive(Default)]
pub struct Engine {
    world: World,
    tick: u64,
    elapsed_seconds: f64,
}

impl Engine {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn world(&self) -> &World {
        &self.world
    }

    pub fn world_mut(&mut self) -> &mut World {
        &mut self.world
    }

    pub fn tick(&self) -> u64 {
        self.tick
    }

    pub fn elapsed_seconds(&self) -> f64 {
        self.elapsed_seconds
    }

    pub fn debug_snapshot(&self) -> EngineDebugSnapshot {
        EngineDebugSnapshot {
            version: ENGINE_CORE_VERSION,
            tick: self.tick,
            elapsed_seconds: self.elapsed_seconds,
            entity_count: self.world.entity_count(),
        }
    }

    pub fn update(&mut self, input: EngineUpdateInput) -> Result<EngineUpdateSummary, EngineError> {
        if !input.delta_seconds.is_finite() || input.delta_seconds < 0.0 {
            return Err(EngineError::InvalidDeltaSeconds(input.delta_seconds));
        }

        self.tick = self.tick.wrapping_add(1);
        self.elapsed_seconds += input.delta_seconds as f64;
        self.world.update_world_transforms();

        Ok(EngineUpdateSummary {
            tick: self.tick,
            delta_seconds: input.delta_seconds,
            elapsed_seconds: self.elapsed_seconds,
            entity_count: self.world.entity_count(),
        })
    }
}

fn with_facade_engine<R>(callback: impl FnOnce(&mut Engine) -> R) -> R {
    static FACADE_ENGINE: OnceLock<Mutex<Engine>> = OnceLock::new();
    let mutex = FACADE_ENGINE.get_or_init(|| Mutex::new(Engine::new()));
    let mut engine = match mutex.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    };

    callback(&mut engine)
}

#[no_mangle]
pub extern "C" fn ofg_engine_core_version() -> u32 {
    ENGINE_CORE_VERSION
}

#[no_mangle]
pub extern "C" fn ofg_engine_create() {
    with_facade_engine(|engine| *engine = Engine::new());
}

#[no_mangle]
pub extern "C" fn ofg_engine_create_entity() -> u64 {
    with_facade_engine(|engine| engine.world_mut().create_entity().to_raw())
}

#[no_mangle]
pub extern "C" fn ofg_engine_update(delta_seconds: f32) -> u32 {
    with_facade_engine(|engine| {
        engine
            .update(EngineUpdateInput { delta_seconds })
            .map(|_| 1)
            .unwrap_or(0)
    })
}

#[no_mangle]
pub extern "C" fn ofg_engine_debug_snapshot() -> EngineDebugSnapshotRaw {
    with_facade_engine(|engine| engine.debug_snapshot().into_raw())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn entity_ids_reject_stale_generations_after_reuse() {
        let mut world = World::new();
        let first = world.create_entity();

        world.destroy_entity(first).unwrap();
        let second = world.create_entity();

        assert_eq!(first.index(), second.index());
        assert_ne!(first.generation(), second.generation());
        assert!(!world.is_alive(first));
        assert!(world.is_alive(second));
        assert_eq!(
            world.local_transform(first),
            Err(WorldError::InvalidEntity(first))
        );
    }

    #[test]
    fn destroying_an_entity_destroys_descendants() {
        let mut world = World::new();
        let parent = world.create_entity();
        let child = world.create_entity();
        let grandchild = world.create_entity();

        world.set_parent(child, Some(parent)).unwrap();
        world.set_parent(grandchild, Some(child)).unwrap();
        world.destroy_entity(parent).unwrap();

        assert_eq!(world.entity_count(), 0);
        assert!(!world.is_alive(parent));
        assert!(!world.is_alive(child));
        assert!(!world.is_alive(grandchild));
    }

    #[test]
    fn reparenting_updates_parent_child_relationships() {
        let mut world = World::new();
        let first_parent = world.create_entity();
        let second_parent = world.create_entity();
        let child = world.create_entity();

        world.set_parent(child, Some(first_parent)).unwrap();
        world.set_parent(child, Some(second_parent)).unwrap();

        assert_eq!(world.parent(child).unwrap(), Some(second_parent));
        assert_eq!(world.children(first_parent).unwrap(), &[]);
        assert_eq!(world.children(second_parent).unwrap(), &[child]);
    }

    #[test]
    fn parent_cycles_are_rejected() {
        let mut world = World::new();
        let parent = world.create_entity();
        let child = world.create_entity();

        world.set_parent(child, Some(parent)).unwrap();

        assert_eq!(
            world.set_parent(parent, Some(child)),
            Err(WorldError::EntityHierarchyCycle {
                child: parent,
                parent: child
            })
        );
        assert_eq!(
            world.set_parent(parent, Some(parent)),
            Err(WorldError::CannotParentEntityToItself(parent))
        );
    }

    #[test]
    fn world_transforms_follow_parent_transforms() {
        let mut world = World::new();
        let parent = world.create_entity();
        let child = world.create_entity();

        world
            .set_local_transform(
                parent,
                LocalTransform {
                    translation: Vec3::new(10.0, 2.0, -4.0),
                    rotation: Quat::IDENTITY,
                    scale: Vec3::new(2.0, 2.0, 2.0),
                },
            )
            .unwrap();
        world
            .set_local_transform(
                child,
                LocalTransform {
                    translation: Vec3::new(1.0, 3.0, 5.0),
                    rotation: Quat::IDENTITY,
                    scale: Vec3::new(0.5, 1.0, 3.0),
                },
            )
            .unwrap();
        world.set_parent(child, Some(parent)).unwrap();
        world.update_world_transforms();

        assert_eq!(
            world.world_transform(child).unwrap(),
            WorldTransform {
                translation: Vec3::new(12.0, 8.0, 6.0),
                rotation: Quat::IDENTITY,
                scale: Vec3::new(1.0, 2.0, 6.0),
            }
        );
    }

    #[test]
    fn world_transforms_follow_parent_rotation() {
        let mut world = World::new();
        let parent = world.create_entity();
        let child = world.create_entity();

        world
            .set_local_transform(
                parent,
                LocalTransform {
                    translation: Vec3::ZERO,
                    rotation: Quat::from_yaw(std::f32::consts::FRAC_PI_2),
                    scale: Vec3::ONE,
                },
            )
            .unwrap();
        world
            .set_local_transform(
                child,
                LocalTransform {
                    translation: Vec3::new(1.0, 0.0, 0.0),
                    rotation: Quat::IDENTITY,
                    scale: Vec3::ONE,
                },
            )
            .unwrap();
        world.set_parent(child, Some(parent)).unwrap();
        world.update_world_transforms();

        assert_vec3_near(
            world.world_transform(child).unwrap().translation,
            Vec3::new(0.0, 0.0, -1.0),
        );
    }

    #[test]
    fn engine_updates_are_deterministic_for_identical_inputs() {
        let mut first = Engine::new();
        let mut second = Engine::new();

        first.world_mut().create_entity();
        second.world_mut().create_entity();

        let first_summary = first
            .update(EngineUpdateInput {
                delta_seconds: 1.0 / 60.0,
            })
            .unwrap();
        let second_summary = second
            .update(EngineUpdateInput {
                delta_seconds: 1.0 / 60.0,
            })
            .unwrap();

        assert_eq!(first_summary, second_summary);
        assert_eq!(first.tick(), 1);
        assert_eq!(first.elapsed_seconds(), (1.0_f32 / 60.0) as f64);
    }

    #[test]
    fn engine_rejects_non_finite_or_negative_delta_time() {
        let mut engine = Engine::new();

        match engine.update(EngineUpdateInput {
            delta_seconds: f32::NAN,
        }) {
            Err(EngineError::InvalidDeltaSeconds(value)) => assert!(value.is_nan()),
            result => panic!("expected invalid NaN delta, got {result:?}"),
        }
        assert_eq!(
            engine.update(EngineUpdateInput {
                delta_seconds: -0.1,
            }),
            Err(EngineError::InvalidDeltaSeconds(-0.1))
        );
        assert_eq!(engine.tick(), 0);
    }

    #[test]
    fn wasm_facade_can_reset_engine_and_report_debug_state() {
        ofg_engine_create();

        let empty = ofg_engine_debug_snapshot();
        assert_eq!(empty.version, ENGINE_CORE_VERSION);
        assert_eq!(empty.tick, 0);
        assert_eq!(empty.entity_count, 0);

        let entity = EntityId::from_raw(ofg_engine_create_entity());
        assert_eq!(entity.index(), 0);
        assert_eq!(entity.generation(), 0);
        assert_eq!(ofg_engine_update(0.25), 1);
        assert_eq!(ofg_engine_update(f32::INFINITY), 0);

        let updated = ofg_engine_debug_snapshot();
        assert_eq!(updated.version, ENGINE_CORE_VERSION);
        assert_eq!(updated.tick, 1);
        assert_eq!(updated.entity_count, 1);
        assert_eq!(updated.elapsed_seconds, 0.25);

        ofg_engine_create();
        let reset = ofg_engine_debug_snapshot();
        assert_eq!(reset.tick, 0);
        assert_eq!(reset.entity_count, 0);
    }

    fn assert_vec3_near(actual: Vec3, expected: Vec3) {
        let epsilon = 1.0e-5;
        assert!(
            (actual.x - expected.x).abs() <= epsilon
                && (actual.y - expected.y).abs() <= epsilon
                && (actual.z - expected.z).abs() <= epsilon,
            "expected {actual:?} to be within {epsilon} of {expected:?}"
        );
    }
}
