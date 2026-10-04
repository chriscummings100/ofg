// Private checked-accessor and conversion boundary between tinygltf and OFG resource descriptions.
#pragma once
#include "resources/gltf-library.h"
#include "resources/model.h"
#include "core/engine-error.h"
#include <string_view>

namespace ofg {
// Validates source table references in one place before dereferencing parser-owned data.
template<typename T>
const T& gltfAt(const std::vector<T>& values, int index, std::string_view kind)
{
    if (index < 0 || size_t(index) >= values.size())
    {
        throw EngineError("Invalid glTF " + std::string(kind) + " index " + std::to_string(index));
    }
    return values[size_t(index)];
}
struct GltfAccessor
{
    size_t count = 0;
    size_t components = 0;
    int type = 0;
    int componentType = 0;
    bool normalized = false;
    std::vector<double> values; // Retains uint32 integer precision while decoding normalized/float attributes.
};
// Decodes checked little-endian strided/sparse data; does not expose borrowed unaligned pointers.
GltfAccessor readGltfAccessor(const tinygltf::Model& model, int index);
// Converts all resources and hierarchy data, rejecting invalid references before publication.
ModelData importGltf(const tinygltf::Model& source, std::vector<std::string>& warnings);
// Converts a glTF node's matrix or TRS into the OFG left-handed coordinate convention.
LocalTransform importGltfTransform(const tinygltf::Node& node);

struct GltfBudget
{
    size_t bytes = 0;
    // Accounts retained CPU payload before allocation, with checked arithmetic and a 512 MiB cap.
    void add(size_t count, size_t elementSize);
};
// Maps PBR resources and textures; decoded pixels are released after creating color/data texture variants.
void importGltfMaterials(const tinygltf::Model& source, ModelData& result, GltfBudget& budget);
// Converts primitives and deformation arrays into one shared Mesh per source mesh.
void importGltfMeshes(const tinygltf::Model& source, ModelData& result, GltfBudget& budget);
} // namespace ofg
