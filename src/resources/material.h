// Shared shader and independently editable, named uniform values; clones share only dependencies.
#pragma once

#include "math/mat.h"
#include "resources/shader.h"
#include <cstdint>
#include <map>
#include <variant>

namespace ofg {
using UniformValue = std::variant<float, int32_t, uint32_t, math::Vec2, math::Vec3, math::Vec4, math::Mat4>;
using UniformValues = std::map<std::string, UniformValue>;

class Material : public Resource
{
public:
    // Creates an uncached material with no uniform values; shader must be non-null.
    static std::shared_ptr<Material> create(std::shared_ptr<Shader> shader);
    // Returns the shared shader dependency.
    const std::shared_ptr<Shader>& shader() const noexcept { return m_shader; }
    // Sets one direct material-block field; reflection checks its name/type during rendering.
    void setUniform(std::string name, UniformValue value);
    // Returns the supplied values without exposing mutation outside setUniform.
    const UniformValues& uniforms() const noexcept { return m_uniforms; }
    // Copies values into a distinct ready resource while sharing the shader.
    std::shared_ptr<Material> clone() const;

private:
    // Retains the validated shader.
    explicit Material(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> m_shader;
    UniformValues m_uniforms;
};
} // namespace ofg
