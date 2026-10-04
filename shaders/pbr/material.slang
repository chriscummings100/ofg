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
#ifdef SLOT0
    float3 slot0TransformX;
    float3 slot0TransformY;
    uint slot0UvSet;
#endif
#ifdef SLOT1
    float3 slot1TransformX;
    float3 slot1TransformY;
    uint slot1UvSet;
#endif
#ifdef SLOT2
    float3 slot2TransformX;
    float3 slot2TransformY;
    uint slot2UvSet;
#endif
#ifdef SLOT3
    float3 slot3TransformX;
    float3 slot3TransformY;
    uint slot3UvSet;
#endif
#ifdef SLOT4
    float3 slot4TransformX;
    float3 slot4TransformY;
    uint slot4UvSet;
#endif
#ifdef SLOT5
    float3 slot5TransformX;
    float3 slot5TransformY;
    uint slot5UvSet;
#endif
#ifdef SLOT6
    float3 slot6TransformX;
    float3 slot6TransformY;
    uint slot6UvSet;
#endif
#ifdef SLOT7
    float3 slot7TransformX;
    float3 slot7TransformY;
    uint slot7UvSet;
#endif
#ifdef SLOT8
    float3 slot8TransformX;
    float3 slot8TransformY;
    uint slot8UvSet;
#endif
#ifdef SLOT9
    float3 slot9TransformX;
    float3 slot9TransformY;
    uint slot9UvSet;
#endif
#ifdef SLOT10
    float3 slot10TransformX;
    float3 slot10TransformY;
    uint slot10UvSet;
#endif
#ifdef SLOT11
    float3 slot11TransformX;
    float3 slot11TransformY;
    uint slot11UvSet;
#endif
#ifdef SLOT12
    float3 slot12TransformX;
    float3 slot12TransformY;
    uint slot12UvSet;
#endif
#ifdef SLOT13
    float3 slot13TransformX;
    float3 slot13TransformY;
    uint slot13UvSet;
#endif
#ifdef SLOT14
    float3 slot14TransformX;
    float3 slot14TransformY;
    uint slot14UvSet;
#endif
};
ConstantBuffer<MaterialParameters> material;
#ifdef SLOT0
Texture2D<float4> slot0Texture;
SamplerState slot0Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv0(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot0UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot0TransformX, uv), dot(material.slot0TransformY, uv));
}
#endif
#ifdef SLOT1
Texture2D<float4> slot1Texture;
SamplerState slot1Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv1(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot1UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot1TransformX, uv), dot(material.slot1TransformY, uv));
}
#endif
#ifdef SLOT2
Texture2D<float4> slot2Texture;
SamplerState slot2Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv2(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot2UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot2TransformX, uv), dot(material.slot2TransformY, uv));
}
#endif
#ifdef SLOT3
Texture2D<float4> slot3Texture;
SamplerState slot3Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv3(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot3UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot3TransformX, uv), dot(material.slot3TransformY, uv));
}
#endif
#ifdef SLOT4
Texture2D<float4> slot4Texture;
SamplerState slot4Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv4(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot4UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot4TransformX, uv), dot(material.slot4TransformY, uv));
}
#endif
#ifdef SLOT5
Texture2D<float4> slot5Texture;
SamplerState slot5Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv5(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot5UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot5TransformX, uv), dot(material.slot5TransformY, uv));
}
#endif
#ifdef SLOT6
Texture2D<float4> slot6Texture;
SamplerState slot6Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv6(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot6UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot6TransformX, uv), dot(material.slot6TransformY, uv));
}
#endif
#ifdef SLOT7
Texture2D<float4> slot7Texture;
SamplerState slot7Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv7(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot7UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot7TransformX, uv), dot(material.slot7TransformY, uv));
}
#endif
#ifdef SLOT8
Texture2D<float4> slot8Texture;
SamplerState slot8Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv8(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot8UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot8TransformX, uv), dot(material.slot8TransformY, uv));
}
#endif
#ifdef SLOT9
Texture2D<float4> slot9Texture;
SamplerState slot9Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv9(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot9UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot9TransformX, uv), dot(material.slot9TransformY, uv));
}
#endif
#ifdef SLOT10
Texture2D<float4> slot10Texture;
SamplerState slot10Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv10(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot10UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot10TransformX, uv), dot(material.slot10TransformY, uv));
}
#endif
#ifdef SLOT11
Texture2D<float4> slot11Texture;
SamplerState slot11Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv11(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot11UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot11TransformX, uv), dot(material.slot11TransformY, uv));
}
#endif
#ifdef SLOT12
Texture2D<float4> slot12Texture;
SamplerState slot12Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv12(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot12UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot12TransformX, uv), dot(material.slot12TransformY, uv));
}
#endif
#ifdef SLOT13
Texture2D<float4> slot13Texture;
SamplerState slot13Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv13(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot13UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot13TransformX, uv), dot(material.slot13TransformY, uv));
}
#endif
#ifdef SLOT14
Texture2D<float4> slot14Texture;
SamplerState slot14Sampler;
// Selects and transforms this slot's coordinates independently of other uses of the same image.
float2 uv14(float2 uv0, float2 uv1)
{
    float3 uv = float3(material.slot14UvSet == 0 ? uv0 : uv1, 1);
    return float2(dot(material.slot14TransformX, uv), dot(material.slot14TransformY, uv));
}
#endif
