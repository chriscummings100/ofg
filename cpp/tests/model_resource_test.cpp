// Doctest coverage for glTF model-resource import and scene instantiation.
//
// These tests prove the first reusable ModelResource layer can load a static
// glTF/GLB mesh once, share its resources, and copy its entity/component graph
// into a live Scene multiple times.
#include "doctest.h"

#include "webgpu_test_utils.hpp"

#include "ofg/assets/gltf_document.hpp"
#include "ofg/assets/gltf_importer.hpp"
#include "ofg/assets/model_resource.hpp"
#include "ofg/core/engine_error.hpp"
#include "ofg/core/control_input.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/resources.hpp"
#include "ofg/resources/texture.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/scene.hpp"
#include "ofg/scene/scene_update.hpp"

#include "../src/assets/gltf_importer_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

// Returns the repository test asset directory supplied by CMake.
std::filesystem::path asset_dir() {
    return std::filesystem::path{OFG_TEST_ASSET_DIR};
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

// Reads a fixture into memory for provider-backed parse tests.
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

// Rewrites a small JSON fixture in memory for focused import-variant tests.
std::vector<std::byte> replace_fixture_text(
    std::vector<std::byte> bytes, std::string_view needle, std::string_view replacement) {
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const std::size_t position = text.find(needle);
    REQUIRE_MESSAGE(position != std::string::npos, "Could not find fixture text: " << needle);
    text.replace(position, needle.size(), replacement);

    std::vector<std::byte> replaced;
    replaced.reserve(text.size());
    for (char character : text) {
        replaced.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return replaced;
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

class ScopedResources {
public:
    // Creates central Resources storage backed by a Dawn null device.
    ScopedResources() {
        std::string error;
        m_owned_gpu = ofg::tests::TestGpuContext::create(error);
        REQUIRE_MESSAGE(m_owned_gpu.has_value(), error);
        create_resources(m_owned_gpu->borrowed_context());
    }

    ScopedResources(const ScopedResources&) = delete;
    ScopedResources& operator=(const ScopedResources&) = delete;

    // Releases Resources before the borrowed test GPU goes away.
    ~ScopedResources() {
        if (ofg::Resources::state() != ofg::ResourcesLifecycleState::Uninitialized) {
            (void)ofg::Resources::release();
            ofg::Resources::destroy();
        }
    }

private:
    void create_resources(ofg::GpuContext gpu) {
        ofg::Resources::destroy();
        ofg::Resources::create(gpu);
        REQUIRE(ofg::Resources::prepare());
    }

    std::optional<ofg::tests::TestGpuContext> m_owned_gpu;
};

// Loads the static box fixture into a reusable model resource.
std::unique_ptr<ofg::ModelResource> import_static_box(ofg::ModelResourceLoader& loader) {
    const ofg::GltfDocument document = ofg::load_gltf_document_from_path(asset_dir() / "static-box.glb");
    return ofg::import_gltf_model_resource(
        document, ofg::GltfImportOptions{"static-box", "assets/models/tests/static-box.glb"}, loader);
}

// Loads a named fixture into a reusable model resource.
std::unique_ptr<ofg::ModelResource> import_fixture_model(
    std::string model_name, std::string source_uri, ofg::ModelResourceLoader& loader) {
    const ofg::GltfDocument document = ofg::load_gltf_document_from_path(asset_dir() / source_uri);
    return ofg::import_gltf_model_resource(
        document, ofg::GltfImportOptions{std::move(model_name), std::move(source_uri)}, loader);
}

} // namespace

// Verifies static GLB import creates a format-neutral reusable model resource.
TEST_CASE("glTF importer converts static box into shared model resources") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_static_box(loader);

    REQUIRE(model != nullptr);
    CHECK(model->label() == "static-box");
    CHECK(model->nodes().size() == 2);
    CHECK(model->skins().empty());
    CHECK(model->animation_clip_count() == 0);
    REQUIRE(model->root_node_indices().size() == 1);
    CHECK(model->root_node_indices()[0] == 0);
    REQUIRE(model->mesh_renderers().size() == 1);
    CHECK(model->mesh_renderers()[0].m_node_index == 1);
    REQUIRE(model->mesh_renderers()[0].m_mesh != nullptr);
    CHECK(loader.mesh_count() == 1);
    CHECK(loader.material_count() == 1);
    CHECK(loader.texture_count() == 3);

    const ofg::Mesh* mesh = model->mesh_renderers()[0].m_mesh.get();
    REQUIRE(mesh != nullptr);
    CHECK(mesh->vertices().size() == 24);
    CHECK(mesh->indices().size() == 36);
    REQUIRE(mesh->submeshes().size() == 1);
    CHECK(mesh->submeshes()[0].m_default_material != nullptr);

    const ofg::math::Mat4 root_from_local = ofg::parent_from_local(model->nodes()[0].m_local_transform);
    const ofg::math::Vec4 local_y = ofg::math::mul(root_from_local, ofg::math::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    const ofg::math::Vec4 local_z = ofg::math::mul(root_from_local, ofg::math::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    CHECK(local_y.z == doctest::Approx(-1.0f));
    CHECK(local_z.y == doctest::Approx(1.0f));
}

// Verifies textured glTF materials import source textures and tangent data.
TEST_CASE("glTF importer creates PBR textures and imports tangents") {
    ScopedResources resources;
    AnimatedCubeFixtureProvider provider(asset_dir());
    const std::vector<std::byte> bytes = read_fixture_bytes(asset_dir() / "animated-cube.gltf");
    const ofg::GltfDocument document = ofg::load_gltf_document("animated-cube.gltf", bytes, provider);
    REQUIRE(document.materials().size() == 1);
    CHECK(document.materials()[0].m_base_color_texture_index >= 0);

    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = ofg::import_gltf_model_resource(
        document, ofg::GltfImportOptions{"animated-cube", "assets/models/tests/animated-cube.gltf"}, loader);

    REQUIRE(model != nullptr);
    REQUIRE(model->mesh_renderers().size() == 1);
    const ofg::Mesh* mesh = model->mesh_renderers()[0].m_mesh.get();
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices().size() > 0);
    CHECK(std::abs(mesh->vertices()[0].m_tangent[3]) == doctest::Approx(1.0f));
    CHECK(loader.texture_count() == 3);

    REQUIRE(mesh->submeshes().size() == 1);
    const ofg::Material* material = mesh->submeshes()[0].m_default_material.get();
    REQUIRE(material != nullptr);
    const ofg::PropertyValue* base_texture_value = material->properties().get("base_color_texture");
    REQUIRE(base_texture_value != nullptr);
    const ofg::Texture* base_texture = std::get<ofg::Ptr<ofg::Texture>>(*base_texture_value).get();
    REQUIRE(base_texture != nullptr);
    CHECK(base_texture->pixel_format() == ofg::TexturePixelFormat::Rgba8Srgb);
    CHECK(base_texture->width() == 1);
    CHECK(base_texture->height() == 1);

    const ofg::PropertyValue* pbr_factors_value = material->properties().get("pbr_factors");
    REQUIRE(pbr_factors_value != nullptr);
    const ofg::math::Vec4 pbr_factors = std::get<ofg::math::Vec4>(*pbr_factors_value);
    CHECK(pbr_factors.w == doctest::Approx(0.0f));
}

// Verifies generated tangents tolerate real-world degenerate UV triangles.
TEST_CASE("glTF generated tangents fall back for degenerate texture coordinates") {
    std::vector<ofg::MeshVertex> vertices(3);
    vertices[0].m_position = {0.0f, 0.0f, 0.0f};
    vertices[1].m_position = {1.0f, 0.0f, 0.0f};
    vertices[2].m_position = {0.0f, 1.0f, 0.0f};
    for (ofg::MeshVertex& vertex : vertices) {
        vertex.m_normal = {0.0f, 0.0f, 1.0f};
        vertex.m_uv = {0.0f, 0.0f};
    }
    const std::vector<std::uint32_t> indices{0, 1, 2};

    REQUIRE_NOTHROW(ofg::gltf_importer_detail::generate_tangents(vertices, indices, 0, 3, 0, 3));

    for (const ofg::MeshVertex& vertex : vertices) {
        const float tangent_length_squared = vertex.m_tangent[0] * vertex.m_tangent[0] +
                                             vertex.m_tangent[1] * vertex.m_tangent[1] +
                                             vertex.m_tangent[2] * vertex.m_tangent[2];
        CHECK(tangent_length_squared == doctest::Approx(1.0f));
        CHECK(vertex.m_tangent[3] == doctest::Approx(1.0f));
    }
}

// Verifies imported animation clips bind to scene entities through an AnimationPlayer component.
TEST_CASE("glTF importer binds animation clips to instantiated scene entities") {
    ScopedResources resources;
    AnimatedCubeFixtureProvider provider(asset_dir());
    const std::vector<std::byte> bytes = read_fixture_bytes(asset_dir() / "animated-cube.gltf");
    const ofg::GltfDocument document = ofg::load_gltf_document("animated-cube.gltf", bytes, provider);

    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = ofg::import_gltf_model_resource(
        document, ofg::GltfImportOptions{"animated-cube", "assets/models/tests/animated-cube.gltf"}, loader);

    REQUIRE(model != nullptr);
    REQUIRE(model->animation_clip_count() == 1);
    ofg::AnimationClip* clip = model->animation_clip(0);
    REQUIRE(clip != nullptr);
    CHECK(clip->name() == "animation_AnimatedCube");
    CHECK(clip->duration_seconds() == doctest::Approx(2.0));
    REQUIRE(clip->channels().size() == 1);
    CHECK(clip->channels()[0].m_target_node_index == 0);
    CHECK(clip->channels()[0].m_target_path == ofg::AnimationTargetPath::Rotation);

    ofg::Scene scene;
    ofg::ModelInstance instance = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    REQUIRE(instance.m_animation_player != nullptr);
    CHECK(scene.animation_player_count() == 1);
    REQUIRE(instance.m_entities_by_node_index.size() == 1);

    instance.m_animation_player->play(*clip, false);
    instance.m_animation_player->set_time_seconds(1.0);
    ofg::ControlInput controls;
    ofg::SceneUpdateContext update_context{controls, 1000.0, 0.0f, nullptr, nullptr};
    scene.update(update_context);

    const ofg::LocalTransform& animated_transform = instance.m_entities_by_node_index[0]->local_transform();
    CHECK(animated_transform.m_rotation.y == doctest::Approx(1.0f));
    CHECK(animated_transform.m_rotation.w == doctest::Approx(0.0f).epsilon(0.0001));

    instance.m_entities_by_node_index[0]->local_transform().m_position = ofg::math::vec3(3.0f, 4.0f, 5.0f);
    CHECK(instance.m_entities_by_node_index[0]->local_transform().m_position.y == doctest::Approx(4.0f));

    ofg::Ptr<ofg::AnimationPlayer> animation_player = instance.m_animation_player;
    scene.clear();
    CHECK(scene.animation_player_count() == 0);
    CHECK(animation_player == nullptr);
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)animation_player->time_seconds(); }()), doctest::Contains("AnimationPlayer"), ofg::EngineError);
}

// Verifies STEP animation interpolation is preserved in imported clip data.
TEST_CASE("glTF importer imports STEP animation interpolation") {
    ScopedResources resources;
    const std::vector<std::byte> bytes = replace_fixture_text(read_fixture_bytes(asset_dir() / "simple-skin.gltf"),
        "\"interpolation\" : \"LINEAR\"",
        "\"interpolation\" : \"STEP\"");
    ofg::FilesystemGltfResourceProvider provider{asset_dir()};
    const ofg::GltfDocument document = ofg::load_gltf_document("simple-skin-step.gltf", bytes, provider);

    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = ofg::import_gltf_model_resource(
        document, ofg::GltfImportOptions{"simple-skin-step", "assets/models/tests/simple-skin-step.gltf"}, loader);

    REQUIRE(model != nullptr);
    REQUIRE(model->animation_clip_count() == 1);
    ofg::AnimationClip* clip = model->animation_clip(0);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->channels().size() == 1);
    CHECK(clip->channels()[0].m_interpolation == ofg::AnimationInterpolation::Step);
}

// Verifies unsupported CUBICSPLINE animation interpolation fails clearly.
TEST_CASE("glTF importer rejects CUBICSPLINE animation interpolation") {
    ScopedResources resources;
    const std::vector<std::byte> bytes = replace_fixture_text(read_fixture_bytes(asset_dir() / "simple-skin.gltf"),
        "\"interpolation\" : \"LINEAR\"",
        "\"interpolation\" : \"CUBICSPLINE\"");
    ofg::FilesystemGltfResourceProvider provider{asset_dir()};
    const ofg::GltfDocument document = ofg::load_gltf_document("simple-skin-cubic.gltf", bytes, provider);

    ofg::ModelResourceLoader loader;
    CHECK_THROWS_WITH_AS((void)ofg::import_gltf_model_resource(document,
                             ofg::GltfImportOptions{"simple-skin-cubic", "assets/models/tests/simple-skin-cubic.gltf"},
                             loader),
        doctest::Contains("CUBICSPLINE"),
        ofg::EngineError);
}

// Verifies a glTF skin without an explicit skeleton root binds to instantiated joint entities.
TEST_CASE("glTF importer binds simple skin joints through mesh renderer metadata") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_fixture_model("simple-skin", "simple-skin.gltf", loader);

    REQUIRE(model != nullptr);
    CHECK(model->nodes().size() == 3);
    REQUIRE(model->skins().size() == 1);
    const ofg::SkinTemplate& skin = model->skins()[0];
    CHECK(skin.m_source_skin_index == 0);
    REQUIRE(skin.m_joint_node_indices.size() == 2);
    CHECK(skin.m_joint_node_indices[0] == 1);
    CHECK(skin.m_joint_node_indices[1] == 2);
    CHECK_FALSE(skin.m_skeleton_root_node_index.has_value());
    REQUIRE(skin.m_inverse_bind_matrices.size() == 2);
    CHECK(skin.m_inverse_bind_matrices[0][0].x == doctest::Approx(1.0f));
    REQUIRE(model->animation_clip_count() == 1);
    ofg::AnimationClip* clip = model->animation_clip(0);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->channels().size() == 1);
    const ofg::AnimationChannel& channel = clip->channels()[0];
    CHECK(channel.m_target_node_index == 2);
    CHECK(channel.m_target_path == ofg::AnimationTargetPath::Rotation);
    REQUIRE(channel.m_input_times_seconds.size() > 1);
    REQUIRE(channel.m_output_values.size() == channel.m_input_times_seconds.size());

    REQUIRE(model->mesh_renderers().size() == 1);
    REQUIRE(model->mesh_renderers()[0].m_skin_template_index.has_value());
    CHECK(*model->mesh_renderers()[0].m_skin_template_index == 0);
    const ofg::Mesh* mesh = model->mesh_renderers()[0].m_mesh.get();
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->vertices().size() > 0);
    CHECK(
        ofg::math::length_squared(ofg::math::vec3(
            mesh->vertices()[0].m_normal[0], mesh->vertices()[0].m_normal[1], mesh->vertices()[0].m_normal[2])) > 0.9f);

    ofg::Scene scene;
    ofg::ModelInstance instance = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    REQUIRE(instance.m_mesh_renderers.size() == 1);
    const ofg::SkinBinding* binding = instance.m_mesh_renderers[0]->skin_binding();
    REQUIRE(binding != nullptr);
    CHECK(binding->m_source_skin_index == 0);
    CHECK(binding->m_skeleton_root == nullptr);
    REQUIRE(binding->m_joints_in_skin_order.size() == 2);
    CHECK(binding->m_joints_in_skin_order[0].get() == instance.m_entities_by_node_index[1].get());
    CHECK(binding->m_joints_in_skin_order[1].get() == instance.m_entities_by_node_index[2].get());
    CHECK(instance.m_entities_by_node_index[2]->parent() == instance.m_entities_by_node_index[1].get());
    REQUIRE(binding->m_inverse_bind_matrices.size() == 2);
    CHECK(binding->m_inverse_bind_matrices[0][0].x == doctest::Approx(skin.m_inverse_bind_matrices[0][0].x));
    REQUIRE(instance.m_animation_player != nullptr);

    const double sample_time = channel.m_input_times_seconds[1];
    const ofg::math::Vec4 expected_rotation = channel.m_output_values[1];
    instance.m_animation_player->play(*clip, false);
    instance.m_animation_player->set_time_seconds(sample_time);
    ofg::ControlInput controls;
    ofg::SceneUpdateContext update_context{controls, 1000.0, 0.0f, nullptr, nullptr};
    scene.update(update_context);
    const ofg::LocalTransform& animated_joint = instance.m_entities_by_node_index[2]->local_transform();
    CHECK(animated_joint.m_rotation.x == doctest::Approx(expected_rotation.x));
    CHECK(animated_joint.m_rotation.y == doctest::Approx(expected_rotation.y));
    CHECK(animated_joint.m_rotation.z == doctest::Approx(expected_rotation.z));
    CHECK(animated_joint.m_rotation.w == doctest::Approx(expected_rotation.w));

    instance.m_entities_by_node_index[2]->local_transform().m_position = ofg::math::vec3(9.0f, 8.0f, 7.0f);
    CHECK(instance.m_entities_by_node_index[2]->local_transform().m_position.x == doctest::Approx(9.0f));
    CHECK(instance.m_entities_by_node_index[2]->local_transform().m_position.y == doctest::Approx(8.0f));
    CHECK(instance.m_entities_by_node_index[2]->local_transform().m_position.z == doctest::Approx(7.0f));
}

// Verifies a glTF skin with skin.skeleton keeps the skeleton root as ordinary scene entity metadata.
TEST_CASE("glTF importer preserves explicit skin skeleton root on mesh renderer binding") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_fixture_model("rigged-simple", "rigged-simple.glb", loader);

    REQUIRE(model != nullptr);
    CHECK(model->nodes().size() == 5);
    REQUIRE(model->skins().size() == 1);
    const ofg::SkinTemplate& skin = model->skins()[0];
    CHECK(skin.m_name == "Armature");
    REQUIRE(skin.m_skeleton_root_node_index.has_value());
    CHECK(*skin.m_skeleton_root_node_index == 3);
    REQUIRE(skin.m_joint_node_indices.size() == 2);
    CHECK(skin.m_joint_node_indices[0] == 3);
    CHECK(skin.m_joint_node_indices[1] == 4);

    ofg::Scene scene;
    ofg::ModelInstance instance = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
    REQUIRE(instance.m_mesh_renderers.size() == 1);
    const ofg::SkinBinding* binding = instance.m_mesh_renderers[0]->skin_binding();
    REQUIRE(binding != nullptr);
    CHECK(binding->m_name == "Armature");
    CHECK(binding->m_skeleton_root.get() == instance.m_entities_by_node_index[3].get());
    REQUIRE(binding->m_joints_in_skin_order.size() == 2);
    CHECK(binding->m_joints_in_skin_order[0].get() == instance.m_entities_by_node_index[3].get());
    CHECK(binding->m_joints_in_skin_order[1].get() == instance.m_entities_by_node_index[4].get());
    CHECK(instance.m_entities_by_node_index[4]->parent() == instance.m_entities_by_node_index[3].get());
}

// Verifies imported model resources can be copied into a Scene many times.
TEST_CASE("ModelResource instantiates distinct entity trees that share mesh resources") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> model = import_static_box(loader);
    ofg::Scene scene;

    std::vector<ofg::ModelInstance> instances;
    for (int index = 0; index < 5; ++index) {
        ofg::ModelInstance instance = ofg::instantiate_model_resource(*model, scene, *scene.get_root());
        REQUIRE(instance.m_root_entity != nullptr);
        instance.m_root_entity->local_transform().m_position = ofg::math::vec3(static_cast<float>(index), 0.0f, 0.0f);
        instances.push_back(std::move(instance));
    }

    CHECK(scene.entity_count() == 16);
    CHECK(scene.mesh_renderer_count() == 5);
    REQUIRE(instances.size() == 5);

    ofg::Mesh* shared_mesh = instances[0].m_mesh_renderers[0]->mesh();
    REQUIRE(shared_mesh != nullptr);
    for (std::size_t index = 0; index < instances.size(); ++index) {
        const ofg::ModelInstance& instance = instances[index];
        REQUIRE(instance.m_entities_by_node_index.size() == model->nodes().size());
        REQUIRE(instance.m_mesh_renderers.size() == 1);
        CHECK(instance.m_mesh_renderers[0]->mesh() == shared_mesh);
        CHECK(instance.m_entities_by_node_index[1]->mesh_renderer() == instance.m_mesh_renderers[0].get());
        CHECK(instance.m_root_entity->local_transform().m_position.x == doctest::Approx(static_cast<float>(index)));
    }

    instances[0].m_root_entity->local_transform().m_scale = ofg::math::vec3(3.0f, 3.0f, 3.0f);
    CHECK(instances[0].m_root_entity->local_transform().m_scale.x == doctest::Approx(3.0f));
    CHECK(instances[1].m_root_entity->local_transform().m_scale.x == doctest::Approx(1.0f));

    scene.clear();
    CHECK_THROWS_WITH_AS(
        ([&]() { (void)instances[0].m_root_entity->id(); }()), doctest::Contains("Entity"), ofg::EngineError);
}

// Verifies resource cache keys keep duplicate imports from rebuilding meshes/materials.
TEST_CASE("glTF importer deduplicates resources by source URI and index") {
    ScopedResources resources;
    ofg::ModelResourceLoader loader;
    std::unique_ptr<ofg::ModelResource> first = import_static_box(loader);
    std::unique_ptr<ofg::ModelResource> second = import_static_box(loader);

    CHECK(loader.mesh_count() == 1);
    CHECK(loader.material_count() == 1);
    REQUIRE(first->mesh_renderers().size() == 1);
    REQUIRE(second->mesh_renderers().size() == 1);
    CHECK(first->mesh_renderers()[0].m_mesh.get() == second->mesh_renderers()[0].m_mesh.get());
}

// Verifies imported resources survive temporary loader destruction.
TEST_CASE("ModelResource instantiation survives temporary loader destruction") {
    ScopedResources resources;
    std::unique_ptr<ofg::ModelResource> model;
    {
        ofg::ModelResourceLoader loader;
        model = import_static_box(loader);
        REQUIRE(model->mesh_renderers().size() == 1);
        REQUIRE(model->mesh_renderers()[0].m_mesh != nullptr);
    }

    CHECK(model->mesh_renderers()[0].m_mesh != nullptr);
    ofg::Scene scene;
    CHECK_NOTHROW((void)ofg::instantiate_model_resource(*model, scene, *scene.get_root()));
}

// Verifies required unsupported extensions fail before partial model import.
TEST_CASE("glTF importer rejects required unsupported extensions") {
    ScopedResources resources;
    const ofg::GltfDocument document =
        ofg::load_gltf_document_from_path(asset_dir() / "material-specular-glossiness-13.glb");
    REQUIRE_FALSE(document.extensions_required().empty());

    ofg::ModelResourceLoader loader;
    CHECK_THROWS_WITH_AS(
        (void)ofg::import_gltf_model_resource(
            document, ofg::GltfImportOptions{"spec-gloss", "material-specular-glossiness-13.glb"}, loader),
        doctest::Contains("unsupported extensions"),
        ofg::EngineError);
}
