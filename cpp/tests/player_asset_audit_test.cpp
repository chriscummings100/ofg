// Native tests that lock down the selected Quaternius player asset audit.
//
// These checks are intentionally metadata-oriented: they prove the target player
// GLBs are parseable by the C++ importer boundary, name the locomotion clips we
// plan to bind, and make material/skinning requirements explicit before runtime
// browser loading is added.
#include "doctest.h"

#include "ofg/assets/gltf_document.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::int32_t _gltf_mode_triangles = 4;
constexpr std::int32_t _component_float = 5126;
constexpr std::int32_t _type_vec3 = 3;
constexpr std::size_t _one_mib = 1024U * 1024U;

struct PositionBounds {
    float m_min_z{std::numeric_limits<float>::infinity()};
    float m_max_z{-std::numeric_limits<float>::infinity()};
    std::size_t m_count{0};
};

// Returns the repository player asset directory supplied by CMake.
std::filesystem::path player_asset_dir() {
    return std::filesystem::path{OFG_PLAYER_ASSET_DIR};
}

// Loads a player GLB by filename from the player asset directory.
ofg::GltfDocument load_player_asset(std::string_view filename) {
    return ofg::load_gltf_document_from_path(player_asset_dir() / std::filesystem::path{std::string(filename)});
}

// Returns the source file size for memory estimates.
std::uintmax_t player_asset_size(std::string_view filename) {
    return std::filesystem::file_size(player_asset_dir() / std::filesystem::path{std::string(filename)});
}

// Returns whether a primitive contains a named attribute semantic.
bool primitive_has_attribute(const ofg::GltfPrimitive& primitive, std::string_view semantic) {
    return std::ranges::any_of(primitive.m_attributes,
        [semantic](const ofg::GltfAttribute& attribute) { return attribute.m_semantic == semantic; });
}

// Returns one named node from the audited document, or fails the active doctest.
const ofg::GltfNode& require_node(const ofg::GltfDocument& document, std::string_view node_name) {
    const auto found = std::ranges::find_if(
        document.nodes(), [node_name](const ofg::GltfNode& node) { return node.m_name == node_name; });
    REQUIRE_MESSAGE(found != document.nodes().end(), "Missing audited player node " << node_name);
    return *found;
}

// Returns the first attribute with the requested semantic, or null when absent.
const ofg::GltfAttribute* find_attribute(const ofg::GltfPrimitive& primitive, std::string_view semantic) noexcept {
    const auto found = std::ranges::find_if(primitive.m_attributes,
        [semantic](const ofg::GltfAttribute& attribute) { return attribute.m_semantic == semantic; });
    if (found == primitive.m_attributes.end()) {
        return nullptr;
    }
    return &*found;
}

// Reads one little-endian float component from a glTF accessor data view.
float read_float_component(
    const ofg::GltfAccessorDataView& view, std::size_t element_index, std::size_t component_index) {
    float value = 0.0f;
    const std::byte* source = view.m_data.data() + element_index * view.m_stride + component_index * sizeof(float);
    std::memcpy(&value, source, sizeof(value));
    return value;
}

// Computes local Z bounds for all POSITION vertices in the mesh referenced by a named node.
PositionBounds node_mesh_position_z_bounds(const ofg::GltfDocument& document, std::string_view node_name) {
    PositionBounds bounds;
    const ofg::GltfNode& node = require_node(document, node_name);
    REQUIRE_MESSAGE(node.m_mesh_index >= 0, "Audited player node has no mesh " << node_name);
    const auto mesh_index = static_cast<std::size_t>(node.m_mesh_index);
    REQUIRE_MESSAGE(mesh_index < document.meshes().size(), "Audited player node has an invalid mesh " << node_name);
    const ofg::GltfMesh& mesh = document.meshes()[mesh_index];

    for (const ofg::GltfPrimitive& primitive : mesh.m_primitives) {
        const ofg::GltfAttribute* position_attribute = find_attribute(primitive, "POSITION");
        if (position_attribute == nullptr) {
            continue;
        }
        REQUIRE(position_attribute->m_accessor_index >= 0);
        const auto accessor_index = static_cast<std::size_t>(position_attribute->m_accessor_index);
        REQUIRE(accessor_index < document.accessors().size());
        const ofg::GltfAccessor& accessor = document.accessors()[accessor_index];
        REQUIRE(accessor.m_component_type == _component_float);
        REQUIRE(accessor.m_type == _type_vec3);

        const ofg::GltfAccessorDataView view = document.accessor_data(accessor_index);
        for (std::size_t vertex_index = 0; vertex_index < accessor.m_count; ++vertex_index) {
            const float z = read_float_component(view, vertex_index, 2U);
            bounds.m_min_z = std::min(bounds.m_min_z, z);
            bounds.m_max_z = std::max(bounds.m_max_z, z);
            ++bounds.m_count;
        }
    }

    REQUIRE(bounds.m_count > 0U);
    return bounds;
}

// Counts how many mesh primitives contain a named attribute semantic.
std::size_t count_primitives_with_attribute(const ofg::GltfDocument& document, std::string_view semantic) {
    std::size_t count = 0;
    for (const ofg::GltfMesh& mesh : document.meshes()) {
        for (const ofg::GltfPrimitive& primitive : mesh.m_primitives) {
            if (primitive_has_attribute(primitive, semantic)) {
                ++count;
            }
        }
    }
    return count;
}

// Counts all primitives using a glTF primitive mode.
std::size_t count_primitives_with_mode(const ofg::GltfDocument& document, std::int32_t mode) {
    std::size_t count = 0;
    for (const ofg::GltfMesh& mesh : document.meshes()) {
        for (const ofg::GltfPrimitive& primitive : mesh.m_primitives) {
            if (primitive.m_mode == mode) {
                ++count;
            }
        }
    }
    return count;
}

// Counts material texture references through one selected GltfMaterial field.
std::size_t count_material_textures(
    const ofg::GltfDocument& document, std::int32_t ofg::GltfMaterial::* texture_member) {
    std::size_t count = 0;
    for (const ofg::GltfMaterial& material : document.materials()) {
        if (material.*texture_member >= 0) {
            ++count;
        }
    }
    return count;
}

// Sums POSITION accessor counts as an approximate vertex count.
std::size_t count_vertices(const ofg::GltfDocument& document) {
    std::size_t count = 0;
    for (const ofg::GltfMesh& mesh : document.meshes()) {
        for (const ofg::GltfPrimitive& primitive : mesh.m_primitives) {
            const auto found = std::ranges::find_if(primitive.m_attributes,
                [](const ofg::GltfAttribute& attribute) { return attribute.m_semantic == "POSITION"; });
            if (found != primitive.m_attributes.end() && found->m_accessor_index >= 0) {
                const auto accessor_index = static_cast<std::size_t>(found->m_accessor_index);
                if (accessor_index < document.accessors().size()) {
                    count += document.accessors()[accessor_index].m_count;
                }
            }
        }
    }
    return count;
}

// Sums primitive index accessor counts.
std::size_t count_indices(const ofg::GltfDocument& document) {
    std::size_t count = 0;
    for (const ofg::GltfMesh& mesh : document.meshes()) {
        for (const ofg::GltfPrimitive& primitive : mesh.m_primitives) {
            if (primitive.m_indices_accessor_index >= 0) {
                const auto accessor_index = static_cast<std::size_t>(primitive.m_indices_accessor_index);
                if (accessor_index < document.accessors().size()) {
                    count += document.accessors()[accessor_index].m_count;
                }
            }
        }
    }
    return count;
}

// Sums decoded image byte storage exposed by tinygltf.
std::size_t decoded_image_bytes(const ofg::GltfDocument& document) {
    std::size_t total = 0;
    for (const ofg::GltfImage& image : document.images()) {
        total += image.m_bytes.size();
    }
    return total;
}

// Sums decoded buffer byte storage exposed by tinygltf.
std::size_t decoded_buffer_bytes(const ofg::GltfDocument& document) {
    std::size_t total = 0;
    for (const ofg::GltfBuffer& buffer : document.buffers()) {
        total += buffer.m_bytes.size();
    }
    return total;
}

// Extracts joint names from the first skin in glTF joint order.
std::vector<std::string> first_skin_joint_names(const ofg::GltfDocument& document) {
    std::vector<std::string> names;
    if (document.skins().empty()) {
        return names;
    }
    names.reserve(document.skins()[0].m_joint_node_indices.size());
    for (const std::int32_t node_index : document.skins()[0].m_joint_node_indices) {
        if (node_index < 0 || static_cast<std::size_t>(node_index) >= document.nodes().size()) {
            names.emplace_back("<invalid>");
        } else {
            names.push_back(document.nodes()[static_cast<std::size_t>(node_index)].m_name);
        }
    }
    return names;
}

// Returns whether an animation name exists in a document.
bool has_animation(const ofg::GltfDocument& document, std::string_view name) {
    return std::ranges::any_of(
        document.animations(), [name](const ofg::GltfAnimation& animation) { return animation.m_name == name; });
}

// Counts animation channels that target one glTF target path.
std::size_t count_animation_channels_with_path(const ofg::GltfDocument& document, std::string_view path) {
    std::size_t count = 0;
    for (const ofg::GltfAnimation& animation : document.animations()) {
        for (const ofg::GltfAnimationChannel& channel : animation.m_channels) {
            if (channel.m_target_path == path) {
                ++count;
            }
        }
    }
    return count;
}

// Returns whether every animation sampler uses the expected interpolation mode.
bool all_animation_samplers_use(const ofg::GltfDocument& document, std::string_view interpolation) {
    for (const ofg::GltfAnimation& animation : document.animations()) {
        for (const ofg::GltfAnimationSampler& sampler : animation.m_samplers) {
            if (sampler.m_interpolation != interpolation) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

// Audits the selected player mesh asset before browser/runtime integration.
TEST_CASE("Quaternius superhero male GLB has the expected skinned PBR mesh metadata") {
    const ofg::GltfDocument player = load_player_asset("quaternius-superhero-male.glb");

    CHECK(player.extensions_required().empty());
    CHECK(player.extensions_used().empty());
    CHECK(player.node_count() == 69);
    CHECK(player.mesh_count() == 3);
    CHECK(player.material_count() == 3);
    CHECK(player.skin_count() == 1);
    CHECK(player.animation_count() == 0);
    CHECK(player.images().size() == 7);
    CHECK(player.textures().size() == 7);

    CHECK(count_primitives_with_mode(player, _gltf_mode_triangles) == 3);
    CHECK(count_primitives_with_attribute(player, "POSITION") == 3);
    CHECK(count_primitives_with_attribute(player, "NORMAL") == 3);
    CHECK(count_primitives_with_attribute(player, "TEXCOORD_0") == 3);
    CHECK(count_primitives_with_attribute(player, "JOINTS_0") == 3);
    CHECK(count_primitives_with_attribute(player, "WEIGHTS_0") == 3);
    CHECK(count_primitives_with_attribute(player, "TANGENT") == 0);

    CHECK(count_material_textures(player, &ofg::GltfMaterial::m_base_color_texture_index) == 3);
    CHECK(count_material_textures(player, &ofg::GltfMaterial::m_normal_texture_index) == 3);
    CHECK(count_material_textures(player, &ofg::GltfMaterial::m_metallic_roughness_texture_index) == 1);

    CHECK(count_vertices(player) == 8483);
    CHECK(count_indices(player) == 42954);
    CHECK(decoded_image_bytes(player) >= 80U * _one_mib);
    CHECK(first_skin_joint_names(player).size() == 65);
    CHECK(player.skins()[0].m_skeleton_node_index == -1);
}

// Documents why the player instance root should not need a facing correction in OFG.
TEST_CASE("Quaternius superhero male visual face is authored on positive glTF Z") {
    const ofg::GltfDocument player = load_player_asset("quaternius-superhero-male.glb");

    const PositionBounds eye_bounds = node_mesh_position_z_bounds(player, "Eyes");
    const PositionBounds brow_bounds = node_mesh_position_z_bounds(player, "Eyebrows");
    CHECK(eye_bounds.m_min_z > 0.04f);
    CHECK(brow_bounds.m_min_z > 0.04f);

    const ofg::GltfNode& armature = require_node(player, "Armature");
    const ofg::GltfNode& eye_node = require_node(player, "Eyes");
    const ofg::GltfNode& brow_node = require_node(player, "Eyebrows");
    const ofg::GltfNode& body_node = require_node(player, "SuperHero_Male");
    CHECK_FALSE(armature.m_has_rotation);
    CHECK_FALSE(eye_node.m_has_rotation);
    CHECK_FALSE(brow_node.m_has_rotation);
    CHECK_FALSE(body_node.m_has_rotation);
}

// Audits the animation-library choice and skeleton compatibility for locomotion.
TEST_CASE("Quaternius UAL1 animation library is compatible with the selected player skeleton") {
    const ofg::GltfDocument player = load_player_asset("quaternius-superhero-male.glb");
    const ofg::GltfDocument ual1 = load_player_asset("quaternius-ual1-standard.glb");
    const ofg::GltfDocument ual2 = load_player_asset("quaternius-ual2-standard.glb");

    CHECK(ual1.extensions_required().empty());
    CHECK(ual1.extensions_used().empty());
    CHECK(ual1.images().empty());
    CHECK(ual1.textures().empty());
    CHECK(ual1.skin_count() == 1);
    CHECK(ual1.animation_count() == 45);
    CHECK(ual2.animation_count() == 43);

    CHECK(first_skin_joint_names(ual1) == first_skin_joint_names(player));
    CHECK(first_skin_joint_names(ual2) == first_skin_joint_names(player));

    CHECK(has_animation(ual1, "Idle_Loop"));
    CHECK(has_animation(ual1, "Walk_Loop"));
    CHECK(has_animation(ual1, "Jog_Fwd_Loop"));
    CHECK(has_animation(ual1, "Sprint_Loop"));
    CHECK_FALSE(has_animation(ual2, "Sprint_Loop"));

    CHECK(all_animation_samplers_use(ual1, "LINEAR"));
    CHECK(count_animation_channels_with_path(ual1, "translation") == 2925);
    CHECK(count_animation_channels_with_path(ual1, "rotation") == 2925);
    CHECK(count_animation_channels_with_path(ual1, "scale") == 2925);

    const std::uintmax_t selected_source_bytes =
        player_asset_size("quaternius-superhero-male.glb") + player_asset_size("quaternius-ual1-standard.glb");
    const std::size_t selected_decoded_bytes = decoded_buffer_bytes(player) + decoded_image_bytes(player) +
                                               decoded_buffer_bytes(ual1) + decoded_image_bytes(ual1);
    CHECK(selected_source_bytes + selected_decoded_bytes > 120U * _one_mib);
}
