// Doctest coverage for the static Resources lifecycle facade.
//
// These tests validate singleton lifecycle behavior without calling WebGPU. The
// fake handles are used only for create/release paths; type-specific GPU resource
// creation remains covered by the resource and smoke tests.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <cstdint>
#include <string>

#include <webgpu/webgpu.h>

namespace {

// Produces a non-null opaque WebGPU device handle for create-only tests.
WGPUDevice fake_device() {
    return reinterpret_cast<WGPUDevice>(static_cast<std::uintptr_t>(10));
}

// Produces a non-null opaque WebGPU queue handle for create-only tests.
WGPUQueue fake_queue() {
    return reinterpret_cast<WGPUQueue>(static_cast<std::uintptr_t>(11));
}

} // namespace

// Verifies Resources rejects invalid setup before any resource creation.
TEST_CASE("Resources create validates GPU handles") {
    ofg::Resources::destroy();

    try {
        ofg::Resources::create(ofg::GpuContext{});
        FAIL("Expected Resources create without GPU handles to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("device and queue") != std::string::npos);
        CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Uninitialized);
    }

    ofg::Resources::destroy();
}

// Verifies lifecycle states expose stable diagnostic names.
TEST_CASE("Resources lifecycle states have diagnostic names") {
    CHECK(std::string(ofg::resources_lifecycle_state_name(ofg::ResourcesLifecycleState::Uninitialized)) ==
          "uninitialized");
    CHECK(std::string(ofg::resources_lifecycle_state_name(ofg::ResourcesLifecycleState::Created)) == "created");
    CHECK(std::string(ofg::resources_lifecycle_state_name(ofg::ResourcesLifecycleState::Preparing)) == "preparing");
    CHECK(std::string(ofg::resources_lifecycle_state_name(ofg::ResourcesLifecycleState::Ready)) == "ready");
    CHECK(std::string(ofg::resources_lifecycle_state_name(ofg::ResourcesLifecycleState::Releasing)) == "releasing");
    CHECK(std::string(ofg::resources_lifecycle_state_name(ofg::ResourcesLifecycleState::Released)) == "released");
    CHECK(std::string(ofg::resources_lifecycle_state_name(ofg::ResourcesLifecycleState::Failed)) == "failed");
    CHECK(
        std::string(ofg::resources_lifecycle_state_name(static_cast<ofg::ResourcesLifecycleState>(100))) == "unknown");
}

// Verifies one live Resources singleton can be prepared, released, and destroyed.
TEST_CASE("Resources lifecycle owns one live singleton") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Created);
    CHECK(ofg::Resources::prepare());
    CHECK(ofg::Resources::prepare());
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Ready);
    CHECK(ofg::Resources::gpu_context().m_backend == "TestBackend");
    CHECK(ofg::Resources::textures().empty());

    try {
        ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "second fake adapter", "TestBackend"});
        FAIL("Expected duplicate Resources create to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("singleton is live") != std::string::npos);
        CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Ready);
    }

    CHECK(ofg::Resources::release());
    CHECK(ofg::Resources::release());
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Released);
    ofg::Resources::destroy();
}

// Verifies create_* only allocates labeled resources in stable storage.
TEST_CASE("Resources create methods allocate labeled resources") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::prepare());
    ofg::Texture& texture = ofg::Resources::create_texture("allocated texture");
    ofg::Shader& shader = ofg::Resources::create_shader("allocated shader");
    ofg::Material& material = ofg::Resources::create_material("allocated material");
    ofg::Mesh& mesh = ofg::Resources::create_mesh("allocated mesh");

    CHECK(texture.label() == "allocated texture");
    CHECK(shader.label() == "allocated shader");
    CHECK(material.label() == "allocated material");
    CHECK(mesh.label() == "allocated mesh");
    CHECK(texture.revision() == 0);
    CHECK(shader.revision() == 0);
    CHECK(material.revision() == 0);
    CHECK(mesh.revision() == 0);
    CHECK(ofg::Resources::textures().size() == 1);
    CHECK(ofg::Resources::shaders().size() == 1);
    CHECK(ofg::Resources::materials().size() == 1);
    CHECK(ofg::Resources::meshes().size() == 1);

    CHECK(ofg::Resources::release());
    try {
        (void)ofg::Resources::create_texture("too late");
        FAIL("Expected create after release to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("before release") != std::string::npos);
    }

    ofg::Resources::destroy();
}

// Verifies released Resources rejects new allocation and prepare retries.
TEST_CASE("Resources rejects prepare and create after release starts") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::prepare());
    CHECK(ofg::Resources::release());

    try {
        (void)ofg::Resources::create_material("too late");
        FAIL("Expected Resources create after release to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("before release") != std::string::npos);
        CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Released);
    }

    try {
        (void)ofg::Resources::prepare();
        FAIL("Expected Resources prepare after release to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("release has started") != std::string::npos);
        CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Released);
    }

    ofg::Resources::destroy();
}

// Verifies destroy only clears the singleton pointer.
TEST_CASE("Resources destroy clears live singleton") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Created);
    ofg::Resources::destroy();
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Uninitialized);
}

// Verifies prepare fails clearly before Resources has been created.
TEST_CASE("Resources prepare requires create") {
    ofg::Resources::destroy();

    try {
        (void)ofg::Resources::prepare();
        FAIL("Expected Resources prepare before init to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("requires Resources::create") != std::string::npos);
        CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Uninitialized);
    }

    ofg::Resources::destroy();
}
