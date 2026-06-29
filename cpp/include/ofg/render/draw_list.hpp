// Resolved render commands consumed by OFG renderer passes.
//
// DrawList intentionally stores non-owning resource pointers. The owning
// Resource storage or a scene bundle must outlive every render call that uses it.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/property_bag.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ofg {

class Material;
class Mesh;

struct MaterialOverride {
    std::uint32_t m_submesh_index{0};
    Material* m_material{nullptr};
};

struct DrawCommand {
    Mesh* m_mesh{nullptr};
    math::Mat4 m_model;
    PropertyBag m_properties;
    std::vector<MaterialOverride> m_material_overrides;
    math::Vec3 m_sort_origin;
};

class DrawList {
public:
    // Adds a command in stable insertion order.
    void add(DrawCommand command);
    // Removes all commands without touching pointed-to resources.
    void clear() noexcept;
    // Returns the commands in their current stable order.
    [[nodiscard]] std::span<const DrawCommand> commands() const noexcept;
    // Reports the number of draw commands.
    [[nodiscard]] std::size_t size() const noexcept;
    // Validates mesh, material, override, and draw-property references.
    void validate() const;

private:
    std::vector<DrawCommand> m_commands;
};

// Resolves a submesh material after applying command-level overrides.
[[nodiscard]] Material& resolve_material(const DrawCommand& command, std::uint32_t submesh_index);

} // namespace ofg
