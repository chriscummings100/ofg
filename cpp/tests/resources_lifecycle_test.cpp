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

#include <array>
#include <cstddef>
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

// Verifies blob load statuses expose stable diagnostic names.
TEST_CASE("Resources blob load statuses have diagnostic names") {
    CHECK(std::string(ofg::blob_load_status_name(ofg::BlobLoadStatus::Missing)) == "missing");
    CHECK(std::string(ofg::blob_load_status_name(ofg::BlobLoadStatus::Queued)) == "queued");
    CHECK(std::string(ofg::blob_load_status_name(ofg::BlobLoadStatus::Loading)) == "loading");
    CHECK(std::string(ofg::blob_load_status_name(ofg::BlobLoadStatus::Loaded)) == "loaded");
    CHECK(std::string(ofg::blob_load_status_name(ofg::BlobLoadStatus::Failed)) == "failed");
    CHECK(std::string(ofg::blob_load_status_name(static_cast<ofg::BlobLoadStatus>(100))) == "unknown");
}

// Verifies release is idempotent before prepare and before create.
TEST_CASE("Resources release handles uninitialized and created states") {
    ofg::Resources::destroy();
    CHECK(ofg::Resources::release());
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Uninitialized);

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Created);
    CHECK(ofg::Resources::release());
    CHECK(ofg::Resources::state() == ofg::ResourcesLifecycleState::Released);
    ofg::Resources::destroy();
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

// Verifies blob requests use normalized relative URIs as deduplication keys.
TEST_CASE("Resources blob requests normalize and deduplicate asset URIs") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::prepare());

    const ofg::BlobLoadId first_id = ofg::Resources::load_blob("/assets/models/player.glb");
    const ofg::BlobLoadId second_id = ofg::Resources::load_blob("assets/models/player.glb");

    CHECK(first_id != ofg::invalid_blob_load_id);
    CHECK(second_id == first_id);
    REQUIRE(ofg::Resources::pending_blob_loads().size() == 1);
    CHECK(ofg::Resources::pending_blob_loads()[0].m_id == first_id);
    CHECK(ofg::Resources::pending_blob_loads()[0].m_uri == "assets/models/player.glb");

    const ofg::BlobView by_id = ofg::Resources::blob(first_id);
    CHECK(by_id.m_status == ofg::BlobLoadStatus::Queued);
    CHECK(by_id.m_uri == "assets/models/player.glb");
    CHECK(by_id.m_bytes.empty());
    CHECK(std::string(ofg::blob_load_status_name(by_id.m_status)) == "queued");

    const ofg::BlobView by_uri = ofg::Resources::blob_by_uri("assets/models/player.glb");
    CHECK(by_uri.m_id == first_id);
    CHECK(by_uri.m_status == ofg::BlobLoadStatus::Queued);

    const ofg::BlobView missing = ofg::Resources::blob_by_uri("assets/models/missing.glb");
    CHECK(missing.m_id == ofg::invalid_blob_load_id);
    CHECK(missing.m_status == ofg::BlobLoadStatus::Missing);

    CHECK(ofg::Resources::release());
    CHECK(ofg::Resources::pending_blob_loads().empty());
    CHECK(ofg::Resources::blob_by_uri("assets/models/player.glb").m_status == ofg::BlobLoadStatus::Missing);

    ofg::Resources::destroy();
}

// Verifies host blob completion exposes stable loaded bytes through lookup APIs.
TEST_CASE("Resources blob requests transition from queued to loaded") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::prepare());

    const ofg::BlobLoadId id = ofg::Resources::load_blob("assets/models/tests/static-box/BoxTextured.glb");
    ofg::Resources::mark_blob_loading(id);
    CHECK(ofg::Resources::pending_blob_loads().empty());

    const std::array<std::byte, 4> bytes{std::byte{0x67}, std::byte{0x6c}, std::byte{0x54}, std::byte{0x46}};
    ofg::Resources::complete_blob_load(id, bytes);

    const ofg::BlobView loaded = ofg::Resources::blob(id);
    CHECK(loaded.is_loaded());
    REQUIRE(loaded.m_bytes.size() == bytes.size());
    CHECK(loaded.m_bytes[0] == std::byte{0x67});
    CHECK(loaded.m_bytes[3] == std::byte{0x46});
    CHECK(loaded.m_error.empty());
    CHECK(ofg::Resources::load_blob("assets/models/tests/static-box/BoxTextured.glb") == id);
    CHECK(ofg::Resources::blob_by_uri("/assets/models/tests/static-box/BoxTextured.glb").is_loaded());

    try {
        ofg::Resources::complete_blob_load(id, bytes);
        FAIL("Expected completing an already loaded blob to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("loading blob request") != std::string::npos);
    }

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies host blob failures are tracked without losing the resource system.
TEST_CASE("Resources blob requests transition from queued to failed") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::prepare());

    const ofg::BlobLoadId id = ofg::Resources::load_blob("assets/models/missing.glb");
    ofg::Resources::mark_blob_loading(id);
    ofg::Resources::fail_blob_load(id, "asset was not found");

    const ofg::BlobView failed = ofg::Resources::blob(id);
    CHECK(failed.m_status == ofg::BlobLoadStatus::Failed);
    CHECK(failed.m_error == "asset was not found");
    CHECK(failed.m_bytes.empty());
    CHECK(std::string(ofg::blob_load_status_name(failed.m_status)) == "failed");

    try {
        ofg::Resources::mark_blob_loading(id);
        FAIL("Expected re-marking a failed blob to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("queued blob request") != std::string::npos);
    }

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies blob APIs fail clearly for invalid ids, unsafe URIs, and released resources.
TEST_CASE("Resources blob requests validate ids uris and lifecycle state") {
    ofg::Resources::destroy();

    ofg::Resources::create(ofg::GpuContext{fake_device(), fake_queue(), "fake adapter", "TestBackend"});
    CHECK(ofg::Resources::prepare());

    try {
        (void)ofg::Resources::load_blob("");
        FAIL("Expected empty blob URI to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("non-empty") != std::string::npos);
    }

    try {
        (void)ofg::Resources::load_blob("assets\\model.glb");
        FAIL("Expected backslash blob URI to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("forward slashes") != std::string::npos);
    }

    try {
        (void)ofg::Resources::load_blob("https://example.test/model.glb");
        FAIL("Expected URL blob URI to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("relative") != std::string::npos);
    }

    try {
        (void)ofg::Resources::load_blob("assets/../model.glb");
        FAIL("Expected parent-directory blob URI to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("cannot contain") != std::string::npos);
    }

    try {
        (void)ofg::Resources::load_blob("assets/model.glb?cache=1");
        FAIL("Expected query string blob URI to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("query strings") != std::string::npos);
    }

    try {
        (void)ofg::Resources::load_blob("assets//model.glb");
        FAIL("Expected empty path segment blob URI to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("path segment") != std::string::npos);
    }

    try {
        (void)ofg::Resources::blob(ofg::invalid_blob_load_id);
        FAIL("Expected invalid blob id lookup to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("valid blob load id") != std::string::npos);
    }

    try {
        (void)ofg::Resources::blob(999U);
        FAIL("Expected unknown blob id lookup to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("unknown blob load id") != std::string::npos);
    }

    try {
        ofg::Resources::mark_blob_loading(ofg::invalid_blob_load_id);
        FAIL("Expected invalid mark-loading id to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("valid blob load id") != std::string::npos);
    }

    try {
        ofg::Resources::mark_blob_loading(999U);
        FAIL("Expected unknown mark-loading id to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("unknown blob load id") != std::string::npos);
    }

    const std::array<std::byte, 1> bytes{std::byte{0x01}};
    try {
        ofg::Resources::complete_blob_load(ofg::invalid_blob_load_id, bytes);
        FAIL("Expected invalid complete id to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("valid blob load id") != std::string::npos);
    }

    try {
        ofg::Resources::complete_blob_load(999U, bytes);
        FAIL("Expected unknown complete id to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("unknown blob load id") != std::string::npos);
    }

    try {
        ofg::Resources::fail_blob_load(ofg::invalid_blob_load_id, "bad id");
        FAIL("Expected invalid failure id to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("valid blob load id") != std::string::npos);
    }

    try {
        ofg::Resources::fail_blob_load(999U, "bad id");
        FAIL("Expected unknown failure id to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("unknown blob load id") != std::string::npos);
    }

    const ofg::BlobLoadId queued_id = ofg::Resources::load_blob("assets/models/queued.glb");
    try {
        ofg::Resources::fail_blob_load(queued_id, "not loading yet");
        FAIL("Expected failing a queued blob to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("loading blob request") != std::string::npos);
    }

    CHECK(ofg::Resources::release());

    try {
        (void)ofg::Resources::load_blob("assets/models/player.glb");
        FAIL("Expected blob load after release to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("before release") != std::string::npos);
    }

    ofg::Resources::destroy();
}
