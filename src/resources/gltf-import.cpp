// Validates model-wide references and converts hierarchy, skin palettes and immutable animation clips.
#include "resources/gltf-import.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace ofg {
namespace {
// Converts one skin, preserving palette order and reflecting inverse bind matrices with C*M*C.
std::shared_ptr<Skin> importSkin(const tinygltf::Model& source, const tinygltf::Skin& skin, GltfBudget& budget)
{
    SkinDesc desc;
    if (skin.inverseBindMatrices < -1 || skin.skeleton < -1)
    {
        throw EngineError("Invalid optional skin reference.");
    }
    desc.name = skin.name;
    budget.add(desc.name.size(), 1);
    budget.add(skin.joints.size(), sizeof(SkinJoint));
    std::optional<GltfAccessor> matrices;
    if (skin.inverseBindMatrices >= 0)
    {
        matrices = readGltfAccessor(source, skin.inverseBindMatrices);
        if (matrices->type != TINYGLTF_TYPE_MAT4 || matrices->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
            matrices->count < skin.joints.size())
        {
            throw EngineError("Inverse bind matrices require at least one float MAT4 per joint.");
        }
    }
    if (skin.skeleton >= 0)
    {
        gltfAt(source.nodes, skin.skeleton, "skeleton root");
        desc.skeletonRoot = uint32_t(skin.skeleton);
    }
    for (size_t i = 0; i < skin.joints.size(); ++i)
    {
        gltfAt(source.nodes, skin.joints[i], "joint node");
        SkinJoint joint{uint32_t(skin.joints[i])};
        if (matrices)
        {
            for (size_t column = 0; column < 4; ++column)
            {
                for (size_t row = 0; row < 4; ++row)
                {
                    joint.inverseBindMatrix[column][row] =
                        float(matrices->values[i * 16 + column * 4 + row]) * ((column == 2) != (row == 2) ? -1.f : 1.f);
                }
            }
        }
        desc.joints.push_back(joint);
    }
    return Skin::create(std::move(desc));
}
// Converts a clip without resampling, preserving cubic tangents and model-local target indices.
std::shared_ptr<Animation> importAnimation(
    const tinygltf::Model& source,
    const tinygltf::Animation& animation,
    ModelData& result,
    GltfBudget& budget
)
{
    AnimationDesc desc;
    desc.name = animation.name;
    budget.add(desc.name.size(), 1);
    budget.add(animation.channels.size(), sizeof(AnimationTrack));
    for (const auto& channel : animation.channels)
    {
        const auto& node = gltfAt(source.nodes, channel.target_node, "animation node");
        const auto& sampler = gltfAt(animation.samplers, channel.sampler, "animation sampler");
        AnimationTrack track;
        track.node = uint32_t(channel.target_node);
        if (sampler.interpolation == "STEP")
        {
            track.interpolation = AnimationInterpolation::Step;
        }
        else if (sampler.interpolation == "CUBICSPLINE")
        {
            track.interpolation = AnimationInterpolation::CubicSpline;
        }
        else if (sampler.interpolation != "LINEAR")
        {
            throw EngineError("Unsupported animation interpolation.");
        }
        int expectedType = TINYGLTF_TYPE_VEC3;
        if (channel.target_path == "translation")
        {
            track.path = AnimationPath::Translation;
        }
        else if (channel.target_path == "scale")
        {
            track.path = AnimationPath::Scale;
        }
        else if (channel.target_path == "rotation")
        {
            track.path = AnimationPath::Rotation;
            track.componentsPerValue = 4;
            expectedType = TINYGLTF_TYPE_VEC4;
        }
        else if (channel.target_path == "weights")
        {
            track.path = AnimationPath::Weights;
            expectedType = TINYGLTF_TYPE_SCALAR;
            const auto& mesh = gltfAt(result.meshes, node.mesh, "animated morph mesh");
            track.componentsPerValue = uint32_t(mesh->deformation().morphTargets.size());
            if (!track.componentsPerValue)
            {
                throw EngineError("Weight animation requires morph targets.");
            }
        }
        else
        {
            throw EngineError("Unsupported animation target path.");
        }
        if (track.path != AnimationPath::Weights && !node.matrix.empty())
        {
            throw EngineError("TRS animation cannot target a matrix node.");
        }
        auto times = readGltfAccessor(source, sampler.input);
        auto values = readGltfAccessor(source, sampler.output);
        if (times.type != TINYGLTF_TYPE_SCALAR || times.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
            values.type != expectedType || values.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        {
            throw EngineError("Invalid animation input/output encoding.");
        }
        budget.add(times.values.size() + values.values.size(), sizeof(float));
        track.timesSeconds.reserve(times.values.size());
        track.values.reserve(values.values.size());
        for (double time : times.values)
        {
            track.timesSeconds.push_back(float(time));
        }
        for (double value : values.values)
        {
            track.values.push_back(float(value));
        }
        if (track.path == AnimationPath::Translation)
        {
            for (size_t i = 2; i < track.values.size(); i += 3)
            {
                track.values[i] = -track.values[i];
            }
        }
        else if (track.path == AnimationPath::Rotation)
        {
            for (size_t i = 0; i < track.values.size(); i += 4)
            {
                track.values[i] = -track.values[i];
                track.values[i + 1] = -track.values[i + 1];
            }
        }
        desc.tracks.push_back(std::move(track));
    }
    return Animation::create(std::move(desc));
}
// Returns whether ancestor includes node; the previously validated hierarchy is acyclic and shallow.
bool ancestorOf(uint32_t ancestor, uint32_t node, const std::vector<int>& parents)
{
    for (int current = int(node); current >= 0; current = parents[size_t(current)])
    {
        if (current == int(ancestor))
        {
            return true;
        }
    }
    return false;
}
// Validates skin hierarchy membership and vertex palette domains before any Model can be published.
void validateSkins(const ModelData& model, const std::vector<int>& parents)
{
    for (const auto& skin : model.skins)
    {
        const auto& desc = skin->desc();
        uint32_t commonRoot = desc.joints.front().node;
        while (parents[commonRoot] >= 0)
        {
            commonRoot = uint32_t(parents[commonRoot]);
        }
        for (const auto& joint : desc.joints)
        {
            if (!ancestorOf(desc.skeletonRoot.value_or(commonRoot), joint.node, parents))
            {
                throw EngineError("Skin joints must share the declared skeleton root or a common ancestor.");
            }
        }
    }
    for (const auto& node : model.nodes)
    {
        if (!node.skin)
        {
            continue;
        }
        if (!node.mesh)
        {
            throw EngineError("A skinned node requires a mesh.");
        }
        const auto& skin = model.skins[*node.skin]->desc();
        const auto& mesh = model.meshes[*node.mesh];
        const auto& sets = mesh->deformation().influenceSets;
        if (sets.empty())
        {
            throw EngineError("Skinned mesh lacks joint/weight attributes.");
        }
        for (size_t vertex = 0; vertex < mesh->vertices().size(); ++vertex)
        {
            float sum = 0;
            for (const auto& set : sets)
            {
                for (size_t c = 0; c < 4; ++c)
                {
                    if (set[vertex].joints[c] >= skin.joints.size())
                    {
                        throw EngineError("Vertex joint outside skin palette.");
                    }
                    sum += set[vertex].weights[c];
                }
            }
            if (sum <= 0)
            {
                throw EngineError("Skinned vertex has no joint weight.");
            }
        }
    }
    for (const auto& scene : model.scenes)
    {
        std::vector<bool> member(model.nodes.size());
        std::vector<uint32_t> pending = scene.roots;
        while (!pending.empty())
        {
            const uint32_t index = pending.back();
            pending.pop_back();
            member[index] = true;
            for (uint32_t child : model.nodes[index].children)
            {
                pending.push_back(child);
            }
        }
        for (size_t index = 0; index < model.nodes.size(); ++index)
        {
            if (!member[index] || !model.nodes[index].skin)
            {
                continue;
            }
            const auto& skin = model.skins[*model.nodes[index].skin]->desc();
            for (const auto& joint : skin.joints)
            {
                if (!member[joint.node])
                {
                    throw EngineError("Skin joint is outside a scene containing its mesh.");
                }
            }
            if (skin.skeletonRoot && !member[*skin.skeletonRoot])
            {
                throw EngineError("Skeleton root is outside the mesh scene.");
            }
        }
    }
}
} // namespace

ModelData importGltf(const tinygltf::Model& source, std::vector<std::string>& warnings)
{
    ModelData result;
    GltfBudget budget;
    budget.add(source.nodes.size(), sizeof(ModelNode));
    budget.add(source.scenes.size(), sizeof(ModelScene));
    importGltfMaterials(source, result, budget);
    importGltfMeshes(source, result, budget);
    result.nodes.resize(source.nodes.size());
    std::vector<int> parents(source.nodes.size(), -1);
    for (size_t i = 0; i < source.nodes.size(); ++i)
    {
        const auto& input = source.nodes[i];
        if (input.mesh < -1 || input.skin < -1 || input.camera < -1)
        {
            throw EngineError("Invalid optional node reference.");
        }
        if (input.camera >= 0)
        {
            gltfAt(source.cameras, input.camera, "node camera");
        }
        auto& output = result.nodes[i];
        output.name = input.name;
        budget.add(output.name.size(), 1);
        output.localTransform = importGltfTransform(input);
        if (input.mesh >= 0)
        {
            gltfAt(result.meshes, input.mesh, "node mesh");
            output.mesh = uint32_t(input.mesh);
        }
        if (input.skin >= 0)
        {
            gltfAt(source.skins, input.skin, "node skin");
            output.skin = uint32_t(input.skin);
        }
        budget.add(input.children.size(), sizeof(uint32_t));
        for (int child : input.children)
        {
            gltfAt(source.nodes, child, "child node");
            if (parents[size_t(child)] != -1)
            {
                throw EngineError("Node has duplicate or multiple parents.");
            }
            parents[size_t(child)] = int(i);
            output.children.push_back(uint32_t(child));
        }
        if (!input.weights.empty())
        {
            if (!output.mesh || input.weights.size() != result.meshes[*output.mesh]->deformation().morphTargets.size())
            {
                throw EngineError("Node morph override does not match mesh targets.");
            }
            budget.add(input.weights.size(), sizeof(float));
            output.morphWeights.emplace();
            output.morphWeights->reserve(input.weights.size());
            for (double weight : input.weights)
            {
                output.morphWeights->push_back(float(weight));
            }
            for (float weight : *output.morphWeights)
            {
                if (!std::isfinite(weight))
                {
                    throw EngineError("Non-finite node morph weight.");
                }
            }
        }
        if (input.camera >= 0 || input.extensions.contains("KHR_lights_punctual"))
        {
            warnings.push_back("Node " + std::to_string(i) + ": camera/light component omitted; transform retained.");
        }
    }
    for (size_t i = 0; i < parents.size(); ++i)
    {
        size_t depth = 0;
        for (int parent = int(i); parent >= 0; parent = parents[size_t(parent)])
        {
            if (++depth > 256)
            {
                throw EngineError("Node hierarchy is cyclic or exceeds the supported depth of 256.");
            }
        }
    }
    for (const auto& scene : source.scenes)
    {
        ModelScene output;
        output.name = scene.name;
        budget.add(output.name.size(), 1);
        std::set<int> roots;
        budget.add(scene.nodes.size(), sizeof(uint32_t));
        for (int root : scene.nodes)
        {
            gltfAt(source.nodes, root, "scene root");
            if (parents[size_t(root)] >= 0 || !roots.insert(root).second)
            {
                throw EngineError("Scene roots must be distinct parentless nodes.");
            }
            output.roots.push_back(uint32_t(root));
        }
        result.scenes.push_back(std::move(output));
    }
    if (source.defaultScene >= 0)
    {
        gltfAt(result.scenes, source.defaultScene, "default scene");
        result.defaultScene = uint32_t(source.defaultScene);
    }
    for (const auto& skin : source.skins)
    {
        result.skins.push_back(importSkin(source, skin, budget));
    }
    validateSkins(result, parents);
    for (const auto& animation : source.animations)
    {
        result.animations.push_back(importAnimation(source, animation, result, budget));
    }
    return result;
}
} // namespace ofg
