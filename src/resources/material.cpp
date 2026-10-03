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
    result->m_textures = m_textures;
    result->m_samplers = m_samplers;
    return result;
}
// Requires a direct root resource field outside the reserved draw/material blocks.
static void checkBindingName(const std::string& name)
{
    if (name.empty() || name.find_first_of(".[]") != std::string::npos || name == "draw" || name == "material")
    {
        throw EngineError("Texture/sampler must name a direct non-reserved shader field.");
    }
}

const std::shared_ptr<Texture>& bindingTexture(const TextureBinding& binding)
{
    if (auto texture = std::get_if<std::shared_ptr<Texture>>(&binding))
    {
        return *texture;
    }
    return std::get<TextureView>(binding).texture();
}

void Material::setTexture(std::string name, std::shared_ptr<Texture> texture)
{
    checkBindingName(name);
    if (!texture || m_samplers.contains(name))
    {
        throw EngineError("Invalid or conflicting texture binding: " + name);
    }
    m_textures.insert_or_assign(std::move(name), std::move(texture));
}

void Material::setTexture(std::string name, TextureView view)
{
    checkBindingName(name);
    if (!view.texture() || m_samplers.contains(name))
    {
        throw EngineError("Invalid or conflicting texture view: " + name);
    }
    m_textures.insert_or_assign(std::move(name), std::move(view));
}

void Material::setSampler(std::string name, std::shared_ptr<Sampler> sampler)
{
    checkBindingName(name);
    if (!sampler || m_textures.contains(name))
    {
        throw EngineError("Invalid or conflicting sampler binding: " + name);
    }
    m_samplers.insert_or_assign(std::move(name), std::move(sampler));
}
} // namespace ofg
