// Named shader property values shared by OFG materials and draw commands.
#include "ofg/resources/property_bag.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/mat.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/texture.hpp"
#include "ofg/resources/shader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ofg {
namespace {

// Appends raw bytes for a trivially copyable value to a byte vector.
template <typename T> void append_value_bytes(std::vector<std::byte>& bytes, const T& value) {
    const auto* value_bytes = reinterpret_cast<const std::byte*>(&value);
    bytes.insert(bytes.end(), value_bytes, value_bytes + sizeof(T));
}

// Appends shader uniform bytes for a previously validated property value.
void append_property_uniform_bytes(
    const PropertyValue& value, ShaderParameterType type, std::vector<std::byte>& bytes) {
    switch (type) {
    case ShaderParameterType::Float:
        append_value_bytes(bytes, std::get<float>(value));
        return;
    case ShaderParameterType::Vec2: {
        const math::Vec2 vector = std::get<math::Vec2>(value);
        append_value_bytes(bytes, vector.x);
        append_value_bytes(bytes, vector.y);
        return;
    }
    case ShaderParameterType::Vec3: {
        const math::Vec3 vector = std::get<math::Vec3>(value);
        append_value_bytes(bytes, vector.x);
        append_value_bytes(bytes, vector.y);
        append_value_bytes(bytes, vector.z);
        return;
    }
    case ShaderParameterType::Vec4: {
        const math::Vec4 vector = std::get<math::Vec4>(value);
        append_value_bytes(bytes, vector.x);
        append_value_bytes(bytes, vector.y);
        append_value_bytes(bytes, vector.z);
        append_value_bytes(bytes, vector.w);
        return;
    }
    case ShaderParameterType::Mat4: {
        const std::array<float, 16> packed = math::pack_mat4(std::get<math::Mat4>(value));
        const auto* matrix_bytes = reinterpret_cast<const std::byte*>(packed.data());
        bytes.insert(bytes.end(), matrix_bytes, matrix_bytes + sizeof(float) * packed.size());
        return;
    }
    case ShaderParameterType::Texture:
        return;
    }
}

} // namespace

// Inserts or replaces a named property value.
void PropertyBag::set(std::string name, PropertyValue value) {
    for (Entry& entry : m_entries) {
        if (entry.m_name == name) {
            entry.m_value = value;
            return;
        }
    }
    m_entries.push_back(Entry{std::move(name), value});
}

// Finds a property value by name.
const PropertyValue* PropertyBag::get(std::string_view name) const noexcept {
    for (const Entry& entry : m_entries) {
        if (entry.m_name == name) {
            return &entry.m_value;
        }
    }
    return nullptr;
}

// Reports the number of properties in the bag.
std::size_t PropertyBag::size() const noexcept {
    return m_entries.size();
}

// Validates the bag against one shader binding scope.
void PropertyBag::validate_for_scope(const Shader& shader, ShaderParameterScope scope) const {
    const std::vector<ShaderParameter> parameters = shader.parameters_for_scope(scope);
    for (const ShaderParameter& parameter : parameters) {
        const PropertyValue* value = get(parameter.m_name);
        if (value == nullptr) {
            if (parameter.m_required) {
                throw EngineError("Missing required " + std::string(shader_parameter_scope_name(scope)) +
                                  " property '" + parameter.m_name + "'.");
            }
            continue;
        }
        if (!property_value_matches_type(*value, parameter.m_type)) {
            throw EngineError("Property '" + parameter.m_name + "' does not match expected type " +
                              shader_parameter_type_name(parameter.m_type) + ".");
        }
    }

    for (const Entry& entry : m_entries) {
        const ShaderParameter* parameter = shader.parameter(entry.m_name);
        if (parameter == nullptr || parameter->m_scope != scope) {
            throw EngineError("Property '" + entry.m_name + "' is not declared for " +
                              std::string(shader_parameter_scope_name(scope)) + " scope.");
        }
    }
}

// Packs uniform-compatible values for one shader binding scope.
std::vector<std::byte> PropertyBag::pack_uniforms_for_scope(const Shader& shader, ShaderParameterScope scope) const {
    validate_for_scope(shader, scope);

    std::vector<std::byte> bytes;
    std::size_t cursor = 0;
    const std::vector<ShaderParameter> parameters = shader.parameters_for_scope(scope);
    for (const ShaderParameter& parameter : parameters) {
        const std::optional<std::size_t> byte_size = shader_parameter_uniform_size(parameter.m_type);
        if (!byte_size.has_value()) {
            continue;
        }
        const PropertyValue* value = get(parameter.m_name);
        if (value == nullptr) {
            continue;
        }

        const std::size_t write_offset = parameter.m_uniform_offset == 0 ? cursor : parameter.m_uniform_offset;
        if (write_offset < cursor) {
            throw EngineError("Property '" + parameter.m_name + "' uniform offset overlaps an earlier property.");
        }

        std::vector<std::byte> property_bytes;
        append_property_uniform_bytes(*value, parameter.m_type, property_bytes);

        const std::size_t write_end = write_offset + property_bytes.size();
        if (bytes.size() < write_end) {
            bytes.resize(write_end, std::byte{0});
        }
        std::memcpy(bytes.data() + write_offset, property_bytes.data(), property_bytes.size());
        cursor = write_end;
    }
    return bytes;
}

// Returns whether a property value matches a shader parameter type.
bool property_value_matches_type(const PropertyValue& value, ShaderParameterType type) noexcept {
    switch (type) {
    case ShaderParameterType::Float:
        return std::holds_alternative<float>(value);
    case ShaderParameterType::Vec2:
        return std::holds_alternative<math::Vec2>(value);
    case ShaderParameterType::Vec3:
        return std::holds_alternative<math::Vec3>(value);
    case ShaderParameterType::Vec4:
        return std::holds_alternative<math::Vec4>(value);
    case ShaderParameterType::Mat4:
        return std::holds_alternative<math::Mat4>(value);
    case ShaderParameterType::Texture:
        return std::holds_alternative<Ptr<Texture>>(value) && static_cast<bool>(std::get<Ptr<Texture>>(value));
    }
    return false;
}

// Returns the uniform byte size for a packable shader parameter type.
std::optional<std::size_t> shader_parameter_uniform_size(ShaderParameterType type) noexcept {
    switch (type) {
    case ShaderParameterType::Float:
        return sizeof(float);
    case ShaderParameterType::Vec2:
        return sizeof(float) * 2;
    case ShaderParameterType::Vec3:
        return sizeof(float) * 3;
    case ShaderParameterType::Vec4:
        return sizeof(float) * 4;
    case ShaderParameterType::Mat4:
        return sizeof(float) * 16;
    case ShaderParameterType::Texture:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace ofg
