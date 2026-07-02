// Reusable imported model template data.
//
// ModelResource is the format-neutral layer between source asset parsing and a
// live Scene. It stores a scene-shaped node graph plus component templates that
// can be instantiated many times while sharing durable mesh/material resources.
#pragma once

#include "ofg/animation/animation_clip.hpp"
#include "ofg/animation/skinning.hpp"
#include "ofg/core/object.hpp"
#include "ofg/core/ptr.hpp"
#include "ofg/render/draw_list.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/scene/entity.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ofg {

class MeshRenderer;
class AnimationPlayer;

struct ModelNodeTemplate {
    std::string m_name;
    std::uint32_t m_source_node_index{0};
    std::int32_t m_parent_node_index{-1};
    std::vector<std::uint32_t> m_child_node_indices;
    LocalTransform m_local_transform;
};

struct MeshRendererTemplate {
    std::uint32_t m_node_index{0};
    Ptr<Mesh> m_mesh;
    std::vector<MaterialOverride> m_material_overrides;
    std::optional<std::uint32_t> m_skin_template_index;
    std::vector<SkinVertexInfluence> m_skin_vertex_influences;
};

struct SkinTemplate {
    std::string m_name;
    std::uint32_t m_source_skin_index{0};
    std::vector<std::uint32_t> m_joint_node_indices;
    std::vector<math::Mat4> m_inverse_bind_matrices;
    std::optional<std::uint32_t> m_skeleton_root_node_index;
};

class ModelResource : public Object {
public:
    ModelResource() = default;
    ModelResource(const ModelResource&) = delete;
    ModelResource& operator=(const ModelResource&) = delete;
    ModelResource(ModelResource&&) = delete;
    ModelResource& operator=(ModelResource&&) = delete;
    ~ModelResource() override = default;

    // Returns the model label used for diagnostics and instance roots.
    [[nodiscard]] const std::string& label() const noexcept;
    // Returns source node indices that are roots of this model graph.
    [[nodiscard]] std::span<const std::uint32_t> root_node_indices() const noexcept;
    // Returns node templates in source-node-index order.
    [[nodiscard]] std::span<const ModelNodeTemplate> nodes() const noexcept;
    // Returns mesh-renderer templates to apply after node instantiation.
    [[nodiscard]] std::span<const MeshRendererTemplate> mesh_renderers() const noexcept;
    // Returns skin templates that mesh-renderer templates can reference.
    [[nodiscard]] std::span<const SkinTemplate> skins() const noexcept;
    // Reports the number of imported animation clips.
    [[nodiscard]] std::size_t animation_clip_count() const noexcept;
    // Returns one imported animation clip by index.
    [[nodiscard]] AnimationClip* animation_clip(std::size_t index) noexcept;
    // Returns one imported animation clip by index.
    [[nodiscard]] const AnimationClip* animation_clip(std::size_t index) const noexcept;

private:
    friend class ModelResourceBuilder;

    std::string m_label;
    std::vector<std::uint32_t> m_root_node_indices;
    std::vector<ModelNodeTemplate> m_nodes;
    std::vector<MeshRendererTemplate> m_mesh_renderers;
    std::vector<SkinTemplate> m_skins;
    std::vector<std::unique_ptr<AnimationClip>> m_animation_clips;
};

struct ModelInstance {
    Ptr<Entity> m_root_entity;
    Ptr<AnimationPlayer> m_animation_player;
    std::vector<Ptr<Entity>> m_entities_by_node_index;
    std::vector<Ptr<MeshRenderer>> m_mesh_renderers;
};

class ModelResourceBuilder {
public:
    // Creates a builder for one model label.
    explicit ModelResourceBuilder(std::string label);
    // Appends one root node index.
    void add_root_node_index(std::uint32_t node_index);
    // Appends one node template.
    void add_node(ModelNodeTemplate node);
    // Appends one mesh-renderer template.
    void add_mesh_renderer(MeshRendererTemplate mesh_renderer);
    // Appends one skin template.
    void add_skin(SkinTemplate skin);
    // Appends one imported animation clip.
    void add_animation_clip(std::unique_ptr<AnimationClip> clip);
    // Builds the final non-movable model resource.
    [[nodiscard]] std::unique_ptr<ModelResource> build();

private:
    std::unique_ptr<ModelResource> m_resource;
};

// Instantiates a model resource under a scene parent.
[[nodiscard]] ModelInstance instantiate_model_resource(const ModelResource& resource, Scene& scene, Entity& parent);

} // namespace ofg
