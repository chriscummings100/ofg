// Validates skin palettes independently of any Model hierarchy or graphics device.
#include "resources/skin.h"
#include "core/engine-error.h"
#include <cmath>
#include <unordered_set>
#include <utility>

namespace ofg {
Skin::Skin(SkinDesc desc)
    : m_desc(std::move(desc))
{
}

std::shared_ptr<Skin> Skin::create(SkinDesc desc)
{
    if (desc.joints.empty() || desc.joints.size() > UINT32_MAX)
    {
        throw EngineError("Skin requires a nonempty uint32-addressable joint palette.");
    }

    std::unordered_set<uint32_t> nodes;
    for (const auto& joint : desc.joints)
    {
        if (!nodes.insert(joint.node).second)
        {
            throw EngineError("Skin joint node appears more than once: " + std::to_string(joint.node));
        }
        for (float value : math::packMat4(joint.inverseBindMatrix))
        {
            if (!std::isfinite(value))
            {
                throw EngineError("Skin inverse bind matrix must be finite.");
            }
        }
        const auto& matrix = joint.inverseBindMatrix;
        if (matrix[0].w != 0 || matrix[1].w != 0 || matrix[2].w != 0 || matrix[3].w != 1)
        {
            throw EngineError("Skin inverse bind matrix must be affine with final row [0,0,0,1].");
        }
    }
    return std::shared_ptr<Skin>(new Skin(std::move(desc)));
}
} // namespace ofg
