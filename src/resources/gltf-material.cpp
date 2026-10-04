// Maps glTF surface descriptions and image/color-space variants into OFG's existing PBR resources.
#include "resources/gltf-import.h"
#include "resources/pbr-material.h"
#include "resources/image-decoder.h"
#include <cmath>
#include <map>

namespace ofg {
namespace {
// Reads an optional numeric extension property, rejecting incompatible JSON types.
float number(const tinygltf::Value& object, const char* name, float fallback)
{
    if (!object.Has(name))
    {
        return fallback;
    }
    const auto& value = object.Get(name);
    if (!value.IsNumber())
    {
        throw EngineError(std::string("Expected numeric material property: ") + name);
    }
    return float(value.GetNumberAsDouble());
}
// Reads an optional exact-width extension vector.
std::vector<float> vector(const tinygltf::Value& object, const char* name, std::vector<float> fallback)
{
    if (!object.Has(name))
    {
        return fallback;
    }
    const auto& value = object.Get(name);
    if (!value.IsArray() || value.ArrayLen() != fallback.size())
    {
        throw EngineError("Invalid material vector width.");
    }
    for (size_t i = 0; i < fallback.size(); ++i)
    {
        if (!value.Get(int(i)).IsNumber())
        {
            throw EngineError("Non-numeric material vector.");
        }
        fallback[i] = float(value.Get(int(i)).GetNumberAsDouble());
    }
    return fallback;
}
// Reads a bounded JSON integer without silently truncating fractional values.
int integer(const tinygltf::Value& object, const char* name, int fallback)
{
    if (!object.Has(name))
    {
        return fallback;
    }
    const auto& value = object.Get(name);
    if (!value.IsInt())
    {
        throw EngineError("Expected integer texture property.");
    }
    return value.GetNumberAsInt();
}
// Maps the three core glTF wrapping modes.
TextureAddressMode addressMode(int value)
{
    switch (value)
    {
    case TINYGLTF_TEXTURE_WRAP_REPEAT:
        return TextureAddressMode::Repeat;
    case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
        return TextureAddressMode::ClampToEdge;
    case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
        return TextureAddressMode::MirroredRepeat;
    default:
        throw EngineError("Invalid glTF sampler wrapping mode.");
    }
}
// Owns per-import image decoding and texture variant deduplication; no state escapes into Model APIs.
struct MaterialImport
{
    const tinygltf::Model& source;
    ModelData& result;
    GltfBudget& budget;
    std::map<int, DecodedImage> images;
    std::map<std::pair<int, bool>, std::shared_ptr<Texture>> textures;

    // Resolves one texture role, sharing decoded pixels and the source sampler independently.
    PbrTexture texture(int index, int uv, const tinygltf::ExtensionMap& extensions, bool srgb)
    {
        if (index == -1)
        {
            return {};
        }
        const auto& definition = gltfAt(source.textures, index, "texture");
        const auto& image = gltfAt(source.images, definition.source, "texture image");
        if (definition.sampler != -1)
        {
            gltfAt(source.samplers, definition.sampler, "texture sampler");
        }
        const auto key = std::pair{definition.source, srgb};
        auto found = textures.find(key);
        if (found == textures.end())
        {
            auto decoded = images.find(definition.source);
            if (decoded == images.end())
            {
                auto pixels = decodeImage(std::as_bytes(std::span(image.image)));
                decoded = images.emplace(definition.source, std::move(pixels)).first;
            }
            const auto& pixels = decoded->second;
            budget.add(pixels.pixels.size(), 1);
            auto created = Texture::create(
                {pixels.width,
                 pixels.height,
                 srgb ? TextureFormat::RGBA8UnormSrgb : TextureFormat::RGBA8Unorm,
                 TextureMipmaps::Generate},
                {pixels.pixels}
            );
            result.textures.push_back(created);
            found = textures.emplace(key, std::move(created)).first;
        }
        PbrTexture output;
        output.texture = found->second;
        output.sampler =
            definition.sampler == -1 ? result.samplers.back() : gltfAt(result.samplers, definition.sampler, "sampler");
        output.uvSet = uint32_t(uv);
        if (auto transform = extensions.find("KHR_texture_transform"); transform != extensions.end())
        {
            if (!transform->second.IsObject())
            {
                throw EngineError("Texture transform must be an object.");
            }
            auto offset = vector(transform->second, "offset", {0, 0});
            auto scale = vector(transform->second, "scale", {1, 1});
            output.offset = {offset[0], offset[1]};
            output.scale = {scale[0], scale[1]};
            output.rotation = number(transform->second, "rotation", 0);
            output.uvSet = uint32_t(integer(transform->second, "texCoord", uv));
        }
        return output;
    }
    // Resolves an extension textureInfo object using the same core texture/transform path.
    PbrTexture texture(const tinygltf::Value& extension, const char* name, bool srgb = false)
    {
        if (!extension.Has(name))
        {
            return {};
        }
        const auto& info = extension.Get(name);
        if (!info.IsObject())
        {
            throw EngineError("Texture info must be an object.");
        }
        const int index = integer(info, "index", -1);
        if (index < 0)
        {
            throw EngineError("Texture info requires an index.");
        }
        tinygltf::ExtensionMap extensions;
        if (info.Has("extensions"))
        {
            if (!info.Get("extensions").IsObject())
            {
                throw EngineError("Texture extensions must be an object.");
            }
            extensions = info.Get("extensions").Get<tinygltf::Value::Object>();
        }
        return texture(index, integer(info, "texCoord", 0), extensions, srgb);
    }
};
} // namespace

void importGltfMaterials(const tinygltf::Model& source, ModelData& result, GltfBudget& budget)
{
    budget.add(source.materials.size() + 1, sizeof(Material));
    budget.add(source.samplers.size() + 1, sizeof(Sampler));
    for (const auto& sampler : source.samplers)
    {
        SamplerDesc desc;
        desc.addressU = addressMode(sampler.wrapS);
        desc.addressV = addressMode(sampler.wrapT);
        if (sampler.magFilter != -1 && sampler.magFilter != 9728 && sampler.magFilter != 9729)
        {
            throw EngineError("Invalid magnification filter.");
        }
        desc.magFilter = sampler.magFilter == 9728 ? TextureFilter::Nearest : TextureFilter::Linear;
        switch (sampler.minFilter)
        {
        case -1:
        case 9987:
            break;
        case 9728:
            desc.minFilter = TextureFilter::Nearest;
            desc.useMipmaps = false;
            break;
        case 9729:
            desc.useMipmaps = false;
            break;
        case 9984:
            desc.minFilter = desc.mipFilter = TextureFilter::Nearest;
            break;
        case 9985:
            desc.mipFilter = TextureFilter::Nearest;
            break;
        case 9986:
            desc.minFilter = TextureFilter::Nearest;
            break;
        default:
            throw EngineError("Invalid minification filter.");
        }
        result.samplers.push_back(Sampler::create(desc));
    }
    result.samplers.push_back(Sampler::create());
    MaterialImport importer{source, result, budget, {}, {}};
    for (const auto& material : source.materials)
    {
        PbrMaterialDesc desc;
        const auto& pbr = material.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() != 4 || material.emissiveFactor.size() != 3)
        {
            throw EngineError("Invalid PBR factor width.");
        }
        desc.baseColor = {
            float(pbr.baseColorFactor[0]),
            float(pbr.baseColorFactor[1]),
            float(pbr.baseColorFactor[2]),
            float(pbr.baseColorFactor[3])
        };
        desc.metallic = float(pbr.metallicFactor);
        desc.roughness = float(pbr.roughnessFactor);
        desc.emissive =
            {float(material.emissiveFactor[0]), float(material.emissiveFactor[1]), float(material.emissiveFactor[2])};
        desc.normalScale = float(material.normalTexture.scale);
        desc.occlusionStrength = float(material.occlusionTexture.strength);
        desc.doubleSided = material.doubleSided;
        desc.alphaCutoff = float(material.alphaCutoff);
        if (material.alphaMode == "MASK")
        {
            desc.alphaMode = AlphaMode::Mask;
        }
        else if (material.alphaMode == "BLEND")
        {
            desc.alphaMode = AlphaMode::Blend;
        }
        else if (material.alphaMode != "OPAQUE")
        {
            throw EngineError("Unknown alpha mode.");
        }
        // All slots, including extension textures, use this material's independently validated transform.
        auto& slots = desc.textures;
        slots[size_t(PbrSlot::BaseColor)] = importer.texture(
            pbr.baseColorTexture.index,
            pbr.baseColorTexture.texCoord,
            pbr.baseColorTexture.extensions,
            true
        );
        slots[size_t(PbrSlot::MetallicRoughness)] = importer.texture(
            pbr.metallicRoughnessTexture.index,
            pbr.metallicRoughnessTexture.texCoord,
            pbr.metallicRoughnessTexture.extensions,
            false
        );
        slots[size_t(PbrSlot::Normal)] = importer.texture(
            material.normalTexture.index,
            material.normalTexture.texCoord,
            material.normalTexture.extensions,
            false
        );
        slots[size_t(PbrSlot::Occlusion)] = importer.texture(
            material.occlusionTexture.index,
            material.occlusionTexture.texCoord,
            material.occlusionTexture.extensions,
            false
        );
        slots[size_t(PbrSlot::Emissive)] = importer.texture(
            material.emissiveTexture.index,
            material.emissiveTexture.texCoord,
            material.emissiveTexture.extensions,
            true
        );
        for (const auto& [name, extension] : material.extensions)
        {
            if (!extension.IsObject())
            {
                throw EngineError("Material extension must be an object.");
            }
            if (name == "KHR_materials_unlit")
            {
                desc.unlit = true;
            }
            else if (name == "KHR_materials_emissive_strength")
            {
                desc.emissiveStrength = number(extension, "emissiveStrength", 1);
            }
            else if (name == "KHR_materials_ior")
            {
                desc.ior = number(extension, "ior", 1.5f);
            }
            else if (name == "KHR_materials_specular")
            {
                desc.specular = number(extension, "specularFactor", 1);
                auto color = vector(extension, "specularColorFactor", {1, 1, 1});
                desc.specularColor = {color[0], color[1], color[2]};
                slots[size_t(PbrSlot::Specular)] = importer.texture(extension, "specularTexture");
                slots[size_t(PbrSlot::SpecularColor)] = importer.texture(extension, "specularColorTexture", true);
            }
            else if (name == "KHR_materials_clearcoat")
            {
                desc.clearcoat = number(extension, "clearcoatFactor", 0);
                desc.clearcoatRoughness = number(extension, "clearcoatRoughnessFactor", 0);
                slots[size_t(PbrSlot::Clearcoat)] = importer.texture(extension, "clearcoatTexture");
                slots[size_t(PbrSlot::ClearcoatRoughness)] = importer.texture(extension, "clearcoatRoughnessTexture");
                slots[size_t(PbrSlot::ClearcoatNormal)] = importer.texture(extension, "clearcoatNormalTexture");
                if (extension.Has("clearcoatNormalTexture"))
                {
                    desc.clearcoatNormalScale = number(extension.Get("clearcoatNormalTexture"), "scale", 1);
                }
            }
            else if (name == "KHR_materials_sheen")
            {
                auto color = vector(extension, "sheenColorFactor", {0, 0, 0});
                desc.sheenColor = {color[0], color[1], color[2]};
                desc.sheenRoughness = number(extension, "sheenRoughnessFactor", 0);
                slots[size_t(PbrSlot::SheenColor)] = importer.texture(extension, "sheenColorTexture", true);
                slots[size_t(PbrSlot::SheenRoughness)] = importer.texture(extension, "sheenRoughnessTexture");
            }
            else if (name == "KHR_materials_iridescence")
            {
                desc.iridescence = number(extension, "iridescenceFactor", 0);
                desc.iridescenceIor = number(extension, "iridescenceIor", 1.3f);
                desc.thicknessMinimum = number(extension, "iridescenceThicknessMinimum", 100);
                desc.thicknessMaximum = number(extension, "iridescenceThicknessMaximum", 400);
                slots[size_t(PbrSlot::Iridescence)] = importer.texture(extension, "iridescenceTexture");
                slots[size_t(PbrSlot::IridescenceThickness)] =
                    importer.texture(extension, "iridescenceThicknessTexture");
            }
            else if (name == "KHR_materials_anisotropy")
            {
                desc.anisotropy = number(extension, "anisotropyStrength", 0);
                desc.anisotropyRotation = number(extension, "anisotropyRotation", 0);
                slots[size_t(PbrSlot::Anisotropy)] = importer.texture(extension, "anisotropyTexture");
            }
        }
        result.materials.push_back(createPbrMaterial(desc));
    }
    // Trailing defaults preserve source indices without giving unreferenced children cache identities.
    result.materials.push_back(createPbrMaterial());
    for (const auto& material : result.materials)
    {
        budget.add(material->uniforms().size(), sizeof(UniformValues::value_type));
        for (const auto& [name, value] : material->uniforms())
        {
            budget.add(name.size(), 1);
        }
        for (const auto& [name, texture] : material->textures())
        {
            budget.add(name.size() + sizeof(TextureBinding), 1);
        }
        for (const auto& [name, sampler] : material->samplers())
        {
            budget.add(name.size() + sizeof(sampler), 1);
        }
    }
}
} // namespace ofg
