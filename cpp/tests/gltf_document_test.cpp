// Doctest coverage for the native-checkable glTF document parse layer.
//
// These tests keep tinygltf behind the OFG API while proving GLB, embedded glTF,
// external resources, and accessor byte views work against repository fixtures.
#include "doctest.h"

#include "ofg/assets/gltf_document.hpp"
#include "ofg/core/engine_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::int32_t _component_float = 5126;
constexpr std::int32_t _type_vec3 = 3;

// Returns the repository test asset directory supplied by CMake.
std::filesystem::path asset_dir() {
    return std::filesystem::path{OFG_TEST_ASSET_DIR};
}

// Reads a binary fixture into memory for the memory-oriented parse API.
std::vector<std::byte> read_fixture_bytes(const std::filesystem::path& path) {
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

class AnimatedCubeFixtureProvider : public ofg::GltfResourceProvider {
public:
    explicit AnimatedCubeFixtureProvider(std::filesystem::path base_directory)
        : m_base_directory(std::move(base_directory)) {}

    std::optional<ofg::AssetFile> load_relative(std::string_view uri) override {
        if (uri == "AnimatedCube.bin") {
            return read_named_file("AnimatedCube.bin", m_base_directory / "animated-cube.bin");
        }
        if (uri == "AnimatedCube_BaseColor.png") {
            return ofg::AssetFile{std::string(uri), transparent_png_bytes()};
        }
        return read_named_file(std::string(uri), m_base_directory / std::filesystem::path{std::string(uri)});
    }

private:
    // Returns a valid 1x1 transparent PNG for the known missing fixture image.
    static std::vector<std::byte> transparent_png_bytes() {
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

    static std::optional<ofg::AssetFile> read_named_file(std::string label, const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::nullopt;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) {
            return std::nullopt;
        }
        file.seekg(0, std::ios::beg);
        ofg::AssetFile result;
        result.m_path = std::move(label);
        result.m_bytes.resize(static_cast<std::size_t>(size));
        if (!result.m_bytes.empty()) {
            file.read(reinterpret_cast<char*>(result.m_bytes.data()), size);
        }
        if (!file) {
            return std::nullopt;
        }
        return result;
    }

    std::filesystem::path m_base_directory;
};

} // namespace

// Verifies a compact GLB fixture parses from native filesystem bytes.
TEST_CASE("GltfDocument parses static GLB fixtures") {
    const ofg::GltfDocument document = ofg::load_gltf_document_from_path(asset_dir() / "static-box.glb");

    CHECK(document.label() == "static-box.glb");
    CHECK(document.is_binary());
    CHECK(document.scene_count() == 1);
    CHECK(document.node_count() >= 1);
    CHECK(document.mesh_count() == 1);
    CHECK(document.material_count() >= 1);
    REQUIRE(document.buffers().size() == 1);
    CHECK(document.buffers()[0].m_bytes.size() > 0);
    REQUIRE(document.accessors().size() >= 1);
    REQUIRE(document.meshes().size() == document.mesh_count());
    REQUIRE(document.meshes()[0].m_primitives.size() == 1);
    CHECK(document.meshes()[0].m_primitives[0].m_mode == 4);
    CHECK_FALSE(document.materials().empty());

    const ofg::GltfAccessorDataView view = document.accessor_data(0);
    CHECK(view.m_data.size() > 0);
    CHECK(view.m_stride >= view.m_element_size);
}

// Verifies embedded data-URI glTF fixtures expose skeleton and animation counts.
TEST_CASE("GltfDocument parses embedded skin and animation data") {
    const ofg::GltfDocument document = ofg::load_gltf_document_from_path(asset_dir() / "simple-skin.gltf");

    CHECK_FALSE(document.is_binary());
    CHECK(document.scene_count() == 1);
    CHECK(document.skin_count() == 1);
    CHECK(document.animation_count() == 1);
    CHECK(document.buffers().size() == 4);
    REQUIRE(document.skins().size() == 1);
    CHECK(document.skins()[0].m_joint_node_indices.size() > 0);
    REQUIRE(document.animations().size() == 1);
    CHECK(document.animations()[0].m_channels.size() > 0);
    REQUIRE(document.accessors().size() > 0);

    bool found_vec3_float_accessor = false;
    for (std::size_t index = 0; index < document.accessors().size(); ++index) {
        const ofg::GltfAccessor& accessor = document.accessors()[index];
        if (accessor.m_component_type == _component_float && accessor.m_type == _type_vec3 &&
            accessor.m_buffer_view_index >= 0) {
            const ofg::GltfAccessorDataView view = document.accessor_data(index);
            CHECK(view.m_element_size == sizeof(float) * 3U);
            CHECK(view.m_data.size() >= view.m_element_size);
            found_vec3_float_accessor = true;
            break;
        }
    }
    CHECK(found_vec3_float_accessor);
}

// Verifies known animated-cube fixture resource issues are handled explicitly by tests.
TEST_CASE("GltfDocument parses animated cube with documented fixture aliases") {
    const std::filesystem::path primary_path = asset_dir() / "animated-cube.gltf";
    std::vector<std::byte> bytes = read_fixture_bytes(primary_path);
    AnimatedCubeFixtureProvider provider{asset_dir()};

    const ofg::GltfDocument document = ofg::load_gltf_document("animated-cube.gltf", bytes, provider);

    CHECK_FALSE(document.is_binary());
    CHECK(document.mesh_count() == 1);
    CHECK(document.animation_count() >= 1);
    REQUIRE(document.buffers().size() == 1);
    CHECK(document.buffers()[0].m_bytes.size() == 1860);
    REQUIRE(document.images().size() == 1);
    CHECK(document.images()[0].m_uri == "AnimatedCube_BaseColor.png");
    CHECK(document.images()[0].m_width == 1);
    CHECK(document.images()[0].m_height == 1);
    CHECK(document.images()[0].m_bytes.size() > 0);
}

// Verifies missing external resources fail with a diagnostic naming the resource.
TEST_CASE("GltfDocument reports missing external fixture resources") {
    const std::filesystem::path primary_path = asset_dir() / "animated-cube.gltf";
    std::vector<std::byte> bytes = read_fixture_bytes(primary_path);
    ofg::FilesystemGltfResourceProvider provider{asset_dir()};

    CHECK_THROWS_WITH_AS(([&]() { (void)ofg::load_gltf_document("animated-cube.gltf", bytes, provider); }()),
        doctest::Contains("AnimatedCube.bin"),
        ofg::EngineError);
}

// Verifies accessor data errors are explicit.
TEST_CASE("GltfDocument rejects invalid accessor views") {
    const ofg::GltfDocument document = ofg::load_gltf_document_from_path(asset_dir() / "static-box.glb");

    CHECK_THROWS_WITH_AS(([&]() { (void)document.accessor_data(document.accessors().size()); }()),
        doctest::Contains("out of range"),
        ofg::EngineError);
}
