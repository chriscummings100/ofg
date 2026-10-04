// Converts glTF right-handed node transforms into OFG TRS, rejecting affine shear.
#include "resources/gltf-import.h"
#include "scene/scene.h"
#include <algorithm>
#include <cmath>

namespace ofg {
LocalTransform importGltfTransform(const tinygltf::Node& node)
{
    LocalTransform result;
    if (node.matrix.empty())
    {
        if (!node.translation.empty())
        {
            if (node.translation.size() != 3)
            {
                throw EngineError("Node translation must have three components.");
            }
            result.position = {float(node.translation[0]), float(node.translation[1]), -float(node.translation[2])};
        }
        if (!node.scale.empty())
        {
            if (node.scale.size() != 3)
            {
                throw EngineError("Node scale must have three components.");
            }
            result.scale = {float(node.scale[0]), float(node.scale[1]), float(node.scale[2])};
        }
        if (!node.rotation.empty())
        {
            if (node.rotation.size() != 4)
            {
                throw EngineError("Node rotation must have four components.");
            }
            result.rotation =
                {-float(node.rotation[0]), -float(node.rotation[1]), float(node.rotation[2]), float(node.rotation[3])};
        }
        for (float value :
             {result.position.x, result.position.y, result.position.z, result.scale.x, result.scale.y, result.scale.z})
        {
            if (!std::isfinite(value))
            {
                throw EngineError("Node TRS must be finite.");
            }
        }
        std::string error;
        auto rotation = math::normalize(result.rotation, error);
        if (!rotation)
        {
            throw EngineError("Node rotation: " + error);
        }
        result.rotation = *rotation;
        return result;
    }
    if (node.matrix.size() != 16 || !node.translation.empty() || !node.rotation.empty() || !node.scale.empty())
    {
        throw EngineError("Node matrix must have 16 components and cannot coexist with TRS.");
    }
    math::Mat4 matrix;
    float magnitude = 1;
    for (size_t column = 0; column < 4; ++column)
    {
        for (size_t row = 0; row < 4; ++row)
        {
            const float value = float(node.matrix[column * 4 + row]) * ((column == 2) != (row == 2) ? -1.f : 1.f);
            if (!std::isfinite(value))
            {
                throw EngineError("Non-finite node matrix.");
            }
            matrix[column][row] = value;
            magnitude = std::max(magnitude, std::abs(value));
        }
    }
    if (matrix[0].w != 0 || matrix[1].w != 0 || matrix[2].w != 0 || matrix[3].w != 1)
    {
        throw EngineError("Node matrix must be affine.");
    }
    result.position = {matrix[3].x, matrix[3].y, matrix[3].z};
    math::Vec3 axes[3];
    float scales[3];
    size_t present = 0;
    for (size_t i = 0; i < 3; ++i)
    {
        axes[i] = {matrix[i].x, matrix[i].y, matrix[i].z};
        scales[i] = float(std::hypot(double(axes[i].x), double(axes[i].y), double(axes[i].z)));
        if (!std::isfinite(scales[i]))
        {
            throw EngineError("Node scale exceeds float range.");
        }
        if (scales[i] > 0)
        {
            axes[i] = math::mul(axes[i], 1 / scales[i]);
            ++present;
        }
    }
    std::string error;
    if (present == 0)
    {
        axes[0] = {1, 0, 0};
        axes[1] = {0, 1, 0};
        axes[2] = {0, 0, 1};
    }
    else if (present == 1)
    {
        const size_t a = scales[0] ? 0 : (scales[1] ? 1 : 2);
        const size_t b = (a + 1) % 3, c = (a + 2) % 3;
        const math::Vec3 reference = std::abs(axes[a].x) < .8f ? math::Vec3{1, 0, 0} : math::Vec3{0, 1, 0};
        axes[b] = *math::normalize(math::cross(axes[a], reference), error);
        axes[c] = math::cross(axes[a], axes[b]);
    }
    else if (present == 2)
    {
        const size_t missing = !scales[0] ? 0 : (!scales[1] ? 1 : 2);
        auto axis = math::normalize(math::cross(axes[(missing + 1) % 3], axes[(missing + 2) % 3]), error);
        if (!axis)
        {
            throw EngineError("Node matrix contains shear.");
        }
        axes[missing] = *axis;
    }
    else if (math::dot(math::cross(axes[0], axes[1]), axes[2]) < 0)
    {
        axes[0] = math::mul(axes[0], -1);
        scales[0] = -scales[0];
    }
    auto rotation = math::quatLookAtLh({}, axes[2], axes[1], error);
    if (!rotation)
    {
        throw EngineError("Node matrix cannot be decomposed: " + error);
    }
    result.rotation = *rotation;
    result.scale = {scales[0], scales[1], scales[2]};
    const auto recomposed = parentFromLocal(result);
    for (size_t column = 0; column < 4; ++column)
    {
        for (size_t row = 0; row < 4; ++row)
        {
            if (std::abs(matrix[column][row] - recomposed[column][row]) > 1e-5f * magnitude)
            {
                throw EngineError("Node matrix contains shear that cannot be represented by TRS.");
            }
        }
    }
    return result;
}
} // namespace ofg
