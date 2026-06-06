// Scene graph and component tests for the browser-free engine core.
use super::*;

#[test]
fn entity_ids_reject_stale_generations_after_reuse() {
    let mut scene = Scene::new();
    let first = scene.create_entity();

    scene.destroy_entity(first).unwrap();
    let second = scene.create_entity();

    assert_eq!(first.index(), second.index());
    assert_ne!(first.generation(), second.generation());
    assert!(!scene.is_alive(first));
    assert!(scene.is_alive(second));
    assert_eq!(
        scene.local_transform(first),
        Err(SceneError::InvalidEntity(first))
    );
}

#[test]
fn entity_ids_round_trip_raw_handles_and_list_live_entities() {
    let mut scene = Scene::new();
    let first = scene.create_entity();
    let child = scene.create_child(first).unwrap();

    let round_tripped = EntityId::from_raw(child.to_raw());

    assert_eq!(round_tripped, child);
    assert_eq!(round_tripped.index(), child.index());
    assert_eq!(round_tripped.generation(), child.generation());
    assert_eq!(scene.entity_ids(), vec![scene.root_id(), first, child]);

    scene.destroy_entity(first).unwrap();

    assert_eq!(scene.entity_ids(), vec![scene.root_id()]);
}

#[test]
fn scene_starts_with_a_root_and_parents_new_entities_under_it() {
    let mut scene = Scene::new();
    let root = scene.root_id();
    let entity = scene.create_entity();

    assert_eq!(scene.entity_count(), 1);
    assert_eq!(scene.parent(root).unwrap(), None);
    assert_eq!(scene.parent(entity).unwrap(), Some(root));
    assert_eq!(scene.children(root).unwrap(), &[entity]);
    assert_eq!(scene.root().id(), root);
    assert_eq!(
        scene.set_parent(root, Some(entity)),
        Err(SceneError::CannotReparentRoot)
    );
    assert_eq!(
        scene.destroy_entity(root),
        Err(SceneError::CannotDestroyRoot)
    );
}

#[test]
fn destroying_an_entity_destroys_descendants() {
    let mut scene = Scene::new();
    let parent = scene.create_entity();
    let child = scene.create_entity();
    let grandchild = scene.create_entity();

    scene.set_parent(child, Some(parent)).unwrap();
    scene.set_parent(grandchild, Some(child)).unwrap();
    scene.destroy_entity(parent).unwrap();

    assert_eq!(scene.entity_count(), 0);
    assert!(!scene.is_alive(parent));
    assert!(!scene.is_alive(child));
    assert!(!scene.is_alive(grandchild));
}

#[test]
fn destroying_an_entity_clears_matching_scene_globals() {
    let mut scene = Scene::new();
    let terrain = scene.create_entity();
    let player = scene.create_child(terrain).unwrap();
    let camera = scene.create_child(player).unwrap();

    scene.set_terrain(Some(terrain)).unwrap();
    scene.set_player(Some(player)).unwrap();
    scene.set_active_camera(Some(camera)).unwrap();
    scene.destroy_entity(terrain).unwrap();

    assert_eq!(scene.terrain_id(), None);
    assert_eq!(scene.player_id(), None);
    assert_eq!(scene.active_camera_id(), None);
}

#[test]
fn scene_rejects_invalid_global_and_parent_handles() {
    let mut scene = Scene::new();
    let invalid = EntityId::from_raw(999);

    assert_eq!(
        scene.create_child(invalid),
        Err(SceneError::InvalidEntity(invalid))
    );
    assert_eq!(
        scene.set_terrain(Some(invalid)),
        Err(SceneError::InvalidEntity(invalid))
    );
    assert_eq!(
        scene.set_player(Some(invalid)),
        Err(SceneError::InvalidEntity(invalid))
    );
    assert_eq!(
        scene.set_active_camera(Some(invalid)),
        Err(SceneError::InvalidEntity(invalid))
    );
    assert_eq!(scene.terrain().err(), Some(SceneError::MissingTerrain));
    assert_eq!(scene.terrain_mut().err(), Some(SceneError::MissingTerrain));
    assert_eq!(scene.player().err(), Some(SceneError::MissingPlayer));
    assert_eq!(scene.player_mut().err(), Some(SceneError::MissingPlayer));
    assert_eq!(
        scene.active_camera().err(),
        Some(SceneError::MissingActiveCamera)
    );
    assert_eq!(
        scene.active_camera_mut().err(),
        Some(SceneError::MissingActiveCamera)
    );
}

#[test]
fn scene_global_accessors_cover_entity_refs_and_mutators() {
    let mut scene = Scene::new();
    let terrain = scene.create_entity();
    let player = scene.create_child(terrain).unwrap();
    let camera = scene.create_child(player).unwrap();

    scene.set_terrain(Some(terrain)).unwrap();
    scene.set_player(Some(player)).unwrap();
    scene.set_active_camera(Some(camera)).unwrap();
    let root_id = scene.root_id();
    let terrain_id = scene.terrain_id().unwrap();

    {
        let mut root = scene.root_mut();
        assert_eq!(root.id(), root_id);
        root.set_local_transform(LocalTransform {
            translation: Vec3::new(10.0, 0.0, 0.0),
            rotation: Quat::IDENTITY,
            scale: Vec3::ONE,
        });
    }
    {
        let mut terrain = scene.terrain_mut().unwrap();
        assert_eq!(terrain.id(), terrain_id);
        assert_eq!(terrain.local_transform(), LocalTransform::default());
        terrain.add_terrain(TerrainComponent { seed: 7, preset: 3 });
        terrain.terrain_mut().unwrap().preset = 9;
    }
    {
        let mut player = scene.player_mut().unwrap();
        player.add_player(PlayerComponent::new(camera));
        player.player_mut().unwrap().yaw = 1.25;
        player.transform_mut().translation = Vec3::new(0.0, 5.0, 0.0);
    }
    {
        let mut camera = scene.active_camera_mut().unwrap();
        camera.add_camera(CameraComponent::default());
        camera.camera_mut().unwrap().near_plane = 0.1;
        camera.set_local_transform(LocalTransform {
            translation: Vec3::new(0.0, 0.0, 2.0),
            rotation: Quat::IDENTITY,
            scale: Vec3::ONE,
        });
    }

    scene.update_world_transforms();

    assert_eq!(scene.root().children(), &[terrain]);
    assert_eq!(scene.terrain().unwrap().parent(), Some(scene.root_id()));
    assert_eq!(scene.terrain().unwrap().children(), &[player]);
    assert_eq!(scene.terrain().unwrap().terrain().unwrap().preset, 9);
    assert_close(scene.player().unwrap().player().unwrap().yaw, 1.25);
    assert_close(
        scene.active_camera().unwrap().camera().unwrap().near_plane,
        0.1,
    );
    assert_vec3_near(
        scene.active_camera().unwrap().world_transform().translation,
        Vec3::new(10.0, 5.0, 2.0),
    );
}

#[test]
fn reparenting_updates_parent_child_relationships() {
    let mut scene = Scene::new();
    let first_parent = scene.create_entity();
    let second_parent = scene.create_entity();
    let child = scene.create_entity();

    scene.set_parent(child, Some(first_parent)).unwrap();
    scene.set_parent(child, Some(second_parent)).unwrap();

    assert_eq!(scene.parent(child).unwrap(), Some(second_parent));
    assert_eq!(scene.children(first_parent).unwrap(), &[]);
    assert_eq!(scene.children(second_parent).unwrap(), &[child]);
}

#[test]
fn entity_accessors_attach_query_and_remove_components() {
    let mut scene = Scene::new();
    let entity = scene.create_entity();
    let camera_entity = scene.create_child(entity).unwrap();
    let mesh = scene.resources_mut().register_mesh("test.mesh");
    let material = scene.resources_mut().register_material("test.material");

    {
        let mut entity = scene.entity_mut(entity).unwrap();
        entity.add_player(PlayerComponent::new(camera_entity));
        entity.add_mesh_renderer(MeshRendererComponent {
            mesh,
            material,
            visible: true,
        });
        entity.transform_mut().translation = Vec3::new(1.0, 2.0, 3.0);
    }

    {
        let mut camera = scene.entity_mut(camera_entity).unwrap();
        camera.add_camera(CameraComponent::default());
    }

    let entity_ref = scene.entity(entity).unwrap();
    assert_eq!(entity_ref.player().unwrap().camera_entity, camera_entity);
    assert_eq!(entity_ref.mesh_renderer().unwrap().mesh, mesh);
    assert_eq!(
        entity_ref.local_transform().translation,
        Vec3::new(1.0, 2.0, 3.0)
    );
    assert!(scene.entity(camera_entity).unwrap().camera().is_some());

    let mut entity_mut = scene.entity_mut(entity).unwrap();
    assert!(entity_mut.remove_mesh_renderer().is_some());
    assert!(entity_mut.mesh_renderer_mut().is_none());
}

#[test]
fn entity_mut_removes_all_component_types() {
    let mut scene = Scene::new();
    let entity = scene.create_entity();
    let camera = scene.create_child(entity).unwrap();
    let mesh = scene.resources_mut().register_mesh("remove.mesh");
    let material = scene.resources_mut().register_material("remove.material");

    {
        let mut entity = scene.entity_mut(entity).unwrap();
        entity.add_camera(CameraComponent::default());
        entity.add_player(PlayerComponent::new(camera));
        entity.add_mesh_renderer(MeshRendererComponent {
            mesh,
            material,
            visible: true,
        });
        entity.add_terrain(TerrainComponent { seed: 4, preset: 5 });
    }

    let mut entity = scene.entity_mut(entity).unwrap();
    assert!(entity.remove_camera().is_some());
    assert!(entity.remove_player().is_some());
    assert!(entity.remove_mesh_renderer().is_some());
    assert!(entity.remove_terrain().is_some());
    assert!(entity.camera_mut().is_none());
    assert!(entity.player_mut().is_none());
    assert!(entity.mesh_renderer_mut().is_none());
    assert!(entity.terrain_mut().is_none());
}

#[test]
fn scene_resources_issue_typed_logical_handles() {
    let mut resources = SceneResources::new();

    let mesh = resources.register_mesh("crate.mesh");
    let material = resources.register_material("crate.material");

    assert_eq!(mesh.index(), 0);
    assert_eq!(mesh.generation(), 0);
    assert_eq!(material.index(), 0);
    assert_eq!(material.generation(), 0);
    assert_eq!(resources.mesh_count(), 1);
    assert_eq!(resources.material_count(), 1);
    assert_eq!(resources.mesh(mesh).unwrap().label, "crate.mesh");
    assert_eq!(
        resources.material(material).unwrap().label,
        "crate.material"
    );
    assert!(resources.mesh(MeshId::new(99, 0)).is_none());
    assert!(resources.material(MaterialId::new(99, 0)).is_none());
}

#[test]
fn player_component_new_uses_first_person_defaults() {
    let camera = EntityId::from_raw(42);
    let player = PlayerComponent::new(camera);

    assert_eq!(player.camera_entity, camera);
    assert_eq!(player.mode, PlayerMode::FirstPerson);
    assert_eq!(player.intent, PlayerMovementIntent::default());
    assert_eq!(player.config, PlayerConfig::default());
    assert_close(player.debug_pitch, -0.35);
}

#[test]
fn parent_cycles_are_rejected() {
    let mut scene = Scene::new();
    let parent = scene.create_entity();
    let child = scene.create_entity();

    scene.set_parent(child, Some(parent)).unwrap();

    assert_eq!(
        scene.set_parent(parent, Some(child)),
        Err(SceneError::EntityHierarchyCycle {
            child: parent,
            parent: child
        })
    );
    assert_eq!(
        scene.set_parent(parent, Some(parent)),
        Err(SceneError::CannotParentEntityToItself(parent))
    );
}

#[test]
fn world_transforms_follow_parent_transforms() {
    let mut scene = Scene::new();
    let parent = scene.create_entity();
    let child = scene.create_entity();

    scene
        .set_local_transform(
            parent,
            LocalTransform {
                translation: Vec3::new(10.0, 2.0, -4.0),
                rotation: Quat::IDENTITY,
                scale: Vec3::new(2.0, 2.0, 2.0),
            },
        )
        .unwrap();
    scene
        .set_local_transform(
            child,
            LocalTransform {
                translation: Vec3::new(1.0, 3.0, 5.0),
                rotation: Quat::IDENTITY,
                scale: Vec3::new(0.5, 1.0, 3.0),
            },
        )
        .unwrap();
    scene.set_parent(child, Some(parent)).unwrap();
    scene.update_world_transforms();

    assert_eq!(
        scene.world_transform(child).unwrap(),
        WorldTransform {
            translation: Vec3::new(12.0, 8.0, 6.0),
            rotation: Quat::IDENTITY,
            scale: Vec3::new(1.0, 2.0, 6.0),
        }
    );
}

#[test]
fn world_transforms_follow_parent_rotation() {
    let mut scene = Scene::new();
    let parent = scene.create_entity();
    let child = scene.create_entity();

    scene
        .set_local_transform(
            parent,
            LocalTransform {
                translation: Vec3::ZERO,
                rotation: Quat::from_yaw(std::f32::consts::FRAC_PI_2),
                scale: Vec3::ONE,
            },
        )
        .unwrap();
    scene
        .set_local_transform(
            child,
            LocalTransform {
                translation: Vec3::new(1.0, 0.0, 0.0),
                rotation: Quat::IDENTITY,
                scale: Vec3::ONE,
            },
        )
        .unwrap();
    scene.set_parent(child, Some(parent)).unwrap();
    scene.update_world_transforms();

    assert_vec3_near(
        scene.world_transform(child).unwrap().translation,
        Vec3::new(0.0, 0.0, -1.0),
    );
}

#[test]
fn world_transform_matrix_uses_translation_rotation_and_scale() {
    let transform = WorldTransform {
        translation: Vec3::new(3.0, 5.0, 7.0),
        rotation: Quat::from_yaw(std::f32::consts::FRAC_PI_2),
        scale: Vec3::new(2.0, 3.0, 4.0),
    };

    let matrix = transform.to_matrix();

    assert_close(matrix[0], 0.0);
    assert_close(matrix[2], -2.0);
    assert_close(matrix[5], 3.0);
    assert_close(matrix[8], 4.0);
    assert_close(matrix[10], 0.0);
    assert_close(matrix[12], 3.0);
    assert_close(matrix[13], 5.0);
    assert_close(matrix[14], 7.0);
}
