// Reusable imported scene descriptions and shared resources; contains no live instance entities.
#pragma once
#include "resources/mesh.h"
#include "resources/skin.h"
#include "resources/animation.h"
#include "scene/entity.h"
#include <optional>

namespace ofg {
struct ModelNode
{
    std::string name;
    LocalTransform localTransform;
    std::vector<uint32_t> children;
    std::optional<uint32_t> mesh;
    std::optional<uint32_t> skin;
    std::optional<std::vector<float>> morphWeights;
};
struct ModelScene
{
    std::string name;
    std::vector<uint32_t> roots;
};
struct ModelData
{
    std::vector<ModelNode> nodes;
    std::vector<ModelScene> scenes;
    std::optional<uint32_t> defaultScene;
    std::vector<std::shared_ptr<Mesh>> meshes;
    std::vector<std::shared_ptr<Material>> materials;
    std::vector<std::shared_ptr<Skin>> skins;
    std::vector<std::shared_ptr<Animation>> animations;
    std::vector<std::shared_ptr<Texture>> textures;
    std::vector<std::shared_ptr<Sampler>> samplers;
};
struct ModelLoad;
class Model : public Resource
{
public:
    // Establishes identity for a glTF/GLB file; I/O starts through Resources::update().
    explicit Model(std::string key);
    // Cancels outstanding reads and releases unpublished import state.
    ~Model() override;
    // Returns the complete CPU description; throws before successful loading.
    const ModelData& data() const;
    // Returns parser/import warnings; no unsupported feature is silently substituted.
    std::span<const std::string> warnings() const noexcept { return m_warnings; }

private:
    // Advances document/dependency reads then CPU conversion, publishing only on complete success.
    bool loadStep() override;
    ModelData m_data;
    std::vector<std::string> m_warnings;
    std::unique_ptr<ModelLoad> m_load;
};
} // namespace ofg
