// Typed construction of glTF surface materials; the returned Material's bindings are the editable state.
#pragma once
#include "resources/material.h"
#include <array>

namespace ofg {
enum class PbrSlot
{
    BaseColor,
    MetallicRoughness,
    Normal,
    Occlusion,
    Emissive,
    Specular,
    SpecularColor,
    Clearcoat,
    ClearcoatRoughness,
    ClearcoatNormal,
    SheenColor,
    SheenRoughness,
    Iridescence,
    IridescenceThickness,
    Anisotropy,
    Count
};
struct PbrTexture
{
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Sampler> sampler;
    math::Vec2 offset{};
    math::Vec2 scale{1, 1};
    float rotation = 0;
    uint32_t uvSet = 0;
};
struct PbrMaterialDesc
{
    math::Vec4 baseColor{1, 1, 1, 1};
    float metallic = 1, roughness = 1;
    math::Vec3 emissive{};
    float emissiveStrength = 1, normalScale = 1, occlusionStrength = 1;
    float ior = 1.5f, specular = 1;
    math::Vec3 specularColor{1, 1, 1};
    float clearcoat = 0, clearcoatRoughness = 0, clearcoatNormalScale = 1;
    math::Vec3 sheenColor{};
    float sheenRoughness = 0;
    float iridescence = 0, iridescenceIor = 1.3f, thicknessMinimum = 100, thicknessMaximum = 400;
    float anisotropy = 0, anisotropyRotation = 0;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false, unlit = false;
    std::array<PbrTexture, static_cast<size_t>(PbrSlot::Count)> textures{};
};
// Validates factors and the portable 16-texture budget (four IBL textures), then shares a source variant.
// Recreate for texture-presence/unlit changes; factor edits use the returned material's named uniforms.
std::shared_ptr<Material> createPbrMaterial(const PbrMaterialDesc& desc = {});
} // namespace ofg
