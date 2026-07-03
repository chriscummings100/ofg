// Model resource template and scene instantiation implementation.
#include "ofg/assets/model_resource.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/scene/animation_player.hpp"
#include "ofg/scene/mesh_renderer.hpp"
#include "ofg/scene/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

// Recursively instantiates one source node and all of its children.
void instantiate_node_recursive(const ModelResource& resource,
    Scene& scene,
    Entity& parent,
    std::uint32_t node_index,
    std::vector<Ptr<Entity>>& entities_by_node_index) {
    if (node_index >= resource.nodes().size()) {
        throw EngineError("ModelResource contains a root or child node index outside its node table.");
    }
    if (entities_by_node_index[node_index] != nullptr) {
        throw EngineError("ModelResource node graph references the same node more than once.");
    }

    const ModelNodeTemplate& node = resource.nodes()[node_index];
    Entity* entity = scene.create_entity(&parent);
    entity->local_transform() = node.m_local_transform;
    entities_by_node_index[node_index] = entity;

    for (std::uint32_t child_index : node.m_child_node_indices) {
        instantiate_node_recursive(resource, scene, *entity, child_index, entities_by_node_index);
    }
}

// Resolves one model skin template into instance-owned mesh-renderer metadata.
SkinBinding instantiate_skin_binding(
    const SkinTemplate& skin_template, const std::vector<Ptr<Entity>>& entities_by_node_index) {
    SkinBinding binding;
    binding.m_name = skin_template.m_name;
    binding.m_source_skin_index = skin_template.m_source_skin_index;
    binding.m_inverse_bind_matrices = skin_template.m_inverse_bind_matrices;
    binding.m_joints_in_skin_order.reserve(skin_template.m_joint_node_indices.size());
    for (const std::uint32_t joint_node_index : skin_template.m_joint_node_indices) {
        if (joint_node_index >= entities_by_node_index.size()) {
            throw EngineError("ModelResource skin references a joint node outside the node table.");
        }
        Entity* joint_entity = entities_by_node_index[joint_node_index].get();
        if (joint_entity == nullptr) {
            throw EngineError("ModelResource skin references a joint node that was not instantiated.");
        }
        binding.m_joints_in_skin_order.push_back(joint_entity);
    }
    if (skin_template.m_skeleton_root_node_index.has_value()) {
        const std::uint32_t skeleton_node_index = *skin_template.m_skeleton_root_node_index;
        if (skeleton_node_index >= entities_by_node_index.size()) {
            throw EngineError("ModelResource skin references a skeleton root outside the node table.");
        }
        Entity* skeleton_root = entities_by_node_index[skeleton_node_index].get();
        if (skeleton_root == nullptr) {
            throw EngineError("ModelResource skin references a skeleton root that was not instantiated.");
        }
        binding.m_skeleton_root = skeleton_root;
    }
    return binding;
}

} // namespace

// Returns the model label used for diagnostics and instance roots.
const std::string& ModelResource::label() const noexcept {
    return m_label;
}

// Returns source node indices that are roots of this model graph.
std::span<const std::uint32_t> ModelResource::root_node_indices() const noexcept {
    return m_root_node_indices;
}

// Returns node templates in source-node-index order.
std::span<const ModelNodeTemplate> ModelResource::nodes() const noexcept {
    return m_nodes;
}

// Returns mesh-renderer templates to apply after node instantiation.
std::span<const MeshRendererTemplate> ModelResource::mesh_renderers() const noexcept {
    return m_mesh_renderers;
}

// Returns skin templates that mesh-renderer templates can reference.
std::span<const SkinTemplate> ModelResource::skins() const noexcept {
    return m_skins;
}

// Reports the number of imported animation clips.
std::size_t ModelResource::animation_clip_count() const noexcept {
    return m_animation_clips.size();
}

// Returns one imported animation clip by index.
AnimationClip* ModelResource::animation_clip(std::size_t index) noexcept {
    if (index >= m_animation_clips.size()) {
        return nullptr;
    }
    return m_animation_clips[index].get();
}

// Returns one imported animation clip by index.
const AnimationClip* ModelResource::animation_clip(std::size_t index) const noexcept {
    if (index >= m_animation_clips.size()) {
        return nullptr;
    }
    return m_animation_clips[index].get();
}

// Creates a builder for one model label.
ModelResourceBuilder::ModelResourceBuilder(std::string label) : m_resource(std::make_unique<ModelResource>()) {
    if (label.empty()) {
        throw EngineError("ModelResource label must not be empty.");
    }
    m_resource->m_label = std::move(label);
}

// Appends one root node index.
void ModelResourceBuilder::add_root_node_index(std::uint32_t node_index) {
    m_resource->m_root_node_indices.push_back(node_index);
}

// Appends one node template.
void ModelResourceBuilder::add_node(ModelNodeTemplate node) {
    m_resource->m_nodes.push_back(std::move(node));
}

// Appends one mesh-renderer template.
void ModelResourceBuilder::add_mesh_renderer(MeshRendererTemplate mesh_renderer) {
    m_resource->m_mesh_renderers.push_back(std::move(mesh_renderer));
}

// Appends one skin template.
void ModelResourceBuilder::add_skin(SkinTemplate skin) {
    m_resource->m_skins.push_back(std::move(skin));
}

// Appends one imported animation clip.
void ModelResourceBuilder::add_animation_clip(std::unique_ptr<AnimationClip> clip) {
    if (clip == nullptr) {
        throw EngineError("ModelResource cannot store a null animation clip.");
    }
    m_resource->m_animation_clips.push_back(std::move(clip));
}

// Builds the final non-movable model resource.
std::unique_ptr<ModelResource> ModelResourceBuilder::build() {
    validate_resource();
    return std::move(m_resource);
}

// Moves the built content into an existing stable model resource.
void ModelResourceBuilder::build_into(ModelResource& resource) {
    validate_resource();
    resource.m_label = std::move(m_resource->m_label);
    resource.m_root_node_indices = std::move(m_resource->m_root_node_indices);
    resource.m_nodes = std::move(m_resource->m_nodes);
    resource.m_mesh_renderers = std::move(m_resource->m_mesh_renderers);
    resource.m_skins = std::move(m_resource->m_skins);
    resource.m_animation_clips = std::move(m_resource->m_animation_clips);
    m_resource.reset();
}

// Validates the pending resource before build or build_into returns.
void ModelResourceBuilder::validate_resource() const {
    if (m_resource == nullptr) {
        throw EngineError("ModelResourceBuilder has already built its resource.");
    }
    if (m_resource->m_nodes.empty()) {
        throw EngineError("ModelResource requires at least one node.");
    }
    if (m_resource->m_root_node_indices.empty()) {
        throw EngineError("ModelResource requires at least one root node.");
    }
}

// Instantiates a model resource under a scene parent.
ModelInstance instantiate_model_resource(const ModelResource& resource, Scene& scene, Entity& parent) {
    if (resource.nodes().empty()) {
        throw EngineError("Cannot instantiate an empty ModelResource.");
    }

    ModelInstance instance;
    Entity* instance_root = scene.create_entity(&parent);
    instance.m_root_entity = instance_root;
    instance.m_entities_by_node_index.resize(resource.nodes().size());

    for (std::uint32_t root_node_index : resource.root_node_indices()) {
        instantiate_node_recursive(resource, scene, *instance_root, root_node_index, instance.m_entities_by_node_index);
    }

    for (const MeshRendererTemplate& mesh_renderer_template : resource.mesh_renderers()) {
        if (mesh_renderer_template.m_node_index >= instance.m_entities_by_node_index.size()) {
            throw EngineError("ModelResource mesh renderer references a missing node.");
        }
        Entity* entity = instance.m_entities_by_node_index[mesh_renderer_template.m_node_index].get();
        if (entity == nullptr) {
            throw EngineError("ModelResource mesh renderer references a node that was not instantiated.");
        }
        if (mesh_renderer_template.m_mesh == nullptr) {
            throw EngineError("ModelResource mesh renderer references a destroyed mesh resource.");
        }
        auto* mesh_renderer = static_cast<MeshRenderer*>(entity->create_component(ComponentType::MeshRenderer));
        mesh_renderer->set_mesh(mesh_renderer_template.m_mesh.get());
        mesh_renderer->set_material_overrides(mesh_renderer_template.m_material_overrides);
        if (mesh_renderer_template.m_skin_template_index.has_value()) {
            const std::uint32_t skin_index = *mesh_renderer_template.m_skin_template_index;
            if (skin_index >= resource.skins().size()) {
                throw EngineError("ModelResource mesh renderer references a missing skin template.");
            }
            SkinBinding binding =
                instantiate_skin_binding(resource.skins()[skin_index], instance.m_entities_by_node_index);
            binding.m_vertex_influences = mesh_renderer_template.m_skin_vertex_influences;
            mesh_renderer->set_skin_binding(std::move(binding));
        }
        instance.m_mesh_renderers.push_back(mesh_renderer);
    }

    if (resource.animation_clip_count() > 0U) {
        auto* animation_player =
            static_cast<AnimationPlayer*>(instance_root->create_component(ComponentType::AnimationPlayer));
        animation_player->bind_targets(instance.m_entities_by_node_index);
        instance.m_animation_player = animation_player;
    }

    return instance;
}

} // namespace ofg
