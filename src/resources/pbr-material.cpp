// Validated surface factors and resource-pruned source variants, retaining the generic Material binding contract.
#include "resources/pbr-material.h"
#include "core/engine-error.h"
#include "pbr-shader.h"
#include <cmath>
#include <bit>
#include <map>
namespace ofg {
std::shared_ptr<Material> createPbrMaterial(const PbrMaterialDesc& desc)
{
    uint32_t key = desc.unlit ? (1u << 15) : 0;
    for (size_t i = 0; i < desc.textures.size(); ++i)
    {
        const auto& slot = desc.textures[i];
        if (slot.texture)
        {
            key |= 1u << i;
        }
        if (slot.uvSet > 1 || !std::isfinite(slot.rotation) || !std::isfinite(slot.offset.x) ||
            !std::isfinite(slot.offset.y) || !std::isfinite(slot.scale.x) || !std::isfinite(slot.scale.y))
        {
            throw EngineError("PBR texture requires finite transform and UV0 or UV1.");
        }
    }
    if (std::popcount(key & 0x7fff) > 12)
    {
        throw EngineError("PBR material exceeds portable budget: at most 12 material textures plus four IBL textures.");
    }
    // All scalar/vector fields must be finite. Physical ranges are checked below.
    if (!std::isfinite(desc.baseColor.x) || desc.baseColor.x < 0)
    {
        throw EngineError("Invalid PBR baseColor.");
    }
    if (!std::isfinite(desc.baseColor.y) || desc.baseColor.y < 0)
    {
        throw EngineError("Invalid PBR baseColor.");
    }
    if (!std::isfinite(desc.baseColor.z) || desc.baseColor.z < 0)
    {
        throw EngineError("Invalid PBR baseColor.");
    }
    if (!std::isfinite(desc.baseColor.w) || desc.baseColor.w < 0)
    {
        throw EngineError("Invalid PBR baseColor.");
    }
    if (!std::isfinite(desc.metallic))
    {
        throw EngineError("Nonfinite PBR metallic.");
    }
    if (!std::isfinite(desc.roughness))
    {
        throw EngineError("Nonfinite PBR roughness.");
    }
    if (!std::isfinite(desc.emissive.x) || desc.emissive.x < 0)
    {
        throw EngineError("Invalid PBR emissive.");
    }
    if (!std::isfinite(desc.emissive.y) || desc.emissive.y < 0)
    {
        throw EngineError("Invalid PBR emissive.");
    }
    if (!std::isfinite(desc.emissive.z) || desc.emissive.z < 0)
    {
        throw EngineError("Invalid PBR emissive.");
    }
    if (!std::isfinite(desc.emissiveStrength))
    {
        throw EngineError("Nonfinite PBR emissiveStrength.");
    }
    if (!std::isfinite(desc.normalScale))
    {
        throw EngineError("Nonfinite PBR normalScale.");
    }
    if (!std::isfinite(desc.occlusionStrength))
    {
        throw EngineError("Nonfinite PBR occlusionStrength.");
    }
    if (!std::isfinite(desc.ior))
    {
        throw EngineError("Nonfinite PBR ior.");
    }
    if (!std::isfinite(desc.specular))
    {
        throw EngineError("Nonfinite PBR specular.");
    }
    if (!std::isfinite(desc.specularColor.x) || desc.specularColor.x < 0)
    {
        throw EngineError("Invalid PBR specularColor.");
    }
    if (!std::isfinite(desc.specularColor.y) || desc.specularColor.y < 0)
    {
        throw EngineError("Invalid PBR specularColor.");
    }
    if (!std::isfinite(desc.specularColor.z) || desc.specularColor.z < 0)
    {
        throw EngineError("Invalid PBR specularColor.");
    }
    if (!std::isfinite(desc.clearcoat))
    {
        throw EngineError("Nonfinite PBR clearcoat.");
    }
    if (!std::isfinite(desc.clearcoatRoughness))
    {
        throw EngineError("Nonfinite PBR clearcoatRoughness.");
    }
    if (!std::isfinite(desc.clearcoatNormalScale))
    {
        throw EngineError("Nonfinite PBR clearcoatNormalScale.");
    }
    if (!std::isfinite(desc.sheenColor.x) || desc.sheenColor.x < 0)
    {
        throw EngineError("Invalid PBR sheenColor.");
    }
    if (!std::isfinite(desc.sheenColor.y) || desc.sheenColor.y < 0)
    {
        throw EngineError("Invalid PBR sheenColor.");
    }
    if (!std::isfinite(desc.sheenColor.z) || desc.sheenColor.z < 0)
    {
        throw EngineError("Invalid PBR sheenColor.");
    }
    if (!std::isfinite(desc.sheenRoughness))
    {
        throw EngineError("Nonfinite PBR sheenRoughness.");
    }
    if (!std::isfinite(desc.iridescence))
    {
        throw EngineError("Nonfinite PBR iridescence.");
    }
    if (!std::isfinite(desc.iridescenceIor))
    {
        throw EngineError("Nonfinite PBR iridescenceIor.");
    }
    if (!std::isfinite(desc.thicknessMinimum))
    {
        throw EngineError("Nonfinite PBR thicknessMinimum.");
    }
    if (!std::isfinite(desc.thicknessMaximum))
    {
        throw EngineError("Nonfinite PBR thicknessMaximum.");
    }
    if (!std::isfinite(desc.anisotropy))
    {
        throw EngineError("Nonfinite PBR anisotropy.");
    }
    if (!std::isfinite(desc.anisotropyRotation))
    {
        throw EngineError("Nonfinite PBR anisotropyRotation.");
    }
    if (!std::isfinite(desc.alphaCutoff))
    {
        throw EngineError("Nonfinite PBR alphaCutoff.");
    }
    for (float value :
         {desc.metallic,
          desc.roughness,
          desc.specular,
          desc.clearcoat,
          desc.clearcoatRoughness,
          desc.sheenRoughness,
          desc.iridescence,
          desc.anisotropy,
          desc.occlusionStrength,
          desc.alphaCutoff,
          desc.baseColor.w})
    {
        if (value < 0 || value > 1)
        {
            throw EngineError("PBR weight/roughness/alpha must be in [0,1].");
        }
    }
    for (float value :
         {desc.baseColor.x,
          desc.baseColor.y,
          desc.baseColor.z,
          desc.specularColor.x,
          desc.specularColor.y,
          desc.specularColor.z,
          desc.sheenColor.x,
          desc.sheenColor.y,
          desc.sheenColor.z})
    {
        if (value > 1)
        {
            throw EngineError("PBR reflectance factors must be in [0,1].");
        }
    }
    if (desc.ior < 1 || desc.iridescenceIor < 1 || desc.thicknessMinimum < 0 ||
        desc.thicknessMaximum < desc.thicknessMinimum || desc.emissiveStrength < 0 || desc.normalScale < 0 ||
        desc.clearcoatNormalScale < 0)
    {
        throw EngineError("Invalid PBR optical parameter.");
    }
    if (desc.alphaMode != AlphaMode::Opaque && desc.alphaMode != AlphaMode::Mask && desc.alphaMode != AlphaMode::Blend)
    {
        throw EngineError("Invalid PBR alpha mode.");
    }
    // Weak keys share programs while materials live; no registry retains abandoned shader variants.
    static std::map<uint32_t, std::weak_ptr<Shader>> variants;
    std::erase_if(
        variants,
        [](const auto& item)
        {
            return item.second.expired();
        }
    );
    auto shader = variants[key].lock();
    if (!shader)
    {
        std::string defines = "#define MATERIAL_ANISOTROPY 1\n";
        if (desc.unlit)
        {
            defines += "#define UNLIT 1\n";
        }
        for (size_t i = 0; i < desc.textures.size(); ++i)
        {
            if (key & (1u << i))
            {
                defines += "#define SLOT" + std::to_string(i) + " 1\n";
            }
        }
        shader = Shader::create("pbr-" + std::to_string(key), defines + pbrShader);
        variants[key] = shader;
    }
    auto material = Material::create(shader);
    material->setRenderState({desc.alphaMode, desc.doubleSided, true, desc.unlit});
    material->setUniform("baseColor", desc.baseColor);
    material->setUniform("metallic", desc.metallic);
    material->setUniform("roughness", desc.roughness);
    material->setUniform("emissive", desc.emissive);
    material->setUniform("emissiveStrength", desc.emissiveStrength);
    material->setUniform("normalScale", desc.normalScale);
    material->setUniform("occlusionStrength", desc.occlusionStrength);
    material->setUniform("ior", desc.ior);
    material->setUniform("specular", desc.specular);
    material->setUniform("specularColor", desc.specularColor);
    material->setUniform("clearcoat", desc.clearcoat);
    material->setUniform("clearcoatRoughness", desc.clearcoatRoughness);
    material->setUniform("clearcoatNormalScale", desc.clearcoatNormalScale);
    material->setUniform("sheenColor", desc.sheenColor);
    material->setUniform("sheenRoughness", desc.sheenRoughness);
    material->setUniform("iridescence", desc.iridescence);
    material->setUniform("iridescenceIor", desc.iridescenceIor);
    material->setUniform("thicknessMinimum", desc.thicknessMinimum);
    material->setUniform("thicknessMaximum", desc.thicknessMaximum);
    material->setUniform("anisotropy", desc.anisotropy);
    material->setUniform("anisotropyRotation", desc.anisotropyRotation);
    material->setUniform("alphaCutoff", desc.alphaCutoff);
    for (size_t i = 0; i < desc.textures.size(); ++i)
    {
        const auto& slot = desc.textures[i];
        if (!slot.texture)
        {
            continue;
        }
        std::string name = "slot" + std::to_string(i);
        material->setTexture(name + "Texture", slot.texture);
        material->setSampler(name + "Sampler", slot.sampler ? slot.sampler : Sampler::create());
        float c = std::cos(slot.rotation), s = std::sin(slot.rotation);
        material->setUniform(name + "TransformX", math::Vec3{c * slot.scale.x, -s * slot.scale.y, slot.offset.x});
        material->setUniform(name + "TransformY", math::Vec3{s * slot.scale.x, c * slot.scale.y, slot.offset.y});
        material->setUniform(name + "UvSet", slot.uvSet);
    }
    return material;
}
} // namespace ofg
