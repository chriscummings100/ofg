// Shared shader, textures and samplers with independently editable named bindings and uniform values.
#pragma once

#include "math/mat.h"
#include "resources/shader.h"
#include "resources/texture.h"
#include "resources/sampler.h"
#include <cstdint>
#include <map>
#include <variant>

namespace ofg {
using UniformValue = std::variant<float, int32_t, uint32_t, math::Vec2, math::Vec3, math::Vec4, math::Mat4>;
using UniformValues = std::map<std::string, UniformValue>;

using TextureBinding = std::variant<std::shared_ptr<Texture>, TextureView>;

// Returns the retained texture independently of the selected view range.
const std::shared_ptr<Texture>& bindingTexture(const TextureBinding& binding);

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
    // Binds the default view, allowing a pending texture; null and reserved names are rejected.
    void setTexture(std::string name, std::shared_ptr<Texture> texture);
    // Binds an explicit loaded mip range.
    void setTexture(std::string name, TextureView view);
    // Binds a shared immutable sampler.
    void setSampler(std::string name, std::shared_ptr<Sampler> sampler);
    // Returns named texture bindings without exposing map mutation.
    const std::map<std::string, TextureBinding>& textures() const noexcept { return m_textures; }
    // Returns named sampler bindings without exposing map mutation.
    const std::map<std::string, std::shared_ptr<Sampler>>& samplers() const noexcept { return m_samplers; }
    // Copies values/bindings into a distinct ready resource sharing shader, textures and samplers.
    std::shared_ptr<Material> clone() const;

private:
    // Retains the validated shader.
    explicit Material(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> m_shader;
    UniformValues m_uniforms;
    std::map<std::string, TextureBinding> m_textures;
    std::map<std::string, std::shared_ptr<Sampler>> m_samplers;
};
} // namespace ofg
