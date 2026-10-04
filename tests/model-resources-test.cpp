// GPU-independent deformation resources and per-instance bindings, including failure atomicity and lifetime.
#include "resources/animation.h"
#include "resources/skin.h"
#include "scene/scene.h"
#include "core/engine-error.h"
#include "math/transform.h"
#include <ostream>
#include <doctest.h>
#include <limits>

using namespace ofg;

namespace {
// Makes a two-key translation track whose target domain is explicitly unrelated to runtime EntityIds.
AnimationTrack translationTrack(uint32_t node = 2)
{
    AnimationTrack track;
    track.node = node;
    track.timesSeconds = {0.25f, 2.0f};
    track.values = {0, 0, 0, 1, 2, 3};
    return track;
}

// Creates a two-joint palette with noncontiguous source node indices and a non-joint skeleton root.
std::shared_ptr<Skin> fixtureSkin()
{
    return Skin::create({"arm", {{4, math::mat4Identity()}, {9, math::mat4Translation({0, -2, 0})}}, 2});
}

// Creates a mesh with two influence sets and two independently weighted morph targets.
MeshDeformationData fixtureDeformation()
{
    MeshDeformationData data;
    data.influenceSets.resize(2);
    data.influenceSets[0].assign(3, {{0, 1, 0, 0}, {0.25f, 0.25f, 0, 0}});
    data.influenceSets[1].assign(3, {{1, 0, 0, 0}, {0.5f, 0, 0, 0}});
    data.morphTargets.resize(2);
    data.morphTargets[0].positions = {{0, 1, 0}, {0, 2, 0}, {0, 3, 0}};
    data.morphTargets[1].normals = {{0.1f, 0, 0}, {0, 0.1f, 0}, {0, 0, 0.1f}};
    data.defaultMorphWeights = {0.25f, -0.5f};
    return data;
}

// Creates real CPU resources without RHI, shader compilation or a mock graphics device.
std::shared_ptr<Mesh> fixtureMesh(MeshDeformationData deformation = fixtureDeformation())
{
    auto material = Material::create(Shader::create("model-resource-test", "source"));
    return Mesh::create(
        {{{-1, 0, 0}, {}, {}}, {{1, 0, 0}, {}, {}}, {{0, 1, 0}, {}, {}}},
        {0, 1, 2},
        {{0, 3, material}},
        std::move(deformation)
    );
}

// Creates one two-target morph track with weights outside [0,1], which are valid extrapolation values.
std::shared_ptr<Animation> morphAnimation(uint32_t node)
{
    AnimationTrack track;
    track.node = node;
    track.path = AnimationPath::Weights;
    track.componentsPerValue = 2;
    track.timesSeconds = {0, 1};
    track.values = {-1, 2, 0, 0};
    return Animation::create({"morph", {track}});
}
} // namespace

TEST_CASE("Skin resources preserve palette order and own their immutable source data")
{
    auto skin = fixtureSkin();
    REQUIRE(skin->isLoaded());
    CHECK(skin->key().empty());
    CHECK(skin->desc().name == "arm");
    REQUIRE(skin->desc().joints.size() == 2);
    CHECK(skin->desc().joints[0].node == 4);
    CHECK(skin->desc().joints[1].node == 9);
    CHECK(skin->desc().joints[1].inverseBindMatrix[3].y == -2);
    CHECK(skin->desc().skeletonRoot == 2);
    CHECK(fixtureSkin() != skin);

    SkinDesc desc{"copied", {{7}}, {}};
    auto copy = Skin::create(desc);
    desc.joints[0].node = 8;
    CHECK(copy->desc().joints[0].node == 7);
    CHECK(math::packMat4(copy->desc().joints[0].inverseBindMatrix) == math::packMat4(math::mat4Identity()));
}

TEST_CASE("Skin rejects empty duplicate nonfinite and nonaffine definitions")
{
    CHECK_THROWS_AS(Skin::create({}), EngineError);
    CHECK_THROWS_AS(Skin::create({"duplicate", {{2}, {2}}, {}}), EngineError);
    SkinDesc desc{"invalid", {{0}}, {}};
    SUBCASE("nonfinite translation")
    {
        desc.joints[0].inverseBindMatrix[3].x = INFINITY;
    }
    SUBCASE("projective row")
    {
        desc.joints[0].inverseBindMatrix[0].w = 0.01f;
    }
    SUBCASE("invalid homogeneous coordinate")
    {
        desc.joints[0].inverseBindMatrix[3].w = 0;
    }
    CHECK_THROWS_AS(Skin::create(desc), EngineError);
}

TEST_CASE("Animation resources retain key timing values and independent channel durations")
{
    auto translation = translationTrack();
    auto scale = translationTrack();
    scale.path = AnimationPath::Scale;
    scale.interpolation = AnimationInterpolation::Step;
    scale.timesSeconds = {1, 3};
    AnimationDesc desc{"walk", {translation, scale}};
    auto animation = Animation::create(desc);
    CHECK(animation->isLoaded());
    CHECK(animation->key().empty());
    CHECK(animation->desc().name == "walk");
    CHECK(animation->durationSeconds() == 3);
    REQUIRE(animation->desc().tracks.size() == 2);
    CHECK(animation->desc().tracks[0].timesSeconds[0] == 0.25f);
    CHECK(animation->desc().tracks[0].node == 2);
    CHECK(animation->desc().tracks[1].interpolation == AnimationInterpolation::Step);
    desc.tracks[0].values[0] = 77;
    CHECK(animation->desc().tracks[0].values[0] == 0);

    auto single = translationTrack();
    single.timesSeconds = {0};
    single.values = {1, 2, 3};
    CHECK(Animation::create({"single", {single}})->durationSeconds() == 0);
}

TEST_CASE("Animation preserves cubic quaternion signs and unnormalized derivatives")
{
    AnimationTrack track;
    track.node = 7;
    track.path = AnimationPath::Rotation;
    track.interpolation = AnimationInterpolation::CubicSpline;
    track.componentsPerValue = 4;
    track.timesSeconds = {0, 1};
    track.values = {0, 0, 0, 0, 0, 0, 0, -1, 2, 0, 0, 0, -3, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    auto animation = Animation::create({"cubic", {track}});
    CHECK(animation->desc().tracks[0].values == track.values);
    CHECK(animation->desc().tracks[0].values[7] == -1);
    CHECK(animation->desc().tracks[0].values[8] == 2);
    CHECK(animation->desc().tracks[0].values[12] == -3);

    track.values[7] = 0;
    CHECK_THROWS_AS(Animation::create({"zero rotation", {track}}), EngineError);
}

TEST_CASE("Animation rejects malformed times properties interpolation and value layouts")
{
    CHECK_THROWS_AS(Animation::create({}), EngineError);
    CHECK_THROWS_AS(Animation::create({"duplicate", {translationTrack(), translationTrack()}}), EngineError);
    auto track = translationTrack();
    SUBCASE("duplicate key time")
    {
        track.timesSeconds = {1, 1};
    }
    SUBCASE("decreasing key times")
    {
        track.timesSeconds = {2, 1};
    }
    SUBCASE("negative time")
    {
        track.timesSeconds[0] = -0.1f;
    }
    SUBCASE("nonfinite time")
    {
        track.timesSeconds[1] = INFINITY;
    }
    SUBCASE("no keys")
    {
        track.timesSeconds.clear();
    }
    SUBCASE("nonfinite output")
    {
        track.values[0] = std::numeric_limits<float>::quiet_NaN();
    }
    SUBCASE("output truncated")
    {
        track.values.pop_back();
    }
    SUBCASE("extra output")
    {
        track.values.push_back(7);
    }
    SUBCASE("wrong width")
    {
        track.componentsPerValue = 4;
    }
    SUBCASE("huge width")
    {
        track.path = AnimationPath::Weights;
        track.componentsPerValue = UINT32_MAX;
    }
    SUBCASE("zero weight width")
    {
        track.path = AnimationPath::Weights;
        track.componentsPerValue = 0;
    }
    SUBCASE("invalid path")
    {
        track.path = static_cast<AnimationPath>(-1);
    }
    SUBCASE("invalid interpolation")
    {
        track.interpolation = static_cast<AnimationInterpolation>(-1);
    }
    SUBCASE("cubic lacks tangents")
    {
        track.interpolation = AnimationInterpolation::CubicSpline;
    }
    SUBCASE("one cubic key")
    {
        track.interpolation = AnimationInterpolation::CubicSpline;
        track.timesSeconds = {0};
        track.values.resize(9);
    }
    CHECK_THROWS_AS(Animation::create({"invalid", {track}}), EngineError);
}

TEST_CASE("Mesh owns all influences morph deltas and defaults without changing base geometry")
{
    auto deformation = fixtureDeformation();
    auto mesh = fixtureMesh(deformation);
    REQUIRE(mesh->deformation().influenceSets.size() == 2);
    CHECK(mesh->deformation().influenceSets[1][2].weights.x == 0.5f);
    REQUIRE(mesh->deformation().morphTargets.size() == 2);
    CHECK(mesh->deformation().morphTargets[0].positions[2].y == 3);
    REQUIRE(mesh->deformation().morphTargets[0].normals.size() == 3);
    CHECK(mesh->deformation().morphTargets[0].normals[0].x == 0);
    CHECK(mesh->deformation().morphTargets[1].normals[0].x == 0.1f);
    CHECK(mesh->deformation().defaultMorphWeights[1] == -0.5f);
    CHECK(mesh->bounds().maximum.y == 1);
    CHECK(mesh->vertices()[2].position.y == 1);
    deformation.morphTargets[0].positions[2].y = 99;
    CHECK(mesh->deformation().morphTargets[0].positions[2].y == 3);
    deformation.defaultMorphWeights.clear();
    auto defaults = fixtureMesh(deformation);
    CHECK(defaults->deformation().defaultMorphWeights == std::vector<float>{0, 0});
    CHECK(fixtureMesh({})->deformation().morphTargets.empty());
}

TEST_CASE("Mesh rejects malformed deformation data before publishing a resource")
{
    auto deformation = fixtureDeformation();
    SUBCASE("influence count")
    {
        deformation.influenceSets[0].pop_back();
    }
    SUBCASE("negative joint weight")
    {
        deformation.influenceSets[0][0].weights.x = -1;
    }
    SUBCASE("joint weight above one")
    {
        deformation.influenceSets[0][0].weights.x = 2;
    }
    SUBCASE("nonfinite joint weight")
    {
        deformation.influenceSets[0][0].weights.x = INFINITY;
    }
    SUBCASE("morph count")
    {
        deformation.morphTargets[0].positions.pop_back();
    }
    SUBCASE("nonfinite position delta")
    {
        deformation.morphTargets[0].positions[0].x = INFINITY;
    }
    SUBCASE("nonfinite normal delta")
    {
        deformation.morphTargets[1].normals[0].y = INFINITY;
    }
    SUBCASE("nonfinite tangent delta")
    {
        deformation.morphTargets[0].tangents = {{INFINITY, 0, 0}, {}, {}};
    }
    SUBCASE("default count")
    {
        deformation.defaultMorphWeights.pop_back();
    }
    SUBCASE("nonfinite default")
    {
        deformation.defaultMorphWeights[0] = INFINITY;
    }
    CHECK_THROWS_AS(fixtureMesh(deformation), EngineError);
}

TEST_CASE("Shared Mesh and Skin have independent renderer joint palettes weights and material overrides")
{
    Scene scene;
    auto firstRoot = scene.createEntity(scene.getRoot());
    auto secondRoot = scene.createEntity(scene.getRoot());
    auto first = scene.createMeshRenderer(firstRoot);
    auto second = scene.createMeshRenderer(secondRoot);
    auto firstJoint = scene.createEntity(firstRoot);
    auto secondJoint = scene.createEntity(secondRoot);
    auto mesh = fixtureMesh();
    auto skin = fixtureSkin();
    first->setMesh(mesh);
    second->setMesh(mesh);
    first->setSkin(skin, {firstRoot, firstJoint});
    second->setSkin(skin, {secondRoot, secondJoint});
    first->setMorphWeights({1.5f, -2});
    auto uniqueMaterial = first->makeMaterialUnique(0);
    CHECK(first->skin() == second->skin());
    CHECK(first->joints()[0] == firstRoot);
    CHECK(second->joints()[0] == secondRoot);
    CHECK(second->morphWeights()[0] == 0.25f);
    CHECK(mesh->deformation().defaultMorphWeights[0] == 0.25f);
    CHECK(first->material(0) == uniqueMaterial);
    CHECK(second->material(0) != uniqueMaterial);

    first->setMesh(mesh);
    CHECK(first->skin() == skin);
    CHECK(first->morphWeights()[0] == 1.5f);
    first->setSkin(nullptr, {});
    CHECK(first->skin() == nullptr);
    CHECK(first->joints().empty());
    CHECK(first->morphWeights()[0] == 1.5f);
    first->setSkin(skin, {firstRoot, firstJoint});
    first->setMesh(fixtureMesh());
    CHECK(first->skin() == nullptr);
    CHECK(first->joints().empty());
    CHECK(first->morphWeights()[0] == 0.25f);
    CHECK(first->materialOverride(0) == nullptr);
    first->setMesh(nullptr);
    CHECK(first->morphWeights().empty());
}

TEST_CASE("Renderer rejects invalid skin and weight assignments without losing valid bindings")
{
    Scene scene, other;
    auto renderer = scene.createMeshRenderer(scene.getRoot());
    auto joint = scene.createEntity(scene.getRoot());
    auto skin = fixtureSkin();
    renderer->setMesh(fixtureMesh());
    renderer->setSkin(skin, {scene.getRoot(), joint});
    CHECK_THROWS_AS(renderer->setSkin(skin, {joint}), EngineError);
    CHECK_THROWS_AS(renderer->setSkin(skin, {joint, joint}), EngineError);
    CHECK_THROWS_AS(renderer->setSkin(skin, {nullptr, joint}), EngineError);
    CHECK_THROWS_AS(renderer->setSkin(skin, {other.getRoot(), joint}), EngineError);
    Ptr<Entity> expired = other.getRoot();
    other.clear();
    CHECK_THROWS_AS(renderer->setSkin(skin, {expired, joint}), EngineError);
    CHECK_THROWS_AS(renderer->setSkin(nullptr, {joint}), EngineError);
    CHECK_THROWS_AS(renderer->setMorphWeights({1}), EngineError);
    CHECK_THROWS_AS(renderer->setMorphWeights({1, INFINITY}), EngineError);
    CHECK(renderer->skin() == skin);
    REQUIRE(renderer->joints().size() == 2);
    CHECK(renderer->joints()[1] == joint);
    CHECK(renderer->morphWeights()[0] == 0.25f);

    renderer->setMesh(fixtureMesh({}));
    CHECK_THROWS_AS(renderer->setSkin(skin, {scene.getRoot(), joint}), EngineError);
    auto invalid = fixtureDeformation();
    invalid.influenceSets[1][0].joints[0] = 2;
    renderer->setMesh(fixtureMesh(invalid));
    CHECK_THROWS_AS(renderer->setSkin(skin, {scene.getRoot(), joint}), EngineError);
    invalid = fixtureDeformation();
    invalid.influenceSets[0][0].weights = {};
    invalid.influenceSets[1][0].weights = {};
    renderer->setMesh(fixtureMesh(invalid));
    CHECK_THROWS_AS(renderer->setSkin(skin, {scene.getRoot(), joint}), EngineError);
    renderer->setMesh(nullptr);
    CHECK_THROWS_AS(renderer->setMorphWeights({}), EngineError);
    CHECK_THROWS_AS(renderer->setSkin(skin, {scene.getRoot(), joint}), EngineError);
}

TEST_CASE("Animators share clips but observe distinct source-indexed targets and remain passive")
{
    Scene scene;
    auto firstRoot = scene.createEntity(scene.getRoot());
    auto secondRoot = scene.createEntity(scene.getRoot());
    auto firstTarget = scene.createEntity(firstRoot);
    auto secondTarget = scene.createEntity(secondRoot);
    auto clip = Animation::create({"shared", {translationTrack(2)}});
    auto first = scene.createAnimator(firstRoot);
    auto second = scene.createAnimator(secondRoot);
    first->setBindings({clip}, {nullptr, nullptr, firstTarget});
    second->setBindings({clip}, {nullptr, nullptr, secondTarget});
    CHECK(firstRoot->animator() == first);
    CHECK(first->animations()[0] == second->animations()[0]);
    CHECK(first->nodes()[2] == firstTarget);
    CHECK(second->nodes()[2] == secondTarget);
    firstTarget->setLocalPosition({7, 8, 9});
    scene.update();
    CHECK(firstTarget->localTransform().position.x == 7);
    CHECK(secondTarget->localTransform().position.x == 0);
    CHECK_THROWS_AS(scene.createAnimator(firstRoot), EngineError);
    CHECK_THROWS_AS(scene.createAnimator(nullptr), EngineError);
    Scene other;
    CHECK_THROWS_AS(other.createAnimator(firstRoot), EngineError);
    CHECK(scene.animators().size() == 2);

    first->setBindings({}, {});
    CHECK(first->animations().empty());
    CHECK(first->nodes().empty());
}

TEST_CASE("Animator binding validates subtree membership source ranges and morph target widths atomically")
{
    Scene scene, other;
    auto root = scene.createEntity(scene.getRoot());
    auto target = scene.createEntity(root);
    auto renderer = scene.createMeshRenderer(target);
    renderer->setMesh(fixtureMesh());
    auto animator = scene.createAnimator(root);
    auto clip = morphAnimation(1);
    animator->setBindings({clip}, {nullptr, target});
    CHECK_THROWS_AS(animator->setBindings({nullptr}, {nullptr, target}), EngineError);
    CHECK_THROWS_AS(animator->setBindings({clip}, {target}), EngineError);
    CHECK_THROWS_AS(animator->setBindings({clip}, {nullptr, other.getRoot()}), EngineError);
    CHECK_THROWS_AS(animator->setBindings({clip}, {nullptr, scene.getRoot()}), EngineError);
    CHECK_THROWS_AS(animator->setBindings({clip}, {target, target}), EngineError);
    CHECK_THROWS_AS(animator->setBindings({clip}, {nullptr, root}), EngineError);
    CHECK(animator->animations()[0] == clip);
    CHECK(animator->nodes()[1] == target);
    scene.update();
    CHECK(renderer->morphWeights()[0] == 0.25f);

    auto wrongWidth = clip->desc();
    wrongWidth.tracks[0].componentsPerValue = 1;
    wrongWidth.tracks[0].values = {0, 1};
    CHECK_THROWS_AS(animator->setBindings({Animation::create(wrongWidth)}, {nullptr, target}), EngineError);
    // An unselected source node is intentionally not required to have a live target renderer.
    animator->setBindings({clip}, {nullptr, nullptr});
    CHECK(animator->nodes()[1] == nullptr);
}

TEST_CASE("Scene growth moves clear and destruction preserve or invalidate all new component observers")
{
    std::weak_ptr<Skin> weakSkin;
    std::weak_ptr<Animation> weakAnimation;
    std::weak_ptr<Mesh> weakMesh;
    Ptr<Animator> animatorObserver;
    Ptr<MeshRenderer> rendererObserver;
    Ptr<Entity> jointObserver;
    {
        Scene scene;
        auto joint = scene.createEntity(scene.getRoot());
        auto renderer = scene.createMeshRenderer(scene.getRoot());
        auto animator = scene.createAnimator(scene.getRoot());
        auto mesh = fixtureMesh();
        auto skin = fixtureSkin();
        auto clip = Animation::create({"lifetime", {translationTrack(1)}});
        weakMesh = mesh;
        weakSkin = skin;
        weakAnimation = clip;
        renderer->setMesh(mesh);
        renderer->setSkin(skin, {scene.getRoot(), joint});
        animator->setBindings({clip}, {scene.getRoot(), joint});
        mesh.reset();
        skin.reset();
        clip.reset();
        CHECK_FALSE(weakMesh.expired());
        CHECK_FALSE(weakSkin.expired());
        CHECK_FALSE(weakAnimation.expired());
        animatorObserver = animator;
        rendererObserver = renderer;
        jointObserver = joint;
        for (int i = 0; i < 200; ++i)
        {
            auto entity = scene.createEntity(scene.getRoot());
            scene.createAnimator(entity);
        }
        Scene moved(std::move(scene));
        CHECK(animatorObserver->entity() == moved.getRoot());
        CHECK(animatorObserver->nodes()[1] == joint);
        CHECK(&joint->scene() == &moved);
        CHECK(rendererObserver->joints()[1] == joint);
        // Membership validation must use the new owning Scene after a move.
        renderer->setSkin(renderer->skin(), {moved.getRoot(), joint});
        animator->setBindings({animator->animations()[0]}, {moved.getRoot(), joint});

        Scene destination;
        Ptr<Animator> displaced = destination.createAnimator(destination.getRoot());
        destination = std::move(moved);
        CHECK(displaced == nullptr);
        CHECK(&joint->scene() == &destination);
        CHECK(animatorObserver->nodes()[1] == joint);
        SUBCASE("clear")
        {
            destination.clear();
            CHECK(animatorObserver == nullptr);
            CHECK(rendererObserver == nullptr);
            CHECK(jointObserver == nullptr);
            CHECK(destination.animators().empty());
        }
        SUBCASE("destruction") {}
    }
    CHECK(animatorObserver == nullptr);
    CHECK(rendererObserver == nullptr);
    CHECK(jointObserver == nullptr);
    CHECK(weakSkin.expired());
    CHECK(weakAnimation.expired());
    CHECK(weakMesh.expired());
}
