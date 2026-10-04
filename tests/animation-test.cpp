// CPU contracts for local pose interpolation, transactional playback and independent instance transport.
#include "scene/animation-pose.h"
#include "scene/animation-binding.h"
#include "scene/scene.h"
#include "core/engine-error.h"
#include <ostream>
#include <doctest.h>
#include <cmath>
#include <limits>

using namespace ofg;
namespace {
// Builds a two-second translation fixture with analytically simple positions.
AnimationTrack translation(AnimationInterpolation interpolation = AnimationInterpolation::Linear)
{
    return {0, AnimationPath::Translation, interpolation, {0, 2}, 3, {0, 0, 0, 4, 8, -2}};
}

// Owns one target and animator without a device or global application state.
struct PlaybackFixture
{
    Scene scene;
    Entity* target = scene.createEntity(scene.getRoot());
    Animator* animator = scene.createAnimator(scene.getRoot());
    // Binds supplied immutable clips while capturing a distinctive instance rest pose.
    explicit PlaybackFixture(std::vector<std::shared_ptr<Animation>> clips)
    {
        target->setLocalPosition({7, 8, 9});
        target->setLocalScale({2, 3, 4});
        animator->setBindings(std::move(clips), {target});
    }
};
} // namespace

TEST_CASE("Animation pose clamps unequal track ranges and STEP holds the left key")
{
    auto track = translation(AnimationInterpolation::Step);
    track.timesSeconds = {1, 3};
    AnimationTrack scale{0, AnimationPath::Scale, AnimationInterpolation::Linear, {2, 4}, 3, {1, 2, 3, 3, 4, 5}};
    auto clip = Animation::create({"ranges", {track, scale}});
    AnimationPose rest{{LocalTransform{}}};
    CHECK(sampleAnimationPose(*clip, rest, -100).localTransforms[0].position.x == 0);
    CHECK(sampleAnimationPose(*clip, rest, 2.99).localTransforms[0].position.x == 0);
    const auto middle = sampleAnimationPose(*clip, rest, 3).localTransforms[0];
    CHECK(middle.position.x == 4);
    CHECK(middle.scale.x == 2);
    const auto end = sampleAnimationPose(*clip, rest, 100).localTransforms[0];
    CHECK(end.position.x == 4);
    CHECK(end.scale.z == 5);
    CHECK(rest.localTransforms[0].position.x == 0);
}

TEST_CASE("Animation pose samples linear TRS and single-key channels")
{
    auto clip = Animation::create({"linear", {translation()}});
    AnimationPose rest{{LocalTransform{}}};
    auto pose = sampleAnimationPose(*clip, rest, .5);
    CHECK(pose.localTransforms[0].position.x == 1);
    CHECK(pose.localTransforms[0].position.y == 2);
    CHECK(pose.localTransforms[0].position.z == -.5f);
    for (auto mode : {AnimationInterpolation::Step, AnimationInterpolation::Linear})
    {
        auto track = translation(mode);
        track.timesSeconds = {0};
        track.values = {3, 4, 5};
        auto single = Animation::create({"single", {track}});
        CHECK(single->durationSeconds() == 0);
        CHECK(sampleAnimationPose(*single, rest, -10).localTransforms[0].position.x == 3);
        CHECK(sampleAnimationPose(*single, rest, 10).localTransforms[0].position.z == 5);
    }
}

TEST_CASE("Animation linear rotations use normalized shortest-path spherical interpolation")
{
    AnimationPose rest{{LocalTransform{}}};
    // A 120-degree rotation around Z, authored with the opposite quaternion sign.
    const float sine = std::sqrt(3.f) / 2;
    AnimationTrack
        track{0, AnimationPath::Rotation, AnimationInterpolation::Linear, {0, 2}, 4, {0, 0, 0, 1, 0, 0, -sine, -.5f}};
    auto clip = Animation::create({"rotation", {track}});
    const auto rotation = sampleAnimationPose(*clip, rest, .5).localTransforms[0].rotation;
    // One quarter of 120 degrees: sin/cos of half of 30 degrees.
    CHECK(rotation.z == doctest::Approx(std::sin(3.141592653589793 / 12)).epsilon(1e-6));
    CHECK(rotation.w == doctest::Approx(std::cos(3.141592653589793 / 12)).epsilon(1e-6));
    track.values = {0, 0, 0, 1, 0, 0, 0, -1};
    clip = Animation::create({"equivalent signs", {track}});
    CHECK(sampleAnimationPose(*clip, rest, 1).localTransforms[0].rotation.w == 1);
    track.interpolation = AnimationInterpolation::Step;
    track.values = {0, 0, 0, 2, 0, 0, 2, 0};
    clip = Animation::create({"step rotations", {track}});
    CHECK(sampleAnimationPose(*clip, rest, 1).localTransforms[0].rotation.w == 1);
    CHECK(sampleAnimationPose(*clip, rest, 2).localTransforms[0].rotation.z == 1);
}

TEST_CASE("Animation cubic Hermite scales tangents by key interval and normalizes rotations")
{
    AnimationPose rest{{LocalTransform{}}};
    AnimationTrack track{
        0,
        AnimationPath::Translation,
        AnimationInterpolation::CubicSpline,
        {1, 3},
        3,
        {0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0}
    };
    auto clip = Animation::create({"cubic", {track}});
    CHECK(sampleAnimationPose(*clip, rest, 2).localTransforms[0].position.x == 2.5f);
    CHECK(sampleAnimationPose(*clip, rest, 0).localTransforms[0].position.x == 0);
    CHECK(sampleAnimationPose(*clip, rest, 4).localTransforms[0].position.x == 4);
    track.path = AnimationPath::Scale;
    CHECK(
        sampleAnimationPose(*Animation::create({"cubic scale", {track}}), rest, 2).localTransforms[0].scale.x == 2.5f
    );
    track.path = AnimationPath::Rotation;
    track.componentsPerValue = 4;
    track.values = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
    auto rotation =
        sampleAnimationPose(*Animation::create({"cubic rotation", {track}}), rest, 2).localTransforms[0].rotation;
    CHECK(rotation.z == doctest::Approx(.75 / std::sqrt(.8125)).epsilon(1e-6));
    CHECK(rotation.w == doctest::Approx(.5 / std::sqrt(.8125)).epsilon(1e-6));
}

TEST_CASE("Animator controls clamp seek pause stop and restore rest properties across clip changes")
{
    auto clip = Animation::create({"move", {translation()}});
    AnimationTrack scale{0, AnimationPath::Scale, AnimationInterpolation::Linear, {0}, 3, {5, 6, 7}};
    PlaybackFixture f({clip, Animation::create({"scale", {scale}})});
    CHECK_FALSE(f.animator->selectedAnimation());
    CHECK_FALSE(f.animator->isPlaying());
    f.scene.updateAnimations(1);
    CHECK(f.target->localTransform().position.x == 7);
    CHECK_THROWS_AS(f.animator->play(), EngineError);
    f.animator->selectAnimation(0);
    CHECK(f.animator->durationSeconds() == 2);
    CHECK_FALSE(f.animator->isPlaying());
    CHECK(f.target->localTransform().position.x == 0);
    f.animator->play();
    f.scene.updateAnimations(.5);
    CHECK(f.animator->timeSeconds() == .5);
    CHECK(f.target->localTransform().position.x == 1);
    f.animator->pause();
    f.scene.updateAnimations(100);
    CHECK(f.animator->timeSeconds() == .5);
    f.animator->seek(100);
    CHECK(f.animator->timeSeconds() == 2);
    CHECK(f.target->localTransform().position.x == 4);
    f.animator->seek(-10);
    CHECK(f.animator->timeSeconds() == 0);
    f.animator->play();
    f.animator->selectAnimation(1);
    CHECK(f.animator->isPlaying());
    CHECK(f.target->localTransform().position.x == 7);
    CHECK(f.target->localTransform().scale.x == 5);
    f.animator->selectAnimation(0);
    CHECK(f.target->localTransform().scale.x == 2);
    f.animator->seek(.75);
    f.animator->stop();
    CHECK_FALSE(f.animator->isPlaying());
    CHECK(f.animator->timeSeconds() == 0);
    CHECK(f.target->localTransform().position.x == 0);
}

TEST_CASE("Animator wraps large deltas and non-looping playback holds and restarts its final pose")
{
    PlaybackFixture f({Animation::create({"move", {translation()}})});
    f.animator->selectAnimation(0);
    f.animator->play();
    f.scene.updateAnimations(2);
    CHECK(f.animator->timeSeconds() == 0);
    f.scene.updateAnimations(2000000000000.5);
    CHECK(f.animator->timeSeconds() == .5);
    f.animator->setPlaybackSpeed(2);
    f.scene.updateAnimations(.25);
    CHECK(f.animator->timeSeconds() == 1);
    f.animator->setPlaybackSpeed(0);
    f.scene.updateAnimations(100);
    CHECK(f.animator->timeSeconds() == 1);
    f.animator->setPlaybackSpeed(std::numeric_limits<float>::max());
    f.scene.updateAnimations(std::numeric_limits<double>::max());
    CHECK(std::isfinite(f.animator->timeSeconds()));
    CHECK(f.animator->timeSeconds() >= 0);
    CHECK(f.animator->timeSeconds() < 2);
    f.animator->setPlaybackSpeed(1);
    f.animator->setLooping(false);
    f.scene.updateAnimations(100);
    CHECK(f.animator->timeSeconds() == 2);
    CHECK_FALSE(f.animator->isPlaying());
    CHECK(f.target->localTransform().position.x == 4);
    f.scene.updateAnimations(100);
    CHECK(f.animator->timeSeconds() == 2);
    f.animator->play();
    CHECK(f.animator->timeSeconds() == 0);
    CHECK(f.target->localTransform().position.x == 0);
}

TEST_CASE("Animator zero-duration clips stay at zero and instances have independent playback")
{
    auto track = translation();
    track.timesSeconds = {0};
    track.values = {3, 4, 5};
    auto single = Animation::create({"single", {track}});
    PlaybackFixture first({single}), second({single});
    first.animator->selectAnimation(0);
    first.animator->play();
    first.scene.updateAnimations(100000);
    CHECK(first.animator->timeSeconds() == 0);
    CHECK(first.target->localTransform().position.x == 3);
    CHECK(second.target->localTransform().position.x == 7);
    CHECK_FALSE(second.animator->selectedAnimation());
    first.animator->setLooping(false);
    first.scene.updateAnimations(1);
    CHECK_FALSE(first.animator->isPlaying());
    auto move = Animation::create({"move", {translation()}});
    first.animator->setBindings({move}, {first.target});
    second.animator->setBindings({move}, {second.target});
    first.animator->selectAnimation(0);
    second.animator->selectAnimation(0);
    first.animator->play();
    second.animator->play();
    first.scene.updateAnimations(.25);
    second.scene.updateAnimations(1);
    CHECK(first.target->localTransform().position.x == .5f);
    CHECK(second.target->localTransform().position.x == 2);
    CHECK(first.animator->animations()[0] == second.animator->animations()[0]);
    first.animator->setPlaybackSpeed(3);
    first.animator->setLooping(false);
    first.animator->setBindings({move}, {first.target});
    CHECK_FALSE(first.animator->selectedAnimation());
    CHECK_FALSE(first.animator->isPlaying());
    CHECK(first.animator->isLooping());
    CHECK(first.animator->playbackSpeed() == 1);
    CHECK(first.animator->timeSeconds() == 0);
}

TEST_CASE("Animation rejects unsupported weights invalid times and targets without publishing a partial pose")
{
    auto clip = Animation::create({"move", {translation()}});
    PlaybackFixture f({clip});
    f.animator->selectAnimation(0);
    f.animator->seek(1);
    f.animator->play();
    CHECK_THROWS_AS(f.animator->selectAnimation(99), EngineError);
    CHECK_THROWS_AS(f.animator->seek(std::numeric_limits<double>::infinity()), EngineError);
    CHECK_THROWS_AS(f.animator->setPlaybackSpeed(-1), EngineError);
    CHECK_THROWS_AS(f.animator->setPlaybackSpeed(std::numeric_limits<float>::quiet_NaN()), EngineError);
    CHECK_THROWS_AS(f.scene.updateAnimations(-1), EngineError);
    CHECK_THROWS_AS(f.scene.updateAnimations(std::numeric_limits<double>::infinity()), EngineError);
    CHECK(f.animator->timeSeconds() == 1);
    CHECK(f.animator->isPlaying());
    CHECK(f.target->localTransform().position.x == 2);
    CHECK_THROWS_AS(sampleAnimationPose(*clip, {}, 0), EngineError);
    AnimationTrack weights{1, AnimationPath::Weights, AnimationInterpolation::Linear, {0}, 1, {1}};
    f.animator->setBindings({clip, Animation::create({"weights", {weights}})}, {f.target, nullptr});
    f.animator->selectAnimation(0);
    f.animator->seek(1);
    CHECK_THROWS_WITH_AS(
        f.animator->selectAnimation(1),
        "Animation 'weights' node 1: Morph weight playback is not supported.",
        EngineError
    );
    CHECK(f.animator->selectedAnimation() == 0);
    CHECK(f.animator->timeSeconds() == 1);
    CHECK(f.target->localTransform().position.x == 2);
    AnimationPose invalid{{LocalTransform{}, LocalTransform{}}};
    invalid.localTransforms[1].rotation = {0, 0, 0, 0};
    auto other = f.scene.createEntity(f.scene.getRoot());
    std::vector<Ptr<Entity>> nodes{f.target, other};
    CHECK_THROWS_AS(applyAnimationPose(invalid, nodes), EngineError);
    CHECK(f.target->localTransform().position.x == 2);
}

TEST_CASE("Animation cubic zero rotations fail transactionally rather than producing NaNs")
{
    AnimationTrack track{
        0,
        AnimationPath::Rotation,
        AnimationInterpolation::CubicSpline,
        {0, 2},
        4,
        {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0}
    };
    PlaybackFixture f({Animation::create({"degenerate cubic", {track}})});
    f.animator->selectAnimation(0);
    f.animator->play();
    CHECK_THROWS_AS(f.animator->seek(1), EngineError);
    CHECK_THROWS_AS(f.scene.updateAnimations(1), EngineError);
    CHECK(f.animator->timeSeconds() == 0);
    CHECK(f.target->localTransform().rotation.w == 1);
    CHECK(f.animator->isPlaying());
}

TEST_CASE("direct animation mapping validates names targets and corresponding parents")
{
    Scene scene;
    ModelInstance target;
    target.root = scene.createEntity(scene.getRoot());
    auto parent = scene.createEntity(target.root.get());
    parent->setName("Root");
    auto child = scene.createEntity(parent);
    child->setName("Joint");
    target.nodes = {child, parent}; // Domain order is deliberately different.
    ModelData source;
    source.nodes.resize(2);
    source.nodes[0].name = "Root";
    source.nodes[0].children = {1};
    source.nodes[1].name = "Joint";
    auto track = translation();
    track.node = 1;
    source.animations = {Animation::create({"test", {track}})};
    auto mapping = mapAnimationNodesByName(source, target);
    CHECK(mapping[0] == parent);
    CHECK(mapping[1] == child);
    SUBCASE("missing name")
    {
        child->setName("missing");
    }
    SUBCASE("duplicate target")
    {
        child->setName("Root");
    }
    SUBCASE("duplicate source")
    {
        source.nodes[1].name = "Root";
    }
    SUBCASE("mismatched hierarchy")
    {
        source.nodes[0].children.clear();
    }
    SUBCASE("invalid target")
    {
        track.node = 2;
        source.animations = {Animation::create({"invalid", {track}})};
    }
    CHECK_THROWS_AS(mapAnimationNodesByName(source, target), EngineError);
}
