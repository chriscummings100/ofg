// Short-lived access wrappers for reading and mutating scene entities.
use crate::scene::{Entity, EntityId, LocalTransform, WorldTransform};
use crate::scene_components::{
    CameraComponent, MeshRendererComponent, PlayerComponent, TerrainComponent,
};

pub struct EntityRef<'a> {
    pub(crate) id: EntityId,
    pub(crate) entity: &'a Entity,
}

impl EntityRef<'_> {
    pub fn id(&self) -> EntityId {
        self.id
    }

    pub fn parent(&self) -> Option<EntityId> {
        self.entity.parent
    }

    pub fn children(&self) -> &[EntityId] {
        &self.entity.children
    }

    pub fn local_transform(&self) -> LocalTransform {
        self.entity.local_transform
    }

    pub fn world_transform(&self) -> WorldTransform {
        self.entity.world_transform
    }

    pub fn camera(&self) -> Option<&CameraComponent> {
        self.entity.components.camera.as_ref()
    }

    pub fn player(&self) -> Option<&PlayerComponent> {
        self.entity.components.player.as_ref()
    }

    pub fn mesh_renderer(&self) -> Option<&MeshRendererComponent> {
        self.entity.components.mesh_renderer.as_ref()
    }

    pub fn terrain(&self) -> Option<&TerrainComponent> {
        self.entity.components.terrain.as_ref()
    }
}

pub struct EntityMut<'a> {
    pub(crate) id: EntityId,
    pub(crate) entity: &'a mut Entity,
}

impl EntityMut<'_> {
    pub fn id(&self) -> EntityId {
        self.id
    }

    pub fn local_transform(&self) -> LocalTransform {
        self.entity.local_transform
    }

    pub fn set_local_transform(&mut self, transform: LocalTransform) {
        self.entity.local_transform = transform;
    }

    pub fn transform_mut(&mut self) -> &mut LocalTransform {
        &mut self.entity.local_transform
    }

    pub fn add_camera(&mut self, component: CameraComponent) -> &mut CameraComponent {
        self.entity.components.camera.insert(component)
    }

    pub fn camera_mut(&mut self) -> Option<&mut CameraComponent> {
        self.entity.components.camera.as_mut()
    }

    pub fn add_player(&mut self, component: PlayerComponent) -> &mut PlayerComponent {
        self.entity.components.player.insert(component)
    }

    pub fn player_mut(&mut self) -> Option<&mut PlayerComponent> {
        self.entity.components.player.as_mut()
    }

    pub fn add_mesh_renderer(
        &mut self,
        component: MeshRendererComponent,
    ) -> &mut MeshRendererComponent {
        self.entity.components.mesh_renderer.insert(component)
    }

    pub fn mesh_renderer_mut(&mut self) -> Option<&mut MeshRendererComponent> {
        self.entity.components.mesh_renderer.as_mut()
    }

    pub fn add_terrain(&mut self, component: TerrainComponent) -> &mut TerrainComponent {
        self.entity.components.terrain.insert(component)
    }

    pub fn terrain_mut(&mut self) -> Option<&mut TerrainComponent> {
        self.entity.components.terrain.as_mut()
    }

    pub fn remove_camera(&mut self) -> Option<CameraComponent> {
        self.entity.components.camera.take()
    }

    pub fn remove_player(&mut self) -> Option<PlayerComponent> {
        self.entity.components.player.take()
    }

    pub fn remove_mesh_renderer(&mut self) -> Option<MeshRendererComponent> {
        self.entity.components.mesh_renderer.take()
    }

    pub fn remove_terrain(&mut self) -> Option<TerrainComponent> {
        self.entity.components.terrain.take()
    }
}
