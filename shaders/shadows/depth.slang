// Shadow depth path with the same PBR base-alpha, transformed UV and vertex-alpha contract as visible geometry.
struct ShadowDrawParameters
{
    column_major float4x4 clipFromLocal;
    float4 transformX;
    float4 transformY;
    float4 alpha;
};
ConstantBuffer<ShadowDrawParameters> shadowDraw;
#ifdef SHADOW_ALPHA_TEXTURE
Texture2D<float4> alphaTexture;
SamplerState alphaSampler;
#endif
struct ShadowVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL1;
    float2 uv : TEXCOORD2;
    float4 tangent : TANGENT3;
    float2 uv1 : TEXCOORD4;
    float4 color : COLOR5;
};
struct ShadowVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float alpha : COLOR0;
};
// Keeps alpha inputs and transforms identical to the material pass.
[shader("vertex")] ShadowVertex vertexMain(ShadowVertexInput input)
{
    ShadowVertex v;
    v.position = mul(shadowDraw.clipFromLocal, float4(input.position, 1));
    float3 uv = float3(shadowDraw.alpha.z > 0 ? input.uv1 : input.uv, 1);
    v.uv = float2(dot(uv, shadowDraw.transformX.xyz), dot(uv, shadowDraw.transformY.xyz));
    v.alpha = input.color.a * shadowDraw.alpha.x;
    return v;
}
    // Late alpha testing preserves derivative-valid implicit texture sampling.
    [shader("fragment")] void fragmentMain(ShadowVertex input)
{
    float alpha = input.alpha;
#ifdef SHADOW_ALPHA_TEXTURE
    alpha *= alphaTexture.Sample(alphaSampler, input.uv).a;
#endif
    if (shadowDraw.alpha.w > 0 && alpha < shadowDraw.alpha.y)
    {
        discard;
    }
}
