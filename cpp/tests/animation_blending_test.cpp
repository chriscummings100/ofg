// Doctest coverage for animation blending and player locomotion weights.
//
// These tests keep generic clip blending and player-owned locomotion animation
// separate from the lower-level glTF import tests.
#include "doctest.h"

#include "ofg/animation/animation_clip.hpp"
#include "ofg/core/control_input.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/camera.hpp"
#include "ofg/scene/player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

// Builds a constant one-channel animation clip.
std::unique_ptr<ofg::AnimationClip> make_constant_clip(
    std::string name, ofg::AnimationTargetPath path, ofg::math::Vec4 value) {
    auto clip = std::make_unique<ofg::AnimationClip>(std::move(name));
    ofg::AnimationChannel channel;
    channel.m_target_node_index = 0;
    channel.m_target_path = path;
    channel.m_input_times_seconds = {0.0, 1.0};
    channel.m_output_values = {value, value};
    clip->add_channel(std::move(channel));
    clip->set_duration_seconds(1.0);
    return clip;
}

// Builds a two-key linear animation clip.
std::unique_ptr<ofg::AnimationClip> make_linear_clip(
    std::string name, ofg::AnimationTargetPath path, ofg::math::Vec4 start, ofg::math::Vec4 end) {
    auto clip = std::make_unique<ofg::AnimationClip>(std::move(name));
    ofg::AnimationChannel channel;
    channel.m_target_node_index = 0;
    channel.m_target_path = path;
    channel.m_input_times_seconds = {0.0, 1.0};
    channel.m_output_values = {start, end};
    clip->add_channel(std::move(channel));
    clip->set_duration_seconds(1.0);
    return clip;
}

// Binds one target entity to an animation player.
void bind_single_target(ofg::AnimationPlayer& animation_player, ofg::Entity& target) {
    std::vector<ofg::Ptr<ofg::Entity>> targets;
    targets.emplace_back(&target);
    animation_player.bind_targets(std::move(targets));
}

} // namespace

// Verifies animation clips reject invalid metadata and preserve valid channels.
TEST_CASE("animation clip validates names durations and channel keyframes") {
    CHECK_THROWS_WITH_AS(([]() { ofg::AnimationClip clip{""}; }()), doctest::Contains("name"), ofg::EngineError);

    ofg::AnimationClip clip{"validation"};
    CHECK(clip.name() == "validation");
    CHECK(clip.duration_seconds() == doctest::Approx(0.0));
    CHECK(clip.channels().empty());
    CHECK_THROWS_WITH_AS(clip.set_duration_seconds(-1.0), doctest::Contains("duration"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(clip.set_duration_seconds(std::numeric_limits<double>::infinity()),
        doctest::Contains("duration"),
        ofg::EngineError);

    ofg::AnimationChannel mismatched;
    mismatched.m_input_times_seconds = {0.0, 1.0};
    mismatched.m_output_values = {ofg::math::vec4(0.0f, 0.0f, 0.0f, 0.0f)};
    CHECK_THROWS_WITH_AS(clip.add_channel(std::move(mismatched)), doctest::Contains("counts"), ofg::EngineError);

    ofg::AnimationChannel empty;
    CHECK_THROWS_WITH_AS(clip.add_channel(std::move(empty)), doctest::Contains("keyframe"), ofg::EngineError);

    ofg::AnimationChannel valid;
    valid.m_target_node_index = 3;
    valid.m_target_path = ofg::AnimationTargetPath::Scale;
    valid.m_interpolation = ofg::AnimationInterpolation::Step;
    valid.m_input_times_seconds = {0.0};
    valid.m_output_values = {ofg::math::vec4(1.0f, 2.0f, 3.0f, 0.0f)};
    clip.add_channel(std::move(valid));
    clip.set_duration_seconds(2.5);
    REQUIRE(clip.channels().size() == 1);
    CHECK(clip.duration_seconds() == doctest::Approx(2.5));
    CHECK(clip.channels()[0].m_target_node_index == 3);
    CHECK(clip.channels()[0].m_target_path == ofg::AnimationTargetPath::Scale);
    CHECK(clip.channels()[0].m_interpolation == ofg::AnimationInterpolation::Step);
}

// Verifies weighted clip states normalize translation contributions per target.
TEST_CASE("animation player blends weighted translation clip states") {
    ofg::Scene scene;
    ofg::Entity* target = scene.create_entity(scene.get_root());
    REQUIRE(target != nullptr);
    target->local_transform().m_position = ofg::math::vec3(42.0f, 0.0f, 0.0f);
    auto* animation_player =
        static_cast<ofg::AnimationPlayer*>(scene.get_root()->create_component(ofg::ComponentType::AnimationPlayer));
    bind_single_target(*animation_player, *target);

    std::unique_ptr<ofg::AnimationClip> low =
        make_constant_clip("low", ofg::AnimationTargetPath::Translation, ofg::math::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::unique_ptr<ofg::AnimationClip> high =
        make_constant_clip("high", ofg::AnimationTargetPath::Translation, ofg::math::vec4(8.0f, 0.0f, 0.0f, 0.0f));
    animation_player->set_clip_state(*low, 1.0f);
    animation_player->set_clip_state(*high, 3.0f);
    CHECK(animation_player->clip_states().size() == 2);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, nullptr, nullptr};
    animation_player->update(context);
    CHECK(target->local_transform().m_position.x == doctest::Approx(6.0f));

    animation_player->set_clip_weight(*low, 0.0f);
    animation_player->set_clip_weight(*high, 0.0f);
    animation_player->update(context);
    CHECK(target->local_transform().m_position.x == doctest::Approx(42.0f));
}

// Verifies rotation blending produces normalized shortest-path quaternions.
TEST_CASE("animation player blends rotation clip states") {
    ofg::Scene scene;
    ofg::Entity* target = scene.create_entity(scene.get_root());
    REQUIRE(target != nullptr);
    auto* animation_player =
        static_cast<ofg::AnimationPlayer*>(scene.get_root()->create_component(ofg::ComponentType::AnimationPlayer));
    bind_single_target(*animation_player, *target);

    std::unique_ptr<ofg::AnimationClip> identity =
        make_constant_clip("identity", ofg::AnimationTargetPath::Rotation, ofg::math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    std::unique_ptr<ofg::AnimationClip> half_turn =
        make_constant_clip("half turn", ofg::AnimationTargetPath::Rotation, ofg::math::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    animation_player->set_clip_state(*identity, 1.0f);
    animation_player->set_clip_state(*half_turn, 1.0f);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, nullptr, nullptr};
    animation_player->update(context);
    CHECK(target->local_transform().m_rotation.y == doctest::Approx(0.707106f).epsilon(0.0001));
    CHECK(target->local_transform().m_rotation.w == doctest::Approx(0.707106f).epsilon(0.0001));
}

// Verifies scale channels, playback state accessors, and stopped playback behavior.
TEST_CASE("animation player applies scale channels and stops cleanly") {
    ofg::Scene scene;
    ofg::Entity* target = scene.create_entity(scene.get_root());
    REQUIRE(target != nullptr);
    target->local_transform().m_scale = ofg::math::vec3(4.0f, 4.0f, 4.0f);
    auto* animation_player =
        static_cast<ofg::AnimationPlayer*>(scene.get_root()->create_component(ofg::ComponentType::AnimationPlayer));
    bind_single_target(*animation_player, *target);

    std::unique_ptr<ofg::AnimationClip> scale = make_linear_clip("scale",
        ofg::AnimationTargetPath::Scale,
        ofg::math::vec4(1.0f, 1.0f, 1.0f, 0.0f),
        ofg::math::vec4(3.0f, 5.0f, 7.0f, 0.0f));
    animation_player->play(*scale, false);
    CHECK(animation_player->clip() == scale.get());

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 500.0, 0.5f, nullptr, nullptr};
    animation_player->update(context);
    CHECK(animation_player->time_seconds() == doctest::Approx(0.5));
    CHECK(target->local_transform().m_scale.x == doctest::Approx(2.0f));
    CHECK(target->local_transform().m_scale.y == doctest::Approx(3.0f));
    CHECK(target->local_transform().m_scale.z == doctest::Approx(4.0f));

    animation_player->stop();
    target->local_transform().m_scale = ofg::math::vec3(9.0f, 9.0f, 9.0f);
    animation_player->update(context);
    CHECK(target->local_transform().m_scale.x == doctest::Approx(9.0f));

    animation_player->clear_clip_states();
    CHECK(animation_player->clip() == nullptr);
    CHECK(animation_player->time_seconds() == doctest::Approx(0.0));
}

// Verifies invalid animation player inputs and stale references fail clearly.
TEST_CASE("animation player reports invalid bindings and playback inputs") {
    ofg::Scene scene;
    ofg::Entity* target = scene.create_entity(scene.get_root());
    REQUIRE(target != nullptr);
    auto* animation_player =
        static_cast<ofg::AnimationPlayer*>(scene.get_root()->create_component(ofg::ComponentType::AnimationPlayer));

    std::vector<ofg::Ptr<ofg::Entity>> missing_target;
    missing_target.emplace_back(nullptr);
    CHECK_THROWS_WITH_AS(animation_player->bind_targets(std::move(missing_target)),
        doctest::Contains("missing model node"),
        ofg::EngineError);
    bind_single_target(*animation_player, *target);

    std::unique_ptr<ofg::AnimationClip> move =
        make_constant_clip("move", ofg::AnimationTargetPath::Translation, ofg::math::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    CHECK_THROWS_WITH_AS(
        animation_player->set_clip_state(*move, -1.0f), doctest::Contains("blend weight"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(animation_player->set_clip_state(*move, 1.0f, true, -1.0f),
        doctest::Contains("playback speed"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(
        animation_player->set_clip_weight(*move, 1.0f), doctest::Contains("not active"), ofg::EngineError);
    animation_player->set_clip_state(*move, 1.0f);
    CHECK_THROWS_WITH_AS(
        animation_player->set_clip_weight(*move, -1.0f), doctest::Contains("blend weight"), ofg::EngineError);
    CHECK_THROWS_WITH_AS(animation_player->set_time_seconds(std::numeric_limits<double>::infinity()),
        doctest::Contains("finite"),
        ofg::EngineError);

    animation_player->play(*move);
    ofg::ControlInput controls;
    ofg::SceneUpdateContext invalid_context{controls, 1000.0, -0.01f, nullptr, nullptr};
    CHECK_THROWS_WITH_AS(
        animation_player->update(invalid_context), doctest::Contains("finite non-negative delta"), ofg::EngineError);

    ofg::AnimationClip bad_target{"bad target"};
    ofg::AnimationChannel bad_channel;
    bad_channel.m_target_node_index = 1;
    bad_channel.m_target_path = ofg::AnimationTargetPath::Translation;
    bad_channel.m_input_times_seconds = {0.0};
    bad_channel.m_output_values = {ofg::math::vec4(0.0f, 0.0f, 0.0f, 0.0f)};
    bad_target.add_channel(std::move(bad_channel));
    bad_target.set_duration_seconds(1.0);
    animation_player->play(bad_target);
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, nullptr, nullptr};
    CHECK_THROWS_WITH_AS(
        animation_player->update(context), doctest::Contains("outside this model instance"), ofg::EngineError);

    ofg::AnimationClip bad_rotation{"bad rotation"};
    ofg::AnimationChannel bad_rotation_channel;
    bad_rotation_channel.m_target_node_index = 0;
    bad_rotation_channel.m_target_path = ofg::AnimationTargetPath::Rotation;
    bad_rotation_channel.m_input_times_seconds = {0.0};
    bad_rotation_channel.m_output_values = {ofg::math::vec4(0.0f, 0.0f, 0.0f, 0.0f)};
    bad_rotation.add_channel(std::move(bad_rotation_channel));
    bad_rotation.set_duration_seconds(1.0);
    animation_player->play(bad_rotation);
    CHECK_THROWS_WITH_AS(animation_player->update(context), doctest::Contains("invalid quaternion"), ofg::EngineError);

    ofg::Scene destroyed_clip_scene;
    ofg::Entity* destroyed_clip_target = destroyed_clip_scene.create_entity(destroyed_clip_scene.get_root());
    REQUIRE(destroyed_clip_target != nullptr);
    ofg::AnimationPlayer destroyed_clip_player(nullptr);
    bind_single_target(destroyed_clip_player, *destroyed_clip_target);
    std::unique_ptr<ofg::AnimationClip> temporary_clip =
        make_constant_clip("temporary", ofg::AnimationTargetPath::Translation, ofg::math::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    destroyed_clip_player.play(*temporary_clip);
    temporary_clip.reset();
    CHECK_THROWS_WITH_AS(
        destroyed_clip_player.update(context), doctest::Contains("clip has been destroyed"), ofg::EngineError);

    ofg::AnimationPlayer detached_player(nullptr);
    bind_single_target(detached_player, *target);
    detached_player.play(*move);
    scene.clear();
    CHECK_THROWS_WITH_AS(detached_player.update(context), doctest::Contains("destroyed"), ofg::EngineError);
}

// Verifies Player maps movement speed to clip weights before animation sampling.
TEST_CASE("player drives idle walk sprint animation weights") {
    ofg::Scene scene;
    ofg::Entity* player_entity = scene.create_entity(scene.get_root());
    ofg::Entity* camera_entity = scene.create_entity(scene.get_root());
    ofg::Entity* model_entity = scene.create_entity(player_entity);
    ofg::Entity* target = scene.create_entity(model_entity);
    REQUIRE(player_entity != nullptr);
    REQUIRE(camera_entity != nullptr);
    REQUIRE(model_entity != nullptr);
    REQUIRE(target != nullptr);

    auto* player = static_cast<ofg::Player*>(player_entity->create_component(ofg::ComponentType::Player));
    auto* camera = static_cast<ofg::Camera*>(camera_entity->create_component(ofg::ComponentType::Camera));
    auto* animation_player =
        static_cast<ofg::AnimationPlayer*>(model_entity->create_component(ofg::ComponentType::AnimationPlayer));
    camera->set_control_mode(ofg::CameraControlMode::FirstPerson);
    bind_single_target(*animation_player, *target);

    std::unique_ptr<ofg::AnimationClip> idle =
        make_constant_clip("Idle_Loop", ofg::AnimationTargetPath::Translation, ofg::math::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::unique_ptr<ofg::AnimationClip> walk = make_constant_clip(
        "Walk_Loop", ofg::AnimationTargetPath::Translation, ofg::math::vec4(10.0f, 0.0f, 0.0f, 0.0f));
    std::unique_ptr<ofg::AnimationClip> sprint = make_constant_clip(
        "Sprint_Loop", ofg::AnimationTargetPath::Translation, ofg::math::vec4(30.0f, 0.0f, 0.0f, 0.0f));
    player->bind_locomotion_animation(*animation_player, *idle, *walk, *sprint);

    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, player, camera};
    scene.update(context);
    CHECK(player->idle_animation_weight() == doctest::Approx(1.0f));
    CHECK(player->walk_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->sprint_animation_weight() == doctest::Approx(0.0f));
    CHECK(target->local_transform().m_position.x == doctest::Approx(0.0f));

    controls.m_move_z = 1.0f;
    scene.update(context);
    CHECK(player->current_speed() == doctest::Approx(player->walk_speed()));
    CHECK(player->idle_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->walk_animation_weight() == doctest::Approx(1.0f));
    CHECK(player->sprint_animation_weight() == doctest::Approx(0.0f));
    CHECK(target->local_transform().m_position.x == doctest::Approx(10.0f));

    controls.m_fast = true;
    scene.update(context);
    CHECK(player->idle_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->walk_animation_weight() == doctest::Approx(0.0f));
    CHECK(player->sprint_animation_weight() == doctest::Approx(1.0f));
    CHECK(target->local_transform().m_position.x == doctest::Approx(30.0f));
}

// Verifies locomotion weight mapping edge cases independently of Player movement invariants.
TEST_CASE("locomotion animation weight helper covers edge speeds") {
    ofg::LocomotionAnimationWeights stopped = ofg::compute_locomotion_animation_weights(0.0f, 3.5f, 7.0f);
    CHECK(stopped.m_idle == doctest::Approx(1.0f));
    CHECK(stopped.m_walk == doctest::Approx(0.0f));
    CHECK(stopped.m_sprint == doctest::Approx(0.0f));

    ofg::LocomotionAnimationWeights no_walk = ofg::compute_locomotion_animation_weights(1.0f, 0.0f, 0.0f);
    CHECK(no_walk.m_idle == doctest::Approx(0.0f));
    CHECK(no_walk.m_walk == doctest::Approx(0.0f));
    CHECK(no_walk.m_sprint == doctest::Approx(1.0f));

    ofg::LocomotionAnimationWeights blended = ofg::compute_locomotion_animation_weights(1.75f, 3.5f, 7.0f);
    CHECK(blended.m_idle == doctest::Approx(0.5f));
    CHECK(blended.m_walk == doctest::Approx(0.5f));
    CHECK(blended.m_sprint == doctest::Approx(0.0f));

    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::compute_locomotion_animation_weights(-1.0f, 3.5f, 7.0f); }()),
        doctest::Contains("player speed"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::compute_locomotion_animation_weights(1.0f, -1.0f, 7.0f); }()),
        doctest::Contains("walk speed"),
        ofg::EngineError);
    CHECK_THROWS_WITH_AS(([&]() {
        (void)ofg::compute_locomotion_animation_weights(1.0f, 3.5f, std::numeric_limits<float>::infinity());
    }()),
        doctest::Contains("sprint speed"),
        ofg::EngineError);
}

// Verifies Player locomotion animation handles missing setup and stale references clearly.
TEST_CASE("player locomotion animation reports incomplete and stale bindings") {
    ofg::Player detached_player(nullptr);
    ofg::ControlInput controls;
    ofg::SceneUpdateContext context{controls, 1000.0, 0.0f, nullptr, nullptr};
    CHECK_NOTHROW(detached_player.update(context));

    ofg::Scene scene;
    ofg::Entity* model_entity = scene.create_entity(scene.get_root());
    REQUIRE(model_entity != nullptr);
    auto* animation_player =
        static_cast<ofg::AnimationPlayer*>(model_entity->create_component(ofg::ComponentType::AnimationPlayer));

    std::unique_ptr<ofg::AnimationClip> idle =
        make_constant_clip("idle", ofg::AnimationTargetPath::Translation, ofg::math::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    std::unique_ptr<ofg::AnimationClip> walk =
        make_constant_clip("walk", ofg::AnimationTargetPath::Translation, ofg::math::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    std::unique_ptr<ofg::AnimationClip> sprint =
        make_constant_clip("sprint", ofg::AnimationTargetPath::Translation, ofg::math::vec4(2.0f, 0.0f, 0.0f, 0.0f));
    detached_player.bind_locomotion_animation(*animation_player, *idle, *walk, *sprint);
    scene.clear();
    CHECK_THROWS_WITH_AS(
        detached_player.update(context), doctest::Contains("target has been destroyed"), ofg::EngineError);

    ofg::Scene clip_scene;
    ofg::Entity* clip_player_entity = clip_scene.create_entity(clip_scene.get_root());
    ofg::Entity* clip_model_entity = clip_scene.create_entity(clip_scene.get_root());
    auto* clip_player = static_cast<ofg::Player*>(clip_player_entity->create_component(ofg::ComponentType::Player));
    auto* clip_animation_player =
        static_cast<ofg::AnimationPlayer*>(clip_model_entity->create_component(ofg::ComponentType::AnimationPlayer));
    clip_player->bind_locomotion_animation(*clip_animation_player, *idle, *walk, *sprint);
    sprint.reset();
    CHECK_THROWS_WITH_AS(clip_player->update(context), doctest::Contains("clip has been destroyed"), ofg::EngineError);
}
