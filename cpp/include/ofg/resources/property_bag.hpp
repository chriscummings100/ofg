// Named shader property values shared by OFG materials and draw commands.
//
// PropertyBag is intentionally small: it validates explicit shader schemas and
// can pack uniform-compatible values in declared layout order.
#pragma once

#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/shader.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ofg {

class Texture;

using PropertyValue = std::variant<float, math::Vec2, math::Vec3, math::Vec4, math::Mat4, Texture*>;

class PropertyBag {
public:
    // Inserts or replaces a named property value.
    void set(std::string name, PropertyValue value);
    // Finds a property value by name.
    [[nodiscard]] const PropertyValue* get(std::string_view name) const noexcept;
    // Reports the number of properties in the bag.
    [[nodiscard]] std::size_t size() const noexcept;
    // Validates the bag against one shader binding scope.
    [[nodiscard]] bool validate_for_scope(const Shader& shader, ShaderParameterScope scope, std::string& error) const;
    // Packs uniform-compatible values for one shader binding scope.
    [[nodiscard]] std::optional<std::vector<std::byte>> pack_uniforms_for_scope(
        const Shader& shader, ShaderParameterScope scope, std::string& error) const;

private:
    struct Entry {
        std::string m_name;
        PropertyValue m_value;
    };

    std::vector<Entry> m_entries;
};

// Returns whether a property value matches a shader parameter type.
[[nodiscard]] bool property_value_matches_type(const PropertyValue& value, ShaderParameterType type) noexcept;

// Returns the uniform byte size for a packable shader parameter type.
[[nodiscard]] std::optional<std::size_t> shader_parameter_uniform_size(ShaderParameterType type) noexcept;

} // namespace ofg
