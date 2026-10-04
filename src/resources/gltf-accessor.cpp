// Checked little-endian glTF accessor decoding, including matrix padding and sparse overrides.
#include "resources/gltf-import.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace ofg {
namespace {
// Returns the width of a legal core glTF scalar; extension-only encodings are deliberately unsupported.
size_t componentBytes(int type)
{
    switch (type)
    {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return 4;
    default:
        throw EngineError("Unsupported glTF accessor component type.");
    }
}
// Reads integers explicitly in glTF little-endian order, without alignment or host-endian assumptions.
uint32_t unsignedValue(const unsigned char* bytes, size_t size)
{
    uint32_t value = 0;
    for (size_t i = 0; i < size; ++i)
    {
        value |= uint32_t(bytes[i]) << (i * 8);
    }
    return value;
}
// Decodes one finite component with the specified normalized-integer interpretation.
double scalarValue(const unsigned char* bytes, int type, bool normalized)
{
    const auto raw = unsignedValue(bytes, componentBytes(type));
    double value = 0;
    switch (type)
    {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
        value = raw < 128 ? double(raw) : double(raw) - 256;
        return normalized ? std::max(value / 127.0, -1.0) : value;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
        value = raw < 32768 ? double(raw) : double(raw) - 65536;
        return normalized ? std::max(value / 32767.0, -1.0) : value;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return normalized ? raw / 255.0 : raw;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return normalized ? raw / 65535.0 : raw;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        return raw;
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        value = std::bit_cast<float>(raw);
        break;
    }
    if (!std::isfinite(value))
    {
        throw EngineError("Nonfinite glTF accessor value.");
    }
    return value;
}
// Borrows a validated bufferView after checking the entire view, not merely the requested accessor range.
std::span<const unsigned char> viewBytes(const tinygltf::Model& model, int index)
{
    const auto& view = gltfAt(model.bufferViews, index, "bufferView");
    const auto& buffer = gltfAt(model.buffers, view.buffer, "buffer").data;
    if (!view.byteLength || view.byteOffset > buffer.size() || view.byteLength > buffer.size() - view.byteOffset)
    {
        throw EngineError("glTF bufferView exceeds its buffer.");
    }
    return std::span(buffer).subspan(view.byteOffset, view.byteLength);
}
// Checks the final element using division to avoid overflow on 32-bit browser builds.
void checkRange(size_t size, size_t offset, size_t count, size_t stride, size_t elementBytes)
{
    if (!count || offset > size || elementBytes > size - offset || count - 1 > (size - offset - elementBytes) / stride)
    {
        throw EngineError("glTF accessor exceeds its bufferView.");
    }
}
} // namespace

GltfAccessor readGltfAccessor(const tinygltf::Model& model, int index)
{
    const auto& source = gltfAt(model.accessors, index, "accessor");
    const size_t scalarBytes = componentBytes(source.componentType);
    size_t rows = 0, columns = 1;
    switch (source.type)
    {
    case TINYGLTF_TYPE_SCALAR:
        rows = 1;
        break;
    case TINYGLTF_TYPE_VEC2:
        rows = 2;
        break;
    case TINYGLTF_TYPE_VEC3:
        rows = 3;
        break;
    case TINYGLTF_TYPE_VEC4:
        rows = 4;
        break;
    case TINYGLTF_TYPE_MAT2:
        rows = columns = 2;
        break;
    case TINYGLTF_TYPE_MAT3:
        rows = columns = 3;
        break;
    case TINYGLTF_TYPE_MAT4:
        rows = columns = 4;
        break;
    default:
        throw EngineError("Invalid glTF accessor shape.");
    }
    const size_t components = rows * columns;
    const size_t columnBytes = columns > 1 ? (rows * scalarBytes + 3) / 4 * 4 : rows * scalarBytes;
    const size_t elementBytes = columns * columnBytes;
    // Column starts are padded, but glTF permits omitting padding after the final matrix column.
    const size_t finalElementBytes = (columns - 1) * columnBytes + rows * scalarBytes;
    const size_t alignment = columns > 1 ? std::max(size_t(4), scalarBytes) : scalarBytes;
    if (!source.count || source.count > (512 * 1024 * 1024) / sizeof(double) / components ||
        (source.normalized && (source.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT ||
                               source.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)))
    {
        throw EngineError("Invalid or oversized glTF accessor.");
    }
    GltfAccessor result{
        source.count,
        components,
        source.type,
        source.componentType,
        source.normalized,
        std::vector<double>(source.count * components)
    };

    // Applies either the regular array or sparse payload to already validated destinations.
    const auto decodeElement = [&](const unsigned char* bytes, size_t target)
    {
        for (size_t column = 0; column < columns; ++column)
        {
            for (size_t row = 0; row < rows; ++row)
            {
                result.values[target * components + column * rows + row] = scalarValue(
                    bytes + column * columnBytes + row * scalarBytes,
                    source.componentType,
                    source.normalized
                );
            }
        }
    };
    if (source.bufferView >= 0)
    {
        const auto& view = gltfAt(model.bufferViews, source.bufferView, "accessor bufferView");
        const auto bytes = viewBytes(model, source.bufferView);
        const size_t stride = view.byteStride ? view.byteStride : elementBytes;
        if (source.byteOffset % scalarBytes || (view.byteOffset + source.byteOffset) % alignment ||
            stride < elementBytes || (view.byteStride && (stride > 252 || stride % 4)))
        {
            throw EngineError("Invalid glTF accessor alignment or stride.");
        }
        checkRange(bytes.size(), source.byteOffset, source.count, stride, finalElementBytes);
        for (size_t i = 0; i < source.count; ++i)
        {
            decodeElement(bytes.data() + source.byteOffset + i * stride, i);
        }
    }
    else if (source.bufferView != -1 || source.byteOffset)
    {
        throw EngineError("glTF accessor without a bufferView cannot have a byte offset.");
    }

    if (source.sparse.isSparse)
    {
        const auto& sparse = source.sparse;
        if (sparse.count <= 0 || size_t(sparse.count) > source.count ||
            (sparse.indices.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
             sparse.indices.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
             sparse.indices.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
        {
            throw EngineError("Invalid glTF sparse index description.");
        }
        const auto& indexView = gltfAt(model.bufferViews, sparse.indices.bufferView, "sparse index view");
        const auto& valueView = gltfAt(model.bufferViews, sparse.values.bufferView, "sparse value view");
        const auto indices = viewBytes(model, sparse.indices.bufferView);
        const auto values = viewBytes(model, sparse.values.bufferView);
        const size_t indexBytes = componentBytes(sparse.indices.componentType);
        if (indexView.byteStride || valueView.byteStride || sparse.indices.byteOffset % indexBytes ||
            (indexView.byteOffset + sparse.indices.byteOffset) % indexBytes || sparse.values.byteOffset % scalarBytes ||
            (valueView.byteOffset + sparse.values.byteOffset) % alignment)
        {
            throw EngineError("Invalid glTF sparse alignment or stride.");
        }
        checkRange(indices.size(), sparse.indices.byteOffset, sparse.count, indexBytes, indexBytes);
        checkRange(values.size(), sparse.values.byteOffset, sparse.count, elementBytes, finalElementBytes);
        uint32_t previous = 0;
        for (size_t i = 0; i < size_t(sparse.count); ++i)
        {
            const auto target = unsignedValue(indices.data() + sparse.indices.byteOffset + i * indexBytes, indexBytes);
            if (target >= source.count || (i && target <= previous))
            {
                throw EngineError("glTF sparse indices must be increasing and within the accessor.");
            }
            decodeElement(values.data() + sparse.values.byteOffset + i * elementBytes, target);
            previous = target;
        }
    }
    return result;
}

void GltfBudget::add(size_t count, size_t elementSize)
{
    constexpr size_t limit = 512 * 1024 * 1024;
    if (elementSize && count > (limit - bytes) / elementSize)
    {
        throw EngineError("Model exceeds the 512 MiB converted CPU payload budget.");
    }
    bytes += count * elementSize;
}
} // namespace ofg
