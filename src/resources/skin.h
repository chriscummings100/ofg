// Immutable skin joint definitions; live joint entities belong to each MeshRenderer instance.
#pragma once

#include "resources/resource.h"
#include "math/mat.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ofg {
struct SkinJoint
{
    uint32_t node = 0; // Node index in the originating Model, not a scene EntityId.
    math::Mat4 inverseBindMatrix = math::mat4Identity();
};

struct SkinDesc
{
    std::string name;
    std::vector<SkinJoint> joints;        // Palette order, also used by Mesh vertex influences.
    std::optional<uint32_t> skeletonRoot; // Model node index; need not itself be a joint.
};

class Skin : public Resource
{
public:
    // Creates a ready uncached resource; requires unique joints and finite affine inverse bind matrices.
    // The importer separately validates Model node references and hierarchy membership.
    static std::shared_ptr<Skin> create(SkinDesc desc);
    // Returns the immutable definition without any references to live scene objects.
    const SkinDesc& desc() const noexcept { return m_desc; }

private:
    // Takes the validated joint description.
    explicit Skin(SkinDesc desc);
    SkinDesc m_desc;
};
} // namespace ofg
