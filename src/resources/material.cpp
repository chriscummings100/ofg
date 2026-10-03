// Material creation, direct-field values and explicit value-copy cloning.
#include "resources/material.h"
#include "core/engine-error.h"
#include <utility>

namespace ofg {
Material::Material(std::shared_ptr<Shader> shader)
    : m_shader(std::move(shader))
{
}

std::shared_ptr<Material> Material::create(std::shared_ptr<Shader> shader)
{
    if (!shader)
    {
        throw EngineError("Material requires a shader.");
    }
    return std::shared_ptr<Material>(new Material(std::move(shader)));
}

void Material::setUniform(std::string name, UniformValue value)
{
    if (name.empty() || name.find_first_of(".[]") != std::string::npos)
    {
        throw EngineError("Material uniform must name a direct field.");
    }
    m_uniforms.insert_or_assign(std::move(name), std::move(value));
}

std::shared_ptr<Material> Material::clone() const
{
    auto result = create(m_shader);
    result->m_uniforms = m_uniforms;
    return result;
}
} // namespace ofg
