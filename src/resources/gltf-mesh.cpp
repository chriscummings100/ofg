// Converts checked glTF primitive streams into shared triangle meshes and dense deformation arrays.
#include "resources/gltf-import.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>

namespace ofg {
namespace {
// Reads a vertex attribute with the shape and component encodings permitted by its semantic.
GltfAccessor attribute(const tinygltf::Model& source, int index, size_t count, int type, bool integerAllowed = false)
{
    auto data = readGltfAccessor(source, index);
    const bool floating = data.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && !data.normalized;
    const bool normalized = data.normalized && (data.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                                                data.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    if (data.count != count || data.type != type || !(floating || (integerAllowed && normalized)))
    {
        throw EngineError("Vertex attribute has an incompatible count, shape or encoding.");
    }
    return data;
}
// Converts a dense VEC3 element, reflecting the Z axis.
math::Vec3 direction(const GltfAccessor& data, size_t vertex)
{
    return {float(data.values[vertex * 3]), float(data.values[vertex * 3 + 1]), -float(data.values[vertex * 3 + 2])};
}
// Parses an indexed attribute semantic without accepting gaps, signs or trailing characters.
size_t semanticIndex(std::string_view semantic, std::string_view prefix)
{
    const auto suffix = semantic.substr(prefix.size());
    size_t index = 0;
    auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() || index == SIZE_MAX)
    {
        throw EngineError("Invalid or unsupported attribute set: " + std::string(semantic));
    }
    return index;
}
// Imports visible vertex attributes and returns the number of declared joint/weight sets.
size_t importVertexAttributes(
    const tinygltf::Model& source,
    const tinygltf::Primitive& primitive,
    std::vector<Vertex>& vertices,
    size_t base,
    size_t count
)
{
    size_t setCount = 0;
    for (const auto& [semantic, index] : primitive.attributes)
    {
        if (semantic == "POSITION")
        {
            continue;
        }
        if (semantic.starts_with("JOINTS_") || semantic.starts_with("WEIGHTS_"))
        {
            const size_t set = semanticIndex(semantic, semantic.starts_with("JOINTS_") ? "JOINTS_" : "WEIGHTS_");
            setCount = std::max(setCount, set + 1);
            continue;
        }
        if (semantic == "NORMAL")
        {
            auto data = attribute(source, index, count, TINYGLTF_TYPE_VEC3);
            for (size_t i = 0; i < count; ++i)
            {
                vertices[base + i].normal = direction(data, i);
            }
        }
        else if (semantic == "TANGENT")
        {
            auto data = attribute(source, index, count, TINYGLTF_TYPE_VEC4);
            for (size_t i = 0; i < count; ++i)
            {
                const size_t at = i * 4;
                if (std::abs(data.values[at + 3]) != 1)
                {
                    throw EngineError("Tangent handedness must be +/-1.");
                }
                vertices[base + i].tangent = {
                    float(data.values[at]),
                    float(data.values[at + 1]),
                    -float(data.values[at + 2]),
                    -float(data.values[at + 3])
                };
            }
        }
        else if (semantic == "TEXCOORD_0" || semantic == "TEXCOORD_1")
        {
            auto data = attribute(source, index, count, TINYGLTF_TYPE_VEC2, true);
            for (size_t i = 0; i < count; ++i)
            {
                auto& uv = semantic == "TEXCOORD_0" ? vertices[base + i].uv : vertices[base + i].uv1;
                uv = {float(data.values[i * 2]), float(data.values[i * 2 + 1])};
            }
        }
        else if (semantic == "COLOR_0")
        {
            int type = gltfAt(source.accessors, index, "color accessor").type;
            if (type != TINYGLTF_TYPE_VEC3 && type != TINYGLTF_TYPE_VEC4)
            {
                throw EngineError("Color must be VEC3 or VEC4.");
            }
            auto data = attribute(source, index, count, type, true);
            for (size_t i = 0; i < count; ++i)
            {
                for (size_t c = 0; c < data.components; ++c)
                {
                    const float value = float(data.values[i * data.components + c]);
                    vertices[base + i].color[c] = std::clamp(value, 0.f, 1.f);
                }
            }
        }
        // Unused additional/custom attributes remain outside the renderer's vertex layout.
    }
    return setCount;
}
// Appends all joint/weight sets, preserving alignment across primitive vertex ranges.
void importInfluences(
    const tinygltf::Model& source,
    const tinygltf::Primitive& primitive,
    MeshDeformationData& deformation,
    GltfBudget& budget,
    size_t base,
    size_t count,
    size_t setCount
)
{
    if (setCount > primitive.attributes.size() / 2)
    {
        throw EngineError("Joint/weight attribute sets must be paired and consecutive.");
    }
    while (deformation.influenceSets.size() < setCount)
    {
        budget.add(base, sizeof(VertexJointInfluences));
        deformation.influenceSets.emplace_back(base);
    }
    for (auto& set : deformation.influenceSets)
    {
        budget.add(count, sizeof(VertexJointInfluences));
        set.resize(base + count);
    }
    for (size_t set = 0; set < setCount; ++set)
    {
        auto jointsIt = primitive.attributes.find("JOINTS_" + std::to_string(set));
        auto weightsIt = primitive.attributes.find("WEIGHTS_" + std::to_string(set));
        if (jointsIt == primitive.attributes.end() || weightsIt == primitive.attributes.end())
        {
            throw EngineError("Joint/weight attribute sets must be paired and consecutive.");
        }
        auto joints = readGltfAccessor(source, jointsIt->second);
        if (joints.type != TINYGLTF_TYPE_VEC4 || joints.count != count || joints.normalized ||
            (joints.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
             joints.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT))
        {
            throw EngineError("Invalid joint attribute encoding.");
        }
        auto weights = attribute(source, weightsIt->second, count, TINYGLTF_TYPE_VEC4, true);
        for (size_t i = 0; i < count; ++i)
        {
            for (size_t c = 0; c < 4; ++c)
            {
                auto& influence = deformation.influenceSets[set][base + i];
                influence.joints[c] = uint32_t(joints.values[i * 4 + c]);
                influence.weights[c] = float(weights.values[i * 4 + c]);
            }
        }
    }
    if (setCount)
    {
        for (size_t i = 0; i < count; ++i)
        {
            double total = 0;
            for (size_t set = 0; set < setCount; ++set)
            {
                for (size_t c = 0; c < 4; ++c)
                {
                    total += deformation.influenceSets[set][base + i].weights[c];
                }
            }
            // Quantized weights have rounding error; normalize the retained values without pruning.
            if (std::abs(total - 1) > .02 || total <= 0)
            {
                throw EngineError("Joint weights must sum to one.");
            }
            for (size_t set = 0; set < setCount; ++set)
            {
                for (size_t c = 0; c < 4; ++c)
                {
                    deformation.influenceSets[set][base + i].weights[c] /= float(total);
                }
            }
        }
    }
}
// Expands sparse or missing target streams into dense deltas aligned with concatenated vertices.
void importMorphTargets(
    const tinygltf::Model& source,
    const tinygltf::Primitive& primitive,
    MeshDeformationData& deformation,
    GltfBudget& budget,
    size_t base,
    size_t count
)
{
    const size_t targetCount = deformation.morphTargets.size();
    for (size_t target = 0; target < targetCount; ++target)
    {
        auto& converted = deformation.morphTargets[target];
        budget.add(count, 3 * sizeof(math::Vec3));
        converted.positions.resize(base + count);
        converted.normals.resize(base + count);
        converted.tangents.resize(base + count);
        for (const auto& [semantic, index] : primitive.targets[target])
        {
            if (semantic != "POSITION" && semantic != "NORMAL" && semantic != "TANGENT")
            {
                throw EngineError("Unsupported morph target attribute.");
            }
            if (semantic != "POSITION" && !primitive.attributes.contains(semantic))
            {
                throw EngineError("Morph delta lacks a base attribute.");
            }
            auto data = attribute(source, index, count, TINYGLTF_TYPE_VEC3);
            auto& values = semantic == "POSITION" ? converted.positions
                                                  : (semantic == "NORMAL" ? converted.normals : converted.tangents);
            for (size_t i = 0; i < count; ++i)
            {
                values[base + i] = direction(data, i);
            }
        }
    }
}
// Appends triangle-list indices with reflected winding and returns this primitive's first index.
uint32_t appendTriangles(
    const tinygltf::Model& source,
    const tinygltf::Primitive& primitive,
    std::vector<uint32_t>& indices,
    GltfBudget& budget,
    size_t base,
    size_t count
)
{
    if (primitive.indices < -1 || primitive.mode < -1)
    {
        throw EngineError("Invalid primitive index or topology reference.");
    }
    std::vector<uint32_t> primitiveIndices;
    if (primitive.indices >= 0)
    {
        auto data = readGltfAccessor(source, primitive.indices);
        if (data.type != TINYGLTF_TYPE_SCALAR || data.normalized ||
            (data.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
             data.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
             data.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
        {
            throw EngineError("Invalid primitive index encoding.");
        }
        for (double value : data.values)
        {
            const uint32_t restart =
                data.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE
                    ? 255u
                    : (data.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 65535u : UINT32_MAX);
            if (value >= count || value == restart)
            {
                throw EngineError("Primitive index outside vertices or reserved restart value.");
            }
            primitiveIndices.push_back(uint32_t(value));
        }
    }
    else
    {
        primitiveIndices.resize(count);
        std::iota(primitiveIndices.begin(), primitiveIndices.end(), 0u);
    }
    const int mode = primitive.mode < 0 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
    const size_t n = primitiveIndices.size();
    if (n < 3 || (mode == TINYGLTF_MODE_TRIANGLES && n % 3))
    {
        throw EngineError("Invalid triangle primitive count.");
    }
    if (mode != TINYGLTF_MODE_TRIANGLES && mode != TINYGLTF_MODE_TRIANGLE_STRIP && mode != TINYGLTF_MODE_TRIANGLE_FAN)
    {
        throw EngineError("Only triangle, strip and fan primitives are supported.");
    }
    const size_t triangles = mode == TINYGLTF_MODE_TRIANGLES ? n / 3 : n - 2;
    budget.add(triangles, 3 * sizeof(uint32_t));
    if (triangles > (UINT32_MAX - indices.size()) / 3)
    {
        throw EngineError("Mesh index count exceeds 32 bits.");
    }
    const uint32_t first = uint32_t(indices.size());
    // Reflect winding together with positions so front faces remain front faces in OFG.
    for (size_t triangle = 0; triangle < triangles; ++triangle)
    {
        size_t a, b, c;
        if (mode == TINYGLTF_MODE_TRIANGLES)
        {
            a = triangle * 3;
            b = a + 1;
            c = a + 2;
        }
        else if (mode == TINYGLTF_MODE_TRIANGLE_FAN)
        {
            a = 0;
            b = triangle + 1;
            c = triangle + 2;
        }
        else
        {
            a = triangle + (triangle % 2);
            b = triangle + 1 - (triangle % 2);
            c = triangle + 2;
        }
        indices.push_back(uint32_t(base) + primitiveIndices[a]);
        indices.push_back(uint32_t(base) + primitiveIndices[c]);
        indices.push_back(uint32_t(base) + primitiveIndices[b]);
    }
    return first;
}
} // namespace

void importGltfMeshes(const tinygltf::Model& source, ModelData& result, GltfBudget& budget)
{
    for (const auto& mesh : source.meshes)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<SubMesh> subMeshes;
        MeshDeformationData deformation;
        if (mesh.primitives.empty())
        {
            throw EngineError("Mesh has no primitives.");
        }
        const size_t targetCount = mesh.primitives.front().targets.size();
        budget.add(targetCount, sizeof(MorphTarget) + sizeof(float));
        deformation.morphTargets.resize(targetCount);
        for (double weight : mesh.weights)
        {
            deformation.defaultMorphWeights.push_back(float(weight));
        }
        for (const auto& primitive : mesh.primitives)
        {
            if (primitive.targets.size() != targetCount)
            {
                throw EngineError("Mesh primitives disagree on morph target count.");
            }
            const auto positionIt = primitive.attributes.find("POSITION");
            if (positionIt == primitive.attributes.end())
            {
                throw EngineError("Primitive is missing POSITION.");
            }
            const auto& positionDesc = gltfAt(source.accessors, positionIt->second, "POSITION accessor");
            auto positions = attribute(source, positionIt->second, positionDesc.count, TINYGLTF_TYPE_VEC3);
            const size_t count = positions.count, base = vertices.size();
            if (count > UINT32_MAX - base)
            {
                throw EngineError("Mesh vertex count exceeds 32-bit indices.");
            }
            budget.add(count, sizeof(Vertex));
            vertices.resize(base + count);
            for (size_t i = 0; i < count; ++i)
            {
                vertices[base + i].position = direction(positions, i);
            }

            const size_t setCount = importVertexAttributes(source, primitive, vertices, base, count);
            importInfluences(source, primitive, deformation, budget, base, count, setCount);
            importMorphTargets(source, primitive, deformation, budget, base, count);
            const uint32_t first = appendTriangles(source, primitive, indices, budget, base, count);
            if (primitive.material != -1)
            {
                gltfAt(source.materials, primitive.material, "primitive material");
            }
            auto material =
                primitive.material == -1 ? result.materials.back() : result.materials[size_t(primitive.material)];
            // Every material-selected UV stream must exist; silently substituting zeros masks broken assets.
            for (const auto& [name, uniform] : material->uniforms())
            {
                if (name.ends_with("UvSet"))
                {
                    if (auto uv = std::get_if<uint32_t>(&uniform);
                        uv && !primitive.attributes.contains("TEXCOORD_" + std::to_string(*uv)))
                    {
                        throw EngineError("Material requires a missing texture coordinate attribute.");
                    }
                }
            }
            budget.add(1, sizeof(SubMesh));
            subMeshes.push_back({first, uint32_t(indices.size()) - first, std::move(material)});
        }
        result.meshes.push_back(
            Mesh::create(std::move(vertices), std::move(indices), std::move(subMeshes), std::move(deformation))
        );
    }
}
} // namespace ofg
