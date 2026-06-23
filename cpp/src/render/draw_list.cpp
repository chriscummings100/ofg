// Resolved render commands consumed by OFG renderer passes.
#include "ofg/render/draw_list.hpp"

#include "ofg/resources/material.hpp"
#include "ofg/resources/mesh.hpp"
#include "ofg/resources/shader.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ofg {

// Adds a command in stable insertion order.
void DrawList::add(DrawCommand command) {
    m_commands.push_back(std::move(command));
}

// Removes all commands without touching pointed-to resources.
void DrawList::clear() noexcept {
    m_commands.clear();
}

// Returns the commands in their current stable order.
std::span<const DrawCommand> DrawList::commands() const noexcept {
    return m_commands;
}

// Reports the number of draw commands.
std::size_t DrawList::size() const noexcept {
    return m_commands.size();
}

// Validates mesh, material, override, and draw-property references.
bool DrawList::validate(std::string& error) const {
    for (const DrawCommand& command : m_commands) {
        if (command.m_mesh == nullptr) {
            error = "Draw command mesh must not be null.";
            return false;
        }

        const std::span<const SubMesh> submeshes = command.m_mesh->submeshes();
        for (const MaterialOverride& material_override : command.m_material_overrides) {
            if (material_override.m_material == nullptr) {
                error = "Draw command material override must not be null.";
                return false;
            }
            if (material_override.m_submesh_index >= submeshes.size()) {
                error = "Draw command material override references a missing submesh.";
                return false;
            }
        }

        for (std::uint32_t submesh_index = 0; submesh_index < submeshes.size(); ++submesh_index) {
            Material* material = resolve_material(command, submesh_index, error);
            if (material == nullptr) {
                return false;
            }
            if (!command.m_properties.validate_for_scope(material->shader(), ShaderParameterScope::Draw, error)) {
                return false;
            }
        }
    }

    error.clear();
    return true;
}

// Resolves a submesh material after applying command-level overrides.
Material* resolve_material(const DrawCommand& command, std::uint32_t submesh_index, std::string& error) {
    if (command.m_mesh == nullptr) {
        error = "Draw command mesh must not be null.";
        return nullptr;
    }
    const std::span<const SubMesh> submeshes = command.m_mesh->submeshes();
    if (submesh_index >= submeshes.size()) {
        error = "Draw command references a missing submesh.";
        return nullptr;
    }

    Material* resolved = submeshes[submesh_index].m_default_material;
    if (resolved == nullptr) {
        error = "Draw command submesh default material must not be null.";
        return nullptr;
    }

    for (const MaterialOverride& material_override : command.m_material_overrides) {
        if (material_override.m_submesh_index != submesh_index) {
            continue;
        }
        if (material_override.m_material == nullptr) {
            error = "Draw command material override must not be null.";
            return nullptr;
        }
        resolved = material_override.m_material;
    }

    error.clear();
    return resolved;
}

} // namespace ofg
