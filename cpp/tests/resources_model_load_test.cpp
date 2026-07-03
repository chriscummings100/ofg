// Doctest coverage for Resources-owned model-resource loading.
//
// These tests exercise the first resource scheduler on top of generic blob
// loads: stable ModelResource pointers, root blob waiting, dependency discovery,
// dependency failure/success, and in-place import into Resources-owned storage.
#include "doctest.h"
#include "webgpu_test_utils.hpp"

#include "ofg/assets/model_resource.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/resources/resource.hpp"
#include "ofg/resources/resources.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Returns the repository test asset directory supplied by CMake.
std::filesystem::path asset_dir() {
    return std::filesystem::path{OFG_TEST_ASSET_DIR};
}

// Reads a fixture file into byte storage.
std::vector<std::byte> read_fixture_bytes(std::string_view filename) {
    const std::filesystem::path path = asset_dir() / std::filesystem::path{std::string(filename)};
    std::ifstream file(path, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "Could not open fixture " << path.string());
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    REQUIRE(size >= 0);
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    REQUIRE_MESSAGE(file.good(), "Could not read fixture " << path.string());
    return bytes;
}

// Builds byte data from ordinary unsigned byte literals.
std::vector<std::byte> byte_values(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (const std::uint8_t value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Returns a valid 1x1 transparent PNG for the animated-cube fixture image.
std::vector<std::byte> transparent_png_bytes() {
    return byte_values({0x89,
        0x50,
        0x4E,
        0x47,
        0x0D,
        0x0A,
        0x1A,
        0x0A,
        0x00,
        0x00,
        0x00,
        0x0D,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x01,
        0x00,
        0x00,
        0x00,
        0x01,
        0x08,
        0x06,
        0x00,
        0x00,
        0x00,
        0x1F,
        0x15,
        0xC4,
        0x89,
        0x00,
        0x00,
        0x00,
        0x0A,
        0x49,
        0x44,
        0x41,
        0x54,
        0x78,
        0x9C,
        0x63,
        0x00,
        0x01,
        0x00,
        0x00,
        0x05,
        0x00,
        0x01,
        0x0D,
        0x0A,
        0x2D,
        0xB4,
        0x00,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4E,
        0x44,
        0xAE,
        0x42,
        0x60,
        0x82});
}

// Creates and prepares the Resources singleton for model loader tests.
ofg::tests::TestGpuContext create_test_resources() {
    std::string error;
    std::optional<ofg::tests::TestGpuContext> gpu = ofg::tests::TestGpuContext::create(error);
    REQUIRE_MESSAGE(gpu.has_value(), error);

    ofg::Resources::destroy();
    ofg::Resources::create(gpu->borrowed_context());
    REQUIRE(ofg::Resources::prepare());
    return std::move(*gpu);
}

// Completes every currently queued blob with bytes selected by URI.
void complete_pending_blob_requests() {
    const std::vector<ofg::PendingBlobLoad> pending(
        ofg::Resources::pending_blob_loads().begin(), ofg::Resources::pending_blob_loads().end());
    for (const ofg::PendingBlobLoad& request : pending) {
        ofg::Resources::mark_blob_loading(request.m_id);
        if (request.m_uri.ends_with("static-box.glb")) {
            ofg::Resources::complete_blob_load(request.m_id, read_fixture_bytes("static-box.glb"));
        } else if (request.m_uri.ends_with("animated-cube.gltf")) {
            ofg::Resources::complete_blob_load(request.m_id, read_fixture_bytes("animated-cube.gltf"));
        } else if (request.m_uri.ends_with("AnimatedCube.bin")) {
            ofg::Resources::complete_blob_load(request.m_id, read_fixture_bytes("animated-cube.bin"));
        } else if (request.m_uri.ends_with("AnimatedCube_BaseColor.png")) {
            ofg::Resources::complete_blob_load(request.m_id, transparent_png_bytes());
        } else {
            FAIL("Unexpected model loader blob request: " << request.m_uri);
        }
    }
}

} // namespace

// Verifies Resource state diagnostics and ModelResource inheritance.
TEST_CASE("Resource state diagnostics are stable") {
    ofg::ModelResource resource;
    CHECK(resource.state() == ofg::ResourceState::Unloaded);
    CHECK_FALSE(resource.is_in_progress());
    CHECK_FALSE(resource.is_loaded());
    CHECK_FALSE(resource.is_failed());
    CHECK_FALSE(resource.is_terminal());
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::Unloaded)) == "unloaded");
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::Queued)) == "queued");
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::LoadingRootBlob)) == "loading_root_blob");
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::DiscoveringDependencies)) ==
          "discovering_dependencies");
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::WaitingForDependencies)) ==
          "waiting_for_dependencies");
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::Importing)) == "importing");
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::Loaded)) == "loaded");
    CHECK(std::string(ofg::resource_state_name(ofg::ResourceState::Failed)) == "failed");
    CHECK(std::string(ofg::resource_state_name(static_cast<ofg::ResourceState>(100))) == "unknown");
}

// Verifies a model resource waits while its root blob has not been completed.
TEST_CASE("Resources load_model_resource waits for a pending root blob") {
    ofg::tests::TestGpuContext gpu = create_test_resources();

    ofg::Ptr<ofg::ModelResource> model = ofg::Resources::load_model_resource("assets/models/tests/static-box.glb");
    REQUIRE(model.get() != nullptr);

    ofg::Resources::advance_loads();
    CHECK(model->state() == ofg::ResourceState::LoadingRootBlob);
    CHECK(model->is_in_progress());

    ofg::Resources::advance_loads();
    CHECK(model->state() == ofg::ResourceState::LoadingRootBlob);
    CHECK(ofg::Resources::pending_blob_loads().size() == 1);

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies invalid completed root bytes fail during dependency discovery.
TEST_CASE("Resources load_model_resource reports root parse failures") {
    ofg::tests::TestGpuContext gpu = create_test_resources();

    ofg::Ptr<ofg::ModelResource> model = ofg::Resources::load_model_resource("assets/models/tests/static-box.glb");
    REQUIRE(model.get() != nullptr);
    REQUIRE(ofg::Resources::pending_blob_loads().size() == 1);
    const ofg::BlobLoadId root_id = ofg::Resources::pending_blob_loads()[0].m_id;

    ofg::Resources::mark_blob_loading(root_id);
    ofg::Resources::complete_blob_load(root_id, byte_values({0x01, 0x02, 0x03, 0x04}));
    for (int step = 0; step < 4 && !model->is_terminal(); ++step) {
        ofg::Resources::advance_loads();
    }

    CHECK(model->is_failed());
    CHECK(model->load_error().find("static-box.glb") != std::string::npos);
    CHECK(model->load_error().find("dependency discovery") != std::string::npos);

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies a single-file GLB model resource loads into a stable Resources-owned object.
TEST_CASE("Resources load_model_resource imports a completed root GLB blob") {
    ofg::tests::TestGpuContext gpu = create_test_resources();

    ofg::Ptr<ofg::ModelResource> model = ofg::Resources::load_model_resource("assets/models/tests/static-box.glb");
    ofg::Ptr<ofg::ModelResource> duplicate = ofg::Resources::load_model_resource("/assets/models/tests/static-box.glb");
    CHECK(model.get() == duplicate.get());
    REQUIRE(model.get() != nullptr);
    CHECK(model->state() == ofg::ResourceState::Queued);
    CHECK(model->source_uri() == "assets/models/tests/static-box.glb");
    CHECK(ofg::Resources::model_resources().size() == 1);

    complete_pending_blob_requests();
    for (int step = 0; step < 4 && !model->is_terminal(); ++step) {
        ofg::Resources::advance_loads();
    }

    CHECK(model->is_loaded());
    CHECK(model->label() == "static-box");
    CHECK_FALSE(model->nodes().empty());
    CHECK_FALSE(model->root_node_indices().empty());

    CHECK(ofg::Resources::release());
    CHECK(model.get() == nullptr);
    ofg::Resources::destroy();
}

// Verifies root blob failures transition the stable model resource to Failed.
TEST_CASE("Resources load_model_resource reports root blob failure") {
    ofg::tests::TestGpuContext gpu = create_test_resources();

    ofg::Ptr<ofg::ModelResource> model = ofg::Resources::load_model_resource("assets/models/tests/static-box.glb");
    REQUIRE(model.get() != nullptr);
    REQUIRE(ofg::Resources::pending_blob_loads().size() == 1);
    const ofg::BlobLoadId root_id = ofg::Resources::pending_blob_loads()[0].m_id;

    ofg::Resources::mark_blob_loading(root_id);
    ofg::Resources::fail_blob_load(root_id, "fixture unavailable");
    for (int step = 0; step < 4 && !model->is_terminal(); ++step) {
        ofg::Resources::advance_loads();
    }

    CHECK(model->is_failed());
    CHECK(model->load_error().find("static-box.glb") != std::string::npos);
    CHECK(model->load_error().find("fixture unavailable") != std::string::npos);
    CHECK(ofg::Resources::load_model_resource("assets/models/tests/static-box.glb").get() == model.get());
    CHECK(ofg::Resources::pending_blob_loads().empty());

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies a text glTF model discovers and waits for external blob dependencies.
TEST_CASE("Resources load_model_resource discovers external glTF blob dependencies") {
    ofg::tests::TestGpuContext gpu = create_test_resources();

    ofg::Ptr<ofg::ModelResource> model = ofg::Resources::load_model_resource(
        "assets/models/tests/animated-cube.gltf", ofg::ModelResourceLoadOptions{"AnimatedCube"});
    REQUIRE(model.get() != nullptr);

    for (int step = 0; step < 20 && !model->is_terminal(); ++step) {
        complete_pending_blob_requests();
        ofg::Resources::advance_loads();
    }

    INFO("state: " << static_cast<int>(model->state()) << " " << std::string(ofg::resource_state_name(model->state())));
    INFO("error: " << model->load_error());
    INFO("pending blob requests: " << ofg::Resources::pending_blob_loads().size());
    CHECK(model->is_loaded());
    CHECK(model->label() == "AnimatedCube");
    CHECK(model->animation_clip_count() == 1);
    CHECK_FALSE(model->mesh_renderers().empty());

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}

// Verifies external glTF dependency failures keep enough URI context for diagnosis.
TEST_CASE("Resources load_model_resource reports external dependency failure") {
    ofg::tests::TestGpuContext gpu = create_test_resources();

    ofg::Ptr<ofg::ModelResource> model = ofg::Resources::load_model_resource(
        "assets/models/tests/animated-cube.gltf", ofg::ModelResourceLoadOptions{"AnimatedCube"});
    REQUIRE(model.get() != nullptr);

    complete_pending_blob_requests();
    ofg::Resources::advance_loads();
    ofg::Resources::advance_loads();
    ofg::Resources::advance_loads();
    REQUIRE(model->state() == ofg::ResourceState::WaitingForDependencies);
    REQUIRE(ofg::Resources::pending_blob_loads().size() == 1);
    ofg::Resources::advance_loads();
    CHECK(model->state() == ofg::ResourceState::WaitingForDependencies);
    const ofg::PendingBlobLoad dependency = ofg::Resources::pending_blob_loads()[0];
    CHECK(dependency.m_uri.ends_with("AnimatedCube.bin"));

    ofg::Resources::mark_blob_loading(dependency.m_id);
    ofg::Resources::fail_blob_load(dependency.m_id, "dependency missing");
    ofg::Resources::advance_loads();

    CHECK(model->is_failed());
    CHECK(model->load_error().find("animated-cube.gltf") != std::string::npos);
    CHECK(model->load_error().find("AnimatedCube.bin") != std::string::npos);
    CHECK(model->load_error().find("dependency missing") != std::string::npos);

    CHECK(ofg::Resources::release());
    ofg::Resources::destroy();
}
