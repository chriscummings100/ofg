// Portable indexed mesh diagnostic: entity/camera transform and independent material tint over a sampled color image.
struct DrawParameters
{
    column_major float4x4 clipFromLocal;
};
struct MaterialParameters
{
    float4 tint;
};
ConstantBuffer<DrawParameters> draw;
ConstantBuffer<MaterialParameters> material;
Texture2D<float4> baseColor;
SamplerState baseColorSampler;

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Maps local vertices into the selected camera's homogeneous clip space.
[shader("vertex")] VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(draw.clipFromLocal, float4(input.position, 1));
    output.uv = input.uv;
    return output;
}

    // Samples decoded linear color; material clones retain independent tints.
    [shader("fragment")] float4 fragmentMain(VertexOutput input)
    : SV_Target
{
    return baseColor.Sample(baseColorSampler, input.uv) * material.tint;
}
