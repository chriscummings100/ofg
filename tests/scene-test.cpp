// Scene hierarchy, lifetime and transform contracts adapted from the backup.
#include "doctest.h"
#include "core/engine-error.h"
#include "core/ptr.h"
#include "scene/scene.h"
#include "scene/component.h"
#include "math/transform.h"
#include <string>
#include <memory>
#include <utility>

namespace {
// Exercises the component base without importing a gameplay component.
class TestComponent : public ofg::Component
{
public:
    // Observes the supplied entity using the inherited lifetime contract.
    explicit TestComponent(ofg::Entity* entity)
        : Component(entity)
    {
    }
};

// Returns a Y-axis quaternion or fails the current doctest.
ofg::math::Quat requireYRotation(float radians)
{
    std::string error;
    std::optional<ofg::math::Quat> rotation =
        ofg::math::quatFromAxisAngle(ofg::math::vec3(0.0f, 1.0f, 0.0f), radians, error);
    REQUIRE_MESSAGE(rotation.has_value(), error);
    return *rotation;
}

} // namespace

TEST_CASE("scene creates a root entity and resolves ids")
{
    ofg::Scene scene;

    ofg::Entity* root = scene.getRoot();
    REQUIRE(root != nullptr);
    CHECK(root->id() == 0);
    CHECK(root->parent() == nullptr);
    CHECK(scene.entityCount() == 1);
    CHECK(scene.getEntity(0) == root);
    CHECK(scene.getEntity(12) == nullptr);
    const ofg::Scene& constScene = scene;
    CHECK(constScene.getRoot() == root);
    CHECK(constScene.getEntity(12) == nullptr);
}

TEST_CASE("scene links entities into a child sibling tree")
{
    ofg::Scene scene;
    ofg::Entity* root = scene.getRoot();
    ofg::Entity* first = scene.createEntity(root);
    ofg::Entity* second = scene.createEntity(root);
    ofg::Entity* grandchild = scene.createEntity(first);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(grandchild != nullptr);
    CHECK(first->id() == 1);
    CHECK(second->id() == 2);
    CHECK(grandchild->id() == 3);
    CHECK(root->firstChild() == first);
    CHECK(first->nextSibling() == second);
    CHECK(second->nextSibling() == nullptr);
    CHECK(first->firstChild() == grandchild);
    CHECK(grandchild->parent() == first);
}

TEST_CASE("scene rejects invalid entity parents")
{
    ofg::Scene scene;
    ofg::Scene otherScene;

    CHECK_THROWS_WITH_AS(
        (
            [&]()
            {
                (void)scene.createEntity(nullptr);
            }()
        ),
        doctest::Contains("parent"),
        ofg::EngineError
    );
    CHECK_THROWS_WITH_AS(
        (
            [&]()
            {
                (void)scene.createEntity(otherScene.getRoot());
            }()
        ),
        doctest::Contains("same scene"),
        ofg::EngineError
    );
}

TEST_CASE("scene supports const traversal after move")
{
    ofg::Scene source;
    ofg::Entity* child = source.createEntity(source.getRoot());
    ofg::Entity* grandchild = source.createEntity(child);

    ofg::Scene moved(std::move(source));
    const ofg::Scene& constMoved = moved;
    const ofg::Entity* constRoot = constMoved.getRoot();
    REQUIRE(constRoot != nullptr);
    const ofg::Entity* constChild = constRoot->firstChild();
    REQUIRE(constChild != nullptr);
    CHECK(constChild->nextSibling() == nullptr);
    const ofg::Entity* constGrandchild = constChild->firstChild();
    REQUIRE(constGrandchild != nullptr);
    CHECK(constGrandchild == grandchild);
    CHECK(constGrandchild->parent() == child);
    CHECK(constMoved.getEntity(grandchild->id()) == grandchild);
    CHECK(source.getRoot() == nullptr);
    CHECK(moved.createEntity(child)->parent() == child);

    ofg::Scene assigned;
    assigned = std::move(moved);
    ofg::Scene* self = &assigned;
    assigned = std::move(*self);
    REQUIRE(assigned.getRoot() != nullptr);
    ofg::Entity* assignedChild = assigned.createEntity(assigned.getRoot());
    REQUIRE(assignedChild != nullptr);
}

TEST_CASE("scene clear resets ids and invalidates entity observers")
{
    ofg::Scene scene;
    const std::uint32_t firstGeneration = scene.generation();
    ofg::Entity* entity = scene.createEntity(scene.getRoot());
    ofg::Ptr<ofg::Entity> observer{entity};
    ofg::Ptr<ofg::Entity> rootObserver{scene.getRoot()};

    scene.clear();

    CHECK(scene.generation() == firstGeneration + 1);
    REQUIRE(scene.getRoot() != nullptr);
    CHECK(scene.getRoot()->id() == 0);
    CHECK(scene.entityCount() == 1);
    CHECK(observer == nullptr);
    CHECK(rootObserver == nullptr);
    CHECK(scene.getRoot()->firstChild() == nullptr);
    ofg::Entity* next = scene.createEntity(scene.getRoot());
    CHECK(next->id() == 1);
}

// Moving the owner preserves incoming observations and releases displaced entities.
TEST_CASE("scene move transfers ownership and invalidates displaced observers")
{
    ofg::Scene source;
    ofg::Entity* child = source.createEntity(source.getRoot());
    ofg::Ptr<ofg::Entity> childObserver{child};
    ofg::Scene destination;
    ofg::Ptr<ofg::Entity> oldRoot{destination.getRoot()};

    destination = std::move(source);

    CHECK(oldRoot == nullptr);
    CHECK(childObserver.get() == child);
    CHECK(destination.createEntity(child)->parent() == child);
    CHECK(source.getRoot() == nullptr);
    source.clear();
    REQUIRE(source.getRoot() != nullptr);
    CHECK(source.entityCount() == 1);
    CHECK_THROWS_AS((void)source.createEntity(child), ofg::EngineError);

    destination.clear();
    CHECK(childObserver == nullptr);
}

// Growing scene storage must not move allocations or break tree links.
TEST_CASE("scene entity addresses survive storage growth")
{
    ofg::Scene scene;
    ofg::Entity* root = scene.getRoot();
    ofg::Entity* first = scene.createEntity(root);
    ofg::Ptr<ofg::Entity> observer{first};
    for (int index = 0; index < 256; ++index)
    {
        (void)scene.createEntity(root);
    }

    CHECK(scene.getRoot() == root);
    CHECK(scene.getEntity(first->id()) == first);
    CHECK(observer.get() == first);
    std::size_t count = 0;
    for (const ofg::Entity* child = root->firstChild(); child; child = child->nextSibling())
    {
        CHECK(child->parent() == root);
        CHECK(child->id() == ++count);
    }
    CHECK(count == 257);
}

// Component bindings observe entities and can safely outlive their destruction.
TEST_CASE("component and entity observers null independently on destruction")
{
    std::unique_ptr<TestComponent> component;
    ofg::Ptr<ofg::Entity> entityObserver;
    ofg::Ptr<TestComponent> componentObserver;
    {
        ofg::Scene scene;
        ofg::Entity* entity = scene.createEntity(scene.getRoot());
        entityObserver = entity;
        component = std::make_unique<TestComponent>(entity);
        componentObserver = component.get();
        const TestComponent& constComponent = *component;
        CHECK(component->entity() == entity);
        CHECK(constComponent.entity() == entity);
    }

    CHECK(entityObserver == nullptr);
    CHECK(component->entity() == nullptr);
    CHECK(componentObserver.get() == component.get());
    component.reset();
    CHECK(componentObserver == nullptr);
}

TEST_CASE("scene transforms compose from local to world")
{
    ofg::Scene scene;
    ofg::Entity* root = scene.getRoot();
    ofg::Entity* child = scene.createEntity(root);
    ofg::Entity* grandchild = scene.createEntity(child);

    root->setLocalPosition(ofg::math::vec3(10.0f, 0.0f, 0.0f));
    child->setLocalPosition(ofg::math::vec3(0.0f, 0.0f, 2.0f));
    child->setLocalRotation(requireYRotation(1.57079632679f));
    grandchild->setLocalPosition(ofg::math::vec3(1.0f, 0.0f, 0.0f));
    grandchild->setLocalScale(ofg::math::vec3(2.0f, 2.0f, 2.0f));

    const ofg::math::Mat4 worldFromGrandchild = ofg::worldFromLocal(*grandchild);
    const ofg::math::Vec4 origin = ofg::math::mul(worldFromGrandchild, ofg::math::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    CHECK(origin.x == doctest::Approx(10.0f));
    CHECK(origin.y == doctest::Approx(0.0f));
    CHECK(origin.z == doctest::Approx(1.0f));

    const ofg::math::Vec4 localX = ofg::math::mul(worldFromGrandchild, ofg::math::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(localX.x == doctest::Approx(10.0f));
    CHECK(localX.z == doctest::Approx(-1.0f));
}
