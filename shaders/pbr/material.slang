// Material ABI and per-slot UV transforms; only present textures consume binding slots.
struct MaterialParameters
{
    float4 baseColor;
    float metallic;
    float roughness;
    float3 emissive;
    float emissiveStrength;
    float normalScale;
    float occlusionStrength;
    float ior;
    float specular;
    float3 specularColor;
    float clearcoat;
    float clearcoatRoughness;
    float clearcoatNormalScale;
    float3 sheenColor;
    float sheenRoughness;
    float iridescence;
    float iridescenceIor;
    float thicknessMinimum;
    float thicknessMaximum;
    float anisotropy;
    float anisotropyRotation;
    float alphaCutoff;
#ifdef HAS_TERRAIN_TEXTURE
    Texture2D<float4> terrainTexture;
    Texture2D<float> terrainValidity;
    SamplerState terrainSampler;
    float4 terrainMapping;
    float terrainSpacing;
    uint terrainChannel;
#endif
#ifdef HAS_BASE_COLOR_TEXTURE
    float3 baseColorTransformX;
    float3 baseColorTransformY;
    uint baseColorUvSet;
#endif
#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
    float3 metallicRoughnessTransformX;
    float3 metallicRoughnessTransformY;
    uint metallicRoughnessUvSet;
#endif
#ifdef HAS_NORMAL_TEXTURE
    float3 normalTransformX;
    float3 normalTransformY;
    uint normalUvSet;
#endif
#ifdef HAS_OCCLUSION_TEXTURE
    float3 occlusionTransformX;
    float3 occlusionTransformY;
    uint occlusionUvSet;
#endif
#ifdef HAS_EMISSIVE_TEXTURE
    float3 emissiveTransformX;
    float3 emissiveTransformY;
    uint emissiveUvSet;
#endif
#ifdef HAS_SPECULAR_TEXTURE
    float3 specularTransformX;
    float3 specularTransformY;
    uint specularUvSet;
#endif
#ifdef HAS_SPECULAR_COLOR_TEXTURE
    float3 specularColorTransformX;
    float3 specularColorTransformY;
    uint specularColorUvSet;
#endif
#ifdef HAS_CLEARCOAT_TEXTURE
    float3 clearcoatTransformX;
    float3 clearcoatTransformY;
    uint clearcoatUvSet;
#endif
#ifdef HAS_CLEARCOAT_ROUGHNESS_TEXTURE
    float3 clearcoatRoughnessTransformX;
    float3 clearcoatRoughnessTransformY;
    uint clearcoatRoughnessUvSet;
#endif
#ifdef HAS_CLEARCOAT_NORMAL_TEXTURE
    float3 clearcoatNormalTransformX;
    float3 clearcoatNormalTransformY;
    uint clearcoatNormalUvSet;
#endif
#ifdef HAS_SHEEN_COLOR_TEXTURE
    float3 sheenColorTransformX;
    float3 sheenColorTransformY;
    uint sheenColorUvSet;
#endif
#ifdef HAS_SHEEN_ROUGHNESS_TEXTURE
    float3 sheenRoughnessTransformX;
    float3 sheenRoughnessTransformY;
    uint sheenRoughnessUvSet;
#endif
#ifdef HAS_IRIDESCENCE_TEXTURE
    float3 iridescenceTransformX;
    float3 iridescenceTransformY;
    uint iridescenceUvSet;
#endif
#ifdef HAS_IRIDESCENCE_THICKNESS_TEXTURE
    float3 iridescenceThicknessTransformX;
    float3 iridescenceThicknessTransformY;
    uint iridescenceThicknessUvSet;
#endif
#ifdef HAS_ANISOTROPY_TEXTURE
    float3 anisotropyTransformX;
    float3 anisotropyTransformY;
    uint anisotropyUvSet;
#endif
#ifdef HAS_BASE_COLOR_TEXTURE
    Texture2D<float4> baseColorTexture;
    SamplerState baseColorSampler;
#endif
#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
    Texture2D<float4> metallicRoughnessTexture;
    SamplerState metallicRoughnessSampler;
#endif
#ifdef HAS_NORMAL_TEXTURE
    Texture2D<float4> normalTexture;
    SamplerState normalSampler;
#endif
#ifdef HAS_OCCLUSION_TEXTURE
    Texture2D<float4> occlusionTexture;
    SamplerState occlusionSampler;
#endif
#ifdef HAS_EMISSIVE_TEXTURE
    Texture2D<float4> emissiveTexture;
    SamplerState emissiveSampler;
#endif
#ifdef HAS_SPECULAR_TEXTURE
    Texture2D<float4> specularTexture;
    SamplerState specularSampler;
#endif
#ifdef HAS_SPECULAR_COLOR_TEXTURE
    Texture2D<float4> specularColorTexture;
    SamplerState specularColorSampler;
#endif
#ifdef HAS_CLEARCOAT_TEXTURE
    Texture2D<float4> clearcoatTexture;
    SamplerState clearcoatSampler;
#endif
#ifdef HAS_CLEARCOAT_ROUGHNESS_TEXTURE
    Texture2D<float4> clearcoatRoughnessTexture;
    SamplerState clearcoatRoughnessSampler;
#endif
#ifdef HAS_CLEARCOAT_NORMAL_TEXTURE
    Texture2D<float4> clearcoatNormalTexture;
    SamplerState clearcoatNormalSampler;
#endif
#ifdef HAS_SHEEN_COLOR_TEXTURE
    Texture2D<float4> sheenColorTexture;
    SamplerState sheenColorSampler;
#endif
#ifdef HAS_SHEEN_ROUGHNESS_TEXTURE
    Texture2D<float4> sheenRoughnessTexture;
    SamplerState sheenRoughnessSampler;
#endif
#ifdef HAS_IRIDESCENCE_TEXTURE
    Texture2D<float4> iridescenceTexture;
    SamplerState iridescenceSampler;
#endif
#ifdef HAS_IRIDESCENCE_THICKNESS_TEXTURE
    Texture2D<float4> iridescenceThicknessTexture;
    SamplerState iridescenceThicknessSampler;
#endif
#ifdef HAS_ANISOTROPY_TEXTURE
    Texture2D<float4> anisotropyTexture;
    SamplerState anisotropySampler;
#endif
};
ParameterBlock<MaterialParameters> material;
#ifdef HAS_BASE_COLOR_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 baseColorUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.baseColorUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.baseColorTransformX, uv), dot(material.baseColorTransformY, uv));
}
#endif
#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 metallicRoughnessUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.metallicRoughnessUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.metallicRoughnessTransformX, uv), dot(material.metallicRoughnessTransformY, uv));
}
#endif
#ifdef HAS_NORMAL_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 normalUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.normalUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.normalTransformX, uv), dot(material.normalTransformY, uv));
}
#endif
#ifdef HAS_OCCLUSION_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 occlusionUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.occlusionUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.occlusionTransformX, uv), dot(material.occlusionTransformY, uv));
}
#endif
#ifdef HAS_EMISSIVE_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 emissiveUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.emissiveUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.emissiveTransformX, uv), dot(material.emissiveTransformY, uv));
}
#endif
#ifdef HAS_SPECULAR_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 specularUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.specularUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.specularTransformX, uv), dot(material.specularTransformY, uv));
}
#endif
#ifdef HAS_SPECULAR_COLOR_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 specularColorUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.specularColorUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.specularColorTransformX, uv), dot(material.specularColorTransformY, uv));
}
#endif
#ifdef HAS_CLEARCOAT_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 clearcoatUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.clearcoatUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.clearcoatTransformX, uv), dot(material.clearcoatTransformY, uv));
}
#endif
#ifdef HAS_CLEARCOAT_ROUGHNESS_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 clearcoatRoughnessUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.clearcoatRoughnessUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.clearcoatRoughnessTransformX, uv), dot(material.clearcoatRoughnessTransformY, uv));
}
#endif
#ifdef HAS_CLEARCOAT_NORMAL_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 clearcoatNormalUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.clearcoatNormalUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.clearcoatNormalTransformX, uv), dot(material.clearcoatNormalTransformY, uv));
}
#endif
#ifdef HAS_SHEEN_COLOR_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 sheenColorUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.sheenColorUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.sheenColorTransformX, uv), dot(material.sheenColorTransformY, uv));
}
#endif
#ifdef HAS_SHEEN_ROUGHNESS_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 sheenRoughnessUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.sheenRoughnessUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.sheenRoughnessTransformX, uv), dot(material.sheenRoughnessTransformY, uv));
}
#endif
#ifdef HAS_IRIDESCENCE_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 iridescenceUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.iridescenceUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.iridescenceTransformX, uv), dot(material.iridescenceTransformY, uv));
}
#endif
#ifdef HAS_IRIDESCENCE_THICKNESS_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 iridescenceThicknessUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.iridescenceThicknessUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.iridescenceThicknessTransformX, uv), dot(material.iridescenceThicknessTransformY, uv));
}
#endif
#ifdef HAS_ANISOTROPY_TEXTURE
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 anisotropyUv(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.anisotropyUvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.anisotropyTransformX, uv), dot(material.anisotropyTransformY, uv));
}
#endif
