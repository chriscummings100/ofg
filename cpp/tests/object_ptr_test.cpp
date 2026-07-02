// Doctest coverage for Object and Ptr lifetime-aware non-owning references.
//
// These tests pin the Milestone 0 safety contract: stored observers become null
// when their Object target is destroyed, and accidental dereference reports a
// clear EngineError instead of following stale memory.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/core/object.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/scene/scene.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <webgpu/webgpu.h>

namespace {

class TestObject : public ofg::Object {
public:
    explicit TestObject(int value) noexcept : m_value(value) {}

    int m_value{0};
};

// Produces a non-null opaque WebGPU device handle for resource-owner tests.
WGPUDevice fake_device() {
    return reinterpret_cast<WGPUDevice>(static_cast<std::uintptr_t>(40));
}

// Produces a non-null opaque WebGPU queue handle for resource-owner tests.
WGPUQueue fake_queue() {
    return reinterpret_cast<WGPUQueue>(static_cast<std::uintptr_t>(41));
}

} // namespace

// Verifies Ptr observes live objects and throws when no live object is present.
TEST_CASE("Ptr reports null access clearly") {
    ofg::Ptr<TestObject> empty;
    CHECK(empty.get() == nullptr);
    CHECK_FALSE(static_cast<bool>(empty));
    CHECK_THROWS_WITH_AS([&]() { (void)empty->m_value; }(), doctest::Contains("Ptr<"), ofg::EngineError);

    TestObject object{7};
    ofg::Ptr<TestObject> pointer{&object};
    REQUIRE(pointer.get() == &object);
    CHECK(pointer->m_value == 7);
    CHECK((*pointer).m_value == 7);

    pointer = nullptr;
    CHECK(pointer.get() == nullptr);
    CHECK_THROWS_WITH_AS([&]() { (void)*pointer; }(), doctest::Contains("live object"), ofg::EngineError);
}

// Verifies Object destruction nulls every registered Ptr copy and move target.
TEST_CASE("Object destruction invalidates registered Ptr values") {
    ofg::Ptr<TestObject> first;
    ofg::Ptr<TestObject> second;
    ofg::Ptr<TestObject> moved;

    {
        auto object = std::make_unique<TestObject>(11);
        first = object.get();
        second = first;
        moved = ofg::Ptr<TestObject>{object.get()};

        CHECK(first.get() == object.get());
        CHECK(second.get() == object.get());
        CHECK(moved.get() == object.get());
        object.reset();
    }

    CHECK(first.get() == nullptr);
    CHECK(second.get() == nullptr);
    CHECK(moved.get() == nullptr);
    CHECK_THROWS_WITH_AS([&]() { (void)first->m_value; }(), doctest::Contains("live object"), ofg::EngineError);
}

// Verifies Ptr copy and move assignment maintain one valid intrusive list entry.
TEST_CASE("Ptr copy and move assignment preserve observer registration") {
    TestObject first_object{3};
    TestObject second_object{4};

    ofg::Ptr<TestObject> first{&first_object};
    ofg::Ptr<TestObject> copy = first;
    ofg::Ptr<TestObject> moved = std::move(copy);
    CHECK(first.get() == &first_object);
    CHECK(copy.get() == nullptr);
    CHECK(moved.get() == &first_object);

    first = &second_object;
    CHECK(first.get() == &second_object);
    CHECK(moved.get() == &first_object);

    moved = std::move(first);
    CHECK(first.get() == nullptr);
    CHECK(moved.get() == &second_object);
}

// Verifies scene-owned Object destruction invalidates external stored observers.
TEST_CASE("Scene clear invalidates Ptr values to entities and components") {
    ofg::Scene scene;
    ofg::Entity* entity = scene.create_entity(scene.get_root());
    (void)entity->create_component(ofg::ComponentType::MeshRenderer);

    ofg::Ptr<ofg::Entity> entity_ref{entity};
    ofg::Ptr<ofg::MeshRenderer> renderer_ref{entity->mesh_renderer()};
    REQUIRE(entity_ref.get() == entity);
    REQUIRE(renderer_ref.get() == entity->mesh_renderer());

    scene.clear();

    CHECK(entity_ref.get() == nullptr);
    CHECK(renderer_ref.get() == nullptr);
    CHECK_THROWS_WITH_AS([&]() { (void)renderer_ref->mesh(); }(), doctest::Contains("live object"), ofg::EngineError);
}

// Verifies Resources destruction invalidates Ptr values to durable resources.
TEST_CASE("Resources release invalidates Ptr values to resources") {
    ofg::Resources::destroy();
    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::prepare());

    ofg::Mesh& mesh = ofg::Resources::create_mesh("observed mesh");
    ofg::Ptr<ofg::Mesh> mesh_ref{&mesh};
    REQUIRE(mesh_ref.get() == &mesh);

    CHECK(ofg::Resources::release());
    CHECK(mesh_ref.get() == nullptr);
    CHECK_THROWS_WITH_AS([&]() { (void)mesh_ref->label(); }(), doctest::Contains("live object"), ofg::EngineError);

    ofg::Resources::destroy();
}
