// Doctest coverage for scene-owned animation-player component behavior.
//
// These tests keep animation-specific scene ownership and update-order checks
// out of the broader scene graph test file as model animation support grows.
#include "doctest.h"

#include "ofg/animation/animation_clip.hpp"
#include "ofg/core/control_input.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/entity.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

#include <utility>
#include <vector>

// Verifies animation-player components are scene-owned and exposed by index.
TEST_CASE("scene creates animation player components in stable order") {
    ofg::Scene scene;
    ofg::Entity* first = scene.create_entity(scene.get_root());
    ofg::Entity* second = scene.create_entity(scene.get_root());

    ofg::Component* first_component = first->create_component(ofg::ComponentType::AnimationPlayer);
    ofg::Component* second_component = second->create_component(ofg::ComponentType::AnimationPlayer);

    REQUIRE(first_component != nullptr);
    REQUIRE(second_component != nullptr);
    CHECK(first_component->type() == ofg::ComponentType::AnimationPlayer);
    CHECK(first_component->entity() == first);
    CHECK(first->animation_player() == first_component);
    CHECK(second->animation_player() == second_component);
    CHECK(scene.animation_player_count() == 2);
    CHECK(scene.get_animation_player(0) == first->animation_player());
    CHECK(scene.get_animation_player(1) == second->animation_player());
    CHECK(scene.get_animation_player(2) == nullptr);
    CHECK_THROWS_WITH_AS(([&]() { (void)first->create_component(ofg::ComponentType::AnimationPlayer); }()),
        doctest::Contains("AnimationPlayer"),
        ofg::EngineError);

    scene.clear();
    CHECK(scene.animation_player_count() == 0);
    CHECK(scene.get_animation_player(0) == nullptr);
}

// Verifies scene update runs players, animation players, then cameras in same-frame order.
TEST_CASE("scene update applies player movement, animation, then camera follow") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    (void)player_entity->create_component(ofg::ComponentType::Player);
    (void)player_entity->create_component(ofg::ComponentType::AnimationPlayer);
    (void)camera_entity->create_component(ofg::ComponentType::Camera);
    ofg::Player* player = player_entity->player();
    ofg::AnimationPlayer* animation_player = player_entity->animation_player();
    ofg::Camera* camera = camera_entity->camera();
    REQUIRE(player != nullptr);
    REQUIRE(animation_player != nullptr);
    REQUIRE(camera != nullptr);
    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);

    ofg::AnimationClip clip("scene update order test clip");
    ofg::AnimationChannel channel;
    channel.m_target_node_index = 0;
    channel.m_target_path = ofg::AnimationTargetPath::Translation;
    channel.m_input_times_seconds = {0.0, 1.0};
    channel.m_output_values = {ofg::math::vec4(10.0f, 0.0f, 0.0f, 0.0f), ofg::math::vec4(10.0f, 0.0f, 0.0f, 0.0f)};
    clip.add_channel(std::move(channel));
    clip.set_duration_seconds(1.0);

    std::vector<ofg::Ptr<ofg::Entity>> animation_targets;
    animation_targets.emplace_back(player_entity);
    animation_player->bind_targets(std::move(animation_targets));
    animation_player->play(clip, false);

    ofg::ControlInput controls;
    controls.m_move_z = 1.0f;
    ofg::SceneUpdateContext context{controls, 1000.0, 1.0f, player, camera};
    scene.update(context);

    CHECK(player_entity->local_transform().m_position.x == doctest::Approx(10.0f));
    CHECK(player_entity->local_transform().m_position.y == doctest::Approx(0.0f));
    CHECK(player_entity->local_transform().m_position.z == doctest::Approx(0.0f));
    CHECK(camera_entity->local_transform().m_position.x == doctest::Approx(10.0f));
    CHECK(camera_entity->local_transform().m_position.y == doctest::Approx(0.7f));
    CHECK(camera_entity->local_transform().m_position.z == doctest::Approx(0.28f));
}
